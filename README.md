# concurrency

A high-performance C++26 work-stealing task system with custom synchronization primitives.

## Header-only library

| File | Description |
|---|---|
| `custom_locks.hpp` | `spin_mutex` and `ticket_mutex` with `try_lock()` support. Uses C++20 `atomic::wait`/`notify` and `hardware_destructive_interference_size` for cache-line padding. C++26 `std::is_sufficiently_aligned` validates alignment at construction. |
| `task_system.hpp` | Work-stealing task system with `async`, `async_with_future`, `wait_all_tasks`, `sync_point`, `stop`, and `clear`. Per-thread notification queues with non-blocking `try_push`/`try_pop` for fast work-stealing, falling back to blocking `push`/`pop` with `std::condition_variable_any`. Task parameters are captured via `std::tuple` + `std::apply`. |

## API

```cpp
task_system ts;

// Fire-and-forget with arguments
ts.async([](int a, int b) { return a + b; }, 10, 20);

// Fire-and-forget with future
auto future = ts.async_with_future([] { return 42; });
int result = future.get(); // blocks until ready

// Synchronization
ts.sync_point();       // wait for all tasks submitted before this call
ts.wait_all_tasks();   // wait for ALL submitted tasks (stricter)

// Lifecycle
ts.clear();            // drop pending (unstarted) tasks
ts.stop();             // request worker threads to stop
// ~task_system() joins all workers
```

## Building

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build

./build/tests      # GoogleTest suite (32 tests)
./build/benchmarks # throughput + latency benchmarks
./build/main       # demo
```

Requires: C++26 (`-std=c++26`), CMake 3.20+, GCC 16+ or Clang 19+.

## Design

Based on [Sean Parent's "Better Code: Concurrency"](https://www.youtube.com/watch?v=zULU6Hhp42w) with custom synchronization primitives from [CppCon 2019](https://github.com/CppCon/CppCon2019/blob/master/Presentations/cpp20_synchronization_library/cpp20_synchronization_library__r2__bryce_adelstein_lelbach__cppcon_2019.pdf).

Notable differences from the reference:
- `spin_mutex` instead of `std::mutex` for queue locks
- `std::condition_variable_any` (was `std::binary_semaphore` originally, changed to fix a race condition)
- `std::move_only_function` instead of `std::function` (avoids heap allocation for small callables)
- `std::jthread` with `std::stop_token` for cooperative shutdown
- Exception-safe task execution (failed tasks don't kill worker threads)
- `try_lock()` on custom mutexes (required for non-blocking queue operations)
- Memory ordering: `release` on task submission/completion, `acquire` on synchronization points
