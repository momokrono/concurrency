#ifndef CONCURRENCY_LOCKS_HPP
#define CONCURRENCY_LOCKS_HPP

#include <atomic>
#include <cassert>
#include <version>
#if __cpp_lib_is_sufficiently_aligned >= 202503L
#include <memory>       // std::is_sufficiently_aligned (C++26)
#endif

namespace concurrency {

struct spin_mutex {
private:
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    constexpr auto lock() noexcept -> void {
        while (flag.test_and_set(std::memory_order_acquire)) { flag.wait(true, std::memory_order_relaxed); }
    }
    constexpr auto unlock() noexcept -> void {
        flag.clear(std::memory_order_release);
        flag.notify_one();
    }
    constexpr auto try_lock() noexcept -> bool {
        return !flag.test_and_set(std::memory_order_acquire);
    }
};


struct ticket_mutex {
private:
    alignas(std::hardware_destructive_interference_size) std::atomic<int>  in{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<int> out{0};
public:
    // C++26: verify cache-line separation at construction time
    ticket_mutex() noexcept {
#if __cpp_lib_is_sufficiently_aligned >= 202503L
        assert(std::is_sufficiently_aligned<
            std::hardware_destructive_interference_size>(&in));
        assert(std::is_sufficiently_aligned<
            std::hardware_destructive_interference_size>(&out));
#endif
    }
    constexpr auto lock() noexcept -> void {
        auto const my = in.fetch_add(1, std::memory_order_acquire);
        while (true) {
            auto const now = out.load(std::memory_order_acquire);
            if (now == my) return;
            out.wait(now, std::memory_order_relaxed);
        }
    }
    constexpr auto unlock() noexcept -> void {
        out.fetch_add(1, std::memory_order_release);
        out.notify_all();
    }
    constexpr auto try_lock() noexcept -> bool {
        // acquire on success (to see prior unlocks), relaxed on failure
        if (auto ticket = out.load(std::memory_order_acquire);
            in.compare_exchange_strong(ticket, ticket + 1,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
            return true;
        }
        return false;
    }
};

}  // namespace concurrency

#endif
