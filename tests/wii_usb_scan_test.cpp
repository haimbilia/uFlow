#include "library.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
void Write(const std::string &path,const std::string &data) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream(path,std::ios::binary)<<data;
}
int main() {
    const std::string path="usb1:/wbfs/Wii Sports [RSPE01]/RSPE01.wbfs";
    std::string wbfs(1024,'\0');
    wbfs.replace(0,4,"WBFS");wbfs[8]=9;wbfs[9]=21;wbfs[12]=1;
    wbfs.replace(512,6,"RSPE01");wbfs.replace(512+24,4,"\x5d\x1c\x9e\xa3",4);
    wbfs.replace(512+32,26,"SPORTS PACK for REVOLUTION");
    Write(path,wbfs);fs::permissions(path,fs::perms::owner_read);
    assert(SaveContentSources({{Platform::Wii,"usb1:/wbfs",true,3}}));
    auto scan=ScanLibrary();assert(scan.games.size()==1);
    const auto &game=scan.games[0];
    assert(game.name=="Wii Sports" && game.titleId=="RSPE01" && game.region=="USA" && game.storage=="FAT32 USB");
    GameEntry old=game;old.name="RSPE01";old.titleId.clear();old.region.clear();old.storage.clear();
    old.favorite=true;old.description="User notes";
    auto offline=old;offline.absolutePath="usb1:/offline.wbfs";
    std::vector<GameEntry> saved{old,offline};
    assert(AddDiscoveredGames(saved,scan.games)==0 && saved.size()==2);
    auto repaired=std::find_if(saved.begin(),saved.end(),[&](const auto &g){return g.absolutePath==path;});
    assert(repaired!=saved.end() && repaired->name=="Wii Sports" && repaired->titleId=="RSPE01");
    assert(repaired->favorite && repaired->description=="User notes" && repaired->region=="USA");
    assert(AddDiscoveredGames(saved,scan.games)==0 && saved.size()==2);
    std::cout<<"PASS: read-only FAT32 WBFS header, friendly folder name, region, storage, additive repair without duplicates or loss\n";
}
