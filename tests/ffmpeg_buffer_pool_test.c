#include <libavutil/buffer.h>
#include <pthread.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static AVBufferPool *pool;
static void *worker(void *opaque) {
    const uint8_t stamp=(uintptr_t)opaque;
    for(unsigned i=0;i<20000;++i) {
        AVBufferRef *a=av_buffer_pool_get(pool);assert(a);
        memset(a->data,stamp,a->size);
        AVBufferRef *b=av_buffer_ref(a);assert(b);
        av_buffer_unref(&a);
        for(unsigned j=0;j<b->size;++j)assert(b->data[j]==stamp);
        av_buffer_unref(&b);
    }
    return 0;
}
static void *release_lease(void *opaque) {
    AVBufferRef *b=opaque;assert(b->data[0]==117);av_buffer_unref(&b);return 0;
}
int main(void) {
    pool=av_buffer_pool_init(4096,0);assert(pool);
    pthread_t threads[4];
    for(uintptr_t i=0;i<4;++i)assert(!pthread_create(&threads[i],0,worker,(void *)(i+1)));
    for(unsigned i=0;i<4;++i)assert(!pthread_join(threads[i],0));
    AVBufferRef *held=av_buffer_pool_get(pool);assert(held);held->data[0]=117;
    av_buffer_pool_uninit(&pool);assert(!pool);
    assert(!pthread_create(&threads[0],0,release_lease,held));assert(!pthread_join(threads[0],0));
    puts("PASS: 80000 concurrent pool leases, exclusive pixel ownership, references and final release after decoder pool teardown");
}
