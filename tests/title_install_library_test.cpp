#define TITLE_INSTALL_BACKEND_TEST
#include "title_install_test.cpp"
#include "library.hpp"
#include "library_tabs.hpp"
int main(){
    const std::string root="fs:/vol/external01/wiiu/apps/uFlow";
    std::filesystem::create_directories(root+"/modules");
    std::ofstream(root+"/libraries.cfg")<<"wiiu\tMy Games\twiiu\twiiflow\t.rpx\t1\t0\n";
    std::ofstream(root+"/content_sources.cfg")<<"";
    assert(LoadLibraries().size()==1&&LoadContentSources().empty());
    std::ofstream(ModulePath)<<"uflow-title-installer-v1\r\n";
    auto libraries=LoadLibraries();assert(libraries.size()==2&&libraries[0].name=="My Games");
    auto sources=LoadContentSources();assert(sources.size()==1&&sources[0].path=="fs:/vol/external01/install");
    UserSettings settings;auto tabs=AvailableLibraryTabs({},libraries,settings);assert(std::find(tabs.begin(),tabs.end(),"install")!=tabs.end());
    package("fs:/vol/external01/install/Game",0x0005000012345600ULL,0);
    package("fs:/vol/external01/install/Update",0x0005000e12345600ULL,16);
    auto scan=ScanLibrary();assert(scan.games.size()==1&&scan.games[0].installPackages.size()==2&&scan.games[0].libraryId=="install");
    assert(SaveLibraryCache(scan.games));std::vector<GameEntry> cached;assert(LoadLibraryCache(cached)&&cached[0].installPackages.size()==2);
    assert(cached[0].installPackages[1].version==16&&cached[0].installPackages[1].path==scan.games[0].installPackages[1].path);
    settings.titleInstallerEnabled=false;assert(!GameAvailableInNavigation(cached[0],settings,libraries));
    settings.titleInstallerEnabled=true;settings.showRawWiiU=false;assert(GameAvailableInNavigation(cached[0],settings,libraries));
    package("fs:/vol/external01/more/DLC",0x0005000c12345600ULL,1);
    sources.push_back({Platform::WiiU,"fs:/vol/external01/more",true,3,GameLauncher::WiiFlow,"install","title-installer"});assert(SaveContentSources(sources));
    auto merged=ScanLibrary();assert(merged.games.size()==1&&merged.games[0].installPackages.size()==3);
    sources.pop_back();
    sources[0].enabled=false;assert(SaveContentSources(sources));assert(ScanLibrary().games.empty());
    // No installed code directories exist: MCP-confirmed status is sufficient.
    auto entries=cached;
    assert(!GameMatchesLibrary(entries[0],"__all")&&GameMatchesLibrary(entries[0],"install"));
    assert(!MergeInstalledPackages(entries)&&entries.size()==1);
    GameEntry raw;raw.titleId=entries[0].titleId;raw.name="raw";raw.absolutePath="usb1:/raw";raw.favorite=true;
    entries.push_back(raw);
    GameEntry unrelated;unrelated.name="Other";unrelated.titleId="0005000099999999";unrelated.absolutePath="usb1:/other";
    entries.push_back(unrelated);
    entries[0].coverPath="existing-cover.png";
    entries[0].installPackages[0].installedVersion[0]=0; // Game succeeds, update fails.
    assert(MergeInstalledPackages(entries)&&entries.size()==3);
    auto installedGame=std::find_if(entries.begin(),entries.end(),[](const auto &g){return g.installed;});
    assert(installedGame!=entries.end()&&installedGame->favorite&&installedGame->installPackages.empty());
    assert(installedGame->absolutePath=="fs:/vol/storage_mlc01/usr/title/00050000/12345600");
    assert(installedGame->coverPath=="existing-cover.png"&&GameMatchesLibrary(*installedGame,"__all")&&GameMatchesLibrary(*installedGame,"wiiu"));
    assert(!MergeInstalledPackages(entries));
    assert(SaveLibraryCache(entries));std::vector<GameEntry> reopened;assert(LoadLibraryCache(reopened)&&reopened.size()==3);
    auto group=std::find_if(entries.begin(),entries.end(),[](const auto &g){return !g.installPackages.empty();});
    group->installPackages[0].installedVersion[1]=0;
    assert(MergeInstalledPackages(entries)&&entries.size()==4);assert(!MergeInstalledPackages(entries));
    auto updateOnly=cached;updateOnly[0].installPackages.erase(updateOnly[0].installPackages.begin());
    updateOnly[0].installPackages[0].installedVersion[0]=16;
    assert(!MergeInstalledPackages(updateOnly)&&updateOnly.size()==1);
    std::cout<<"PASS: optional module discovery, preserved libraries, default folder, empty tab, grouped scan/cache, disabled source, All exclusion, immediate verified game merge, partial jobs, destinations and cache persistence\n";
}
