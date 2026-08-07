# Storage Engine

## 1. Purpose

The storage engine is the lowest layer of TinyDB. It is responsible for durably persisting pages to disk and retrieving them on demand. It has no knowledge of what the bytes it stores represent.

Every higher layer — the page manager, buffer pool, B+ tree, and query executor — depends on the storage engine as its only I/O foundation. Because it sits at the bottom of the stack, its correctness and performance characteristics propagate upward to every subsystem that builds on it.

---

## 2. Responsibilities

The storage engine is accountable for the following, and nothing more:

| Responsibility      | Description                                                                                              |
|---------------------|----------------------------------------------------------------------------------------------------------|
| File lifecycle      | Creating, opening, and closing the database file on disk.                                                |
| Page allocation     | Assigning a new page identifier and reserving space in the database file for it.                         |
| Page I/O            | Reading a page from disk into a caller-supplied buffer; writing a buffer back to its on-disk position.   |
| Durability          | Providing a mechanism to ensure pending writes reach stable storage when requested by higher layers.      |
| Page count          | Reporting the total number of pages currently allocated in the database file.                            |
| Free-space tracking | Maintaining a record of which pages are available for reuse. (Deferred — see Future Extensions.)         |

---

## 3. Non-responsibilities

The following concerns must not be introduced into the storage engine. Each belongs to a dedicated layer above it:

| Concern                                          | Owner                          |
|--------------------------------------------------|--------------------------------|
| Caching pages in memory                          | Buffer Pool Manager            |
| Interpreting page contents (records, tree nodes) | Page Manager / B+ Tree         |
| Concurrency control and locking                  | Transaction / Lock Manager     |
| Write-ahead logging and crash recovery           | Log Manager                    |
| Query planning and execution                     | Query Engine                   |
| Free-slot tracking within a page                 | Page Manager                   |

Every subsystem in TinyDB should define its non-responsibilities explicitly. Clear boundaries prevent scope creep and make each layer easier to reason about, test, and replace independently.

---

## 4. Database File Layout

A TinyDB database is represented as a single binary file on disk. Its layout is a contiguous sequence of fixed-size pages, preceded by a file header occupying the first page.

```
database.tdb
┌─────────────────────┐  ← byte 0
│  Page 0             │
│  File Header        │
│  ───────────────    │
│  • Magic number     │
│  • Format version   │
│  • Page size        │
│  • Page count       │
├─────────────────────┤  ← byte PAGE_SIZE
│  Page 1             │
├─────────────────────┤  ← byte PAGE_SIZE × 2
│  Page 2             │
├─────────────────────┤
│  ...                │
├─────────────────────┤  ← byte PAGE_SIZE × N
│  Page N             │
└─────────────────────┘
```

The file header is the only page whose contents the storage engine interprets. All other pages are treated as opaque byte arrays. Ownership of their internal structure belongs to higher layers.

The on-disk position of any page is determined by a single formula:

```
offset = page_id × page_size
```

Given a page identifier, the storage engine can seek directly to the correct location without consulting any index or directory structure. This is only possible because pages are fixed size.

---

## 5. Page Concept

A page is the fundamental unit of I/O in TinyDB. All reads and writes are page-granular — the storage engine never reads a partial page or a variable-size region.

### Why pages?

Disk and SSD I/O carries high per-operation overhead regardless of how many bytes are transferred. Grouping data into fixed-size chunks amortises that overhead: a single read operation retrieves a full page, which may contain many records, index entries, or tree nodes. This also aligns naturally with the operating system's own virtual memory page size, enabling efficient integration with the OS page cache.

### Why fixed size?

Fixed-size pages make the offset formula above trivially correct. There is no directory of variable-length extents to maintain, no fragmentation to manage at this layer, and no metadata lookup required to locate any given page. Any page identifier maps to an exact byte offset in constant time.

### Page size

The initial implementation is expected to use 4 KiB (4096-byte) pages because this aligns well with common operating system page sizes and is a common choice in many database systems. The page size is stored in the file header and is fixed for the lifetime of a given database file. The design does not preclude supporting alternative page sizes in the future.

### Atomicity

Many systems provide atomicity guarantees at the filesystem or storage device level for relatively small writes, although the exact guarantees depend on the underlying hardware and filesystem. TinyDB therefore treats crash recovery as a separate concern handled by the recovery subsystem rather than relying solely on storage atomicity.

---

## 6. Page Identifier

A page identifier is an unsigned integer that uniquely and permanently identifies one page within a database file. It is assigned at allocation time and does not change for the lifetime of that page.

Properties of a valid page identifier:

- Immutable after allocation.
- Unique within one database file.
- Not meaningful across different database files.
- In the initial implementation, identifiers are monotonically increasing — once a page is allocated its identifier is not recycled.

Two special values are reserved:

- **Header page**: identifier `0`, always present and always refers to the file header.
- **Invalid page**: a sentinel value indicating "no page" or an uninitialized reference. Its exact representation is defined in `include/tinydb/storage/page_id.h`.

