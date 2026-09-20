#pragma once
#include "video.hpp"
#include <cstdlib>

// A silent theme loop, separate from idle-triggered game previews. The caller
// hands the decoder over only AFTER the other player finishes cancellation.
class GlassBackground {
public:
    explicit GlassBackground(SDL_Renderer *renderer) : player_(renderer) {}
    void Stop() { player_.Stop(); }
    bool Released() { return player_.CanStart(); }
    bool Active() const { return player_.Active(); }
    bool Ready() const { return player_.Ready(); }
    bool Draw(int width,int height) const { return player_.Draw(width,height); }
    VideoPlayer::Stats Statistics() const { return player_.Statistics(); }
    void Update(bool enabled,bool paused,bool previewRequested,bool previewRetired) {
        if(!enabled||previewRequested) { Stop(); Released(); return; }
        if(!previewRetired) return;
        const bool available=player_.Active()||Released();
        if(!player_.Active()&&!failed_&&available) {
#ifdef UFLOW_HOST_PREVIEW
            const char *path=std::getenv("UFLOW_GLASS_BACKGROUND_VIDEO");
#else
            const char *path="fs:/vol/content/skins/glass/background.mp4";
#endif
            if(!path||!*path)return; // deterministic host fixtures opt in
            if(!player_.Start(path,0))failed_=true;
        }
        if(!player_.Active())return;
        // Motion OFF still decodes one frame for a static theme background.
        player_.Suspend(paused&&player_.Ready());
        player_.Update();
        if(const auto error=player_.TakeError();!error.empty()) {
            SDL_Log("Glass background disabled: %s",error.c_str());
            failed_=true;Stop(); // retain the procedural fallback; never retry-spam
        }
    }
private:
    VideoPlayer player_;
    bool failed_=false;
};
