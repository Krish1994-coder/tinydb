# TinyDB
> A relational database engine built from scratch in Modern C++17.

TinyDB is an educational and portfolio project focused on understanding the
internal architecture and implementation of modern relational database
systems.

Rather than simply reproducing an existing implementation, the goal is to
design and implement each subsystem from first principles while studying the
engineering concepts behind production databases such as SQLite and
PostgreSQL.

The project follows an engineering-first approach:

```text
Architecture → Design → Implementation → Testing → Benchmarking → Documentation
```

---

# Goals

- Understand database storage internals.
- Learn page-based storage management.
- Design and implement a Storage Manager.
- Design and implement a Page Manager.
- Build a Buffer Pool Manager.
- Implement a B+ Tree index.
- Build a SQL parser.
- Implement query planning and execution.
- Add transaction management.
- Implement concurrency control.
- Implement Write-Ahead Logging (WAL) and crash recovery.
- Analyze and optimize database performance.
- Develop production-oriented systems programming skills using Modern C++.

---

# Project Roadmap

| Phase | Component | Status |
|------:|-----------|--------|
| Phase 0 | Development Environment | Completed |
| Phase 1 | Repository Setup | Completed |
| Phase 2 | Architecture & Design | In Progress |
| Phase 3 | Storage Engine Implementation | Planned |
| Phase 4 | Page Manager | Planned |
| Phase 5 | Buffer Pool Manager | Planned |
| Phase 6 | B+ Tree Index | Planned |
| Phase 7 | SQL Parser | Planned |
| Phase 8 | Query Planning & Execution | Planned |
| Phase 9 | Transactions & Concurrency Control | Planned |
| Phase 10 | Recovery & WAL | Planned |
| Phase 11 | Performance Engineering | Planned |

## Current Phase Status

```text
Phase 2 — Architecture & Design
│
├── Project Overview
│   └── ✓ Completed
│
├── Storage Engine Design
│   └── ✓ Completed
│
├── Storage Engine Learning Notes
│   └── ✓ Completed
│
├── Storage Manager Public Interface
│   └── ✓ Defined
│
├── Page Identifier
│   └── ✓ Defined
│
├── Storage Engine Architecture Diagram
│   └── ✓ Completed
│
└── Next
    └── Page Manager Design
```

The Storage Engine has been designed but has not yet been implemented.
Implementation begins after the Page Manager and related design work are
completed.

## High-Level Architecture

TinyDB uses a layered architecture in which query processing, data access,
memory management, and persistent storage have clearly separated
responsibilities.

The Catalog, Transaction Manager, Concurrency Control, and WAL operate as
supporting services rather than simple sequential layers.

```text
                     Client Application
                            │
                            ▼
                     +--------------+
                     |  Client API  |
                     +------+-------+
                            │
                            ▼
                     +--------------+
                     |    Parser    |
                     |   SQL → AST  |
                     +------+-------+
                            │
                            ▼
                +-------------------------+
                | Analyzer + Catalog      |
                | • Name resolution       |
                | • Type validation       |
                | • Metadata lookup       |
                +-----------+-------------+
                            │
                            ▼
                +-------------------------+
                | Planner / Optimizer     |
                | AST → Execution Plan    |
                +-----------+-------------+
                            │
                            ▼
                     +--------------+
                     |   Executor   |
                     +------+-------+
                            │
                +-----------+-----------+
                │                       │
                ▼                       ▼
         +-------------+         +-------------+
         | Table / Heap|         |   B+ Tree   |
         | Scan        |         |    Index    |
         +------+------+         +------+------+
                │                       │
                +-----------+-----------+
                            │
                            ▼
                     +--------------+
                     | Buffer Pool  |
                     |  Page Cache  |
                     +------+-------+
                            │
                            ▼
                     +--------------+
                     |   Storage    |
                     |   Manager    |
                     +------+-------+
                            │
                            ▼
                     +--------------+
                     | database.tdb |
                     +--------------+
```

