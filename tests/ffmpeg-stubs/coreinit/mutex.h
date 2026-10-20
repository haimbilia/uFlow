#pragma once
#include <pthread.h>
#include <assert.h>
typedef pthread_mutex_t OSMutex;
static inline void OSInitMutex(OSMutex *m) { assert(!pthread_mutex_init(m,0)); }
static inline void OSLockMutex(OSMutex *m) { assert(!pthread_mutex_lock(m)); }
static inline void OSUnlockMutex(OSMutex *m) { assert(!pthread_mutex_unlock(m)); }
