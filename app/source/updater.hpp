#pragma once
#include "update_model.hpp"
#include <memory>
namespace UFlowUpdate {
class Client {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    Client();~Client();
    Snapshot Poll();
    void Check(bool checkVersion=true);void Accept();void Dismiss(bool skipVersion=false);void Stop();
    void HandoffFailed(const std::string &message);
};
}
