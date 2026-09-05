# Storage Engine

## 1. Purpose

The storage engine is the lowest layer of TinyDB's persistence stack. It is
responsible for translating logical page operations into physical file I/O.

The storage engine knows nothing about records, slots, B+ tree nodes, or query
execution. It stores and retrieves fixed-size pages and maintains the metadata
required to locate those pages.

The storage engine is intentionally kept simple so that higher layers can
build caching, page interpretation, indexing, transactions, and recovery on
top of it.

---

## 2. Responsibilities

The storage engine is responsible for:

| Responsibility | Description |
|----------------|-------------|
| File lifecycle | Creating, opening, and closing the database file on disk. |
| Page allocation | Assigning a new page identifier and reserving space in the database file for it. |
| Page I/O | Reading and writing complete pages. |
| Durability | Providing an explicit operation that forces pending writes to stable storage. |
| Page count | Reporting the total number of pages currently allocated in the database file. |

The storage engine does **not** own:

- Page caching.
- Record layout.
- Slot management.
- B+ tree structures.
- Locking or concurrency control.
- Write-ahead logging.
- Crash recovery.
- Query execution.
- Free-space tracking in the current implementation.

These responsibilities belong to higher layers or future phases.

---

## 3. Design Principles

The storage engine follows several core principles.

### 3.1 Page-granular I/O

All database I/O is performed in complete pages.

The storage engine never exposes arbitrary byte-range operations to higher
layers.

### 3.2 Opaque data pages

The storage engine interprets only the database file header.

All pages after page 0 are treated as opaque byte arrays. Their internal
structure belongs to the page manager and higher layers.

### 3.3 Explicit durability

A successful write does not necessarily mean that data has reached stable
storage.

`WritePage()` performs the requested file write. `Flush()` provides the
explicit durability boundary.

This separation allows higher layers such as the buffer pool to batch writes
before forcing them to stable storage.

### 3.4 Stable page identifiers

Page identifiers are logical identifiers rather than physical byte offsets.

The mapping from a page identifier to a physical file offset is deterministic
because all pages have a fixed size.

---

## 4. Database File Layout

A TinyDB database is represented as a single binary file on disk. Its layout
is a contiguous sequence of fixed-size pages, preceded by a file header
occupying the first page.

```text
database.tdb
+---------------------+  <- byte 0
|  Page 0             |
|  File Header        |
|  ---------------    |
|  * Magic number     |
|  * Format version   |
|  * Page size        |
|  * Page count       |
+---------------------+  <- byte PAGE_SIZE
|  Page 1             |
+---------------------+  <- byte PAGE_SIZE * 2
|  Page 2             |
+---------------------+
|  ...                |
+---------------------+  <- byte PAGE_SIZE * N
|  Page N             |
+---------------------+
```

The file header is the only page whose contents the storage engine interprets.
All other pages are treated as opaque byte arrays. Ownership of their internal
structure belongs to higher layers.

The on-disk position of any page is determined by:

```text
offset = page_id * page_size
```

Given a page identifier, the storage engine can seek directly to the correct
location without consulting any index or directory structure. This is only
possible because pages are fixed size.

### 4.1 File Header Format

The first page of every TinyDB database file is reserved for the storage
engine file header.

The Phase-2 implementation uses a 4096-byte header page.

Only the first 20 bytes contain defined metadata. The remaining bytes in page 0
are reserved and are currently zero-filled.

| Offset | Size | Field | Value / Meaning |
|--------|------|-------|-----------------|
| 0 | 8 bytes | Magic number | `TINYDB\0\0` |
| 8 | 4 bytes | Format version | `1` |
| 12 | 4 bytes | Page size | `4096` |
| 16 | 4 bytes | Page count | Total allocated pages, including page 0 |
| 20 | 4076 bytes | Reserved | Currently zero-filled |

The logical file-header metadata occupies 20 bytes, while the physical file
header occupies the complete 4096-byte page 0.

#### 4.1.1 Magic Number

The database magic number is:

```text
TINYDB\0\0
```

It identifies a file as a TinyDB database and prevents the storage engine from
mistaking an unrelated binary file for a TinyDB database.

