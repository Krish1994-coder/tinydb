# Project Overview

## What is TinyDB?

TinyDB is a relational database engine built from scratch in C++17.

The goal is not to replace PostgreSQL or SQLite.
The goal is to understand how a database actually works internally —
by designing and implementing each component from first principles.

---

## Why I Am Building This

Every system I build professionally sits on top of a database.
I have been using them for years without understanding what happens
below SQL.

I could not confidently answer questions like:

- Why does a database use fixed-size pages instead of storing records
  directly on disk?
- What actually happens at the byte level when you call COMMIT?
- How does an index know which page to look in for a specific key?
- What does crash recovery mean in practice?
- Why does concurrent access require a lock manager?

TinyDB is a structured way to explore and understand those questions
through design, implementation, and experimentation.

---

## Scope

### What TinyDB will support in version 1.0

```sql
CREATE TABLE accounts (
    id      INTEGER PRIMARY KEY,
    name    VARCHAR(100),
    balance DECIMAL(15,2)
);

INSERT INTO accounts VALUES (1, 'Sai Krishna', 50000.00);

SELECT id, name, balance
FROM   accounts
WHERE  id = 1;

UPDATE accounts SET balance = 60000.00 WHERE id = 1;

DELETE FROM accounts WHERE id = 1;

BEGIN;
    UPDATE accounts SET balance = balance - 10000 WHERE id = 1;
    UPDATE accounts SET balance = balance + 10000 WHERE id = 2;
COMMIT;
```

### What is deliberately out of scope for version 1.0

- Distributed execution
- Replication
- Cost-based query optimization
- Full SQL compliance (window functions, CTEs, subqueries)
- Network protocol — TinyDB is an embedded engine, not a server
- Authentication and user management

These are real database features. They are excluded not because they
are unimportant, but because implementing everything at once means
understanding nothing properly. Each excluded feature is a candidate
for a future phase.

---

## Architecture

```
                    Application
                         │
                         ▼
                 ┌─────────────────┐
                 │   Client API    │
                 └────────┬────────┘
                          │
                          ▼
                 ┌─────────────────┐
                 │     Parser      │
                 └────────┬────────┘
                          │
                          ▼
                 ┌─────────────────┐
                 │     Planner     │
                 └────────┬────────┘
                          │
                          ▼
                 ┌─────────────────┐
                 │    Executor     │
                 └────────┬────────┘
                          │
                          ▼
                 ┌─────────────────┐
                 │   Buffer Pool   │
                 └────────┬────────┘
                          │
                          ▼
                 ┌─────────────────┐
                 │ Storage Engine  │
                 └────────┬────────┘
                          │
                          ▼
                      tiny.db
```

Each layer owns a single responsibility and communicates only with
the layer directly below it. This separation keeps the system modular,
testable, and easier to evolve over time.

Future versions may introduce a dedicated query optimizer between the
planner and executor once the core execution pipeline is complete.

---

## Roadmap

| Phase | Component | Status |
|------:|-----------|--------|
| 0 | Development Environment | ✅ Complete |
| 1 | Repository Setup | ✅ Complete |
| 2 | Architecture & Documentation | 🔄 In Progress |
| 3 | Storage Engine | ⏳ Planned |
| 4 | Page Manager | ⏳ Planned |
| 5 | Buffer Pool | ⏳ Planned |
| 6 | B+ Tree Index | ⏳ Planned |
| 7 | Catalog | ⏳ Planned |
| 8 | SQL Parser | ⏳ Planned |
| 9 | Query Planner | ⏳ Planned |
| 10 | Query Executor | ⏳ Planned |
| 11 | Transaction Manager | ⏳ Planned |
| 12 | Concurrency Control | ⏳ Planned |
| 13 | Recovery (WAL) | ⏳ Planned |
| 14 | Performance Engineering | ⏳ Planned |

---

## Component Responsibilities

### Client API
The entry point into TinyDB. Accepts SQL strings and exposes a
clean interface for execution. Responsible for the boundary between
the caller and the database internals.

