#include "browser_worker.hpp"
#include "directory_entry.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

static SDL_sem *entered;
static SDL_threadID ui;
static std::atomic<int> cancelledJobs{0};
static unsigned statCalls=0;
extern "C" int __real_stat(const char *,struct stat *);
extern "C" int __wrap_stat(const char *path,struct stat *info){++statCalls;return __real_stat(path,info);}
static BrowserWorker::Result Slow(const BrowserWorker::Request &r,const BrowserCancelled &cancelled){
    assert(SDL_ThreadID()!=ui);
    if(r.path=="slow"){
        SDL_SemPost(entered);
        const auto start=SDL_GetTicks();
        while(!cancelled() && SDL_GetTicks()-start<3000)SDL_Delay(1);
        assert(cancelled());++cancelledJobs;
    }
    return {{r.path},"",true};
}
static BrowserWorker::Result Wait(BrowserWorker &worker){
    const auto start=SDL_GetTicks();
    while(SDL_GetTicks()-start<3000){auto r=worker.Take();if(r.available)return r;SDL_Delay(1);}
    assert(false);return {};
}
int main(){
    assert(SDL_Init(0)==0);ui=SDL_ThreadID();entered=SDL_CreateSemaphore(0);
    {
        BrowserWorker worker(Slow);assert(worker.Available());worker.Start({"slow"});
        assert(SDL_SemWaitTimeout(entered,3000)==0);
        assert(!worker.Take().available); // UI remains usable while storage is blocked
        worker.Start({"new"});auto r=Wait(worker);
        assert(r.entries==std::vector<std::string>{"new"} && cancelledJobs==1);
        worker.Start({"slow"});assert(SDL_SemWaitTimeout(entered,3000)==0);worker.Cancel();
        worker.Start({"last"});r=Wait(worker);assert(r.entries[0]=="last" && cancelledJobs==2);
        worker.Start({"slow"});assert(SDL_SemWaitTimeout(entered,3000)==0);
    } // shutdown cancels and joins, without publishing an obsolete result
    assert(cancelledJobs==3);
    namespace fs=std::filesystem;
    fs::create_directories("large/Child");
    for(int i=0;i<10000;++i)std::ofstream("large/rom-"+std::to_string(i)+".nes").put('x');
    for(int i=0;i<120;++i)fs::create_directory("large/Folder-"+std::to_string(i));
    std::ofstream("large/Default.png").put('x');
    statCalls=0;
    auto dirs=ListDirectories("large");assert(dirs.size()==121);
    auto files=ListMediaFiles("large",MediaType::FullCover);assert(files.size()==122 && files.back()=="Default.png");
    assert(statCalls==0); // known entries must not be searched again by path
    dirent unknown{};unknown.d_type=DT_UNKNOWN;
    assert(S_ISDIR(DirectoryEntryMode(unknown,"large/Child")) && statCalls==1);
    fs::create_directory_symlink("Child","large/Link");
    dirs=ListDirectories("large");assert(dirs.size()==122 && statCalls==2);
    int checks=0;assert(ListDirectories("large",[&]{return ++checks>20;}).empty());
    checks=0;assert(ListMediaFiles("large",MediaType::FullCover,[&]{return ++checks>20;}).empty());
    {
        BrowserWorker worker;
        worker.Start({"large"});assert(Wait(worker).entries.size()==122);
        worker.Start({"large",true,MediaType::FullCover});assert(Wait(worker).entries.size()==123);
        worker.Start({"missing"});auto r=Wait(worker);assert(r.entries.empty() && !r.error.empty());
    }
    SDL_DestroySemaphore(entered);SDL_Quit();
    puts("PASS: 10,000-file folder, zero redundant stat lookups, unknown/symlink fallback, async listing, navigation cancellation, stale result rejection, read errors and shutdown");
}
