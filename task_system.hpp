#ifndef TASK_SYSTEM_HPP
#define TASK_SYSTEM_HPP

#include <deque>
#include <thread>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <print>
#include <exception>
#include <future>
#include "custom_locks.hpp"

using lock_t = std::unique_lock<spin_mutex>;

class notification_queue {
private:
    std::deque<std::move_only_function<void()>> _q;
    bool _done{false};
    const unsigned _count{std::thread::hardware_concurrency()};
    spin_mutex _mutex;
    std::condition_variable_any _ready;

public:
    auto try_pop(std::move_only_function<void()>& x) noexcept -> bool {
        lock_t lock{_mutex, std::try_to_lock};
        if ( !lock || _q.empty() ) { return false; }
        x = std::move(_q.front());
        _q.pop_front();
        return true;
    }

    constexpr auto try_push(auto && f) noexcept -> bool {
        {
            lock_t lock{_mutex, std::try_to_lock};
            if (!lock) { return false; }
            _q.emplace_back(std::forward<decltype(f)>(f));
        }
        _ready.notify_one();
        return true;
    }

    auto done() noexcept -> void {
        {
            lock_t lock{_mutex};
            _done = true;
        }
        _ready.notify_all();
    }

    auto clear() noexcept -> size_t {
        size_t dropped = 0;
        {
            lock_t lock{_mutex};
            dropped = _q.size();
            _q.clear();
        }
        _ready.notify_all();
        return dropped;
    }

    auto pop(std::move_only_function<void()>& x) noexcept -> bool {
        lock_t lock{_mutex};
        while (_q.empty() && !_done) {
            _ready.wait(lock);
        }
        if (_q.empty()) {
            return false;
        }
        x = std::move(_q.front());
        _q.pop_front();
        return true;
    }

    auto push(auto && f) noexcept -> void {
        {
            lock_t lock{_mutex};
            _q.emplace_back( std::forward<decltype(f)>(f));
        }
        _ready.notify_one();
    }
};


class task_system {
    const unsigned _count{std::thread::hardware_concurrency()};
    std::vector<std::jthread> _threads;
    std::vector<notification_queue> _q{_count};
    std::atomic<unsigned> _index{0};
    std::atomic<unsigned> _active_tasks{0};
    std::atomic<unsigned long> _submitted_tasks{0};
    std::atomic<unsigned long> _completed_tasks{0};

    constexpr auto run(std::stop_token const & s, unsigned i) noexcept -> void {
        unsigned idle_streak = 0;  // adaptive: shrink probe window when queues are dry
        while ( !s.stop_requested() ) {
            auto f = std::move_only_function<void()>{};
            // Adapt probe window: 6× on busy, 2× after 8+ consecutive empty probes
            unsigned const probes = (idle_streak < 8) ? _count * 6 : _count * 2;
            for ( unsigned n = 0; n != probes; ++n ) {
                if ( _q[ (i + n) % _count].try_pop(f) ) {
                    idle_streak = 0;
                    break;
                }
            }
            if ( !f ) {
                ++idle_streak;
                if ( !_q[i].pop(f) ) { break; }
                idle_streak = 0;
            }

            _active_tasks.fetch_add(1, std::memory_order_relaxed);
            try {
                f();
            } catch (const std::exception& e) {
                std::println(stderr, "Task system caught exception in thread {}: {}", i, e.what());
                // C++26: std::breakpoint_if_debugging(); — not yet in GCC 16.1 libstdc++
            } catch (...) {
                std::println(stderr, "Task system caught unknown exception in thread {}.", i);
                // C++26: std::breakpoint_if_debugging();
            }
            _active_tasks.fetch_sub(1, std::memory_order_relaxed);
            _completed_tasks.fetch_add(1, std::memory_order_release);
            _completed_tasks.notify_all();
        }
    }

public:
    task_system(unsigned thread_count = std::thread::hardware_concurrency())
        : _count{thread_count}, _q{_count} {
        for ( unsigned n = 0; n != _count; ++n ) {
            _threads.emplace_back( [&, n, s = std::stop_token{}] { run(s, n); } );
        }
    }

    ~task_system() {
        for ( auto& e : _q ) e.done();
        for ( auto& e : _threads ) e.join();
    }

    constexpr auto stop() noexcept -> void {
        for ( auto & t : _threads ) { t.request_stop(); }
    }

    /// Drops all pending (queued but not yet started) tasks.
    /// Returns the number of tasks dropped.
    constexpr auto clear() noexcept -> size_t {
        size_t dropped = 0;
        for ( auto & q : _q ) { dropped += q.clear(); }
        // Subtract dropped tasks so wait_all_tasks/sync_point don't wait for them.
        _submitted_tasks.fetch_sub(dropped, std::memory_order_release);
        return dropped;
    }

    constexpr auto sync_point() noexcept -> void {
        // acquire: see all async() submissions before this call.
        unsigned long target = _submitted_tasks.load(std::memory_order_acquire);
        unsigned long current = _completed_tasks.load(std::memory_order_acquire);
        while (current < target) {
            _completed_tasks.wait(current, std::memory_order_relaxed);
            current = _completed_tasks.load(std::memory_order_acquire);
        }
    }

    constexpr auto wait_all_tasks() noexcept -> void {
        unsigned long submitted = _submitted_tasks.load(std::memory_order_acquire);
        unsigned long completed = _completed_tasks.load(std::memory_order_acquire);
        while (completed < submitted) {
            _completed_tasks.wait(completed, std::memory_order_relaxed);
            completed = _completed_tasks.load(std::memory_order_acquire);
            submitted  = _submitted_tasks.load(std::memory_order_acquire);
        }
    }

    template<typename F, typename ...Args>
    constexpr auto async(F && f, Args &&... args) noexcept -> void {
        // Wrap once, only consume on successful enqueue.
        // Increment submitted BEFORE enqueuing: prevents a race where a worker
        // pops and completes before the counter update, causing wait_all_tasks()
        // to miss follow-up re-entrant async submissions.
        _submitted_tasks.fetch_add(1, std::memory_order_release);
        auto task = std::move_only_function<void()>(
            [ fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ]() mutable {
                return std::apply(std::move(fn), std::move(args));
            });
        auto i = _index++;
        for ( unsigned n = 0; n != _count * 8; ++n ) {
            if ( _q[ (i + n) % _count ].try_push(std::move(task)) ) {
                return;
            }
        }
        _q[ i % _count ].push(std::move(task));
    }

    template<typename F, typename ...Args>
    auto async_with_future(F && f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_t = std::invoke_result_t<F, Args...>;

        auto task = std::packaged_task<return_t()>(
            [fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...}]() {
                return std::apply(std::move(fn), std::move(args));
            }
        );

        auto future = task.get_future();

        async([t = std::move(task)]() mutable {
            t();
        });

        return future;
    }
};


#endif