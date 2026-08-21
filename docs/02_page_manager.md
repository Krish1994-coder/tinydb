# Page Manager

## 1. Purpose

The Page Manager defines and implements the internal layout of a TinyDB data
page and provides algorithms for inserting, reading, deleting, and updating
variable-length records within a fixed-size 4 KiB page.

It operates purely on in-memory byte buffers supplied by the Buffer Pool. It
performs no disk I/O and owns no page memory.

---

## 2. Responsibilities

- Define the physical layout of a 4 KiB data page (header, slot directory,
  free space, record data).
- Initialise a fresh page with a valid empty state.
- Load and validate an existing page read from disk by the Buffer Pool.
- Insert a variable-length record and return a stable RecordID.
- Retrieve a record given a RecordID (returns a non-owning ByteView).
- Delete a record (mark slot as tombstone; bytes not immediately reclaimed).
- Update a record in-place (same or smaller size) or by relocation within the
  page (larger size), preserving the RecordID in both cases.
- Compact a page to reclaim fragmented space from deleted and shrunk records.
- Validate all page invariants and detect corruption.
- Report free space, slot count, and free-slot count.

---

## 3. Non-Responsibilities

PageManager does NOT:

- Call `ReadPage()` or `WritePage()` — that is StorageManager's job.
- Open, close, or interact with the database file — StorageManager.
- Allocate or free pages — StorageManager / Buffer Pool.
- Manage a page cache or buffer pool — Buffer Pool.
- Own or allocate page frame memory — Buffer Pool.
- Know about tables, schemas, column types, SQL, or indexes — Catalog layer.
- Perform `fsync()` — StorageManager.
- Manage overflow pages for records larger than a single page — deferred.

---

## 4. Architectural Position

PageManager is not a layer in the storage I/O chain. It is a page-format and
record-management component that operates on memory owned by the Buffer Pool.

```
Executor / Table Heap
        │
        ▼
   Buffer Pool
        │
        ├────────────────────────────────┐
        │                               │
        ▼                               ▼
   Page memory                    StorageManager
        │                               │
        ▼                               ▼
   PageManager                    database.tdb
        │
        │  interprets / modifies
        ▼
   Page Header / Slots / Records
```

PageManager receives page memory from the Buffer Pool. StorageManager reads
and writes that same memory to disk. These are independent paths. PageManager
never calls StorageManager.

---

## 5. Page Format

### 5.1 Layout Overview

```
Byte 0
│
▼
┌─────────────────────────────────────────────┐  ← offset 0
│                 Page Header                 │    28 bytes
├─────────────────────────────────────────────┤  ← offset 28
│             Slot Directory                  │
│   Slot 0  (6 bytes)                        │  ← offset 28
│   Slot 1  (6 bytes)                        │  ← offset 34
│   Slot 2  (6 bytes)                        │  ← offset 40
│   ...                                       │
│   Slot N-1                                  │
├─────────────────────────────────────────────┤  ← free_space_start
│                                             │
│               Free Space                   │
│       (contiguous gap, available now)       │
│                                             │
├─────────────────────────────────────────────┤  ← free_space_end
│               Record Data                  │
│   (allocated from the end of the page      │
│    toward lower byte offsets)               │
│   Record N-1                                │
│   ...                                       │
│   Record 1                                  │
│   Record 0                                  │
└─────────────────────────────────────────────┘
Byte 4096
```

### 5.2 Growth Directions

- Slot directory grows toward **higher** byte offsets.
- Record data grows toward **lower** byte offsets.
- Free space is the contiguous gap between `free_space_start` and
  `free_space_end`.

### 5.3 Free Space Region

```
Contiguous free space = free_space_end − free_space_start
```

This is the space immediately available for a new insertion without
compaction. Fragmented space (from deletions and shrunk records) is not
included and requires `Compact()` to reclaim.

**Empty page:**

```
free_space_start = 28       (kHeaderSize)
free_space_end   = 4096     (kPageSize)
free_space       = 4068 bytes
```

**After 3 insertions (sizes 100, 200, 150):**

