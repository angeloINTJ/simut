/* Native stub — the log manager's cross-core mutex, which a host test does not
 * have and does not need: the native suites are single-threaded by
 * construction. Present so LogManager.h compiles, not to model anything. */
#pragma once
typedef struct { int dummy; } mutex_t;
typedef struct { int dummy; } recursive_mutex_t;
static inline void mutex_init(mutex_t*) {}
static inline void mutex_enter_blocking(mutex_t*) {}
static inline void mutex_exit(mutex_t*) {}
static inline bool mutex_try_enter(mutex_t*, unsigned*) { return true; }
static inline void recursive_mutex_init(recursive_mutex_t*) {}
static inline void recursive_mutex_enter_blocking(recursive_mutex_t*) {}
static inline void recursive_mutex_exit(recursive_mutex_t*) {}
