#pragma once

#include <cstdint>
#include <string>

enum class ControlAction {
    Launch, Details, Favorite, Settings, PreviousLibrary, NextLibrary, PageBack, PageForward,
    ZoomFront, ZoomBack, Count
};

struct UserSettings {
    bool wrapNavigation = true;
    bool showInstalled = true;
    bool showRawWiiU = true;
    bool showWii = true;
    bool showGameCube = true;
    bool backgroundMotion = true;
    bool useBoxArt = true;
    bool useBackgrounds = true;
    bool useLogos = true;
    bool usePreviews = true;
    bool reflections = true;
    int animationSpeed = 1;
    int coverSpacing = 1;
    int coverLayout = 0;
    int skin = 1;
    bool welcomeDismissed = false;
    int theme = 0;
    int startTab = 0;
    bool musicEnabled = false;
    int musicVolume = 35;
    std::string musicTrack;
    bool startOnBoot = false;
    std::uint32_t bindings[static_cast<int>(ControlAction::Count)] = {
        0x8000, 0x1000, 0x2000, 0x0008, 0x0020, 0x0010, 0x0080, 0x0040, 0x0200, 0x0100
    };
};

UserSettings LoadSettings();
void SaveSettings(const UserSettings &settings);
const char *OnOff(bool value);
bool SetStartOnBoot(bool enabled);
const char *ControlActionName(ControlAction action);
const char *ControlBindingName(std::uint32_t button);
void ResetControlBindings(UserSettings &settings);
