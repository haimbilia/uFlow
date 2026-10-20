#include "settings.hpp"
#include "saved_file.hpp"
#include "startup_config.hpp"
#include <cerrno>
#include <sstream>
#include <bit>

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
    else if (key == "title_installer_enabled") settings.titleInstallerEnabled = enabled;
    else if (key == "show_installed") settings.showInstalled = enabled;
    else if (key == "auto_sort_installed_vc") settings.autoSortInstalledVC = enabled;
    else if (key == "show_raw_wiiu") settings.showRawWiiU = enabled;
    else if (key == "show_wii") settings.showWii = enabled;
    else if (key == "show_gamecube") settings.showGameCube = enabled;
    else if (key == "background_motion") settings.backgroundMotion = enabled;
    else if (key == "startup_video") settings.startupVideo = enabled;
    else if (key == "demo_mode") settings.demoMode = enabled;
    else if (key == "demo_idle_seconds") settings.demoIdleSeconds = std::clamp(value,DemoMode::MinSeconds,DemoMode::MaxSeconds);
    else if (key == "preload_builtin_covers") settings.preloadBuiltInCovers = enabled;
    else if (key == "use_box_art") settings.useBoxArt = enabled;
    else if (key == "use_backgrounds") settings.useBackgrounds = enabled;
    else if (key == "background_transparency") settings.backgroundTransparency = std::clamp(value, 0, 100);
    else if (key == "use_logos") settings.useLogos = enabled;
    else if (key == "preview_delay_seconds") settings.previewDelaySeconds=std::clamp(value,0,30);
    else if (key == "use_previews") settings.usePreviews = enabled;
    else if (key == "reflections") settings.reflections = enabled;
    else if (key == "animation_speed") settings.animationSpeed = std::clamp(value, 0, 2);
    else if (key == "cover_spacing") settings.coverSpacing = 75 + std::clamp(value, 0, 2)*25;
    else if (key == "cover_spacing_percent") settings.coverSpacing = std::clamp(value, CoverSpacing::Min, CoverSpacing::Max);
    else if (key == "inspection_zoom") settings.inspectionZoom=std::clamp(value,2,8);
    else if (key == "cover_zoom") settings.coverZoom = std::clamp(value, CoverZoom::Min, CoverZoom::Max);
    else if (key == "cover_layout") settings.coverLayout = std::clamp(value, 0, CoverPresentation::LayoutCount(CoverPresentation::Box)-1);
    else if (key == "cover_type") settings.coverType = std::clamp(value, 0, CoverPresentation::TypeCount-1);
    else if (key == "logo_layout") settings.logoLayout = std::clamp(value, 0, CoverPresentation::LayoutCount(CoverPresentation::Logo)-1);
    else if (key == "thumbnail_layout") settings.thumbnailLayout = std::clamp(value, 0, CoverPresentation::LayoutCount(CoverPresentation::Thumbnails)-1);
    else if (key == "media_layout") settings.mediaLayout = std::clamp(value, 0, CoverPresentation::LayoutCount(CoverPresentation::Media)-1);
    else if (key == "alphabet_filter_mode") settings.alphabetFilterMode = enabled;
    else if (key == "sort_mode") settings.sortMode = std::clamp(value, 0, 6);
    else if (key == "skin") settings.skin = std::clamp(value, 0, 3);
    else if (key == "welcome_dismissed") settings.welcomeDismissed = enabled;
    else if (key == "theme") settings.theme = std::clamp(value, 0, AccentThemes::Count-1);
    else if (key == "start_tab") settings.startTab = std::max(value, 0);
    else if (key == "music_enabled") settings.musicEnabled = enabled;
    else if (key == "music_volume") settings.musicVolume = std::clamp(value, 0, 100);
    else if (key == "menu_sounds_enabled") settings.menuSoundsEnabled = enabled;
    else if (key == "menu_sounds_volume") settings.menuSoundsVolume = std::clamp(value, 0, 100);
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
        if(line.rfind("combo_",0)==0) {
            try {
                size_t used=0;const auto key=line.substr(6,separator-6);const int action=std::stoi(key,&used);
                if(used!=key.size()||action<0||action>=int(ControlAction::Count))continue;
                std::istringstream values(line.substr(separator+1));std::string token;
                while(std::getline(values,token,',')&&settings.combos[action].size()<8){size_t n=0;auto value=std::stoull(token,&n);
                    if(n==token.size()&&value<=0xffffffffu&&!(value&~0x0ff6fffcu)&&std::popcount(uint32_t(value))>1)AddControlBinding(settings,action,uint32_t(value));}
            }catch(...){}
            continue;
        }
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
        if(line.substr(0,separator)=="skin_id") {
            auto id=line.substr(separator+1);if(!id.empty()&&id.back()=='\r')id.pop_back();
            if(id.size()<128&&id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos)settings.skinId=id;
            continue;
        }
        if(line.substr(0,separator)=="start_library") {
            auto id=line.substr(separator+1);
            if(!id.empty() && id.back()=='\r')id.pop_back();
            if(id.size()<=128 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")==std::string::npos)
                settings.startLibrary=id;
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
    if(controlsVersion<3) {
        if(settings.bindings[8]==0x0200)settings.bindings[8]=StickButton::Up;
        if(settings.bindings[9]==0x0100)settings.bindings[9]=StickButton::Down;
    }
    if(controlsVersion<5){
        if(settings.bindings[8]==StickButton::Up && settings.combos[8].empty())settings.bindings[8]=StickButton::RUp;
        if(settings.bindings[9]==StickButton::Down && settings.combos[9].empty())settings.bindings[9]=StickButton::RDown;
        if(settings.bindings[15]==0x0200 && settings.combos[15].empty())settings.bindings[15]|=StickButton::Up;
        if(settings.bindings[16]==0x0100 && settings.combos[16].empty())settings.bindings[16]|=StickButton::Down;
    }
    return settings;
}

void SaveSettings(const UserSettings &settings) {
    if (!IsDirectory(kDirectory)) mkdir(kDirectory, 0777);
    std::ofstream output(kPath, std::ios::trunc);
    output << "wrap_navigation=" << settings.wrapNavigation << '\n';
    output << "title_installer_enabled=" << settings.titleInstallerEnabled << '\n';
    output << "show_installed=" << settings.showInstalled << '\n';
    output << "auto_sort_installed_vc=" << settings.autoSortInstalledVC << '\n';
    output << "show_raw_wiiu=" << settings.showRawWiiU << '\n';
    output << "show_wii=" << settings.showWii << '\n';
    output << "show_gamecube=" << settings.showGameCube << '\n';
    output << "background_motion=" << settings.backgroundMotion << '\n';
    output << "startup_video=" << settings.startupVideo << '\n';
    output << "demo_mode=" << settings.demoMode << '\n';
    output << "demo_idle_seconds=" << std::clamp(settings.demoIdleSeconds,DemoMode::MinSeconds,DemoMode::MaxSeconds) << '\n';
    output << "preload_builtin_covers=" << settings.preloadBuiltInCovers << '\n';
    output << "use_box_art=" << settings.useBoxArt << '\n';
    output << "use_backgrounds=" << settings.useBackgrounds << '\n';
    output << "background_transparency=" << std::clamp(settings.backgroundTransparency, 0, 100) << '\n';
    output << "use_logos=" << settings.useLogos << '\n';
    output << "use_previews=" << settings.usePreviews << '\n';
    output << "preview_delay_seconds=" << settings.previewDelaySeconds << '\n';
    output << "reflections=" << settings.reflections << '\n';
    output << "animation_speed=" << settings.animationSpeed << '\n';
    output << "cover_spacing_percent=" << settings.coverSpacing << '\n';
    output << "inspection_zoom=" << std::clamp(settings.inspectionZoom,2,8) << '\n';
    output << "cover_zoom=" << std::clamp(settings.coverZoom, CoverZoom::Min, CoverZoom::Max) << '\n';
    output << "cover_layout=" << settings.coverLayout << '\n';
    output << "alphabet_filter_mode=" << settings.alphabetFilterMode << '\n';
    output << "sort_mode=" << settings.sortMode << '\n';
    output << "skin_id=" << settings.skinId << '\n';
    output << "skin=" << settings.skin << '\n';
    output << "welcome_dismissed=" << settings.welcomeDismissed << '\n';
    output << "theme=" << std::clamp(settings.theme,0,AccentThemes::Count-1) << '\n';
    output << "start_tab=" << settings.startTab << '\n';
    output << "start_library=" << settings.startLibrary << '\n';
    output << "cover_type=" << settings.coverType << '\n';
    output << "logo_layout=" << settings.logoLayout << '\n';
    output << "thumbnail_layout=" << settings.thumbnailLayout << '\n';
    output << "media_layout=" << settings.mediaLayout << '\n';
    output << "music_enabled=" << settings.musicEnabled << '\n';
    output << "music_volume=" << settings.musicVolume << '\n';
    output << "menu_sounds_enabled=" << settings.menuSoundsEnabled << '\n';
    output << "menu_sounds_volume=" << settings.menuSoundsVolume << '\n';
    if(settings.musicTrack.find_first_of("/\\\r\n")==std::string::npos)
        output << "music_track=" << settings.musicTrack << '\n';
    output << "controls_version=5\n";
    for (int action=0; action<static_cast<int>(ControlAction::Count); ++action) {
        output << "bind_" << action << '=' << settings.bindings[action] << '\n';
        output << "combo_" << action << '=';
        for(size_t i=0;i<settings.combos[action].size();++i){if(i)output<<',';output<<settings.combos[action][i];}
        output << '\n';
    }
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
        "LAUNCH OPTIONS", "SORT LIBRARY", "A-Z SELECTOR", "NAVIGATE LEFT", "NAVIGATE RIGHT", "NAVIGATE UP", "NAVIGATE DOWN", "SYSTEM BUTTONS", "NEXT LAYOUT"};
    const int index = static_cast<int>(action);
    return index >= 0 && index < static_cast<int>(ControlAction::Count) ? names[index] : "UNKNOWN";
}