### Parser
Converts a SQL string into an Abstract Syntax Tree. Responsible
for syntactic correctness only — it does not validate whether
tables or columns exist.

*Design document: 05_query_engine.md*

### Planner
Receives the AST, performs semantic validation against the catalog,
resolves table and column references, and produces a query plan.
The planner is responsible for both logical correctness and
producing a plan the executor can run.

*Design document: 05_query_engine.md*

### Executor
Executes the query plan by pulling data through a tree of operators.
Responsible for producing the final result rows and returning them
to the caller.

*Design document: 05_query_engine.md*

### Buffer Pool
Responsible for reducing disk access latency by caching frequently
accessed pages in memory. Manages page state throughout the cache
lifecycle and handles eviction when the cache reaches capacity.

*Design document: 03_buffer_pool.md*

### Storage Engine
Manages the database file on disk. Responsible for reading and
writing pages by ID. Does not know about tables, records, or SQL.

*Design document: 01_storage_engine.md*

### Page Manager
Defines how records are organized inside a page. Responsible for
inserting, reading, updating, and deleting variable-length records
within a single page, and for managing free space within that page.
The initial design is expected to use a slotted-page organization.

*Design document: 02_page_manager.md*

### B+ Tree Index
Provides efficient key-based lookup and range scan over table data.
Responsible for maintaining a balanced tree structure as records
are inserted and deleted.

*Design document: 04_bplus_tree.md*

### Catalog
Manages metadata about tables, columns, and indexes. Every query
passes through the catalog so that table and column references can
be validated and resolved before execution begins.

### Transaction Manager
Responsible for ensuring that a group of operations either all
complete or have no effect. Implements BEGIN, COMMIT, and ROLLBACK.

*Design document: 06_transactions.md*

### Concurrency Control
Responsible for allowing multiple threads to read and write safely.
The initial implementation focuses on correctness before advanced
concurrency features.

### Recovery
Responsible for ensuring that committed transactions survive system
crashes. Before any page is modified, the modification is recorded
in a log. On restart after a crash, that log is used to bring the
database back to a consistent state.

*Design document: 07_recovery.md*

---

## Engineering Workflow

Every subsystem follows the same development lifecycle.

```
Read
    Understand the underlying concepts

        │
        ▼

Design
    Write the design document

        │
        ▼

Diagram
    Create the architecture diagram

        │
        ▼

Implement
    Develop the feature

        │
        ▼

Test
    Write and execute unit tests

        │
        ▼

Benchmark
    Measure and analyze performance

        │
        ▼

Document
    Record design decisions and trade-offs

        │
        ▼

Commit
    Maintain a clean and meaningful Git history
```

No implementation begins before its design has been documented.
This keeps the project understandable, maintainable, and easier to
extend as additional components are introduced.

---

## Technology Choices

### Modern C++17

TinyDB is implemented in Modern C++17 because it provides the language
features and performance characteristics expected in systems software
such as storage engines, database kernels, and low-latency services.

### CMake

CMake provides a portable and maintainable build system that supports
multiple compilers and development environments.

### Google Test

Google Test provides a structured framework for unit testing,
regression testing, and automated verification of individual
components.

### Page-Based Storage

The storage layer is organized around fixed-size pages. This simplifies
page management, caching, and interaction with the underlying storage
subsystem. Specific page layout decisions are documented separately in
the Storage Engine design document.

### B+ Tree Index

B+ Trees efficiently support both point lookups and range queries,
making them a practical indexing structure for relational databases.

---

## References

- CMU 15-445 Database Systems — Andy Pavlo
- Database Internals — Alex Petrov
- Database System Concepts — Silberschatz, Korth, Sudarshan
- Architecture of a Database System — Hellerstein, Haas, Hamilton
- SQLite Source Code
- PostgreSQL Documentation
- OSTEP (Operating Systems: Three Easy Pieces)

---

*Current Status: Phase 2 — Architecture & Documentation*

