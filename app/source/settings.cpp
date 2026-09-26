#include "settings.hpp"
#include "saved_file.hpp"
#include "startup_config.hpp"
#include <cerrno>

#include <sys/stat.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

constexpr const char *kDirectory = "fs:/vol/external01/wiiu/apps/uFlow";
constexpr const char *kPath = "fs:/vol/external01/wiiu/apps/uFlow/uflow.cfg";

bool IsDirectory(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

void SetValue(UserSettings &settings, const std::string &key, int value) {
    const bool enabled = value != 0;
    if (key == "wrap_navigation") settings.wrapNavigation = enabled;
    else if (key == "show_installed") settings.showInstalled = enabled;
    else if (key == "show_raw_wiiu") settings.showRawWiiU = enabled;
    else if (key == "show_wii") settings.showWii = enabled;
    else if (key == "show_gamecube") settings.showGameCube = enabled;
    else if (key == "background_motion") settings.backgroundMotion = enabled;
    else if (key == "preload_builtin_covers") settings.preloadBuiltInCovers = enabled;
    else if (key == "use_box_art") settings.useBoxArt = enabled;
    else if (key == "use_backgrounds") settings.useBackgrounds = enabled;
    else if (key == "use_logos") settings.useLogos = enabled;
    else if (key == "use_previews") settings.usePreviews = enabled;
    else if (key == "reflections") settings.reflections = enabled;
    else if (key == "animation_speed") settings.animationSpeed = std::clamp(value, 0, 2);
    else if (key == "cover_spacing") settings.coverSpacing = std::clamp(value, 0, 2);
    else if (key == "cover_layout") settings.coverLayout = std::clamp(value, 0, 3);
    else if (key == "skin") settings.skin = std::clamp(value, 0, 1);
    else if (key == "welcome_dismissed") settings.welcomeDismissed = enabled;
    else if (key == "theme") settings.theme = std::clamp(value, 0, 3);
    else if (key == "start_tab") settings.startTab = std::clamp(value, 0, 4);
    else if (key == "music_enabled") settings.musicEnabled = enabled;
    else if (key == "music_volume") settings.musicVolume = std::clamp(value, 0, 100);
    else if (key.rfind("bind_", 0) == 0) {
        const int action = std::atoi(key.c_str() + 5);
        if (action >= 0 && action < static_cast<int>(ControlAction::Count)) settings.bindings[action] = static_cast<std::uint32_t>(value);
    }
}

} // namespace

UserSettings LoadSettings() {
    UserSettings settings;
    settings.startOnBoot=UFlowStartup::Enabled();
    std::ifstream input(kPath);
    std::string line;
    int controlsVersion=0;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        if(line.substr(0,separator)=="controls_version") {
            try { controlsVersion=std::stoi(line.substr(separator+1)); } catch (...) {}
            continue;
        }
        if(line.substr(0,separator)=="music_track") {
            settings.musicTrack=line.substr(separator+1);
            if(!settings.musicTrack.empty() && settings.musicTrack.back()=='\r') settings.musicTrack.pop_back();
            if(settings.musicTrack.find_first_of("/\\\r\n")!=std::string::npos) settings.musicTrack.clear();
            continue;
        }
        try {
            SetValue(settings, line.substr(0, separator), std::stoi(line.substr(separator + 1)));
        } catch (...) {
        }
    }
    // Version 1 labelled the Minus/HOME bits as ZL/ZR. Migrate only those exact
    // legacy defaults; all user-selected face buttons and library bindings survive.
    if(controlsVersion<2) {
        if(settings.bindings[static_cast<int>(ControlAction::PageBack)]==0x0004)
            settings.bindings[static_cast<int>(ControlAction::PageBack)]=0x0080;
        if(settings.bindings[static_cast<int>(ControlAction::PageForward)]==0x0002)
            settings.bindings[static_cast<int>(ControlAction::PageForward)]=0x0040;
    }
    return settings;
}

