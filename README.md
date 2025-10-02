# concurrency

### A small repo for my concurrency stuff

there are two header files:

- `custom_locks.hpp` : two mutexes are defined, a `spin_lock` and a `ticket_mutex`, based on [these two](https://github.com/CppCon/CppCon2019/blob/master/Presentations/cpp20_synchronization_library/cpp20_synchronization_library__r2__bryce_adelstein_lelbach__cppcon_2019.pdf) but with an added `try_lock()` method, required for the `task_system`.

- `task_system.hpp` : implements a task system based on [this one](https://www.youtube.com/watch?v=zULU6Hhp42w) but using a `semaphore` instead of a `condition_variable`, a `spin_lock`, and with new methods to stop and clear running tasks, and the added ability to add task parameters.