const char *ControlBindingName(std::uint32_t button) {
    if(button && (button&(button-1))) {
        static std::string labels[8];static unsigned slot=0;
        auto &label=labels[(slot++)%8];label.clear();
        for(unsigned i=0;i<31;++i)if(button&(1u<<i)){if(!label.empty())label+=" / ";label+=ControlBindingName(1u<<i);}
        return label.c_str();
    }
    switch (button) {
        case StickButton::RLeft:return "R STICK LEFT";case StickButton::RRight:return "R STICK RIGHT";
        case StickButton::RUp:return "R STICK UP";case StickButton::RDown:return "R STICK DOWN";
        case StickButton::Left:return "L STICK LEFT";case StickButton::Right:return "L STICK RIGHT";
        case StickButton::Up:return "L STICK UP";case StickButton::Down:return "L STICK DOWN";
        case 0x8000: return "A"; case 0x4000: return "B"; case 0x2000: return "X"; case 0x1000: return "Y";
        case 0x0008: return "+"; case 0x0020: return "L"; case 0x0010: return "R";
        case 0x20000: return "R3"; case 0x40000: return "L3";
        case 0x0080: return "ZL"; case 0x0040: return "ZR";
        case 0x0800: return "DPAD LEFT"; case 0x0400: return "DPAD RIGHT";
        case 0x0200: return "DPAD UP"; case 0x0100: return "DPAD DOWN";
        case 0x0004: return "-"; case 0x0002: return "HOME"; case 0x0001: return "SYNC";
    }
    return "UNASSIGNED";
}

