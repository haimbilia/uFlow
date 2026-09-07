#include "library.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kDevice = "fs:/vol/external01";
constexpr const char *kAppDirectory = "fs:/vol/external01/wiiu/apps/uFlow";
constexpr const char *kFavoritesFile = "fs:/vol/external01/wiiu/apps/uFlow/favorites.txt";

bool IsDirectory(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool IsFile(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool EnsureDirectory(const std::string &path) {
    if (IsDirectory(path)) return true;
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && !EnsureDirectory(path.substr(0, slash))) return false;
    return mkdir(path.c_str(), 0777) == 0 || IsDirectory(path);
}

std::string ReadTextFile(const std::string &path, std::size_t maximum = 256 * 1024) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::string result(maximum, '\0');
    input.read(result.data(), static_cast<std::streamsize>(result.size()));
    result.resize(static_cast<std::size_t>(input.gcount()));
    return result;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string Trim(std::string value) {
    const auto visible = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), visible));
    value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(), value.end());
    return value;
}

void ReplaceAll(std::string &value, const std::string &from, const std::string &to) {
    std::size_t offset = 0;
    while ((offset = value.find(from, offset)) != std::string::npos) {
        value.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string XmlValue(const std::string &xml, const std::string &tag) {
    const std::string opening = "<" + tag;
    const std::string closing = "</" + tag + ">";
    const auto begin = xml.find(opening);
    if (begin == std::string::npos) return {};
    const auto valueBegin = xml.find('>', begin + opening.size());
    const auto end = valueBegin == std::string::npos ? std::string::npos : xml.find(closing, valueBegin + 1);
    if (end == std::string::npos) return {};
    std::string value = xml.substr(valueBegin + 1, end - valueBegin - 1);
    ReplaceAll(value, "&amp;", "&");
    ReplaceAll(value, "&quot;", "\"");
    ReplaceAll(value, "&apos;", "'");
    return Trim(value);
}

std::string SaveKey(const std::string &titleId, const std::string &folder) {
    const std::string &source = titleId.empty() ? folder : titleId;
    std::string result;
    for (unsigned char c : source) {
        if (std::isalnum(c)) result.push_back(static_cast<char>(std::tolower(c)));
        else if (!result.empty() && result.back() != '_') result.push_back('_');
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "unknown" : result;
}

std::string FindMainRpx(const std::string &base) {
    const std::string codePath = base + "/code";
    std::string configured = XmlValue(ReadTextFile(codePath + "/cos.xml"), "argstr");
    const auto extension = Lower(configured).find(".rpx");
    if (extension != std::string::npos) {
        configured.resize(extension + 4);
        const auto slash = configured.find_last_of("/\\");
        if (slash != std::string::npos) configured = configured.substr(slash + 1);
        if (IsFile(codePath + "/" + configured)) return configured;
    }

    DIR *directory = opendir(codePath.c_str());
    if (!directory) return {};
    std::vector<std::string> candidates;
    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".rpx" && IsFile(codePath + "/" + name)) {
            candidates.push_back(name);
        }
    }
    closedir(directory);
    std::sort(candidates.begin(), candidates.end());
    return candidates.empty() ? std::string() : candidates.front();
}

std::string CleanDiscName(std::string value) {
    const auto extension = value.find_last_of('.');
    if (extension != std::string::npos) value.resize(extension);
    const auto bracket = value.rfind(" [");
    if (bracket != std::string::npos && value.back() == ']') value.resize(bracket);
    std::replace(value.begin(), value.end(), '_', ' ');
    return Trim(value);
}

std::string ExtractId(const std::string &value) {
    const auto close = value.rfind(']');
    const auto open = close == std::string::npos ? std::string::npos : value.rfind('[', close);
    if (open != std::string::npos && close - open == 7) return value.substr(open + 1, 6);
    return {};
}

std::string FindCover(const std::string &base, const std::string &id, const std::string &wiiUMeta = {}) {
    const std::vector<std::string> candidates = {
        base + "/cover.png",
        base + "/cover.tga",
        wiiUMeta,
        id.empty() ? std::string() : std::string(kDevice) + "/covers/" + id + ".png",
        id.empty() ? std::string() : std::string(kDevice) + "/wiiu/apps/usbloader_gx/images/" + id + ".png",
    };
    for (const auto &candidate : candidates) {
        if (!candidate.empty() && IsFile(candidate)) return candidate;
    }
    return {};
}

void ScanWiiURoot(ScanResult &result, const std::string &relativeRoot) {
    const std::string absoluteRoot = std::string(kDevice) + "/" + relativeRoot;
    DIR *directory = opendir(absoluteRoot.c_str());
    if (!directory) return;
    while (dirent *entry = readdir(directory)) {
        const std::string folder = entry->d_name;
        if (folder == "." || folder == "..") continue;
        const std::string base = absoluteRoot + "/" + folder;
        if (!IsDirectory(base)) continue;
        const std::string rpx = FindMainRpx(base);
        if (rpx.empty() || !IsDirectory(base + "/content")) {
            ++result.invalidWiiU;
            continue;
        }
        const std::string meta = ReadTextFile(base + "/meta/meta.xml");
        const std::string app = ReadTextFile(base + "/code/app.xml");
        GameEntry game;
        game.platform = Platform::WiiU;
        game.folder = folder;
        game.absolutePath = base;
        game.name = XmlValue(meta, "longname_en");
        if (game.name.empty()) game.name = XmlValue(meta, "shortname_en");
        if (game.name.empty()) game.name = folder;
        game.publisher = XmlValue(meta, "publisher_en");
        game.titleId = Lower(XmlValue(meta, "title_id"));
        if (game.titleId.empty()) game.titleId = Lower(XmlValue(app, "title_id"));
        game.coverPath = FindCover(base, game.titleId, base + "/meta/iconTex.tga");
        const std::string relativeBase = relativeRoot + "/" + folder;
        game.rpxRelative = relativeBase + "/code/" + rpx;
        game.contentRelative = relativeBase + "/content";
        game.codeRelative = relativeBase + "/code";
        game.saveRelative = "wiiu/raw-saves/" + SaveKey(game.titleId, folder);
        result.games.push_back(std::move(game));
    }
    closedir(directory);
}

void AddDiscGame(ScanResult &result, Platform platform, const std::string &path, const std::string &displaySource) {
    GameEntry game;
    game.platform = platform;
    game.absolutePath = path;
    game.folder = displaySource;
    game.titleId = ExtractId(displaySource);
    game.name = CleanDiscName(displaySource);
    game.coverPath = FindCover(path.substr(0, path.find_last_of('/')), game.titleId);
    result.games.push_back(std::move(game));
}

void ScanWiiRoot(ScanResult &result) {
    const std::string root = std::string(kDevice) + "/wbfs";
    DIR *directory = opendir(root.c_str());
    if (!directory) return;
    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = root + "/" + name;
        if (IsFile(path) && name.size() > 5 && Lower(name.substr(name.size() - 5)) == ".wbfs") {
            AddDiscGame(result, Platform::Wii, path, name);
        } else if (IsDirectory(path)) {
            DIR *nested = opendir(path.c_str());
            if (!nested) continue;
            while (dirent *child = readdir(nested)) {
                const std::string childName = child->d_name;
                const std::string childPath = path + "/" + childName;
                if (childName.size() > 5 && Lower(childName.substr(childName.size() - 5)) == ".wbfs" && IsFile(childPath)) {
                    AddDiscGame(result, Platform::Wii, childPath, name);
                    break;
                }
            }
            closedir(nested);
        }
    }
    closedir(directory);
}

