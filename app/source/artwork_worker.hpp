#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

// CPU-only decoding. Textures and all renderer calls stay on the UI thread.
// At most one completed surface and one in-flight decode are retained.
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
        if (ready_.surface) SDL_FreeSurface(ready_.surface);
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
            if (path != active_ && (!ready_.available || path != ready_.path) &&
                std::find(queue_.begin(), queue_.end(), path) == queue_.end()) queue_.push_back(path);
        if (ready_.available && std::find(wanted_.begin(), wanted_.end(), ready_.path) == wanted_.end()) {
            if (ready_.surface) SDL_FreeSurface(ready_.surface);
            ready_ = {};
        }
        SDL_CondSignal(condition_); SDL_UnlockMutex(mutex_);
    }
    Result Take() {
        if (!thread_) return {};
        SDL_LockMutex(mutex_);
        Result result = std::move(ready_); ready_ = {};
        SDL_CondSignal(condition_); SDL_UnlockMutex(mutex_);
        return result;
    }
    void Reset() {
        if (!thread_) return;
        SDL_LockMutex(mutex_); ++generation_; wanted_.clear(); queue_.clear();
        if (ready_.surface) SDL_FreeSurface(ready_.surface);
        ready_ = {}; SDL_UnlockMutex(mutex_);
    }
    static constexpr std::size_t Capacity = 24;
private:
    static int Run(void *opaque) { return static_cast<ArtworkWorker *>(opaque)->Work(); }
    int Work() {
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
        SDL_LockMutex(mutex_);
        while (!stop_) {
            while (!stop_ && (queue_.empty() || ready_.available)) SDL_CondWait(condition_, mutex_);
            if (stop_) break;
            active_ = std::move(queue_.front()); queue_.erase(queue_.begin());
            const std::string path = active_;
            const unsigned generation = generation_;
            SDL_UnlockMutex(mutex_);
            SDL_Surface *surface = decode_(path);
            SDL_LockMutex(mutex_);
            active_.clear();
            if (!stop_ && generation == generation_ &&
                std::find(wanted_.begin(), wanted_.end(), path) != wanted_.end()) ready_ = {path, surface, true};
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
    Result ready_;
};
