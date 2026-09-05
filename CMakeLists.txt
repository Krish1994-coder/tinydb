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
    # StorageManager
    src/storage/storage_manager.cpp

    # Phase 4: src/page/page.cpp
    # Phase 5: src/buffer/buffer_pool.cpp

    # PageManager
    src/storage/page_manager.cpp
)

target_include_directories(tinydb_lib PUBLIC include)

# ── Tests ────────────────────────────────────────────────────
enable_testing()

# PageManager tests
add_executable(test_page_manager
    tests/storage/page_manager_test.cpp
)

target_link_libraries(test_page_manager
    PRIVATE tinydb_lib
    PRIVATE GTest::gtest_main
)

target_compile_features(test_page_manager PRIVATE cxx_std_17)

include(GoogleTest)
gtest_discover_tests(test_page_manager)

# StorageManager tests
add_executable(test_storage_manager
    tests/storage/storage_manager_test.cpp
)

target_link_libraries(test_storage_manager
    PRIVATE tinydb_lib
    PRIVATE GTest::gtest_main
)

target_compile_features(test_storage_manager PRIVATE cxx_std_17)

gtest_discover_tests(test_storage_manager)
