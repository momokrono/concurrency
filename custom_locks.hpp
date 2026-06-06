// Deprecated — use <concurrency/locks.hpp> instead.
// This header exists for backward compatibility and re-exports
// the lock types into the global namespace (not recommended).
#include <concurrency/locks.hpp>
using concurrency::spin_mutex;
using concurrency::ticket_mutex;