---

## 7. StorageManager

The `StorageManager` provides the public interface to the storage engine. It exposes page-granular I/O to higher layers and abstracts all direct interaction with the underlying file system.

It is expected to expose operations for:

- **Creating a database** — creating a new file on disk and writing the initial file header.
- **Opening a database** — opening an existing file and reading the header to restore internal state.
- **Closing a database** — ensuring pending writes are durable and releasing the file descriptor.
- **Allocating pages** — extending the file by one page and returning the new identifier.
- **Reading pages** — locating a page by identifier and reading its contents into a caller-supplied buffer.
- **Writing pages** — locating a page by identifier and writing a caller-supplied buffer to that position.
- **Flushing** — requesting that all pending writes be pushed from the OS page cache to stable storage.

The complete interface declaration lives in `include/tinydb/storage/storage_manager.h`.

---

## 8. Read Flow

When the buffer pool asks the storage engine to read a page, the following sequence executes:

1. **Validate** — confirm the page identifier is within the range of allocated pages.
2. **Compute offset** — derive the exact byte position from the page identifier.
3. **Locate page** — position the file cursor at the computed offset.
4. **Read page** — read exactly one full page into the caller-supplied buffer.
5. **Return result** — report success or an error status.

A short read (fewer than `page_size` bytes returned) is always treated as an error. The storage engine never returns a partial page.

The OS may satisfy the read from its page cache without touching physical storage. The storage engine neither knows nor controls this — cache management at that level is the operating system's concern.

---

## 9. Write Flow

When the buffer pool asks the storage engine to write a page, the following sequence executes:

1. **Validate** — confirm the page identifier is within the range of allocated pages.
2. **Compute offset** — derive the exact byte position from the page identifier.
3. **Locate page** — position the file cursor at the computed offset.
4. **Write page** — write exactly one full page from the caller-supplied buffer.
5. **Return result** — report success or an error status.

A write operation is not durable on its own. After the write, data may reside in the OS page cache and will be lost if the process or machine crashes before it is flushed to disk. The storage engine exposes a separate flush operation that blocks until the drive confirms the data has been persisted.

The flush is intentionally separate from the write. Forcing a sync on every page write would eliminate the opportunity for batching and make write-heavy workloads impractically slow. The buffer pool manager decides when to flush — typically at checkpoint boundaries — and the storage engine executes that decision without imposing its own policy.

---

## 10. Error Handling

All public methods on `StorageManager` return a `Status` value. No exceptions cross the storage engine boundary. Callers are expected to check every return value.

The initial set of status codes the storage engine is expected to produce:

| Status code        | Trigger condition                                                                   |
|--------------------|------------------------------------------------------------------------------------|
| `OK`               | The operation completed successfully.                                               |
| `IOError`          | An OS call returned an error. The status message includes the syscall and errno.    |
| `InvalidArgument`  | Page identifier out of range, null buffer pointer, or empty file path.              |
| `AlreadyExists`    | `CreateDatabase` called when the file already exists.                               |
| `NotFound`         | `OpenDatabase` called on a file that does not exist.                                |
| `Corruption`       | File header magic number or version does not match expectations.                    |
| `OutOfSpace`       | `AllocatePage` would exceed the maximum representable page count.                   |
| `NotOpen`          | Any I/O method called before `OpenDatabase` or after `CloseDatabase`.               |

The `Status` type will be defined in `include/tinydb/common/status.h` during Phase 3.

---

## 11. Future Extensions

The following are intentionally deferred from Phase 2. Recording them here ensures the current interface does not accidentally foreclose them.

| Extension               | Phase | Notes                                                                                        |
|-------------------------|-------|----------------------------------------------------------------------------------------------|
| Free page list          | 3     | Track deallocated pages in the file header; allocation checks the list before appending.     |
| Page checksums          | 3     | Write a checksum into each page to detect torn or corrupted writes at startup.               |
| Memory-mapped I/O       | 4     | Replace explicit read/write calls with memory-mapped access for zero-copy reads.             |
| Direct I/O              | 5     | Bypass the OS page cache for full control over buffer management.                            |
| Multi-file support      | 6     | Spread a large database across multiple files (tablespace model).                            |
| Asynchronous I/O        | 6     | Submit batches of read/write operations without blocking the calling thread.                 |

---

## 12. References

| Resource                                          | What to read                                                                  |
|---------------------------------------------------|-------------------------------------------------------------------------------|
| CMU 15-445 — Lecture: Storage I                   | Pages, slots, heap files, and file organisation.                              |
| *Database Internals* — Alex Petrov                | Chapter 1 (Introduction) and Chapter 2 (B-Tree Basics and storage layout).   |
| SQLite file format specification                  | `sqlite.org/fileformat2.html` — file header fields and page layout.           |
| PostgreSQL source — `src/backend/storage/smgr/`   | Reference implementation of a production storage manager.                    |
| POSIX — `open`, `lseek`, `read`, `write`, `fsync` | System call semantics, return values, and errno conventions.                  |
