# TinyDB

> A relational database engine built from scratch in Modern C++17, with a
> production-oriented systems architecture.

TinyDB is an actively developed database engine focused on understanding and
implementing the core systems behind modern relational databases.

The project is built incrementally from the storage layer upward, with an
emphasis on clear subsystem boundaries, explicit ownership, well-defined
interfaces, correctness, failure handling, durability, and testability.

The architecture is informed by established database systems such as SQLite
and PostgreSQL while maintaining an independent implementation and file format.

---

# Engineering Status

> **Work in Progress — foundational storage components are implemented and
> the database engine is being extended incrementally.**

Current implementation status:

| Component | Status |
|-----------|--------|
| Project Architecture | Complete |
| Storage Manager | Complete |
| Database File Format | Complete |
| Page Allocation | Complete |
| Page I/O | Complete |
| File Header Validation | Complete |
| Explicit Durability / `fsync()` | Complete |
| Page Manager | Complete |
| Record Management | Complete |
| Page Validation | Complete |
| Page Compaction | Complete |
| Buffer Pool Manager | Planned |
| B+ Tree Index | Planned |
| Query Engine | Planned |
| Transactions | Planned |
| Concurrency Control | Planned |
| WAL / Crash Recovery | Planned |
| Performance Engineering | Planned |

The current implementation establishes the persistence and page-management
foundations required by the higher layers.

---

# Current Milestone

The current milestone focuses on the storage and page-management foundations.

Implemented functionality includes:

- Database file creation and opening.
- Persistent database file metadata.
- Fixed-size 4 KiB pages.
- Monotonically allocated page identifiers.
- Page-granular reads and writes.
- Explicit durability boundaries through `fsync()`.
- File-header validation and corruption detection.
- Zero-initialized newly allocated pages.
- Page lifecycle management.
- Slotted-page record organization.
- Record insertion, retrieval, deletion, and update.
- Tombstone handling.
- Page validation and invariant checking.
- Page compaction.
- Comprehensive unit-test coverage using GoogleTest.

The implementation currently has **109 automated tests passing** across the
storage and page-management components.

---

# Architecture

TinyDB follows a layered architecture with explicit ownership and
responsibilities between subsystems.

```text
                         Client Application
                                |
                                v
                         +--------------+
                         |  Client API  |
                         +------+-------+
                                |
                                v
                         +--------------+
                         |    Parser    |
                         |   SQL -> AST |
                         +------+-------+
                                |
                                v
                  +---------------------------+
                  | Analyzer + Catalog        |
                  |                           |
                  | Name resolution           |
                  | Type validation           |
                  | Metadata lookup           |
                  +-------------+-------------+
                                |
                                v
                  +---------------------------+
                  | Planner / Optimizer       |
                  |                           |
                  | AST -> Execution Plan     |
                  +-------------+-------------+
                                |
                                v
                         +--------------+
                         |   Executor   |
                         +------+-------+
                                |
                    +-----------+-----------+
                    |                       |
                    v                       v
             +-------------+         +-------------+
             | Table / Heap|         |   B+ Tree   |
             |    Scan     |         |    Index    |
             +------+------+         +------+------+
                    |                       |
                    +-----------+-----------+
                                |
                                v
                       +----------------+
                       |  Buffer Pool   |
                       |   Page Cache   |
                       +-------+--------+
                               |
                               v
                       +----------------+
                       | StorageManager |
                       +-------+--------+
                               |
                               v
                        +--------------+
                        | database.tdb |
                        +--------------+

        Cross-cutting services:
        Transaction Manager
        Concurrency Control
        WAL / Recovery
```

The architecture deliberately separates:

- **Planning** — determines what operations should be performed.
- **Execution** — performs the selected operations.
- **Buffer Pool** — manages in-memory database pages.
- **Storage Manager** — performs persistent page-level I/O.
- **Page Manager** — interprets and manages the contents of individual pages.
- **Catalog** — owns database metadata.
- **Transaction Manager** — manages transaction lifecycle.
- **Concurrency Control** — coordinates concurrent access.
- **WAL / Recovery** — provides crash-recovery infrastructure.