#### 4.1.2 Format Version

The current database format version is:

```text
1
```

The version allows future TinyDB implementations to reject incompatible
on-disk formats rather than interpreting them using incorrect assumptions.

#### 4.1.3 Page Size

The current implementation uses:

```text
4096 bytes
```

The page size is persisted in the file header.

The Phase-2 implementation currently accepts only a stored page size of 4096
bytes. Alternative page sizes are reserved for future format versions.

#### 4.1.4 Page Count

The page count represents the total number of allocated pages, including
page 0.

Immediately after database creation:

```text
page_count = 1
```

Therefore:

```text
page 0 = file header
```

The first data page allocated by `AllocatePage()` receives page identifier 1.

#### 4.1.5 Reserved Bytes

Bytes 20 through 4095 of the header page are reserved for future metadata.

The current implementation initializes these bytes to zero when writing a new
file header.

Future versions may use this space for additional file metadata without
changing the physical page boundary.

### 4.2 Initial Database State

After a successful `CreateDatabase()` operation:

```text
page_count = 1
page_size  = 4096
```

The file contains exactly one page:

```text
Page 0
+-- File Header
```

No data pages exist until `AllocatePage()` is called.

### 4.3 Page Allocation

Pages are allocated monotonically.

The current page count determines the identifier of the next page.

For example:

```text
Initial state:
    page_count = 1

AllocatePage():
    new page_id = 1
    page_count  = 2

AllocatePage():
    new page_id = 2
    page_count  = 3

AllocatePage():
    new page_id = 3
    page_count  = 4
```

Allocated pages are appended to the database file and are initially
zero-filled.

Page identifiers are not recycled in the current implementation.

Free-page tracking is intentionally deferred to a later phase.

---

## 5. Page Concept

A page is the fundamental unit of I/O in TinyDB. All reads and writes are
page-granular — the storage engine never reads a partial page or a
variable-size region.

**Why pages?**

Disk and SSD I/O carries high per-operation overhead regardless of how many
bytes are transferred. Grouping data into fixed-size chunks amortises that
overhead: a single read operation retrieves a full page, which may contain many
records, index entries, or tree nodes.

This also aligns naturally with the operating system's own virtual memory page
size, enabling efficient integration with the OS page cache.

**Why fixed size?**

Fixed-size pages make the offset formula above trivially correct. There is no
directory of variable-length extents to maintain, no fragmentation to manage
at this layer, and no metadata lookup required to locate any given page.

Any page identifier maps to an exact byte offset in constant time.

**Page size**

The Phase-2 implementation uses 4 KiB (4096-byte) pages.

The page size is stored in the file header and restored when an existing
database is opened.

The current implementation validates that the stored page size is 4096 bytes.
Alternative page sizes are reserved for a future format version.

**Atomicity**

Many systems provide atomicity guarantees at the filesystem or storage device
level for relatively small writes, although the exact guarantees depend on the
underlying hardware and filesystem.

TinyDB therefore treats crash recovery as a separate concern handled by the
recovery subsystem rather than relying solely on storage atomicity.

---

## 6. Page Identifier

A page identifier is an unsigned integer that uniquely and permanently
identifies one page within a database file. It is assigned at allocation time
and does not change for the lifetime of that page.

Properties of a valid page identifier:

- Immutable after allocation.
- Unique within one database file.
- Not meaningful across different database files.

In the initial implementation, identifiers are monotonically increasing.
Allocated identifiers are not recycled.

Two special values are reserved:

- **Header page:** identifier `0`, always present and always refers to the file
  header.
- **Invalid page:** a sentinel value indicating "no page" or an uninitialized
  reference. Its exact representation is defined in
  `include/tinydb/storage/page_id.h`.

---

## 7. StorageManager

The `StorageManager` provides the public interface to the storage engine. It
exposes page-granular I/O to higher layers and abstracts all direct interaction
with the underlying file system.

It provides the following operations:

- **Creating a database** — creating a new file on disk and writing the initial
  file header.
- **Opening a database** — opening an existing file and reading the header to
  restore internal state.
- **Closing a database** — ensuring pending writes are durable and releasing
  the file descriptor.
