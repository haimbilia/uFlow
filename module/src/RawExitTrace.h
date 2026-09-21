#pragma once
#include <atomic>
#include <cstdint>

// Latched for the application lifetime: raw profile cleanup happens before all
// exit hooks have returned. Do not lose diagnostics when active is cleared.
class RawExitTraceState {
public:
    void Begin(bool raw){enabled.store(raw);lastStatus.store(-1);}
    bool Enabled() const{return enabled.load();}
    bool Changed(int status){return Enabled() && lastStatus.exchange(status)!=status;}
private:
    std::atomic<bool> enabled{false};
    std::atomic<int> lastStatus{-1};
};
void RawExitTraceBegin(bool raw);
void RawExitTracePoint(const char *point);
