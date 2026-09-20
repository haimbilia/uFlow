#include "launcher_overrides.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
int NoReplaceRename(const char *from,const char *to) {
    if(std::filesystem::exists(to)){errno=EEXIST;return -1;}
    return std::rename(from,to);
}
int FailCommitRename(const char *from,const char *to) {
    if(std::string(from).ends_with(".tmp")){errno=EIO;return -1;}
    return NoReplaceRename(from,to);
}
int main() {
    std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    GameEntry g;g.platform=Platform::Files;g.libraryId="ds";g.absolutePath="fs:/vol/external01/nds/sub/game.nds";
    LibraryDefinition ds;ds.id="ds";ds.parser=Platform::Files;ds.launcherId="noods";
    ContentSource s;s.platform=Platform::Files;s.libraryId="ds";s.path="fs:/vol/external01/nds";s.launcherId="ds-vc";
    LauncherOverrides overrides;std::vector<ContentSource> sources={s};
    assert(EffectiveLauncher(g,overrides,sources,{ds})=="ds-vc");
    overrides[LauncherGameKey(g)]="noods";
    assert(EffectiveLauncher(g,overrides,sources,{ds})=="noods");
    assert(SaveLauncherOverrides(overrides));assert(LoadLauncherOverrides()==overrides);
    const std::string target=LauncherOverridesPath,temp=target+".tmp";
    {std::ofstream f(temp);f<<LauncherGameKey(g)<<"\tds-vc\n";}
    assert(CommitLauncherOverrides(temp,target,NoReplaceRename));
    assert(LoadLauncherOverrides().at(LauncherGameKey(g))=="ds-vc");
    {std::ofstream f(temp);f<<LauncherGameKey(g)<<"\tnoods\n";}
    assert(!CommitLauncherOverrides(temp,target,FailCommitRename));
    assert(LoadLauncherOverrides().at(LauncherGameKey(g))=="ds-vc");
    assert(std::rename(target.c_str(),(target+".bak").c_str())==0);
    assert(LoadLauncherOverrides().at(LauncherGameKey(g))=="ds-vc");
    assert(std::rename((target+".bak").c_str(),target.c_str())==0);
    overrides.clear();assert(SaveLauncherOverrides(overrides));assert(LoadLauncherOverrides().empty());
    ContentSource nested=s;nested.path+="/sub";nested.launcherId="";sources.push_back(nested);
    assert(EffectiveLauncher(g,overrides,sources,{ds})=="noods");
    sources[1].enabled=false;assert(EffectiveLauncher(g,overrides,sources,{ds})=="ds-vc");
    g.absolutePath="fs:/vol/external01/nds-other/game.nds";
    assert(EffectiveLauncher(g,overrides,sources,{ds})=="noods");
    assert(SaveContentSources(sources));const auto loaded=LoadContentSources();
    assert(loaded.size()==2&&loaded[0].launcherId=="ds-vc"&&loaded[1].launcherId.empty());
    g.platform=Platform::WiiU;g.installed=true;g.titleId="0005000012345600";
    assert(LauncherGameKey(g)=="title:0005000012345600");
    g.libraryId="wiiu";assert(EffectiveLauncher(g,{},sources,{ds})=="native");
    g.platform=Platform::Wii;g.installed=false;g.launcher=GameLauncher::USBLoaderGX;
    assert(EffectiveLauncher(g,{},sources,{ds})=="usbloadergx");
    std::cout<<"PASS: launcher precedence, nested paths, boundary matching, persistence, source round trip, legacy fallback\n";
}