- **Allocating pages** — extending the file by one page and returning the new
  identifier.
- **Reading pages** — locating a page by identifier and reading its contents
  into a caller-supplied buffer.
- **Writing pages** — locating a page by identifier and writing a caller-
  supplied buffer to that position.
- **Flushing** — requesting that all pending writes be pushed from the OS page
  cache to stable storage.

The complete interface declaration lives in:

```text
include/tinydb/storage/storage_manager.h
```

### 7.1 Phase-2 Implementation Semantics

The current `StorageManager` implementation provides:

- Exclusive database creation.
- Existing database opening.
- File-header validation.
- Idempotent database closing.
- Monotonically increasing page allocation.
- Zero-filled newly allocated pages.
- Full-page reads.
- Full-page writes.
- Short-read detection.
- Short-write detection.
- Explicit durability through `Flush()`.
- Persistent page-count metadata.
- POSIX file-descriptor based I/O.

`WritePage()` does not implicitly call `fsync()`.

This allows higher layers to batch writes and then establish a
durability boundary when required.

`Flush()` calls `fsync()` on the database file descriptor to request that
pending file data and metadata be synchronized with stable storage.

`CloseDatabase()`:

- Flushes pending writes.
- Writes the current page count to the file header.
- Synchronizes the file.
- Closes the POSIX file descriptor.
- Marks the database as closed.

### 7.2 Thread Safety

`StorageManager` is not thread-safe.

The caller is responsible for synchronization.

In the intended architecture, the buffer pool manager coordinates access before
invoking `StorageManager` operations.

Synchronization primitives do not belong in `StorageManager` itself.

---

## 8. Read Flow

When the buffer pool asks the storage engine to read a page, the following
sequence executes:

1. **Validate** — confirm the page identifier is within the range of allocated
   pages.
2. **Compute offset** — derive the exact byte position from the page identifier
   and page size.
3. **Locate page** — position the file cursor at the computed offset.
4. **Read page** — read exactly one full page into the caller-supplied buffer.
5. **Return result** — report success or an error status.

A short read (fewer than `page_size` bytes returned) is always treated as an
error.

The storage engine never returns a partial page.

The OS may satisfy the read from its page cache without touching physical
storage. The storage engine neither knows nor controls this — cache management
at that level is the operating system's concern.

---

## 9. Write Flow

When the buffer pool asks the storage engine to write a page, the following
sequence executes:

1. **Validate** — confirm the page identifier is within the range of allocated
   pages.
2. **Compute offset** — derive the exact byte position from the page identifier
   and page size.
3. **Locate page** — position the file cursor at the computed offset.
4. **Write page** — write exactly one full page from the caller-supplied buffer.
5. **Return result** — report success or an error status.

A write operation is not durable on its own.

After the write, data may reside in the OS page cache and may be lost if the
process or machine crashes before it is flushed to stable storage.

The storage engine exposes a separate flush operation that blocks until the OS
accepts the synchronization request.

The flush is intentionally separate from the write. Forcing a sync on every
page write would eliminate the opportunity for batching and make write-heavy
workloads impractically slow.

The buffer pool manager decides when to flush — typically at checkpoint
boundaries — and the storage engine executes that decision without imposing
its own policy.

---

## 10. Flush and Durability

`Flush()` is the explicit durability boundary of the storage engine.

When the database is open:

```text
WritePage()
     |
     v
OS page cache
     |
     | Flush()
     v
fsync()
     |
     v
stable storage
```

The storage engine does not call `fsync()` after every page write.

This design allows higher layers to batch writes and then establish a
durability boundary when required.

---

## 11. Close Flow

`CloseDatabase()` is idempotent.

Calling it when the database is already closed returns success.

When a database is open, the close sequence is:

1. Flush pending writes.
2. Update the persistent page count in page 0.
3. Synchronize the file.
4. Close the POSIX file descriptor.
5. Mark the `StorageManager` as closed.

The updated page count is therefore available when the database is subsequently
opened.

---

## 12. Error Handling

All public methods on `StorageManager` return a `Status` value.

No exceptions cross the storage engine boundary.

Callers are expected to check every return value.

The initial set of status codes used by the storage engine is:

