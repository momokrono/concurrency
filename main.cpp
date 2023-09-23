#include <latch>
#include <ranges>
#include <chrono>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include "task_system.hpp"


int main() {
    auto num_of_tests = 50;
    auto task_manager = task_system();
    auto start = std::chrono::steady_clock::now();
    for ( auto i{0}; i < num_of_tests; ++i ) {
        auto count_number = 100000u;
        auto latch = std::latch{count_number};
        auto func = [&]() -> void {
            latch.count_down();
        };
        for (auto k: std::views::iota(0u, count_number)) {
            task_manager.async(func);
        }
        latch.wait();
    }
    auto duration = std::chrono::steady_clock::now() - start;
    fmt::print("done in {}\n", std::chrono::duration_cast<std::chrono::microseconds>(duration));

}

