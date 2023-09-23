#ifndef CUSTOM_LOCKS_HPP
#define CUSTOM_LOCKS_HPP

#include <atomic>

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
        auto const my = in.load(std::memory_order_acquire);
        auto const now = out.load(std::memory_order_acquire);
        if (now == my) {
            in.fetch_add(1, std::memory_order_acquire);
            return true;
        }
        return false;
    }
};

#endif

