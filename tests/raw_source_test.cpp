#include "library.hpp"
#include "raw_launch.hpp"
#include "../module/src/RawSourcePath.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
int main(){
    const std::string root="usb1:/Games/Test";
    std::filesystem::create_directories(root+"/code");
    std::filesystem::create_directories(root+"/content");
    std::ofstream(root+"/code/app.xml")<<"<app><title_id>0005000010157400</title_id></app>";
    std::ofstream(root+"/code/cos.xml")<<"<app><argstr>main.rpx</argstr></app>";
    unsigned char elf[20]={0x7f,'E','L','F',1,2};elf[19]=20;
    std::ofstream(root+"/code/main.rpx",std::ios::binary).write((char*)elf,20);
    GameEntry game;game.platform=Platform::WiiU;game.titleId="0005000010157400";
    game.absolutePath=root;game.rpxRelative=root+"/code/main.rpx";
    assert(ValidateRawGame(game).empty());
    assert(SaveContentSources({{Platform::WiiU,"usb1:/Games",true,3}}));
    const auto scan=ScanLibrary();assert(scan.games.size()==1);
    const auto &found=scan.games.front();
    assert(found.rpxRelative==root+"/code/main.rpx"&&found.contentRelative==root+"/content"&&found.codeRelative==root+"/code");
    assert(found.saveRelative.rfind("wiiu/raw-saves/",0)==0&&ValidateRawGame(found).empty());
    assert(RawSourcePath(game.rpxRelative.c_str())=="ufusb:/Games/Test/code/main.rpx");
    assert(RawSourcePath("Games/Test/code/main.rpx")=="fs:/vol/external01/Games/Test/code/main.rpx");
    game.rpxRelative="missing";assert(!ValidateRawGame(game).empty());
    game.absolutePath="other:/Games/Test";assert(!ValidateRawGame(game).empty());
    std::cout<<"PASS: raw USB preflight and module USB/SD path resolution\n";
}
