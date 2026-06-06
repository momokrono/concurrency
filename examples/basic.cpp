/// Basic usage: submit many tasks, wait with std::latch.
///
/// Build:  g++ -std=c++26 -O3 -o basic examples/basic.cpp -Iinclude -lpthread
/// Run:    ./basic

#include <concurrency/task_system.hpp>
#include <chrono>
#include <latch>
#include <print>

using concurrency::task_system;

int main() {
    constexpr int rounds = 50;
    constexpr unsigned tasks_per_round = 100'000;

    task_system ts;
    auto t0 = std::chrono::steady_clock::now();

    for (int r = 0; r < rounds; ++r) {
        std::latch latch{tasks_per_round};
        for (unsigned i = 0; i < tasks_per_round; ++i) {
            ts.async([&] { latch.count_down(); });
        }
        latch.wait();
    }

    auto dt = std::chrono::steady_clock::now() - t0;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(dt);
    std::println("{} rounds × {} tasks done in {} ({:.2f} µs/round)",
                 rounds, tasks_per_round, us, us.count() / (double)rounds);
}
