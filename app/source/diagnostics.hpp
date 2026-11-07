#pragma once
#include <memory>
#include <string>
namespace UFlowSupport {
// No per-frame storage writes. The UI only updates an in-memory heartbeat.
class FreezeWatch {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    explicit FreezeWatch(std::string root, int timeoutMs=30000);
    ~FreezeWatch();
    void Active(bool active);
    // Pausing monitoring never declares an orderly exit or erases evidence.
    void Transition(const std::string &stage);
    void CleanExit();
    void Beat(int phase=0); // 0 events, 1 update/input, 2 rendering
    bool Pending() const;
    void Acknowledge();
};
struct Snapshot {
    bool busy=false, recording=false, saved=false;
    std::string message="Reports are sent only after confirmation.", reportId;
};
// Fixed allowlist only. No remote listener, command execution or arbitrary files.
std::string Redact(const std::string &text);
std::string Quote(const std::string &text);
class Client {
    struct Impl; std::unique_ptr<Impl> impl;
public:
    explicit Client(std::string root="fs:/vol/external01/wiiu/apps/uFlow");
    ~Client();
    Snapshot Poll();
    void Event(const std::string &event);
    void Record(bool enabled);
    // Save locally, then upload. Call only following explicit consent.
    void Send(std::size_t games, std::size_t libraries, std::size_t media);
};
}

// SDL callbacks publish numeric checkpoints only: no allocation, locks or file I/O.
extern "C" void UFlow_HandoffStage(unsigned stage);