| Status code | Trigger condition |
|-------------|-------------------|
| `OK` | The operation completed successfully. |
| `IOError` | An OS call returned an error or a full page could not be transferred. |
| `InvalidArgument` | Page identifier is invalid/out of range, a required pointer is null, or the file path is empty. |
| `AlreadyExists` | `CreateDatabase` is called when the file already exists. |
| `NotFound` | `OpenDatabase` is called when the file does not exist. |
| `Corruption` | The file header is incomplete or contains invalid/unsupported metadata. |
| `OutOfSpace` | `AllocatePage` would exceed the maximum representable page count or page offset. |
| `NotOpen` | An operation requiring an open database is called while the database is closed. |

The `Status` type is defined in:

```text
include/tinydb/common/status.h
```

---

## 13. File Validation

When opening an existing database, `StorageManager` validates the file header
before exposing the database to higher layers.

The following conditions are checked:

- The header page can be read completely.
- The magic number matches `TINYDB\0\0`.
- The format version matches version `1`.
- The page size is non-zero.
- The page size is currently supported and equals `4096`.
- The page count is non-zero.

If validation fails, the database is rejected with `Corruption`.

The file descriptor is closed and the `StorageManager` remains closed.

---

## 14. Offset Calculation

Because all pages have a fixed size, a page identifier maps directly to a byte
offset:

```text
offset = page_id * page_size
```

For the current 4096-byte page size:

```text
page 0  ->  offset 0
page 1  ->  offset 4096
page 2  ->  offset 8192
page 3  ->  offset 12288
```

The implementation checks that the calculated offset can be represented by the
platform's `off_t` type before performing the seek.

---

## 15. Physical File Growth

When `AllocatePage()` succeeds, `StorageManager` appends exactly one page to
the database file.

The newly allocated page is initialized to zero bytes.

For a 4096-byte page:

```text
before allocation:
    file size = page_count * 4096

after allocation:
    file size = (page_count + 1) * 4096
```

The page identifier returned by the allocation is the previous page count.

---

## 16. Separation From PageManager

The file header and data-page header are different concepts.

| Aspect | File Header | Data Page Header |
|--------|-------------|------------------|
| Location | Page 0 | Pages 1 and above |
| Owner | StorageManager | PageManager |
| Purpose | Database/file metadata | Data-page layout metadata |
| Contains | Magic, version, page size, page count | Page ID, page type, slots, free-space metadata, etc. |
| Interpreted by | Storage engine | Page manager |

`StorageManager` must not interpret `PageManager`-specific data-page structures.

Likewise, `PageManager` must not directly access the underlying file
descriptor.

All page I/O passes through `StorageManager`.

---

## 17. Current Limitations

The Phase-2 implementation intentionally does not provide:

- Free-page reuse.
- Page checksums.
- Write-ahead logging.
- Crash recovery.
- Transactions.
- Memory-mapped I/O.
- Direct I/O.
- Asynchronous I/O.
- Multiple database files.
- Alternative page sizes.

These features are reserved for later phases.

---

## 18. Future Extensions

The following are intentionally deferred from Phase 2. Recording them here
ensures the current interface does not accidentally foreclose them.

| Extension | Phase | Notes |
|-----------|-------|-------|
| Free page list | 3 | Track deallocated pages in the file header; allocation checks the list before appending. |
| Page checksums | 3 | Write a checksum into each page to detect torn or corrupted writes at startup. |
| Memory-mapped I/O | 4 | Replace explicit read/write calls with memory-mapped access for zero-copy reads. |
| Direct I/O | 5 | Bypass the OS page cache for full control over buffer management. |
| Multi-file support | 6 | Spread a large database across multiple files (tablespace model). |
| Asynchronous I/O | 6 | Submit batches of read/write operations without blocking the calling thread. |

---

## 19. Reference Material

The storage-engine design was informed by established database storage
architectures and file-format documentation.

Relevant references include:

- SQLite file-format documentation.
- PostgreSQL storage and buffer-management concepts.
- POSIX file I/O primitives including `open`, `lseek`, `read`, `write`, and
  `fsync`.

These references are used for design guidance only. TinyDB has its own file
format and storage-engine interface.
