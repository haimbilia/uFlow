#pragma once
#include "library.hpp"
#include "settings.hpp"
#include "wiiu_vc.hpp"
#include <algorithm>
#include <set>

inline int StartupLibraryIndex(const std::vector<std::string> &tabs,const UserSettings &settings) {
    if(tabs.empty())return 0;
    if(!settings.startLibrary.empty()) {
        const auto found=std::find(tabs.begin(),tabs.end(),settings.startLibrary);
        return found==tabs.end()?0:static_cast<int>(found-tabs.begin());
    }
    return std::clamp(settings.startTab,0,static_cast<int>(tabs.size())-1);
}

inline std::string GameLibraryId(const GameEntry &game) {
    if(!game.navigationLibraryId.empty())return game.navigationLibraryId;
    return game.libraryId.empty() ? DefaultLibraryId(game.platform) : game.libraryId;
}
inline std::string DesiredMediaLibrary(const GameEntry &game,const std::vector<LibraryDefinition> &libraries){
    const auto id=GameLibraryId(game);
    if(id!=game.libraryId&&game.platform==Platform::WiiU&&game.installPackages.empty())
        for(const auto &library:libraries)if(library.id==id&&library.mediaByContentType)return id;
    return game.libraryId;
}
inline void ApplyVCCategories(std::vector<GameEntry> &games,const UserSettings &settings,
                              const std::vector<ContentSource> &sources) {
    for(auto &game:games){
        game.navigationLibraryId.clear();
        if(game.platform!=Platform::WiiU||!game.installPackages.empty()||game.libraryId=="install"||!WiiUVC::Recognized(game.vcSystem))continue;
        bool enabled=settings.autoSortInstalledVC;
        if(!game.installed){
            const ContentSource *best=nullptr;
            for(const auto &source:sources){
                if(!source.enabled||source.platform!=Platform::WiiU||source.libraryId=="install")continue;
                const auto id=source.libraryId.empty()?"wiiu":source.libraryId;
                if(id!=game.libraryId)continue;
                auto path=source.path;while(path.size()>1&&path.back()=='/')path.pop_back();
                if(path.empty()||(game.absolutePath!=path&&game.absolutePath.rfind(path+"/",0)!=0))continue;
                if(!best||path.size()>best->path.size())best=&source;
            }
            enabled=best&&best->autoSortVC;
        }
        if(enabled)game.navigationLibraryId=game.vcSystem;
    }
}
inline bool GameAvailableInNavigation(const GameEntry &game, const UserSettings &settings,
                                      const std::vector<LibraryDefinition> &libraries) {
    if(game.libraryId=="install" && (!settings.titleInstallerEnabled || !TitleInstall::Available()))return false;
    if ((game.installed && !settings.showInstalled) ||
        (game.installPackages.empty() && !game.installed && game.platform==Platform::WiiU && !settings.showRawWiiU) ||
        (game.platform==Platform::Wii && !settings.showWii) ||
        (game.platform==Platform::GameCube && !settings.showGameCube)) return false;
    const auto id=GameLibraryId(game);
    const auto definition=std::find_if(libraries.begin(),libraries.end(),[&](const auto &l){return l.id==id;});
    return definition==libraries.end() || definition->enabled;
}
// Installation packages belong to their dedicated library, not the game catalog.
inline bool GameMatchesLibrary(const GameEntry &game,const std::string &library) {
    if(library=="__all")return game.installPackages.empty() && GameLibraryId(game)!="install";
    return (library=="__favorites" && game.favorite) || library==GameLibraryId(game);
}
// Called when the library/visibility changes, never once per rendered frame.
inline std::vector<std::string> AvailableLibraryTabs(const std::vector<GameEntry> &games,
    const std::vector<LibraryDefinition> &libraries,const UserSettings &settings) {
    std::set<std::string> available;
    for(const auto &game:games)if(GameAvailableInNavigation(game,settings,libraries))available.insert(GameLibraryId(game));
    if(settings.titleInstallerEnabled&&TitleInstall::Available()&&std::any_of(libraries.begin(),libraries.end(),[](const auto&l){return l.id=="install"&&l.enabled;}))available.insert("install");
    std::vector<std::string> tabs;
    if(!available.empty())tabs.push_back("__all");
    for(const auto &library:libraries)if(library.enabled && available.erase(library.id))tabs.push_back(library.id);
    for(const auto &id:available)if(id!="__all"&&id!="__favorites")tabs.push_back(id);
    tabs.push_back("__favorites");
    return tabs;
}
