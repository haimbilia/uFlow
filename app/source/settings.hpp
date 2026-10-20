#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include "accent_themes.hpp"
#include "cover_presentation.hpp"
#include "demo_mode.hpp"

enum class ControlAction {
    Launch, Details, Favorite, Settings, PreviousLibrary, NextLibrary, PageBack, PageForward,
    ZoomFront, ZoomBack, LaunchOptions, Sort, Alphabet, NavigateLeft, NavigateRight, NavigateUp, NavigateDown, FocusSystemButtons, NextLayout, Count
};

namespace StickButton {
inline constexpr uint32_t Left=1u<<20, Right=1u<<21, Up=1u<<22, Down=1u<<23;
inline constexpr uint32_t RLeft=1u<<24,RRight=1u<<25,RUp=1u<<26,RDown=1u<<27;
inline uint32_t RightDirections(float x,float y){return (x<-.42f?RLeft:x>.42f?RRight:0)|(y>.48f?RUp:y<-.48f?RDown:0);}
inline uint32_t Directions(float x,float y){return (x<-.42f?Left:x>.42f?Right:0)|(y>.48f?Up:y<-.48f?Down:0);}
}

namespace CoverSpacing {
inline constexpr int Min=50, Normal=100, Max=200, Step=5;
}

namespace CoverZoom {
inline constexpr int Min=40, Normal=120, Max=200, Step=5;
}

struct UserSettings {
    bool wrapNavigation = true;
    bool showInstalled = true;
    bool autoSortInstalledVC = false;
    bool titleInstallerEnabled = true;
    bool showRawWiiU = true;
    bool showWii = true;
    bool showGameCube = true;
    bool backgroundMotion = true;
    bool preloadBuiltInCovers = true;
    bool startupVideo = true;
    bool demoMode = false;
    int demoIdleSeconds = DemoMode::DefaultSeconds;
    bool useBoxArt = true;
    bool useBackgrounds = true;
    int backgroundTransparency = 50;
    bool useLogos = true;
    bool usePreviews = true;
    int previewDelaySeconds = 0;
    bool reflections = true;
    int animationSpeed = 1;
    int coverSpacing = CoverSpacing::Normal;
    int coverZoom = CoverZoom::Normal;
    int inspectionZoom = 4; // half steps: 2=1.0x, 8=4.0x
    int coverLayout = 0;
    int coverType = CoverPresentation::Box;
    int logoLayout = 0;
    int mediaLayout = 0;
    int thumbnailLayout = 0;
    int sortMode = 0;
    bool alphabetFilterMode = false;
    int skin = 1;
    std::string skinId;
    bool welcomeDismissed = false;
    int theme = 0;
    int startTab = 0;
    std::string startLibrary; // Stable library ID; empty accepts the legacy tab index.
    bool musicEnabled = false;
    int musicVolume = 35;
    bool menuSoundsEnabled = true;
    int menuSoundsVolume = 35;
    std::string musicTrack;
    bool startOnBoot = false;
    std::array<std::vector<uint32_t>,static_cast<int>(ControlAction::Count)> combos;
    std::uint32_t bindings[static_cast<int>(ControlAction::Count)] = {
        0x8000, 0x1000, 0x2000, 0x0008, 0x0020, 0x0010, 0x0080, 0x0040, StickButton::RUp, StickButton::RDown, 0x0004, 0x4000, 0x20000, 0x0800|StickButton::Left, 0x0400|StickButton::Right, 0x0200|StickButton::Up, 0x0100|StickButton::Down, 0x40000, 0
    };
};

UserSettings LoadSettings();
void SaveSettings(const UserSettings &settings);
const char *OnOff(bool value);
bool SetStartOnBoot(bool enabled);
const char *ControlActionName(ControlAction action);
const char *ControlBindingName(std::uint32_t button);
void ResetControlBindings(UserSettings &settings);

bool ControlHeld(const UserSettings &settings,ControlAction action,uint32_t held);
bool ControlPressed(const UserSettings &settings,ControlAction action,uint32_t held,uint32_t pressed);
std::string ControlBindingLabel(const UserSettings &settings,ControlAction action);
void ClearControlBindings(UserSettings &settings,int action);
void AddControlBinding(UserSettings &settings,int action,uint32_t mask);

// Prefix buttons wait until release, so a chord cannot fire their single actions first.
struct ControlChordInput {
    bool initialized=false;
    uint32_t liveHold=0,liveTrigger=0,releasedSingles=0,pendingSingles=0;
    void Reset(){*this={};}
    void Update(const UserSettings &settings,uint32_t held,uint32_t pressed);
};
