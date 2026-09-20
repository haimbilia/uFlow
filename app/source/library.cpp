#include "library.hpp"
#include "browser_path.hpp"
#include "directory_entry.hpp"
#include "case_profiles.hpp"
#include "wii_request.hpp"
#include "saved_file.hpp"
#include "vc_package.hpp"

#include <coreinit/mcp.h>
#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kDevice = "fs:/vol/external01";
constexpr const char *kAppDirectory = "fs:/vol/external01/wiiu/apps/uFlow";
constexpr const char *kFavoritesFile = "fs:/vol/external01/wiiu/apps/uFlow/favorites.txt";
constexpr const char *kSourcesFile = "fs:/vol/external01/wiiu/apps/uFlow/content_sources.cfg";
constexpr const char *kLibrariesFile = "fs:/vol/external01/wiiu/apps/uFlow/libraries.cfg";
constexpr const char *kLibraryCacheFile = "fs:/vol/external01/wiiu/apps/uFlow/cache/library-v1.bin";
constexpr std::uint32_t kLibraryCacheMagic = 0x55464C43; // UFLC
constexpr std::uint32_t kLibraryCacheVersion = 4; // Separate cover faces and system presets.
constexpr std::uint32_t kLibraryCacheMaxEntries = 8192;
constexpr std::uint32_t kLibraryCacheMaxString = 256 * 1024;
constexpr std::size_t kLibraryCacheMaxBytes = 32 * 1024 * 1024;

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

std::string CacheInstalledIcon(const std::string &sourcePath, const std::string &titleId) {
    const std::string directory = std::string(kAppDirectory) + "/cache/icons";
    if (!EnsureDirectory(directory)) return {};
    const std::string destination = directory + "/" + titleId + ".tga";
    if (IsFile(destination)) return destination;
    std::ifstream input(sourcePath, std::ios::binary);
    if (!input) return {};
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) return {};
    output << input.rdbuf();
    output.close();
    return IsFile(destination) ? destination : std::string();
}