```
free_space_start = 46       (28 + 3 × 6)
free_space_end   = 3646     (4096 − 450)
free_space       = 3600 bytes
```

---

## 6. Page Header

Total size: **28 bytes**. Located at byte offset 0 of every data page.

| Offset | Size | Field             | Type     | Purpose                                               |
|--------|------|-------------------|----------|-------------------------------------------------------|
| 0      | 4    | page_id           | uint32_t | Logical page ID. Detects wrong-page reads.            |
| 4      | 2    | page_type         | uint16_t | Page kind. See PageType enum.                         |
| 6      | 2    | slot_count        | uint16_t | Total slots allocated (including tombstones).         |
| 8      | 2    | free_slot_count   | uint16_t | Tombstone slots available for reuse.                  |
| 10     | 2    | free_space_start  | uint16_t | First free byte after slot directory.                 |
| 12     | 2    | free_space_end    | uint16_t | First byte of lowest live record.                     |
| 14     | 2    | flags             | uint16_t | Reserved bit flags. Must be 0.                        |
| 16     | 4    | reserved_lsn_lo   | uint32_t | Reserved for WAL LSN (lower 32 bits). Must be 0.     |
| 20     | 4    | reserved_lsn_hi   | uint32_t | Reserved for WAL LSN (upper 32 bits). Must be 0.     |
| 24     | 4    | checksum          | uint32_t | Reserved. Must be 0 in Phase 2/3.                    |

**Invariant:** `free_space_start == kHeaderSize + slot_count × kSlotSize`

**Note — LSN fields:** The 64-bit Log Sequence Number is represented by two
`uint32_t` fields: `reserved_lsn_lo` (lower 32 bits) and `reserved_lsn_hi`
(upper 32 bits). Using two `uint32_t` fields instead of a single `uint64_t`
avoids introducing stricter alignment requirements for the LSN field and keeps
the intended serialised header layout at 28 bytes on the supported platforms.
The `sizeof()` and `offsetof()` checks in `page.h` enforce the expected layout
at compile time. Both fields must remain 0 until WAL is implemented in Phase 10.
When WAL is implemented, the 64-bit LSN value is reconstructed as:

```cpp
uint64_t lsn =
    (static_cast<uint64_t>(reserved_lsn_hi) << 32) |
    static_cast<uint64_t>(reserved_lsn_lo);
```

**Note — checksum:** Reserved at offset 24. Coverage and algorithm (CRC32,
xxHash, etc.) will be defined when WAL/recovery is designed in Phase 10.

**Note — endianness:** Currently uses native host endianness. Explicit
endianness rules will be defined before the on-disk format is stable.

---

## 7. Page Types

Only `kData` has a defined layout in Phase 2. All others are reserved.

| Value | Name           | Status                    |
|-------|----------------|---------------------------|
| 0     | kInvalid       | Not a valid page          |
| 1     | kData          | Heap data page (Phase 2)  |
| 2     | kBPlusInternal | Reserved (Phase 6)        |
| 3     | kBPlusLeaf     | Reserved (Phase 6)        |
| 4     | kFreeSpace     | Reserved (Phase 5+)       |
| 5     | kCatalog       | Reserved (Phase 7+)       |

---

## 8. File Header vs Data Page Header

| Aspect   | File Header (Page 0)             | Data Page Header             |
|----------|----------------------------------|------------------------------|
| Owner    | StorageManager                   | PageManager                  |
| Contains | magic, version, page_size, count | layout fields (Section 6)    |
| Access   | StorageManager reads/writes only | PageManager reads/writes only|

PageManager never reads or writes Page 0. StorageManager is responsible for
raw page I/O and does not interpret the PageManager-specific PageHeader,
slot-directory, or record-layout fields.

---

## 9. Slot Directory

### 9.1 Slot Structure

Total size: **6 bytes**. Located at `kHeaderSize + slot_id × kSlotSize`.

| Offset | Size | Field    | Type     | Purpose                               |
|--------|------|----------|----------|---------------------------------------|
| 0      | 2    | offset   | uint16_t | Byte offset of record. 0 = tombstone. |
| 2      | 2    | length   | uint16_t | Byte length of record. 0 = tombstone. |
| 4      | 2    | reserved | uint16_t | Must be 0. Future: overflow, MVCC.    |

