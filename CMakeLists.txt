cmake_minimum_required(VERSION 3.20)

project(TinyDB
    VERSION 0.1.0
    DESCRIPTION "A relational database engine built from scratch in C++17"
    LANGUAGES CXX
)

# ── Standards ────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Compiler warnings ────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wno-unused-parameter
)

# ── Include paths ────────────────────────────────────────────
include_directories(include)

# ── Google Test ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── TinyDB core library ──────────────────────────────────────
# Source files added explicitly as each module is implemented.
# Never use GLOB_RECURSE — it hides file additions from CMake.
add_library(tinydb_lib
    # Phase 3: src/storage/storage_manager.cpp
    # Phase 4: src/page/page.cpp
    # Phase 5: src/buffer/buffer_pool.cpp
)

target_include_directories(tinydb_lib PUBLIC include)

# ── Tests ────────────────────────────────────────────────────
enable_testing()
# Uncomment as each module is implemented:
# add_executable(test_storage tests/storage/test_storage_manager.cpp)
# target_link_libraries(test_storage tinydb_lib GTest::gtest_main)
# add_test(NAME StorageTests COMMAND test_storage)

