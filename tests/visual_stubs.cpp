#include "library.hpp"
#include "settings.hpp"
UserSettings LoadSettings() { return {}; }
void SaveSettings(const UserSettings &) {}
bool SetStartOnBoot(bool) { return true; }
const char *OnOff(bool value) { return value ? "ON" : "OFF"; }
const char *PlatformName(Platform value) { return value == Platform::WiiU ? "WII U" : value == Platform::Wii ? "WII" : "GAMECUBE"; }
std::vector<ContentSource> LoadContentSources() { return {}; }
bool SaveContentSources(const std::vector<ContentSource> &) { return true; }
void SaveFavorites(const std::vector<GameEntry> &) {}
std::vector<std::string> ListDirectories(const std::string &) { return {}; }
bool DirectoryExists(const std::string &) { return false; }
