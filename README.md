# concurrency

A high-performance C++26 work-stealing task system with custom synchronization primitives.

Header-only. No dependencies. Drop `include/` into your project and go.

## Quick Start

```cpp
#include <concurrency/task_system.hpp>

int main() {
    concurrency::task_system ts;  // uses all cores

    // Fire-and-forget
    ts.async([] { heavy_work(); });

    // With future
    auto f = ts.async_with_future([] { return compute(); });
    int result = f.get();

    // Synchronize
    ts.sync_point();       // all tasks submitted before this call
    ts.wait_all_tasks();   // every task, including later submissions
}
```

## API

```cpp
namespace concurrency {

class task_system {
public:
    task_system(unsigned threads = std::thread::hardware_concurrency());
    ~task_system();  // joins all workers

    // Submit
    void async(F&& f, Args&&... args);          // fire-and-forget
    bool try_async(F&& f, Args&&... args);      // returns false if queues full
    auto async_with_future(F&& f, Args&&...);  // → std::future<R>

    // Synchronize
    void sync_point();       // wait for tasks submitted before this call
    void wait_all_tasks();   // wait for all tasks (including during wait)

    // Observe
    unsigned active_tasks() const;      // currently executing
    unsigned long pending_tasks() const; // submitted minus completed
    unsigned worker_count() const;      // thread count

    // Control
    void stop();             // request workers to stop
    size_t clear();          // drop queued tasks, returns count
};

struct spin_mutex;     // park-on-contention, atomic_flag::wait
struct ticket_mutex;   // FIFO fair, atomic::wait

}
```

## Building

### CMake (as dependency)
```cmake
include(FetchContent)
FetchContent_Declare(concurrency
    GIT_REPOSITORY https://github.com/momokrono/concurrency
    GIT_TAG master)
FetchContent_MakeAvailable(concurrency)
target_link_libraries(my_app concurrency::concurrency)
```

### Direct include
```bash
g++ -std=c++26 -Ipath/to/concurrency/include my_app.cpp -lpthread
```

### Run tests
```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
./build/tests/tests    # 40 tests, TSAN-clean
```

## Examples

```bash
# Mandelbrot renderer — one async per row
g++ -std=c++26 -O3 -o mandelbrot examples/mandelbrot.cpp -Iinclude -lpthread
./mandelbrot > image.pgm
```

## Design

Based on [Sean Parent's "Better Code: Concurrency"](https://www.youtube.com/watch?v=zULU6Hhp42w) with custom locks from [CppCon 2019](https://github.com/CppCon/CppCon2019/tree/master/Presentations/cpp20_synchronization_library).

Key features:
- Work-stealing across per-thread notification queues
- `std::atomic::wait`/`notify` (C++20) — no busy-spinning
- `std::move_only_function` — no heap allocation for small callables
- `std::jthread` with `std::stop_token` — cooperative shutdown
- Exception-safe — failed tasks don't kill workers
- Adaptive probe windows — 6×_count probes when busy, 2× after idle
- C++26 `std::is_sufficiently_aligned` validation in `ticket_mutex`

Requires: C++23 (`std::move_only_function`), C++26 for alignment checks. GCC 16+ or Clang 19+.
