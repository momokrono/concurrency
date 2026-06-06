/// Baseline benchmark — trimmed for <15s runtime.
/// Run BEFORE and AFTER changes to detect regressions.
/// g++ -std=c++26 -O3 -o bl_bench baseline_bench.cpp -lpthread -lfmt -I. -Iextern/fmt/include

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include <concurrency/task_system.hpp>

using concurrency::spin_mutex;
using concurrency::ticket_mutex;
using concurrency::task_system;
#include "custom_locks.hpp"

using namespace std::chrono;

// ─── 1. Lock Contention ────────────────────────────────────────────────

void bench_lock_contention() {
    std::cout << "\n═══ Lock Contention ═══\n";
    constexpr int threads = 32;
    constexpr long iters = 10'000;
    constexpr long total = threads * iters;

    auto bench = [&](auto& mtx, const char* name) {
        long counter = 0;
        std::vector<std::jthread> workers;
        auto t0 = high_resolution_clock::now();
        for (int t = 0; t < threads; ++t) {
            workers.emplace_back([&] {
                for (long i = 0; i < iters; ++i) { mtx.lock(); ++counter; mtx.unlock(); }
            });
        }
        workers.clear();
        auto dt = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();
        std::cout << "  " << std::setw(16) << name
                  << "  " << std::setw(6) << dt << " µs"
                  << "  per-op=" << std::setw(6) << std::fixed << std::setprecision(1)
                  << (dt * 1000.0 / total) << " ns\n";
    };

    { spin_mutex s; bench(s, "spin_mutex(warm)"); }
    { spin_mutex s; bench(s, "spin_mutex"); }
    { ticket_mutex t; bench(t, "ticket_mutex"); }
    { std::mutex m; bench(m, "std::mutex"); }
}

// ─── 2. Task Latency Distribution ──────────────────────────────────────

void bench_latency_distribution() {
    std::cout << "\n═══ Task Latency Distribution ═══\n";
    task_system ts;
    constexpr int samples = 2'000;
    std::vector<double> lat;

    for (int i = 0; i < samples; ++i) {
        std::latch ready(1);
        auto t0 = high_resolution_clock::now();
        ts.async([&, t0] {
            auto dt = duration_cast<nanoseconds>(high_resolution_clock::now() - t0).count();
            lat.push_back(static_cast<double>(dt) / 1000.0);
            ready.count_down();
        });
        ready.wait();
    }

    std::ranges::sort(lat);
    size_t n = lat.size();
    double sum = std::accumulate(lat.begin(), lat.end(), 0.0);
    double mean = sum / n;
    double sq = 0;
    for (auto v : lat) sq += (v - mean) * (v - mean);
    auto p = [&](double pct) { return lat[static_cast<size_t>(n * pct / 100.0)]; };

    std::cout << "  mean=" << std::fixed << std::setprecision(1) << mean
              << " p50=" << p(50) << " p95=" << p(95) << " p99=" << p(99)
              << " p999=" << p(99.9) << " µs  (n=" << n << ")\n";
}

// ─── 3. Throughput vs Producers ────────────────────────────────────────

void bench_throughput_scaling() {
    std::cout << "\n═══ Throughput Scaling (50k tasks) ═══\n";
    constexpr int total = 50'000;
    int hw = static_cast<int>(std::thread::hardware_concurrency());

    for (int producers : {1, 2, 4, 8, 16, hw}) {
        task_system ts;
        std::atomic<int> counter{0};
        int per = total / producers;

        auto t0 = high_resolution_clock::now();
        std::vector<std::jthread> pool;
        for (int p = 0; p < producers; ++p) {
            pool.emplace_back([&, per] {
                for (int i = 0; i < per; ++i)
                    ts.async([&] { counter.fetch_add(1, std::memory_order_relaxed); });
            });
        }
        pool.clear();
        ts.wait_all_tasks();
        auto dt = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();

        std::cout << "  producers=" << std::setw(3) << producers
                  << "  " << std::setw(7) << dt << " µs"
                  << "  tps=" << std::setw(12) << std::fixed << std::setprecision(0)
                  << (total / (dt / 1'000'000.0)) << "\n";
    }
}

// ─── 4. Work-stealing Efficiency ───────────────────────────────────────

