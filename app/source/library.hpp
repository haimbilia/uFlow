#pragma once
#include "scan_progress.hpp"
#include "title_install.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class Platform {
    WiiU,
    Wii,
    GameCube,
    Files,
};
enum class GameLauncher { WiiFlow, USBLoaderGX };

// A library is a user-owned category.  Sources reference its stable id, so it
// can be renamed/reordered without rescanning or losing media associations.
struct LibraryDefinition {
    bool operator==(const LibraryDefinition &) const = default;
    std::string id;
    std::string name;
    Platform parser = Platform::WiiU;
    GameLauncher launcher = GameLauncher::WiiFlow;
    std::string extensions;
    bool enabled = true;
    int order = 0;
    std::string launcherId; // empty preserves existing per-directory adapter
    int caseWidthMm = 135;
    int caseHeightMm = 190;
    int caseThicknessMm = 14;
    bool mediaByContentType = false;
};

struct GameMedia {
    std::string cover,full,back,spine,sleeve,background,logo,video;
    float backEnd=0,frontStart=0;
    std::string physical;
};

struct GameEntry {
    Platform platform = Platform::WiiU;
    // Stable user-library identifier. Platform remains for the existing scanners and launch adapters.
    std::string libraryId = "wiiu";
    std::string name;
    std::string publisher;
    std::string titleId;
    std::string folder;
    std::string absolutePath;
    std::string coverPath;
    std::string fullCoverPath; // back | spine | front sleeve, distinct from flat/icon art
    std::string backgroundPath;
    std::string logoPath;
    std::string previewPath;
    std::string description;
    std::string developer;
    std::string genre;
    std::string region;
    std::string rating;
    std::string releaseYear;
    std::string players;
    std::string rpxRelative;
    std::string contentRelative;
    std::string codeRelative;
    std::string saveRelative;
    std::string storage;
    std::uint64_t installedTitleId = 0;
    bool installed = false;
    bool favorite = false;
    GameLauncher launcher = GameLauncher::WiiFlow;
    // Transient ownership token, never serialized into the library cache.
    // Only fresh VC preparation may move its private output during publication.
    bool privateInjectionStage = false;
    std::vector<TitleInstall::Package> installPackages;
    std::string backCoverPath;
    std::string spinePath;
    std::string defaultSleevePath; // Supplies only missing faces, never hides a real front/back/spine.
    float defaultBackEnd=0, defaultFrontStart=0; // Optional authored cuts, independent of box proportions.
    std::string vcSystem; // Original console of a Wii U VC/injected title; "wiiu" means native.
    std::string navigationLibraryId; // Transient category; never changes media or launch routing.
    std::string mediaLibraryId; // Transient applied appearance; launch/source identity stays intact.
    std::shared_ptr<const GameMedia> sourceMedia;
    std::string physicalMediaPath; // Transparent disc/cartridge art, separate from its case.
};

inline GameMedia CaptureGameMedia(const GameEntry &g){return {g.coverPath,g.fullCoverPath,g.backCoverPath,g.spinePath,g.defaultSleevePath,g.backgroundPath,g.logoPath,g.previewPath,g.defaultBackEnd,g.defaultFrontStart,g.physicalMediaPath};}
inline void SetGameMedia(GameEntry &g,const GameMedia &m){g.coverPath=m.cover;g.fullCoverPath=m.full;g.backCoverPath=m.back;g.spinePath=m.spine;g.defaultSleevePath=m.sleeve;g.backgroundPath=m.background;g.logoPath=m.logo;g.previewPath=m.video;g.defaultBackEnd=m.backEnd;g.defaultFrontStart=m.frontStart;g.physicalMediaPath=m.physical;}
inline void RestoreSourceMedia(GameEntry &g){if(g.sourceMedia)SetGameMedia(g,*g.sourceMedia);g.sourceMedia.reset();g.mediaLibraryId.clear();}

struct ScanResult {
    std::vector<GameEntry> games;
    std::size_t invalidWiiU = 0;
};

struct ContentSource {
    bool operator==(const ContentSource &) const = default;
    Platform platform = Platform::WiiU;
    std::string path;
    bool enabled = true;
    int depth = 3;
    GameLauncher launcher = GameLauncher::WiiFlow;
    // Appended to retain compatibility with existing source fixtures/configuration code.
    // A source belongs to a user-facing library, not merely a hard-coded platform tab.
    std::string libraryId;
    std::string launcherId; // empty inherits the content-type default
    bool autoSortVC = false;
};