## Storage Architecture

The current persistence model uses a single binary database file composed of
fixed-size pages.

```text
database.tdb
+---------------------------+
| Page 0 -- File Header     |
|                           |
| Magic                     |
| Format Version            |
| Page Size                 |
| Page Count                |
| Reserved                  |
+---------------------------+
| Page 1                    |
+---------------------------+
| Page 2                    |
+---------------------------+
| Page 3                    |
+---------------------------+
| ...                       |
+---------------------------+
| Page N                    |
+---------------------------+
```

For the current Phase-2 format:

- Page size = 4096 bytes

The physical location of a page is deterministic:

```text
offset = page_id * page_size
```

Page 0 is reserved for database-level metadata.

Pages after page 0 are treated as opaque storage by the Storage Manager.
Their internal structure is owned by the Page Manager and higher layers.

Detailed storage-engine design is documented in:

```text
docs/01_storage_engine.md
```

## Storage Manager

The `StorageManager` is the persistence boundary between the database engine
and the operating system.

It is responsible for:

- Database creation.
- Database opening.
- Database closing.
- File-header management.
- Page allocation.
- Full-page reads.
- Full-page writes.
- Page-count management.
- File validation.
- Explicit durability through `fsync()`.

It deliberately does not understand:

- Records.
- Slots.
- Tables.
- Indexes.
- SQL.
- Query plans.
- Transactions.
- Buffer-pool policy.

This keeps physical file I/O isolated from logical database structures.

The public interface is defined in:

```text
include/tinydb/storage/storage_manager.h
```

The implementation is located in:

```text
src/storage/storage_manager.cpp
```

## Page Manager

The Page Manager owns the internal layout of data pages.

The current implementation uses a slotted-page organization for variable-length
records inside fixed-size pages.

```text
+------------------------------------------------+
|                 Page Header                    |
+------------------------------------------------+
|                                                |
|                 Record Data                    |
|                                                |
|                   Free Space                   |
|                                                |
+------------------------------------------------+
|               Slot Directory                   |
+------------------------------------------------+
```

The Page Manager is responsible for:

- Page initialization.
- Page validation.
- Record insertion.
- Record retrieval.
- Record deletion.
- Record updates.
- Slot management.
- Tombstone handling.
- Free-space accounting.
- Page compaction.
- Preservation of record identifiers where required.

The Page Manager does not perform direct database-file I/O.

All persistent page access passes through the Storage Manager.

Detailed design is documented in:

```text
docs/02_page_manager.md
```

## Buffer Pool

The Buffer Pool Manager is the next major persistence-layer component.

Its responsibility will be to provide an in-memory cache of database pages
between higher-level components and the Storage Manager.

Conceptually:

```text
              Higher-Level Components
                       |
                       v
                +-------------+
                | Buffer Pool |
                | Page Cache  |
                +------+------+
                       |
              +--------+--------+
              |                 |
              v                 v
          Cache Hit         Cache Miss
              |                 |
              |                 v
              |          StorageManager
              |                 |
              |                 v
              |            database.tdb
              |
              v
          Page Manager
```

The Buffer Pool will own page residency, frame management, eviction policy,
pinning/unpinning, and dirty-page tracking.

## Query Processing

The planned query-processing pipeline is:

```text
SQL
 |
 v
Parser
 |
 v
AST
 |
 v
Analyzer + Catalog
 |
 v
Planner / Optimizer
 |
 v
Execution Plan
 |
 v
Executor
 |
 +---------------+
 |               |
 v               v
Table Scan    Index Scan
 |               |
 +-------+-------+
         |
         v
     Buffer Pool
         |
         v
    Page Manager
         |
         v
   Storage Manager
         |
         v
    database.tdb
```

The query engine will be implemented after the foundational storage and memory
management layers are stable.

## Transactions, Concurrency, and Recovery

