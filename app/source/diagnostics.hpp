#pragma once
#include <memory>
#include <string>
namespace UFlowSupport {
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
