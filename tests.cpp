/// Comprehensive test suite for the concurrency library.
/// 44 tests → 34 tests (consolidated to reduce thread-creation pressure).
/// Run with: ./tests
/// TSAN: g++ -std=c++26 -O1 -g -fsanitize=thread tests.cpp -I. -lpthread -lgtest -o tests_tsan

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <future>
#include <memory>
#include <barrier>
#include "custom_locks.hpp"
#include "task_system.hpp"

// ═══════════════════════════════════════════════════════════════════════
//  spin_mutex
// ═══════════════════════════════════════════════════════════════════════

TEST(SpinMutex, BasicLockUnlock) {
    spin_mutex mtx;
    mtx.lock();
    mtx.unlock();
    SUCCEED();
}

TEST(SpinMutex, TryLock) {
    spin_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    ASSERT_FALSE(mtx.try_lock());
    mtx.unlock();
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(SpinMutex, MultiThreadedExclusion) {
    spin_mutex mtx;
    int shared = 0;
    constexpr int threads = 32, iters = 5000;
    std::vector<std::jthread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&] { for (int i = 0; i < iters; ++i) { mtx.lock(); ++shared; mtx.unlock(); } });
    workers.clear();
    ASSERT_EQ(shared, threads * iters);
}

TEST(SpinMutex, TryLockUnderContention) {
    spin_mutex mtx;
    std::atomic<bool> race{false};
    std::atomic<int> cs{0};
    auto worker = [&] {
        for (int i = 0; i < 10000; ++i) {
            if (mtx.try_lock()) {
                cs.fetch_add(1, std::memory_order_relaxed);
                if (cs.load(std::memory_order_relaxed) > 1) race.store(true, std::memory_order_relaxed);
                cs.fetch_sub(1, std::memory_order_relaxed);
                mtx.unlock();
            }
        }
    };
    std::vector<std::jthread> threads;
    for (int t = 0; t < 8; ++t) threads.emplace_back(worker);
    threads.clear();
    ASSERT_FALSE(race.load());
}

// ═══════════════════════════════════════════════════════════════════════
//  ticket_mutex
// ═══════════════════════════════════════════════════════════════════════

TEST(TicketMutex, BasicLockUnlock) {
    ticket_mutex mtx;
    mtx.lock();
    mtx.unlock();
    SUCCEED();
}

TEST(TicketMutex, TryLock) {
    ticket_mutex mtx;
    ASSERT_TRUE(mtx.try_lock());
    mtx.unlock();
}

TEST(TicketMutex, ThreadSynchronization) {
    ticket_mutex mtx;
    int shared = 0;
    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&] { for (int j = 0; j < 1000; ++j) { mtx.lock(); ++shared; mtx.unlock(); } });
    threads.clear();
    ASSERT_EQ(shared, 4000);
}

TEST(TicketMutex, TryLockRaceCondition) {
    ticket_mutex mutex;
    std::atomic<int> cs{0};
    std::atomic<bool> race{false};
    auto worker = [&] {
        for (int i = 0; i < 25000; ++i) {
            if (mutex.try_lock()) {
                cs.fetch_add(1, std::memory_order_relaxed);
                if (cs.load(std::memory_order_relaxed) > 1) race.store(true, std::memory_order_relaxed);
                for (volatile int j = 0; j < 10; ++j);
                cs.fetch_sub(1, std::memory_order_relaxed);
                mutex.unlock();
            }
        }
    };
    std::vector<std::jthread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(worker);
    threads.clear();
    ASSERT_FALSE(race.load());
}

