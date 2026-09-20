#pragma once
#include "update_model.hpp"
#include <memory>
namespace UFlowUpdate {
class Client {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    Client();~Client();
    Snapshot Poll();
    void Check();void Accept();void Dismiss();void Stop();
};
}