// Append values: existing persisted types/fixtures retain their meaning.
enum class MediaType { Cover, FullCover, Background, Logo, Preview, Metadata, BackCover, Spine, PhysicalMedia, Count };
inline constexpr int kMediaTypeCount=static_cast<int>(MediaType::Count);
inline bool IsCoverMedia(MediaType type) {
    return type==MediaType::Cover || type==MediaType::FullCover || type==MediaType::BackCover || type==MediaType::Spine;
}
// Persisted values 0/1 retain the original Background/Foreground choices.
enum class VideoMode { Background = 0, Foreground = 1, Showcase = 2 };
inline const char *VideoModeName(VideoMode mode) {
    return mode==VideoMode::Background?"BACKGROUND":mode==VideoMode::Foreground?"FOREGROUND":"SHOWCASE";
}
struct VideoOptions {
    bool operator==(const VideoOptions &) const = default;
    int delayMs = 1000;
    VideoMode mode = VideoMode::Showcase;
    int volume = 70;
    int foregroundOpacity = 100; // UI over background video only, percent.
};
struct MediaSource {
    bool operator==(const MediaSource &) const = default;
    MediaType type = MediaType::Cover;
    int library = -1; // -1 all libraries; otherwise Platform
    std::string path;
    bool enabled = true;
    int depth = 0;
    std::string libraryId;
    VideoOptions video;
    std::string defaultFile; // Absolute/relative fallback; empty FullCover auto-detects Default.png.
};
bool MediaFileMatches(MediaType type, const std::string &path);
// Browser entries: directories end in '/', files are filtered by media type.
using BrowserCancelled = std::function<bool()>;
std::vector<std::string> ListMediaFiles(const std::string &path, MediaType type,
    const BrowserCancelled &cancelled = {}, std::string *error = nullptr);
std::string FindDefaultMediaFile(const MediaSource &source);
const char *MediaTypeName(MediaType type);
const char *MediaLibraryName(int library);
std::vector<MediaSource> LoadMediaSources();
bool SaveMediaSources(const std::vector<MediaSource> &sources);
bool MediaRefreshNeeded();
bool RequestMediaRefresh();
void CompleteMediaRefresh();
void RefreshLibraryMedia(std::vector<GameEntry> &games,const ScanObserver &progress = {});
void EnrichLibraryMedia(std::vector<GameEntry> &games,const ScanObserver &progress = {},const std::vector<MediaSource> *scopedSources=nullptr);
void ApplyContentTypeMedia(std::vector<GameEntry> &games,const std::vector<LibraryDefinition> &libraries,const ScanObserver &progress={});
// Resolve options against the indexed path; no directory access during playback.
const MediaSource *VideoSourceFor(const std::vector<MediaSource> &sources, const GameEntry &game);

const char *PlatformName(Platform platform);
const char *DefaultLibraryId(Platform platform);
ScanResult ScanLibrary(const ScanObserver &progress = {});
// Refresh only system-installed games, preserving cached SD entries/media.
// Returns true when the catalog changed. Failed system queries preserve it.
bool RefreshInstalledGames(std::vector<GameEntry> &games, bool keepMissing = false);
// Add discoveries without replacing existing entries or removing offline games.
std::size_t AddDiscoveredGames(std::vector<GameEntry> &games, std::vector<GameEntry> discovered);
// Merge base games from freshly MCP-verified package status; no directory scan.
bool MergeInstalledPackages(std::vector<GameEntry> &games);
// The catalog stores scan results (including resolved media paths) so regular
// starts do not walk every configured directory before the UI is usable.
bool LoadLibraryCache(std::vector<GameEntry> &games);
bool SaveLibraryCache(const std::vector<GameEntry> &games);
void InvalidateLibraryCache();
std::vector<ContentSource> LoadContentSources();
bool SaveContentSources(const std::vector<ContentSource> &sources,bool invalidateCache=true);
std::vector<LibraryDefinition> LoadLibraries();
bool SaveLibraries(const std::vector<LibraryDefinition> &libraries,bool invalidateCache=true);
std::vector<std::string> ListDirectories(const std::string &path,
    const BrowserCancelled &cancelled = {}, std::string *error = nullptr);
bool DirectoryExists(const std::string &path);
void SaveFavorites(const std::vector<GameEntry> &games);
