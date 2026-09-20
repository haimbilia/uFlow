#include "library.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
const std::string root="fs:/vol/external01";
void Write(const std::string &path,const std::string &data="image") {
    fs::create_directories(fs::path(path).parent_path()); std::ofstream(path)<<data;
}
int main() {
    assert(!fs::exists(root)); // run only in the disposable test directory
    const auto defaults=LoadMediaSources();
    assert(defaults.size()==kMediaTypeCount && defaults[1].type==MediaType::FullCover);
    GameEntry wiiu; wiiu.platform=Platform::WiiU; wiiu.name="140"; wiiu.titleId="00050000101fa800";
    Write(defaults[0].path+"/00050000101FA800.PNG");
    std::vector<GameEntry> games{wiiu}; EnrichLibraryMedia(games);
    assert(games[0].coverPath==defaults[0].path+"/00050000101FA800.PNG");
    const auto shared=root+"/art/shared", scoped=root+"/art/wiiu", wrong=root+"/art/gamecube";
    const auto nested=root+"/art/nested", metadata=root+"/art/info", videos=root+"/art/videos";
    Write(shared+"/140.jpg"); Write(scoped+"/00050000101fa800.JPEG"); Write(wrong+"/00050000101fa800.png");
    Write(nested+"/child/140.PNG"); Write(nested+"/child/too-deep/140.png");
    Write(metadata+"/140.ini","description=Numeric title test\ngenre=Platformer\nyear=2016\nplayers=1\npublisher=Test\n");
    Write(videos+"/140.mp4");
    std::vector<MediaSource> sources{
        {MediaType::Cover,-1,shared,true,0}, {MediaType::Cover,2,wrong,true,0},
        {MediaType::Cover,0,scoped,true,0}, {MediaType::Background,0,nested,true,0},
        {MediaType::Metadata,0,metadata,true,0}, {MediaType::Preview,-1,videos,true,0}};
    assert(SaveMediaSources(sources));
    const auto saved=LoadMediaSources();
    assert(saved.size()==6 && saved[2].library==0 && saved[2].path==scoped);
    auto invalid=sources; invalid.back().path="bad\tpath";
    assert(!SaveMediaSources(invalid));
    assert(LoadMediaSources().size()==saved.size());
    games={wiiu}; EnrichLibraryMedia(games);
    assert(games[0].coverPath==scoped+"/00050000101fa800.JPEG");
    assert(games[0].backgroundPath.empty());
    assert(games[0].description=="Numeric title test" && games[0].genre=="Platformer" && games[0].releaseYear=="2016");
    assert(games[0].previewPath==videos+"/140.mp4");
    sources[3].depth=1; sources[2].enabled=false;
    assert(SaveMediaSources(sources)); games={wiiu}; EnrichLibraryMedia(games);
    assert(games[0].coverPath==shared+"/140.jpg");
    assert(games[0].backgroundPath==nested+"/child/140.PNG");
    GameEntry gc=wiiu; gc.platform=Platform::GameCube; games={gc}; EnrichLibraryMedia(games);
    assert(games[0].coverPath==wrong+"/00050000101fa800.png");
    // Name normalization and specificity when both sources use the same key.
    GameEntry name; name.name="Mario Kart 8 (USA)";
    Write(shared+"/mario_kart_8.png"); Write(scoped+"/Mario Kart 8.PNG");
    sources[2].enabled=true; assert(SaveMediaSources(sources)); games={name}; EnrichLibraryMedia(games);
    assert(games[0].coverPath==scoped+"/Mario Kart 8.PNG");
    assert(SaveMediaSources({})); assert(LoadMediaSources().empty());
    games={wiiu}; EnrichLibraryMedia(games); assert(games[0].coverPath.empty());
    // Unavailable paths don't prevent healthy sources from loading.
    assert(SaveMediaSources({{MediaType::Cover,-1,root+"/absent",true,8},{MediaType::Cover,-1,shared,true,0}}));
    games={wiiu}; EnrichLibraryMedia(games); assert(games[0].coverPath==shared+"/140.jpg");
    // Old rows keep the 1-second foreground default; optional video fields round-trip.
    Write(root+"/wiiu/apps/uFlow/media_sources.cfg","previews\t-1\t1\t0\t"+videos+"\n");
    auto videoSources=LoadMediaSources();
    assert(videoSources.size()==1 && videoSources[0].video.delayMs==1000 && videoSources[0].video.foreground && videoSources[0].video.volume==70);
    videoSources[0].video={2250,false,30}; assert(SaveMediaSources(videoSources));
    videoSources=LoadMediaSources();
    assert(videoSources[0].video.delayMs==2250 && !videoSources[0].video.foreground && videoSources[0].video.volume==30);
    games={wiiu}; EnrichLibraryMedia(games);
    const auto *videoSource=VideoSourceFor(videoSources,games[0]);
    assert(videoSource && videoSource->video.delayMs==2250);
    Write(videos+"/Disney Bolt (USA).AVI");
    GameEntry bolt; bolt.name="Disney Bolt"; bolt.platform=Platform::Wii; bolt.libraryId="wii";
    videoSources.push_back({MediaType::Preview,-1,videos,true,0,"wii",{1000,true,0}});
    assert(SaveMediaSources(videoSources)); games={bolt}; EnrichLibraryMedia(games);
    assert(games[0].previewPath==videos+"/Disney Bolt (USA).AVI");
    assert(VideoSourceFor(videoSources,games[0])==&videoSources[1]);
    games[0].previewPath=videos+"-other/Disney Bolt (USA).AVI";
    assert(!VideoSourceFor(videoSources,games[0])); // path component boundary
    games[0].previewPath=videos+"/child/Disney Bolt (USA).AVI";
    assert(!VideoSourceFor(videoSources,games[0]));
    videoSources[1].depth=1;
    assert(VideoSourceFor(videoSources,games[0])==&videoSources[1]);
    videoSources[1].enabled=false;
    assert(!VideoSourceFor(videoSources,games[0]));
    Write(root+"/wiiu/apps/uFlow/media_sources.cfg","previews\t-1\t1\t0\t"+videos+"\tbogus\t0\t50\r\n");
    videoSources=LoadMediaSources();
    assert(videoSources.size()==1 && videoSources[0].video.delayMs==1000 && !videoSources[0].video.foreground && videoSources[0].video.volume==50);
    videoSources[0].video={99999,true,-50}; assert(SaveMediaSources(videoSources));
    videoSources=LoadMediaSources(); assert(videoSources[0].video.delayMs==10000 && videoSources[0].video.volume==0);
    assert(videoSources[0].video.foregroundOpacity==100);
    videoSources[0].video.foregroundOpacity=45;
    assert(SaveMediaSources(videoSources)); videoSources=LoadMediaSources();
    assert(videoSources[0].video.foregroundOpacity==45 && videoSources[0].defaultFile.empty());
    videoSources[0].video.foregroundOpacity=-20;assert(SaveMediaSources(videoSources));
    assert(LoadMediaSources()[0].video.foregroundOpacity==0);
    videoSources[0].video.foregroundOpacity=150;assert(SaveMediaSources(videoSources));
    assert(LoadMediaSources()[0].video.foregroundOpacity==100);
    Write(root+"/wiiu/apps/uFlow/media_sources.cfg","previews\t-1\t1\t0\t"+videos+"\t1000\t0\t70\t\tbad\n");
    assert(LoadMediaSources()[0].video.foregroundOpacity==100);
    // Optional defaults: explicit selection, independent media types and scoped precedence.
    const auto fallback=root+"/defaults", custom=root+"/defaults/custom";
    Write(fallback+"/Default.PNG"); Write(custom+"/Default.jpg");
    Write(fallback+"/Default.mp4"); Write(fallback+"/Default.ini","publisher=Generic\ndescription=No synopsis available\n");
    GameEntry missing; missing.name="Missing"; missing.titleId="MISSING"; missing.libraryId="ds"; missing.platform=Platform::Files;
    missing.absolutePath=root+"/nds/Missing.nds";
    std::vector<MediaSource> fallbacks{
        {MediaType::FullCover,-1,fallback,true,0},
        {MediaType::FullCover,-1,custom,true,0,"ds"},
        {MediaType::Logo,-1,fallback,true,0},
        {MediaType::Preview,-1,videos,true,0,"ds",{3000,false,20}},
        {MediaType::Metadata,-1,metadata,true,0}};
    assert(SaveMediaSources(fallbacks)); games={missing}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==fallback+"/Default.PNG"); // Copy-only automatic sleeve fallback.
    assert(games[0].logoPath.empty()&&games[0].previewPath.empty()); // no implicit defaults for other media
    fallbacks[0].defaultFile="Default.PNG"; // relative filename with case preserved
    fallbacks[1].defaultFile=custom+"/Default.jpg";
    fallbacks[2].defaultFile=fallback+"/Default.PNG";
    fallbacks[3].defaultFile=fallback+"/Default.mp4"; // outside scanned folder
    fallbacks[3].video.foregroundOpacity=35;
    fallbacks[4].defaultFile=fallback+"/Default.ini";
    assert(SaveMediaSources(fallbacks));
    auto reloaded=LoadMediaSources();
    for(std::size_t i=0;i<fallbacks.size();++i) assert(reloaded[i].defaultFile==fallbacks[i].defaultFile);
    assert(reloaded[3].video.delayMs==3000 && !reloaded[3].video.foreground && reloaded[3].video.volume==20);
    assert(reloaded[3].video.foregroundOpacity==35);
    missing.publisher="Actual publisher";
    games={missing}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==custom+"/Default.jpg");
    assert(games[0].coverPath.empty() && games[0].backgroundPath.empty());
    assert(games[0].logoPath==fallback+"/Default.PNG");
    assert(games[0].previewPath==fallback+"/Default.mp4");
    assert(games[0].publisher=="Actual publisher" && games[0].description=="No synopsis available");
    assert(VideoSourceFor(reloaded,games[0])==&reloaded[3]);
    assert(SaveLibraryCache(games));
    std::vector<GameEntry> cached; assert(LoadLibraryCache(cached));
    assert(cached[0].fullCoverPath==games[0].fullCoverPath && cached[0].previewPath==games[0].previewPath);
    // A real shared match beats a library-specific default, including later source entries.
    Write(fallback+"/Missing.png");
    games={missing}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==fallback+"/Missing.png");
    missing.name="Another"; missing.titleId="ANOTHER";
    missing.coverPath="native/icon.tga"; missing.backgroundPath="native/background.tga";
    fallbacks.push_back({MediaType::Cover,-1,fallback,true,0}); fallbacks.back().defaultFile="Default.PNG";
    fallbacks.push_back({MediaType::Background,-1,fallback,true,0}); fallbacks.back().defaultFile="Default.PNG";
    assert(SaveMediaSources(fallbacks)); games={missing}; EnrichLibraryMedia(games);
    assert(games[0].coverPath=="native/icon.tga" && games[0].backgroundPath=="native/background.tga");
    fallbacks[1].enabled=false; assert(SaveMediaSources(fallbacks)); games={missing}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath.empty()); // native icon wins over a generic sleeve
    fallbacks[1].enabled=true; fallbacks[1].defaultFile=custom+"/absent.png";
    assert(SaveMediaSources(fallbacks)); games={missing}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath.empty());
    // Missing/disabled defaults never leak into other content types.
    fallbacks[0].defaultFile.clear(); fallbacks[0].enabled=false; fallbacks[1].defaultFile=custom+"/Default.jpg";
    assert(SaveMediaSources(fallbacks));
    GameEntry other=missing; other.libraryId="wii"; other.platform=Platform::Wii;
    other.coverPath.clear();
    games={other}; EnrichLibraryMedia(games); assert(games[0].fullCoverPath.empty() && games[0].previewPath.empty());
    // Same scope retains configured source order; legacy platform scope is also supported.
    fallbacks[0].defaultFile="Default.PNG"; fallbacks[0].library=1; fallbacks[0].enabled=true;
    assert(SaveMediaSources(fallbacks)); games={other}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==fallback+"/Default.PNG");
    fallbacks[1].libraryId.clear(); fallbacks[1].library=1;
    assert(SaveMediaSources(fallbacks)); games={other}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==fallback+"/Default.PNG");
    // Reject malformed defaults before touching the saved config.
    auto badDefault=fallbacks; badDefault[0].defaultFile="Default.png\tbroken";
    assert(!SaveMediaSources(badDefault));
    badDefault[0].defaultFile="Default.mp4"; assert(!SaveMediaSources(badDefault));
    assert(LoadMediaSources()[0].defaultFile=="Default.PNG");
    const auto files=ListMediaFiles(fallback,MediaType::FullCover);
    assert(files.size()==3 && files[0]=="custom/" && files[1]=="Default.PNG" && files[2]=="Missing.png");
    assert(ListMediaFiles(fallback,MediaType::Preview)==std::vector<std::string>({"custom/","Default.mp4"}));
    assert(ListMediaFiles(fallback+"/absent",MediaType::Cover).empty());
    assert(FindDefaultMediaFile(fallbacks[0])==fallback+"/Default.PNG");
    assert(FindDefaultMediaFile(fallbacks[3]).empty()); // video sources never choose an image
    assert(FindDefaultMediaFile(fallbacks[1]).empty()); // Default.jpg is not the requested Default.png
    const auto dsPath=root+"/ds/fullcovers";
    Write(dsPath+"/New Super Mario Bros.jpg"); Write(dsPath+"/Default.png");
    MediaSource dsSource{MediaType::FullCover,-1,dsPath,true,0,"custom_1"};
    dsSource.defaultFile=FindDefaultMediaFile(dsSource);
    assert(SaveMediaSources({dsSource}));
    GameEntry dsGame; dsGame.platform=Platform::Files; dsGame.libraryId="custom_1"; dsGame.name="New Super Mario Bros. (USA)";
    games={dsGame}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==dsPath+"/New Super Mario Bros.jpg");
    dsGame.name="Missing DS title"; games={dsGame}; EnrichLibraryMedia(games);
    assert(games[0].fullCoverPath==dsPath+"/Default.png");
    std::cout<<"PASS: optional default files, persistence/cache, relative/absolute paths, real/native media priority, scope/order, disabled/missing defaults, video settings, metadata and filtered file browser\n";
    std::cout<<"PASS: default media, persistence, PNG/TGA/JPEG, numeric names, case, ID/name priority, platform scope, depth, disabled/empty/unavailable paths, INI and video indexing\n";
    std::cout<<"PASS: video options migration, persistence, limits, MP4/AVI names, source scope/depth/disabled resolution\n";
}
