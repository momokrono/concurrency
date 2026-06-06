cmake_minimum_required(VERSION 3.20)

project(Concurrency)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-interference-size")

# --- Library Definition ---
# Define a header-only INTERFACE library. This allows other targets (like the
# executables below, or external projects) to "link" to it to get its properties,
# like include directories.
add_library(concurrency INTERFACE)
add_library(concurrency::concurrency ALIAS concurrency) # Modern namespaced alias

# For any target that links to "concurrency", add the current source directory
# to its include paths.
target_include_directories(concurrency INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


# --- Dependencies ---
add_subdirectory(extern/googletest)
add_subdirectory(extern/fmt)


# --- Executable Targets ---
add_executable(
    main
    main.cpp
)

add_executable(
    tests
    tests.cpp
)

add_executable(
    benchmarks
    benchmarks.cpp
)

add_executable(
    baseline_bench
    baseline_bench.cpp
)

# --- Link Libraries ---
# Link the executables against the new library target.
# This automatically handles include paths.
target_link_libraries(
    main
    concurrency::concurrency
    fmt::fmt
)

target_link_libraries(
    tests
    concurrency::concurrency
    GTest::gtest_main
    pthread
    fmt::fmt
)

target_link_libraries(
    benchmarks
    concurrency::concurrency
    pthread
    fmt::fmt
)

target_link_libraries(
    baseline_bench
    concurrency::concurrency
    pthread
    fmt::fmt
)
