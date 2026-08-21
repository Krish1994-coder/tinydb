# Page Manager — Study Notes

Personal study notes used during the Page Manager design phase.
These notes are intentionally separate from the formal engineering
documentation in docs/02_page_manager.md.

---

## Slotted Page — Key Insight

The central insight of the slotted page design:

    Slot directory   grows downward (toward higher byte offsets)
    Record data      grows upward   (toward lower byte offsets)
    Free space       is the gap between them

This means:
- Insertions are O(1) as long as free space exists.
- Neither region relocates when the other grows.
- Stable RecordIDs are possible via slot indirection.

---

## Why Slot Indirection Enables Stable RecordIDs

Without slots:
    RecordID = page_id + byte_offset
    Compaction changes byte_offsets → RecordIDs broken

With slots:
    RecordID = page_id + slot_id
    Compaction updates slot.offset but NOT slot_id
    RecordID remains valid → indexes and foreign refs survive

---

## Tombstone Encoding

Deleted slot: offset == 0 && length == 0

Why offset == 0 works as a sentinel:
    Offset 0 = start of page = page header
    No valid record can start at offset 0
    Therefore offset == 0 is unambiguous: "deleted"
    No extra flag bit needed in Slot.reserved

---

## Free Space Arithmetic

    PAGE_SIZE        = 4096
    kHeaderSize      = 28    bytes
    kSlotSize        = 6     bytes

    Empty page:
        free_space_start = 28
        free_space_end   = 4096
        free_space       = 4068 bytes

    After 3 inserts (100, 200, 150 bytes):
        slots            = 3 × 6  = 18 bytes
        records          = 100 + 200 + 150 = 450 bytes
        free_space_start = 28 + 18  = 46
        free_space_end   = 4096 - 450 = 3646
        free_space       = 3646 - 46 = 3600 bytes

    Cost of inserting a new record (size R, no slot reuse):
        needed = R + kSlotSize

    Cost of inserting a new record (size R, slot reused):
        needed = R

---

## C++17 Compatibility Notes

std::span<const std::byte>  →  C++20 only. Use ByteView instead.
operator== = default        →  C++20 only. Implement explicitly.
Concepts                    →  C++20 only. Not used.
std::bit_cast               →  C++20 only. Use memcpy instead.

---

## Persistent Memory Access — Why reinterpret_cast Is Wrong

    std::byte page[4096];   // alignment: alignof(std::byte) == 1
    PageHeader* hdr = reinterpret_cast<PageHeader*>(page);
    // UNDEFINED BEHAVIOUR: PageHeader requires alignof(uint32_t) == 4
    // The byte array does not guarantee 4-byte alignment.

    Correct approach:
    uint32_t page_id;
    std::memcpy(&page_id, page + 0, sizeof(page_id));
    // memcpy handles unaligned source; result in page_id is well-defined.

---

## UpdateRecord — RecordID Stability Design

Version 1 design (WRONG): Delete + Insert
    - DeleteRecord(old_rid)   → tombstone
    - InsertRecord(new_data)  → might reuse old slot → same slot_id
    - But NOT guaranteed → RecordID unstable

Version 3 design (CORRECT): Relocate, preserve slot_id
    - Allocate new space at free_space_end
    - Copy new_data to new location
    - Update slot[slot_id].offset and .length
    - Old bytes become fragmentation
    - slot_id NEVER changes → RecordID always stable

---

## Compaction — ByteView Invalidation

Pinning a page (Buffer Pool pin count > 0):
    → Prevents frame eviction
    → Does NOT prevent PageManager from moving bytes within the frame

Therefore:
    After Compact(), any ByteView (raw pointer) into the record area
    is INVALID. The pointed-to bytes have been relocated.

Safe pattern:
    ByteView view = GetRecord(page, rid);     // read bytes
    use(view);                                 // use immediately
    // Do NOT store view across a Compact() or UpdateRecord() call

---

## References

- CMU 15-445 Lecture 4 — Database Storage II (slotted pages)
- Database Internals, Alex Petrov, Chapter 3 (pages 56–80)
- PostgreSQL src/include/storage/bufpage.h (PageHeaderData, ItemId)
- CMU 15-445 Project 1 — Buffer Pool Manager (pin count, dirty state)