## Query Processing Flow

A typical SQL query flows through TinyDB approximately as follows:

```text
Client Application
        │
        ▼
     Client API
        │
        ▼
      Parser
        │
        ▼
 Abstract Syntax Tree
        │
        ▼
 Analyzer + Catalog
        │
        ├── Validate table and column references
        ├── Resolve metadata and data types
        └── Identify available indexes
        │
        ▼
 Planner / Optimizer
        │
        └── Select an execution strategy
        │
        ▼
   Execution Plan
        │
        ▼
     Executor
        │
        ├───────────────────────┐
        │                       │
        ▼                       ▼
   Table / Heap            B+ Tree Index
       Scan                    Scan
        │                       │
        └───────────┬───────────┘
                    ▼
               Buffer Pool
                    │
             ┌──────┴──────┐
             │             │
             ▼             ▼
         Cache Hit     Cache Miss
             │             │
             │             ▼
             │      Storage Manager
             │             │
             │             ▼
             │        database.tdb
             │
             └─────────────► Executor
```

The Buffer Pool Manager keeps frequently accessed pages in memory.
When the required page is already cached, the executor can operate on the
in-memory page.

When the required page is not cached, the Buffer Pool requests the page from
the Storage Manager.

The Storage Manager performs page-granular file I/O against `database.tdb`.
It treats page contents as opaque bytes and does not interpret records,
indexes, or SQL.

The Page Manager is responsible for interpreting the layout of an individual
page and managing records and free space within that page.

## Storage Architecture

The Buffer Pool and Storage Manager form the persistent page-I/O path.
The Page Manager is responsible for interpreting the contents of pages that
have been brought into memory by the Buffer Pool. It is not a replacement
for the Storage Manager and it does not perform database-file I/O.

```text
                    Executor / Index
                           │
                           ▼
                    +--------------+
                    | Buffer Pool  |
                    |  Page Cache  |
                    +------+-------+
                           │
              ┌────────────┴────────────┐
              │                         │
              ▼                         ▼
       Page already cached        Page not cached
              │                         │
              │                         ▼
              │                 +---------------+
              │                 | Storage       |
              │                 | Manager       |
              │                 +-------+-------+
              │                         │
              │                         ▼
              │                   database.tdb
              │
              ▼
       +--------------+
       | Page Manager |
       | Page Layout  |
       +------+-------+
              │
              ▼
       Page Header / Slots / Records
```

### Storage Manager

The Storage Manager is responsible for:

- Database file creation, opening, and closing.
- Page allocation.
- Page-granular reads.
- Page-granular writes.
- Page count management.
- Flushing pending writes using `fsync`.

The Storage Manager does not understand records, tables, indexes, SQL, or
query plans.

### Page Manager

The Page Manager is responsible for the internal organization of data inside
individual pages.

The initial design is expected to use a slotted-page organization for storing
variable-length records and managing free space within a fixed-size page.

### Buffer Pool Manager

The Buffer Pool Manager provides the in-memory page cache between higher-level
database components and persistent storage.

## Cross-Cutting Services

Some database services do not form a simple vertical layer. They interact
with multiple components.

```text
        +----------------------+
        | Transaction Manager  |
        +----------+-----------+
                   │
                   ▼
        +----------------------+
        | Lock / Concurrency   |
        | Control              |
        +----------+-----------+
                   │
                   ▼
             Query Execution
             / Buffer Pool
        +----------------------+
        |    WAL / Log Manager |
        +----------+-----------+
                   │
                   ▼
             Persistent Log
```

These services will be designed and implemented in later phases.

---

# Storage Engine Design

The initial database format uses a single binary file:

```text
database.tdb

+---------------------------+
| Page 0 — File Header      |
| Magic                     |
| Format Version            |
| Page Size                 |
| Page Count                |
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

The initial page size is designed around 4 KiB pages.

The Storage Manager locates a page using:

```text
offset = page_id × page_size
```

The Storage Manager exposes page-level operations such as:

- `CreateDatabase()`
- `OpenDatabase()`
- `CloseDatabase()`
- `AllocatePage()`
- `ReadPage()`
- `WritePage()`
- `Flush()`
- `GetPageCount()`
- `GetPageSize()`
- `IsOpen()`

The public interface is defined in:

```text
include/tinydb/storage/storage_manager.h
```

The page identifier definition is located in:

```text
include/tinydb/storage/page_id.h
```

---

# Example Query Execution

Consider:

```sql
SELECT id, name
FROM accounts
WHERE id = 42;
```

The conceptual execution path is:

```text
Client Application
        │
        ▼
     Client API
        │
        ▼
      Parser
        │
        ▼
 Analyzer + Catalog
        │
        ├── accounts exists?
        ├── id exists?
        ├── name exists?
        └── What indexes are available?
        │
        ▼
     Planner
        │
        └── Select IndexScan if appropriate
        │
        ▼
     Executor
        │
        ▼
   B+ Tree Index
        │
        ▼
    Buffer Pool
        │
   ┌────┴────┐
   │         │
   ▼         ▼
Cache Hit  Cache Miss
   │         │
   │         ▼
   │   Storage Manager
   │         │
   │         ▼
   │    database.tdb
   │
   └────────► Executor
```

The important distinction is:

- **Planner** decides *WHAT* should happen.
- **Executor** performs the chosen operations.
- **Buffer Pool** decides *WHETHER* the required page is already in memory.
- **Storage Manager** performs persistent page I/O when required.
- **Page Manager** understands *HOW* data is organized inside a page.

---

# Transactions, Concurrency, and WAL

These components are intentionally treated as cross-cutting services.

### Transaction Manager

Responsible for:

- `BEGIN`
- `COMMIT`
- `ROLLBACK`
- Transaction state
- Transaction lifecycle

### Concurrency Control

Responsible for coordinating concurrent operations.

Future responsibilities include:

- Locks or latches where appropriate.
- Transaction isolation.
- Safe concurrent access.
- Deadlock handling where applicable.

The initial implementation prioritizes correctness over advanced concurrency.

### WAL / Recovery

The WAL subsystem will provide crash recovery.

```text
Transaction
     │
     ▼
Change
     │
     ▼
WAL / Log Manager
     │
     ▼
Persistent Log
```

The core durability principle will be based on Write-Ahead Logging:

> Log record must reach durable storage  
> before the corresponding dirty database page  
> is allowed to become the durable representation  
> of that change.

Detailed WAL and recovery design will be introduced in a later phase.

---

# Repository Structure

```text
tinydb/
├── include/
│   └── tinydb/
│       ├── buffer/
│       ├── catalog/
│       ├── common/
│       ├── concurrency/
│       ├── execution/
│       ├── index/
│       ├── parser/
│       ├── recovery/
│       ├── storage/
│       │   ├── page_id.h
│       │   └── storage_manager.h
│       └── transaction/
├── src/
│   ├── buffer/
│   ├── catalog/
│   ├── common/
│   ├── concurrency/
│   ├── execution/
│   ├── index/
│   ├── parser/
│   ├── recovery/
│   ├── storage/
│   └── transaction/
├── tests/
│   ├── buffer/
│   ├── catalog/
│   ├── concurrency/
│   ├── execution/
│   ├── index/
│   ├── parser/
│   ├── recovery/
│   ├── storage/
│   └── transaction/
├── docs/
├── diagrams/
├── notes/
├── benchmarks/
├── examples/
├── scripts/
├── third_party/
├── CMakeLists.txt
├── README.md
└── LICENSE
```

---

# Build

TinyDB uses CMake as its build system.

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

The project targets Modern C++17.

---

# Testing

Testing will be introduced alongside each subsystem rather than postponed
until the end of the project.

```text
Implementation
      ↓
Unit Tests
      ↓
Integration Tests
      ↓
Correctness Validation
      ↓