### 9.2 Tombstone Encoding

A slot is a **tombstone** (logically deleted) when `offset == 0 && length == 0`.

Offset 0 is always the page header; no valid record can start there.
This makes the tombstone sentinel unambiguous without a flag bit.

### 9.3 Slot Reuse on Insert

On every `InsertRecord`, the slot directory is scanned for the first tombstone.
If found, that slot is reused (slot_id recycled, `free_slot_count` decremented).
If not found, a new slot is appended at `free_space_start`.

---

## 10. RecordID

```cpp
struct RecordID {
    page_id_t page_id;   // which page
    SlotID    slot_id;   // which slot in that page's directory
};
```

Total: **6 bytes** (`uint32_t` + `uint16_t`).

`kInvalidRecordID = { kInvalidPageId, kInvalidSlotID }`

RecordID is a logical identifier. Its physical record offset is stored only in
the slot directory and may change during update relocation or compaction; the
RecordID itself does not change.

### Stability Guarantee

RecordIDs are stable across:

- In-place updates (same or smaller size).
- Relocation updates (larger size) — `slot_id` is preserved; only the slot's
  internal `offset` field changes.
- Page compaction — `slot_id` is preserved; `offset` fields are updated.

RecordIDs become invalid only when a record is deleted or a page is dropped.

---

## 11. Free Space Management

### 11.1 free_space_start and free_space_end

```
free_space_start  — first byte AFTER the slot directory
free_space_end    — first byte OF the record-data region
```

`Contiguous free space = free_space_end − free_space_start`

### 11.2 Contiguous vs Fragmented Space

`GetFreeSpace()` reports **contiguous** free space only.

Fragmented space arises from:

1. Deleted records (tombstone slots; bytes not reclaimed until `Compact()`).
2. Shrunk records (in-place update shorter than old record; unused suffix bytes).
3. Relocated records (larger update; old location abandoned).

### 11.3 Effect of Each Operation on Free Space

| Operation                       | free_space_start | free_space_end     | Notes                         |
|---------------------------------|------------------|--------------------|-------------------------------|
| Insert (new slot)               | + kSlotSize      | − record_size      | Both pointers move.           |
| Insert (slot reused)            | unchanged        | − record_size      | Only end pointer moves.       |
| Delete                          | unchanged        | unchanged          | Bytes become fragmented.      |
| Update in-place (0 < new ≤ old) | unchanged        | unchanged          | Suffix bytes = fragmentation. |
| Update relocation (new > old)   | unchanged        | − new_record_size  | Old location = fragmentation. |
| Compact                         | unchanged        | increases          | Fragmented gaps reclaimed.    |

---

## 12. Record Storage

### 12.1 Physical Record Format

PageManager stores records as **opaque byte sequences**. It does not interpret
record contents. Records have no embedded header at the physical storage layer.

Logical structure (column types, NULL flags, variable-length field encoding)
is the responsibility of the **Catalog / Tuple layer**, not PageManager.

### 12.2 Variable-Length Records

Records must be at least 1 byte in length. Zero-length records are invalid.

The maximum physical record size for an insertion requiring a new slot is:

```
kPageSize - kHeaderSize - kSlotSize   (= 4062 bytes)
```

For an insertion that reuses an existing deleted slot:

```
kPageSize - kHeaderSize               (= 4068 bytes)
```

The actual insertion limit is determined by the current contiguous free space
and slot-reuse state. The slot's `length` field records the exact byte count.

### 12.3 Alignment

Record placement is not currently aligned beyond what the `free_space_end`
pointer naturally provides. Alignment requirements for specific field types
are a Catalog-layer concern.

---

## 13. Operations

### 13.1 Init

Returns `kInvalidArgument` if `page_id` is 0 or `kInvalidPageId` (consistent
with I-01: `header.page_id >= 1`).

On success, zero the header and set:

```
page_id          = page_id (argument)
page_type        = kData
slot_count       = 0
free_slot_count  = 0
free_space_start = kHeaderSize   (= 28)
free_space_end   = kPageSize     (= 4096)
flags            = 0
reserved_lsn_lo  = 0
reserved_lsn_hi  = 0
checksum         = 0
```

### 13.2 InitFromExistingPage

Call `ValidatePage()`. Return `kCorruption` on any invariant violation.

### 13.3 InsertRecord

**Precondition:** `[data, data + size)` must not overlap `[page, page + kPageSize)`.

1. Validate `size > 0`. Return `kInvalidArgument` if not.
2. Scan slots for the first tombstone to determine whether the insertion
   will reuse an existing slot.
3. Validate the maximum record size:
   - Reused slot: `size <= kPageSize - kHeaderSize` (= 4068).
   - New slot:    `size <= kPageSize - kHeaderSize - kSlotSize` (= 4062).
   Return `kInvalidArgument` if the size exceeds the applicable maximum.
4. Compute `needed = size + (reuse_slot ? 0 : kSlotSize)`.
5. If `needed > GetFreeSpace()`, return `kPageFull`.
6. `free_space_end -= size`; write record bytes at `free_space_end`.
7. Write or update the slot entry (`offset = free_space_end`, `length = size`).
8. Update header fields.
9. Return `RecordID{page_id, slot_id}`.

### 13.4 GetRecord

1. Validate `rid.page_id` matches `header.page_id`. Return `kInvalidRecordID`
   if not.
2. Validate `rid.slot_id < header.slot_count`. Return `kInvalidRecordID` if not.
3. Load slot fields using serialisation helpers (not `reinterpret_cast`).
4. If tombstone (`offset == 0 && length == 0`), return `kRecordNotFound`.
5. Validate `slot.reserved == 0`. Return `kCorruption` if not.
6. Validate `slot.offset >= kHeaderSize` and
   `slot.offset + slot.length <= kPageSize` (overflow-safe). Return
   `kCorruption` if not.
7. Return `ByteView{page + slot.offset, slot.length}`.

No copy is made. The caller must keep the page pinned. See Section 16 for
`ByteView` lifetime rules.

### 13.5 DeleteRecord

NOT idempotent. Deleting an already-deleted record returns `kRecordNotFound`.

1. Validate `rid.page_id` matches `header.page_id`; otherwise return
   `kInvalidRecordID`.
2. Validate `rid.slot_id < header.slot_count`; otherwise return
   `kInvalidRecordID`.
3. Load the slot using the serialisation helper.
4. If tombstone (`offset == 0 && length == 0`), return `kRecordNotFound`.
5. Validate `slot.reserved == 0`; otherwise return `kCorruption`.
6. Validate `slot.offset >= kHeaderSize` and
   `slot.offset + slot.length <= kPageSize`, using overflow-safe arithmetic.
   Return `kCorruption` if invalid.
7. Write tombstone: `slot.offset = 0`, `slot.length = 0`.
8. Increment `header.free_slot_count`.

Record bytes are not zeroed or reclaimed. `GetFreeSpace()` does not increase.
The record is logically deleted; any previously obtained `ByteView` must not
be used to infer live record contents.

### 13.6 UpdateRecord

**Precondition:** `[new_data, new_data + new_size)` must not overlap
`[page, page + kPageSize)`.

1. Validate `rid.page_id` matches `header.page_id`; otherwise return
   `kInvalidRecordID`.
2. Validate `rid.slot_id < header.slot_count`; otherwise return
   `kInvalidRecordID`.
3. Load the slot using the serialisation helper.
4. If tombstone (`offset == 0 && length == 0`), return `kRecordNotFound`.
5. Validate `slot.reserved == 0`; otherwise return `kCorruption`.
6. Validate `slot.offset >= kHeaderSize` and
   `slot.offset + slot.length <= kPageSize`, using overflow-safe arithmetic.
   Return `kCorruption` if invalid.
