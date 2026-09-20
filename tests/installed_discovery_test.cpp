#include "library_tabs.hpp"
#include <coreinit/mcp.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const std::string root="fs:/vol/storage_mlc01/usr/title/00050000/10123400";
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(root+"/code");
    std::filesystem::create_directories(root+"/meta");
    std::ofstream(root+"/code/app.xml")<<"<app/>";
    std::ofstream(root+"/meta/meta.xml")<<"<longname_en>Installed Test</longname_en>";
    MCPTitleListType title{};title.titleId=0x0005000010123400ULL;title.appType=MCP_APP_TYPE_GAME;
    std::strcpy(title.path,"/vol/storage_mlc01/usr/title/00050000/10123400");
    std::strcpy(title.indexedDevice,"mlc");testInstalledTitles={title};
    std::vector<GameEntry> games;
    assert(RefreshInstalledGames(games));assert(games.size()==1&&games[0].installed&&games[0].name=="Installed Test");
    auto fresh=ScanLibrary();assert(fresh.games.size()==1&&fresh.games[0].installed); // no configured folder required
    assert((AvailableLibraryTabs(games,LoadLibraries(),UserSettings{})==std::vector<std::string>{"__all","wiiu","__favorites"}));
    games[0].coverPath="cached-art.png";
    assert(!RefreshInstalledGames(games));assert(games[0].coverPath=="cached-art.png");
    GameEntry sd;sd.libraryId="wii";sd.platform=Platform::Wii;sd.absolutePath="sd-game.wbfs";games.push_back(sd);
    testMcpFailure=true;assert(!RefreshInstalledGames(games));assert(games.size()==2);
    testMcpFailure=false;testInstalledTitles.clear();assert(RefreshInstalledGames(games));
    assert(games.size()==1&&games[0].absolutePath==sd.absolutePath);
    // Installed titles supersede duplicate raw entries, but never redirected SD titles.
    GameEntry raw;raw.titleId="0005000010123400";raw.absolutePath="raw-game";games.push_back(raw);
    testInstalledTitles={title};assert(RefreshInstalledGames(games));assert(games.size()==2);
    std::strcpy(testInstalledTitles[0].path,"/vol/external01/redirected");
    assert(RefreshInstalledGames(games));assert(games.size()==1);
    const std::string usb="fs:/vol/storage_usb01/usr/title/00050000/10123400";
    std::filesystem::create_directories(usb+"/code");std::ofstream(usb+"/code/app.xml")<<"<app/>";
    std::strcpy(testInstalledTitles[0].path,"/vol/storage_usb01/usr/title/00050000/10123400");
    std::strcpy(testInstalledTitles[0].indexedDevice,"usb");
    assert(RefreshInstalledGames(games));
    assert(std::any_of(games.begin(),games.end(),[](const auto &g){return g.installed&&g.storage=="usb";}));
    std::cout<<"PASS: startup installed discovery, cached art, removal, SD preservation, failure and duplicate handling\n";
}