Benchmarking
```

Storage-related tests will live under:

```text
tests/storage/
```

---

# Documentation

| Document | Description |
|----------|-------------|
| `docs/00_project_overview.md` | Overall project scope and architecture |
| `docs/01_storage_engine.md` | Storage Engine design and public interface |
| `docs/02_page_manager.md` | Page layout and record organization |
| `docs/03_buffer_pool.md` | Buffer Pool and page caching |
| `docs/04_bplus_tree.md` | B+ Tree index |
| `docs/05_query_engine.md` | Parsing, planning, and query execution |
| `docs/06_transactions.md` | Transaction management and concurrency control |
| `docs/07_recovery.md` | WAL and crash recovery |
| `docs/08_performance.md` | Performance analysis and optimization |

---

# Learning Notes

The `notes/` directory contains personal study material used during the
design process.

```text
notes/
└── storage_engine_notes.md
```

These notes are intentionally separated from the formal engineering
documentation under `docs/`.

---

# Engineering Workflow

Every subsystem follows the same development lifecycle:

```text
Learn
  │
  ▼
Understand the problem and underlying system concepts
  │
  ▼
Architect
  │
  ▼
Define component boundaries and interactions
  │
  ▼
Design
  │
  ▼
Define interfaces, data structures, invariants, and failure modes
  │
  ▼
Implement
  │
  ▼
Test
  │
  ▼
Benchmark
  │
  ▼
Document
  │
  ▼
Review
  │
  ▼
Commit
```

---

# Design Principles

- **Separation of Responsibilities** — each subsystem owns a clearly defined concern.
- **Explicit Interfaces** — components communicate through well-defined APIs.
- **Page-Oriented Storage** — persistent database state is organized around fixed-size pages.
- **Centralized Buffering** — the Buffer Pool owns database-level page caching.
- **Correctness Before Optimization** — establish correct behavior and invariants before introducing advanced optimizations.
- **Clear Ownership** — resources such as files, pages, frames, and locks have explicit ownership and lifetime rules.
- **Testability** — subsystems should be independently testable where practical.
- **Production-Oriented Design** — design decisions are compared with concepts used by systems such as SQLite and PostgreSQL.

---

# Technology

| Area | Choice |
|------|--------|
| Language | Modern C++17 |
| Build System | CMake |
| Testing | Google Test |
| Platform | Linux / POSIX-oriented development |
| Version Control | Git |

---

# Design References

- **CMU 15-445 Database Systems** — Database storage, buffer pools, indexing, execution, and transactions
- **Database Internals — Alex Petrov** — Storage engines, B+ Trees, storage layouts, and database internals
- **SQLite File Format** — Concrete reference for page-oriented database storage
- **PostgreSQL** — Reference for production-oriented storage and database architecture
- **POSIX APIs** — `open()`, `pread()`, `pwrite()`, `fsync()`

---

# Future Areas

The following areas are intentionally deferred until the foundational
components are stable:

- Advanced Query Optimization
- Distributed Execution
- Replication
- Networking
- Authentication
- Multi-node Storage
- Advanced Recovery
- Advanced Concurrency
- Async I/O
- Direct I/O
- Memory Mapping
- Performance Tuning

---

# Next Milestone

The next subsystem is the **Page Manager**.

The Page Manager will define how records are organized inside a fixed-size
page.

The intended relationship is:

```text
                Buffer Pool
                     │
                     │ page memory
                     ▼
                Page Manager
                     │
         +-----------+-----------+
         │                       │
         ▼                       ▼
    Page Header             Slot Directory
         │                       │
         +-----------+-----------+
                     │
                     ▼
                Record Data
```

The Buffer Pool obtains pages through the Storage Manager when necessary.
The Page Manager interprets the contents of those pages.

The next design document is:

```text
docs/02_page_manager.md
```

---

# Author

**Sai Krishna Varanasi**  
Senior C++ Systems Engineer

Modern C++ • Linux Systems Programming • Database Internals • Distributed Systems • Performance Engineering
