#pragma once

#include <cstddef>
#include <string>
#include <vector>

enum class Platform {
    WiiU,
    Wii,
    GameCube,
};

struct GameEntry {
    Platform platform = Platform::WiiU;
    std::string name;
    std::string publisher;
    std::string titleId;
    std::string folder;
    std::string absolutePath;
    std::string coverPath;
    std::string rpxRelative;
    std::string contentRelative;
    std::string codeRelative;
    std::string saveRelative;
    bool favorite = false;
};

struct ScanResult {
    std::vector<GameEntry> games;
    std::size_t invalidWiiU = 0;
};

const char *PlatformName(Platform platform);
ScanResult ScanLibrary();
void SaveFavorites(const std::vector<GameEntry> &games);

