#pragma once

// Development-key-only snapshots. No allocation, file I/O, thread suspension,
// heap locking, or modification of the inspected threads/heap.
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/debug.h>
#include <coreinit/memorymap.h>
#include <coreinit/memheap.h>
#include <coreinit/thread.h>
#include <cstdint>
#include <atomic>
#include "heap_owner_probe.hpp"

namespace ThreadDiagnostics {
inline bool Readable(const void *pointer, unsigned bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address && bytes && address <= UINT32_MAX - (bytes - 1) &&
        OSIsAddressValid(address) && OSIsAddressValid(address + bytes - 1);
}

inline unsigned HeapOwner() {
    const auto *heap=reinterpret_cast<const volatile MEMHeapHeader *>(0x80000000u);
    return Readable(const_cast<const MEMHeapHeader *>(heap),sizeof(*heap)) &&
        heap->tag==MEM_EXPANDED_HEAP_TAG ? heap->lock.owner : 0;
}

// Static storage avoids allocating from the heap being investigated. Opted in
// only by the existing development key; normal installations create no thread.
class OwnerWatch {
    alignas(8) OSThread thread_{};
    alignas(32) unsigned char stack_[32*1024]{};
    std::atomic<bool> running_{false};
    bool started_=false;
    HeapOwnerProbe::Detector foregroundDetector_;
    static int Run(int,const char **argument) {
        auto &self=*reinterpret_cast<OwnerWatch *>(const_cast<char **>(argument));
        HeapOwnerProbe::Detector detector;
        while(self.running_.load()) {
            const auto owner=HeapOwner();
            if(detector.Sample(owner,OSTicksToMilliseconds(OSGetSystemTime())))Capture(owner);
            OSSleepTicks(OSMillisecondsToTicks(100));
        }
        return 0;
    }
    static void Capture(unsigned owner) {
        const auto *thread=reinterpret_cast<const volatile OSThread *>(owner);
        if(HeapOwner()!=owner)return;
        if(!Readable(const_cast<const OSThread *>(thread),sizeof(*thread))) {
            OSReport("uFlow: heap-owner-invalid owner=%08x unmapped\n",owner);return;
        }
        if(thread->tag!=OS_THREAD_TAG) {
            OSReport("uFlow: heap-owner-invalid owner=%08x tag=%08x\n",owner,unsigned(thread->tag));return;
        }
        char name[48]{};
        const auto *source=thread->name;
        for(unsigned i=0;i+1<sizeof(name) && HeapOwner()==owner && Readable(source,1);++i,++source) {
            name[i]=*source;if(!name[i])break;
        }
        const unsigned state=thread->state,affinity=thread->attr;
        const int priority=thread->priority,base=thread->basePriority,suspend=thread->suspendCounter;
        const unsigned pc=thread->context.srr0,lr=thread->context.lr,sp=thread->context.gpr[1];
        const unsigned r3=thread->context.gpr[3],r4=thread->context.gpr[4],locks=thread->context.spinLockCount;
        const auto low=reinterpret_cast<std::uintptr_t>(thread->stackEnd);
        const auto high=reinterpret_cast<std::uintptr_t>(thread->stackStart);
        HeapOwnerProbe::Frame frames[24]{};
        const auto count=HeapOwnerProbe::Stack(sp,low,high,frames,24,[owner](unsigned address,unsigned &value) {
            const auto *word=reinterpret_cast<const volatile unsigned *>(address);
            if(HeapOwner()!=owner || !Readable(const_cast<const unsigned *>(word),sizeof(*word)))return false;
            value=*word;return true;
        });
        if(HeapOwner()!=owner)return;
        OSReport("uFlow: heap-owner-stall owner=%08x name=%s state=%u affinity=%u priority=%d/%d suspend=%d locks=%u pc=%08x lr=%08x sp=%08x r3=%08x r4=%08x stack=%08x..%08x\n",
            owner,name,state,affinity,priority,base,suspend,locks,pc,lr,sp,r3,r4,unsigned(low),unsigned(high));
        for(unsigned i=0;i<count;++i)OSReport("uFlow: heap-owner-frame %u sp=%08x lr=%08x\n",i,frames[i].address,frames[i].link);
        OSReport("uFlow: heap-owner-stall-end current=%08x\n",HeapOwner());
    }
public:
    void PollForeground() {
        // A file I/O waiter can disable scheduling on core 0, preventing the
        // observer there from running while the UI is still responsive.
        // Use an independent detector here; neither observer acquires the heap.
        if(!started_)return;
        const auto owner=HeapOwner();
        if(foregroundDetector_.Sample(owner,OSTicksToMilliseconds(OSGetSystemTime())))Capture(owner);
    }
    void Start(bool enabled) {
        if(!enabled || started_)return;
        static_assert(std::atomic<bool>::is_always_lock_free);
        foregroundDetector_={};
        running_.store(true);
        // Core 0 remained available in the Nintendo Land stall while cores 1/2
        // spun. This cannot promise a capture if all CPUs stop scheduling.
        if(!OSCreateThread(&thread_,Run,0,reinterpret_cast<char *>(this),
                          stack_+sizeof(stack_),sizeof(stack_),0,OS_THREAD_ATTRIB_AFFINITY_CPU0)) {
            running_.store(false);OSReport("uFlow: heap-owner-watch unavailable\n");return;
        }
        OSSetThreadName(&thread_,"uflow-heap-watch");
        started_=true;OSResumeThread(&thread_);
        OSReport("uFlow: heap-owner-watch started\n");
    }
    void Stop() {
        if(!started_)return;
        running_.store(false);OSJoinThread(&thread_,nullptr);started_=false;
    }
};

inline unsigned Dump() {
    // This address is diagnostic evidence from the console heartbeat dump,
    // not an allocator handle: never pass it to a heap operation.
    const auto *heap = reinterpret_cast<const volatile MEMHeapHeader *>(0x80000000u);
    const unsigned owner = Readable(const_cast<const MEMHeapHeader *>(heap), sizeof(*heap)) &&
        heap->tag == MEM_EXPANDED_HEAP_TAG ? heap->lock.owner : 0;
    OSReport("uFlow: thread-snapshot heap=80000000 owner=%08x\n", owner);
    OSThread *seen[96]{};
    unsigned count = 0;
    auto *self = OSGetCurrentThread();
    // Walk both directions from the current thread; tolerate an ending thread,
    // a changed list or a cycle by limiting and validating every observation.
    for (unsigned direction = 0; direction < 2; ++direction) {
        auto *thread = self;
        if (direction) {
            OSThreadLink link{};
            OSGetActiveThreadLink(self, &link);
            thread = link.prev;
        }
        while (thread && count < 96) {
            if (!Readable(thread, sizeof(*thread)) || thread->tag != OS_THREAD_TAG) break;
            bool duplicate = false;
            for (unsigned i = 0; i < count; ++i) if (seen[i] == thread) duplicate = true;
            if (duplicate) break;
            seen[count++] = thread;
            char name[64]{};
            const auto *source = thread->name;
            for (unsigned i = 0; i + 1 < sizeof(name) && Readable(source, 1); ++i, ++source) {
                name[i] = *source;
                if (!name[i]) break;
            }
            OSReport("uFlow: thread=%p name=%s state=%u priority=%d/%d suspend=%d entry=%p pc=%08x sp=%08x stack=%p..%p\n",
                thread, name, unsigned(thread->state), int(thread->priority), int(thread->basePriority),
                int(thread->suspendCounter), reinterpret_cast<void *>(thread->entryPoint),
                unsigned(thread->context.srr0), unsigned(thread->context.gpr[1]), thread->stackEnd, thread->stackStart);
            OSThreadLink link{};
            OSGetActiveThreadLink(thread, &link);
            thread = direction ? link.prev : link.next;
        }
    }
    OSReport("uFlow: thread-snapshot-end count=%u\n", count);
    return count;
}
}
#endif