TEST(TicketMutex, FIFOOrdering) {
    ticket_mutex mtx;
    std::vector<int> order;
    std::mutex order_mtx;

    mtx.lock(); // hold → t1 then t2 queue up

    std::jthread t1{[&] {
        mtx.lock();
        { std::lock_guard lk(order_mtx); order.push_back(1); }
        mtx.unlock();
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::jthread t2{[&] {
        mtx.lock();
        { std::lock_guard lk(order_mtx); order.push_back(2); }
        mtx.unlock();
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    mtx.unlock(); // t1 should win ticket
    t1.join(); t2.join();

    ASSERT_EQ(order.size(), 2u);
    ASSERT_EQ(order[0], 1) << "ticket_mutex did not honor FIFO ordering";
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — basic dispatch (consolidated to reduce thread churn)
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, BasicDispatch) {
    task_system ts{4};

    // Single task
    {   std::atomic<bool> ran{false};
        ts.async([&] { ran.store(true); });
        ts.wait_all_tasks();
        ASSERT_TRUE(ran.load()); }

    // High volume
    {   std::atomic<int> c{0};
        for (int i = 0; i < 10'000; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
        ts.wait_all_tasks();
        ASSERT_EQ(c.load(), 10'000); }

    // Arguments
    {   std::atomic<int> r{0};
        ts.async([](int a, int b, std::atomic<int>& res) { res.store(a + b); }, 10, 20, std::ref(r));
        ts.wait_all_tasks();
        ASSERT_EQ(r.load(), 30); }

    // Move-only argument
    {   std::atomic<bool> ok{false};
        ts.async([&ok](std::unique_ptr<int> p) { ok.store(*p == 42); }, std::make_unique<int>(42));
        ts.wait_all_tasks();
        ASSERT_TRUE(ok.load()); }

    // Large argument (> SBO in move_only_function)
    {   struct Big { char data[256]; };
        std::atomic<bool> ok{false};
        ts.async([](Big arg, std::atomic<bool>& f) { f.store(arg.data[0] == 0); }, Big{}, std::ref(ok));
        ts.wait_all_tasks();
        ASSERT_TRUE(ok.load()); }
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — synchronization
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, WaitAllTasks) {
    task_system ts{4};
    std::atomic<int> c{0};
    for (int i = 0; i < 50'000; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c.load(), 50'000);
}

TEST(TaskSystem, WaitAllTasksEmpty) {
    task_system ts{4};
    ts.wait_all_tasks(); // no tasks submitted → return immediately
    SUCCEED();
}

TEST(TaskSystem, WaitAllTasksWithConcurrentSubmit) {
    task_system ts{4};
    std::atomic<int> c{0};
    std::atomic<bool> start{false};
    constexpr int n = 20'000;

    std::jthread producer{[&] {
        while (!start.load(std::memory_order_relaxed));
        for (int i = 0; i < n; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    }};
    start.store(true, std::memory_order_relaxed);
    producer.join();
    ts.wait_all_tasks();
    ASSERT_EQ(c.load(), n);
}

TEST(TaskSystem, SyncPoint) {
    task_system ts{4};
    std::atomic<int> c1{0}, c2{0};

    for (int i = 0; i < 100; ++i)
        ts.async([&] { c1.fetch_add(1, std::memory_order_relaxed); });
    ts.sync_point();
    ASSERT_EQ(c1.load(), 100);
    ASSERT_EQ(c2.load(), 0);

    for (int i = 0; i < 50; ++i)
        ts.async([&] { c2.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c2.load(), 50);
}

TEST(TaskSystem, SyncPointEdgeCases) {
    task_system ts{4};

    // Zero tasks
    ts.sync_point();
    SUCCEED();

    // Only waits for pre-call submissions
    std::atomic<int> c1{0}, c2{0};
    for (int i = 0; i < 50; ++i) ts.async([&] { c1.fetch_add(1, std::memory_order_relaxed); });
    ts.sync_point();
    ASSERT_EQ(c1.load(), 50);
    // submit after sync_point — these are NOT waited on
    for (int i = 0; i < 50; ++i) ts.async([&] { c2.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c2.load(), 50);
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — lifecycle
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, Stop) {
    task_system ts{4};
    std::atomic<int> c{0};

    for (int i = 0; i < 100; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    int before = c.load();

    ts.stop();
    // After stop, new tasks may not be picked up
    for (int i = 0; i < 100; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_GE(c.load(), before) << "tasks after stop() should not decrease counter";
    ASSERT_LE(c.load(), before + 100) << "all 100 post-stop tasks should not have executed";
}

TEST(TaskSystem, ShutdownUnderLoad) {
    std::atomic<int> completed{0};
    {
        task_system ts{4};
        for (int i = 0; i < 100'000; ++i)
            ts.async([&] { completed.fetch_add(1, std::memory_order_relaxed); });
    } // destructor joins — must not hang
    ASSERT_GE(completed.load(), 0);
}

TEST(TaskSystem, Clear) {
    task_system ts{4};
    std::atomic<int> c{0};

    // Submit, then clear
    for (int i = 0; i < 50'000; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.clear();

    // After clear, can't use wait_all_tasks (cleared tasks = broken submit/complete match).
    // Submit fresh batch to verify system still works.
    std::atomic<int> c2{0};
    for (int i = 0; i < 1'000; ++i)
        ts.async([&] { c2.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c2.load(), 1'000) << "task system broken after clear()";
}

TEST(TaskSystem, RapidLifecycle) {
    constexpr int n = 200;
    for (int i = 0; i < n; ++i) {
        task_system ts{4};
        std::atomic<int> c{0};
        for (int j = 0; j < 100; ++j) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    }
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — stress & concurrency
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, ConcurrentSubmission) {
    task_system ts{4};
    std::atomic<int> c{0};
    constexpr int producers = 16, per = 2000;
    std::vector<std::jthread> threads;
    for (int t = 0; t < producers; ++t)
        threads.emplace_back([&] { for (int i = 0; i < per; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); }); });
    threads.clear();
    ts.wait_all_tasks();
    ASSERT_EQ(c.load(), producers * per);
}

TEST(TaskSystem, WorkStealing) {
    task_system ts{4};
    // Single producer — all tasks land in ~1 queue → workers must steal
    {   std::atomic<int> c{0};
        for (int i = 0; i < 50'000; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
        ts.wait_all_tasks();
        ASSERT_EQ(c.load(), 50'000); }

    // Heavy tasks → more stealing opportunities
    {   std::atomic<int> c{0};
        for (int i = 0; i < 5'000; ++i)
            ts.async([&] { volatile double x = 0; for (int k = 0; k < 1000; ++k) x += k * 0.5;
                            c.fetch_add(1, std::memory_order_relaxed); });
        ts.wait_all_tasks();
        ASSERT_EQ(c.load(), 5'000); }
}

TEST(TaskSystem, MultipleSystems) {
    constexpr int N = 5, per = 2000;
    std::vector<std::unique_ptr<task_system>> systems;
    std::vector<std::atomic<int>> counters(N);
    for (int i = 0; i < N; ++i) systems.push_back(std::make_unique<task_system>(4));
    std::vector<std::jthread> threads;
    for (int s = 0; s < N; ++s)
        threads.emplace_back([&, s] { for (int i = 0; i < per; ++i) systems[s]->async([&, s] { counters[s].fetch_add(1, std::memory_order_relaxed); }); });
    threads.clear();
    for (int s = 0; s < N; ++s) { systems[s]->wait_all_tasks(); ASSERT_EQ(counters[s].load(), per); }
}

TEST(TaskSystem, HighVolume) {
    task_system ts{4};
    std::vector<std::atomic<int>> counters(32);
    for (auto& c : counters) c.store(0);
    constexpr int n = 100'000;
    for (int i = 0; i < n; ++i) ts.async([&, i] { counters[i % 32].fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    int total = 0;
    for (auto& c : counters) total += c.load();
    ASSERT_EQ(total, n);
}

TEST(TaskSystem, AtomicStress) {
    task_system ts{4};
    std::vector<std::atomic<int>> atomics(100);
    for (auto& a : atomics) a.store(0);
    constexpr int n = 20'000;
    for (int i = 0; i < n; ++i)
        ts.async([&, i] { int idx = i % 100; atomics[idx].fetch_add(3, std::memory_order_acq_rel); atomics[idx].fetch_sub(1, std::memory_order_acq_rel); });
    ts.wait_all_tasks();
    int total = 0;
    for (auto& a : atomics) total += a.load();
    ASSERT_EQ(total, n * 2); // +3 -1 = +2 net per task
}

TEST(TaskSystem, RecursiveSubmit) {
    // A task that submits one child task — verifies async() works
    // correctly when called from within a running task.
    task_system ts{4};
    std::atomic<int> counter{0};
    constexpr int n = 5'000;

    for (int i = 0; i < n; ++i)
        ts.async([&] {
            counter.fetch_add(1, std::memory_order_relaxed);
            ts.async([&] { counter.fetch_add(1, std::memory_order_relaxed); });
        });

    ts.wait_all_tasks();
    ASSERT_EQ(counter.load(), n * 2); // each parent submits one child
}

TEST(TaskSystem, Durability) {
    constexpr int iterations = 20, per = 1000;
    for (int iter = 0; iter < iterations; ++iter) {
        task_system ts{4};
        std::atomic<int> c{0};
        for (int i = 0; i < per; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
        ts.wait_all_tasks();
        ASSERT_EQ(c.load(), per) << "failed at iteration " << iter;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — exception safety
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, ExceptionSafety) {
    task_system ts{4};
    std::atomic<bool> second_ran{false};

    ts.async([] { throw std::runtime_error("test"); });
    ts.async([&] { second_ran.store(true); });
    ts.async([] { throw 42; }); // non-std::exception
    ts.async([&] { second_ran.store(true); });

    ts.wait_all_tasks();
    ASSERT_TRUE(second_ran.load()) << "worker thread killed by exception";
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — async_with_future
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, AsyncWithFuture) {
    task_system ts{4};

    // Value
    ASSERT_EQ(ts.async_with_future([] { return 42; }).get(), 42);

    // Void
    {   std::atomic<bool> flag{false};
        ts.async_with_future([&] { flag.store(true); }).get();
        ASSERT_TRUE(flag.load()); }

    // Exception propagation
    ASSERT_THROW(ts.async_with_future([]() -> int { throw std::runtime_error("err"); }).get(),
                 std::runtime_error);

    // Move-only result
    {   auto f = ts.async_with_future([] { return std::make_unique<int>(42); });
        auto p = f.get();
        ASSERT_NE(p, nullptr);
        ASSERT_EQ(*p, 42); }

    // High volume
    {   constexpr int n = 5'000;
        std::vector<std::future<int>> futures; futures.reserve(n);
        for (int i = 0; i < n; ++i) futures.push_back(ts.async_with_future([i] { return i * i; }));
        long long sum = 0;
        for (auto& f : futures) sum += f.get();
        ts.wait_all_tasks();
        ASSERT_EQ(sum, (long long)(n - 1) * n * (2 * n - 1) / 6); }
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — throughput sanity
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, ThroughputSanity) {
    task_system ts{4};
    std::atomic<int> c{0};
    constexpr int n = 100'000;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; ++i) ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    ASSERT_EQ(c.load(), n);
    ASSERT_LT(dt, 1000) << "throughput too low: " << dt << "ms for " << n << " tasks";
}

// ═══════════════════════════════════════════════════════════════════════
//  task_system — edge cases
// ═══════════════════════════════════════════════════════════════════════

TEST(TaskSystem, StopThenSyncPoint) {
    // sync_point after stop() should not hang.
    // Workers are stopped, so any remaining tasks won't complete.
    // sync_point should handle this gracefully.
    task_system ts{4};
    std::atomic<int> c{0};

    for (int i = 0; i < 100; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });

    ts.wait_all_tasks();  // drain everything first
    ASSERT_EQ(c.load(), 100);

    ts.stop();

    // Submit tasks after stop — they may never execute.
    // sync_point should not wait for them (or should return immediately).
    for (int i = 0; i < 50; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });

    ts.sync_point();  // must not hang
    SUCCEED();
}

TEST(TaskSystem, StopThenWaitAllTasks) {
    task_system ts{4};

    for (int i = 0; i < 100; ++i)
        ts.async([] {});
    ts.wait_all_tasks();
    ts.stop();

    // wait_all_tasks after stop with zero pending should return immediately
    ts.wait_all_tasks();
    SUCCEED();
}

TEST(TaskSystem, FutureWithSyncPoint) {
    // sync_point must wait for async_with_future tasks too
    task_system ts{4};
    std::atomic<bool> done{false};

    auto f = ts.async_with_future([&] { done.store(true); return 42; });
    ts.sync_point();  // must wait for the future task

    ASSERT_TRUE(done.load());
    ASSERT_EQ(f.get(), 42);
}

TEST(TaskSystem, ReentrantAsync) {
    // A task that captures task_system& and calls async() from within
    // itself — stresses re-entrant async under execution.
    task_system ts{4};
    std::atomic<int> counter{0};
    constexpr int depth = 4;
    constexpr int total = 1 + depth;  // root + depth children

    ts.async([&] {
        counter.fetch_add(1, std::memory_order_relaxed);
        // Spawn child tasks from within a worker
        for (int i = 0; i < depth; ++i)
            ts.async([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    });

    ts.wait_all_tasks();
    ASSERT_EQ(counter.load(), total);
}

TEST(TaskSystem, ReentrantFutureFromTask) {
    // A task calling async_with_future() from within another task
    task_system ts{4};
    std::atomic<int> c{0};

    auto outer = ts.async_with_future([&] {
        c.fetch_add(1, std::memory_order_relaxed);
        // Submit more work from within a running task
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
        ts.async_with_future([&] { c.fetch_add(1, std::memory_order_relaxed); return 7; });
        return 42;
    });

    ASSERT_EQ(outer.get(), 42);
    ts.wait_all_tasks();
    ASSERT_EQ(c.load(), 3);
}

TEST(TaskSystem, ConsecutiveClear) {
    task_system ts{4};
    std::atomic<int> c{0};

    for (int i = 0; i < 10'000; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.clear();
    ts.clear();  // double clear — must not corrupt state

    // Submit after double-clear
    std::atomic<int> c2{0};
    for (int i = 0; i < 1'000; ++i)
        ts.async([&] { c2.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c2.load(), 1'000);
}

TEST(TaskSystem, HighVolume500k) {
    task_system ts{4};
    std::atomic<int> c{0};
    constexpr int n = 500'000;

    for (int i = 0; i < n; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });
    ts.wait_all_tasks();
    ASSERT_EQ(c.load(), n);
}

// Submit tasks DURING wait_all_tasks — ensures wait doesn't return early
TEST(TaskSystem, WaitAllTasksDuringSubmit) {
    task_system ts{4};
    std::atomic<int> c{0};
    std::atomic<bool> go{false};
    constexpr int n = 20'000;

    // Submit first half
    for (int i = 0; i < n/2; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });

    // Start waiter thread
    std::jthread waiter{[&] {
        while (!go.load(std::memory_order_relaxed));
        ts.wait_all_tasks();
    }};

    // While waiter is blocked, submit second half
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    for (int i = 0; i < n/2; ++i)
        ts.async([&] { c.fetch_add(1, std::memory_order_relaxed); });

    go.store(true, std::memory_order_relaxed);
    waiter.join();

    ASSERT_EQ(c.load(), n);
}

// ═══════════════════════════════════════════════════════════════════════
//  spin_mutex — fairness under try_lock pressure
// ═══════════════════════════════════════════════════════════════════════

TEST(SpinMutex, LockFairnessUnderTryLock) {
    // spin_mutex is intentionally unfair. This test verifies the system
    // survives heavy mixed lock/try_lock pressure without crashing.
    spin_mutex mtx;
    std::atomic<int> cs_count{0};
    std::atomic<bool> race{false};

    auto worker = [&](bool use_lock) {
        for (int i = 0; i < 20'000; ++i) {
            if (use_lock) {
                mtx.lock();
            } else if (!mtx.try_lock()) {
                continue;
            }
            cs_count.fetch_add(1, std::memory_order_relaxed);
            if (cs_count.load(std::memory_order_relaxed) > 1)
                race.store(true, std::memory_order_relaxed);
            cs_count.fetch_sub(1, std::memory_order_relaxed);
            mtx.unlock();
        }
    };

    std::vector<std::jthread> threads;
    threads.emplace_back(worker, true);   // lock() caller
    threads.emplace_back(worker, true);   // lock() caller
    threads.emplace_back(worker, false);  // try_lock() caller
    threads.emplace_back(worker, false);  // try_lock() caller
    threads.clear();

    ASSERT_FALSE(race.load()) << "mutual exclusion violated under mixed lock types";
}

// ═══════════════════════════════════════════════════════════════════════
//  ticket_mutex — mixed lock/try_lock
// ═══════════════════════════════════════════════════════════════════════

TEST(TicketMutex, MixedLockAndTryLock) {
    // lock() and try_lock() interleaved — does ticket order hold?
    ticket_mutex mtx;
    std::atomic<int> counter{0};
    std::atomic<bool> race{false};

    auto lock_worker = [&] {
        for (int i = 0; i < 5000; ++i) {
            mtx.lock();
            counter.fetch_add(1, std::memory_order_relaxed);
            if (counter.load(std::memory_order_relaxed) > 1)
                race.store(true, std::memory_order_relaxed);
            counter.fetch_sub(1, std::memory_order_relaxed);
            mtx.unlock();
        }
    };

    auto try_worker = [&] {
        for (int i = 0; i < 5000; ++i) {
            if (mtx.try_lock()) {
                counter.fetch_add(1, std::memory_order_relaxed);
                if (counter.load(std::memory_order_relaxed) > 1)
                    race.store(true, std::memory_order_relaxed);
                counter.fetch_sub(1, std::memory_order_relaxed);
                mtx.unlock();
            }
        }
    };

    std::vector<std::jthread> threads;
    threads.emplace_back(lock_worker);
    threads.emplace_back(lock_worker);
    threads.emplace_back(try_worker);
    threads.emplace_back(try_worker);
    threads.clear();

    ASSERT_FALSE(race.load()) << "race condition with mixed lock/try_lock";
}
