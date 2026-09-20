#pragma once
#include <cstdint>
#include <string>
namespace UFlowUpdate {
#ifndef UFLOW_UPDATE_SEQUENCE
#define UFLOW_UPDATE_SEQUENCE 2026092001ULL
#endif
inline constexpr uint64_t BuildSequence=UFLOW_UPDATE_SEQUENCE;
inline constexpr const char *BaseURL="https://karotech.io/updates/uFlow/";
enum class State {Idle,Checking,Available,Downloading,Installing,Success,Error};
struct Snapshot {
    State state=State::Idle;
    std::string version,notes,message;
    uint64_t done=0,total=0;
};
inline bool Busy(State s){return s==State::Downloading||s==State::Installing;}
}
