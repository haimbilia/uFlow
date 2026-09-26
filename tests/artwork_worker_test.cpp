#include "artwork_worker.hpp"
#include <atomic>
#include <cassert>
#include <cstdio>

static SDL_sem *entered, *releaseDecode;
static SDL_threadID mainThread;
static std::atomic<unsigned> calls;
static SDL_Surface *Decode(const std::string &path) {
    assert(SDL_ThreadID() != mainThread);
    ++calls; SDL_SemPost(entered);
    assert(SDL_SemWaitTimeout(releaseDecode, 3000) == 0);
    if (path == "bad") return nullptr;
    if (path == "large") return SDL_CreateRGBSurfaceWithFormat(0,2048,1024,32,SDL_PIXELFORMAT_RGBA32);
    return SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
}
static ArtworkWorker::Result Wait(ArtworkWorker &worker) {
    const auto start = SDL_GetTicks();
    while (SDL_GetTicks() - start < 3000) {
        auto result = worker.Take();
        if (result.available) return result;
        SDL_Delay(1);
    }
    assert(false && "Worker did not deliver result"); return {};
}
int main() {
    assert(SDL_Init(0) == 0); mainThread = SDL_ThreadID();
    entered = SDL_CreateSemaphore(0); releaseDecode = SDL_CreateSemaphore(0);
    {
        ArtworkWorker worker(Decode); assert(worker.Available());
        worker.Request({"obsolete", "older"});
        assert(SDL_SemWaitTimeout(entered, 3000) == 0);
        // The decoder is blocked: Request/Take must not block the UI with it.
        worker.Request({"visible", "visible"}); assert(!worker.Take().available);
        SDL_SemPost(releaseDecode);
        assert(SDL_SemWaitTimeout(entered, 3000) == 0);
        assert(calls == 2); // obsolete queued work was replaced, duplicates removed
        SDL_SemPost(releaseDecode);
        auto result = Wait(worker);
        assert(result.path == "visible" && result.surface); SDL_FreeSurface(result.surface);
        worker.Request({"before-reset"});
        assert(SDL_SemWaitTimeout(entered, 3000) == 0);
        worker.Reset(); worker.Request({"after-reset"}); SDL_SemPost(releaseDecode);
        assert(SDL_SemWaitTimeout(entered, 3000) == 0); SDL_SemPost(releaseDecode);
        result = Wait(worker);
        assert(result.path == "after-reset" && result.surface); SDL_FreeSurface(result.surface);
        worker.Request({"bad"});
        assert(SDL_SemWaitTimeout(entered, 3000) == 0); SDL_SemPost(releaseDecode);
        result = Wait(worker); assert(result.path == "bad" && !result.surface);
        worker.Request({"one", "two", "three", "four", "five"});
        // Four icons decode without a single UI Take/frame in between.
        for(unsigned i=0;i<ArtworkWorker::ReadyCapacity;++i) {
            assert(SDL_SemWaitTimeout(entered,3000)==0); SDL_SemPost(releaseDecode);
        }
        assert(SDL_SemWaitTimeout(entered,50)==SDL_MUTEX_TIMEDOUT);
        worker.Reset(); worker.Request({"large","after-large"});
        assert(SDL_SemWaitTimeout(entered,3000)==0); SDL_SemPost(releaseDecode);
        assert(SDL_SemWaitTimeout(entered,100)==SDL_MUTEX_TIMEDOUT); // byte budget stops batch early
        result=Wait(worker); assert(result.path=="large" && result.surface); SDL_FreeSurface(result.surface);
        assert(SDL_SemWaitTimeout(entered,3000)==0); SDL_SemPost(releaseDecode);
        result=Wait(worker); assert(result.path=="after-large"); SDL_FreeSurface(result.surface);
        worker.Reset(); worker.Request({"shutdown"});
        assert(SDL_SemWaitTimeout(entered, 3000) == 0);
        SDL_SemPost(releaseDecode); // destructor must join and discard/free the result
    }
    SDL_DestroySemaphore(releaseDecode); SDL_DestroySemaphore(entered); SDL_Quit();
    puts("PASS: nonblocking requests, decode-thread isolation, current-view priority, deduplication, reset generation, failure, bounded ready queue, joined shutdown");
}