void ScanGameCubeRoot(ScanResult &result) {
    const std::string root = std::string(kDevice) + "/games";
    DIR *directory = opendir(root.c_str());
    if (!directory) return;
    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = root + "/" + name;
        if (IsFile(path) && name.size() > 4 && Lower(name.substr(name.size() - 4)) == ".iso") {
            AddDiscGame(result, Platform::GameCube, path, name);
        } else if (IsDirectory(path)) {
            const std::vector<std::string> candidates = {path + "/game.iso", path + "/disc2.iso"};
            for (const auto &candidate : candidates) {
                if (IsFile(candidate)) {
                    AddDiscGame(result, Platform::GameCube, candidate, name);
                    break;
                }
            }
        }
    }
    closedir(directory);
}

std::string FavoriteKey(const GameEntry &game) {
    return std::to_string(static_cast<int>(game.platform)) + "|" + game.absolutePath;
}

std::set<std::string> LoadFavoriteKeys() {
    std::set<std::string> keys;
    std::ifstream input(kFavoritesFile);
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (!line.empty()) keys.insert(line);
    }
    return keys;
}

} // namespace

const char *PlatformName(Platform platform) {
    switch (platform) {
        case Platform::WiiU: return "WII U";
        case Platform::Wii: return "WII";
        case Platform::GameCube: return "GAMECUBE";
    }
    return "UNKNOWN";
}

ScanResult ScanLibrary() {
    ScanResult result;
    EnsureDirectory(std::string(kDevice) + "/wiiu/raw-saves");
    EnsureDirectory(kAppDirectory);
    ScanWiiURoot(result, "wiiu/raw-games");
    ScanWiiURoot(result, "wiiu/games");
    ScanWiiRoot(result);
    ScanGameCubeRoot(result);

    const auto favorites = LoadFavoriteKeys();
    for (auto &game : result.games) game.favorite = favorites.contains(FavoriteKey(game));
    std::sort(result.games.begin(), result.games.end(), [](const GameEntry &left, const GameEntry &right) {
        const std::string leftName = Lower(left.name);
        const std::string rightName = Lower(right.name);
        if (leftName != rightName) return leftName < rightName;
        return left.absolutePath < right.absolutePath;
    });
    result.games.erase(std::unique(result.games.begin(), result.games.end(), [](const GameEntry &left, const GameEntry &right) {
        return left.absolutePath == right.absolutePath;
    }), result.games.end());
    return result;
}

void SaveFavorites(const std::vector<GameEntry> &games) {
    EnsureDirectory(kAppDirectory);
    std::ofstream output(kFavoritesFile, std::ios::trunc);
    for (const auto &game : games) {
        if (game.favorite) output << FavoriteKey(game) << '\n';
    }
}
