#include "mapped-heap-gate.hpp"
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <cstdio>
int main() {
    std::recursive_mutex gate;
    std::atomic<int> inside{0}, completed{0};
    std::vector<std::thread> workers;
    // Emulate allocation, free, resize and a nested visitor on different cores.
    for(int i=0;i<4;++i) workers.emplace_back([&] {
        for(int n=0;n<25000;++n) {
            MappedHeapGuard<std::recursive_mutex> lock(&gate);
            assert(inside.fetch_add(1)==0);
            { MappedHeapGuard<std::recursive_mutex> nested(&gate); }
            if(n%256==0) std::this_thread::yield();
            assert(inside.fetch_sub(1)==1);
            ++completed;
        }
    });
    for(auto &worker:workers)worker.join();
    assert(completed==100000 && inside==0);
    // Unmapped heaps bypass the shared gate; early return and exceptions release.
    {MappedHeapGuard<std::recursive_mutex> bypass(nullptr);}
    try {MappedHeapGuard<std::recursive_mutex> lock(&gate);throw 1;}catch(int){}
    std::thread other([&]{assert(gate.try_lock());gate.unlock();});other.join();
    puts("PASS: 100000 concurrent mapped-heap operations, recursion, bypass and release");
}
