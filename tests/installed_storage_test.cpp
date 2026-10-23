#include "library.hpp"
#include <coreinit/mcp.h>
#include <mocha/mocha.h>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
    for(const auto &device:{std::string("mlc"),std::string("usb")}){
        const auto prefix=device=="mlc"?"ufmlc:":"ufwfs:";
        const std::string canonical="fs:/vol/storage_"+device+"01/usr/title/00050000/10135e00";
        const std::string readable=std::string(prefix)+"/usr/title/00050000/10135e00";
        assert(!std::filesystem::exists(canonical));
        std::filesystem::create_directories(readable+"/code");
        std::filesystem::create_directories(readable+"/meta");
        std::ofstream(readable+"/code/app.xml")<<"<app/>";
        std::ofstream(readable+"/meta/meta.xml")<<"<longname_en>Installed Batman</longname_en>";
        MCPTitleListType title{};title.titleId=0x0005000010135e00ULL;title.appType=MCP_APP_TYPE_GAME;
        std::strcpy(title.path,canonical.substr(3).c_str());std::strcpy(title.indexedDevice,device.c_str());
        testInstalledTitles={title};std::vector<GameEntry> games;
        assert(RefreshInstalledGames(games));assert(games.size()==1);
        assert(games[0].name=="Installed Batman"&&games[0].absolutePath==canonical);
        assert(games[0].installedTitleId==title.titleId);
        assert(!RefreshInstalledGames(games));
    }
    assert((testStorageMounts==std::vector<std::string>{"ufmlc=/vol/storage_mlc01","ufwfs=/vol/storage_usb01"}));
    std::cout<<"PASS: installed titles discovered through owned privileged mounts; MCP identity preserved\n";
}