void ResetControlBindings(UserSettings &settings) {
    const UserSettings defaults;
    for(int i=0;i<static_cast<int>(ControlAction::Count);++i){settings.bindings[i]=defaults.bindings[i];settings.combos[i].clear();}
}

void ClearControlBindings(UserSettings &settings,int action){settings.bindings[action]=0;settings.combos[action].clear();}
void AddControlBinding(UserSettings &settings,int action,uint32_t mask){
    if(!mask)return;
    if(std::popcount(mask)==1){settings.bindings[action]|=mask;return;}
    auto &list=settings.combos[action];
    if(list.size()<8&&std::find(list.begin(),list.end(),mask)==list.end())list.push_back(mask);
}
namespace {
bool ShadowedChord(const UserSettings &s,uint32_t candidate,uint32_t held) {
    for(const auto &list:s.combos)for(auto chord:list)
        if(chord!=candidate&&(chord&candidate)==candidate&&(held&chord)==chord)return true;
    return false;
}
}
bool ControlPressed(const UserSettings &s,ControlAction action,uint32_t held,uint32_t pressed){
    const auto index=int(action);held|=pressed;
    for(auto chord:s.combos[index])if((held&chord)==chord&&(pressed&chord)&&!ShadowedChord(s,chord,held))return true;
    auto bits=s.bindings[index]&pressed;
    while(bits){auto bit=bits&(~bits+1);bits&=~bit;if(!ShadowedChord(s,bit,held))return true;}
    return false;
}
bool ControlHeld(const UserSettings &s,ControlAction action,uint32_t held){return ControlPressed(s,action,held,held);}
std::string ControlBindingLabel(const UserSettings &s,ControlAction action){
    const auto index=int(action);std::string result=s.bindings[index]?ControlBindingName(s.bindings[index]):"";
    for(auto chord:s.combos[index]){if(!result.empty())result+=" / ";bool first=true;
        for(unsigned i=0;i<24;++i)if(chord&(1u<<i)){if(!first)result+=" + ";result+=ControlBindingName(1u<<i);first=false;}}
    return result.empty()?"NONE":result;
}

void ControlChordInput::Update(const UserSettings &s,uint32_t held,uint32_t pressed){
    initialized=true;uint32_t participants=0,matched=0;
    for(const auto &list:s.combos)for(auto chord:list){participants|=chord;if((held&chord)==chord)matched|=chord;}
    pendingSingles|=pressed&participants;
    pendingSingles&=~matched;
    releasedSingles=pendingSingles&~held;
    pendingSingles&=held;
    liveTrigger=pressed&(~participants|matched);
    liveHold=held&(~participants|matched);
}
