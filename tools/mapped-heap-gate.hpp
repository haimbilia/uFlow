#pragma once
// One recursive gate is shared by all mapped heaps: nested operations cannot
// invert a pair of heap locks. Normal application heaps bypass the gate.
template<class Mutex> class MappedHeapGuard {
    Mutex *mutex_;
public:
    explicit MappedHeapGuard(Mutex *mutex):mutex_(mutex) { if(mutex_)mutex_->lock(); }
    ~MappedHeapGuard() { if(mutex_)mutex_->unlock(); }
    MappedHeapGuard(const MappedHeapGuard&)=delete;
    MappedHeapGuard& operator=(const MappedHeapGuard&)=delete;
};
