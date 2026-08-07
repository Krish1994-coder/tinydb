# Storage Engine — Learning Notes

> Personal study notebook for TinyDB.
>
> This document captures the concepts, questions, reading notes, and personal observations gathered while studying database storage engines.
>
> Unlike `docs/01_storage_engine.md`, which defines the Storage Engine design, this document records the learning process and the reasoning that informed those design decisions.

---

# Learning Objectives

By the end of this study session I should be able to explain, in my own words:

- What a database page is.
- Why databases use fixed-size pages.
- How page identifiers work.
- How a database file is organised.
- How the storage engine performs disk I/O.
- Why writes are not durable until flushed.
- The difference between the Storage Engine and the Buffer Pool.
- Why concurrency control does not belong inside the storage engine.

---

# Reading Plan

| Resource | Focus |
|----------|-------|
| CMU 15-445 – Storage I | Pages, heap files, slotted pages, storage architecture |
| Database Internals – Alex Petrov (Ch. 1 & 2) | Storage engine fundamentals |
| SQLite File Format | Database header and page layout |
| PostgreSQL Storage Manager | Production reference implementation |
| POSIX APIs | open, read, write, pread, pwrite, fsync |

---

# Questions and Answers

## What is a page?

A page is a fixed-size block of bytes that represents the smallest unit of disk I/O in a database system. Rather than reading or writing individual records, the database always transfers complete pages between disk and memory. Higher-level structures such as heap tables, indexes, and B+ tree nodes are stored inside these pages.

---

## Why does a database use pages?

Disk access is significantly slower than memory access. Reading one record at a time would require an excessive number of system calls and disk operations. By grouping many records into a page, a single I/O operation transfers useful data for many future accesses.

Pages also align naturally with the operating system's page cache, improving overall I/O efficiency.

---

## Why are pages fixed size?

Fixed-size pages greatly simplify storage management.

Every page can be located directly using:


offset = page_id × page_size


There is no need to maintain variable-length extent tables or complex offset calculations. Fixed-size pages also simplify buffer pool management because every cached frame has the same size.

---

## What is a page identifier?

A page identifier (Page ID) uniquely identifies one page within a database file.

Unlike a byte offset, a page identifier is a logical reference that remains stable for the lifetime of the page. Higher layers communicate using page identifiers instead of raw file offsets.

Initially, TinyDB allocates page identifiers monotonically. Page reuse will be introduced in a later phase through free-space management.

---

## What does a database file look like?

TinyDB stores the database inside a single binary file.

The first page stores metadata such as:

- Magic number
- File format version
- Page size
- Total page count

The remaining pages store user data and internal database structures.

Conceptually:


+----------------------+
| Header Page          |
+----------------------+
| Data Page 1          |
+----------------------+
| Data Page 2          |
+----------------------+
| Data Page 3          |
+----------------------+
| ...                  |
+----------------------+


---

## How does the storage engine locate a page?

The storage engine computes the page offset using:


offset = page_id × page_size


It then seeks directly to that position and performs the required read or write.

Because the mapping is arithmetic rather than lookup-based, locating any page takes constant time.

---

## What happens during ReadPage()?

The storage engine performs the following sequence:

1. Validate the page identifier.
2. Compute the byte offset.
3. Seek to the correct position.
4. Read exactly one page.
5. Return success or an error.

The storage engine never interprets the bytes being read.

---

## What happens during WritePage()?

The write sequence is similar:

1. Validate the page identifier.
2. Compute the byte offset.
3. Seek to the page location.
4. Write exactly one page.
5. Return success or an error.

After the write completes, the data typically resides in the operating system's page cache rather than on permanent storage.

---

## Why is WritePage() not durable?

A successful `write()` only guarantees that the operating system accepted the data into its cache.

If the machine crashes before the cached data reaches disk, the changes may be lost.

Durability is only guaranteed after a successful `fsync()`.

---

## Why is Flush() separate from WritePage()?

Calling `fsync()` after every write would force every page write to become synchronous, dramatically reducing throughput.

Instead, writes accumulate in the operating system's cache.

When durability is required, the buffer pool calls `Flush()`, which invokes `fsync()` once to persist all pending writes.

This batching strategy provides much better performance.

---

## Why does the Storage Engine not cache pages?

Caching belongs entirely to the Buffer Pool Manager.

If the Storage Engine maintained its own cache, the system would contain multiple independent caches, increasing complexity and creating consistency problems.

Keeping the Storage Engine responsible only for disk I/O results in a cleaner separation of responsibilities.

---

## Why does synchronisation belong in the Buffer Pool?

The Storage Engine performs page-level I/O and has no knowledge of concurrent transactions or thread ownership.

The Buffer Pool coordinates shared access to cached pages and therefore owns the required synchronization primitives.

Keeping synchronization outside the Storage Engine makes the storage layer simpler, easier to test, and reusable.

---

# Notes From CMU 15-445

**Lecture Date:**

**Important concepts**

-

-

-

**Questions**

-

-

-

---

# Notes From Database Internals

**Chapter(s):**

**Important concepts**

-

-

-

**Interesting observations**

-

-

-

**Questions**

-

-

-

---

# Notes From SQLite

**Important header fields**

- Magic string
- Page size
- Database page count

**Interesting implementation ideas**

-

-

-

**Possible ideas for TinyDB**

-

-

-

---

# Key Takeaways

- Fixed-size pages simplify both storage management and buffer management.
- The Storage Engine owns disk I/O only; higher layers interpret page contents.
- Durability requires explicit flushing to stable storage.
- Clean separation of responsibilities produces a simpler architecture.

---

# Things I Want To Revisit

-

-

-

---

# Future Topics

- Heap files
- Slotted pages
- Free-space maps
- Buffer replacement algorithms
- B+ Tree page layout
- Write-Ahead Logging (WAL)
- Checksums
- mmap
- O_DIRECT
- io_uring

---

# Personal Observations

(Add personal thoughts, implementation ideas, comparisons with SQLite or PostgreSQL, or anything worth revisiting.)

---

# Phase 2 Checklist

- [x] Review `docs/00_project_overview.md`
- [x] Review `docs/01_storage_engine.md`
- [x] Create `include/tinydb/storage/page_id.h`
- [x] Create `include/tinydb/storage/storage_manager.h`
- [x] Create storage engine architecture diagram
- [ ] Study CMU 15-445 Storage I
- [ ] Read *Database Internals* (Chapters 1–2)
- [ ] Review SQLite file format specification
- [ ] Refine Storage Engine design after study
- [ ] Begin `docs/02_page_manager.md`

---

## Related Files


docs/00_project_overview.md
docs/01_storage_engine.md
include/tinydb/storage/page_id.h
include/tinydb/storage/storage_manager.h
diagrams/storage_engine_architecture.png


---

**Status:** Phase 2 — Storage Engine Design Complete
**Next Milestone:** Page Manager Design (`docs/02_page_manager.md`)