7. Validate `new_size != 0`; otherwise return `kInvalidArgument`.
8. Validate `new_size <= kPageSize - kHeaderSize` (= 4068); otherwise return
   `kInvalidArgument`. UpdateRecord reuses the existing slot, so the upper
   limit is `kPageSize - kHeaderSize`. Valid range:
   `1 <= new_size <= kPageSize - kHeaderSize`.

**Case A — `0 < new_size <= old_size` (in-place):**

9. Write `new_data` at `slot.offset`.
10. Update `slot.length = new_size`.
11. Suffix bytes (`old_size - new_size`) become fragmentation.
12. Return same `rid`. RecordID and physical address unchanged.

**Case B — `new_size > old_size` (relocation):**

9. If `new_size > GetFreeSpace()`: call `Compact(page, scratch)`, retry.
10. If still insufficient after compaction: return `kPageFull`.
11. `free_space_end -= new_size`; write `new_data` at `free_space_end`.
12. Update `slot.offset = free_space_end`, `slot.length = new_size`.
13. Old record bytes at the previous offset become fragmentation.
14. Return same `rid`. `slot_id` is preserved; RecordID unchanged, physical
    address changed.

### 13.7 Compact

**Preconditions:**

- `page` points to a valid `kPageSize` page buffer.
- `scratch` points to a separate `kPageSize` buffer.
- `page` and `scratch` must not overlap.

**Algorithm:**

1. Validate `page`. Return `kCorruption` if invalid.
2. Copy the complete page image from `page` to `scratch`.
3. Read `slot_count` from the original `page`.
4. Set `scratch.free_space_end = kPageSize`.
5. Iterate through all slots in slot-id order:
   - **Tombstone:** preserve the tombstone slot unchanged in `scratch`.
   - **Live slot:** read the original record `offset` and `length` from `page`.
     Decrease `scratch.free_space_end` by `length`. Copy the record bytes from
     the **original `page` buffer** into `scratch` at the new location. Update
     that slot's `offset` in `scratch`.
6. Preserve `slot_count`, `free_slot_count`, and all header fields not changed
   by compaction.
7. Write the final `scratch.free_space_end`.
8. Validate `scratch`. If validation fails, return `kCorruption` without
   modifying the original `page`. The caller's page remains intact.
9. Copy the complete `scratch` page back to `page`.

**Important:** Every record copy reads from the original `page` buffer. The
`scratch` buffer is the only destination. Do not read from `scratch`'s record
area as a source for subsequent copies.

Compaction repacks live record data toward the end of the page (higher byte
offsets), starting from `kPageSize` and moving `free_space_end` toward lower
byte offsets with each record placed. This matches the allocation model:
`free_space_end -= record_size`.

Slot IDs and RecordIDs are preserved. All `ByteView` instances referring to
record data in `page` are **invalid** after `Compact()` returns.

---

## 14. Page Invariants

```
I-01  header.page_id >= 1

I-02  header.page_type == static_cast<uint16_t>(PageType::kData)
      PageManager currently implements only the kData page layout.
      Other page types are reserved for future phases and are not
      valid PageManager data pages.

I-03  header.slot_count * kSlotSize + kHeaderSize <= kPageSize

I-04  header.free_slot_count <= header.slot_count

I-05  header.free_space_start == kHeaderSize + header.slot_count * kSlotSize

I-06  header.free_space_end >= header.free_space_start

I-07  header.free_space_end <= kPageSize

I-08  header.checksum == 0   (Phase 2/3)

I-09  header.reserved_lsn_lo == 0 &&
      header.reserved_lsn_hi == 0   (until WAL)

I-10  header.flags == 0

For every live (non-tombstone) slot[i]:
I-11  slot[i].offset >= header.free_space_end;
      every live record lies in the record-data region.

I-12  slot[i].offset >= kHeaderSize &&
      slot[i].offset + slot[i].length <= kPageSize
      (range calculation must be overflow-safe)

I-13  slot[i].length >= 1

I-14  slot[i].reserved == 0

For every tombstone slot[i]:
I-15  slot[i].offset == 0 && slot[i].length == 0

I-16  No two non-tombstone slots have overlapping byte ranges.

I-17  Count of tombstone slots == header.free_slot_count.

I-18  Slot directory [kHeaderSize, free_space_start) does not overlap
      record data [free_space_end, kPageSize).
      This follows from I-05 and I-06.
```

