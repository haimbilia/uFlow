#pragma once

#include <string>

struct UserSettings {
    bool wrapNavigation = true;
    bool showInstalled = true;
    bool showRawWiiU = true;
    bool showWii = true;
    bool showGameCube = true;
    bool backgroundMotion = true;
    int animationSpeed = 1;
    int coverSpacing = 1;
    int theme = 0;
    int startTab = 0;
};

UserSettings LoadSettings();
void SaveSettings(const UserSettings &settings);
const char *OnOff(bool value);

