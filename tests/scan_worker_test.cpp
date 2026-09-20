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
    observer({"MATCHING ARTWORK","test",2,1,2});ScanResult result;result.games.resize(2);return result;
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
    SDL_DestroySemaphore(entered);SDL_Quit();
    std::cout<<"PASS: live scan stages/counts, measured artwork progress, nonblocking polling, interruption/error preserve cache, descriptor cleanup, saving and cache-failure reporting\n";
}
