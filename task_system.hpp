#ifndef TASK_SYSTEM_HPP
#define TASK_SYSTEM_HPP

#include <deque>
#include <thread>
#include <condition_variable>
#include <functional>
//#include "new_locks.hpp"
#include "custom_locks.hpp"


using lock_t = std::unique_lock<spin_mutex>;

class notification_queue {
    const unsigned _count{std::thread::hardware_concurrency()};
    std::deque<std::function<void()>> _q;
    bool _done{false};
    spin_mutex _mutex;
    std::binary_semaphore _pop{0};

public:
    constexpr auto try_pop(std::function<void()>& x) noexcept -> bool {
        if (_q.empty() || !_pop.try_acquire() ) { return false; }
        lock_t lock{_mutex};
        x = std::move(_q.front());
        _q.pop_front();
        return true;
    }

    template<typename F>
    constexpr auto try_push(F && f) noexcept -> bool {
        {
            lock_t lock{_mutex, std::try_to_lock};
            if (!lock) { return false; }
            _q.emplace_back(std::forward<F>(f));
        }
        _pop.release();
        return true;
    }

    auto clear() noexcept -> void {
        lock_t lock{_mutex};
        _q.clear();
    }

    auto done() noexcept -> void {
        {
            lock_t lock{_mutex};
            _done = true;
        }
        _pop.release(_count);
    }

    constexpr auto pop(std::function<void()>& x) noexcept -> bool {
        _pop.acquire();
        if ( _q.empty() ) {
            _pop.release();
            return false;
        }
        lock_t lock{_mutex};
        x = std::move( _q.front() );
        _q.pop_front();
        return true;
    }

    template<typename F, typename ...Args>
    constexpr auto push(F && f, Args... args) noexcept -> void {
        {
            lock_t lock{_mutex};
            _q.emplace_back( [fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ] {
                return std::apply(std::move(fn), args);
            });
        }
        _pop.release();
    }
};


class task_system {
    const unsigned _count{std::thread::hardware_concurrency()};
    std::vector<std::jthread> _threads;
    std::vector<notification_queue> _q{_count};
    std::atomic<unsigned> _index{0};

    constexpr auto run(std::stop_token const & s, unsigned i) noexcept -> void {
        while ( !s.stop_requested() ) {
            auto f = std::function<void()>{};
            for ( unsigned n = 0; n != _count * 2; ++n ) {
                if ( _q[ (i + n) % _count].try_pop(f) ) { break; }
            }
            if ( !f && !_q[i].pop(f) ) { break; }

            f();
        }
    }

public:
    task_system() {
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

    constexpr auto clear() noexcept -> void {
        for ( auto & q : _q ) { q.clear(); }
    }

    template<typename F, typename ...Args>
    constexpr auto async(F && f, Args... args) noexcept -> void {
        auto i = _index++;

        for ( unsigned n = 0; n != _count * 4; ++n ) {
            if ( _q[ (i + n) % _count ].try_push(
                    [ fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ] {
                        return std::apply(std::move(fn), args);
                    } ) ) { return; }
        }

        _q[ i % _count ].push(
                [ fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ] {
                    return std::apply(std::move(fn), args); } );
    }
};


#endif