`ValidatePage()` must not mutate the page.

---

## 15. Corruption Detection

| What to detect                                      | Owner                            |
|-----------------------------------------------------|----------------------------------|
| Short read / truncated file                         | StorageManager                   |
| Wrong byte count on read                            | StorageManager                   |
| Invalid page ID (zero or reserved invalid ID)       | PageManager (I-01)               |
| Page ID does not match caller's expected page ID    | PageManager caller/API           |
| Unsupported or invalid page type                    | PageManager (I-02)               |
| slot_count out of range                             | PageManager (I-03)               |
| free_space_start inconsistent                       | PageManager (I-05)               |
| Free-space pointers inverted                        | PageManager (I-06)               |
| Live slot offset not in record-data region          | PageManager (I-11)               |
| Record out of page bounds                           | PageManager (I-12)               |
| Overlapping records                                 | PageManager (I-16)               |
| Tombstone count mismatch                            | PageManager (I-17)               |
| Checksum mismatch                                   | PageManager (deferred, Phase 10) |
| Cache coherency / stale frame                       | Buffer Pool                      |

`ValidatePage()` runs I-01 through I-18 in order, stopping at the first
violation and returning `kCorruption` with a descriptive message.
`ValidatePage()` must not mutate the page.

**When to call ValidatePage:**

- After every page load from storage, before exposing the page frame.
- In debug/test builds, after every PageManager mutation.
- In production builds, post-mutation validation is optional depending on
  performance requirements.

---

## 16. Memory Contract

- `page` points to exactly `kPageSize` bytes.
- `page` memory is writable for mutating operations.
- `page` memory is owned by the Buffer Pool; PageManager never allocates or
  frees it.
- PageManager never stores a pointer to `page` between calls.

**ByteView lifetime:** A `ByteView` returned by `GetRecord()` remains
physically valid only while:

1. The page remains pinned in the Buffer Pool, AND
2. The referenced record bytes are not relocated or otherwise invalidated.

The following operations may invalidate the `ByteView`'s physical address:

- `Compact()` — live records are repacked to new locations within the page;
  all existing pointers into the record-data area become invalid.
- `UpdateRecord()` when relocation occurs — the record is written to a new
  location within the page; the old address is abandoned.
- Buffer Pool eviction or reuse of the page frame — the underlying memory is
  reclaimed.

`DeleteRecord()` does not reclaim or physically move the record bytes
immediately; however, the record becomes logically deleted and the `ByteView`
must no longer be treated as a valid representation of a live record.

An in-place `UpdateRecord()` (`0 < new_size <= old_size`) preserves the
record's physical start address, but may change the record contents and
length. A previously obtained `ByteView` therefore must not be used to infer
the previous record contents or length after any mutation.

**Aliasing precondition:** For `InsertRecord` and `UpdateRecord`, the input
data range `[data, data + size)` must not overlap `[page, page + kPageSize)`.
The caller must copy data out of the page before passing it as input.

---

## 16a. Persistent Memory Access

PageManager accesses all persistent page fields through `memcpy`-based helpers,
never through `reinterpret_cast<PageHeader*>` or `reinterpret_cast<Slot*>`
over the raw byte buffer.

A `std::byte[kPageSize]` buffer does not guarantee the alignment required by
`uint32_t` or `PageHeader`. Direct struct overlay raises alignment,
object-lifetime, padding, and strict-aliasing concerns under the C++ standard.

```cpp
// Correct — memcpy satisfies alignment and aliasing rules
uint16_t slot_count;
std::memcpy(&slot_count, page + 6, sizeof(slot_count));

// Incorrect — undefined behaviour if page is not suitably aligned
// const PageHeader* hdr = reinterpret_cast<const PageHeader*>(page);
// uint16_t sc = hdr->slot_count;
```

`static_assert(sizeof(PageHeader) == kHeaderSize)` is a compile-time size
check only. It does not authorise struct-overlay usage.