std::string ReadTextFile(const std::string &path, std::size_t maximum = 256 * 1024) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length <= 0 || static_cast<std::uint64_t>(length) > maximum) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    input.seekg(0, std::ios::beg);
    input.read(result.data(), length);
    result.resize(static_cast<std::size_t>(input.gcount()));
    return result;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string FileName(const std::string &path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string RelativeLaunchPath(const std::string &path) {
    constexpr const char *prefix = "fs:/vol/external01/";
    if (path.rfind(prefix, 0) == 0) return path.substr(std::strlen(prefix));
    return path;
}

std::string Trim(std::string value);

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

std::string AsFsPath(std::string path) {
    if (path.rfind("/vol/", 0) == 0) path.insert(0, "fs:");
    return path;
}

std::string FormatTitleId(std::uint64_t titleId) {
    char text[17]{};
    std::snprintf(text, sizeof(text), "%08X%08X", static_cast<unsigned>(titleId >> 32),
                  static_cast<unsigned>(titleId));
    return Lower(text);
}

bool ScanInstalledGames(ScanResult &result, const std::vector<GameEntry> *cached = nullptr) {
    const int32_t handle = static_cast<int32_t>(MCP_Open());
    if (handle < 0) return false;
    const int32_t count = static_cast<int32_t>(MCP_TitleCount(handle));
    if (count <= 0 || count > 4096) {
        MCP_Close(handle);
        return count == 0;
    }
    const std::size_t bytes = static_cast<std::size_t>(count) * sizeof(MCPTitleListType);
    auto *titles = static_cast<MCPTitleListType *>(memalign(0x40, bytes));
    if (!titles) {
        MCP_Close(handle);
        return false;
    }
    std::memset(titles, 0, bytes);
    uint32_t received = static_cast<uint32_t>(count);
    const int32_t listResult = static_cast<int32_t>(MCP_TitleList(handle, &received, titles, bytes));
    MCP_Close(handle);
    if (listResult >= 0) {
        for (uint32_t index = 0; index < std::min(received, static_cast<uint32_t>(count)); ++index) {
            const auto &title = titles[index];
            if (title.appType != MCP_APP_TYPE_GAME || (title.titleId >> 32) != 0x00050000ULL) continue;
            GameEntry game;
            game.platform = Platform::WiiU;
            game.installed = true;
            game.installedTitleId = title.titleId;
            game.titleId = FormatTitleId(title.titleId);
            game.absolutePath = AsFsPath(std::string(title.path, strnlen(title.path, sizeof(title.path))));
            game.storage = std::string(title.indexedDevice, strnlen(title.indexedDevice, sizeof(title.indexedDevice)));
            // MCP can also describe redirected titles. An installed entry must
            // point at an actual installed title on MLC or Wii U USB storage.
            if (game.absolutePath.rfind("fs:/vol/storage_mlc01/usr/title/00050000/", 0) != 0 &&
                game.absolutePath.rfind("fs:/vol/storage_usb01/usr/title/00050000/", 0) != 0) continue;
            if (!IsDirectory(game.absolutePath + "/code") || !IsFile(game.absolutePath + "/code/app.xml")) continue;
            if (cached) {
                const auto previous = std::find_if(cached->begin(), cached->end(), [&](const auto &entry) {
                    return entry.installed && entry.installedTitleId == game.installedTitleId &&
                        entry.absolutePath == game.absolutePath;
                });
                if (previous != cached->end()) {
                    result.games.push_back(*previous);
                    continue;
                }
            }
            const std::string metaPath = game.absolutePath + "/meta";
            const std::string meta = ReadTextFile(metaPath + "/meta.xml");
            game.name = XmlValue(meta, "longname_en");
            if (game.name.empty()) game.name = XmlValue(meta, "shortname_en");
            if (game.name.empty()) game.name = "INSTALLED TITLE " + game.titleId;
            game.publisher = XmlValue(meta, "publisher_en");
            game.coverPath = FindCover(metaPath, game.titleId);
            if (game.coverPath.empty()) game.coverPath = CacheInstalledIcon(metaPath + "/iconTex.tga", game.titleId);
            result.games.push_back(std::move(game));
        }
    }
    free(titles);
    return listResult >= 0;
}

using UpdateMetadata = std::map<std::string, std::pair<unsigned long, GameEntry>>;

void ReadWiiUMetadata(GameEntry &game, const std::string &base, const std::string &meta) {
    game.name = XmlValue(meta, "longname_en");
    if (game.name.empty()) game.name = XmlValue(meta, "shortname_en");
    game.publisher = XmlValue(meta, "publisher_en");
    game.coverPath = FindCover(base, game.titleId, base + "/meta/iconTex.tga");
}

// Returns true for a title directory, including updates/DLC. Do not descend
// into its code/content assets looking for more launchable games.
bool ScanWiiUPath(ScanResult &result, UpdateMetadata &updates, const std::string &base) {
    const std::string app = ReadTextFile(base + "/code/app.xml");
    const std::string appTitleId = Lower(XmlValue(app, "title_id"));
    if (appTitleId.size() == 16 && appTitleId.substr(0, 8) != "00050000") {
        if (appTitleId.substr(0, 8) == "0005000e") {
            GameEntry metadata;
            metadata.titleId = "00050000" + appTitleId.substr(8);
            ReadWiiUMetadata(metadata, base, ReadTextFile(base + "/meta/meta.xml"));
            const auto version = std::strtoul(XmlValue(app, "title_version").c_str(), nullptr, 16);
            const auto previous = updates.find(metadata.titleId);
            if (previous == updates.end() || previous->second.first < version) {
                const auto baseId = metadata.titleId;
                updates[baseId] = {version, std::move(metadata)};
            }
        }
        return true;
    }
    const std::string rpx = FindMainRpx(base);
    const bool hasCode = IsDirectory(base + "/code");
    const bool hasContent = IsDirectory(base + "/content");
    if (rpx.empty() || !hasContent) {
        if (hasCode || hasContent) ++result.invalidWiiU;
        return hasCode || hasContent;
    }
    const std::string meta = ReadTextFile(base + "/meta/meta.xml");
    GameEntry game;
    game.platform = Platform::WiiU;
    game.folder = FileName(base);
    game.absolutePath = base;
    // app.xml identifies the executable type. An update's meta.xml may contain
    // the BASE title ID, so it must never decide whether this is a base game.
    game.titleId = appTitleId.empty() ? Lower(XmlValue(meta, "title_id")) : appTitleId;
    ReadWiiUMetadata(game, base, meta);
    const std::string relativeBase = RelativeLaunchPath(base);
    game.rpxRelative = relativeBase + "/code/" + rpx;
    game.contentRelative = relativeBase + "/content";
    game.codeRelative = relativeBase + "/code";
    game.saveRelative = "wiiu/raw-saves/" + SaveKey(game.titleId, game.folder);
    result.games.push_back(std::move(game));
    return true;
}

void ScanWiiURoot(ScanResult &result, UpdateMetadata &updates, const std::string &absoluteRoot, int depth) {
    if(VCPackage::IsPackageName(absoluteRoot))return;
    if (!IsDirectory(absoluteRoot)) return;
    if (ScanWiiUPath(result, updates, absoluteRoot) || depth <= 0) return;
    DIR *directory = opendir(absoluteRoot.c_str());
    if (!directory) return;
    while (dirent *entry = readdir(directory)) {
        const std::string folder = entry->d_name;
        if (folder == "." || folder == "..") continue;
        const std::string base = absoluteRoot + "/" + folder;
        if (!IsDirectory(base)) continue;
        ScanWiiURoot(result, updates, base, depth - 1);
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
    if (platform == Platform::GameCube) {
        std::string discTitle, discId;
        if (WiiRequest::CheckGameCube(path, {}, &discTitle, &discId).empty()) {
            game.titleId = discId; // real header ID, not an optional folder-name tag
            if (!discTitle.empty()) game.name = discTitle;
        }
    }
    game.coverPath = FindCover(path.substr(0, path.find_last_of('/')), game.titleId);
    result.games.push_back(std::move(game));
}

void ScanDiscRoot(ScanResult &result, Platform platform, const std::string &root, int depth) {
    DIR *directory = opendir(root.c_str());
    if (!directory) return;
    while (dirent *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = root + "/" + name;
        const std::string lower = Lower(name);
        const bool wii = platform == Platform::Wii && lower.size() > 5 && lower.substr(lower.size() - 5) == ".wbfs";
        const bool gameCube = platform == Platform::GameCube && lower.size() > 4 &&
                              lower.substr(lower.size() - 4) == ".iso" && lower != "disc2.iso";
        if (IsFile(path) && (wii || gameCube)) {
            const std::string parent = FileName(root);
            AddDiscGame(result, platform, path,
                        (lower == "game.iso" && !parent.empty()) ? parent : name);
        } else if (depth > 0 && IsDirectory(path)) {
            ScanDiscRoot(result, platform, path, depth - 1);
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

template <typename T>
bool WriteValue(std::ofstream &output, const T &value) {
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
    return static_cast<bool>(output);
}

template <typename T>
bool ReadValue(std::ifstream &input, T &value) {
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
    return static_cast<bool>(input);
}

bool WriteCacheString(std::ofstream &output, const std::string &value) {
    if (value.size() > kLibraryCacheMaxString) return false;
    const auto length = static_cast<std::uint32_t>(value.size());
    if (!WriteValue(output, length)) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(output);
}

struct CacheReader {
    std::ifstream input;
    std::size_t payloadBytes = 0;

    bool String(std::string &value) {
        std::uint32_t length = 0;
        if (!ReadValue(input, length) || length > kLibraryCacheMaxString ||
            length > kLibraryCacheMaxBytes - payloadBytes) return false;
        payloadBytes += length;
        value.assign(length, '\0');
        if (length) input.read(value.data(), static_cast<std::streamsize>(length));
        return static_cast<bool>(input);
    }
};

bool WriteCacheGame(std::ofstream &output, const GameEntry &game) {
    const auto platform = static_cast<std::uint8_t>(game.platform);
    const auto launcher = static_cast<std::uint8_t>(game.launcher);
    const std::uint8_t flags = (game.installed ? 1 : 0) | (game.favorite ? 2 : 0);
    if (!WriteValue(output, platform) || !WriteValue(output, launcher) || !WriteValue(output, flags) ||
        !WriteValue(output, game.installedTitleId)) return false;
    const std::array<const std::string *, 26> values{{
        &game.libraryId, &game.name, &game.publisher, &game.titleId, &game.folder, &game.absolutePath,
        &game.coverPath, &game.fullCoverPath, &game.backgroundPath, &game.logoPath, &game.previewPath,
        &game.description, &game.developer, &game.genre, &game.region, &game.rating, &game.releaseYear,
        &game.players, &game.rpxRelative, &game.contentRelative, &game.codeRelative, &game.saveRelative,
        &game.storage, &game.backCoverPath, &game.spinePath, &game.defaultSleevePath,
    }};
    for (const auto *value : values) if (!WriteCacheString(output, *value)) return false;
    return WriteValue(output,game.defaultBackEnd) && WriteValue(output,game.defaultFrontStart);
}

bool ReadCacheGame(CacheReader &reader, GameEntry &game) {
    std::uint8_t platform = 0, launcher = 0, flags = 0;
    if (!ReadValue(reader.input, platform) || !ReadValue(reader.input, launcher) || !ReadValue(reader.input, flags) ||
        !ReadValue(reader.input, game.installedTitleId) || platform > static_cast<std::uint8_t>(Platform::Files) ||
        launcher > static_cast<std::uint8_t>(GameLauncher::USBLoaderGX)) return false;
    game.platform = static_cast<Platform>(platform);
    game.launcher = static_cast<GameLauncher>(launcher);
    game.installed = (flags & 1) != 0;
    game.favorite = (flags & 2) != 0;
    const std::array<std::string *, 26> values{{
        &game.libraryId, &game.name, &game.publisher, &game.titleId, &game.folder, &game.absolutePath,
        &game.coverPath, &game.fullCoverPath, &game.backgroundPath, &game.logoPath, &game.previewPath,
        &game.description, &game.developer, &game.genre, &game.region, &game.rating, &game.releaseYear,
        &game.players, &game.rpxRelative, &game.contentRelative, &game.codeRelative, &game.saveRelative,
        &game.storage, &game.backCoverPath, &game.spinePath, &game.defaultSleevePath,
    }};
    for (auto *value : values) if (!reader.String(*value)) return false;
    if(!ReadValue(reader.input,game.defaultBackEnd) || !ReadValue(reader.input,game.defaultFrontStart))return false;
    if(!((game.defaultBackEnd==0 && game.defaultFrontStart==0) ||
         (game.defaultBackEnd>0 && game.defaultBackEnd<game.defaultFrontStart && game.defaultFrontStart<1)))return false;
    return !game.absolutePath.empty();
}

} // namespace

const char *PlatformName(Platform platform) {
    switch (platform) {
        case Platform::WiiU: return "WII U";
        case Platform::Wii: return "WII";
        case Platform::GameCube: return "GAMECUBE";
        case Platform::Files: return "FILES";
    }
    return "UNKNOWN";
}

const char *DefaultLibraryId(Platform platform) {
    switch (platform) {
        case Platform::WiiU: return "wiiu";
        case Platform::Wii: return "wii";
        case Platform::GameCube: return "gamecube";
        case Platform::Files: return "files";
    }
    return "library";
}

std::vector<LibraryDefinition> LoadLibraries() {
    std::vector<LibraryDefinition> libraries;
    auto input=SavedFile::Open(kLibrariesFile); std::string line;
    while (std::getline(input,line)) {
        std::vector<std::string> parts; std::size_t start=0, end;
        while ((end=line.find('\t',start))!=std::string::npos) { parts.push_back(line.substr(start,end-start)); start=end+1; }
        parts.push_back(line.substr(start)); if(parts.size()<7) continue;
        LibraryDefinition library; library.id=Trim(parts[0]); library.name=Trim(parts[1]);
        const auto parser=Lower(parts[2]); library.parser=parser=="wii"?Platform::Wii:parser=="gamecube"?Platform::GameCube:parser=="files"?Platform::Files:Platform::WiiU;
        library.launcher=Lower(parts[3])=="usbloadergx"?GameLauncher::USBLoaderGX:GameLauncher::WiiFlow;
        library.extensions=Trim(parts[4]); try { library.enabled=std::stoi(parts[5])!=0; library.order=std::stoi(parts[6]); } catch(...) { continue; }
        if(parts.size()>7) library.launcherId=Trim(parts[7]);
        const auto extensions=Lower(library.extensions);
        if(extensions==".nes")library.extensions=VCSystems::Find("nes")->extensions;
        const auto preset=CaseProfiles::For(library.id,library.launcherId,library.extensions);
        library.caseWidthMm=preset.width;library.caseHeightMm=preset.height;library.caseThicknessMm=preset.thickness;
        // Old configurations have no dimensions; retain their platform defaults.
        const auto dimension=[&](std::size_t index,int fallback,int minimum,int maximum) {
            if(parts.size()<=index) return fallback;
            try { std::size_t used=0; const auto value=Trim(parts[index]); int n=std::stoi(value,&used);
                return used==value.size()&&n>=minimum&&n<=maximum?n:fallback;
            } catch(...) { return fallback; }
        };
        library.caseWidthMm=dimension(8,library.caseWidthMm,50,200);
        library.caseHeightMm=dimension(9,library.caseHeightMm,50,250);
        library.caseThicknessMm=dimension(10,library.caseThicknessMm,2,40);
        // Older installers wrote DVD dimensions for every system. Upgrade only
        // that legacy tuple; custom sizes and versioned saves remain untouched.
        if(parts.size()<12 && library.caseWidthMm==135 && library.caseHeightMm==190 && library.caseThicknessMm==14) {
            library.caseWidthMm=preset.width;library.caseHeightMm=preset.height;library.caseThicknessMm=preset.thickness;
        }
        if(!library.id.empty()&&!library.name.empty()) libraries.push_back(std::move(library));
    }
    if(!input.is_open()) libraries={{"wiiu","Wii U",Platform::WiiU,GameLauncher::WiiFlow,".rpx",true,0},{"wii","Wii",Platform::Wii,GameLauncher::WiiFlow,".wbfs",true,1},{"gamecube","GameCube",Platform::GameCube,GameLauncher::WiiFlow,".iso,.gcm",true,2}};
    std::sort(libraries.begin(),libraries.end(),[](const auto&a,const auto&b){return a.order<b.order;}); return libraries;
}

bool SaveLibraries(const std::vector<LibraryDefinition> &libraries) {
    std::set<std::string> ids;
    for(const auto &l:libraries) if(l.id.empty() || l.name.empty() || !ids.insert(l.id).second ||
        (l.id+l.name+l.extensions+l.launcherId).find_first_of("\t\r\n")!=std::string::npos ||
        l.caseWidthMm<50||l.caseWidthMm>200||l.caseHeightMm<50||l.caseHeightMm>250||
        l.caseThicknessMm<2||l.caseThicknessMm>40) return false;
    EnsureDirectory(kAppDirectory); const std::string temporary=std::string(kLibrariesFile)+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    for(const auto &library:libraries) output<<library.id<<'\t'<<library.name<<'\t'<<DefaultLibraryId(library.parser)<<'\t'<<(library.launcher==GameLauncher::USBLoaderGX?"usbloadergx":"wiiflow")<<'\t'<<library.extensions<<'\t'<<library.enabled<<'\t'<<library.order<<'\t'<<library.launcherId<<'\t'<<library.caseWidthMm<<'\t'<<library.caseHeightMm<<'\t'<<library.caseThicknessMm<<"\t1\n";
    output.flush(); const bool ok=static_cast<bool>(output); output.close();
    const bool saved = ok && !output.fail() && SavedFile::Commit(temporary,kLibrariesFile);
    if (saved) InvalidateLibraryCache();
    return saved;
}

std::vector<ContentSource> LoadContentSources() {
    std::vector<ContentSource> sources;
    auto input=SavedFile::Open(kSourcesFile);
    const bool hasSavedConfiguration = static_cast<bool>(input);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        const auto third = second == std::string::npos ? std::string::npos : line.find('\t', second + 1);
        if (third == std::string::npos) continue;
        ContentSource source;
        const std::string type = Lower(line.substr(0, first));
        if (type == "wiiu") source.platform = Platform::WiiU;
        else if (type == "wii") source.platform = Platform::Wii;
        else if (type == "gamecube") source.platform = Platform::GameCube;
        else if (type == "files") source.platform = Platform::Files;
        else continue;
        try {
            source.enabled = std::stoi(line.substr(first + 1, second - first - 1)) != 0;
            source.depth = std::clamp(std::stoi(line.substr(second + 1, third - second - 1)), 0, 8);
        } catch (...) {
            continue;
        }
        const auto fourth = line.find('\t', third + 1);
        source.path = Trim(line.substr(third + 1, fourth == std::string::npos ? std::string::npos : fourth-third-1));
        source.libraryId = DefaultLibraryId(source.platform);
        if(fourth != std::string::npos) {
            const auto fifth = line.find('\t', fourth + 1);
            if(Trim(line.substr(fourth+1, fifth == std::string::npos ? std::string::npos : fifth-fourth-1)) == "usbloadergx")
                source.launcher = GameLauncher::USBLoaderGX;
            // Sixth field is optional so old installations keep working unchanged.
            if(fifth != std::string::npos) {
                const auto sixth = line.find('\t', fifth+1);
                const std::string library = Trim(line.substr(fifth+1, sixth==std::string::npos?std::string::npos:sixth-fifth-1));
                if(sixth!=std::string::npos) source.launcherId=Trim(line.substr(sixth+1));
                if(!library.empty()) source.libraryId = library;
            }
        }
        while (source.path.size() > 1 && source.path.back() == '/') source.path.pop_back();
        if (!source.path.empty()) sources.push_back(std::move(source));
    }
    if (!hasSavedConfiguration) {
        sources = {
            {Platform::WiiU, std::string(kDevice) + "/wiiu/raw-games", true, 3, GameLauncher::WiiFlow, "wiiu"},
            {Platform::WiiU, std::string(kDevice) + "/wiiu/games", true, 3, GameLauncher::WiiFlow, "wiiu"},
            {Platform::Wii, std::string(kDevice) + "/wbfs", true, 3, GameLauncher::WiiFlow, "wii"},
            {Platform::GameCube, std::string(kDevice) + "/games", true, 3, GameLauncher::WiiFlow, "gamecube"},
        };
    }
    return sources;
}

bool SaveContentSources(const std::vector<ContentSource> &sources) {
    EnsureDirectory(kAppDirectory);
    for(const auto &s:sources)if((s.path+s.libraryId+s.launcherId).find_first_of("\t\r\n")!=std::string::npos)return false;
    const std::string temporary=std::string(kSourcesFile)+".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    for (const auto &source : sources) {
        const char *type = DefaultLibraryId(source.platform);
        output << type << '\t' << (source.enabled ? 1 : 0) << '\t'
               << std::clamp(source.depth, 0, 8) << '\t' << source.path << '\t'
               << (source.launcher == GameLauncher::USBLoaderGX ? "usbloadergx" : "wiiflow") << '\t'
               << (source.libraryId.empty() ? DefaultLibraryId(source.platform) : source.libraryId) << '\t' << source.launcherId << '\n';
    }
    output.flush();
    const bool ok=static_cast<bool>(output);output.close();
    const bool saved = ok&&!output.fail()&&SavedFile::Commit(temporary,kSourcesFile);
    if (saved) InvalidateLibraryCache();
    return saved;
}

std::vector<std::string> ListDirectories(const std::string &path, const BrowserCancelled &cancelled, std::string *error) {
    std::vector<std::string> directories;
    if(error)error->clear();
    const auto canOpen=[&](const std::string &candidate){
        if(cancelled && cancelled())return false;
        DIR *dir=opendir(candidate.c_str());if(!dir)return false;
        closedir(dir);return true;
    };
    BrowserPath::AddNamespaceEntries(path,directories,canOpen);
    if(cancelled && cancelled())return {};
    DIR *directory = opendir(path.c_str());
    if(!directory && !BrowserPath::Container(path) && error)*error="CANNOT OPEN FOLDER";
    if (directory) {
    while (!cancelled || !cancelled()) {
        errno=0;
        dirent *entry=readdir(directory);
        if(!entry){if(errno && errno!=ENOENT && error)*error="FOLDER READ FAILED";break;}
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const auto child=BrowserPath::Child(path,name);
        if (S_ISDIR(DirectoryEntryMode(*entry,child))||(BrowserPath::Container(path)&&canOpen(child))) directories.push_back(name);
    }
    closedir(directory);
    }
    if(cancelled && cancelled())return {};
    std::sort(directories.begin(), directories.end(), [](const std::string &left, const std::string &right) {
        return Lower(left) < Lower(right);
    });
    directories.erase(std::unique(directories.begin(),directories.end()),directories.end());
    return directories;
}

bool DirectoryExists(const std::string &path) {
    return IsDirectory(path);
}

static bool MatchesExtension(const std::string &name,const std::string &extensions) {
    const auto dot=name.find_last_of('.');if(dot==std::string::npos)return false;
    const auto ext=Lower(name.substr(dot));std::istringstream list(extensions);std::string candidate;
    while(std::getline(list,candidate,',')){
        candidate=Lower(Trim(candidate));if(!candidate.empty()&&candidate.front()!='.')candidate='.'+candidate;
        if(candidate==ext)return true;
    }
    return false;
}
static void ScanFiles(ScanResult &result,const std::string &path,int depth,const std::string &extensions) {
    DIR *dir=opendir(path.c_str()); if(!dir) return;
    std::map<std::string,GameEntry> local;
    while(auto *entry=readdir(dir)) {
        std::string name=entry->d_name; if(name=="."||name=="..") continue;
        const auto full=path+"/"+name;
        if(VCPackage::IsPackageName(full)){
            GameEntry source,target;
            if(VCPackage::Load(full,source,target)&&MatchesExtension(source.absolutePath,extensions))local[source.absolutePath]=std::move(source);
            continue; // Never scan ROMs/BIOS from inside a package, even if damaged.
        }
        if(IsDirectory(full)) { if(depth>0) ScanFiles(result,full,depth-1,extensions); continue; }
        if(MatchesExtension(name,extensions)&&IsFile(full)){
            if(RomArchive::Ext(name)==".zip"&&extensions.find(".nes")!=std::string::npos&&VCSystems::File(full)!="nes")continue;
            GameEntry game;game.platform=Platform::Files;game.name=name.substr(0,name.find_last_of('.'));game.folder=name;game.absolutePath=full;
            local.emplace(full,std::move(game)); // Package metadata wins in either directory order.
        }
    }
    closedir(dir);
    for(auto &[path,game]:local)result.games.push_back(std::move(game));
}

ScanResult ScanLibrary() {
    ScanResult result;
    UpdateMetadata updates;
    EnsureDirectory(std::string(kDevice) + "/wiiu/raw-saves");
    EnsureDirectory(kAppDirectory);
    ScanInstalledGames(result);
    const auto libraries=LoadLibraries();
    const auto legacyInjections=VCPackage::LegacySources();
    for (const auto &source : LoadContentSources()) {
        if (!source.enabled || !IsDirectory(source.path)) continue;
        auto definition=std::find_if(libraries.begin(),libraries.end(),[&](const auto &l){return l.id==source.libraryId;});
        if(definition!=libraries.end()&&!definition->enabled) continue;
        const auto parser=definition==libraries.end()?source.platform:definition->parser;
        const auto firstNew = result.games.size();
        if (parser == Platform::WiiU) ScanWiiURoot(result, updates, source.path, source.depth);
        else if(parser==Platform::Files) {
            const auto extensions=definition==libraries.end()?"":definition->extensions;
            ScanFiles(result,source.path,source.depth,extensions);
            auto prefix=source.path;while(!prefix.empty()&&prefix.back()=='/')prefix.pop_back();prefix+='/';
            for(const auto &saved:legacyInjections){
                if(saved.absolutePath.rfind(prefix,0)!=0||!MatchesExtension(saved.absolutePath,extensions))continue;
                const auto tail=saved.absolutePath.substr(prefix.size());
                if(std::count(tail.begin(),tail.end(),'/')>source.depth)continue;
                if(std::none_of(result.games.begin()+firstNew,result.games.end(),[&](const auto &g){return g.absolutePath==saved.absolutePath;}))result.games.push_back(saved);
            }
        }
        else ScanDiscRoot(result, parser, source.path, source.depth);
        for(size_t i=firstNew;i<result.games.size();++i) {
            result.games[i].launcher=source.launcher;
            result.games[i].libraryId=source.libraryId.empty() ? DefaultLibraryId(source.platform) : source.libraryId;
        }
    }

    std::set<std::string> installedIds;
    for (const auto &game : result.games) if (game.installed && !game.titleId.empty()) installedIds.insert(game.titleId);
    result.games.erase(std::remove_if(result.games.begin(), result.games.end(), [&](const GameEntry &game) {
        return !game.installed && game.platform == Platform::WiiU && !game.titleId.empty() && installedIds.contains(game.titleId);
    }), result.games.end());

    const auto favorites = LoadFavoriteKeys();
    for (auto &game : result.games) {
        if (!game.installed && game.platform == Platform::WiiU) {
            const auto update = updates.find(game.titleId);
            if (update != updates.end()) {
                const auto &metadata = update->second.second;
                if (game.name.empty()) game.name = metadata.name;
                if (game.publisher.empty()) game.publisher = metadata.publisher;
                if (game.coverPath.empty()) game.coverPath = metadata.coverPath;
            }
            if (game.name.empty()) game.name = CleanDiscName(game.folder);
        }
        game.favorite = favorites.contains(FavoriteKey(game));
    }
    EnrichLibraryMedia(result.games);
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

bool RefreshInstalledGames(std::vector<GameEntry> &games) {
    ScanResult installed;
    // A failed system query must not discard the last known installed catalog.
    if (!ScanInstalledGames(installed, &games)) return false;
    std::vector<GameEntry> added;
    for (const auto &game : installed.games) {
        if (std::none_of(games.begin(), games.end(), [&](const auto &old) {
            return old.installed && old.installedTitleId == game.installedTitleId && old.absolutePath == game.absolutePath;
        })) added.push_back(game);
    }
    const auto oldCount = std::count_if(games.begin(), games.end(), [](const auto &g) { return g.installed; });
    if (added.empty() && static_cast<std::size_t>(oldCount) == installed.games.size()) return false;
    // Existing entries keep their resolved artwork: only new titles need media lookup.
    if (!added.empty()) EnrichLibraryMedia(added);
    for (auto &game : installed.games) {
        const auto enriched = std::find_if(added.begin(), added.end(), [&](const auto &g) { return g.absolutePath == game.absolutePath; });
        if (enriched != added.end()) game = std::move(*enriched);
    }
    std::set<std::string> ids;
    for (const auto &game : installed.games) ids.insert(game.titleId);
    games.erase(std::remove_if(games.begin(), games.end(), [&](const auto &game) {
        return game.installed || (game.platform == Platform::WiiU && ids.contains(game.titleId));
    }), games.end());
    const auto favorites = LoadFavoriteKeys();
    for (auto &game : installed.games) {
        game.favorite = favorites.contains(FavoriteKey(game));
        games.push_back(std::move(game));
    }
    std::sort(games.begin(), games.end(), [](const auto &a, const auto &b) {
        const auto left = Lower(a.name), right = Lower(b.name);
        return left == right ? a.absolutePath < b.absolutePath : left < right;
    });
    return true;
}

bool LoadLibraryCache(std::vector<GameEntry> &games) {
    CacheReader reader{SavedFile::Open(kLibraryCacheFile, std::ios::binary)};
    if (!reader.input) return false;
    std::uint32_t magic = 0, version = 0, count = 0;
    if (!ReadValue(reader.input, magic) || !ReadValue(reader.input, version) || !ReadValue(reader.input, count) ||
        magic != kLibraryCacheMagic || version != kLibraryCacheVersion || count > kLibraryCacheMaxEntries) return false;
    std::vector<GameEntry> cached;
    try {
        cached.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            GameEntry game;
            if (!ReadCacheGame(reader, game)) return false;
            cached.push_back(std::move(game));
        }
    } catch (...) {
        return false;
    }
    const auto favorites = LoadFavoriteKeys();
    for (auto &game : cached) game.favorite = favorites.contains(FavoriteKey(game));
    games = std::move(cached);
    return true;
}

bool SaveLibraryCache(const std::vector<GameEntry> &games) {
    if (games.size() > kLibraryCacheMaxEntries || !EnsureDirectory(std::string(kAppDirectory) + "/cache")) return false;
    const std::string temporary = std::string(kLibraryCacheFile) + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    const auto count = static_cast<std::uint32_t>(games.size());
    bool ok = WriteValue(output, kLibraryCacheMagic) && WriteValue(output, kLibraryCacheVersion) && WriteValue(output, count);
    for (const auto &game : games) if (ok) ok = WriteCacheGame(output, game);
    output.flush();
    ok = ok && static_cast<bool>(output);
    output.close();
    if (!ok || output.fail() || !SavedFile::Commit(temporary, kLibraryCacheFile)) {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

void InvalidateLibraryCache() {
    std::remove(kLibraryCacheFile);
    std::remove((std::string(kLibraryCacheFile)+".bak").c_str());
}

void SaveFavorites(const std::vector<GameEntry> &games) {
    EnsureDirectory(kAppDirectory);
    std::ofstream output(kFavoritesFile, std::ios::trunc);
    for (const auto &game : games) {
        if (game.favorite) output << FavoriteKey(game) << '\n';
    }
}
