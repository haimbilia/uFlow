/* AVBufferPool is shared by uFlow's decoder, converter and GPU leases even
 * when FFmpeg codec-internal threading is disabled. Pools initialize their
 * mutex before publication; destroy runs only after the final atomic lease. */
#ifndef UFLOW_WIIU_BUFFER_POOL_H
#define UFLOW_WIIU_BUFFER_POOL_H
#include <coreinit/mutex.h>
typedef OSMutex UFlowBufferMutex;
static inline int uflow_buffer_mutex_init(UFlowBufferMutex *m, const void *attr)
{ (void)attr; OSInitMutex(m); return 0; }
static inline int uflow_buffer_mutex_lock(UFlowBufferMutex *m)
{ OSLockMutex(m); return 0; }
static inline int uflow_buffer_mutex_unlock(UFlowBufferMutex *m)
{ OSUnlockMutex(m); return 0; }
static inline int uflow_buffer_mutex_destroy(UFlowBufferMutex *m)
{ (void)m; return 0; }
#endif