void bench_work_stealing() {
    std::cout << "\n═══ Work-Stealing Efficiency ═══\n";
    constexpr int tasks = 30'000;

    for (int p : {1, 8}) {
        task_system ts;
        std::atomic<long long> work{0};
        auto t0 = high_resolution_clock::now();
        int per = tasks / p;

        std::vector<std::jthread> producers;
        for (int j = 0; j < p; ++j) {
            producers.emplace_back([&, per] {
                for (int i = 0; i < per; ++i) {
                    ts.async([&] {
                        volatile int x = 0;
                        for (int j = 0; j < 50; ++j) x += j;
                        work.fetch_add(1, std::memory_order_relaxed);
                    });
                }
            });
        }
        producers.clear();
        ts.wait_all_tasks();
        auto dt = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();
        std::cout << "  producers=" << std::setw(2) << p
                  << "  " << std::setw(7) << dt << " µs"
                  << "  tps=" << std::setw(12) << std::fixed << std::setprecision(0)
                  << (tasks / (dt / 1'000'000.0)) << "\n";
    }
}

// ─── 5. async_with_future ──────────────────────────────────────────────

void bench_future_throughput() {
    std::cout << "\n═══ async_with_future (5k tasks) ═══\n";
    task_system ts;
    constexpr int tasks = 5'000;

    auto t0 = high_resolution_clock::now();
    std::vector<std::future<int>> futures;
    futures.reserve(tasks);
    for (int i = 0; i < tasks; ++i)
        futures.push_back(ts.async_with_future([i] { return i * i; }));
    long long sum = 0;
    for (auto& f : futures) sum += f.get();
    ts.wait_all_tasks();
    auto dt = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();

    std::cout << "  " << dt << " µs  tps=" << std::fixed << std::setprecision(0)
              << (tasks / (dt / 1'000'000.0)) << "  sum=" << sum << "\n";
}

// ─── 6. try_lock Hit Rate ──────────────────────────────────────────────

void bench_trylock_rate() {
    std::cout << "\n═══ try_lock Hit Rate ═══\n";

    auto test = [](auto& mtx, const char* name) {
        std::atomic<long> ok{0}, fail{0};
        std::vector<std::jthread> workers;
        for (int t = 0; t < 32; ++t) {
            workers.emplace_back([&] {
                for (int i = 0; i < 5'000; ++i) {
                    if (mtx.try_lock()) { ok.fetch_add(1); mtx.unlock(); }
                    else fail.fetch_add(1);
                }
            });
        }
        workers.clear();
        long s = ok.load(), f = fail.load();
        std::cout << "  " << std::setw(16) << name
                  << "  hit-rate=" << std::fixed << std::setprecision(1)
                  << (100.0 * s / (s + f)) << "%\n";
    };

    { spin_mutex s; test(s, "spin_mutex"); }
    { ticket_mutex t; test(t, "ticket_mutex"); }
}

// ─── 7. Pipeline ───────────────────────────────────────────────────────

void bench_pipeline() {
    std::cout << "\n═══ Pipeline (20 batches × 200 tasks) ═══\n";
    task_system ts;
    constexpr int batches = 20;
    constexpr int per = 200;

    auto t0 = high_resolution_clock::now();
    for (int b = 0; b < batches; ++b) {
        for (int i = 0; i < per; ++i)
            ts.async([] {});
        ts.sync_point();
    }
    auto dt = duration_cast<microseconds>(high_resolution_clock::now() - t0).count();
    int total = batches * per;
    std::cout << "  " << dt << " µs  tps=" << std::fixed << std::setprecision(0)
              << (total / (dt / 1'000'000.0)) << "\n";
}

// ─── Main ──────────────────────────────────────────────────────────────

int main() {
    std::cout << std::unitbuf;  // unbuffered for real-time progress
    std::cout << "══════════ BASELINE — " << std::thread::hardware_concurrency()
              << " cores ══════════\n";

    bench_lock_contention();
    bench_latency_distribution();
    bench_throughput_scaling();
    bench_work_stealing();
    bench_future_throughput();
    bench_trylock_rate();
    bench_pipeline();

    std::cout << "\n═══ DONE ═══\n";
    return 0;
}
