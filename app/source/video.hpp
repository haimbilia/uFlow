#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <cmath>

// Authenticated live-check A/B control; no persisted setting or system-thread changes.
void VideoDecoderWakePriority(bool enabled);

// Fixed source-frame cadence keeps selection identical before and after
// decoding. Reference frames outside it must still be decoded, but need not
// be converted/uploaded. Invalid timestamps/rates never authorize skipping.
inline bool VideoPreviewFrameWanted(double seconds, double fps) {
    if (!std::isfinite(seconds) || !std::isfinite(fps) || fps<=30 || fps>240 || seconds<0)
        return true;
    const int step=static_cast<int>(std::ceil(fps/30.0));
    const double frame=std::floor(seconds*fps+.5);
    return std::fmod(frame,step)<.5;
}

inline bool WiiUVideoH264Supported(int profile, int level) {
    return (profile == 66 || profile == 77 || profile == 100) && level > 0 && level <= 42;
}

// Pure timer state: testable without SDL, storage or a decoder. Dismissal lasts
// until focus changes; opening a menu always requires a fresh idle interval.
class VideoTrigger {
public:
    void Focus(const std::string &key) {
        if (key != key_) { key_ = key; armed_ = fired_ = dismissed_ = false; }
    }
    bool Tick(bool eligible, std::uint32_t now, int delayMs) {
        if (!eligible || key_.empty()) { armed_ = fired_ = false; return false; }
        if (!armed_) { since_ = now; armed_ = true; }
        if (fired_ || dismissed_ || now - since_ < static_cast<std::uint32_t>(delayMs)) return false;
        fired_ = true;
        return true;
    }
    void Dismiss() { dismissed_ = true; }
    void Reset() { armed_ = fired_ = false; }
private:
    std::string key_;
    std::uint32_t since_ = 0;
    bool armed_ = false, fired_ = false, dismissed_ = false;
};

// Reading, decoding and color conversion use separate cancellable workers. Only the UI
// creates/updates textures. Owned-frame queues are bounded; no whole-file loads.
class VideoPlayer {
public:
    struct Stats {
        std::uint32_t decoded, presented, audioFrames, loops;
        std::uint32_t converted, lateDropped, readUs, decodeUs, convertUs, uploadUs;
        bool hardware;
        int outputWidth, outputHeight;
        std::uint32_t maxPresentGapMs, longPresentGaps, audioMissingFrames;
        int videoLagMs;
        std::uint32_t handoffUs, decoderCores, converterCores;
        std::uint32_t reorderedFrames, timestampCorrections;
        std::uint32_t nativeDecodeUs, skippedNonreference;
        std::uint32_t packetQueuePeak;
        bool gpuColour;
        bool nativePlanes;
    };
    explicit VideoPlayer(SDL_Renderer *renderer);
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;
    bool Start(const std::string &path, int volume, bool loop = true);
    bool CanStart(); // Reap completed cancellation without blocking.
    void Stop(bool retainFrame = false); // Optional last GPU frame for a UI fade; playback stops immediately.
    void Suspend(bool suspended); // atomics only; safe on foreground release
    bool Update();
    bool Draw(int width, int height, Uint8 opacity = 255) const;
    bool Active() const;
    bool Ready() const;
    bool Finished() const; // Natural end, after the final picture and audio drain.
    std::string TakeError();
    Stats Statistics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
