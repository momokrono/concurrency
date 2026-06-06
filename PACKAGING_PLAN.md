# Library Packaging Plan

## Current State

Two header-only files in a repo mixed with build infrastructure:

```
concurrency/
├── task_system.hpp       ← task_system + notification_queue + spin_mutex
├── custom_locks.hpp      ← spin_mutex + ticket_mutex
├── tests.cpp             ← 40 GoogleTest cases
├── benchmarks.cpp        ← original benchmarks
├── baseline_bench.cpp    ← comprehensive benchmark suite
├── main.cpp              ← demo
├── CMakeLists.txt        ← mixed: lib + tests + benchmarks + googletest + fmt
├── extern/googletest/    ← submodule
├── extern/fmt/           ← submodule
└── README.md
```

## Target Structure

```
concurrency/
├── include/concurrency/
│   ├── locks.hpp             ← spin_mutex, ticket_mutex (standalone, no deps)
│   └── task_system.hpp       ← task_system (depends on locks.hpp)
├── tests/
│   ├── CMakeLists.txt        ← gtest optional (BUILD_TESTING)
│   └── tests.cpp
├── benchmarks/
│   ├── CMakeLists.txt
│   ├── benchmarks.cpp
│   └── baseline_bench.cpp
├── examples/
│   └── mandelbrot.cpp        ← your actual use case
├── CMakeLists.txt            ← library target only, install rules
└── README.md
```

## CMakeLists.txt (library-only)

```cmake
cmake_minimum_required(VERSION 3.20)
project(concurrency VERSION 1.0.0 LANGUAGES CXX)

add_library(concurrency INTERFACE)
add_library(concurrency::concurrency ALIAS concurrency)
target_include_directories(concurrency INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(concurrency INTERFACE cxx_std_20)

install(TARGETS concurrency EXPORT concurrency-targets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT concurrency-targets DESTINATION lib/cmake/concurrency)

option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
```

## Missing Features to Add

### 1. Namespace

Everything should be under `namespace concurrency`:

```cpp
namespace concurrency {
    struct spin_mutex { ... };
    struct ticket_mutex { ... };
    class task_system { ... };
}
```

### 2. Observability API

Currently zero runtime visibility. Add:

```cpp
class task_system {
public:
    // How many tasks are currently executing?
    auto active_tasks() const noexcept -> unsigned {
        return _active_tasks.load(std::memory_order_relaxed);
    }

    // How many submitted but not yet completed?
    auto pending_tasks() const noexcept -> unsigned long {
        unsigned long sub = _submitted_tasks.load(std::memory_order_acquire);
        unsigned long comp = _completed_tasks.load(std::memory_order_acquire);
        return sub > comp ? sub - comp : 0;
    }

    // How many worker threads?
    auto thread_count() const noexcept -> unsigned { return _count; }
};
```

### 3. Backpressure: `try_async()`

`async()` always succeeds. Add a version that returns false when queues are full:

```cpp
// Returns true if enqueued, false if all queues are full.
// Caller must handle the rejected task.
auto try_async(F&& f, Args&&... args) noexcept -> bool {
    _submitted_tasks.fetch_add(1, std::memory_order_release);
    auto task = /* wrap f+args */;
    auto i = _index++;
    for (unsigned n = 0; n != _count * 8; ++n) {
        if (_q[(i + n) % _count].try_push(std::move(task))) return true;
    }
    _submitted_tasks.fetch_sub(1, std::memory_order_release); // undo
    return false;
}
```

### 4. Runtime resize

```cpp
// Grow or shrink the worker pool. Growing adds threads immediately.
// Shrinking requests stop on excess threads.
void resize(unsigned thread_count);
```

### 5. Separate locks header

`spin_mutex` and `ticket_mutex` should be usable without pulling in `<deque>`, `<thread>`, `<future>`, etc. They only need `<atomic>`, `<cassert>`, `<memory>`.

`task_system.hpp` would `#include <concurrency/locks.hpp>` internally.

## Consumer Usage

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(concurrency
    GIT_REPOSITORY https://github.com/momokrono/concurrency
    GIT_TAG main)
FetchContent_MakeAvailable(concurrency)
target_link_libraries(my_app concurrency::concurrency)
```

### Direct include

```cpp
#include <concurrency/task_system.hpp>

concurrency::task_system ts;
ts.async([]{ do_work(); });
ts.wait_all_tasks();
```

### Mandelbrot example

```cpp
#include <concurrency/task_system.hpp>
#include <vector>

concurrency::task_system ts;
std::vector<std::future<Color>> rows(height);

for (int y = 0; y < height; ++y)
    rows[y] = ts.async_with_future([=] {
        return compute_row(y, width, x_min, x_max, y_min, y_max, max_iter);
    });

ts.sync_point();  // all rows done

for (auto& f : rows)
    draw_row(f.get());
```

## Implementation Order

1. Create `include/concurrency/` directory, move headers with namespace
2. Split `custom_locks.hpp` → `include/concurrency/locks.hpp` (no deps on task system)
3. Update `task_system.hpp` to include `<concurrency/locks.hpp>`, add namespace
4. Rewrite `CMakeLists.txt` with install targets, `BUILD_TESTING` option
5. Create `tests/CMakeLists.txt`, update test includes
6. Add observability API (`active_tasks`, `pending_tasks`, `thread_count`)
7. Add `try_async()` with backpressure
8. Add `resize()` for runtime thread count changes
9. Create `examples/mandelbrot.cpp`
10. Update `README.md` for consumers
