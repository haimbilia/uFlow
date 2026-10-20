#include "scan_worker.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static SDL_sem *entered;
static std::atomic<int> mode{0},saves{0};
static SDL_threadID ui;
static ScanResult FakeScan(const ScanObserver &observer){
    assert(SDL_ThreadID()!=ui);SDL_SemPost(entered);
    if(mode==0)for(;;){observer({"SCANNING SOURCE 1 OF 1","usb1:/Decrypted",3,0,0});SDL_Delay(1);}
    if(mode==1)throw std::runtime_error("read failed");
    observer({"MATCHING ARTWORK","test",2,1,2});ScanResult result;result.games.resize(2);result.games[0].absolutePath="usb1:/wbfs/existing.wbfs";result.games[0].titleId="RSPE01";result.games[1].absolutePath="usb1:/wbfs/new.wbfs";return result;
}
static bool FakeSave(const std::vector<GameEntry> &games){assert(SDL_ThreadID()!=ui && games.size()==2);++saves;return mode!=3;}
static ScanWorker::Status Wait(ScanWorker &worker){
    const auto start=SDL_GetTicks();
    while(SDL_GetTicks()-start<3000){auto s=worker.Poll();if(s.finished)return s;SDL_Delay(1);}
    assert(false);return {};
}
static void Write(const std::string &path,const std::string &data){fs::create_directories(fs::path(path).parent_path());std::ofstream(path)<<data;}
static size_t Handles(){return std::distance(fs::directory_iterator("/proc/self/fd"),fs::directory_iterator{});}
int main(){
    assert(SDL_Init(0)==0);ui=SDL_ThreadID();entered=SDL_CreateSemaphore(0);
    {
        ScanWorker worker(FakeScan,FakeSave);worker.Start();assert(SDL_SemWaitTimeout(entered,3000)==0);
        for(int i=0;i<100;++i){auto s=worker.Poll();assert(!s.finished);}
        worker.RequestCancel();assert(!Wait(worker).error.empty() && saves==0);
        worker.Stop();
        mode=1;worker.Start();auto s=Wait(worker);assert(!s.error.empty() && saves==0);
        mode=2;worker.Start();s=Wait(worker);assert(s.error.empty() && s.cacheSaved && saves==1);
        assert(s.progress.games==2 && worker.Take().games.size()==2);
        mode=3;worker.Start();s=Wait(worker);assert(s.error.empty() && !s.cacheSaved && worker.Take().games.size()==2);
    }
    {
        GameEntry offline;offline.absolutePath="usb1:/offline/game.nds";offline.name="Offline";offline.coverPath="kept.png";
        GameEntry existing;existing.absolutePath="usb1:/wbfs/existing.wbfs";existing.titleId="RSPE01";existing.name="Custom title";existing.favorite=true;existing.description="Keep metadata";
        std::vector<GameEntry> old{offline,existing};assert(SaveLibraryCache(old));
        ScanWorker worker(FakeScan,SaveLibraryCache);
        mode=0;worker.Start(old,true);SDL_Delay(10);worker.RequestCancel();assert(!Wait(worker).error.empty());
        std::vector<GameEntry> saved;assert(LoadLibraryCache(saved)&&saved.size()==2);
        mode=1;worker.Start(old,true);assert(!Wait(worker).error.empty());assert(LoadLibraryCache(saved)&&saved.size()==2);
        mode=2;worker.Start(old,true);assert(Wait(worker).cacheSaved);auto merged=worker.Take().games;assert(merged.size()==3);
        auto known=std::find_if(merged.begin(),merged.end(),[](const auto &g){return g.titleId=="RSPE01";});
        assert(known!=merged.end()&&known->name=="Custom title"&&known->favorite&&known->description=="Keep metadata");
        assert(LoadLibraryCache(saved)&&saved.size()==3);
        worker.Start(saved,true);assert(Wait(worker).cacheSaved&&worker.Take().games.size()==3); // idempotent
        auto different=existing;different.titleId="OTHER1";assert(AddDiscoveredGames(saved,{different})==1); // swapped drive, same path
        assert(SaveContentSources({{Platform::Wii,"usb1:/wbfs",true,3}},false));
        assert(LoadLibraryCache(saved)&&saved.size()==3); // saving a path must preserve the merge baseline
        worker.Start(saved,false);assert(Wait(worker).cacheSaved&&worker.Take().games.size()==2); // explicit complete scan replaces
        std::cout<<"PASS: add-only scan keeps offline entries, metadata and favorites, skips repeats, preserves cache on cancel/error; complete scan replaces\n";
    }
    const std::string root="fs:/vol/external01";
    for(int i=0;i<8;++i){
        const auto base=root+"/Decrypted/Game-"+std::to_string(i);
        Write(base+"/code/app.xml","<app><title_id>000500001012F000</title_id></app>");
        Write(base+"/code/cos.xml","<app><argstr>main.rpx</argstr></app>");
        std::string elf(20,'\0');elf[0]=0x7f;elf[1]='E';elf[2]='L';elf[3]='F';elf[4]=1;elf[5]=2;elf[19]=20;
        Write(base+"/code/main.rpx",elf);fs::create_directories(base+"/content");
    }
    assert(SaveContentSources({{Platform::WiiU,root+"/Decrypted",true,3}}));
    Write(root+"/art/Game-0.png","fixture");
    MediaSource media;media.type=MediaType::FullCover;media.path=root+"/art";assert(SaveMediaSources({media}));
    bool discovered=false,indexed=false,matched=false,sorted=false;
    auto scan=ScanLibrary([&](const ScanProgress &p){
        discovered|=p.stage.find("SCANNING SOURCE")==0 && p.path.find("Decrypted")!=std::string::npos;
        indexed|=p.stage=="INDEXING ARTWORK";
        matched|=p.stage=="MATCHING ARTWORK" && p.completed==8 && p.total==8;
        sorted|=p.stage=="SORTING LIBRARY";
    });
    assert(scan.games.size()==8 && discovered && indexed && matched && sorted);
    assert(SaveLibraryCache(scan.games));
    const auto handles=Handles();
    for(const std::string stage:{"SCANNING SOURCE","INDEXING ARTWORK"}){
        bool interrupted=false;
        try{ScanLibrary([&](const ScanProgress &p){if(p.stage.find(stage)==0 && (p.path.ends_with("Game-0") || p.path.ends_with("Game-0.png")))throw 7;});}
        catch(int){interrupted=true;}
        assert(interrupted && Handles()==handles);
    }
    std::vector<GameEntry> cached;assert(LoadLibraryCache(cached) && cached.size()==8);
    {
        const auto catalog=root+"/wiiu/apps/uFlow/cache/library-v1.bin";
        const auto bytes=[&]{std::ifstream in(catalog,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
        CompleteMediaRefresh();const auto before=bytes();
        media.video.volume=25;media.video.delayMs=2000;
        assert(SaveMediaSources({media}) && !MediaRefreshNeeded() && bytes()==before);
        // Saving a new media path must leave even offline games in the catalog.
        fs::remove_all(root+"/Decrypted");
        media.path=root+"/replacement-art";Write(media.path+"/Game-0.png","new-art");
        assert(SaveMediaSources({media}) && MediaRefreshNeeded() && bytes()==before);
        assert(LoadLibraryCache(cached) && cached.size()==8 && MediaRefreshNeeded());
        const auto original=cached;
        bool cancelled=false;
        auto draft=cached;
        try {RefreshLibraryMedia(draft,[](const auto &){throw 7;});}catch(int){cancelled=true;}
        assert(cancelled && bytes()==before && MediaRefreshNeeded());
        // A failed cache commit must retain the old catalog and pending retry.
        ScanWorker failed(FakeScan,+[](const std::vector<GameEntry> &){return false;});
        failed.StartMedia(cached,{},true);auto status=Wait(failed);
        assert(status.error.empty() && !status.cacheSaved && bytes()==before && MediaRefreshNeeded());
        mode=1; // FakeScan throws if a media refresh accidentally starts discovery.
        ScanWorker worker(FakeScan,SaveLibraryCache);worker.StartMedia(cached,{},true);
        status=Wait(worker);assert(status.error.empty() && status.cacheSaved && !MediaRefreshNeeded());
        assert(LoadLibraryCache(cached) && cached.size()==original.size());
        bool newArt=false;
        for(size_t i=0;i<cached.size();++i) {
            assert(cached[i].absolutePath==original[i].absolutePath && cached[i].name==original[i].name);
            assert(cached[i].libraryId==original[i].libraryId && cached[i].titleId==original[i].titleId);
            newArt|=cached[i].fullCoverPath==media.path+"/Game-0.png";
        }
        assert(newArt);
        assert(SaveMediaSources({}) && MediaRefreshNeeded());
        worker.StartMedia(cached,{},true);assert(Wait(worker).cacheSaved);
        assert(LoadLibraryCache(cached) && cached.size()==8 && !MediaRefreshNeeded());
        for(const auto &game:cached)assert(game.fullCoverPath.empty());
    }
    {
        LibraryDefinition snes{"snes","SNES",Platform::Files};snes.mediaByContentType=true;
        auto entry=cached[0];entry.navigationLibraryId="snes";entry.vcSystem="snes";entry.fullCoverPath="original-dvd.png";
        ScanWorker worker;worker.StartMedia({entry},{snes});const auto state=Wait(worker);
        assert(state.finished&&state.error.empty()&&state.cacheSaved);
        auto result=worker.Take();assert(result.games.size()==1&&result.games[0].mediaLibraryId=="snes"&&result.games[0].fullCoverPath.empty());
        assert(LoadLibraryCache(cached)&&cached.size()==8); // appearance refresh never replaces the catalog
        InvalidateLibraryCache();worker.StartMedia({entry},{snes});assert(Wait(worker).error.empty());
        assert(!LoadLibraryCache(cached)); // preserve a pending source/scanner rescan
    }
    SDL_DestroySemaphore(entered);SDL_Quit();
    std::cout<<"PASS: live scan stages/counts, measured artwork progress, nonblocking polling, interruption/error preserve cache, descriptor cleanup, saving and cache-failure reporting\n";
}
