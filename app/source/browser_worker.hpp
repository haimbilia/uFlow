#pragma once
#include "library.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <utility>

// One directory job at a time; navigation replaces the pending job and cancels
// old enumeration between entries. Never hold the UI mutex during filesystem I/O.
class BrowserWorker {
public:
    struct Request { std::string path; bool media = false; MediaType type = MediaType::FullCover; };
    struct Result { std::vector<std::string> entries; std::string error; bool available = false; };
    using List = Result (*)(const Request &, const BrowserCancelled &);
    explicit BrowserWorker(List list = Read) : list_(list) {
        mutex_=SDL_CreateMutex(); condition_=SDL_CreateCond();
        if(mutex_ && condition_)thread_=SDL_CreateThreadWithStackSize(Run,"uflow-browser",128*1024,this);
    }
    ~BrowserWorker() {
        if(thread_){
            SDL_LockMutex(mutex_);stop_=true;++generation_;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);
            SDL_WaitThread(thread_,nullptr);
        }
        if(condition_)SDL_DestroyCond(condition_);
        if(mutex_)SDL_DestroyMutex(mutex_);
    }
    BrowserWorker(const BrowserWorker &)=delete;
    BrowserWorker &operator=(const BrowserWorker &)=delete;
    bool Available() const {return thread_!=nullptr;}
    void Start(Request request) {
        if(!thread_)return;
        SDL_LockMutex(mutex_);++generation_;request_=std::move(request);pending_=true;ready_={};
        SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);
    }
    void Cancel() {
        if(!thread_)return;
        SDL_LockMutex(mutex_);++generation_;pending_=false;ready_={};SDL_UnlockMutex(mutex_);
    }
    Result Take() {
        if(!thread_)return {};
        SDL_LockMutex(mutex_);auto result=std::move(ready_);ready_={};SDL_UnlockMutex(mutex_);
        return result;
    }
    static Result Read(const Request &request,const BrowserCancelled &cancelled) {
        Result result;
        result.entries=request.media?ListMediaFiles(request.path,request.type,cancelled,&result.error):
                                     ListDirectories(request.path,cancelled,&result.error);
        result.available=true;return result;
    }
private:
    static int Run(void *self){return static_cast<BrowserWorker *>(self)->Work();}
    int Work() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        SDL_LockMutex(mutex_);
        while(!stop_){
            while(!stop_ && !pending_)SDL_CondWait(condition_,mutex_);
            if(stop_)break;
            const auto request=request_;const unsigned generation=generation_.load();pending_=false;
            SDL_UnlockMutex(mutex_);
            Result result;
            try {result=list_(request,[this,generation]{return generation_.load()!=generation;});}
            catch(...){result.error="CANNOT READ FOLDER";}
            SDL_LockMutex(mutex_);
            if(!stop_ && generation_.load()==generation){result.available=true;ready_=std::move(result);}
        }
        SDL_UnlockMutex(mutex_);return 0;
    }
    List list_;
    SDL_mutex *mutex_=nullptr;
    SDL_cond *condition_=nullptr;
    SDL_Thread *thread_=nullptr;
    std::atomic<unsigned> generation_{0};
    bool stop_=false,pending_=false;
    Request request_;
    Result ready_;
};
