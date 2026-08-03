# TinyDB

> A relational database engine built from scratch in Modern C++17.

TinyDB is an educational and portfolio project focused on understanding the internal architecture of modern relational database systems.

Rather than simply reproducing an existing implementation, the goal is to design and implement each subsystem from first principles while studying the engineering concepts behind production databases such as SQLite and PostgreSQL.

This project follows an engineering-first approach:

Architecture → Design → Implementation → Testing → Benchmarking → Documentation

---

# Goals

- Understand database storage internals
- Learn page-based storage management
- Build a buffer pool manager
- Implement a B+ Tree index
- Build a SQL parser
- Implement a query execution engine
- Add transaction management
- Implement Write-Ahead Logging (WAL)
- Analyze and optimize performance

---

# Project Roadmap

| Phase | Component | Status |
|--------|-----------|--------|
| Phase 0 | Development Environment | ✅ Completed |
| Phase 1 | Repository Setup | ✅ Completed |
| Phase 2 | Architecture & Design | 🔄 In Progress |
| Phase 3 | Storage Engine | ⏳ Planned |
| Phase 4 | Page Manager | ⏳ Planned |
| Phase 5 | Buffer Pool | ⏳ Planned |
| Phase 6 | B+ Tree Index | ⏳ Planned |
| Phase 7 | SQL Parser | ⏳ Planned |
| Phase 8 | Query Execution | ⏳ Planned |
| Phase 9 | Transactions | ⏳ Planned |
| Phase 10 | Recovery (WAL) | ⏳ Planned |
| Phase 11 | Performance Engineering | ⏳ Planned |

---

# High-Level Architecture

```text
                SQL Query
                    │
                    ▼
             +--------------+
             |    Parser    |
             +--------------+
                    │
                    ▼
             +--------------+
             |   Planner    |
             +--------------+
                    │
                    ▼
             +--------------+
             |  Executor    |
             +--------------+
                    │
                    ▼
             +--------------+
             |   Catalog    |
             +--------------+
                    │
                    ▼
             +--------------+
             | Buffer Pool  |
             +--------------+
                    │
                    ▼
             +--------------+
             |   Storage    |
             +--------------+
                    │
                    ▼
                 Disk File
```

---

# Repository Structure

```text
tinydb/
├── include/
├── src/
├── tests/
├── docs/
├── diagrams/
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

```bash
mkdir build
cd build

cmake ..

cmake --build .
```

---

# Documentation

| Document | Description |
|----------|-------------|
| 00_project_overview.md | Overall project architecture |
| 01_storage_engine.md | Storage engine design |
| 02_page_manager.md | Page layout |
| 03_buffer_pool.md | Buffer pool |
| 04_bplus_tree.md | Index implementation |
| 05_query_engine.md | Query execution |
| 06_transactions.md | Transaction manager |
| 07_recovery.md | WAL & recovery |
| 08_performance.md | Performance analysis |

---

# Engineering Workflow

Every subsystem follows the same development process.

```text
Learn
    ↓
Design
    ↓
Architecture Diagram
    ↓
Implementation
    ↓
Unit Tests
    ↓
Benchmark
    ↓
Documentation
    ↓
Git Commit
```

---

# Author

**Sai Krishna Varanasi**

Senior C++ Systems Engineer

Modern C++ • Linux Systems Programming • Database Internals • Distributed Systems • Performance Engineering
