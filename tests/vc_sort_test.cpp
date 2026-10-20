#include "library_tabs.hpp"
#include "launcher_overrides.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

namespace fs=std::filesystem;
void Put(const std::string &p,const std::string &s){fs::create_directories(fs::path(p).parent_path());std::ofstream(p,std::ios::binary)<<s;}
int main(){
    const std::string root="fs:/vol/external01",base=root+"/raw";
    assert(!fs::exists(root));
    fs::create_directories(root+"/wiiu/apps/uFlow");
    const std::pair<const char*,const char*> codes[]={{"FBDE","nes"},{"JANE","snes"},{"NAAE","n64"},{"DADE","ds"},{"PAKE","gba"},{"PNWE","pce"},{"MNZJ","msx"},{"VAKE","wii"},{"ABCE","wiiu"}};
    for(auto [code,id]:codes)assert(WiiUVC::System(std::string("WUP-N-")+code)==id);
    assert(WiiUVC::System("WUP-N-GKGE",true)=="gamecube");
    assert(WiiUVC::System("WUP-N-D43E",true)=="gamecube");
    assert(WiiUVC::System("WUP-N-RMGE",true)=="wii");
    assert(WiiUVC::System("WUP-P-ABCD")=="wiiu");
    GameEntry g;g.absolutePath=base+"/game";g.titleId="0005000012345600";g.vcSystem="gamecube";
    g.physicalMediaPath="disc.png";g.coverPath="keep.png";g.rpxRelative="raw/game/code/main.rpx";g.favorite=true;
    std::vector<GameEntry> games{g,g};games[1].installed=true;games[1].installedTitleId=0x0005000012345600ULL;
    UserSettings prefs;assert(!prefs.autoSortInstalledVC);
    std::vector<ContentSource> sources{{Platform::WiiU,base,true,3,GameLauncher::WiiFlow,"wiiu","native"}};
    ApplyVCCategories(games,prefs,sources);assert(GameLibraryId(games[0])=="wiiu"&&GameLibraryId(games[1])=="wiiu");
    sources[0].autoSortVC=true;ApplyVCCategories(games,prefs,sources);
    assert(GameLibraryId(games[0])=="gamecube"&&GameLibraryId(games[1])=="wiiu");
    auto libraries=LoadLibraries();
    assert(GameMatchesLibrary(games[0],"gamecube")&&!GameMatchesLibrary(games[0],"wiiu"));
    assert(GameMatchesLibrary(games[0],"__all")&&GameMatchesLibrary(games[0],"__favorites"));
    assert(EffectiveLauncher(games[0],{},sources,libraries)=="native");
    assert(games[0].platform==Platform::WiiU&&games[0].libraryId=="wiiu"&&games[0].coverPath==g.coverPath&&games[0].rpxRelative==g.rpxRelative);
    prefs.autoSortInstalledVC=true;sources[0].autoSortVC=false;ApplyVCCategories(games,prefs,sources);
    assert(GameLibraryId(games[0])=="wiiu"&&GameLibraryId(games[1])=="gamecube");
    prefs.showInstalled=false;assert(!GameAvailableInNavigation(games[1],prefs,libraries));
    prefs.showInstalled=true;SaveSettings(prefs);assert(LoadSettings().autoSortInstalledVC);
    prefs.autoSortInstalledVC=false;SaveSettings(prefs);assert(!LoadSettings().autoSortInstalledVC);
    sources[0].autoSortVC=true;assert(SaveContentSources(sources));auto loaded=LoadContentSources();assert(loaded[0].autoSortVC&&loaded[0].launcherId=="native");
    // Nested sources take precedence and similarly named siblings never match.
    sources.push_back({Platform::WiiU,base+"/game",true,3,GameLauncher::WiiFlow,"wiiu","native",false});
    ApplyVCCategories(games,prefs,sources);assert(GameLibraryId(games[0])=="wiiu");
    sources.resize(1);games[0].absolutePath=base+"-other/game";ApplyVCCategories(games,prefs,sources);assert(GameLibraryId(games[0])=="wiiu");
    games[0]=g;games[0].vcSystem="unknown";ApplyVCCategories(games,prefs,sources);assert(GameLibraryId(games[0])=="wiiu");
    games[0]=g;games[0].libraryId="install";ApplyVCCategories(games,prefs,sources);assert(GameLibraryId(games[0])=="install");
    assert(SaveLibraryCache({g}));assert(SaveContentSources(sources,false));
    std::vector<GameEntry> cached;assert(LoadLibraryCache(cached)&&cached[0].vcSystem=="gamecube"&&cached[0].navigationLibraryId.empty());
    assert(cached[0].physicalMediaPath=="disc.png");
    // Migrate an actual v7 record without scanning; metadata is read once.
    const std::string cache=root+"/wiiu/apps/uFlow/cache/library-v1.bin";
    std::ifstream input(cache,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
    bytes.erase(12,4); // v7-v9 did not contain the v10 locale header.
    auto v8=bytes;v8.resize(v8.size()-4-g.physicalMediaPath.size());uint32_t version8=8;std::memcpy(v8.data()+4,&version8,4);Put(cache,v8);
    assert(LoadLibraryCache(cached) && cached[0].vcSystem=="gamecube" && cached[0].physicalMediaPath.empty());
    bytes.resize(bytes.size()-8-g.vcSystem.size()-g.physicalMediaPath.size());uint32_t version=7;std::memcpy(bytes.data()+4,&version,4);Put(cache,bytes);
    Put(g.absolutePath+"/meta/meta.xml","<menu><product_code>WUP-N-GKGE</product_code></menu>");Put(g.absolutePath+"/code/fw.img","fixture");
    assert(LoadLibraryCache(cached)&&cached[0].vcSystem=="gamecube"&&cached[0].coverPath==g.coverPath);
    fs::remove_all(g.absolutePath);assert(LoadLibraryCache(cached)&&cached[0].vcSystem=="gamecube");
    std::cout<<"PASS: VC classification, independent switches, source boundaries, launch/media preservation, settings and cache migration\n";
}
