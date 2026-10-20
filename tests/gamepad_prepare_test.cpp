#include "../wii/gamepad-probe/prepare.hpp"
#include <cassert>
#include <vector>
struct Backend {
    int value=0; bool read=true, journal=true, enable=true, prepare=true,
        restore=true, cleanup=true, pending=false;
    std::vector<int> calls;
    bool Read(int &out) { calls.push_back(0); out=value; return read; }
    bool Journal(int original) { assert(original==0); calls.push_back(1); pending=journal; return journal; }
    bool Set(int v) { calls.push_back(v ? 2 : 4); value=v; return v ? enable : restore; }
    bool Prepare() { calls.push_back(3); assert(value==1); return prepare; }
    bool ClearJournal() { calls.push_back(5); if(cleanup) pending=false; return cleanup; }
};
int main() {
    using namespace GamePadProbe;
    Backend b;
    assert(PrepareWithDrc(b)==Preparation::Ready);
    assert(b.value==0 && !b.pending && b.calls==std::vector<int>({0,1,2,3,4,5}));
    b={}; b.value=1;
    assert(PrepareWithDrc(b)==Preparation::Ready && b.calls==std::vector<int>({0,3}));
    b={}; b.prepare=false;
    assert(PrepareWithDrc(b)==Preparation::PrepareFailed && b.value==0 && !b.pending);
    b={}; b.enable=false;
    assert(PrepareWithDrc(b)==Preparation::EnableFailed && b.value==0 && !b.pending);
    assert(b.calls==std::vector<int>({0,1,2,4,5}));
    b={}; b.restore=false;
    assert(PrepareWithDrc(b)==Preparation::RestoreFailed && b.pending && b.calls.back()==4);
    b={}; b.journal=false;
    assert(PrepareWithDrc(b)==Preparation::JournalFailed && b.value==0 && b.calls.back()==1);
    b={}; b.cleanup=false;
    assert(PrepareWithDrc(b)==Preparation::CleanupFailed && b.pending && b.value==0);
    b={}; b.read=false;
    assert(PrepareWithDrc(b)==Preparation::ReadFailed && b.calls.size()==1);
    b={}; b.value=2;
    assert(PrepareWithDrc(b)==Preparation::ReadFailed && b.calls.size()==1);
}
