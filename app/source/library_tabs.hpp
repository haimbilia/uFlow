#pragma once
#include "library.hpp"
#include "settings.hpp"
#include <algorithm>
#include <set>

inline std::string GameLibraryId(const GameEntry &game) {
    return game.libraryId.empty() ? DefaultLibraryId(game.platform) : game.libraryId;
}
inline bool GameAvailableInNavigation(const GameEntry &game, const UserSettings &settings,
                                      const std::vector<LibraryDefinition> &libraries) {
    if ((game.installed && !settings.showInstalled) ||
        (!game.installed && game.platform==Platform::WiiU && !settings.showRawWiiU) ||
        (game.platform==Platform::Wii && !settings.showWii) ||
        (game.platform==Platform::GameCube && !settings.showGameCube)) return false;
    const auto id=GameLibraryId(game);
    const auto definition=std::find_if(libraries.begin(),libraries.end(),[&](const auto &l){return l.id==id;});
    return definition==libraries.end() || definition->enabled;
}
// Called when the library/visibility changes, never once per rendered frame.
inline std::vector<std::string> AvailableLibraryTabs(const std::vector<GameEntry> &games,
    const std::vector<LibraryDefinition> &libraries,const UserSettings &settings) {
    std::set<std::string> available;
    for(const auto &game:games)if(GameAvailableInNavigation(game,settings,libraries))available.insert(GameLibraryId(game));
    std::vector<std::string> tabs;
    if(!available.empty())tabs.push_back("__all");
    for(const auto &library:libraries)if(library.enabled && available.erase(library.id))tabs.push_back(library.id);
    for(const auto &id:available)if(id!="__all"&&id!="__favorites")tabs.push_back(id);
    tabs.push_back("__favorites");
    return tabs;
}
