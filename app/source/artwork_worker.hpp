#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// CPU-only decoding. Textures and all renderer calls stay on the UI thread.
// Small batches avoid waiting for a UI frame between each icon decode.
// Completed surfaces are bounded by count and bytes, plus one in-flight decode.
class ArtworkWorker {
public:
    using Decode = SDL_Surface *(*)(const std::string &);
    struct Result { std::string path; SDL_Surface *surface = nullptr; bool available = false; };
    explicit ArtworkWorker(Decode decode) : decode_(decode) {
        mutex_ = SDL_CreateMutex(); condition_ = SDL_CreateCond();
        if (mutex_ && condition_)
            thread_ = SDL_CreateThreadWithStackSize(Run, "uflow-artwork", 256 * 1024, this);
    }
    ~ArtworkWorker() {
        if (thread_) {
            SDL_LockMutex(mutex_); stop_ = true; SDL_CondSignal(condition_); SDL_UnlockMutex(mutex_);
            SDL_WaitThread(thread_, nullptr);
        }
        ClearReady();
        if (condition_) SDL_DestroyCond(condition_);
        if (mutex_) SDL_DestroyMutex(mutex_);
    }
    ArtworkWorker(const ArtworkWorker &) = delete;
    ArtworkWorker &operator=(const ArtworkWorker &) = delete;
    bool Available() const { return thread_ != nullptr; }
    void Request(const std::vector<std::string> &paths) {
        if (!thread_) return;
        SDL_LockMutex(mutex_);
        wanted_.assign(paths.begin(), paths.begin() + std::min(paths.size(), Capacity));
        queue_.clear();
        for (const auto &path : wanted_)
            if (path != active_ && std::none_of(ready_.begin(),ready_.end(),[&](const auto &r){return r.path==path;}) &&
                std::find(queue_.begin(), queue_.end(), path) == queue_.end()) queue_.push_back(path);
        for(auto it=ready_.begin();it!=ready_.end();) {
            if(std::find(wanted_.begin(),wanted_.end(),it->path)!=wanted_.end()) { ++it; continue; }
            readyBytes_-=Bytes(it->surface);
            if(it->surface) SDL_FreeSurface(it->surface);
            it=ready_.erase(it);
        }
        SDL_CondSignal(condition_); SDL_UnlockMutex(mutex_);
    }
    Result Take() {
        if (!thread_) return {};
        SDL_LockMutex(mutex_);
        Result result;
        if(!ready_.empty()) {
            // Selection can change while decoding: upload in the latest requested order.
            auto next=std::min_element(ready_.begin(),ready_.end(),[&](const auto &a,const auto &b){
                return std::find(wanted_.begin(),wanted_.end(),a.path)<std::find(wanted_.begin(),wanted_.end(),b.path);
            });
            result=std::move(*next); ready_.erase(next);
            readyBytes_-=Bytes(result.surface);
        }
        SDL_CondSignal(condition_); SDL_UnlockMutex(mutex_);
        return result;
    }
    bool Idle() {
        if(!thread_)return true;
        SDL_LockMutex(mutex_);
        const bool idle=active_.empty()&&queue_.empty();
        SDL_UnlockMutex(mutex_);return idle;
    }
    void Reset() {
        if (!thread_) return;
        SDL_LockMutex(mutex_); ++generation_; wanted_.clear(); queue_.clear();
        ClearReady(); SDL_UnlockMutex(mutex_);
    }
    static constexpr std::size_t Capacity = 96;
    static constexpr std::size_t ReadyCapacity = 4;
    static constexpr std::size_t ReadyBytes = 8 * 1024 * 1024;
private:
    static std::size_t Bytes(SDL_Surface *s) { return s ? std::size_t(s->pitch)*s->h : 0; }
    void ClearReady() {
        for(auto &r:ready_) if(r.surface) SDL_FreeSurface(r.surface);
        ready_.clear(); readyBytes_=0;
    }
    static int Run(void *opaque) { return static_cast<ArtworkWorker *>(opaque)->Work(); }
    int Work() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        SDL_LockMutex(mutex_);
        while (!stop_) {
            while (!stop_ && (queue_.empty() || ready_.size()>=ReadyCapacity || readyBytes_>=ReadyBytes)) SDL_CondWait(condition_, mutex_);
            if (stop_) break;
            active_ = std::move(queue_.front()); queue_.erase(queue_.begin());
            const std::string path = active_;
            const unsigned generation = generation_;
            SDL_UnlockMutex(mutex_);
            SDL_Surface *surface = decode_(path);
            SDL_LockMutex(mutex_);
            active_.clear();
            if (!stop_ && generation == generation_ &&
                std::find(wanted_.begin(), wanted_.end(), path) != wanted_.end()) {
                ready_.push_back({path,surface,true}); readyBytes_+=Bytes(surface);
            }
            else if (surface) SDL_FreeSurface(surface);
        }
        SDL_UnlockMutex(mutex_);
        return 0;
    }
    Decode decode_;
    SDL_mutex *mutex_ = nullptr;
    SDL_cond *condition_ = nullptr;
    SDL_Thread *thread_ = nullptr;
    bool stop_ = false;
    unsigned generation_ = 0;
    std::string active_;
    std::vector<std::string> wanted_, queue_;
    std::vector<Result> ready_;
    std::size_t readyBytes_=0;
};
