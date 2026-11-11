#include "navigation_default_sleeve.hpp"
#include "library_tabs.hpp"
#include "launcher_overrides.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
void Put(const std::string &path,const std::string &data="fixture"){fs::create_directories(fs::path(path).parent_path());std::ofstream(path)<<data;}
int main(){
    const std::string root="fs:/vol/external01",app=root+"/wiiu/apps/uFlow";
    assert(!fs::exists(root));fs::create_directories(app);
    LibraryDefinition snes{"snes","SNES",Platform::Files,GameLauncher::WiiFlow,".sfc",true,1,"snes-retroarch",178,125,25};
    assert(!snes.mediaByContentType);snes.mediaByContentType=true;
    std::vector<LibraryDefinition> libraries{{"wiiu","Wii U"},snes};
    assert(SaveLibraries(libraries));assert(LoadLibraries()[1].mediaByContentType);
    GameEntry g;g.absolutePath=root+"/raw/Test";g.titleId="0005000010123400";g.name="Super Test";g.vcSystem="snes";g.navigationLibraryId="snes";
    g.coverPath="wiiu-front.png";g.fullCoverPath="wiiu-dvd.png";g.backCoverPath="wiiu-back.png";g.previewPath="original.mp4";g.favorite=true;
    g.rpxRelative="raw/Test/code/main.rpx";const auto original=CaptureGameMedia(g);
    const auto covers=root+"/snes/front",sleeves=root+"/snes/full",video=root+"/snes/video";
    Put(covers+"/Super Test.png");Put(sleeves+"/Default.png");Put(video+"/Super Test.mp4");Put(root+"/wiiu/0005000010123400.png");
    std::vector<MediaSource> sources{{MediaType::Cover,-1,covers,true,0,"snes"},{MediaType::FullCover,-1,sleeves,true,0,"snes"},{MediaType::Preview,-1,video,true,0,"snes"},{MediaType::FullCover,-1,root+"/wiiu",true,0,"wiiu"}};
    sources[1].defaultFile="Default.png";sources[2].video.volume=30;assert(SaveMediaSources(sources));
    std::vector<GameEntry> games{g};ApplyContentTypeMedia(games,libraries);
    auto &styled=games[0];assert(styled.coverPath==covers+"/Super Test.png"&&styled.fullCoverPath.empty());
    assert(styled.defaultSleevePath==sleeves+"/Default.png"&&styled.backCoverPath.empty());
    assert(styled.mediaLibraryId=="snes"&&styled.previewPath==video+"/Super Test.mp4");
    assert(VideoSourceFor(sources,styled)->video.volume==30);
    assert(styled.libraryId=="wiiu"&&styled.platform==Platform::WiiU&&styled.favorite&&styled.rpxRelative==g.rpxRelative);
    assert(EffectiveLauncher(styled,{}, {},libraries)=="native");
    assert(SaveLibraryCache(games));assert(SaveLibraries(libraries,false));
    std::vector<GameEntry> cached;assert(LoadLibraryCache(cached));
    assert(cached[0].fullCoverPath==original.full&&cached[0].coverPath==original.cover&&cached[0].mediaLibraryId.empty());
    libraries[1].mediaByContentType=false;ApplyContentTypeMedia(games,libraries);
    assert(games[0].coverPath==original.cover&&games[0].fullCoverPath==original.full&&games[0].previewPath==original.video&&games[0].sourceMedia);
    assert(games[0].defaultSleevePath==sleeves+"/Default.png");
    libraries[1].mediaByContentType=true;games[0].navigationLibraryId.clear();ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath==original.full); // sorting off restores source appearance
    games[0].navigationLibraryId="snes";assert(SaveMediaSources({}));ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath.empty()&&games[0].coverPath.empty()&&games[0].mediaLibraryId=="snes"&&games[0].previewPath==original.video);
    RestoreSourceMedia(games[0]);assert(games[0].fullCoverPath==original.full);
    libraries[1].mediaByContentType=false;
    const auto nativeDefault=root+"/wiiu/Default.png";Put(nativeDefault);
    sources.back().defaultFile="Default.png";assert(SaveMediaSources(sources));
    games={g};games[0].coverPath.clear();games[0].fullCoverPath=nativeDefault;games[0].defaultSleevePath=nativeDefault;
    assert(ContentTypeMediaNeeded(games[0],libraries));ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath.empty()&&games[0].defaultSleevePath==sleeves+"/Default.png");
    assert(!ContentTypeMediaNeeded(games[0],libraries));
    assert(SaveLibraryCache(games));assert(LoadLibraryCache(cached));assert(cached[0].fullCoverPath==nativeDefault);
    games[0].navigationLibraryId.clear();assert(ContentTypeMediaNeeded(games[0],libraries));ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath==nativeDefault&&!ContentTypeMediaNeeded(games[0],libraries));
    for(const std::string id:{"ds","nes","snes","gba","n64","pce","msx"})for(bool own:{false,true}){
        auto type=snes;type.id=id;type.mediaByContentType=own;
        const auto folder=root+"/defaults/"+id;Put(folder+"/Default.png");
        MediaSource fallback{MediaType::FullCover,-1,folder,true,0,id};fallback.defaultFile="Default.png";
        Put(folder+"/labels/Default.png");
        MediaSource label{MediaType::CartridgeLabel,-1,folder+"/labels",true,0,id};label.defaultFile="Default.png";
        assert(SaveMediaSources({sources.back(),fallback,label}));
        games={g};games[0].navigationLibraryId=id;games[0].coverPath.clear();games[0].backCoverPath.clear();
        games[0].fullCoverPath=games[0].defaultSleevePath=nativeDefault;
        ApplyContentTypeMedia(games,{libraries[0],type});
        assert(games[0].defaultSleevePath==folder+"/Default.png");
        assert(games[0].cartridgeLabelPath==folder+"/labels/Default.png");
        assert(games[0].fullCoverPath!=nativeDefault&&!ContentTypeMediaNeeded(games[0],{libraries[0],type}));
    }
    std::map<std::string,GameMedia> defaults;defaults["wiiu"].sleeve=nativeDefault;defaults["ds"].sleeve="ds-default.png";
    auto fast=g;fast.navigationLibraryId="ds";fast.coverPath.clear();fast.fullCoverPath=fast.defaultSleevePath=nativeDefault;
    ApplyNavigationDefaultSleeve(fast,defaults);assert(fast.fullCoverPath.empty()&&fast.defaultSleevePath=="ds-default.png");
    assert(fast.sourceMedia->full==nativeDefault);ApplyNavigationDefaultSleeve(fast,defaults);assert(fast.defaultSleevePath=="ds-default.png");
    fast.navigationLibraryId.clear();ApplyNavigationDefaultSleeve(fast,defaults);assert(fast.fullCoverPath==nativeDefault);
    fast.navigationLibraryId="ds";fast.fullCoverPath="real-game.png";ApplyNavigationDefaultSleeve(fast,defaults);assert(fast.fullCoverPath=="real-game.png");
    std::cout<<"PASS: type media, shared-artwork system defaults, reversible appearance and source cache\n";
}
