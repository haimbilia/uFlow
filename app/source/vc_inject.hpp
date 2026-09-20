#pragma once
#include "library.hpp"
#include "vc_systems.hpp"
#include <functional>
#include <string>

namespace VCInject {
struct Settings {
    std::string base; // SD-relative decrypted code/content/meta directory
    bool reuse = true;
    int compression = 1; // ZIP deflate: 0 (stored), 1, 6
    int brightness = -1; // -1 preserves the base configuration
    int upscale = -1; // -1 preserves the base configuration; 0/1
    int cacheMiB = 1024;
    std::string system = "ds";
};
using Progress = std::function<bool(const std::string &, unsigned long long, unsigned long long)>;
Settings Load(const std::string &id);
bool Save(const std::string &id, const Settings &settings);
std::string CheckBase(const Settings &settings);
// No writes to source/base/installed titles. Completed generations are immutable.
// Preparation uses private cache staging; publication moves new output beside the ROM.
// Saves live outside both and are keyed by ROM SHA-256 and base title ID.
std::string Prepare(GameEntry &game, const Settings &settings, Progress progress = {});
bool HasSaved(const GameEntry &rom);
// Publish an immutable generation beside the ROM; updates prepared's launch paths.
std::string SavePrepared(const GameEntry &rom, GameEntry &prepared, Progress progress = {});
// Loads the saved output, without reading the source ROM/base or re-injecting.
std::string LaunchSaved(GameEntry &rom, Progress progress = {});
// Play reuses a saved injection, or prepares and records a missing one before
// returning the raw launch target. Does not start a console title itself.
std::string LaunchOrPrepare(GameEntry &rom, const Settings &settings, Progress progress = {});
}
