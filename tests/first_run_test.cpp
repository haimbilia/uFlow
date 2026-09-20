#include "library_tabs.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){
    UserSettings settings;assert(settings.skin==1&&!settings.welcomeDismissed);
    std::vector<LibraryDefinition> libraries={{"wiiu","Wii U",Platform::WiiU,GameLauncher::WiiFlow,".rpx",true,0},
        {"wii","Wii",Platform::Wii,GameLauncher::WiiFlow,".wbfs",true,1},
        {"ds","DS",Platform::Files,GameLauncher::WiiFlow,".nds",true,2}};
    std::vector<GameEntry> games;
    assert(AvailableLibraryTabs(games,libraries,settings)==std::vector<std::string>{"__favorites"});
    GameEntry game;game.platform=Platform::Wii;game.libraryId="wii";games.push_back(game);
    assert((AvailableLibraryTabs(games,libraries,settings)==std::vector<std::string>{"__all","wii","__favorites"}));
    games.push_back(game); // no duplicate tab
    assert(AvailableLibraryTabs(games,libraries,settings).size()==3);
    settings.showWii=false;assert(AvailableLibraryTabs(games,libraries,settings).size()==1);
    settings.showWii=true;libraries[1].enabled=false;assert(AvailableLibraryTabs(games,libraries,settings).size()==1);
    libraries[1].enabled=true;game.platform=Platform::Files;game.libraryId="ds";games.push_back(game);
    assert((AvailableLibraryTabs(games,libraries,settings)==std::vector<std::string>{"__all","wii","ds","__favorites"}));
    games.clear();game.platform=Platform::WiiU;game.libraryId="";game.installed=true;games.push_back(game);
    assert((AvailableLibraryTabs(games,libraries,settings)==std::vector<std::string>{"__all","wiiu","__favorites"}));
    settings.showInstalled=false;assert(AvailableLibraryTabs(games,libraries,settings).size()==1);
    const std::string dir="fs:/vol/external01/wiiu/apps/uFlow";
    assert(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    auto fresh=LoadSettings();assert(fresh.skin==1&&!fresh.welcomeDismissed);
    // Installers may pre-create visibility settings: still a first launch.
    std::ofstream(dir+"/uflow.cfg")<<"show_installed=0\n";
    fresh=LoadSettings();assert(fresh.skin==1&&!fresh.welcomeDismissed);
    fresh.welcomeDismissed=true;SaveSettings(fresh);
    auto saved=LoadSettings();assert(saved.skin==1&&saved.welcomeDismissed);
    saved.skin=0;SaveSettings(saved);assert(LoadSettings().skin==0); // keep user's explicit Pixel Deck choice
    std::cout<<"PASS: first-run defaults/persistence and only populated enabled/visible navigation tabs\n";
}
