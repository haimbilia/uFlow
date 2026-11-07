#pragma once
#include "settings.hpp"
#include <SDL2/SDL.h>
#include <optional>
#include <utility>

// One writer, one replaceable pending snapshot. Never hold the queue lock over I/O.
class SettingsWriter {
public:
    using Write=bool (*)(const UserSettings &);
    struct Result { unsigned revision=0; bool ok=true; Uint64 microseconds=0; };
    explicit SettingsWriter(Write write=SaveSettingsChecked):write_(write) {
        mutex_=SDL_CreateMutex();condition_=SDL_CreateCond();
        if(mutex_&&condition_)thread_=SDL_CreateThreadWithStackSize(Run,"uflow-settings",128*1024,this);
    }
    ~SettingsWriter(){
        if(thread_){SDL_LockMutex(mutex_);stop_=true;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);SDL_WaitThread(thread_,nullptr);}
        if(condition_)SDL_DestroyCond(condition_);if(mutex_)SDL_DestroyMutex(mutex_);
    }
    SettingsWriter(const SettingsWriter &)=delete;
    SettingsWriter &operator=(const SettingsWriter &)=delete;
    bool Submit(const UserSettings &settings){
        if(!thread_)return false;
        SDL_LockMutex(mutex_);pending_=settings;++submitted_;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);return true;
    }
    Result Poll(){if(!thread_)return {};SDL_LockMutex(mutex_);auto r=result_;SDL_UnlockMutex(mutex_);return r;}
    bool Idle(){if(!thread_)return true;SDL_LockMutex(mutex_);bool idle=!pending_&&!busy_;SDL_UnlockMutex(mutex_);return idle;}
    void Drain(){if(!thread_)return;SDL_LockMutex(mutex_);while(pending_||busy_)SDL_CondWait(condition_,mutex_);SDL_UnlockMutex(mutex_);}
private:
    static int Run(void *p){
        auto &self=*static_cast<SettingsWriter *>(p);SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        SDL_LockMutex(self.mutex_);
        for(;;){
            while(!self.pending_&&!self.stop_)SDL_CondWait(self.condition_,self.mutex_);
            if(!self.pending_)break;
            auto settings=std::move(*self.pending_);self.pending_.reset();const auto revision=self.submitted_;self.busy_=true;
            SDL_UnlockMutex(self.mutex_);
            const auto start=SDL_GetPerformanceCounter();const bool ok=self.write_(settings);
            const auto elapsed=(SDL_GetPerformanceCounter()-start)*1000000/SDL_GetPerformanceFrequency();
            SDL_LockMutex(self.mutex_);self.result_={revision,ok,elapsed};self.busy_=false;SDL_CondBroadcast(self.condition_);
        }
        SDL_UnlockMutex(self.mutex_);return 0;
    }
    Write write_;SDL_mutex *mutex_=nullptr;SDL_cond *condition_=nullptr;SDL_Thread *thread_=nullptr;
    std::optional<UserSettings> pending_;Result result_;unsigned submitted_=0;bool busy_=false,stop_=false;
};
