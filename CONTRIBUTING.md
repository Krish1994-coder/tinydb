# Contributing

Thank you for your interest in TinyDB.

Although this project is primarily intended as a learning and portfolio project, all contributions are welcome.

---

# Development Workflow

Every module is developed using the following process:

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
```

No implementation should begin before the corresponding design has been documented.

---

# Coding Standards

- Modern C++17
- Follow the project's `.clang-format`
- Follow `.clang-tidy` recommendations
- Keep classes focused on a single responsibility
- Prefer RAII over manual resource management
- Avoid raw ownership where modern C++ alternatives exist
- Write readable, maintainable code

---

# Commit Message Convention

```text
chore: repository setup
docs: add storage engine design
feat(storage): implement StorageManager
test(storage): add StorageManager tests
refactor(buffer): simplify page replacement
perf(index): optimize B+ Tree search
fix(parser): handle malformed SQL
```

---

# Pull Request Checklist

Before submitting a pull request, ensure that:

- Code builds successfully
- All unit tests pass
- Formatting has been applied
- Documentation is updated
- Benchmarks are updated if performance is affected

---

# Philosophy

The primary objective of TinyDB is to understand database internals through careful engineering rather than reproducing existing implementations.

Every subsystem should be designed, implemented, tested, benchmarked, and documented before moving to the next.
