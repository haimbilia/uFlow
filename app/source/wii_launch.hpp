#pragma once
#include "library.hpp"
#include "wii_request.hpp"

// Returns an actionable error, or fills the exact SD/FAT32 request without changing files.
std::string PrepareWiiLaunch(const GameEntry &game, WiiRequest::Request &request);
std::string PrepareGameCubeLaunch(const GameEntry &game, WiiRequest::Request &request);
// Publishes a one-shot request and asks CafeOS to launch the DWFA vWii channel.
// Returns empty only if the OS accepted the transition.
std::string LaunchWiiGame(const WiiRequest::Request &request);
void CancelWiiLaunchRequest();