Serialisation helpers (`ReadHeader`, `WriteHeader`, `ReadSlot`, `WriteSlot`)
are private implementation details declared in `page_manager.h` and defined
in `page_manager.cpp`.

---

## 17. Buffer Pool Interaction

```
Executor / Table Heap
        │
        │  buffer_pool.FetchPage(page_id)     ← pin the frame
        ▼
   Buffer Pool
        │  cache miss? → storage_manager.ReadPage(page_id, frame)
        ▼
   std::byte* page_memory   (frame owned by Buffer Pool)
        │
        │  page_manager.InsertRecord(page_memory, data, size)
        ▼
   PageManager  — reads and writes page_memory bytes via helpers
        │         no I/O, no allocation, no StorageManager calls
        ▼
   std::byte* page_memory   (dirty)
        │
        │  buffer_pool.UnpinPage(page_id, dirty=true)
        ▼
   Buffer Pool — marks frame dirty
        │         at eviction: storage_manager.WritePage(page_id, frame)
        ▼
   database.tdb
```

| Resource          | Owner       | Lifetime                                                        |
|-------------------|-------------|-----------------------------------------------------------------|
| Page frame memory | Buffer Pool | Until frame is evicted from pool.                               |
| PageHeader fields | PageManager | Read/written via helpers; no separate copy.                     |
| Slot entries      | PageManager | Read/written via helpers; no separate copy.                     |
| ByteView.data     | Caller      | Non-owning pointer. Valid only while the page frame remains     |
|                   |             | pinned and the referenced record storage has not been           |
|                   |             | relocated or reused. `DeleteRecord()` logically invalidates     |
|                   |             | the record. In-place `UpdateRecord()` preserves the physical    |
|                   |             | address but may change the contents and length.                 |

---

## 18. Storage Manager Interaction

PageManager must NOT call `ReadPage()` or `WritePage()`.

Reasons:

1. **Testability:** PageManager tests use a stack-allocated byte array. No
   file, no mock, no StorageManager required.
2. **WAL correctness:** WAL intercepts all writes between Buffer Pool and
   StorageManager (Phase 10). Direct `WritePage()` calls from PageManager
   would bypass WAL.
3. **Dirty-page tracking:** The Buffer Pool marks frames dirty after PageManager
   mutations. Direct writes would produce stale dirty flags.
4. **Separation of concerns:** StorageManager speaks page-level raw I/O;
   PageManager speaks record layout within a page.

---

## 19. Error Handling

> Before implementing, verify whether `StatusCode` already exists in
> `include/tinydb/common/result.h`. If so, use the existing definition
> and add only genuinely absent codes.

| Code             | Meaning                                                                    |
|------------------|----------------------------------------------------------------------------|
| kOk              | Success.                                                                   |
| kInvalidArgument | Invalid operation argument, including zero or oversized record length,     |
|                  | invalid page_id in Init(), or invalid Compact() scratch-buffer arguments   |
|                  | if those are runtime-checked.                                              |
| kRecordNotFound  | Slot is a tombstone (record was deleted).                                  |
| kInvalidRecordID | slot_id >= slot_count, wrong page_id, or otherwise not valid.              |
| kPageFull        | Insufficient contiguous space even after compaction.                       |
| kCorruption      | Page invariant violated.                                                   |

`Init()` returns `kInvalidArgument` if `page_id` is 0 or `kInvalidPageId`
(consistent with I-01: `header.page_id >= 1`).

| Operation        | Possible Codes                                                                  |
|------------------|---------------------------------------------------------------------------------|
| Init             | kOk, kInvalidArgument                                                           |
| InitFromExisting | kOk, kCorruption                                                                |
| InsertRecord     | kOk, kInvalidArgument, kPageFull, kCorruption                                   |
| GetRecord        | kOk, kInvalidRecordID, kRecordNotFound, kCorruption                             |
| DeleteRecord     | kOk, kInvalidRecordID, kRecordNotFound, kCorruption                             |
| UpdateRecord     | kOk, kInvalidRecordID, kRecordNotFound, kPageFull, kInvalidArgument, kCorruption|
| Compact          | kOk, kInvalidArgument, kCorruption                                              |
| ValidatePage     | kOk, kCorruption                                                                |

