#include "settings.hpp"

#include <sys/stat.h>

#include <algorithm>
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
    else if (key == "use_box_art") settings.useBoxArt = enabled;
    else if (key == "use_backgrounds") settings.useBackgrounds = enabled;
    else if (key == "use_logos") settings.useLogos = enabled;
    else if (key == "use_previews") settings.usePreviews = enabled;
    else if (key == "animation_speed") settings.animationSpeed = std::clamp(value, 0, 2);
    else if (key == "cover_spacing") settings.coverSpacing = std::clamp(value, 0, 2);
    else if (key == "cover_layout") settings.coverLayout = std::clamp(value, 0, 3);
    else if (key == "skin") settings.skin = std::clamp(value, 0, 1);
    else if (key == "theme") settings.theme = std::clamp(value, 0, 3);
    else if (key == "start_tab") settings.startTab = std::clamp(value, 0, 4);
}

} // namespace

UserSettings LoadSettings() {
    UserSettings settings;
    std::ifstream input(kPath);
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        try {
            SetValue(settings, line.substr(0, separator), std::stoi(line.substr(separator + 1)));
        } catch (...) {
        }
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
    output << "use_box_art=" << settings.useBoxArt << '\n';
    output << "use_backgrounds=" << settings.useBackgrounds << '\n';
    output << "use_logos=" << settings.useLogos << '\n';
    output << "use_previews=" << settings.usePreviews << '\n';
    output << "animation_speed=" << settings.animationSpeed << '\n';
    output << "cover_spacing=" << settings.coverSpacing << '\n';
    output << "cover_layout=" << settings.coverLayout << '\n';
    output << "skin=" << settings.skin << '\n';
    output << "theme=" << settings.theme << '\n';
    output << "start_tab=" << settings.startTab << '\n';
}

const char *OnOff(bool value) {
    return value ? "ON" : "OFF";
}
