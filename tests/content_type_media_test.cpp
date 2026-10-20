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
    assert(games[0].coverPath==original.cover&&games[0].fullCoverPath==original.full&&games[0].previewPath==original.video&&!games[0].sourceMedia);
    libraries[1].mediaByContentType=true;games[0].navigationLibraryId.clear();ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath==original.full); // sorting off restores source appearance
    games[0].navigationLibraryId="snes";assert(SaveMediaSources({}));ApplyContentTypeMedia(games,libraries);
    assert(games[0].fullCoverPath.empty()&&games[0].coverPath.empty()&&games[0].mediaLibraryId=="snes"&&games[0].previewPath==original.video);
    RestoreSourceMedia(games[0]);assert(games[0].fullCoverPath==original.full);
    std::cout<<"PASS: type media persistence, strict library matching, SNES sleeve fallback, video settings, native launch identity, reversible appearance and original-media cache\n";
}