void SaveSettings(const UserSettings &settings) {
    if (!IsDirectory(kDirectory)) mkdir(kDirectory, 0777);
    std::ofstream output(kPath, std::ios::trunc);
    output << "wrap_navigation=" << settings.wrapNavigation << '\n';
    output << "show_installed=" << settings.showInstalled << '\n';
    output << "show_raw_wiiu=" << settings.showRawWiiU << '\n';
    output << "show_wii=" << settings.showWii << '\n';
    output << "show_gamecube=" << settings.showGameCube << '\n';
    output << "background_motion=" << settings.backgroundMotion << '\n';
    output << "preload_builtin_covers=" << settings.preloadBuiltInCovers << '\n';
    output << "use_box_art=" << settings.useBoxArt << '\n';
    output << "use_backgrounds=" << settings.useBackgrounds << '\n';
    output << "use_logos=" << settings.useLogos << '\n';
    output << "use_previews=" << settings.usePreviews << '\n';
    output << "reflections=" << settings.reflections << '\n';
    output << "animation_speed=" << settings.animationSpeed << '\n';
    output << "cover_spacing=" << settings.coverSpacing << '\n';
    output << "cover_layout=" << settings.coverLayout << '\n';
    output << "skin=" << settings.skin << '\n';
    output << "welcome_dismissed=" << settings.welcomeDismissed << '\n';
    output << "theme=" << settings.theme << '\n';
    output << "start_tab=" << settings.startTab << '\n';
    output << "music_enabled=" << settings.musicEnabled << '\n';
    output << "music_volume=" << settings.musicVolume << '\n';
    if(settings.musicTrack.find_first_of("/\\\r\n")==std::string::npos)
        output << "music_track=" << settings.musicTrack << '\n';
    output << "controls_version=2\n";
    for (int action=0; action<static_cast<int>(ControlAction::Count); ++action)
        output << "bind_" << action << '=' << settings.bindings[action] << '\n';
}

const char *OnOff(bool value) {
    return value ? "ON" : "OFF";
}

bool SetStartOnBoot(bool enabled) {
    if(!enabled) return std::remove(UFlowStartup::Flag)==0 || errno==ENOENT;
    if(!IsDirectory(kDirectory)) mkdir(kDirectory,0777);
    const std::string temporary=std::string(UFlowStartup::Flag)+".tmp";
    FILE *file=std::fopen(temporary.c_str(),"wb");
    if(!file) return false;
    bool ok=std::fwrite("enabled=1\n",1,10,file)==10;
    if(std::fclose(file)!=0) ok=false;
    if(ok) ok=SavedFile::Commit(temporary,UFlowStartup::Flag);
    if(!ok) std::remove(temporary.c_str());
    return ok;
}

const char *ControlActionName(ControlAction action) {
    static constexpr const char *names[] = {"LAUNCH GAME", "TITLE DETAILS", "FAVORITE", "OPEN SETTINGS",
        "PREVIOUS LIBRARY", "NEXT LIBRARY", "PAGE BACK", "PAGE FORWARD", "ZOOM FRONT COVER", "ZOOM BACK COVER",
        "LAUNCH OPTIONS"};
    const int index = static_cast<int>(action);
    return index >= 0 && index < static_cast<int>(ControlAction::Count) ? names[index] : "UNKNOWN";
}

const char *ControlBindingName(std::uint32_t button) {
    switch (button) {
        case 0x8000: return "A"; case 0x4000: return "B"; case 0x2000: return "X"; case 0x1000: return "Y";
        case 0x0008: return "+"; case 0x0020: return "L"; case 0x0010: return "R";
        case 0x0080: return "ZL"; case 0x0040: return "ZR";
        case 0x0800: return "DPAD LEFT"; case 0x0400: return "DPAD RIGHT";
        case 0x0200: return "DPAD UP"; case 0x0100: return "DPAD DOWN";
        case 0x0004: return "-"; case 0x0002: return "HOME"; case 0x0001: return "SYNC";
    }
    return "UNASSIGNED";
}

void ResetControlBindings(UserSettings &settings) {
    const std::uint32_t defaults[] = {0x8000,0x1000,0x2000,0x0008,0x0020,0x0010,0x0080,0x0040,0x0200,0x0100,0x0004};
    for (int index=0;index<static_cast<int>(ControlAction::Count);++index) settings.bindings[index]=defaults[index];
}
