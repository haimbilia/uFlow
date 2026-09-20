#pragma once
#include "library.hpp"
#include <SDL2/SDL.h>
#include <atomic>
#include <utility>

class ScanWorker {
public:
    struct Status { ScanProgress progress; bool finished=false,cacheSaved=false; std::string error; };
    using Scan=ScanResult (*)(const ScanObserver &);
    using Save=bool (*)(const std::vector<GameEntry> &);
    explicit ScanWorker(Scan scan=ScanLibrary,Save save=SaveLibraryCache):scan_(scan),save_(save){mutex_=SDL_CreateMutex();}
    ~ScanWorker(){Stop();if(mutex_)SDL_DestroyMutex(mutex_);}
    ScanWorker(const ScanWorker &)=delete;
    ScanWorker &operator=(const ScanWorker &)=delete;
    void Start(){
        Stop();stop_=false;status_={};status_.progress.stage="STARTING SCAN";result_={};
        if(mutex_)thread_=SDL_CreateThreadWithStackSize(Run,"uflow-scan",512*1024,this);
        if(!thread_){status_.finished=true;status_.error="CANNOT START SCAN";}
    }
    void Stop(){stop_=true;if(thread_){SDL_WaitThread(thread_,nullptr);thread_=nullptr;}}
    void RequestCancel(){stop_=true;}
    Status Poll(){if(!mutex_)return status_;SDL_LockMutex(mutex_);auto value=status_;SDL_UnlockMutex(mutex_);return value;}
    ScanResult Take(){ // Only after Poll reports finished. Worker published under the same mutex.
        if(!mutex_)return {};
        SDL_LockMutex(mutex_);auto result=std::move(result_);SDL_UnlockMutex(mutex_);return result;
    }
private:
    struct Interrupted {};
    static int Run(void *self){return static_cast<ScanWorker *>(self)->Work();}
    int Work(){
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        Status final;
        try {
            Uint32 last=0;std::string stage;
            auto result=scan_([&](const ScanProgress &progress){
                if(stop_)throw Interrupted{};
                const auto now=SDL_GetTicks();
                if(progress.stage!=stage || now-last>=50 || (progress.total && progress.completed==progress.total)){
                    SDL_LockMutex(mutex_);status_.progress=progress;SDL_UnlockMutex(mutex_);
                    stage=progress.stage;last=now;
                }
            });
            if(stop_)throw Interrupted{};
            SDL_LockMutex(mutex_);status_.progress={"SAVING LIBRARY","",result.games.size(),0,0};SDL_UnlockMutex(mutex_);
            // Commit is not interrupted halfway; SaveLibraryCache publishes a complete temporary file.
            final.cacheSaved=save_(result.games);
            final.progress={"SCAN COMPLETE","",result.games.size(),1,1};
            SDL_LockMutex(mutex_);result_=std::move(result);SDL_UnlockMutex(mutex_);
        }catch(const Interrupted &){final.error="SCAN INTERRUPTED - LIBRARY UNCHANGED";}
        catch(...){final.error="SCAN FAILED - LIBRARY UNCHANGED";}
        final.finished=true;
        SDL_LockMutex(mutex_);status_=std::move(final);SDL_UnlockMutex(mutex_);
        return 0;
    }
    Scan scan_;Save save_;
    SDL_mutex *mutex_=nullptr;
    SDL_Thread *thread_=nullptr;
    std::atomic<bool> stop_{false};
    Status status_;
    ScanResult result_;
};