Transactions, concurrency control, and recovery are treated as cross-cutting
database services rather than simple sequential layers.

The intended architecture is:

```text
                 +----------------------+
                 | Transaction Manager  |
                 +----------+-----------+
                            |
                            v
                 +----------------------+
                 | Concurrency Control  |
                 +----------+-----------+
                            |
                            v
                      Query Execution
                            |
                            v
                       Buffer Pool
                            |
                            v
                 +----------------------+
                 |    WAL / Recovery    |
                 +----------+-----------+
                            |
                            v
                     Persistent Log
```

The recovery subsystem will use Write-Ahead Logging to establish the ordering
between durable log records and durable database pages.

These capabilities are planned for later development phases.

---

# Correctness and Reliability

Correctness is treated as a first-class engineering requirement.

The implementation emphasizes:

- Explicit API contracts.
- Input validation.
- Persistent metadata validation.
- Corruption detection.
- Deterministic page layout.
- Clear ownership and lifetime rules.
- Explicit error propagation.
- Protection against partial page I/O.
- Explicit durability boundaries.
- Page-level invariants.
- Failure-path testing.
- Regression testing for previously implemented behavior.

Storage operations return explicit status values rather than relying on
exceptions crossing the storage-engine boundary.

The current status model includes conditions such as:

- `OK`
- `InvalidArgument`
- `AlreadyExists`
- `NotFound`
- `Corruption`
- `IOError`
- `OutOfSpace`
- `NotOpen`

---

# Testing

Testing is developed alongside each subsystem rather than being postponed
until the end of the project.

The current test suite covers:

```text
Page Manager
    |
    +-- Page initialization
    +-- Page validation
    +-- Record insertion
    +-- Record retrieval
    +-- Record deletion
    +-- Record updates
    +-- Tombstone handling
    +-- Page compaction
    +-- Boundary / corruption cases

Storage Manager
    |
    +-- Database creation
    +-- Existing-file handling
    +-- Database opening
    +-- Database closing
    +-- Page allocation
    +-- Zero initialization
    +-- Page read/write
    +-- Persistence across reopen
    +-- Invalid arguments
    +-- Flush
    +-- Header corruption
```

Run the complete suite with:

```bash
ctest --test-dir build --output-on-failure
```

The current implementation passes:

```text
109 / 109 tests
```

---

# Build

TinyDB uses CMake and targets Modern C++17.

```bash
git clone <repository-url>
cd tinydb
mkdir -p build
cd build
cmake ..
cmake --build . -j"$(nproc)"
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

---

# Repository Structure

```text
tinydb/
|-- include/
|   +-- tinydb/
|       |-- buffer/
|       |-- catalog/
|       |-- common/
|       |-- concurrency/
|       |-- execution/
|       |-- index/
|       |-- parser/
|       |-- recovery/
|       |-- storage/
|       |   |-- page_id.h
|       |   |-- storage_manager.h
|       |   +-- ...
|       +-- transaction/
|
|-- src/
|   |-- buffer/
|   |-- catalog/
|   |-- common/
|   |-- concurrency/
|   |-- execution/
|   |-- index/
|   |-- parser/
|   |-- recovery/
|   |-- storage/
|   |   |-- page_manager.cpp
|   |   +-- storage_manager.cpp
|   +-- transaction/
|
|-- tests/
|   |-- buffer/
|   |-- catalog/
|   |-- concurrency/
|   |-- execution/
|   |-- index/
|   |-- parser/
|   |-- recovery/
|   |-- storage/
|   |   |-- page_manager_test.cpp
|   |   +-- storage_manager_test.cpp
|   +-- transaction/
|
|-- docs/
|-- diagrams/
|-- notes/
|-- benchmarks/
|-- examples/
|-- scripts/
|-- third_party/
|-- CMakeLists.txt
|-- README.md
+-- LICENSE
```

---

# Documentation

| Document | Description |
|----------|-------------|
| `docs/00_project_overview.md` | Overall architecture and project scope |
| `docs/01_storage_engine.md` | Storage engine design and persistence model |
| `docs/02_page_manager.md` | Page layout and record organization |
| `docs/03_buffer_pool.md` | Buffer Pool and page caching |
| `docs/04_bplus_tree.md` | B+ Tree index |
| `docs/05_query_engine.md` | Parsing, planning, and query execution |
| `docs/06_transactions.md` | Transaction management and concurrency control |
| `docs/07_recovery.md` | WAL and crash recovery |
| `docs/08_performance.md` | Performance analysis and optimization |

---

# Engineering Workflow

Each subsystem follows a repeatable engineering lifecycle:

```text
Understand
    |
    v
