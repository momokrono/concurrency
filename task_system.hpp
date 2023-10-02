#ifndef TASK_SYSTEM_HPP
#define TASK_SYSTEM_HPP

#include <deque>
#include <thread>
#include <condition_variable>
#include <functional>
#include "custom_locks.hpp"


using lock_t = std::unique_lock<spin_mutex>;

class notification_queue {
private:
    std::deque<std::function<void()>> _q;
    bool _done{false};
    const unsigned _count{std::thread::hardware_concurrency()};
    spin_mutex _mutex;
    std::binary_semaphore _pop{0};

public:
    auto try_pop(std::function<void()>& x) noexcept -> bool {
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
        _pop.release();
        return true;
    }

    auto done() noexcept -> void {
        {
            lock_t lock{_mutex};
            _done = true;
        }
        _pop.release(_count);
    }

    auto clear() noexcept -> void {
        {
            lock_t lock{_mutex};
            _q.clear();
        }
        _pop.release(_count);
    }

    auto pop(std::function<void()>& x) noexcept -> bool {
        while ( _q.empty() && !_done ) {
            _pop.acquire();
        }
        lock_t lock{_mutex};
        if ( _q.empty() ) {
            _pop.release();
            return false;
        }
        x = std::move( _q.front() );
        _q.pop_front();
        return true;
    }

    auto push(auto && f) noexcept -> void {
        {
            lock_t lock{_mutex};
            _q.emplace_back( std::forward<decltype(f)>(f));
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
    constexpr auto async(F && f, Args &&... args) noexcept -> void {
        auto i = _index++;
        for ( unsigned n = 0; n != _count * 4; ++n ) {
            if ( _q[ (i + n) % _count ].try_push(
                    [ fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ] {
                        return std::apply(std::move(fn), std::move(args));
                    } ) ) { return; }
        }
        _q[ i % _count ].push(
                [ fn = std::forward<F>(f), args = std::tuple{std::forward<Args>(args)...} ] {
                    return std::apply(std::move(fn), std::move(args)); } );
    }
};


#endif
