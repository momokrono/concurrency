# concurrency

### A small repo for my concurrency stuff

there are two header files:

- `custom_locks.hpp` : two mutexes are defined, a `spin_lock` and a `ticket_mutex`, based on [these two](https://github.com/CppCon/CppCon2019/blob/master/Presentations/cpp20_synchronization_library/cpp20_synchronization_library__r2__bryce_adelstein_lelbach__cppcon_2019.pdf) but with an added `try_lock()` method, required for the `task_system`.

- `task_system.hpp` : implements a task system based on [this one](https://www.youtube.com/watch?v=zULU6Hhp42w) but using a `semaphore` instead of a `condition_variable`, a `spin_lock`, and with new methods to stop and clear running tasks, and the added ability to add task parameters.

Inside `main.cpp` there's a simple example I used to benchmark my `task_system` against the one displayed in the youtube video. The idea is to dispatch a ton of extremely simple tasks, in this case decrement a `std::latch`, so that the difference in timing has to come from the tasking system used. Using `hyperfine`, the original `task_system` runs in `3.753 s ±  0.184 s`, while mine in `2.092 s ±  0.552 s`, a performance improvement of roughly 40-45%.