Architect
    |
    v
Define Interfaces
    |
    v
Define Invariants
    |
    v
Implement
    |
    v
Unit Test
    |
    v
Failure-Path Test
    |
    v
Integration Test
    |
    v
Benchmark
    |
    v
Document
    |
    v
Review
```

The goal is to establish correctness and clear contracts before introducing
performance optimizations or additional system complexity.

---

# Design Principles

**Separation of Responsibilities**
Each subsystem owns a clearly defined concern and exposes explicit interfaces
to adjacent components.

**Explicit Ownership**
Files, pages, frames, locks, and other resources have defined ownership and
lifetime rules.

**Correctness Before Optimization**
Correctness, invariants, and failure handling are established before
performance optimizations are introduced.

**Page-Oriented Storage**
Persistent database state is organized around fixed-size pages.

**Centralized Buffering**
Database-level page caching belongs to the Buffer Pool rather than the
Storage Manager.

**Explicit Durability**
Persistence and durability are separate concepts. Page writes can be batched,
while explicit flush operations establish durability boundaries.

**Testability**
Subsystems are designed so that their behavior and failure modes can be
validated independently where practical.

**Production-Oriented Engineering**
Design decisions consider real systems concerns including failure handling,
data corruption, durability, resource ownership, API contracts, concurrency
boundaries, observability, performance, and maintainability.

---

# Technology

| Area | Choice |
|------|--------|
| Language | Modern C++17 |
| Build System | CMake |
| Testing | GoogleTest |
| Platform | Linux / POSIX |
| Version Control | Git |

---

# Roadmap

| Phase | Component | Status |
|-------|-----------|--------|
| 0 | Development Environment | Complete |
| 1 | Repository & Architecture | Complete |
| 2 | Storage Engine | Complete |
| 3 | Page Manager | Complete |
| 4 | Buffer Pool Manager | Next |
| 5 | B+ Tree Index | Planned |
| 6 | SQL Parser | Planned |
| 7 | Query Planning & Execution | Planned |
| 8 | Transactions & Concurrency | Planned |
| 9 | WAL & Crash Recovery | Planned |
| 10 | Performance Engineering | Planned |

The roadmap is intentionally incremental. Higher-level database functionality
will be built on top of the storage and memory-management foundations rather
than implemented independently of them.

---

# Design References

The implementation is informed by established database and systems concepts
including:

- CMU 15-445 Database Systems.
- Database Internals — Alex Petrov.
- SQLite file-format and storage architecture.
- PostgreSQL storage and buffer-management architecture.
- POSIX file I/O primitives such as `open()`, `pread()`, `pwrite()`, and
  `fsync()`.

These references provide engineering guidance and architectural context.
TinyDB maintains its own implementation, interfaces, and on-disk format.

---

# Current Focus

```text
Storage Engine
      |
      v
Page Manager
      |
      v
Buffer Pool       <-- current focus
      |
      v
B+ Tree
      |
      v
Query Execution
      |
      v
Transactions
      |
      v
WAL / Recovery
      |
      v
Performance Engineering
```

The next milestone is the Buffer Pool Manager, which will establish the
in-memory page-management layer between higher-level database components and
persistent storage.

---

# Author

**Sai Krishna Varanasi**

C++ Systems Engineering | Linux | Database Internals | Backend Systems |
Performance Engineering
