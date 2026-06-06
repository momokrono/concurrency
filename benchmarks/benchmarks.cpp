#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <atomic>
#include <future>
#include <latch>
#include <concurrency/task_system.hpp>

using concurrency::spin_mutex;
using concurrency::ticket_mutex;
using concurrency::task_system;
#include "custom_locks.hpp"

// Benchmark utilities
struct BenchmarkResult {
    std::string name;
    double duration_ms;
    double ops_per_sec;
    size_t iterations;
};

class BenchmarkRunner {
private:
    std::vector<BenchmarkResult> results;

public:
    template<typename Func>
    void run(const std::string& name, Func&& func, size_t iterations = 1) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        double duration_ms = duration.count() / 1000.0;
        double ops_per_sec = iterations / (duration_ms / 1000.0);
        
        results.push_back({name, duration_ms, ops_per_sec, iterations});
    }
    
    void print_results() {
        std::cout << "\n=== Benchmark Results ===" << std::endl;
        std::cout << "Benchmark Name                | Duration (ms) | Ops/sec        | Iterations" << std::endl;
        std::cout << "------------------------------|---------------|----------------|----------" << std::endl;
        for (const auto& result : results) {
            std::cout << std::left << std::setw(30) << result.name 
                      << "| " << std::setw(13) << result.duration_ms 
                      << "| " << std::setw(15) << static_cast<size_t>(result.ops_per_sec)
                      << "| " << result.iterations << std::endl;
        }
    }
};

// Various benchmark tests
void benchmark_throughput_small_tasks() {
    std::cout << "Running throughput benchmark with small tasks..." << std::endl;
    
    task_system ts;
    std::atomic<int> counter{0};
    const size_t num_tasks = 100000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_tasks; ++i) {
        ts.async([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    // Wait for all tasks to complete
    while (counter.load() < static_cast<int>(num_tasks)) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double duration_ms = duration.count() / 1000.0;
    double tasks_per_sec = num_tasks / (duration_ms / 1000.0);
    
    std::cout << "  " << num_tasks << " small tasks in " << duration_ms << " ms = " 
              << static_cast<size_t>(tasks_per_sec) << " tasks/sec" << std::endl;
}

void benchmark_throughput_large_tasks() {
    std::cout << "Running throughput benchmark with large tasks..." << std::endl;
    
    task_system ts;
    const size_t num_tasks = 10000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_tasks; ++i) {
        ts.async([i]() {
            // Simulate more substantial work
            volatile int sum = 0;
            for (int j = 0; j < 1000; ++j) {
                sum += j * j + i;
            }
        });
    }
    
    // Wait using a latch for more accurate timing
    std::latch latch(static_cast<ptrdiff_t>(num_tasks));
    for (size_t i = 0; i < num_tasks; ++i) {
        ts.async([&latch]() {
            volatile int sum = 0;
            for (int j = 0; j < 1000; ++j) {
                sum += j * j;
            }
            latch.count_down();
        });
    }
    latch.wait();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double duration_ms = duration.count() / 1000.0;
    double tasks_per_sec = num_tasks / (duration_ms / 1000.0);
    
    std::cout << "  " << num_tasks << " large tasks in " << duration_ms << " ms = " 
              << static_cast<size_t>(tasks_per_sec) << " tasks/sec" << std::endl;
}

void benchmark_single_task_latency() {
    std::cout << "Running single task latency benchmark..." << std::endl;
    
    task_system ts;
    const size_t num_iterations = 1000;
    std::vector<int64_t> latencies;
    latencies.reserve(num_iterations);
    
    for (size_t iter = 0; iter < num_iterations; ++iter) {
        // Use a latch for proper synchronization without race conditions
        auto start_time = std::chrono::high_resolution_clock::now();
        std::atomic<int64_t> *latency_ptr = new std::atomic<int64_t>(0);
        std::latch completion_latch(1);
        
        ts.async([start_time, latency_ptr, &completion_latch]() {
            auto exec_time = std::chrono::high_resolution_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(exec_time - start_time);
            latency_ptr->store(latency.count());
            completion_latch.count_down();
        });
        
        completion_latch.wait();  // Wait for the task to complete and record latency
        latencies.push_back(latency_ptr->load());
        delete latency_ptr;
    }
    
    int64_t total_latency = 0;
    for (auto lat : latencies) {
        total_latency += lat;
    }
    int64_t avg_latency = total_latency / num_iterations;
    std::cout << "  Average single task latency: " << avg_latency << " microseconds (" << num_iterations << " samples)" << std::endl;
}

void benchmark_task_creation_overhead() {
    std::cout << "Running task creation overhead benchmark..." << std::endl;
    
    task_system ts;
    const size_t num_batches = 1000;
    const size_t tasks_per_batch = 100;
    std::atomic<int> completed_count{0};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t batch = 0; batch < num_batches; ++batch) {
        std::latch batch_latch(tasks_per_batch);
        
        for (size_t i = 0; i < tasks_per_batch; ++i) {
            ts.async([&batch_latch, &completed_count]() {
                completed_count.fetch_add(1, std::memory_order_relaxed);
                batch_latch.count_down();
            });
        }
        
        batch_latch.wait();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double duration_ms = duration.count() / 1000.0;
    double total_tasks = num_batches * tasks_per_batch;
    double tasks_per_sec = total_tasks / (duration_ms / 1000.0);
    
    std::cout << "  " << total_tasks << " tasks in " << duration_ms << " ms = " 
              << static_cast<size_t>(tasks_per_sec) << " tasks/sec (with sync)" << std::endl;
}

void benchmark_work_stealing_efficiency() {
    std::cout << "Running work stealing efficiency benchmark..." << std::endl;
    
    // Create an uneven workload to trigger work stealing
    task_system ts;
    std::atomic<int> producer_counter{0};
    std::atomic<int> consumer_counter{0};
    
    const size_t num_producer_tasks = 20000;
    const size_t num_consumer_tasks = 20000;
    
    // Producer thread creates many tasks quickly
    std::thread producer([&ts, &producer_counter, num_producer_tasks]() {
        for (size_t i = 0; i < num_producer_tasks; ++i) {
            ts.async([&producer_counter, i]() {
                producer_counter.fetch_add(1, std::memory_order_relaxed);
                
                // Add slight delay to create more work-stealing opportunities
                if (i % 100 == 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            });
        }
    });
    
    // Wait for tasks to complete
    while (producer_counter.load() < static_cast<int>(num_producer_tasks)) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    producer.join();
    
    std::cout << "  Work stealing efficiency test completed with " 
              << num_producer_tasks << " tasks" << std::endl;
}

// Run all benchmarks
int main() {
    std::cout << "Task System Benchmark Suite" << std::endl;
    std::cout << "===========================" << std::endl;
    
    benchmark_throughput_small_tasks();
    benchmark_throughput_large_tasks();
    benchmark_single_task_latency();
    benchmark_task_creation_overhead();
    benchmark_work_stealing_efficiency();
    
    std::cout << "\nBenchmark suite completed." << std::endl;
    
    return 0;
}