---

## 20. C++17 Public Interface

See:

- `include/tinydb/common/byte_view.h`
- `include/tinydb/storage/record_id.h`
- `include/tinydb/storage/page.h`
- `include/tinydb/storage/page_manager.h`

No implementation code is present in these headers. The public headers must
compile cleanly as C++17 translation units with warnings enabled. Definitions
for non-inline methods are provided by `page_manager.cpp`.

---

## 21. Testing Strategy

All tests use `std::array<std::byte, kPageSize>` on the stack. No Buffer Pool,
no StorageManager, no file system required.

**Groups:**

- Group 1 — Initialisation
- Group 2 — Insert (including slot reuse)
- Group 3 — GetRecord
- Group 4 — Delete (NOT idempotent)
- Group 5 — Update (in-place and relocation; RecordID stability)
- Group 6 — Compaction (slot IDs preserved; ByteView contract)
- Group 7 — Free Space accounting
- Group 8 — Invariants and corruption detection
- Group 9 — Boundary conditions

**Insert tests:**

- `Insert_ZeroLength_ReturnsInvalidArgument`
- `Insert_RecordTooLarge_ReturnsInvalidArgument`
- `Insert_ExactFit`
- `Insert_NewSlotAccountsForSlotDirectorySpace`
- `Insert_ReusedSlotDoesNotConsumeSlotDirectorySpace`

**Delete tests:**

- `Delete_InvalidRecordID_ReturnsInvalidRecordID`
- `Delete_DeletedRecord_ReturnsRecordNotFound`
- `Delete_CorruptSlot_ReturnsCorruption`

**Update tests:**

- `Update_ZeroLength_ReturnsInvalidArgument`
- `Update_RecordTooLarge_ReturnsInvalidArgument`
- `Update_InvalidRecordID_ReturnsInvalidRecordID`
- `Update_DeletedRecord_ReturnsRecordNotFound`
- `Update_CorruptSlot_ReturnsCorruption`
- `Update_InPlace_PreservesPhysicalOffset`
- `Update_Relocation_ConsumesNewRecordSize`
- `Update_Relocation_LeavesOldBytesFragmented`
- `Update_Relocation_AfterCompaction`
- `Update_InsufficientSpace_ReturnsPageFull`
- `Update_LargerRecord_PreservesSlotID`
- `Update_LargerRecord_AfterCompaction_PreservesSlotID`

**Compaction tests:**

- `Compact_PreservesRecordContents`
- `Compact_PreservesRecordIDs`
- `Compact_ReclaimsFragmentedSpace`
- `Compact_PreservesTombstones`
- `Compact_InvalidatesByteView_ByContract`
- `Compact_DetectsCorruptedSourcePage`
- `Compact_FailureLeavesOriginalPageUnchanged`

**GetRecord tests (corruption):**

- `GetRecord_CorruptSlot_ReturnsCorruption`

**ValidatePage usage:**

Every test that expects the page to remain valid after a successful mutation
must verify:

```cpp
ASSERT_EQ(page_manager.ValidatePage(page.data()), Status::kOk);
```

Tests that intentionally construct corrupted pages must instead verify the
expected `kCorruption` result.

---

## 22. Future Extensions

- **Overflow pages:** records larger than one page. Deferred.
- **Checksums:** algorithm and page-region coverage to be defined in Phase 10.
- **WAL / LSN:** `reserved_lsn_lo` and `reserved_lsn_hi` together represent
  the 64-bit page LSN in Phase 10. Reconstructed as:
  ```cpp
  uint64_t lsn =
      (static_cast<uint64_t>(reserved_lsn_hi) << 32) |
      static_cast<uint64_t>(reserved_lsn_lo);
  ```
- **MVCC visibility:** future use of `Slot.reserved` field.
- **Explicit endianness / serialisation spec:** before format is stable.
- **B+ Tree page types:** `kBPlusInternal`, `kBPlusLeaf` (Phase 6).
- **Free-space directory page:** `kFreeSpace` (Phase 5+).
