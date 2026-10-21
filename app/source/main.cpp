#include "library.hpp"
#include <ctime>
#include "library_sort.hpp"
#include "browser_path.hpp"
#ifndef UFLOW_HOST_PREVIEW
#include "usb_storage.hpp"
#include <nn/act.h>
#include <nn/ac.h>
#include <nn/pdm/pdm_cpp.h>
#include <coreinit/launch.h>
#endif
#include "vc_launch.hpp"
#include "injection_dialog.hpp"
#include "preparation_progress.hpp"
#include "update_model.hpp"
#ifndef UFLOW_HOST_PREVIEW
#include "updater.hpp"
#endif
#include "launchers.hpp"
#include "launcher_overrides.hpp"
#include <dirent.h>
#include "raw_launch.hpp"
#include "wii_launch.hpp"
#include "wii_vc.hpp"
#include "return_to_uflow.hpp"
#include "settings.hpp"
#include "binding_capture.hpp"
#include "touch_swipe.hpp"
#include "case_model.hpp"
#include "case_sleeve.hpp"
#include "case_profiles.hpp"
#include "cover_faces.hpp"
#include "cover_projection.hpp"
#include "glass_ttf.h"
#include "switch_ttf.h"
#include "skin.hpp"
#include "user_statistics.hpp"
#include "visual_check.hpp"
#include "thread_diagnostics.hpp"
#include "glass_material.hpp"
#include "cover_transition.hpp"
#include "browse_motion.hpp"
#include "artwork_worker.hpp"
#include "cover_texture_cache.hpp"
#include "cover_display_cache.hpp"
#include "browser_worker.hpp"
#include "scan_worker.hpp"
#include "music.hpp"
#include "menu_sounds.hpp"
#include "video.hpp"
#include "glass_background.hpp"
#include "media_groups.hpp"
#include "library_tabs.hpp"
#include "text_utf8.hpp"
#include "retroarch_args.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/memory.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include <gx2/event.h>
#include <png.h>
#include <rpxloader/rpxloader.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include "wii_remote.hpp"
#include <whb/sdcard.h>
#endif
#include <png.h>
#include <turbojpeg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef UFLOW_HOST_PREVIEW
#include <filesystem>
#endif

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr int kMaxArtworkDimension = 2048;
constexpr std::size_t kMaxArtworkPixels = 4 * 1024 * 1024;
constexpr const char *kTracePath = "fs:/vol/external01/wiiu/apps/uFlow/uflow.log";
constexpr const char *kAllLibraryId = "__all";
constexpr const char *kFavoritesLibraryId = "__favorites";

// How a homebrew launcher receives the selected game.
struct HomebrewLaunchPlan {
    enum class Transport { None, Browser, RomRequest, RetroArchArgs };
    Transport transport = Transport::None;
    std::string executable;      // SD-relative .rpx/.wuhb to start
    std::string contentBundle;   // RetroArch only: .wuhb providing /vol/content
    std::string requestFile;     // rom-request-v1 only
    std::vector<char> arguments; // RetroArch only: two NUL-terminated strings
};

// Launch Options (Minus by default) chooses how a title starts, this time only:
// any compatible launcher, or Nintendo VC / injection for authored Wii VC titles.
enum class LaunchMethod { Default, NintendoVC, Injection, Launcher };
struct LaunchOption { std::string title, note; LaunchMethod method = LaunchMethod::Default; std::string launcher; bool available = true; };

#ifndef UFLOW_HOST_PREVIEW
using LaunchLooseFn = RPXLoaderStatus (*)(const char *, const char *, const char *, const char *,
                                          const char *, const char *, const char *);
#endif

struct Color { Uint8 r, g, b, a; };
struct TextureInfo { SDL_Texture *texture = nullptr; int width = 0; int height = 0; };
struct CachedTexture { TextureInfo info; Uint32 lastUse = 0; std::size_t bytes = 0; };

struct SmoothTextEntry { SDL_Texture *texture = nullptr; int width = 0; int height = 0; Uint32 lastUse = 0; };
std::array<TTF_Font *, 6> gSmoothFonts{},gAlternateFonts{};
bool gSwitchFont=false;
void SetSwitchFont(bool enabled){if(enabled!=gSwitchFont){gSmoothFonts.swap(gAlternateFonts);gSwitchFont=enabled;}}
std::unordered_map<std::string, SmoothTextEntry> gSmoothTextCache;
bool gSmoothText = false;
bool gPopupTextGlow = false;
struct PopupTextGlowScope {
    bool previous=gPopupTextGlow;
    explicit PopupTextGlowScope(bool enabled){gPopupTextGlow=enabled;}
    ~PopupTextGlowScope(){gPopupTextGlow=previous;}
};
GlassMaterial gGlassMaterial;
SDL_Texture *gGlassBackdrop = nullptr; // never aliases the active render target
bool gGlassOverVideo = false;
bool gGlassTextHalo = false;
SDL_Texture *gEllipseShadow = nullptr;
SDL_Texture *gStockMenuSymbols = nullptr;

constexpr int kSmoothFontSizes[] = {0, 17, 20, 25, 33, 42};
// glass.ttf is Latin-only. Japanese, Chinese and Korean titles fall back to
// the console's own system fonts, read in place from shared memory (never
// copied or shipped). Each face opens on first use.
struct FallbackFont { const void *data = nullptr; int size = 0; std::array<TTF_Font *, 6> faces{}; };
std::array<FallbackFont, 4> gFallbackFonts{};

void InitFallbackFonts() {
#ifndef UFLOW_HOST_PREVIEW
    // Standard covers Japanese; the others add Chinese and Korean glyphs.
    const OSSharedDataType order[] = {OS_SHAREDDATATYPE_FONT_STANDARD, OS_SHAREDDATATYPE_FONT_TAIWANESE,
                                      OS_SHAREDDATATYPE_FONT_CHINESE, OS_SHAREDDATATYPE_FONT_KOREAN};
    for (std::size_t i = 0; i < gFallbackFonts.size(); ++i) {
        void *data = nullptr;
        uint32_t size = 0;
        if (OSGetSharedData(order[i], 0, &data, &size) && data && size && size < 0x7FFFFFFF)
            gFallbackFonts[i] = {data, static_cast<int>(size), {}};
    }
#else
    // Host preview: optional CJK font file for visual checks.
    static std::vector<unsigned char> preview;
    if (const char *path = std::getenv("UFLOW_PREVIEW_CJK_FONT")) {
        if (FILE *file = std::fopen(path, "rb")) {
            std::fseek(file, 0, SEEK_END);
            preview.resize(static_cast<std::size_t>(std::max(0L, std::ftell(file))));
            std::fseek(file, 0, SEEK_SET);
            preview.resize(std::fread(preview.data(), 1, preview.size(), file));
            std::fclose(file);
            if (!preview.empty()) gFallbackFonts[0] = {preview.data(), static_cast<int>(preview.size()), {}};
        }
    }
#endif
}

TTF_Font *FallbackFace(FallbackFont &fallback, int index) {
    if (!fallback.data) return nullptr;
    if (!fallback.faces[index]) {
        SDL_RWops *stream = SDL_RWFromConstMem(fallback.data, fallback.size);
        fallback.faces[index] = stream ? TTF_OpenFontRW(stream, 1, kSmoothFontSizes[index]) : nullptr;
        if (!fallback.faces[index]) fallback.data = nullptr; // unusable: do not retry every frame
    }
    return fallback.faces[index];
}

bool InitSmoothFonts() {
    if (TTF_Init() != 0) return false;
    const int fontSize = static_cast<int>(glass_ttf_size);
    for (int index = 1; index <= 5; ++index) {
        SDL_RWops *stream = SDL_RWFromConstMem(glass_ttf, fontSize);
        gSmoothFonts[index] = stream ? TTF_OpenFontRW(stream, 1, kSmoothFontSizes[index]) : nullptr;
        SDL_RWops *plain = SDL_RWFromConstMem(switch_ttf,static_cast<int>(switch_ttf_size));
        gAlternateFonts[index]=plain?TTF_OpenFontRW(plain,1,kSmoothFontSizes[index]):nullptr;
        if (gSmoothFonts[index] && index <= 2) TTF_SetFontStyle(gSmoothFonts[index], TTF_STYLE_BOLD);
    }
    InitFallbackFonts();
    return gSmoothFonts[1] != nullptr;
}

// The UI font, or the first system font that has this character.
TTF_Font *FontForCodepoint(std::uint32_t cp, int index) {
    TTF_Font *primary = gSmoothFonts[index];
    if (cp < 0x80 || TTF_GlyphIsProvided32(primary, cp)) return primary;
    for (auto &fallback : gFallbackFonts)
        if (TTF_Font *face = FallbackFace(fallback, index); face && TTF_GlyphIsProvided32(face, cp)) return face;
    return primary;
}

// Consecutive characters that render with the same font. Control characters
// (meta.xml line breaks) become spaces.
std::vector<std::pair<TTF_Font *, std::string>> TextRuns(const std::string &text, int index) {
    std::vector<std::pair<TTF_Font *, std::string>> runs;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        std::uint32_t cp = Utf8::Next(text, i);
        std::string piece = cp < 32 ? std::string(" ") : text.substr(start, i - start);
        if (cp < 32) cp = ' ';
        TTF_Font *font = FontForCodepoint(cp, index);
        if (runs.empty() || runs.back().first != font) runs.push_back({font, std::move(piece)});
        else runs.back().second += piece;
    }
    return runs;
}

int RunsWidth(const std::string &text, int index) {
    int width = 0;
    for (const auto &[font, run] : TextRuns(text, index)) {
        int part = 0;
        TTF_SizeUTF8(font, run.c_str(), &part, nullptr);
        width += part;
    }
    return width;
}

// One surface for mixed-font text, glyphs aligned on a shared baseline.
SDL_Surface *RenderTextSurface(const std::string &text, int index) {
    const SDL_Color white{255, 255, 255, 255};
    if (!Utf8::HasNonAscii(text)) return TTF_RenderUTF8_Blended(gSmoothFonts[index], text.c_str(), white);
    const auto runs = TextRuns(text, index);
    if (runs.size() == 1) return TTF_RenderUTF8_Blended(runs[0].first, runs[0].second.c_str(), white);
    std::vector<SDL_Surface *> parts;
    int width = 0, ascent = 0, descent = 0;
    for (const auto &[font, run] : runs) {
        SDL_Surface *part = TTF_RenderUTF8_Blended(font, run.c_str(), white);
        parts.push_back(part);
        if (!part) continue;
        width += part->w;
        ascent = std::max(ascent, TTF_FontAscent(font));
        descent = std::max(descent, part->h - TTF_FontAscent(font));
    }
    SDL_Surface *out = width > 0 && ascent + descent > 0 ?
        SDL_CreateRGBSurfaceWithFormat(0, width, ascent + descent, 32, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    if (out) SDL_FillRect(out, nullptr, 0);
    int x = 0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!parts[i]) continue;
        if (out) {
            SDL_SetSurfaceBlendMode(parts[i], SDL_BLENDMODE_NONE); // runs never overlap: copy alpha as-is
            SDL_Rect at{x, ascent - TTF_FontAscent(runs[i].first), parts[i]->w, parts[i]->h};
            SDL_BlitSurface(parts[i], nullptr, out, &at);
        }
        x += parts[i]->w;
        SDL_FreeSurface(parts[i]);
    }
    return out;
}

// The pixel skin's bitmap font is ASCII-only; other text uses the smooth fonts
// scaled to roughly the pixel glyph height.
bool UseSmoothText(const std::string &text, int scale) {
    return scale >= 1 && scale <= 5 && gSmoothFonts[scale] && (gSmoothText || scale == 1 || Utf8::HasNonAscii(text));
}
float SmoothTextScale(int scale) {
    return (gSmoothText || scale == 1) ? 1.0f : 9.0f * scale / std::max(1, TTF_FontHeight(gSmoothFonts[scale]));
}

void ShutdownSmoothFonts() {
    gGlassMaterial.Clear();
    if (gStockMenuSymbols) SDL_DestroyTexture(gStockMenuSymbols);
    if (gEllipseShadow) SDL_DestroyTexture(gEllipseShadow);
    gStockMenuSymbols = gEllipseShadow = nullptr;
    for (auto &[unused, entry] : gSmoothTextCache) if (entry.texture) SDL_DestroyTexture(entry.texture);
    gSmoothTextCache.clear();
    for (TTF_Font *&font : gAlternateFonts) { if (font) TTF_CloseFont(font); font = nullptr; }
    for (TTF_Font *&font : gSmoothFonts) { if (font) TTF_CloseFont(font); font = nullptr; }
    for (auto &fallback : gFallbackFonts) {
        for (TTF_Font *&face : fallback.faces) { if (face) TTF_CloseFont(face); face = nullptr; }
        fallback = {};
    }
    TTF_Quit();
}

bool gPersistTrace = true;
void Trace(const char *event, bool persist = false) {
#ifndef UFLOW_HOST_PREVIEW
    OSReport("uFlow: %s\n", event);
    // Once browsing starts, diagnostic SD I/O must not block the UI on the
    // same allocator/filesystem being diagnosed. Keep startup and clean-exit
    // checkpoints; live events remain available through the system log.
    if(!gPersistTrace && !persist)return;
    FILE *file = std::fopen(kTracePath, "ab");
    if (!file) return;
    std::fprintf(file, "ticks=%u event=%s\n", SDL_GetTicks(), event);
    std::fclose(file);
#else
    (void)event;
    (void)persist;
#endif
}

void SetColor(SDL_Renderer *renderer, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

Color Mix(Color a, Color b, float amount) {
    const auto channel = [amount](Uint8 left, Uint8 right) {
        return static_cast<Uint8>(left + (right - left) * amount);
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

Color AccentFor(Platform platform, int theme = 0) {
    if(theme>0 && theme<AccentThemes::Count) {
        const auto &color=AccentThemes::Presets[theme];
        return {color.r,color.g,color.b,255};
    }
    switch (platform) {
        case Platform::WiiU: return {0, 190, 236, 255};
        case Platform::Wii: return {80, 197, 236, 255};
        case Platform::GameCube: return {132, 102, 238, 255};
        case Platform::Files: return {45,174,222,255};
    }
    return {0, 190, 236, 255};
}

Color AccentInk(Color accent) { return Mix({20,34,43,255},accent,.38f); }

const std::array<SDL_FPoint, 36> &RoundedRectArcPoints() {
    static const std::array<SDL_FPoint, 36> points = [] {
        std::array<SDL_FPoint, 36> result{};
        constexpr float kPi = 3.14159265358979323846f;
        for (int corner = 0; corner < 4; ++corner) for (int step = 0; step <= 8; ++step) {
            const float angle = -kPi * .5f + corner * kPi * .5f + step * (kPi * .5f / 8.0f);
            result[corner * 9 + step] = {std::cos(angle), std::sin(angle)};
        }
        return result;
    }();
    return points;
}

const std::array<SDL_FPoint, 37> &EllipseArcPoints() {
    static const std::array<SDL_FPoint, 37> points = [] {
        std::array<SDL_FPoint, 37> result{};
        constexpr float kPi = 3.14159265358979323846f;
        for (int segment = 0; segment <= 36; ++segment) {
            const float angle = segment * (2.0f * kPi / 36.0f);
            result[segment] = {std::cos(angle), std::sin(angle)};
        }
        return result;
    }();
    return points;
}

void FillRoundedRect(SDL_Renderer *renderer, SDL_Rect rect, int radius, Color color) {
    if (rect.w <= 0 || rect.h <= 0) return;
    radius = std::min(radius, std::min(rect.w, rect.h) / 2);
    if (radius <= 1) {
        SetColor(renderer, color);
        SDL_RenderFillRect(renderer, &rect);
        return;
    }

    constexpr int kArcSteps = 8;
    constexpr int kPerimeterPoints = 4 * (kArcSteps + 1);
    std::array<SDL_Vertex, kPerimeterPoints + 1> vertices{};
    std::array<int, kPerimeterPoints * 3> indices{};
    vertices[0].position = {rect.x + rect.w * .5f, rect.y + rect.h * .5f};
    vertices[0].color = {color.r, color.g, color.b, color.a};
    int vertex = 1;
    const auto &arc = RoundedRectArcPoints();
    for (int corner = 0; corner < 4; ++corner) {
        const float centerX = corner == 0 || corner == 1 ? rect.x + rect.w - radius : rect.x + radius;
        const float centerY = corner == 0 || corner == 3 ? rect.y + radius : rect.y + rect.h - radius;
        for (int step = 0; step <= kArcSteps; ++step) {
            const auto point = arc[corner * (kArcSteps + 1) + step];
            vertices[vertex].position = {centerX + point.x * radius, centerY + point.y * radius};
            vertices[vertex].color = {color.r, color.g, color.b, color.a};
            ++vertex;
        }
    }
    for (int point = 0; point < kPerimeterPoints; ++point) {
        indices[point * 3] = 0;
        indices[point * 3 + 1] = point + 1;
        indices[point * 3 + 2] = point + 1 == kPerimeterPoints ? 1 : point + 2;
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void FillVerticalGradient(SDL_Renderer *renderer, SDL_Rect rect, Color top, Color bottom,
                          int = 6) {
    SDL_Vertex vertices[4]{};
    vertices[0].position = {static_cast<float>(rect.x), static_cast<float>(rect.y)};
    vertices[1].position = {static_cast<float>(rect.x + rect.w), static_cast<float>(rect.y)};
    vertices[2].position = {static_cast<float>(rect.x + rect.w), static_cast<float>(rect.y + rect.h)};
    vertices[3].position = {static_cast<float>(rect.x), static_cast<float>(rect.y + rect.h)};
    vertices[0].color = vertices[1].color = {top.r, top.g, top.b, top.a};
    vertices[2].color = vertices[3].color = {bottom.r, bottom.g, bottom.b, bottom.a};
    const int indices[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

void FillEllipse(SDL_Renderer *renderer, SDL_FRect rect, Color color) {
    constexpr int kSegments = 36;
    const auto &arc = EllipseArcPoints();
    std::array<SDL_Vertex, kSegments + 2> vertices{};
    std::array<int, kSegments * 3> indices{};
    const float centerX = rect.x + rect.w * .5f;
    const float centerY = rect.y + rect.h * .5f;
    vertices[0].position = {centerX, centerY};
    vertices[0].color = {color.r, color.g, color.b, color.a};
    for (int segment = 0; segment <= kSegments; ++segment) {
        vertices[segment + 1].position = {centerX + arc[segment].x * rect.w * .5f,
                                          centerY + arc[segment].y * rect.h * .5f};
        vertices[segment + 1].color = {color.r, color.g, color.b, color.a};
    }
    for (int segment = 0; segment < kSegments; ++segment) {
        indices[segment * 3] = 0;
        indices[segment * 3 + 1] = segment + 1;
        indices[segment * 3 + 2] = segment + 2;
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void FillSmoothDot(SDL_Renderer *renderer,float x,float y,float radius,Color color) {
    constexpr int segments=64;
    std::array<SDL_Vertex,1+(segments+1)*2> vertices{};
    std::array<int,segments*9> indices{};
    vertices[0].position={x,y};vertices[0].color={color.r,color.g,color.b,color.a};
    for(int i=0;i<=segments;++i) {
        const float a=(i%segments)*6.2831853f/segments;
        for(int edge=0;edge<2;++edge) {
            const float r=radius-.5f+edge;
            auto &v=vertices[1+i*2+edge];
            v.position={x+std::cos(a)*r,y+std::sin(a)*r};
            v.color={color.r,color.g,color.b,Uint8(edge?0:color.a)};
        }
        if(i<segments) {
            const int v=1+i*2;
            const int triangle[]={0,v,v+2,v,v+1,v+2,v+1,v+3,v+2};
            std::copy(std::begin(triangle),std::end(triangle),indices.begin()+i*9);
        }
    }
    SDL_RenderGeometry(renderer,nullptr,vertices.data(),vertices.size(),indices.data(),indices.size());
}

void FillSmoothAnnulus(SDL_Renderer *renderer,float x,float y,float outer,float inner,Color color) {
    constexpr int steps=128;
    static const auto points=[] {std::array<SDL_FPoint,steps+1> p{};for(int i=0;i<=steps;++i){const float a=i*6.283185307f/steps;p[i]={std::cos(a),std::sin(a)};}return p;}();
    std::array<SDL_Vertex,(steps+1)*4> vertices{};std::array<int,steps*18> indices{};
    const float radii[]={inner-1.f,inner,outer,outer+1.f};
    for(int i=0;i<=steps;++i)for(int ring=0;ring<4;++ring) {
        auto &v=vertices[i*4+ring];v.position={x+points[i].x*radii[ring],y+points[i].y*radii[ring]};
        v.color={color.r,color.g,color.b,Uint8(ring==0||ring==3?0:color.a)};
        if(i<steps && ring<3){const int a=i*4+ring;const int quad[]={a,a+4,a+1,a+1,a+4,a+5};std::copy(quad,quad+6,indices.begin()+i*18+ring*6);}
    }
    SDL_RenderGeometry(renderer,nullptr,vertices.data(),vertices.size(),indices.data(),indices.size());
}
SDL_Texture *EllipseShadow(SDL_Renderer *renderer) {
    if (gEllipseShadow) return gEllipseShadow;
    constexpr int width = 128;
    constexpr int height = 64;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
        const float py = (y + .5f - height * .5f) / (height * .5f);
        for (int x = 0; x < width; ++x) {
            const float px = (x + .5f - width * .5f) / (width * .5f);
            const float distance = std::sqrt(px * px + py * py);
            const float opacity = std::pow(std::max(0.0f, 1.0f - distance), 2.2f);
            row[x] = SDL_MapRGBA(surface->format, 32, 55, 67, static_cast<Uint8>(opacity * 255.0f));
        }
    }
    gEllipseShadow = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    if (gEllipseShadow) {
        SDL_SetTextureBlendMode(gEllipseShadow, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(gEllipseShadow, SDL_ScaleModeLinear);
    }
    return gEllipseShadow;
}

void DrawSoftEllipseShadow(SDL_Renderer *renderer, SDL_FRect rect, Uint8 strength) {
    if (SDL_Texture *shadow = EllipseShadow(renderer)) {
        SDL_SetTextureAlphaMod(shadow, static_cast<Uint8>(strength * .24f));
        SDL_RenderCopyF(renderer, shadow, nullptr, &rect);
        SDL_SetTextureAlphaMod(shadow, 255);
        return;
    }
    for (int layer = 9; layer >= 0; --layer) {
        const float spreadX = layer * 4.0f;
        const float spreadY = layer * 2.0f;
        const float closeness = 1.0f - layer / 10.0f;
        const Uint8 alpha = static_cast<Uint8>(strength * (.012f + closeness * closeness * .026f));
        FillEllipse(renderer, {rect.x - spreadX, rect.y - spreadY,
                               rect.w + spreadX * 2.0f, rect.h + spreadY * 2.0f},
                    {32, 55, 67, alpha});
    }
}

void DrawSoftShadow(SDL_Renderer *renderer, SDL_Rect rect, int radius, int offsetY = 12,
                    Uint8 strength = 72) {
    for (int layer = 11; layer >= 0; --layer) {
        const int spread = layer * 2;
        const float closeness = 1.0f - layer / 12.0f;
        const Uint8 alpha = static_cast<Uint8>(strength * (.010f + closeness * closeness * .026f));
        FillRoundedRect(renderer,
                        {rect.x - spread, rect.y + offsetY - spread / 2,
                         rect.w + spread * 2, rect.h + spread},
                        radius + spread / 2, {20,31,39,alpha});
    }
}

struct CaseProfile {
    // Physical dimensions in millimetres: width, height, spine thickness.
    float widthMm = 135.0f;
    float heightMm = 190.0f;
    float thicknessMm = 14.0f;
    bool sidewaysSleeve = false;
    bool sidewaysDefaultSleeve = true;
};

// The single camera model for a case. Drawing, carousel layout and the bounds
// of a cached side case all project through it, so they cannot drift apart.
struct CaseScene { float x,y,z,pixels,screenX,screenY; };
struct CaseView {
    static constexpr float kReferenceHeightMm = 190.f; // shared, so smaller cases stay smaller
    static constexpr float kCamera = 2.45f;
    struct Point { SDL_FPoint point; float depth; };
    float caseWidth, caseHeight, caseDepth, pixelScale, centerX, centerY, camera;
    float yawSine, yawCosine, pitchSine, pitchCosine, rollSine, rollCosine;
    float sceneX=0,sceneY=0,sceneZ=0;
    CaseView(SDL_Rect rect, const CaseProfile &profile, float yaw, float pitch = 0.f, float roll = 0.f,
             float cameraDistance = kCamera, const CaseScene *scene=nullptr)
        : caseWidth(profile.widthMm / kReferenceHeightMm), caseHeight(profile.heightMm / kReferenceHeightMm),
          caseDepth(profile.thicknessMm / kReferenceHeightMm),
          pixelScale(std::min(rect.w / std::max(135.f / kReferenceHeightMm, caseWidth),
                              rect.h / (1.023f * std::max(1.f, caseHeight)))),
          centerX(rect.x + rect.w * .5f), centerY(rect.y + rect.h * .5f), camera(cameraDistance),
          yawSine(std::sin(yaw)), yawCosine(std::cos(yaw)), pitchSine(std::sin(pitch)),
          pitchCosine(std::cos(pitch)), rollSine(std::sin(roll)), rollCosine(std::cos(roll)) {
        if(scene){sceneX=scene->x;sceneY=scene->y;sceneZ=scene->z;pixelScale=scene->pixels;centerX=scene->screenX;centerY=scene->screenY;}
    }
    Point operator()(float x, float y, float z) const {
        x *= caseWidth;
        y *= caseHeight;
        z *= caseDepth;
        const float yawX = x * yawCosine + z * yawSine;
        const float yawZ = -x * yawSine + z * yawCosine;
        const float pitchY = y * pitchCosine - yawZ * pitchSine;
        const float pitchZ = y * pitchSine + yawZ * pitchCosine;
        const float rollX = yawX * rollCosine - pitchY * rollSine;
        const float rollY = yawX * rollSine + pitchY * rollCosine;
        const float perspective = camera / (camera - pitchZ - sceneZ);
        return {{centerX + (rollX+sceneX) * pixelScale * perspective,
                 centerY - (rollY+sceneY) * pixelScale * perspective}, pitchZ+sceneZ};
    }
    // Screen box of the mesh and sleeve: a box projects inside the hull of its corners.
    SDL_FRect Bounds() const {
        float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
        for (const float x : {-.502f, .5f}) for (const float y : {-.5075f, .5153f}) for (const float z : {-.515f, .515f}) {
            const SDL_FPoint p = (*this)(x, y, z).point;
            left = std::min(left, p.x); right = std::max(right, p.x);
            top = std::min(top, p.y); bottom = std::max(bottom, p.y);
        }
        return {left, top, right - left, bottom - top};
    }
    // Width of the side face turned toward the viewer: the spine for yaw > 0,
    // the opening edge otherwise.
    float SideFaceWidth() const {
        const float x = yawSine > 0.f ? -.502f : .5f;
        return std::abs((*this)(x, 0.f, .515f).point.x - (*this)(x, 0.f, -.515f).point.x);
    }
};

// Extract along the case's slot first. Only a fully clear case may roll; the
// same path in reverse aligns it before it slides between neighboring cases.
struct BookshelfPose {
    CaseScene scene;
    float yaw, pitch, roll;
    bool clear;
};
static BookshelfPose BookshelfCasePose(const CaseProfile &profile, float slot, float focus,
                                      float pixels, float screenX, float screenY,
                                      SDL_Rect origin, float targetHeight) {
    constexpr float yaw=1.f,pitch=.65f,camera=2.5f,clearAt=.60f;
    const auto ease=[](float p){p=std::clamp(p,0.f,1.f);return p*p*(3.f-2.f*p);};
    const float w=profile.widthMm/190.f,h=profile.heightMm/190.f,d=profile.thicknessMm/190.f;
    const float radius=std::sqrt(w*w+h*h+d*d)*.5f+.08f;
    const float pull=ease(focus/clearAt),spin=ease((focus-clearAt)/(1.f-clearAt));
    const float x=w*.5f+(-radius-w*.5f)*pull;
    const float sy=std::sin(yaw),cy=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    const float depth=slot*cy-x*sy;
    CaseScene scene{x*cy+slot*sy,h*.5f*cp-depth*sp,h*.5f*sp+depth*cp,pixels,screenX,screenY};
    const float targetZ=camera*(1.f-pixels/targetHeight),projected=pixels*camera/(camera-targetZ);
    scene.x+=((origin.x+origin.w*.5f-screenX)/projected-scene.x)*spin;
    scene.y+=((screenY-origin.y-origin.h*.5f)/projected-scene.y)*spin;
    scene.z+=(targetZ-scene.z)*spin;
    return {scene,yaw+(-.12f-yaw)*spin,pitch+(.025f-pitch)*spin,
            (6.2831853f+.15f)*spin,focus>clearAt};
}

static std::string LowerCopy(const std::string &value) {
    std::string out = value;
    for (char &ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

static std::string ExtensionLower(const std::string &path) {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
    return LowerCopy(path.substr(dot));
}

static CaseProfile CaseProfileForGame(const GameEntry &game, const std::vector<LibraryDefinition> &libraries) {
    const auto &id=game.mediaLibraryId.empty()?game.libraryId:game.mediaLibraryId;
    for(const auto &library:libraries) if(library.id==id) {
        const auto preset=CaseProfiles::For(library.id,library.launcherId,library.extensions);
        return {static_cast<float>(library.caseWidthMm),static_cast<float>(library.caseHeightMm),
                static_cast<float>(library.caseThicknessMm),preset.sidewaysSleeve,preset.sidewaysDefaultSleeve};
    }
    const auto size=CaseProfiles::For(id,{},ExtensionLower(game.absolutePath));
    return {static_cast<float>(size.width),static_cast<float>(size.height),
            static_cast<float>(size.thickness),size.sidewaysSleeve,size.sidewaysDefaultSleeve};
}

void DrawGlassSurface(SDL_Renderer *renderer, SDL_Rect rect, int radius, Color accent,
                      Uint8 opacity = 150, bool elevated = true, float reveal = 1.f) {
    reveal=std::clamp(reveal,0.f,1.f);
    if (elevated) gGlassMaterial.DrawShadow(renderer, rect,static_cast<Uint8>(255*reveal));
    if (gGlassBackdrop && rect.w > 80) {
        // Rounded lens mesh samples the blurred scene with stronger edge displacement.
        // A shallow convex profile makes content bend at the bevel, rather than simply tinting it.
        // The blurred source hides a dense tessellation.  This keeps the curved
        // edge while reducing the large-panel lens from 12,288 to 3,072 indices.
        constexpr int columns = 16, maxRows = 32;
        const int rows=rect.h>200?maxRows:4;
        std::array<SDL_Vertex, (columns+1)*(maxRows+1)> mesh{};
        std::array<int, columns*maxRows*6> indices{};
        const float r = std::min(static_cast<float>(radius),std::min(rect.w,rect.h)*.5f);
        for (int y=0; y<=rows; ++y) {
            const float py = rect.y + rect.h * y / static_cast<float>(rows);
            const float edgeY = std::min(py-rect.y, rect.y+rect.h-py);
            const float inset = edgeY < r ? r - std::sqrt(std::max(0.f,r*r-(r-edgeY)*(r-edgeY))) : 0.f;
            for (int x=0; x<=columns; ++x) {
                const float px = rect.x + inset + (rect.w-2*inset) * x / columns;
                const float nx = (px-(rect.x+rect.w*.5f))/(rect.w*.5f);
                const float ny = (py-(rect.y+rect.h*.5f))/(rect.h*.5f);
                const float edge = std::pow(std::max(std::abs(nx),std::abs(ny)),8.f);
                auto &v = mesh[y*(columns+1)+x];
                const float displacement=rect.h>200?3.f+edge*12.f:.6f+edge*3.5f;
                v.position = {px,py}; v.color = {255,255,255,static_cast<Uint8>((rect.h>200?210:150)*reveal)};
                v.tex_coord = {std::clamp((px-nx*displacement)/kWidth,0.f,1.f),
                               std::clamp((py-ny*displacement)/kHeight,0.f,1.f)};
            }
        }
        int index=0;
        for (int y=0;y<rows;++y) for(int x=0;x<columns;++x) {
            const int a=y*(columns+1)+x,b=a+1,c=a+columns+1,d=c+1;
            for (int v : {a,b,c,b,d,c}) indices[index++]=v;
        }
        SDL_RenderGeometry(renderer,gGlassBackdrop,mesh.data(),(columns+1)*(rows+1),indices.data(),index);
    }
    // A light accent wash leaves the refracted background and white bevel clear.
    FillRoundedRect(renderer,rect,radius,{accent.r,accent.g,accent.b,static_cast<Uint8>(opacity/5*reveal)});
    gGlassMaterial.Draw(renderer, rect, false,static_cast<Uint8>(std::min(255,160+opacity)*reveal));
}

void DrawDetailGlassButton(SDL_Renderer *renderer, SDL_Rect rect, bool focused, Color accent) {
    // Refract the existing backdrop; no new render targets or readbacks per button.
    if(gGlassBackdrop) {
        constexpr int n=8;
        std::array<SDL_Vertex,(n+1)*(n+1)> vertices{};
        std::array<int,n*n*6> indices{};
        const float radius=std::min(rect.w,rect.h)*.25f;
        for(int y=0;y<=n;++y)for(int x=0;x<=n;++x) {
            const float py=rect.y+rect.h*y/static_cast<float>(n);
            const float edge=std::min(py-rect.y,rect.y+rect.h-py);
            const float inset=edge<radius?radius-std::sqrt(std::max(0.f,radius*radius-(radius-edge)*(radius-edge))):0.f;
            const float px=rect.x+inset+(rect.w-2*inset)*x/n;
            auto &v=vertices[y*(n+1)+x];v.position={px,py};v.color={255,255,255,100};
            v.tex_coord={std::clamp((px+(x-n*.5f)*.45f)/kWidth,0.f,1.f),
                std::clamp((py+(y-n*.5f)*.45f)/kHeight,0.f,1.f)};
        }
        int pos=0;
        for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
            const int a=y*(n+1)+x,b=a+1,c=a+n+1,d=c+1;
            for(int index:{a,b,c,b,d,c})indices[pos++]=index;
        }
        SDL_RenderGeometry(renderer,gGlassBackdrop,vertices.data(),vertices.size(),indices.data(),indices.size());
    }
    if(focused)FillRoundedRect(renderer,rect,std::min(rect.w,rect.h)/4,{accent.r,accent.g,accent.b,26});
    gGlassMaterial.DrawButton(renderer,rect,focused,{accent.r,accent.g,accent.b,255});
}

void DrawFloatingMenuSymbol(SDL_Renderer *renderer, int kind, float x, float y, float size,
                            Uint8 alpha) {
    const int s = static_cast<int>(size);
    const Color body = {235,244,246,alpha};
    const Color edge = {255,255,255,static_cast<Uint8>(std::min(255,alpha*2))};
    const Color detail = {104,137,149,static_cast<Uint8>(alpha*.72f)};
    if (kind == 0) {
        SDL_Rect shell={static_cast<int>(x-s*.72f),static_cast<int>(y-s*.42f),
                        static_cast<int>(s*1.44f),static_cast<int>(s*.84f)};
        DrawSoftShadow(renderer,shell,std::max(5,s/7),5,static_cast<Uint8>(alpha*1.8f));
        FillRoundedRect(renderer,shell,std::max(5,s/7),edge);
        FillRoundedRect(renderer,{shell.x+2,shell.y+2,shell.w-4,shell.h-4},std::max(4,s/7-2),body);
        FillRoundedRect(renderer,{static_cast<int>(x-s*.32f),static_cast<int>(y-s*.25f),
                                  static_cast<int>(s*.64f),static_cast<int>(s*.50f)},
                        std::max(2,s/18),detail);
        FillEllipse(renderer,{x-s*.57f,y-s*.07f,s*.13f,s*.13f},detail);
        FillEllipse(renderer,{x+s*.44f,y-s*.07f,s*.13f,s*.13f},detail);
    } else if (kind == 1) {
        SDL_Rect remote={static_cast<int>(x-s*.22f),static_cast<int>(y-s*.62f),
                         static_cast<int>(s*.44f),static_cast<int>(s*1.24f)};
        DrawSoftShadow(renderer,remote,std::max(5,s/6),5,static_cast<Uint8>(alpha*1.6f));
        FillRoundedRect(renderer,remote,std::max(5,s/6),edge);
        FillRoundedRect(renderer,{remote.x+2,remote.y+2,remote.w-4,remote.h-4},
                        std::max(4,s/6-2),body);
        FillEllipse(renderer,{x-s*.09f,y-s*.42f,s*.18f,s*.18f},detail);
        FillRoundedRect(renderer,{static_cast<int>(x-s*.06f),static_cast<int>(y-s*.08f),
                                  static_cast<int>(s*.12f),static_cast<int>(s*.36f)},2,detail);
    } else if (kind == 2) {
        DrawSoftEllipseShadow(renderer,{x-s*.52f,y-s*.22f,s*1.04f,s*.54f},
                              static_cast<Uint8>(alpha*1.5f));
        FillEllipse(renderer,{x-s*.48f,y-s*.48f,s*.96f,s*.96f},edge);
        FillEllipse(renderer,{x-s*.43f,y-s*.43f,s*.86f,s*.86f},body);
        FillEllipse(renderer,{x-s*.12f,y-s*.12f,s*.24f,s*.24f},detail);
    } else if (kind == 3) {
        SDL_Rect horizontal={static_cast<int>(x-s*.5f),static_cast<int>(y-s*.14f),s,
                             std::max(3,static_cast<int>(s*.28f))};
        SDL_Rect vertical={static_cast<int>(x-s*.14f),static_cast<int>(y-s*.5f),
                           std::max(3,static_cast<int>(s*.28f)),s};
        DrawSoftShadow(renderer,{horizontal.x,horizontal.y,horizontal.w,horizontal.h},
                       std::max(2,s/12),5,static_cast<Uint8>(alpha*1.4f));
        FillRoundedRect(renderer,horizontal,std::max(2,s/12),body);
        FillRoundedRect(renderer,vertical,std::max(2,s/12),body);
    } else {
        SDL_Rect tile={static_cast<int>(x-s*.48f),static_cast<int>(y-s*.40f),
                       static_cast<int>(s*.96f),static_cast<int>(s*.80f)};
        DrawSoftShadow(renderer,tile,std::max(5,s/7),5,static_cast<Uint8>(alpha*1.5f));
        FillRoundedRect(renderer,tile,std::max(5,s/7),edge);
        FillRoundedRect(renderer,{tile.x+2,tile.y+2,tile.w-4,tile.h-4},std::max(4,s/7-2),body);
        FillRoundedRect(renderer,{tile.x+tile.w/3,tile.y+tile.h/3,tile.w/3,tile.h/3},
                        std::max(2,s/14),detail);
    }
}

void DrawStockMenuSymbols(SDL_Renderer *renderer, float time) {
    struct Symbol { float x,y,size,speed,phase,depth; int kind; };
    static constexpr std::array<Symbol,14> symbols={{
        {1110,118,82,.84f,.1f,1.0f,0},{980,230,54,.58f,1.4f,.72f,3},
        {1138,305,48,.72f,2.2f,.68f,4},{845,140,42,.48f,3.0f,.52f,1},
        {735,245,58,.62f,4.1f,.63f,2},{575,125,68,.44f,5.0f,.55f,4},
        {405,220,44,.78f,1.9f,.48f,1},{266,112,46,.52f,2.7f,.45f,2},
        {176,300,58,.66f,3.6f,.52f,4},{1035,455,36,.88f,4.6f,.42f,2},
        {890,540,42,.56f,5.7f,.46f,1},{640,585,50,.74f,.8f,.50f,3},
        {355,500,70,.46f,2.9f,.58f,0},{1190,570,66,.62f,5.2f,.58f,4}
    }};
    for(const auto &symbol:symbols){
        const float x=symbol.x+std::sin(time*symbol.speed*.16f+symbol.phase)*42.0f*symbol.depth;
        const float y=symbol.y+std::cos(time*symbol.speed*.13f+symbol.phase*.71f)*31.0f*symbol.depth;
        const float pulse=1.0f+std::sin(time*.11f+symbol.phase)*.045f;
        DrawFloatingMenuSymbol(renderer,symbol.kind,x,y,symbol.size*pulse,
                               static_cast<Uint8>(7+symbol.depth*12));
    }
}

SDL_Texture *StockMenuSymbols(SDL_Renderer *renderer) {
    if (gStockMenuSymbols) return gStockMenuSymbols;
    constexpr int width = 640, height = 360;
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!texture) return nullptr;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    SDL_Texture *target = SDL_GetRenderTarget(renderer);
    int logicalWidth = 0, logicalHeight = 0;
    SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
    float scaleX = 1.0f, scaleY = 1.0f;
    SDL_RenderGetScale(renderer, &scaleX, &scaleY);
    SDL_Rect viewport{}, clip{};
    SDL_RenderGetViewport(renderer, &viewport);
    SDL_RenderGetClipRect(renderer, &clip);
    const bool clipped = SDL_RenderIsClipEnabled(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        SDL_DestroyTexture(texture);
        return nullptr;
    }
    SDL_RenderSetLogicalSize(renderer, kWidth, kHeight);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_RenderSetScale(renderer, 1.0f, 1.0f);
    SetColor(renderer, {0,0,0,0});
    SDL_RenderClear(renderer);
    DrawStockMenuSymbols(renderer, 0.0f);
    SDL_SetRenderTarget(renderer, target);
    SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
    SDL_RenderSetScale(renderer, scaleX, scaleY);
    SDL_RenderSetViewport(renderer, &viewport);
    SDL_RenderSetClipRect(renderer, clipped ? &clip : nullptr);
    gStockMenuSymbols = texture;
    return texture;
}

void DrawStockMenuBackgroundLoop(SDL_Renderer *renderer, bool animate) {
    if (SDL_Texture *symbols = StockMenuSymbols(renderer)) {
        // Retain a little life without rebuilding fourteen translucent objects
        // and their multi-pass shadows on every frame.
        const int drift = animate ? static_cast<int>(std::sin(SDL_GetTicks() / 1900.0f) * 5.0f) : 0;
        SDL_Rect destination = {drift, 0, kWidth, kHeight};
        SDL_RenderCopy(renderer, symbols, nullptr, &destination);
        return;
    }
    DrawStockMenuSymbols(renderer, animate ? SDL_GetTicks()/1000.0f : 0.0f);
}

std::array<Uint8, 7> Glyph(char input) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
    switch (c) {
        case 'A': return {14,17,17,31,17,17,17}; case 'B': return {30,17,17,30,17,17,30};
        case 'C': return {14,17,16,16,16,17,14}; case 'D': return {30,17,17,17,17,17,30};
        case 'E': return {31,16,16,30,16,16,31}; case 'F': return {31,16,16,30,16,16,16};
        case 'G': return {14,17,16,23,17,17,15}; case 'H': return {17,17,17,31,17,17,17};
        case 'I': return {14,4,4,4,4,4,14}; case 'J': return {7,2,2,2,18,18,12};
        case 'K': return {17,18,20,24,20,18,17}; case 'L': return {16,16,16,16,16,16,31};
        case 'M': return {17,27,21,21,17,17,17}; case 'N': return {17,25,21,19,17,17,17};
        case 'O': return {14,17,17,17,17,17,14}; case 'P': return {30,17,17,30,16,16,16};
        case 'Q': return {14,17,17,17,21,18,13}; case 'R': return {30,17,17,30,20,18,17};
        case 'S': return {15,16,16,14,1,1,30}; case 'T': return {31,4,4,4,4,4,4};
        case 'U': return {17,17,17,17,17,17,14}; case 'V': return {17,17,17,17,17,10,4};
        case 'W': return {17,17,17,21,21,21,10}; case 'X': return {17,17,10,4,10,17,17};
        case 'Y': return {17,17,10,4,4,4,4}; case 'Z': return {31,1,2,4,8,16,31};
        case '0': return {14,17,19,21,25,17,14}; case '1': return {4,12,4,4,4,4,14};
        case '2': return {14,17,1,2,4,8,31}; case '3': return {30,1,1,14,1,1,30};
        case '4': return {2,6,10,18,31,2,2}; case '5': return {31,16,16,30,1,1,30};
        case '6': return {14,16,16,30,17,17,14}; case '7': return {31,1,2,4,8,8,8};
        case '8': return {14,17,17,14,17,17,14}; case '9': return {14,17,17,15,1,1,14};
        case '.': return {0,0,0,0,0,6,6}; case ',': return {0,0,0,0,6,4,8};
        case ':': return {0,6,6,0,6,6,0}; case ';': return {0,6,6,0,6,4,8};
        case '-': return {0,0,0,31,0,0,0}; case '_': return {0,0,0,0,0,0,31};
        case '/': return {1,2,2,4,8,8,16}; case '\\': return {16,8,8,4,2,2,1};
        case '[': return {14,8,8,8,8,8,14}; case ']': return {14,2,2,2,2,2,14};
        case '(': return {2,4,8,8,8,4,2}; case ')': return {8,4,2,2,2,4,8};
        case '!': return {4,4,4,4,4,0,4}; case '?': return {14,17,1,2,4,0,4};
        case '+': return {0,4,4,31,4,4,0}; case '=': return {0,0,31,0,31,0,0};
        case '*': return {0,21,14,31,14,21,0}; case '#': return {10,10,31,10,31,10,10};
        case '%': return {25,25,2,4,8,19,19};
        case '\'': return {4,4,2,0,0,0,0}; case '"': return {10,10,5,0,0,0,0};
        default: return {0,0,0,0,0,0,0};
    }
}

int TextWidth(const std::string &text, int scale) {
    if (UseSmoothText(text, scale)) {
        int width = 0;
        if (Utf8::HasNonAscii(text)) width = RunsWidth(text, scale);
        else TTF_SizeUTF8(gSmoothFonts[scale], text.c_str(), &width, nullptr);
        return gSmoothText ? width : static_cast<int>(width * SmoothTextScale(scale) + .5f);
    }
    return text.empty() ? 0 : static_cast<int>(text.size()) * 6 * scale - scale;
}

std::string Ellipsize(const std::string &text, std::size_t maximum) { return Utf8::Ellipsize(text, maximum); }

void DrawText(SDL_Renderer *renderer, const std::string &text, int x, int y, int scale, Color color,
              bool centered = false) {
    if(!gPopupTextGlow && gGlassOverVideo && color.r<160 && color.g<175 && color.b<190)
        color={241,248,253,color.a};
    if (UseSmoothText(text, scale)) {
        const std::string key = std::to_string(gSwitchFont) + ":" + std::to_string(scale) + ":" + std::to_string(color.r) + ":" +
                                std::to_string(color.g) + ":" + std::to_string(color.b) + ":" + text;
        auto found = gSmoothTextCache.find(key);
        if (found == gSmoothTextCache.end()) {
            if (gSmoothTextCache.size() >= 256) {
                auto oldest = std::min_element(gSmoothTextCache.begin(), gSmoothTextCache.end(),
                    [](const auto &left, const auto &right) { return left.second.lastUse < right.second.lastUse; });
                if (oldest != gSmoothTextCache.end()) {
                    if (oldest->second.texture) SDL_DestroyTexture(oldest->second.texture);
                    gSmoothTextCache.erase(oldest);
                }
            }
            SDL_Surface *surface = RenderTextSurface(text, scale);
            SmoothTextEntry entry;
            if (surface) {
                entry.width = surface->w; entry.height = surface->h;
                entry.texture = SDL_CreateTextureFromSurface(renderer, surface);
                SDL_FreeSurface(surface);
            }
            entry.lastUse = SDL_GetTicks();
            found = gSmoothTextCache.emplace(key, entry).first;
        }
        found->second.lastUse = SDL_GetTicks();
        if (found->second.texture) {
            SDL_SetTextureAlphaMod(found->second.texture, color.a);
            const float k = SmoothTextScale(scale);
            const int width = static_cast<int>(found->second.width * k + .5f);
            if (centered) x -= width / 2;
            SDL_Rect destination = {x, y, width, static_cast<int>(found->second.height * k + .5f)};
            if(gPopupTextGlow) {
                // Reuse the glyph alpha mask: soft white outer halo, then a
                // crisp dark face. No extra textures or per-frame rasterization.
                SDL_SetTextureColorMod(found->second.texture,255,255,255);
                for(int radius=3;radius>=1;--radius){
                    SDL_SetTextureAlphaMod(found->second.texture,static_cast<Uint8>(color.a*(radius==1?100:radius==2?48:20)/255));
                    for(const auto &offset:std::array<SDL_Point,8>{{{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}}}){
                        SDL_Rect halo=destination;halo.x+=offset.x*radius;halo.y+=offset.y*radius;
                        SDL_RenderCopy(renderer,found->second.texture,nullptr,&halo);
                    }
                }
            }else if(gGlassTextHalo && color.r<180 && color.g<190 && color.b<200) {
                // Small light halo keeps gray lettering readable over gray artwork.
                SDL_SetTextureColorMod(found->second.texture,255,255,255);
                SDL_SetTextureAlphaMod(found->second.texture,static_cast<Uint8>(color.a*170/255));
                for(const auto offset:std::array<SDL_Point,4>{{{-1,0},{1,0},{0,-1},{0,1}}}){
                    SDL_Rect halo=destination;halo.x+=offset.x;halo.y+=offset.y;
                    SDL_RenderCopy(renderer,found->second.texture,nullptr,&halo);
                }
            }else if(gGlassOverVideo) {
                SDL_Rect shadow=destination; shadow.x+=1; shadow.y+=2;
                SDL_SetTextureColorMod(found->second.texture,0,0,0);
                SDL_SetTextureAlphaMod(found->second.texture,static_cast<Uint8>(color.a*180/255));
                SDL_RenderCopy(renderer,found->second.texture,nullptr,&shadow);
                SDL_SetTextureColorMod(found->second.texture,255,255,255);
                SDL_SetTextureAlphaMod(found->second.texture,color.a);
            }
            SDL_SetTextureColorMod(found->second.texture,color.r,color.g,color.b);
            SDL_SetTextureAlphaMod(found->second.texture,color.a);
            SDL_RenderCopy(renderer, found->second.texture, nullptr, &destination);
        }
        return;
    }
    if (centered) x -= TextWidth(text, scale) / 2;
    SetColor(renderer, color);
    for (char c : text) {
        const auto glyph = Glyph(c);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if (glyph[row] & (1 << (4 - column))) {
                    SDL_Rect pixel = {x + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

// Fit columns using the actual font metrics, not a guessed character count.
void DrawFittedText(SDL_Renderer *renderer, std::string text, int x, int y, int width,
                    int scale, Color color, bool right = false) {
    if (TextWidth(text, scale) > width) {
        do {
            std::size_t last = text.size() - 1;
            while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
            text.resize(last);
        } while (!text.empty() && TextWidth(text + "...", scale) > width);
        text += "...";
    }
    DrawText(renderer, text, right ? x + width - TextWidth(text,scale) : x, y, scale, color);
}


bool ValidArtworkSize(int width, int height) {
    return width > 0 && height > 0 && width <= kMaxArtworkDimension && height <= kMaxArtworkDimension &&
           static_cast<std::size_t>(width) * static_cast<std::size_t>(height) <= kMaxArtworkPixels;
}
int ArtworkDivisor(int width,int height,int targetEdge=0) {
    // Bound source work separately from texture memory. Decode large scans to
    // a smaller surface; never allocate a full-size RGBA copy of them.
    if(width<=0 || height<=0 || width>8192 || height>8192 ||
       static_cast<std::uint64_t>(width)*height>16*1024*1024) return 0;
    int divisor=1;
    while(!ValidArtworkSize((width+divisor-1)/divisor,(height+divisor-1)/divisor)) divisor*=2;
    if(targetEdge>0) divisor=std::max(divisor,std::max(width,height)/targetEdge);
    return divisor;
}

SDL_Surface *LoadPng(const std::string &path,int targetEdge=0) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return nullptr;
    png_byte signature[8];
    if (std::fread(signature, 1, sizeof(signature), file) != sizeof(signature) || png_sig_cmp(signature, 0, 8)) {
        std::fclose(file);
        return nullptr;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    struct ReadState { SDL_Surface *surface; png_bytep row; };
    auto *state=static_cast<ReadState *>(std::calloc(1,sizeof(ReadState)));
    if (!png || !info || !state || setjmp(png_jmpbuf(png))) {
        if(state) { if(state->surface) SDL_FreeSurface(state->surface); std::free(state->row); std::free(state); }
        if (png) png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
        std::fclose(file);
        return nullptr;
    }
    png_init_io(png, file);
    png_set_sig_bytes(png, 8);
    png_set_user_limits(png,8192,8192);
    png_read_info(png, info);
    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const int divisor=ArtworkDivisor(width,height,targetEdge);
    if(!divisor) png_error(png,"Artwork source exceeds safe limits");
    const int colorType = png_get_color_type(png, info);
    const int bitDepth = png_get_bit_depth(png, info);
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    const bool transparency=png_get_valid(png, info, PNG_INFO_tRNS);
    if (transparency) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(colorType & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    const int passes=png_set_interlace_handling(png);
    png_read_update_info(png, info);
    const int outputWidth=(width+divisor-1)/divisor,outputHeight=(height+divisor-1)/divisor;
    state->surface=SDL_CreateRGBSurfaceWithFormat(0,outputWidth,outputHeight,32,SDL_PIXELFORMAT_RGBA32);
    state->row=static_cast<png_bytep>(std::malloc(png_get_rowbytes(png,info)));
    if(!state->surface || !state->row) png_error(png,"Artwork allocation failed");
    // Adam7 passes update only their own sample positions, so one source row
    // plus the reduced output is sufficient even for interlaced scans.
    constexpr int xStart[]={0,4,0,2,0,1,0},xStep[]={8,8,4,4,2,2,1};
    constexpr int yStart[]={0,0,4,0,2,0,1},yStep[]={8,8,8,4,4,2,2};
    for(int pass=0;pass<passes;++pass) {
        int outputY=0;
        for(png_uint_32 y=0;y<height;++y) {
            png_read_row(png,state->row,nullptr);
            if(outputY>=outputHeight || y!=std::min(static_cast<int>(height)-1,outputY*divisor+divisor/2)) continue;
            auto *dst=static_cast<unsigned char *>(state->surface->pixels)+outputY*state->surface->pitch;
            ++outputY;
            if(passes>1 && (static_cast<int>(y)<yStart[pass] || (y-yStart[pass])%yStep[pass])) continue;
            for(int x=0;x<outputWidth;++x) {
                const int sourceX=std::min(static_cast<int>(width)-1,x*divisor+divisor/2);
                if(passes>1 && (sourceX<xStart[pass] || (sourceX-xStart[pass])%xStep[pass])) continue;
                std::memcpy(dst+x*4,state->row+sourceX*4,4);
            }
        }
    }
    png_read_end(png,nullptr);
    SDL_Surface *surface=state->surface;
    std::free(state->row); std::free(state);
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    return surface;
}

// Reduce UI artwork once using every source pixel, not a few bilinear samples.
// Weight RGB by alpha so transparent edge colors cannot produce dark fringes.
// Consumes the RGBA32 surface returned by LoadPng; retains it if allocation fails.
SDL_Surface *FilterUiImage(SDL_Surface *source,int edge) {
    if(!source || edge<=0 || std::max(source->w,source->h)<=edge)return source;
    const float scale=float(edge)/std::max(source->w,source->h);
    const int width=std::max(1,int(std::lround(source->w*scale)));
    const int height=std::max(1,int(std::lround(source->h*scale)));
    auto *output=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);
    if(!output)return source;
    const float sx=float(source->w)/width,sy=float(source->h)/height;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const float left=x*sx,right=(x+1)*sx,top=y*sy,bottom=(y+1)*sy;
        float alpha=0,red=0,green=0,blue=0,area=0;
        for(int py=int(top);py<std::min(source->h,int(std::ceil(bottom)));++py) {
            const auto *row=static_cast<const Uint8 *>(source->pixels)+py*source->pitch;
            const float dy=std::min(bottom,float(py+1))-std::max(top,float(py));
            for(int px=int(left);px<std::min(source->w,int(std::ceil(right)));++px) {
                const float weight=dy*(std::min(right,float(px+1))-std::max(left,float(px)));
                const auto *p=row+px*4;
                const float opacity=weight*p[3];
                area+=weight;alpha+=opacity;
                red+=opacity*p[0];green+=opacity*p[1];blue+=opacity*p[2];
            }
        }
        auto *p=static_cast<Uint8 *>(output->pixels)+y*output->pitch+x*4;
        const auto byte=[](float v){return Uint8(std::clamp(std::lround(v),0l,255l));};
        p[0]=alpha>0?byte(red/alpha):0;p[1]=alpha>0?byte(green/alpha):0;
        p[2]=alpha>0?byte(blue/alpha):0;p[3]=byte(alpha/area);
    }
    SDL_FreeSurface(source);
    return output;
}

SDL_Texture *UiOutlineGlow(SDL_Renderer *renderer,SDL_Surface *source) {
    // A small separable Gaussian-like blur of alpha only; never blur the logo RGB.
    constexpr int padding=4;
    constexpr int weights[]={1,8,28,56,70,56,28,8,1};
    const int w=source->w+padding*2,h=source->h+padding*2;
    auto *glow=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);
    if(!glow)return nullptr;
    std::vector<float> horizontal(w*h,0.f);
    for(int y=0;y<source->h;++y) for(int x=0;x<w;++x) {
        const auto *row=static_cast<const Uint8 *>(source->pixels)+y*source->pitch;
        float alpha=0;
        for(int i=-padding;i<=padding;++i) {
            const int px=x-padding+i;
            if(px>=0 && px<source->w)alpha+=row[px*4+3]*weights[i+padding]/256.f;
        }
        horizontal[(y+padding)*w+x]=alpha;
    }
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        float alpha=0;
        for(int i=-padding;i<=padding;++i)
            if(y+i>=0 && y+i<h)alpha+=horizontal[(y+i)*w+x]*weights[i+padding]/256.f;
        auto *p=static_cast<Uint8 *>(glow->pixels)+y*glow->pitch+x*4;
        p[0]=80;p[1]=211;p[2]=255;p[3]=Uint8(std::clamp(std::lround(alpha*.65f),0l,255l));
    }
    auto *texture=SDL_CreateTextureFromSurface(renderer,glow);SDL_FreeSurface(glow);
    if(texture) {
        SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture,SDL_ScaleModeLinear);
    }
    return texture;
}

SDL_Surface *LoadTga(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return nullptr;
    unsigned char header[18];
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header) || header[1] != 0 || header[2] != 2) {
        std::fclose(file);
        return nullptr;
    }
    const int width = header[12] | (header[13] << 8);
    const int height = header[14] | (header[15] << 8);
    const int bytes = header[16] / 8;
    if ((bytes != 3 && bytes != 4) || !ValidArtworkSize(width, height)) {
        std::fclose(file);
        return nullptr;
    }
    std::fseek(file, header[0], SEEK_CUR);
    std::vector<unsigned char> source(static_cast<std::size_t>(width) * height * bytes);
    if (std::fread(source.data(), 1, source.size(), file) != source.size()) {
        std::fclose(file);
        return nullptr;
    }
    std::fclose(file);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    const bool topOrigin = (header[17] & 0x20) != 0;
    for (int y = 0; y < height; ++y) {
        const int sourceY = topOrigin ? y : height - 1 - y;
        auto *destination = static_cast<unsigned char *>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < width; ++x) {
            const auto *pixel = &source[(static_cast<std::size_t>(sourceY) * width + x) * bytes];
            destination[x * 4 + 0] = pixel[2]; destination[x * 4 + 1] = pixel[1];
            destination[x * 4 + 2] = pixel[0]; destination[x * 4 + 3] = bytes == 4 ? pixel[3] : 255;
        }
    }
    return surface;
}

SDL_Surface *LoadJpeg(const std::string &path,int targetEdge=0) {
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return nullptr;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return nullptr; }
    const long length = std::ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file); return nullptr;
    }
    std::vector<unsigned char> encoded(static_cast<std::size_t>(length));
    if (std::fread(encoded.data(), 1, encoded.size(), file) != encoded.size()) {
        std::fclose(file); return nullptr;
    }
    std::fclose(file);

    // Use the stable TurboJPEG 2 API, also available in TurboJPEG 3.  The
    // host visual-preview image still ships v2 while the Wii U toolchain has
    // v3, so this keeps both build paths identical.
    tjhandle decoder = tjInitDecompress();
    if (!decoder) return nullptr;
    int width = 0, height = 0;
    int subsamp = 0, colorspace = 0;
    if (tjDecompressHeader3(decoder, encoded.data(), static_cast<unsigned long>(encoded.size()),
                            &width, &height, &subsamp, &colorspace) < 0) {
        tjDestroy(decoder); return nullptr;
    }
    int divisor=ArtworkDivisor(width,height);
    if (!divisor) {
        tjDestroy(decoder); return nullptr;
    }
    // JPEG can discard DCT work at 1/2, 1/4 or 1/8 resolution. Thumbnail
    // preparation must not decode a detailed sleeve just to shrink it again.
    if(targetEdge>0) while(divisor<8 && (std::max(width,height)+divisor*2-1)/(divisor*2)>=targetEdge) divisor*=2;
    width=(width+divisor-1)/divisor; height=(height+divisor-1)/divisor;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface || tjDecompress2(decoder, encoded.data(), static_cast<unsigned long>(encoded.size()),
                                  static_cast<unsigned char *>(surface->pixels), width, surface->pitch,
                                  height, TJPF_RGBA, 0) < 0) {
        if (surface) SDL_FreeSurface(surface);
        tjDestroy(decoder); return nullptr;
    }
    tjDestroy(decoder);
    return surface;
}

#ifndef UFLOW_HOST_PREVIEW
extern "C" unsigned UFlow_BC3TextureBytes(SDL_Texture *texture);
#endif
class TextureCache {
public:
#ifdef UFLOW_HOST_PREVIEW
    static constexpr bool DefaultAsync = false;
#else
    static constexpr bool DefaultAsync = true;
#endif
    explicit TextureCache(SDL_Renderer *renderer, bool asynchronous = DefaultAsync) : renderer_(renderer) {
        SDL_RendererInfo info{};SDL_GetRendererInfo(renderer,&info);
        bool supported=false;
        for(unsigned i=0;i<info.num_texture_formats;++i)if(info.texture_formats[i]==SDL_FOURCC('D','X','T','5'))supported=true;
        packedCovers_.store(supported);
        if (asynchronous) EnableAsync();
    }
    void EnableAsync() {
        if(worker_) return;
        worker_ = std::make_unique<ArtworkWorker>(Decode);
        if (!worker_->Available()) SDL_Log("Artwork worker unavailable; retaining placeholders: %s", SDL_GetError());
    }
    ~TextureCache() { worker_.reset(); Clear(); }
    void BeginFrame() { pending_.clear(); urgentThumbnails_.clear(); }
    std::uint64_t Revision() const {return revision_;}
    std::size_t Count() const { return textures_.size()+resident_.size(); }
    std::size_t Bytes() const { return bytes_+residentBytes_; }
    std::size_t Pending() const { return pending_.size(); }
    std::size_t ResidentCount() const { return resident_.size(); }
    std::size_t ResidentBytes() const { return residentBytes_; }
    bool NativeCovers() const { return packedCovers_.load(); }
    int SharpLookAhead() const { return NativeCovers()?40:12; }
    // Every displayed cover gets a bounded resident thumbnail. Detailed artwork
    // uses the ordinary LRU and cannot evict these browsing fallbacks.
    // Preparation is asynchronous, including when the user skips its progress screen.
    void PrepareIcons(const std::vector<std::string> &paths) {
        if (!worker_ || !worker_->Available()) return;
        for (const auto &path:paths) {
            if (warmPaths_.size()>=8192) break;
            if (!path.empty() && !Known(path) && warming_.insert(path).second) warmPaths_.push_back(path);
        }
        thumbnailEdge_=std::min(128,static_cast<int>(std::sqrt(double(kResidentBytes)/
            (4*std::max<std::size_t>(1,warmPaths_.size())))));
    }
    std::size_t IconsTotal() const { return warmPaths_.size(); }
    std::size_t IconsDone() const { return warmPaths_.size()-warming_.size(); }
    bool PreparingIcons() const { return !warming_.empty(); }
#ifdef UFLOW_HOST_PREVIEW
    static unsigned SourceDecodes() { return sourceDecodes_.load(); }
    static bool CheckNativeCoverCache(const std::string &source) {
        const std::string path="native-cover-cache-check.jpg";
        std::filesystem::copy_file(source,path,std::filesystem::copy_options::overwrite_existing);
        const auto file=CoverTextureCache::File(path),cached=file+".native1024";
        std::remove(file.c_str());std::remove(cached.c_str());std::remove((file+".native2048").c_str());
        struct Reset {~Reset(){packedCovers_.store(false);}} reset;
        packedCovers_.store(true);
        const auto before=sourceDecodes_.load();
        auto *first=DecodeCover(path,1024);
        if(!first || !CoverTextureCache::Packed(first))return false;
        const unsigned w=CoverTextureCache::Width(first),h=CoverTextureCache::Height(first);
        const bool dimensions=w && h && std::max(w,h)<=1024;
        SDL_FreeSurface(first);
        if(!dimensions || sourceDecodes_!=before+1 || !std::filesystem::is_regular_file(cached))return false;
        auto *second=DecodeCover(path,1024);
        if(!second || !CoverTextureCache::Packed(second))return false;SDL_FreeSurface(second);
        if(sourceDecodes_!=before+1)return false;
        const auto readBefore=CoverTextureCache::payloadBytesRead.load();
        auto *thumb=Decode(std::string(kThumbnailPrefix)+"128|"+path);
        if(!thumb || CoverTextureCache::Packed(thumb) || thumb->w>128 || thumb->h>128)return false;
        SDL_FreeSurface(thumb);if(sourceDecodes_!=before+1)return false;
        if(CoverTextureCache::payloadBytesRead-readBefore>48*1024)return false;
        SDL_Log("Native preload payload: %zu bytes (full cache: %zu bytes)",
            CoverTextureCache::payloadBytesRead.load()-readBefore,std::size_t(std::filesystem::file_size(cached)));
        auto *near=Decode(std::string(kNearbyPrefix)+path);
        if(!near || !CoverTextureCache::Packed(near) || std::max(CoverTextureCache::Width(near),CoverTextureCache::Height(near))>512)return false;
        SDL_FreeSurface(near);if(sourceDecodes_!=before+1)return false;
        auto *detail=DecodeCover(path,2048);
        if(!detail || !CoverTextureCache::Packed(detail))return false;SDL_FreeSurface(detail);
        if(sourceDecodes_!=before+2)return false;
        FILE *broken=std::fopen(cached.c_str(),"wb");if(!broken)return false;
        std::fputs("bad",broken);std::fclose(broken);
        auto *repaired=DecodeCover(path,1024);
        if(!repaired || !CoverTextureCache::Packed(repaired))return false;SDL_FreeSurface(repaired);
        if(sourceDecodes_!=before+3)return false;
        std::remove(cached.c_str());
        auto *coldNearby=Decode(std::string(kNearbyPrefix)+path);
        if(!coldNearby)return false;SDL_FreeSurface(coldNearby);
        auto *coldFull=DecodeCover(path,1024);
        if(!coldFull || CoverTextureCache::Width(coldFull)!=w || CoverTextureCache::Height(coldFull)!=h)return false;
        SDL_FreeSurface(coldFull);if(sourceDecodes_!=before+4)return false;
        SDL_Log("PASS: native compressed cover decode, cache reuse, preload mips, independent detail cache, corruption recovery");
        return true;
    }
    static bool CheckThumbnailCache(const std::string &source) {
        std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow/cache");
        const std::string path="thumbnail-cache-check.jpg";
        std::filesystem::copy_file(source,path,std::filesystem::copy_options::overwrite_existing);
        const auto full=CoverTextureCache::File(path),cached=ThumbnailFile(path,128),job=std::string(kThumbnailPrefix)+"128|"+path;
        std::remove(cached.c_str());std::remove(full.c_str());
        const auto initial=sourceDecodes_.load();
        auto decode=[&]() {
            auto *surface=Decode(job);
            const bool valid=surface && surface->w<=128 && surface->h<=128;
            if(surface) SDL_FreeSurface(surface);
            return valid;
        };
        if(!decode() || sourceDecodes_!=initial+1 || !std::filesystem::is_regular_file(cached) || std::filesystem::exists(full)) return false;
        if(!decode() || sourceDecodes_!=initial+1) return false;
        // A damaged thumbnail regenerates; a changed original gets a new key.
        auto *file=std::fopen(cached.c_str(),"wb");if(!file) return false;
        std::fputs("truncated",file);std::fclose(file);
        if(!decode() || sourceDecodes_!=initial+2) return false;
        file=std::fopen(path.c_str(),"ab");if(!file) return false;
        std::fputc(0,file);std::fclose(file);
        if(ThumbnailFile(path,128)==cached || !decode() || sourceDecodes_!=initial+3) return false;
        if(!decode() || sourceDecodes_!=initial+3) return false;
        // Existing full-cover caches also supply thumbnails without decoding.
        auto *original=DecodeCover(path,2048);if(!original || CoverTextureCache::Packed(original))return false;SDL_FreeSurface(original);
        if(std::filesystem::exists(CoverTextureCache::File(path)) || std::filesystem::exists(CoverDisplayCache::File(path,2048)))return false;
        auto *prepared=DecodeCover(path,1,true);if(!prepared)return false;SDL_FreeSurface(prepared);
        std::remove(ThumbnailFile(path,128).c_str());
        const auto cachedDecodes=sourceDecodes_.load();
        if(!decode() || sourceDecodes_!=cachedDecodes)return false;
        return true;
    }
#endif
    bool Known(const std::string &path) const { return path.empty() || resident_.contains(path) || textures_.contains(path); }
    bool DetailKnown(const std::string &path) const { return path.empty() || textures_.contains(path); }
    bool HasTexture(const std::string &path) const {
        const auto found = textures_.find(path);
        if(found!=textures_.end() && found->second.info.texture) return true;
        const auto resident=resident_.find(path);
        return resident!=resident_.end() && resident->second.texture;
    }
    void PinBackground(const std::string &path) {
        pinnedBackground_=path.empty()?std::string():std::string(kBackgroundPrefix)+path;
    }
    void SetCoverDetail(bool enabled) { highDetail_=enabled; }
    void PinCover(const std::string &path) { if(path!=pinnedCover_)lastCoverChange_=SDL_GetTicks(); pinnedCover_=path; nearbyCovers_.clear(); }
    void PinNearbyCover(const std::string &path) {
        if(!path.empty() && nearbyCovers_.size()<6)nearbyCovers_.push_back(path);
    }
    bool CoverKnown(const std::string &path,bool detail) const {
        detail=detail && highDetail_;
        return Known(CoverKey(path,detail)) || (detail && Known(CoverKey(path,false))) || resident_.contains(path);
    }
    bool CoverDetailKnown(const std::string &path,bool detail) const {
        detail=detail && highDetail_;
        return DetailKnown(CoverKey(path,detail));
    }
    bool HasCover(const std::string &path,bool detail) const {
        detail=detail && highDetail_;
        return HasTexture(CoverKey(path,detail)) || (detail && HasTexture(CoverKey(path,false))) ||
            (resident_.contains(path) && resident_.at(path).texture);
    }
    TextureInfo GetCover(const std::string &path,bool detail) {
        detail=detail && highDetail_;
        if(path.empty())return {};
        // The next/previous originals are already decoded: use them during the
        // approach animation too, before the selected styling threshold flips.
        if(detail || path==pinnedCover_ || std::find(nearbyCovers_.begin(),nearbyCovers_.end(),path)!=nearbyCovers_.end()) {
            const auto original=textures_.find(CoverKey(path,true));
            if(original!=textures_.end() && original->second.info.texture) {
                original->second.lastUse=SDL_GetTicks();return original->second.info;
            }
        }
        const bool nearFocus=path==pinnedCover_ || std::find(nearbyCovers_.begin(),nearbyCovers_.end(),path)!=nearbyCovers_.end();
        // Distant cases need fewer texels. Prefer any already resident sharper
        // image, but do not queue 1024px reads for every tiny on-screen case.
        if(NativeCovers() && !detail && !nearFocus && !HasTexture(CoverKey(path,false))) {
            auto result=Get(std::string(kNearbyPrefix)+path);
            if(result.texture)return result;
            const auto fallback=resident_.find(path);
            return fallback!=resident_.end()?fallback->second:result;
        }
        auto result=Get(CoverKey(path,detail));
        if(detail && CoverDetailKnown(path,true) && result.texture && textures_.at(CoverKey(path,true)).info.texture)return result;
        // A focus change upgrades asynchronously; keep the existing image until
        // its original-resolution texture is ready.
        if(detail) {
            const auto found=textures_.find(CoverKey(path,false));
            if(found!=textures_.end() && found->second.info.texture)return found->second.info;
        }
        if(result.texture)return result;
        if(NativeCovers()) {
            const auto nearby=textures_.find(std::string(kNearbyPrefix)+path);
            if(nearby!=textures_.end() && nearby->second.info.texture) {
                nearby->second.lastUse=SDL_GetTicks();return nearby->second.info;
            }
        }
        const auto found=resident_.find(path);
        return found!=resident_.end()?found->second:result;
    }
    void PrefetchCover(const std::string &path,bool detail,bool distant=false) {
        if(distant && NativeCovers()) {
            if(!HasTexture(CoverKey(path,false)))Prefetch(path.empty()?path:std::string(kNearbyPrefix)+path);
            return;
        }
        detail=detail && highDetail_;
        // After an explicit preload skip or a library change, fill visible
        // resident fallbacks before spending time on full-resolution conversion.
        if(warming_.contains(path) && urgentThumbnails_.size()<32) {
            const auto job=ThumbnailJob(path);
            if(std::find(urgentThumbnails_.begin(),urgentThumbnails_.end(),job)==urgentThumbnails_.end())urgentThumbnails_.push_back(job);
        }
        Prefetch(CoverKey(path,detail));
    }
    TextureInfo GetBackground(const std::string &path) {
        return path.empty()?TextureInfo{}:Get(std::string(kBackgroundPrefix)+path);
    }
    void PrefetchBackground(const std::string &path) {
        if(!path.empty())Prefetch(std::string(kBackgroundPrefix)+path);
    }
    TextureInfo Get(const std::string &path) {
        if (path.empty()) return {};
        if (const auto found = textures_.find(path); found != textures_.end()) {
            found->second.lastUse = SDL_GetTicks();
            if(found->second.info.texture || !resident_.contains(path)) return found->second.info;
        }
        if (const auto found=resident_.find(path); found!=resident_.end()) {
            if(found->second.texture && !textures_.contains(path)) Queue(path);
            return found->second;
        }
        if (!worker_) return Load(path);
        Queue(path);
        return {};
    }
    void Prefetch(const std::string &path) {
        if (path.empty()) return;
        if(auto found=textures_.find(path);found!=textures_.end()) {
            found->second.lastUse=SDL_GetTicks();
            return;
        }
        if(resident_.contains(path) && !resident_.at(path).texture) return;
        if (!worker_) Load(path); else Queue(path);
    }
    void PumpOne(bool preload=true) {
        if (!worker_) return;
        while (warmCursor_<warmPaths_.size() && !warming_.contains(warmPaths_[warmCursor_])) ++warmCursor_;
        for (std::size_t i=warmCursor_;preload && i<warmPaths_.size() && pending_.size()<ArtworkWorker::Capacity;++i)
            if (warming_.contains(warmPaths_[i])) Queue(ThumbnailJob(warmPaths_[i]));
        // Sharp visible covers precede tiny fallbacks. No long, non-cancellable
        // full-sleeve compression job can block a newly selected cover.
        if(preload && pending_.empty() && warming_.empty() && !displayWrites_.empty() &&
           SDL_GetTicks()-lastCoverChange_>=1200)
            Queue(std::string(kDisplayWritePrefix)+displayWrites_.front());
        auto wanted=pending_;
        for(const auto &job:urgentThumbnails_) {
            if(wanted.size()>=ArtworkWorker::Capacity)break;
            if(std::find(wanted.begin(),wanted.end(),job)==wanted.end())wanted.push_back(job);
        }
        worker_->Request(wanted);
        const auto start=SDL_GetTicks();
        // During video, only visible artwork requests run, with one upload per
        // frame. Background preparation resumes when playback stops.
        for(unsigned uploaded=0;uploaded<(preload?ArtworkWorker::ReadyCapacity:1);++uploaded) {
            auto ready = worker_->Take();
            if(!ready.available) break;
            if(ready.path.starts_with(kDisplayWritePrefix)) {
                const auto source=ready.path.substr(std::strlen(kDisplayWritePrefix));
                if(ready.surface)SDL_FreeSurface(ready.surface);
                displayWritten_.insert(source);
                std::erase(displayWrites_,source);
            }
            else if(ready.path.starts_with(kPreparePrefix)) {
                if(ready.surface)SDL_FreeSurface(ready.surface);
                ++idleCoverCursor_;
            }
            else if (ready.path.starts_with(kThumbnailPrefix)) {
                const auto path=ready.path.substr(ready.path.find('|')+1);
                if(!resident_.contains(path)) Upload(path,ready.surface,true);
                else { warming_.erase(path); if(ready.surface) SDL_FreeSurface(ready.surface); }
            }
            else if (!textures_.contains(ready.path)) {
                if(!packedCovers_.load() && ready.surface && ready.path.starts_with(kBrowsePrefix)) {
                    const auto source=ready.path.substr(std::strlen(kBrowsePrefix));
                    if(!displayWritten_.contains(source) && std::find(displayWrites_.begin(),displayWrites_.end(),source)==displayWrites_.end()) {
                        if(displayWrites_.size()>=64)displayWrites_.erase(displayWrites_.begin());
                        displayWrites_.push_back(source);
                    }
                }
                Upload(ready.path, ready.surface);
            }
            else if (ready.surface) SDL_FreeSurface(ready.surface);
            if(SDL_GetTicks()-start>=3) break;
        }
    }
    void Clear() {
        ++revision_;
        if (worker_) worker_->Reset();
        for (auto &[unused, cached] : textures_) if (cached.info.texture) SDL_DestroyTexture(cached.info.texture);
        textures_.clear();
        for (auto &[unused, info]:resident_) if(info.texture) SDL_DestroyTexture(info.texture);
        resident_.clear(); residentBytes_=0;
        warmPaths_.clear(); warming_.clear(); warmCursor_=0;idleCoverCursor_=0;
        pending_.clear();urgentThumbnails_.clear();displayWrites_.clear();displayWritten_.clear();
        pinnedBackground_.clear();
        pinnedCover_.clear();
        nearbyCovers_.clear();
        bytes_ = 0;
    }
private:
    inline static std::atomic<bool> packedCovers_{false};
    static constexpr std::size_t kMaxEntries = 128;
    static constexpr std::size_t kMaxBytes = 96 * 1024 * 1024;
    // Leave room for detailed artwork and the hardware video workspace.
    static constexpr std::size_t kResidentBytes = 48 * 1024 * 1024;
    std::size_t idleCoverCursor_=0;
    Uint32 lastCoverChange_=0;
    static constexpr const char *kDisplayWritePrefix="@uflow-write-display:";
    std::vector<std::string> displayWrites_;
    std::unordered_set<std::string> displayWritten_;
    static constexpr const char *kPreparePrefix="@uflow-prepare-cover:";
    static constexpr const char *kThumbnailPrefix = "@uflow-thumbnail:";
    static constexpr const char *kBackgroundPrefix = "@uflow-background:";
    static constexpr const char *kBrowsePrefix = "@uflow-browse-cover:";
    static constexpr const char *kNearbyPrefix = "@uflow-nearby-cover:";
    static constexpr const char *kDetailPrefix = "@uflow-detail-cover:";
    static std::string CoverKey(const std::string &path,bool detail) {
        return path.empty()?path:std::string(detail?kDetailPrefix:kBrowsePrefix)+path;
    }
#ifdef UFLOW_HOST_PREVIEW
    static inline std::atomic<unsigned> sourceDecodes_{0};
#endif
    int thumbnailEdge_=128;
    std::string ThumbnailJob(const std::string &path) const {
        return std::string(kThumbnailPrefix)+std::to_string(thumbnailEdge_)+"|"+path;
    }
    void Queue(const std::string &path) {
        if (std::find(pending_.begin(), pending_.end(), path) != pending_.end()) return;
        // A bounded queue keeps a rapid scroll from retaining obsolete covers.
        if (pending_.size() >= ArtworkWorker::Capacity) return;
        pending_.push_back(path);
    }
    bool RemoveOldest() {
        auto oldest=textures_.end();
        for(auto it=textures_.begin();it!=textures_.end();++it) {
            if(it->first==pinnedBackground_ || it->first==CoverKey(pinnedCover_,true) || it->first==CoverKey(pinnedCover_,false) ||
               std::any_of(nearbyCovers_.begin(),nearbyCovers_.end(),[&](const auto &p){return it->first==CoverKey(p,false);}) ||
               std::any_of(nearbyCovers_.begin(),nearbyCovers_.begin()+std::min(std::size_t(2),nearbyCovers_.size()),[&](const auto &p){return it->first==CoverKey(p,true);}))continue;
            if(oldest==textures_.end() || it->second.lastUse<oldest->second.lastUse)oldest=it;
        }
        if (oldest == textures_.end()) return false;
        if (oldest->second.info.texture) SDL_DestroyTexture(oldest->second.info.texture);
        bytes_ -= oldest->second.bytes;
        textures_.erase(oldest);
        return true;
    }
    void MakeRoom(std::size_t needed) {
        while (!textures_.empty() && (textures_.size() >= kMaxEntries || bytes_ + needed > kMaxBytes))
            if(!RemoveOldest())break;
    }
    static std::string ThumbnailFile(const std::string &path,unsigned edge) {
        const auto file=CoverTextureCache::File(path);
        return file.empty()?file:file+".thumb"+std::to_string(edge);
    }
    static SDL_Surface *Decode(const std::string &path) {
        if(path.starts_with(kNearbyPrefix))return DecodeCover(path.substr(std::strlen(kNearbyPrefix)),512);
        if(path.starts_with(kDisplayWritePrefix))return DecodeCover(path.substr(std::strlen(kDisplayWritePrefix)),1024,false,true);
        if(path.starts_with(kPreparePrefix))return DecodeCover(path.substr(std::strlen(kPreparePrefix)),1,true);
        // Cached mip levels keep side cards sharp without decoding whole scans.
        if(path.starts_with(kBrowsePrefix) || path.starts_with(kDetailPrefix)) {
            const bool detail=path.starts_with(kDetailPrefix);
            const auto source=path.substr(std::strlen(detail?kDetailPrefix:kBrowsePrefix));
            return DecodeCover(source,detail?2048:1024);
        }
        // Backgrounds fill a 1280x720 screen. Decode at display size instead of
        // keeping full scans alongside the selected cover and decoder workspace.
        if(path.starts_with(kBackgroundPrefix))return DecodeSource(path.substr(std::strlen(kBackgroundPrefix)),1280);
        if(path.starts_with(kThumbnailPrefix)) {
            const auto split=path.find('|');if(split==std::string::npos)return nullptr;
            const int edge=std::clamp(std::atoi(path.c_str()+std::strlen(kThumbnailPrefix)),1,128);
            const auto sourcePath=path.substr(split+1),thumb=ThumbnailFile(sourcePath,edge);
            // Reuse previously prepared full-cover mips. On a miss decode only
            // thumbnail size; never compress a 2048px scan to make a tiny icon.
            // Preload ON prepares the sharp on-disk working textures as well.
            // One-time conversion is reused on subsequent launches and revisits.
            SDL_Surface *packed=nullptr;
            if(packedCovers_.load()) {
                const auto file=CoverTextureCache::File(sourcePath);
                if(!file.empty())packed=CoverTextureCache::Read(file+".native1024",edge);
                if(!packed)packed=CoverTextureCache::Read(file,edge);
                if(!packed) {
                    auto *sharp=DecodeCover(sourcePath,1024);if(sharp)SDL_FreeSurface(sharp);
                    if(!file.empty())packed=CoverTextureCache::Read(file+".native1024",edge);
                }
            }
            if(!packed)packed=CoverTextureCache::Read(thumb,edge);
            if(!packed)packed=CoverTextureCache::Read(CoverTextureCache::File(sourcePath),edge);
            if(!packed) {
                auto *small=DecodeSource(sourcePath,edge);
                if(!small)return nullptr;
                packed=CoverTextureCache::Build(small,thumb,edge);SDL_FreeSurface(small);
            }
            if(!packed)return nullptr;
            auto *surface=CoverTextureCache::Expand(packed);SDL_FreeSurface(packed);
            if(surface && std::max(surface->w,surface->h)>edge) {
                const double scale=double(edge)/std::max(surface->w,surface->h);
                auto *small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*scale)),std::max(1,int(surface->h*scale)),32,SDL_PIXELFORMAT_RGBA32);
                SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);
                if(!small || SDL_BlitScaled(surface,nullptr,small,nullptr)!=0){if(small)SDL_FreeSurface(small);SDL_FreeSurface(surface);return nullptr;}
                SDL_FreeSurface(surface);surface=small;
            }
            return surface;
        }
        return DecodeSource(path);
    }
    static SDL_Surface *DecodeCover(const std::string &path,unsigned edge,bool prepareOnly=false,bool persist=false) {
        if(packedCovers_.load() && !prepareOnly) {
            const auto original=CoverTextureCache::File(path);
            const auto cached=original.empty()?std::string():original+(edge>1024?".native2048":".native1024");
            if(auto *ready=CoverTextureCache::Read(cached,edge))return ready;
            if(auto *ready=CoverTextureCache::Read(original,edge))return ready;
            auto *decoded=DecodeSource(path,edge>1024?2048:1024);if(!decoded)return nullptr;
            auto *ready=CoverTextureCache::Build(decoded,cached,edge);SDL_FreeSurface(decoded);
            return ready;
        }
        const auto display=CoverDisplayCache::File(path,edge);
        if(!prepareOnly)if(auto *ready=CoverDisplayCache::Read(display,edge))return ready;
        const auto file=CoverTextureCache::File(path);
        auto *packed=CoverTextureCache::Read(file,edge);
        SDL_Surface *surface=nullptr;
        if(!packed) {
            if(!prepareOnly)surface=DecodeSource(path,edge);
            else {
                auto *source=DecodeSource(path);if(!source)return nullptr;
                packed=CoverTextureCache::Build(source,file,edge);SDL_FreeSurface(source);
            }
        }
        if(prepareOnly)return packed;
        if(packed){surface=CoverTextureCache::Expand(packed);SDL_FreeSurface(packed);}
        if(surface && std::max(surface->w,surface->h)>int(edge)) {
            const double scale=double(edge)/std::max(surface->w,surface->h);
            auto *small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*scale)),std::max(1,int(surface->h*scale)),32,SDL_PIXELFORMAT_RGBA32);
            SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);
            if(!small || SDL_BlitScaled(surface,nullptr,small,nullptr)!=0){if(small)SDL_FreeSurface(small);SDL_FreeSurface(surface);return nullptr;}
            SDL_FreeSurface(surface);surface=small;
        }
        // Visible misses return immediately after decode. Compression and SD
        // writes run only as an explicit idle job, after the sharp row is ready.
        if(persist)CoverDisplayCache::Save(display,surface);
        return surface;
    }
    static SDL_Surface *DecodeSource(const std::string &path,int targetEdge=0) {
#ifdef UFLOW_HOST_PREVIEW
        ++sourceDecodes_;
#endif
        SDL_Surface *surface = nullptr;
        const auto dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension == "png") surface = LoadPng(path,targetEdge);
        else if (extension == "tga") surface = LoadTga(path);
        else if (extension == "jpg" || extension == "jpeg") surface = LoadJpeg(path,targetEdge);
        if(surface && targetEdge>0 && std::max(surface->w,surface->h)>targetEdge) {
            const double scale=double(targetEdge)/std::max(surface->w,surface->h);
            auto *small=SDL_CreateRGBSurfaceWithFormat(0,std::max(1,int(surface->w*scale)),
                std::max(1,int(surface->h*scale)),32,SDL_PIXELFORMAT_RGBA32);
            SDL_SetSurfaceBlendMode(surface,SDL_BLENDMODE_NONE);
            if(!small || SDL_BlitScaled(surface,nullptr,small,nullptr)!=0) {
                if(small) SDL_FreeSurface(small);SDL_FreeSurface(surface);return nullptr;
            }
            SDL_FreeSurface(surface);surface=small;
        }
        return surface;
    }
    TextureInfo Load(const std::string &path) { return Upload(path, Decode(path)); }
    TextureInfo Upload(const std::string &path, SDL_Surface *surface,bool thumbnail=false) {
        ++revision_;
        TextureInfo result;
        std::size_t bytes = 0;
        if(CoverTextureCache::Packed(surface)) {
            result.width=CoverTextureCache::Width(surface);result.height=CoverTextureCache::Height(surface);
            bytes=CoverTextureCache::Bytes(result.width,result.height);
            MakeRoom(bytes);
            if(bytes_+bytes<=kMaxBytes) {
                result.texture=SDL_CreateTexture(renderer_,SDL_FOURCC('D','X','T','5'),SDL_TEXTUREACCESS_STATIC,result.width,result.height);
                if(result.texture && SDL_UpdateTexture(result.texture,nullptr,
                    static_cast<unsigned char*>(surface->pixels)+CoverTextureCache::Header,
                    ((result.width+3)/4)*16)!=0) {SDL_DestroyTexture(result.texture);result.texture=nullptr;}
#ifndef UFLOW_HOST_PREVIEW
                if(result.texture)bytes=UFlow_BC3TextureBytes(result.texture);
#endif
                MakeRoom(bytes);
                if(result.texture && bytes_+bytes>kMaxBytes){SDL_DestroyTexture(result.texture);result.texture=nullptr;}
            }
            if(result.texture) {
                SDL_SetTextureBlendMode(result.texture,SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(result.texture,SDL_ScaleModeLinear);bytes_+=bytes;
            } else bytes=0;
            SDL_FreeSurface(surface);
            textures_[path]={result,SDL_GetTicks(),bytes};return result;
        }
        if(thumbnail) warming_.erase(path);
        const bool retain=thumbnail && (!surface || (surface->w<=thumbnailEdge_ && surface->h<=thumbnailEdge_ &&
            residentBytes_+std::size_t(surface->w)*surface->h*4<=kResidentBytes));
        if (surface) {
            result.width = surface->w; result.height = surface->h;
            bytes = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 4;
            if (!retain) MakeRoom(bytes);
            result.texture = bytes <= (retain?kResidentBytes:kMaxBytes) && (retain || bytes_+bytes<=kMaxBytes) ? SDL_CreateTextureFromSurface(renderer_, surface) : nullptr;
            if (result.texture) {
                SDL_SetTextureBlendMode(result.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(result.texture, SDL_ScaleModeLinear);
                if (retain) residentBytes_+=bytes; else bytes_ += bytes;
            } else {
                bytes = 0;
            }
            SDL_FreeSurface(surface);
        }
        if (retain) { resident_[path]=result; return result; }
        MakeRoom(0);
        textures_[path] = {result, SDL_GetTicks(), bytes};
        return result;
    }
    std::uint64_t revision_=0;
    bool highDetail_=true;
    SDL_Renderer *renderer_;
    std::unique_ptr<ArtworkWorker> worker_;
    std::unordered_map<std::string, CachedTexture> textures_;
    std::unordered_map<std::string, TextureInfo> resident_;
    std::vector<std::string> warmPaths_;
    std::unordered_set<std::string> warming_;
    std::size_t residentBytes_=0,warmCursor_=0;
    std::vector<std::string> pending_,urgentThumbnails_;
    std::string pinnedBackground_;
    std::string pinnedCover_;
    std::vector<std::string> nearbyCovers_;
    std::size_t bytes_ = 0;
};

class Dashboard {
    enum class DraftExit { None, Editor, Text, Category, Settings };
public:
    Dashboard(SDL_Renderer *renderer, std::vector<GameEntry> &games)
        : renderer_(renderer), games_(games), textures_(renderer), preferences_(LoadSettings()),
          sources_(LoadContentSources()), mediaSources_(LoadMediaSources()) {
        LoadSkins();
        LoadProfile();
        userStats_.Load("fs:/vol/external01/wiiu/apps/uFlow/user-stats-"+std::to_string(profileId_)+".cfg");
        sourceMediaRefreshPending_=MediaRefreshNeeded();
        history_.Load(kHistoryPath);
        if(RefreshInstallStatus()&&MergeInstalledPackages(games_))SaveLibraryCache(games_);
        sceneTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
        blurTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, 192, 108);
        blurScratch_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, 192, 108);
        lensTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
        if (lensTexture_) SDL_SetTextureBlendMode(lensTexture_, SDL_BLENDMODE_BLEND);
        // Size buckets keep small carousel cards from clearing the details-size
        // target. Each is allocated once; retain real clears for GX2 alpha testing.
        for (std::size_t i=0; i<caseTextures_.size(); ++i) {
            auto &texture = caseTextures_[i];
            texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET, kCaseTargetSizes[i].x, kCaseTargetSizes[i].y);
            if (!texture) continue;
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
            // Rendering onto transparent black produces premultiplied RGB.
            // Preserve that when compositing: ordinary alpha blending adds dark rims.
            const auto premultiplied = SDL_ComposeCustomBlendMode(
                SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
            if (SDL_SetTextureBlendMode(texture, premultiplied) != 0) {
                SDL_DestroyTexture(texture); texture = nullptr;
            }
        }
#ifdef UFLOW_HOST_PREVIEW
        SDL_Log("Case 2x supersampling buckets: %d/%zu", int(std::count_if(caseTextures_.begin(),caseTextures_.end(),[](auto *p){return p!=nullptr;})),caseTextures_.size());
#endif
        if (sceneTexture_) SDL_SetTextureBlendMode(sceneTexture_, SDL_BLENDMODE_NONE);
        if (sceneTexture_) SDL_SetTextureScaleMode(sceneTexture_, SDL_ScaleModeLinear);
        if (blurScratch_) SDL_SetTextureScaleMode(blurScratch_, SDL_ScaleModeLinear);
        if (blurTexture_) {
            SDL_SetTextureBlendMode(blurTexture_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(blurTexture_, SDL_ScaleModeLinear);
        }
        ApplyVCCategories(games_,preferences_,sources_);
        libraryTabs_=AvailableLibraryTabs(games_,libraries_,preferences_);
        welcomeOpen_=!preferences_.welcomeDismissed;
#ifdef UFLOW_HOST_PREVIEW
        welcomeOpen_=false; // normal visual fixtures stay independent of first-run state
#endif
        tab_ = StartupLibraryIndex(LibraryTabs(),preferences_);
        if(preferences_.startLibrary.empty())preferences_.startLibrary=CurrentLibraryId();
        visualTab_ = static_cast<float>(tab_);
        RebuildVisible();
        PrepareBuiltInIcons();
#ifndef UFLOW_HOST_PREVIEW
        startupIntroPending_=preferences_.startupVideo;
        musicTracks_=ListMusicTracks();
        ApplyMusic();
#endif
    }
    ~Dashboard() {
#ifndef UFLOW_HOST_PREVIEW
        if(networkInitialized_)ACFinalize();
#endif
        SDL_DestroyTexture(profileTexture_);
        scanWorker_.Stop();
        browserWorker_.reset(); // join directory I/O before storage teardown
        video_.Stop();detailsVideo_.Stop();
        glassBackground_.Stop();
        FlushSettingsSave(true);
        music_.Stop();
        menuSounds_.Stop();
        ClearSideCases();
        for (auto *texture : caseTextures_) if (texture) SDL_DestroyTexture(texture);
        if (bookshelfTexture_) SDL_DestroyTexture(bookshelfTexture_);
        if (lensTexture_) SDL_DestroyTexture(lensTexture_);
        if (blurScratch_) SDL_DestroyTexture(blurScratch_);
        if (blurTexture_) SDL_DestroyTexture(blurTexture_);
        if (sceneTexture_) SDL_DestroyTexture(sceneTexture_);
        if (preparationLogo_) SDL_DestroyTexture(preparationLogo_);
        if (preparationLogoGlow_) SDL_DestroyTexture(preparationLogoGlow_);
        if (startupFrame_) SDL_DestroyTexture(startupFrame_);
    }

    void ReplaceGames(std::vector<GameEntry> games) {
        StopVideo(); videoSelection_ = nullptr; videoTrigger_.Focus("");
        ClearSideCases();
        detailsBackdropReady_ = false;
        coverInspectOpen_ = coverInspectBack_ = false;
        coverInspectProgress_ = coverInspectBackProgress_ = 0.f;
        textures_.Clear(); games_ = std::move(games); RefreshInstallStatus(); selected_ = 0; visualSelected_ = 0.0f; RebuildVisible();
        PrepareBuiltInIcons();
    }
    void PrepareBuiltInIcons() {
        if (!preferences_.preloadBuiltInCovers) return;
        std::vector<std::string> paths;
        for (const auto &game:games_) paths.push_back(!game.fullCoverPath.empty()?game.fullCoverPath:
            !game.coverPath.empty()?game.coverPath:game.defaultSleevePath);
        textures_.PrepareIcons(paths);
        iconPreparationSkipped_=false;
    }
    bool PreparingIcons() const { return !iconPreparationSkipped_ &&
        (startupIntroPending_ || textures_.PreparingIcons()); }
    void SkipIconPreparation() { iconPreparationSkipped_=true; if(startupIntroPending_)FinishStartupIntro(); }
    // Reversible fixtures: never scan, launch, or save settings while previewing.
    bool VisualPreview(const std::string &screen) {
        settingsAction_=-1;
        welcomeOpen_=false;
        StopVideo(); videoSelection_ = nullptr; videoTrigger_.Focus("");
        ClearSideCases();
        detailsBackdropReady_ = false;
        if (screen == "live") {
            if (preview_) { games_ = std::move(liveGames_); preferences_ = livePreferences_; sources_ = std::move(liveSources_); mediaSources_=std::move(liveMediaSources_); }
            preview_ = false;
        } else {
            const bool vcProfile=screen.rfind("launcher",0)==0&&VCSystems::Find(screen.substr(8));
            const bool n64Case=screen.rfind("n64case",0)==0;
            if (screen != "detailspixel" && screen != "detailsmediapixel" && screen != "detailslauncherpixel" && screen != "favoriteglass" && screen != "favoritepixel" && screen != "detailsinstall50" && screen != "detailsinstallpixel50" && screen != "detailspixel50" && screen != "detailsinstall" && screen != "detailsinstalled" && screen != "detailsinstallpixel" && screen != "coverfront" && screen != "coverback" && screen != "detailsinject" && screen != "detailsinjected" && screen != "detailslauncher" && screen != "library" && screen != "snescase" && screen != "libraryfadehalf" && screen != "libraryfadezero" && screen != "navtransition" && screen != "details" && screen != "settings" && screen != "directories" &&
                !vcProfile && !n64Case && screen.rfind("snescase",0)!=0 && screen != "launchers" && screen != "launcheredit" && screen != "launchervc" && screen != "missing" && screen != "sources" && screen != "audio" && screen != "media" && screen != "mediaedit" && screen != "sourceedit" && screen != "mediaguide" && screen != "details25" && screen != "details50" && screen != "details75" && screen != "detailssource" && screen != "detailsmedia" && screen != "detailsinfo" && screen != "detailspixel") return false;
            if (!preview_) { liveGames_ = games_; livePreferences_ = preferences_; liveSources_ = sources_; liveMediaSources_=mediaSources_; }
            preview_ = true;
            preferences_ = UserSettings{}; preferences_.skin = 1; preferences_.backgroundMotion = false;
            games_.clear();
            for (int index = 0; index < 7; ++index) {
                GameEntry game;
                game.name = index == 3 ? "Glass / visual study" : "Sample title " + std::to_string(index + 1);
                game.publisher = "uFlow visual fixture"; game.genre = "Adventure";
                game.releaseYear = "2015"; game.players = "1-2"; game.region = "USA";
                game.description = "A fixed scene for checking typography, glass surfaces and cover animation. "
                    "These sample titles cannot be launched. Missing artwork uses the same case geometry as a real title.";
                const bool snesCase=screen.rfind("snescase",0)==0;
                if(snesCase || n64Case)game.libraryId=n64Case?"n64":"snes";
                // Stable artwork fixture installed alongside the development key, never a game scan.
#ifdef UFLOW_HOST_PREVIEW
                const std::string root = "/fixture/";
#else
                const std::string root = "fs:/vol/external01/wiiu/apps/uFlow/visual-fixture/";
#endif
                if (screen != "missing" && index != 5) game.coverPath = root + "cover.png";
                if (screen != "missing" && index != 5 && (!(snesCase || n64Case) || screen.find("frontonly")==std::string::npos)) game.fullCoverPath = root + "fullcover.png";
                if((snesCase || n64Case) && screen.find("default")!=std::string::npos){
                    game.coverPath.clear();game.fullCoverPath.clear();game.defaultSleevePath=root+"default.png";
                    game.defaultBackEnd=n64Case?.452f:.468099f;game.defaultFrontStart=n64Case?.55f:.561732f;
                }
                game.backgroundPath = root + "background.png";
#ifdef UFLOW_HOST_PREVIEW
                if(const char *logo=SDL_getenv("UFLOW_PREVIEW_LOGO")) {
                    game.logoPath=logo;
                    game.name="ARC STYLE: BASEBALL!!SP";
                }
#endif
                games_.push_back(std::move(game));
            }
            sources_ = {{Platform::WiiU, "fs:/vol/external01/wiiu/raw-games", true, 3}};
            sources_.push_back({Platform::WiiU, "fs:/vol/external01/wiiu/games", true, 3});
            sources_.push_back({Platform::Wii, "fs:/vol/external01/wbfs", true, 3});
            sources_.push_back({Platform::GameCube, "fs:/vol/external01/games", true, 3});
            mediaSources_={{MediaType::Cover,0,"fs:/vol/external01/wiiu/apps/uFlow/media/covers",true,0},
                           {MediaType::FullCover,0,"fs:/vol/external01/wiiu/apps/uFlow/media/fullcovers",true,0},
                           {MediaType::Background,-1,"fs:/vol/external01/media/backgrounds",true,2}};
        }
        tab_ = 0; visualTab_ = 0; selected_ = preview_ ? 3 : 0; visualSelected_ = static_cast<float>(selected_); tabSlide_ = 0;
        mediaMode_=screen=="media" || screen=="mediaedit" || screen=="mediaguide";
        mediaHelp_=screen=="mediaguide";
        details_ = screen.rfind("details", 0) == 0; settings_ = screen == "settings" || screen == "audio" || screen == "directories" || screen == "sources" || screen=="sourceedit" || mediaMode_;
        detailsRow_=screen=="detailssource"?3:(screen=="detailsmedia"||screen=="detailsmediapixel")?4:screen=="detailsinfo"?5:0; detailsPage_=0;
        detailsLauncherEditing_=false;
        detailsPanelFocus_=false;detailsPanelRow_=0;
        if(screen=="detailslauncher"||screen=="detailslauncherpixel")detailsRow_=2;
        if(screen=="detailspixel" || ((screen.rfind("snescase",0)==0 || screen.rfind("n64case",0)==0) && screen.find("pixel")!=std::string::npos)) preferences_.skin=0;
        directoryBrowser_ = screen == "directories"; sourceManager_ = sourceEditor_ = directoryBrowser_;
        sourceManager_ = sourceManager_ || screen == "sources" || screen=="sourceedit" || mediaMode_;
        settingsCategory_ = mediaMode_ ? 2 : sourceManager_ ? 1 : 0; settingsRow_ = 0; sourceRow_ = 0;
        settingsSidebar_=screen=="settings";
        settingsEditing_=false;
        defaultFileBrowser_=false;
        launcherEditor_=launcherBrowse_=false;vcMapMode_=0;typeManager_=typeEditor_=typeText_=false;
        if(screen.rfind("launcher",0)==0){settings_=true;settingsCategory_=5;launcherRow_=0;
            if(screen!="launchers"){launcherEditor_=true;launcherRow_=5;
                // The built-in list no longer holds VC presets; build the DS injector page.
                if(screen=="launchervc"){const auto *ds=VCSystems::Find("ds");
                    launcherDraft_={"ds-vc","Nintendo DS - VC injector",LauncherKind::DSInjection,ds->name,ds->extensions,"",true,"ds-vc"};
                    injectionDraft_={};injectionDraft_.system="ds";injectionDraft_.base="wiiu/vc-bases/ds/default";}
                else launcherDraft_=launchers_[std::min<std::size_t>(3,launchers_.size()-1)];}}
        for(const auto &system:VCSystems::all)if(screen==std::string("launcher")+system.id){
            launcherEditor_=true;launcherDraft_={std::string(system.id)+"-vc",std::string(system.name)+" - VC injector",LauncherKind::DSInjection,system.name,system.extensions,"",true,std::string(system.id)+"-vc"};
            injectionDraft_={};injectionDraft_.system=system.id;injectionDraft_.base=std::string("wiiu/vc-bases/")+system.id+"/default";launcherRow_=10;
        }
        audioSettings_=screen=="audio";
        if(mediaMode_) expandedMediaGroups_.clear();
        if(screen=="mediaedit") FocusMediaSource(0);
        if (screen=="sourceedit" || screen=="mediaedit") { if(!mediaMode_) sourceRow_=1; SourceActivate(); }
        browserPath_ = "fs:/vol/external01"; browserDirectories_ = {"install", "wiiu", "wbfs", "games"}; browserRow_ = 1;
        detailsProgress_ = details_ ? (screen == "details25" ? .25f : (screen == "details50" || screen=="detailsinstall50" || screen=="detailsinstallpixel50" || screen=="detailspixel50") ? .5f : screen == "details75" ? .75f : 1.f) : 0;
        settingsProgress_ = settings_ ? 1.f : 0;
        coverInspectOpen_ = screen == "coverfront" || screen == "coverback" || screen.rfind("snescase",0)==0 || screen.rfind("n64case",0)==0;
        coverInspectBack_ = screen == "coverback" || ((screen.rfind("snescase",0)==0 || screen.rfind("n64case",0)==0) && screen.find("back")!=std::string::npos);
        coverInspectProgress_ = coverInspectOpen_ ? 1.f : 0.f;
        coverInspectBackProgress_ = coverInspectBack_ ? 1.f : 0.f;
        // Preview starts on All, not the empty live catalog's Favorites tab.
        // Do not preserve a tab from the preceding synthetic scene.
        if(preview_)libraryTabs_.clear();
        status_.clear(); RebuildVisible();
        if((screen=="detailsinject"||screen=="detailsinjected")&&!visible_.empty()) {
            auto &g=games_[visible_[selected_]];g.platform=Platform::Files;g.libraryId="ds";
            g.absolutePath="fs:/vol/external01/nds/preview.nds";
            detailsRow_=2;detailsPanelFocus_=true;detailsPanelRow_=screen=="detailsinject"?1:0;
            savedInjectionState_[LauncherGameKey(g)]=screen=="detailsinjected";
            gameLaunchers_[LauncherGameKey(g)]=screen=="detailsinjected"?"ds-vc":"noods";
            launchers_.push_back({"ds-vc","Nintendo DS - Virtual Console",LauncherKind::DSInjection,"Nintendo DS",".nds,.srl","",true,"ds-vc"});
        }
        libraryFade_=1;libraryFadePhase_=0;pendingTab_=-1;
        if(screen=="libraryfadehalf" || screen=="libraryfadezero") {
            libraryFadePhase_=1;libraryFade_=screen=="libraryfadehalf"?.5f:0.f;
        }
        if(screen=="navtransition") visualTab_=1.5f;
        if(screen.rfind("detailsinstall",0)==0){
            auto &g=games_[visible_[selected_]];g.name="Captain Toad: Treasure Tracker";g.titleId="0005000010180600";
            for(int i=0;i<3;++i){TitleInstall::Package p;p.titleId=0x5000010180600ULL;p.kind=static_cast<TitleInstall::Kind>(i);p.bytes=i==0?1800000000:120000000;p.version=i==0?0:16;if(screen=="detailsinstalled")p.installedVersion[1]=p.version;g.installPackages.push_back(p);}
            installTarget_=1;installRow_=4;installSpace_="24.3 GB free";installMessage_=screen=="detailsinstalled"?"Installation complete. Source files kept.":"";
            if(screen=="detailsinstalled"){
                GameEntry installed=g;installed.installPackages.clear();installed.installed=true;installed.installedTitleId=0x5000010180600ULL;
                installed.libraryId="wiiu";installed.absolutePath="fs:/vol/storage_usb01/usr/title/00050000/10180600";
                games_.push_back(std::move(installed));installRow_=5;
            }
            if(screen.rfind("detailsinstallpixel",0)==0)preferences_.skin=0;
        }
        if(screen=="detailspixel50"||screen=="detailspixel"||screen=="detailsmediapixel"||screen=="detailslauncherpixel")preferences_.skin=0;
        if(screen=="favoriteglass"||screen=="favoritepixel") {preferences_.skin=screen=="favoriteglass"?1:0;games_[visible_[selected_]].favorite=true;}
        return true;
    }
    bool PreviewActive() const { return preview_; }
    void IsolateVideo(bool enabled) {
        // Authenticated A/B check only. Automatically restore the full UI;
        // no saved preferences, files, decoder settings or clocks change.
        videoIsolationUntil_=enabled?SDL_GetTicks()+45000:0;
    }
    std::string VideoStats() const {
        const auto stats=video_.Statistics();
        const auto *game=Selected();
        std::string id=game?game->titleId:"";
        if(id.size()>32 || id.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-")!=std::string::npos) id.clear();
        char result[768];
        std::snprintf(result,sizeof(result),
            "{\"title_id\":\"%s\",\"selection\":%zu,\"visible\":%zu,\"active\":%s,\"uptime_ms\":%u,"
            "\"decoded\":%u,\"shown\":%u,\"audio_frames\":%u,\"audio_missing\":%u,\"lag_ms\":%d,"
            "\"native_us\":%u,\"decode_us\":%u,\"convert_us\":%u,\"skip_nonref\":%u,\"late\":%u,\"loops\":%u,\"width\":%d,\"height\":%d,\"converted\":%u,\"upload_us\":%u,\"gpu_colour\":%s,\"native_planes\":%s}",
            id.c_str(),selected_,visible_.size(),video_.Active()?"true":"false",SDL_GetTicks(),
            stats.decoded,stats.presented,stats.audioFrames,stats.audioMissingFrames,stats.videoLagMs,
            stats.nativeDecodeUs,stats.decodeUs,stats.convertUs,stats.skippedNonreference,stats.lateDropped,stats.loops,stats.outputWidth,stats.outputHeight,
            stats.converted,stats.uploadUs,stats.gpuColour?"true":"false",stats.nativePlanes?"true":"false");
        return result;
    }
    std::string PerformanceStats() {
        // On-demand CPU allocator statistics, NOT total RAM or free GPU memory.
        // No disk writes/readback; fixed counters cannot grow during idle.
        const auto heap = mallinfo();
        char result[1024];
        std::snprintf(result,sizeof(result),
            "{\"uptime_ms\":%u,\"malloc_used_bytes\":%u,\"malloc_free_bytes\":%u,"
            "\"side_cases\":%zu,\"artwork_entries\":%zu,\"artwork_bytes\":%zu,\"artwork_pending\":%zu,\"text_entries\":%zu,"
            "\"resident_icons\":%zu,\"resident_icon_bytes\":%zu,\"icons_prepared\":%zu,\"icons_total\":%zu,"
            "\"compressed_covers\":%s,\"frames\":%u,\"render_mean_ms\":%.3f,\"render_max_ms\":%.3f,\"shelf_builds\":%u,\"shelf_cache_hits\":%u,\"shelf_faces\":%u,\"shelf_vertices\":%u}",
            SDL_GetTicks(),static_cast<unsigned>(heap.uordblks),static_cast<unsigned>(heap.fordblks),
            sideCases_.size(),textures_.Count(),textures_.Bytes(),textures_.Pending(),gSmoothTextCache.size(),
            textures_.ResidentCount(),textures_.ResidentBytes(),textures_.IconsDone(),textures_.IconsTotal(),textures_.NativeCovers()?"true":"false",performanceFrames_,
            performanceFrames_ ? performanceTotalMs_/performanceFrames_ : 0.,performanceMaxMs_,bookshelfBuilds_,bookshelfCacheHits_,bookshelfFaces_,bookshelfVertices_);
        performanceFrames_=0; performanceTotalMs_=0; performanceMaxMs_=0;
        return result;
    }
#ifdef UFLOW_HOST_PREVIEW
    bool CheckStartupAnimation(const std::string &output) {
        VisualPreview("library");startupIntroPending_=true;
        const auto began=SDL_GetTicks();int capture=0;bool heardAudio=false;
        const std::array<unsigned,5> times={1000,3500,6800,7500,8300};
        while(SDL_GetTicks()-began<9200) {
            std::string path;
            if(capture<5 && SDL_GetTicks()-began>=times[capture])path=output+"-"+std::to_string(capture++)+".png";
            if(!Render(0,path))return false;
            heardAudio=heardAudio || video_.Statistics().audioFrames>0;
            if(video_.Statistics().loops)return false;
            SDL_Delay(16);
        }
        if(!heardAudio)return false;
        if(!startupIntroReady_ || startupIntroPending_ || startupFrame_ || !video_.CanStart())return false;
        musicStatus_="already playing";SkipIconPreparation();
        if(musicStatus_!="already playing")return false; // cover-only skip must not restart music
        iconPreparationSkipped_=false;
        startupIntroPending_=true;startupIntroRequested_=startupIntroReady_=false;
        Render(0);SkipIconPreparation();Render(0);
        if(PreparingIcons() || video_.Active())return false;
        SDL_Log("Startup animation PASS: real packaged clip, single reveal, dissolve, library fade, decoder release and skip");
        return true;
    }
    bool CheckAlphabet(const std::string &output) {
        if(!VisualPreview("library"))return false;
        const auto fixture=games_.front();games_.assign(6,fixture);
        const char *names[]={"Alpha","  apple","Bomberman","Zelda","007 Legends","Mario"};
        for(size_t i=0;i<games_.size();++i){games_[i].name=names[i];games_[i].favorite=i==0;}
        tab_=0;searchQuery_.clear();searchFolded_.clear();alphabetFilter_=0;RebuildVisible();
        if(AlphabetBucket(" ")==0 || AlphabetBucket("  apple")!=2 || AlphabetBucket("007")!=1 || AlphabetBucket("Zelda")!=27)return false;
        const auto original=Selected();const auto originalList=visible_;
        OpenAlphabet();AlphabetInput(0,0,0,false,false,false,false);
        if(AlphabetOpen() || Selected()!=original || visible_!=originalList)return false;
        OpenAlphabet();AlphabetInput(.8f,0,10,true,false,false,false);
        if(alphabetChoice_!=2)return false;
        AlphabetInput(.8f,0,100,true,false,false,false);if(alphabetChoice_!=2)return false;
        AlphabetInput(0,0,110,false,false,false,false);
        if(Selected()->name!="  apple" || visible_!=originalList)return false;
        OpenAlphabet();AlphabetInput(0,0,120,true,true,false,false);
        if(!preferences_.alphabetFilterMode)return false;
        AlphabetInput(0,0,130,true,false,false,false,3);
        AlphabetInput(0,0,140,false,false,false,false);
        if(alphabetFilter_!=3 || visible_.size()!=1 || Selected()->name!="Bomberman")return false;
        OpenSearch();searchBuffer_="Alpha";ApplySearch();if(!visible_.empty())return false;
        OpenAlphabet();AlphabetInput(0,0,150,true,false,false,false,0);AlphabetInput(0,0,160,false,false,false,false);
        if(alphabetFilter_ || visible_.size()!=1 || searchQuery_!="Alpha")return false;
        OpenAlphabet(false);AlphabetInput(0,0,170,false,false,true,false,27);
        if(AlphabetOpen() || !visible_.empty())return false;
        FindOrClearSearch();if(alphabetFilter_ || searchQuery_!="Alpha" || visible_.size()!=1)return false;
        FindOrClearSearch();if(visible_.size()!=6)return false;
        const auto favorite=std::find(libraryTabs_.begin(),libraryTabs_.end(),kFavoritesLibraryId);
        if(favorite==libraryTabs_.end())return false;
        tab_=static_cast<int>(favorite-libraryTabs_.begin());RebuildVisible();
        OpenAlphabet();AlphabetInput(0,0,180,true,false,false,false,0);AlphabetInput(0,0,190,false,false,false,false);
        if(CurrentLibraryId()!=kFavoritesLibraryId || visible_.size()!=1)return false;
        tab_=0;RebuildVisible();preferences_.alphabetFilterMode=false;alphabetChoice_=1;
        OpenAlphabet();AlphabetInput(0,-1,200,true,false,false,false);if(alphabetChoice_!=27)return false;
        AlphabetInput(0,0,201,true,false,false,true);if(AlphabetOpen())return false;
        OpenAlphabet();if(alphabetChoice_!=27)return false;
        AlphabetInput(0,0,202,true,true,false,false);alphabetChoice_=0;AlphabetInput(0,-1,203,true,false,false,false);
        if(alphabetChoice_!=27)return false;
        for(int skin=0;skin<2;++skin)for(int mode=0;mode<2;++mode){
            preferences_.skin=skin;preferences_.alphabetFilterMode=mode!=0;OpenAlphabet();
            alphabetChoice_=3;alphabetVisual_=3;alphabetProgress_=1;
            if(!Render(0,output+"-alphabet-"+std::to_string(skin)+"-"+std::to_string(mode)+".png"))return false;
            AlphabetInput(0,0,210,true,false,false,true);alphabetProgress_=0;
        }
        OpenAlphabet(false);AlphabetInput(0,0,220,false,false,false,true);
        if(AlphabetOpen())return false;
        preferences_.bindings[static_cast<int>(ControlAction::Alphabet)]=0x0040;
        if(!ButtonPressed(ControlAction::Alphabet,0x0040) || ButtonPressed(ControlAction::Alphabet,0x20000) || !ButtonHeld(ControlAction::Alphabet,0x0040))return false;
        OpenAlphabet();if(AlphabetConsumedBinding()!=0x0040)return false;
        AlphabetInput(0,0,230,false,false,false,false);alphabetProgress_=0;
        for(int skin=0;skin<2;++skin){
            preferences_.skin=skin;settings_=true;settingsCategory_=4;settingsSidebar_=false;
            settingsRow_=static_cast<int>(ControlAction::Alphabet);settingsProgress_=1;
            if(settingsRow_-SettingsFirstRow()>=12 || !Render(0,output+"-controls-"+std::to_string(skin)+".png"))return false;
        }
        settings_=false;ResetControlBindings(preferences_);
        if(preferences_.bindings[static_cast<int>(ControlAction::Alphabet)]!=0x20000)return false;
        SDL_Log("Alphabet PASS: release/cancel, jump without reorder, filtered/search/Favorites intersection, ALL preservation, wrap, mode switch, repeat and both skins");
        return true;
    }
    bool CheckSearch(const std::string &output) {
        if(!VisualPreview("library") || games_.size()<3)return false;
        const auto fixture=games_.front();games_.assign(3,fixture);
        games_[0].name="Super Mario World";games_[1].name="Mario Kart 8";games_[2].name="Bomberman 94";
        tab_=0;RebuildVisible();selected_=static_cast<size_t>(std::find(visible_.begin(),visible_.end(),2)-visible_.begin());
        OpenSort();SortInput(0,false,false,-1,-1,true);
        if(!SearchOpen() || SortOpen() || !OverlayOpen())return false;
        searchBuffer_="  mArIo  ";SearchInput(0,0,false,false,false,true);
        if(visible_.size()!=2 || searchQuery_!="mArIo" || selected_!=0 || visualSelected_!=0)return false;
        OpenSearch();searchBuffer_="unmatched";SearchInput(0,0,false,true,false,false);
        if(searchQuery_!="mArIo" || visible_.size()!=2)return false;
        ApplySort(1);if(visible_.size()!=2)return false;
        OpenSearch();SearchInput(0,0,false,false,false,false,true);
        SearchInput(0,0,false,false,false,false,false,340+60+10,300+10);
        if(searchBuffer_!="B")return false;
        SearchInput(0,0,false,false,true,false);if(!searchBuffer_.empty())return false;
        searchBuffer_="missing title";SearchInput(0,0,false,false,false,true);
        if(!visible_.empty() || Selected()!=nullptr || LibraryPosition()!="0/0")return false;
        for(int skin=0;skin<2;++skin){
            preferences_.skin=skin;
            if(!Render(0,output+"-empty-"+std::to_string(skin)+".png"))return false;
            OpenSearch();searchBuffer_="MARIO";
            if(!Render(0,output+"-keyboard-"+std::to_string(skin)+".png"))return false;
            SearchInput(0,0,false,true,false,false);
        }
        OpenSearch();SearchInput(0,0,false,false,false,false,true);
        SearchInput(0,0,false,false,false,false,false,650,510);
        if(SearchOpen() || !searchQuery_.empty() || visible_.size()!=3)return false;
        OpenSearch();searchBuffer_="world";ApplySearch();
        if(visible_.size()!=1 || Selected()->name!="Super Mario World")return false;
        const auto selected=Selected();OpenSearch();searchBuffer_="mario";ApplySearch();
        if(Selected()!=selected)return false;
        games_[0].favorite=true;games_[1].favorite=false;games_[2].favorite=false;
        const auto favoriteTab=std::find(libraryTabs_.begin(),libraryTabs_.end(),kFavoritesLibraryId);
        if(favoriteTab==libraryTabs_.end())return false;
        tab_=static_cast<int>(favoriteTab-libraryTabs_.begin());RebuildVisible();
        if(visible_.size()!=1 || Selected()->name!="Super Mario World")return false;
        games_[0].favorite=false;RebuildVisible();if(!visible_.empty())return false;
        tab_=0;RebuildVisible();if(visible_.size()!=2)return false;
        const auto retained=Selected();UpdateControlHints();
        if(footerHints_.back().action!="CLEAR")return false;
        FindOrClearSearch();UpdateControlHints();
        if(!searchQuery_.empty() || visible_.size()!=3 || Selected()!=retained || SortOpen() || footerHints_.back().action!="FIND")return false;
        FindOrClearSearch();if(!SortOpen())return false;SortInput(0,false,true);
        OpenSearch();searchBuffer_="nothing matches";ApplySearch();FindOrClearSearch();
        if(visible_.size()!=3 || SortOpen())return false;
        OpenSearch();SearchNavigate(.2f,0,0,0,100);if(searchKey_!=0)return false;
        SearchNavigate(.8f,0,0,0,110);if(searchKey_!=1)return false;
        SearchNavigate(.8f,0,0,0,459);if(searchKey_!=1)return false;
        SearchNavigate(.8f,0,0,0,460);if(searchKey_!=2)return false;
        SearchNavigate(.35f,0,0,0,580);if(searchKey_!=3)return false;
        SearchNavigate(.1f,0,0,0,700);if(searchKey_!=3)return false;
        SearchNavigate(.35f,0,0,0,800);if(searchKey_!=3)return false;
        SearchNavigate(-.8f,0,0,0,810);if(searchKey_!=2)return false;
        SearchNavigate(0,-.8f,0,0,820);if(searchKey_!=12)return false;
        SearchNavigate(0,.8f,0,0,830);if(searchKey_!=2)return false;
        SearchNavigate(0,0,-1,0,840);if(searchKey_!=1)return false;
        SearchInput(0,0,false,true,false,false);OpenSearch();
        SearchNavigate(.8f,0,0,0,850);if(searchKey_!=1)return false;
        SearchInput(0,0,false,true,false,false);
        SDL_Log("Search PASS: case-insensitive substring, trim, menu, keyboard touch, cancel, erase, clear, no matches, selection, Favorites, sort and both skins");
        return true;
    }
    bool CheckSorting(const std::string &output) {
        VisualPreview("library");
        settingsCategory_=3;settingsRow_=14;sourceManager_=false;audioSettings_=false;
        if(visible_.size()<2 || SettingsFirstRow()!=9 || !SettingsSelectorRow(14))return false;
        const auto selected=visible_[selected_];
        for(int mode=0;mode<LibrarySort::Count;++mode) {
            SettingsAdjust(1);
            if(visible_[selected_]!=selected || visualSelected_!=float(selected_))return false;
        }
        for(int skin=0;skin<2;++skin){
            preferences_.skin=skin;settings_=true;settingsSidebar_=false;settingsEditing_=true;
            preferences_.sortMode=2;
            if(!Render(0,output+"-"+std::to_string(skin)+".png"))return false;
        }
        settings_=false;preferences_.sortMode=0;
        OpenSort();SortInput(1,false,true);if(SortOpen()||preferences_.sortMode!=0)return false;
        OpenSort();SortInput(2,true,false);if(SortOpen()||preferences_.sortMode!=2)return false;
        OpenSort();SortInput(0,false,false,640,190+4*48+10);if(SortOpen()||preferences_.sortMode!=4)return false;
        for(int skin=0;skin<2;++skin){
            preferences_.skin=skin;OpenSort();sortChoice_=2;
            if(!Render(0,output+"-popup-"+std::to_string(skin)+".png"))return false;
            SortInput(0,false,true);
        }
        SDL_Log("Sorting PASS: all modes, selection retained and scrollable settings in both skins");
        return true;
    }
    bool CheckIconPreparation(const std::string &output) {
        auto *sample=SDL_CreateRGBSurfaceWithFormat(0,4,4,32,SDL_PIXELFORMAT_RGBA32);
        if(!sample)return false;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
            auto *p=static_cast<Uint8 *>(sample->pixels)+y*sample->pitch+x*4;
            const bool opaque=(x+y)%2==0;
            p[0]=opaque?255:0;p[1]=0;p[2]=opaque?0:255;p[3]=opaque?255:0;
        }
        sample=FilterUiImage(sample,2);
        bool filtered=sample->w==2 && sample->h==2;
        for(int y=0;y<sample->h;++y)for(int x=0;x<sample->w;++x) {
            const auto *p=static_cast<const Uint8 *>(sample->pixels)+y*sample->pitch+x*4;
            filtered=filtered && p[0]==255 && p[1]==0 && p[2]==0 && p[3]==128;
        }
        SDL_FreeSurface(sample);if(!filtered)return false;
        for(int skin=0;skin<2;++skin) for(int theme=0;theme<4;++theme) {
            preferences_.skin=skin; preferences_.theme=theme;
            for(int frame=0;frame<7;++frame) {
                preferences_.backgroundMotion=frame!=0;
                constexpr std::size_t completed[]={0,1,335,481,669,1004,1338};
                DrawIconPreparation(completed[frame],1338,frame?2.5f:0.f);
                if(!SaveVisualFrame(renderer_,output+"-"+std::to_string(skin)+"-"+std::to_string(theme)+"-"+std::to_string(frame)+".png"))return false;
                SDL_RenderPresent(renderer_);
            }
            // Same progress must keep the logo AND marker still as time changes,
            // including when decorative background motion is disabled.
            const SDL_Rect region{450,80,380,360};
            std::vector<Uint32> baseline(region.w*region.h),current(baseline.size());
            preferences_.backgroundMotion=true;DrawIconPreparation(481,1338,0.f);
            if(SDL_RenderReadPixels(renderer_,&region,SDL_PIXELFORMAT_RGBA32,baseline.data(),region.w*4))return false;
            for(bool motion:{true,false}) {
                preferences_.backgroundMotion=motion;DrawIconPreparation(481,1338,3.7f);
                if(SDL_RenderReadPixels(renderer_,&region,SDL_PIXELFORMAT_RGBA32,current.data(),region.w*4) || current!=baseline)return false;
            }
        }
        if(!preparationLogo_ || !preparationLogoGlow_)return false;
        int logoW=0,logoH=0;SDL_QueryTexture(preparationLogo_,nullptr,nullptr,&logoW,&logoH);
        if(logoW!=310 || logoH!=310)return false;
        settingsCategory_=3;settingsRow_=12;audioSettings_=false;
        preferences_.preloadBuiltInCovers=true;
        if(SettingsRowCount()!=16 || !SettingsSelectorRow(12))return false;
        SettingsAdjust(1);if(preferences_.preloadBuiltInCovers)return false;
        SettingsAdjust(-1);if(!preferences_.preloadBuiltInCovers)return false;
        settingsRow_=13;preferences_.startupVideo=true;
        SettingsAdjust(1);if(preferences_.startupVideo || !preferences_.preloadBuiltInCovers)return false;
        SettingsAdjust(-1);if(!preferences_.startupVideo)return false;
        VisualPreview("settings");settingsCategory_=3;sourceManager_=false;settingsRow_=13;
        if(!Render(0,output+"-settings.png"))return false;
        SDL_Log("Preparation PASS: filtered alpha, stationary logo/glow and progress marker, both skins, accents, motion on/off, settings toggle");
        return true;
    }
    bool CheckScanDialog(const std::string &output) {
        VisualPreview("settings");scanApplied_=true;scanOpen_=true;
        for(int skin=0;skin<2;++skin){
            preferences_.skin=skin;
            scanState_={};scanState_.progress={"SCANNING SOURCE 1 OF 3","usb1:/Decrypted/A very long game folder/code/app.xml",173,0,0};scanElapsed_=82;
            ScanInput(true);if(!scanOpen_)return false;
            if(!Render(0,output+"-"+std::to_string(skin)+"-discovery.png"))return false;
            CancelScan();if(!scanCancelling_ || !scanOpen_)return false;
            if(!Render(0,output+"-"+std::to_string(skin)+"-cancel.png"))return false;
            scanCancelling_=false;
            scanState_.progress={"MATCHING ARTWORK","usb1:/Decrypted/Game",1000,350,1000};
            if(!Render(0,output+"-"+std::to_string(skin)+"-artwork.png"))return false;
            scanState_.finished=true;scanState_.cacheSaved=true;scanState_.progress={"SCAN COMPLETE","",1000,1,1};
            if(!Render(0,output+"-"+std::to_string(skin)+"-complete.png"))return false;
            scanState_.cacheSaved=false;
            if(!Render(0,output+"-"+std::to_string(skin)+"-cache-warning.png"))return false;
            scanState_.error="SCAN FAILED - LIBRARY UNCHANGED";
            if(!Render(0,output+"-"+std::to_string(skin)+"-error.png"))return false;
        }
        ScanInput(true);if(scanOpen_)return false;
        const auto beforeCancel=games_.size();
        StartScan();if(!scanStartPending_)return false;
        CancelScan();PollScan();
        if(scanStartPending_ || !scanState_.finished || games_.size()!=beforeCancel)return false;
        ScanInput(true);if(scanOpen_)return false;
        // Exercise the actual worker-to-dashboard handoff, including cache-save
        // result and opening the modal without requiring the settings screen.
        StartScan();if(!ScanOpen())return false;
        const auto start=SDL_GetTicks();
        while(!scanApplied_ && SDL_GetTicks()-start<10000){PollScan();SDL_Delay(1);}
        if(!scanApplied_ || !scanState_.finished || !scanState_.error.empty())return false;
        ScanInput(true);if(ScanOpen())return false;
        SDL_Log("PASS: scan popup discovery, measured artwork progress, success, cache warning, error, modal input and both skins");return true;
    }
    bool CheckAsyncBrowser(const std::string &output) {
        VisualPreview("directories");
        browserWorker_=std::make_unique<BrowserWorker>();
        browserPath_="fs:/vol/external01/browser-large-test";
        std::filesystem::create_directories(browserPath_+"/Child");
        for(int i=0;i<1000;++i)std::ofstream(browserPath_+"/Game-"+std::to_string(i)+".nes").put('x');
        RefreshBrowser();
        if(!browserLoading_ || browserDirectories_!=std::vector<std::string>{"../"})return false;
        Render(0,output+"-loading.png");
        auto wait=[&]{const auto start=SDL_GetTicks();while(browserLoading_ && SDL_GetTicks()-start<3000){PumpBrowser();SDL_Delay(1);}return !browserLoading_;};
        if(!wait() || !browserError_.empty() || browserDirectories_!=std::vector<std::string>{"../","Child"})return false;
        Render(0,output+"-ready.png");
        browserRow_=1;BrowserOpenSelected();if(!browserLoading_)return false;
        BrowserBack();if(!wait() || browserDirectories_.size()!=2)return false;
        RefreshBrowser();BrowserCancel();SDL_Delay(5);PumpBrowser();
        if(browserLoading_ || directoryBrowser_)return false;
        directoryBrowser_=true;browserPath_+="/missing";RefreshBrowser();
        if(!wait() || browserError_.empty())return false;
        Render(0,output+"-error.png");BrowserCancel();
        SDL_Log("PASS: asynchronous browser UI, 1,000 files, parent/cancel while loading, errors");return true;
    }
    bool CheckAsyncArtwork(const std::string &output) {
        png_image image{}; image.version = PNG_IMAGE_VERSION;
        image.width = 8; image.height = 8; image.format = PNG_FORMAT_RGBA;
        std::array<unsigned char,8*8*4> pixels{}; pixels.fill(255);
        const std::string path = output + "-worker.png";
        if (!png_image_write_to_file(&image,path.c_str(),0,pixels.data(),0,nullptr)) return false;
        TextureCache cache(renderer_,true);
        if (cache.Get(path).texture) return false; // first request never decodes inline
        const auto deadline = SDL_GetTicks() + 3000;
        while (!cache.Known(path) && !SDL_TICKS_PASSED(SDL_GetTicks(),deadline)) {
            cache.BeginFrame(); cache.Prefetch(path); cache.PumpOne(); SDL_Delay(1);
        }
        if (!cache.Get(path).texture || cache.Bytes() != 8*8*4) return false;
        const auto count = cache.Count();
        for (int i=0;i<60;++i) { cache.BeginFrame(); cache.Get(path); cache.PumpOne(); }
        if (cache.Count()!=count || cache.Pending()!=0) return false;
        cache.PinBackground(path);
        TextureInfo pinned{};
        const auto backgroundDeadline=SDL_GetTicks()+3000;
        while(!(pinned=cache.GetBackground(path)).texture && !SDL_TICKS_PASSED(SDL_GetTicks(),backgroundDeadline)) {
            cache.PumpOne();cache.BeginFrame();SDL_Delay(1);
        }
        if(!pinned.texture)return false;
        cache.Clear();
        if (cache.Count() || cache.Bytes()) return false;
        std::vector<std::string> icons;
        for(int i=0;i<150;++i) {
            const auto icon=output+"-resident-"+std::to_string(i)+".png";
            if(!png_image_write_to_file(&image,icon.c_str(),0,pixels.data(),0,nullptr)) return false;
            icons.push_back(icon);
        }
        icons.push_back(output+"-missing-icon.png");
        icons.push_back(icons.front()); // duplicate games share one resident texture
        cache.PrepareIcons(icons);
        const auto warmDeadline=SDL_GetTicks()+10000;
        while(cache.PreparingIcons() && !SDL_TICKS_PASSED(SDL_GetTicks(),warmDeadline)) {
            cache.BeginFrame(); cache.PumpOne(); SDL_Delay(1);
        }
        if(cache.PreparingIcons() || cache.IconsTotal()!=151 || cache.ResidentCount()!=151 ||
           cache.ResidentBytes()!=150*8*8*4 || cache.HasTexture(icons[150]) || !cache.Known(icons[150])) return false;
        cache.PinBackground(path);
        const auto pinDeadline=SDL_GetTicks()+3000;
        while(!(pinned=cache.GetBackground(path)).texture && !SDL_TICKS_PASSED(SDL_GetTicks(),pinDeadline)) {
            cache.PumpOne();cache.BeginFrame();SDL_Delay(1);
        }
        if(!pinned.texture)return false;
        // Scanned art churns the ordinary 128-entry cache; native fronts must survive.
        for(int i=0;i<150;++i) {
            const auto scan=output+"-scan-"+std::to_string(i)+".png";
            if(!png_image_write_to_file(&image,scan.c_str(),0,pixels.data(),0,nullptr)) return false;
            const auto until=SDL_GetTicks()+3000;
            while(!cache.Known(scan) && !SDL_TICKS_PASSED(SDL_GetTicks(),until)) {
                cache.BeginFrame();cache.Prefetch(scan);cache.PumpOne();SDL_Delay(1);
            }
            if(!cache.HasTexture(scan)) return false;
        }
        if(cache.GetBackground(path).texture!=pinned.texture || cache.Bytes()>48*1024*1024 || cache.Count()>128+cache.ResidentCount())return false;
        for(int i=149;i>=0;--i) if(!cache.Get(icons[i]).texture) return false;
        if(cache.Count()>151+128 || cache.ResidentCount()!=151) return false;
        cache.Clear();
        if(cache.Count() || cache.Bytes() || cache.PreparingIcons() || cache.IconsTotal()) return false;
        // Imported covers are retained at browsing size and upgraded on demand.
        image.width=image.height=512;
        std::vector<unsigned char> largePixels(512*512*4,255);
        icons.clear();
        std::filesystem::create_directories("cache/icons");
        for(int i=0;i<97;++i) {
            const auto icon=i==0?std::string("cache/icons/native.png"):output+"-large-icon-"+std::to_string(i)+".png";
            if(!png_image_write_to_file(&image,icon.c_str(),0,largePixels.data(),0,nullptr)) return false;
            icons.push_back(icon);
        }
        cache.PrepareIcons(icons);
        const auto budgetDeadline=SDL_GetTicks()+15000;
        while(cache.PreparingIcons() && !SDL_TICKS_PASSED(SDL_GetTicks(),budgetDeadline)) {
            cache.BeginFrame();cache.PumpOne();SDL_Delay(1);
        }
        if(cache.PreparingIcons() || cache.ResidentCount()!=97 || cache.ResidentBytes()!=97*128*128*4 ||
           !cache.HasTexture(icons.back())) return false;
        if(cache.Get(icons.back()).width!=128) return false;
        if(cache.DetailKnown(icons.back())) return false;
        cache.PrepareIcons({output+"-video-deferred-missing.png"});
        const auto detailDeadline=SDL_GetTicks()+3000;
        while(cache.Get(icons.back()).width!=512 && !SDL_TICKS_PASSED(SDL_GetTicks(),detailDeadline)) {
            cache.BeginFrame();cache.Prefetch(icons.back());cache.PumpOne(false);SDL_Delay(1);
        }
        if(cache.Get(icons.back()).width!=512 || !cache.DetailKnown(icons.back()) ||
           !cache.PreparingIcons() || cache.ResidentBytes()!=97*128*128*4) return false;
        // Eviction of the detailed version still leaves its immediate thumbnail.
        for(int i=0;i<96;++i) {
            const auto until=SDL_GetTicks()+3000;
            while(cache.Get(icons[i]).width!=512 && !SDL_TICKS_PASSED(SDL_GetTicks(),until)) {
                cache.BeginFrame();cache.Prefetch(icons[i]);cache.PumpOne();SDL_Delay(1);
            }
            if(cache.Get(icons[i]).width!=512) return false;
        }
        if(cache.Get(icons.back()).width!=128 || cache.ResidentBytes()>48*1024*1024) return false;
        cache.Clear();
        if(cache.Bytes() || cache.Count()) return false;
        // Pending preparation remains pending until its worker result is taken;
        // elapsed time must not silently override the enabled preload setting.
        textures_.EnableAsync();textures_.Clear();
        iconPreparationSkipped_=false;startupIntroPending_=false;
        textures_.PrepareIcons({output+"-preload-pending.png"});
        SDL_Delay(1600);
        if(!PreparingIcons() || textures_.IconsDone()==textures_.IconsTotal())return false;
        SkipIconPreparation();
        if(PreparingIcons() || !textures_.PreparingIcons())return false;
        iconPreparationSkipped_=false;
        const auto preparationDeadline=SDL_GetTicks()+3000;
        while(PreparingIcons() && !SDL_TICKS_PASSED(SDL_GetTicks(),preparationDeadline)) {
            textures_.BeginFrame();textures_.PumpOne();SDL_Delay(1);
        }
        if(PreparingIcons() || textures_.IconsDone()!=textures_.IconsTotal())return false;
        textures_.Clear();preferences_.preloadBuiltInCovers=false;PrepareBuiltInIcons();
        if(PreparingIcons() || textures_.IconsTotal())return false;
        GameEntry firstGame;firstGame.name="First";firstGame.libraryId="wiiu";firstGame.platform=Platform::WiiU;firstGame.coverPath=path;
        GameEntry secondGame=firstGame;secondGame.name="Second";secondGame.libraryId="gc";secondGame.platform=Platform::GameCube;
        games_={firstGame,secondGame};tab_=0;RebuildVisible();
        textures_.PrepareIcons({path});
        const auto libraryDeadline=SDL_GetTicks()+3000;
        while(textures_.PreparingIcons() && !SDL_TICKS_PASSED(SDL_GetTicks(),libraryDeadline)) {
            textures_.BeginFrame();textures_.PumpOne();SDL_Delay(1);
        }
        const auto resident=textures_.GetCover(path,false);
        if(!resident.texture || textures_.PreparingIcons())return false;
        for(int i=0;i<static_cast<int>(LibraryTabs().size())+1;++i) {
            ChangeTab(1);AdvanceLibraryFade(1);AdvanceLibraryFade(1);
            if(textures_.GetCover(path,false).texture!=resident.texture || textures_.ResidentCount()!=1)return false;
        }
        textures_.Clear();
        SDL_Log("PASS: preload waits beyond former cutoff, B skips without discarding work, OFF does not queue, library switching retains resident cover");
        SDL_Log("Async texture cache PASS: resident icons/imported-cover thumbnails, detail upgrades, eviction fallback, duplicate/failure completion, bounded memory and reset");
        return true;
    }
    bool CheckArtworkWorkingSet(const std::string &output) {
        std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow/cache");
        const auto failed=[](int line){SDL_Log("Artwork working-set check failed at %d",line);return false;};
        png_image image{};image.version=PNG_IMAGE_VERSION;image.width=1536;image.height=1024;image.format=PNG_FORMAT_RGBA;
        std::vector<unsigned char> pixels(std::size_t(image.width)*image.height*4,255);
        std::vector<std::string> paths;
        for(int i=0;i<33;++i) {
            paths.push_back(output+"-pressure-"+std::to_string(i)+".png");
            if(!png_image_write_to_file(&image,paths.back().c_str(),0,pixels.data(),0,nullptr))return failed(__LINE__);
        }
        TextureCache cache(renderer_,true);
        cache.PrepareIcons(paths);cache.PinBackground(paths.front());
        const auto pump=[&](unsigned selected) {
            cache.BeginFrame();cache.PinCover(paths[selected]);
            for(int offset:{-1,1})cache.PinNearbyCover(paths[(selected+paths.size()+offset)%paths.size()]);
            cache.GetCover(paths[selected],true);cache.GetBackground(paths.front());
            for(int offset=-8;offset<=8;++offset)if(offset)cache.GetCover(paths[(selected+paths.size()+offset)%paths.size()],false);
            for(int offset:{-1,1})cache.PrefetchCover(paths[(selected+paths.size()+offset)%paths.size()],true);
            cache.PumpOne(cache.PreparingIcons());SDL_Delay(1);
        };
        const auto settled=[&](unsigned selected) {
            if(cache.PreparingIcons() || !cache.GetBackground(paths.front()).texture)return false;
            for(int offset=-8;offset<=8;++offset) {
                const unsigned i=(selected+paths.size()+offset)%paths.size();
                const bool original=i==selected || i==(selected+1)%paths.size() || i==(selected+paths.size()-1)%paths.size();
                if(!cache.CoverDetailKnown(paths[i],original))return false;
                const auto info=cache.GetCover(paths[i],i==selected);
                if(!info.texture || info.width!=(original?1536:1024))return false;
            }
            for(int offset:{-1,1})if(!cache.CoverDetailKnown(paths[(selected+paths.size()+offset)%paths.size()],true))return false;
            return true;
        };
        // Thirty-three original 6 MiB scans exceed the detail cache twice over.
        // Their display-size neighbors must settle without repeated decoding,
        // while each newly focused title still receives its original detail.
        for(unsigned selected=0;selected<paths.size();++selected) {
            const auto until=SDL_GetTicks()+15000;
            do {pump(selected);}while(!settled(selected) && !SDL_TICKS_PASSED(SDL_GetTicks(),until));
            if(!settled(selected))return failed(__LINE__);
            const auto decoded=TextureCache::SourceDecodes();
            auto *focus=cache.GetCover(paths[selected],true).texture;
            auto *background=cache.GetBackground(paths.front()).texture;
            for(unsigned frame=0;frame<80;++frame)pump(selected);
            if(TextureCache::SourceDecodes()!=decoded || cache.GetCover(paths[selected],true).texture!=focus ||
               cache.GetBackground(paths.front()).texture!=background || cache.Bytes()>96*1024*1024+cache.ResidentBytes())return failed(__LINE__);
        }
        cache.Clear();if(cache.Count() || cache.Bytes())return failed(__LINE__);
        // No resident thumbnail: selecting an already drawn browse cover must
        // still resolve its face while the original is pending (no blank frame).
        const auto browseUntil=SDL_GetTicks()+5000;
        do {cache.BeginFrame();cache.PrefetchCover(paths.front(),false);cache.PumpOne(false);SDL_Delay(1);}
        while(!cache.CoverDetailKnown(paths.front(),false) && !SDL_TICKS_PASSED(SDL_GetTicks(),browseUntil));
        auto *browseTexture=cache.GetCover(paths.front(),false).texture;
        if(!browseTexture || cache.ResidentCount() || !cache.CoverKnown(paths.front(),true) ||
           !cache.HasCover(paths.front(),true) || cache.GetCover(paths.front(),true).texture!=browseTexture)return failed(__LINE__);
        cache.Clear();
        // Reproduce resident thumbnail + ready browse image + pending original:
        // focusing must use the browse image, not return the thumbnail early.
        cache.PrepareIcons({paths.front()});
        const auto until=SDL_GetTicks()+5000;
        do {cache.BeginFrame();cache.PrefetchCover(paths.front(),false);cache.PumpOne();SDL_Delay(1);}
        while((cache.PreparingIcons() || !cache.CoverDetailKnown(paths.front(),false)) && !SDL_TICKS_PASSED(SDL_GetTicks(),until));
        if(cache.PreparingIcons() || cache.CoverDetailKnown(paths.front(),true) || cache.GetCover(paths.front(),true).width!=1024)return failed(__LINE__);
        const auto fullUntil=SDL_GetTicks()+5000;
        do {cache.BeginFrame();cache.PrefetchCover(paths.front(),true);cache.PumpOne();SDL_Delay(1);}
        while(!cache.CoverDetailKnown(paths.front(),true) && !SDL_TICKS_PASSED(SDL_GetTicks(),fullUntil));
        cache.PinNearbyCover(paths.front());
        if(cache.GetCover(paths.front(),false).width!=1536)return failed(__LINE__);
        cache.Clear();
        // Normal browsing must never request a full scan just because focus
        // changes. The same sharp display texture serves both states.
        cache.SetCoverDetail(false);
        const auto displayUntil=SDL_GetTicks()+5000;
        do {cache.BeginFrame();cache.PrefetchCover(paths.front(),true);cache.PumpOne(false);SDL_Delay(1);}
        while(!cache.CoverDetailKnown(paths.front(),true) && !SDL_TICKS_PASSED(SDL_GetTicks(),displayUntil));
        auto sharp=cache.GetCover(paths.front(),false);
        if(sharp.width!=1024 || cache.GetCover(paths.front(),true).texture!=sharp.texture)return failed(__LINE__);
        const auto displayFile=CoverDisplayCache::File(paths.front(),1024);
        // The cover is already usable without an SD write. Once idle, the
        // actual worker queue persists it and revisits no longer decode source.
        if(std::filesystem::exists(displayFile))return failed(__LINE__);
        const auto idleUntil=SDL_GetTicks()+5000;
        while(!std::filesystem::exists(displayFile) && !SDL_TICKS_PASSED(SDL_GetTicks(),idleUntil)) {
            cache.BeginFrame();cache.PrefetchCover(paths.front(),false);cache.PumpOne(true);SDL_Delay(1);
        }
        if(!std::filesystem::exists(displayFile))return failed(__LINE__);
        const auto decodedBefore=TextureCache::SourceDecodes();
        cache.Clear();
        do {cache.BeginFrame();cache.PrefetchCover(paths.front(),false);cache.PumpOne(false);SDL_Delay(1);}
        while(!cache.CoverDetailKnown(paths.front(),false) && !SDL_TICKS_PASSED(SDL_GetTicks(),displayUntil));
        if(!cache.HasCover(paths.front(),false) || TextureCache::SourceDecodes()!=decodedBefore)return failed(__LINE__);
        cache.SetCoverDetail(true);cache.Clear();
        // Worst-case square scans: three 16 MiB originals plus nearby browse
        // levels must remain usable with the ordinary RGBA upload path.
        image.width=image.height=2048;pixels.assign(2048*2048*4,255);paths.resize(9);
        for(const auto &path:paths)if(!png_image_write_to_file(&image,path.c_str(),0,pixels.data(),0,nullptr))return failed(__LINE__);
        for(int focusIndex:{3,4,5}) {
            const auto ready=[&]() {
                for(int offset:{0,-1,1}) {
                    const auto &path=paths[focusIndex+offset];
                    if(!cache.CoverDetailKnown(path,true) || !cache.HasCover(path,true) || cache.GetCover(path,true).width!=2048)return false;
                }
                for(const auto &path:paths)if(!cache.CoverDetailKnown(path,false) || !cache.HasCover(path,false))return false;
                return true;
            };
            const auto deadline=SDL_GetTicks()+20000;
            do {
                cache.BeginFrame();cache.PinCover(paths[focusIndex]);
                for(int offset:{-1,1,-2,2,-3,3})cache.PinNearbyCover(paths[focusIndex+offset]);
                for(int offset:{0,-1,1})cache.PrefetchCover(paths[focusIndex+offset],true);
                for(const auto &path:paths)cache.PrefetchCover(path,false);
                cache.PumpOne(false);SDL_Delay(1);
            }while(!ready() && !SDL_TICKS_PASSED(SDL_GetTicks(),deadline));
            if(!ready() || cache.Bytes()>96*1024*1024)return failed(__LINE__);
        }
        cache.Clear();
        SDL_Log("PASS: 2048px RGBA originals and six nearby covers remain available within 96 MiB across focus changes");
        SDL_Log("PASS: resident thumbnail cannot mask ready browse art; protected neighboring original is used before focus styling");
        SDL_Log("PASS: 198 MiB source working set, 33 focus changes with original-resolution neighbors, stable idle decode count, pinned focus/background, 96 MiB cache bound and cleanup");
        return true;
    }
    bool CheckArtworkPolish(const std::string &output) {
        if(ArtworkDivisor(3260,1370)!=2 || ArtworkDivisor(9000,20)!=0 || ArtworkDivisor(8192,8192)!=0) return false;
        // Verify streamed PNG sampling exactly, including Adam7 and odd sizes.
        for(const auto dims:std::array<std::array<int,2>,3>{{{17,9},{4097,17},{3260,1370}}}) for(int interlaced:{0,1}) {
            const int w=dims[0],h=dims[1];
            std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*4);
            std::vector<png_bytep> rows(h);
            for(int y=0;y<h;++y) {
                rows[y]=pixels.data()+y*w*4;
                for(int x=0;x<w;++x) {
                    auto *p=rows[y]+x*4; p[0]=x%251;p[1]=y%251;p[2]=(x+y)%251;p[3]=(x^y)%256;
                }
            }
            FILE *file=std::fopen("artwork-check.png","wb"); if(!file) return false;
            auto *png=png_create_write_struct(PNG_LIBPNG_VER_STRING,nullptr,nullptr,nullptr);
            auto *info=png?png_create_info_struct(png):nullptr;
            if(!png || !info) return false;
            if(setjmp(png_jmpbuf(png))) { png_destroy_write_struct(&png,&info);std::fclose(file);return false; }
            png_init_io(png,file);
            png_set_IHDR(png,info,w,h,8,PNG_COLOR_TYPE_RGBA,interlaced?PNG_INTERLACE_ADAM7:PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
            png_write_info(png,info);png_write_image(png,rows.data());png_write_end(png,nullptr);
            png_destroy_write_struct(&png,&info);std::fclose(file);
            for(int edge:{0,128}) {
            SDL_Surface *decoded=LoadPng("artwork-check.png",edge); if(!decoded) return false;
            const int divisor=ArtworkDivisor(w,h,edge);
            bool equal=decoded->w==(w+divisor-1)/divisor && decoded->h==(h+divisor-1)/divisor;
            for(int y=0;y<decoded->h && equal;++y) for(int x=0;x<decoded->w;++x) {
                const int sx=std::min(w-1,x*divisor+divisor/2),sy=std::min(h-1,y*divisor+divisor/2);
                if(std::memcmp(static_cast<unsigned char *>(decoded->pixels)+y*decoded->pitch+x*4,rows[sy]+sx*4,4)) { equal=false;break; }
            }
            SDL_FreeSurface(decoded); if(!equal) return false;
            }
        }
        const std::string base="/project/artifacts/media-polish-20260913/";
        for(const auto &name:{"ds-cover.original.jpg","ds-default.original.png"}) {
            auto *surface=name==std::string("ds-cover.original.jpg")?LoadJpeg(base+name):LoadPng(base+name);
            if(!surface) return false;
            const bool reduced=surface->w==1630 && surface->h==685;
            SDL_FreeSurface(surface); if(!reduced) return false;
        }
        const auto start=SDL_GetPerformanceCounter();
        auto *detail=LoadJpeg(base+"ds-cover.original.jpg");
        const auto middle=SDL_GetPerformanceCounter();
        auto *thumbnail=LoadJpeg(base+"ds-cover.original.jpg",128);
        const auto end=SDL_GetPerformanceCounter();
        const bool reduced=detail && thumbnail && detail->w==1630 && thumbnail->w==408 && thumbnail->h==172;
        if(detail) SDL_FreeSurface(detail);if(thumbnail) SDL_FreeSurface(thumbnail);
        if(!reduced) return false;
#ifdef UFLOW_HOST_PREVIEW
        if(!TextureCache::CheckThumbnailCache(base+"ds-cover.original.jpg")) return false;
        if(!TextureCache::CheckNativeCoverCache(base+"ds-cover.original.jpg")) return false;
#endif
        SDL_Log("JPEG decode timing: detail %.2f ms, thumbnail %.2f ms; cache reuse, corruption recovery and source invalidation PASS",
            (middle-start)*1000./SDL_GetPerformanceFrequency(),(end-middle)*1000./SDL_GetPerformanceFrequency());
        std::filesystem::create_directory("auto-default-check");
        std::filesystem::copy_file("artwork-check.png","auto-default-check/Default.png",std::filesystem::copy_options::overwrite_existing);
        VisualPreview("mediaedit"); editingSourceIndex_=-1; defaultFileChosen_=false;
        draftMediaSource_.path="auto-default-check"; draftMediaSource_.type=MediaType::FullCover;
        AutoSelectDraftDefault(); if(draftMediaSource_.defaultFile!="auto-default-check/Default.png") return false;
        defaultFileBrowser_=true; browserDirectories_={"AUTO (DEFAULT.PNG)"};browserRow_=0;BrowserOpenSelected();
        AutoSelectDraftDefault(); if(!draftMediaSource_.defaultFile.empty()) return false;
        for(int skin:{1,0}) {
            VisualPreview("library"); preferences_.skin=skin;
            LibraryDefinition ds{"custom_1","DS",Platform::Files};ds.caseHeightMm=122;ds.caseThicknessMm=15;
            libraries_.push_back(ds);
            auto &game=games_[selected_];game.name="New Super Mario Bros. (USA)";
            game.platform=Platform::Files;game.libraryId="custom_1";game.absolutePath="game.nds";
            game.fullCoverPath=base+"ds-cover.original.jpg";
            const auto suffix=skin==1?"-glass":"-pixel";
            if(!Render(0,output+suffix+"-ds-cover.png")) return false;
            game.fullCoverPath=base+"ds-default.original.png";
            if(!Render(0,output+suffix+"-ds-default.png")) return false;
            const auto tabs=LibraryTabs();
            for(int tab=0;tab<static_cast<int>(tabs.size());++tab) {
                tab_=tab;visualTab_=tab;RebuildVisible();
                if(!Render(0,output+suffix+"-tab-"+std::to_string(tab)+".png")) return false;
            }
        }
        SDL_Log("Artwork polish PASS: real DS JPEG/PNG downscaled, exact streamed/Adam7 RGBA sampling, size limits, auto-default/explicit-none, both skins and all tab labels");
        return true;
    }
    bool CheckCaseRendering(const std::string &output) {
        // A ruled sleeve exposes affine diagonal bending much better than a
        // photograph. Horizontal artwork lines must stay straight during yaw.
        const std::string gridPath="projection-grid.png";
        std::vector<Uint8> grid(1024*512*4);
        for(int y=0;y<512;++y)for(int x=0;x<1024;++x) {
            auto *p=grid.data()+(y*1024+x)*4;
            const bool line=x%32<2 || y%32<2;
            p[0]=line?20:235;p[1]=line?35:242;p[2]=line?50:248;p[3]=255;
            if(y>=252 && y<=259){p[0]=230;p[1]=40;p[2]=45;}
        }
        png_image gridImage{};gridImage.version=PNG_IMAGE_VERSION;
        gridImage.width=1024;gridImage.height=512;gridImage.format=PNG_FORMAT_RGBA;
        const bool written=png_image_write_to_file(&gridImage,gridPath.c_str(),0,grid.data(),4096,nullptr);
        png_image_free(&gridImage);if(!written)return false;
        for(const int skin:{1,0}) {
            VisualPreview("coverback"); preferences_.skin=skin;
            if(std::any_of(caseTextures_.begin(),caseTextures_.end(),[](auto *p){return !p;})) return false;
            // A blank placeholder cannot exercise the sleeve/hinge draw path.
            if(!Selected() || !textures_.Get(gridPath).texture) return false;
            games_[visible_[selected_]].fullCoverPath=gridPath;
            caseDirectFallbacks_=0;
            for(int step=0;step<=24;++step) {
                coverInspectBackProgress_=step/24.f;
                const auto suffix=skin==1?"-glass-":"-pixel-";
                const auto capture=step%6==0?output+suffix+std::to_string(step)+".png":std::string{};
                if(!Render(0,capture) || caseDirectFallbacks_!=0) return false;
            }
            // Exercise the full details spin as well as inspection front/back.
            VisualPreview("details"); preferences_.skin=skin; caseDirectFallbacks_=0;
            games_[visible_[selected_]].fullCoverPath=gridPath;
            for(int step=0;step<=24;++step) {
                detailsProgress_=step/24.f;
                if(!Render(0) || caseDirectFallbacks_!=0) return false;
            }
        }
        SDL_Log("Case rendering PASS: both skins, inspection front/spine/back and full details spin retain 2x AA; no direct-render fallbacks");
        return true;
    }
    bool CheckCoverFaces(const std::string &output) {
        // Synthetic primary-colour faces make swaps, mirroring and bleed obvious.
        const auto fixture=[](const std::string &path,int w,int h,Uint8 r,Uint8 g,Uint8 b) {
            std::vector<Uint8> pixels(w*h*4);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
                auto *p=pixels.data()+(y*w+x)*4;
                p[0]=r;p[1]=g;p[2]=b;p[3]=255;
                if(x<w/8 || y<h/8){p[0]=p[1]=p[2]=255;}
            }
            png_image image{};image.version=PNG_IMAGE_VERSION;image.width=w;image.height=h;image.format=PNG_FORMAT_RGBA;
            const bool ok=png_image_write_to_file(&image,path.c_str(),0,pixels.data(),w*4,nullptr);png_image_free(&image);return ok;
        };
        const std::string front="face-front.png",back="face-back.png",spine="face-spine.png";
        if(!fixture(front,135,190,240,30,30) || !fixture(back,135,190,30,220,30) || !fixture(spine,14,190,30,50,240))return false;
        for(int skin:{1,0})for(int variant=0;variant<3;++variant) {
            VisualPreview("coverfront");preferences_.skin=skin;preferences_.reflections=true;
            if(visible_.empty())return false;
            auto &game=games_[visible_[selected_]];game.fullCoverPath.clear();game.coverPath=front;
            game.backCoverPath=variant>=1?back:"";game.spinePath=variant==2?spine:"";
            game.defaultSleevePath="/project/native-installer/assets/default-covers/wiiu.png";
            game.defaultBackEnd=.4745f;game.defaultFrontStart=.5255f;
            game.name="FRONT RED / BACK GREEN / SPINE BLUE";
            for(int step:{0,12,24}) {
                coverInspectBackProgress_=step/24.f;
                if(!Render(0,output+"-"+std::to_string(skin)+"-"+std::to_string(variant)+"-"+std::to_string(step)+".png"))return false;
            }
        }
        for(const auto *id:{"nes","snes","gba","n64","pce","msx","ds"}) {
            VisualPreview("coverfront");if(visible_.empty())return false;
            auto &game=games_[visible_[selected_]];
            game.libraryId=id;game.fullCoverPath.clear();game.coverPath.clear();
            game.defaultSleevePath=std::string("/project/native-installer/assets/default-covers/")+id+".png";
            std::ifstream layout(std::string("/project/native-installer/assets/default-covers/")+id+".layout");
            std::string magic;std::uint64_t bytes;layout>>magic>>bytes>>game.defaultBackEnd>>game.defaultFrontStart;
            game.name=std::string(id)+" / SYSTEM DEFAULT DIMENSIONS";
            if(!Render(0,output+"-size-"+id+".png"))return false;
        }
        SDL_Log("PASS: separate/default faces, both skins, front/spine/back poses and seven system profiles");
        return true;
    }
    bool CheckMediaAccordion(const std::string &output) {
        const auto failed=[](int line){SDL_Log("Media accordion failure line %d",line);return false;};
        for(int skin:{1,0}) {
            VisualPreview("media"); preferences_.skin=skin; settingsSidebar_=false;
            libraries_={{"wiiu","Wii U"},{"wii","Wii"},{"gc","GameCube"},{"ds","Nintendo DS"}};
            mediaSources_={{MediaType::FullCover,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/fullcovers",true,0},
                {MediaType::Preview,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/Wii/Videos",true,0,"wii"},
                {MediaType::Cover,0,"fs:/vol/external01/wiiu/apps/uFlow/media/WiiU/Covers",true,0},
                {MediaType::Logo,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/Wii/Logos",false,0,"wii"},
                {MediaType::Cover,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/DS/Covers",true,0,"ds"}};
            auto rows=MediaRows();
            if(rows.size()!=7 || rows[2].key!="wii" || rows[2].directories!=2) return failed(__LINE__);
            const auto suffix=skin==1?"-glass":"-pixel";
            if(!Render(0,output+suffix+"-collapsed.png")) return failed(__LINE__);
            sourceRow_=2; SettingsInput(0,0,true);
            if(sourceEditor_ || MediaRows().size()!=9 || sourceRow_!=2) return failed(__LINE__);
            SourceMove(1); SettingsInput(0,0,true);
            if(!sourceEditor_ || editingSourceIndex_!=1 || draftMediaSource_.type!=MediaType::Preview) return failed(__LINE__);
            CloseOverlay();
            SourceMove(1); SourceActivate();
            if(editingSourceIndex_!=3 || draftMediaSource_.enabled) return failed(__LINE__);
            CloseOverlay(); sourceRow_=2;
            if(!Render(0,output+suffix+"-expanded.png")) return failed(__LINE__);
            // Touch the header to collapse, then re-expand and touch its child.
            SettingsTouch(500,278+2*48);
            if(MediaRows().size()!=7 || sourceEditor_) return failed(__LINE__);
            SettingsTouch(500,278+2*48); SettingsTouch(500,278+3*48);
            if(!sourceEditor_ || editingSourceIndex_!=1) return failed(__LINE__);
            CloseOverlay(); sourceRow_=2; SourceAdd();
            if(!sourceEditor_ || editingSourceIndex_!=-1 || draftMediaSource_.libraryId!="wii") return failed(__LINE__);
            CloseOverlay();
            // Simulate saved reassignment/add/remove; selection follows the saved source.
            mediaSources_[1].libraryId="ds"; FocusMediaSource(1);
            if(MediaRows()[sourceRow_].source!=1 || MediaRows()[sourceRow_].key!="ds") return failed(__LINE__);
            for(int i=0;i<9;++i) mediaSources_.push_back({MediaType::Background,-1,"fs:/vol/external01/media/Wii/extra-"+std::to_string(i),true,0,"wii"});
            FocusMediaSource(static_cast<int>(mediaSources_.size())-1);
            const int expected=MediaRows()[sourceRow_].source;
            const int first=std::max(0,std::min(sourceRow_-4,SourceListCount()-6));
            if(first<=0) return failed(__LINE__);
            if(!Render(0,output+suffix+"-scrolled.png")) return failed(__LINE__);
            SettingsTouch(500,278+(sourceRow_-first)*48);
            if(!sourceEditor_ || editingSourceIndex_!=expected) return failed(__LINE__);
            CloseOverlay();
            sourceRow_=0; SourceMove(-1);
            if(MediaRows()[sourceRow_].kind!=MediaDirectoryRow::Help) return failed(__LINE__);
            SourceActivate(); if(!mediaHelp_) return failed(__LINE__); CloseOverlay();
            sourceRow_=SourceListCount()-2; scanRequested_=false; SourceActivate();
            if(scanRequested_ || !sourceMediaRefreshPending_) return failed(__LINE__);
            sourceMediaRefreshPending_=false;settings_=true;
            sourceManager_=true; mediaSources_.clear(); sourceRow_=0;
            if(SourceListCount()!=3 || !Render(0,output+suffix+"-empty.png")) return failed(__LINE__);
            SourceActivate(); if(!sourceEditor_ || editingSourceIndex_!=-1) return failed(__LINE__);
        }
        SDL_Log("Media accordion PASS: populated content types, controller/touch expand-collapse, correct editor mapping, add group inheritance, reassignment, scroll hit targets, wrapping/actions, empty state, both skins");
        return true;
    }
    bool CheckMediaDefaults(const std::string &output) {
        for(int skin:{1,0}) {
            VisualPreview("mediaedit"); preferences_.skin=skin; settingsSidebar_=false;
            draftMediaSource_.type=MediaType::FullCover;
            draftMediaSource_.library=2;
            draftMediaSource_.path="fs:/vol/external01/wiiu/apps/uFlow/media/GameCube/FullCover";
            const auto directory=draftMediaSource_.path;
            sourceEditorRow_=5;
            if(SourceEditorCount()!=8 || SettingsSelectorSelected()) return false;
            SettingsInput(0,0,true);
            if(!directoryBrowser_ || !defaultFileBrowser_ || settingsEditing_) return false;
            // Deterministic browser rows; actual on-disk filtering is covered by media_test.
            browserPath_=directory; browserDirectories_={"NONE (NO DEFAULT)","alternatives/","Default.png","Game.png"}; browserRow_=2;
            const auto suffix=skin==1?"-glass":"-pixel";
            if(!Render(0,output+suffix+"-picker.png")) return false;
            SettingsInput(0,0,true);
            if(directoryBrowser_ || draftMediaSource_.defaultFile!=directory+"/Default.png" || DraftPath()!=directory) return false;
            if(!Render(0,output+suffix+"-selected.png")) return false;
            // Touch the value area (not a selector); cancel preserves the draft.
            SettingsTouch(1050,278+5*38);
            if(!directoryBrowser_ || settingsEditing_) return false;
            BrowserCancel(); if(draftMediaSource_.defaultFile.empty()) return false;
            SourceEditorActivate(); browserRow_=1; SettingsInput(0,0,true); // ../ precedes AUTO
            if(directoryBrowser_ || !draftMediaSource_.defaultFile.empty()) return false;
            sourceEditorRow_=0; SourceEditorActivate();
            if(defaultFileBrowser_) return false; // regular folder picker unchanged
            browserPath_=directory; BrowserSelectCurrent();
            if(DraftPath()!=directory) return false;
            draftMediaSource_.defaultFile=directory+"/Default.png";
            sourceEditorRow_=1; SourceEditorAdjust(1);
            if(draftMediaSource_.defaultFile.empty()) return false; // image to image retains selection
            SourceEditorAdjust(2);
            if(!VideoSourceEditing() || !draftMediaSource_.defaultFile.empty() || SourceEditorCount()!=11) return false;
            sourceEditorRow_=8;
            if(SettingsSelectorSelected() || SourceEditorFirst()!=2) return false;
            SettingsTouch(1050,278+6*38);
            if(!directoryBrowser_ || !defaultFileBrowser_) return false;
            browserPath_=directory; browserDirectories_={"NONE (NO DEFAULT)","Default.mp4"}; browserRow_=1;
            BrowserSelectCurrent();
            if(directoryBrowser_ || draftMediaSource_.defaultFile!=directory+"/Default.mp4") return false;
            SourceEditorMove(2);
            if(sourceEditorRow_!=10 || SourceEditorFirst()!=4) return false;
        }
        SDL_Log("Media defaults PASS: optional picker, controller/touch, select/none/cancel, folder picker preservation, type filtering/reset, video row scrolling, both skins");
        return true;
    }
    bool CheckAccentThemes(const std::string &output) {
        const auto failed=[](int line){SDL_Log("Accent check failed at line %d",line);return false;};
        const auto pixel=[](const std::string &file,int x,int y){
            std::array<int,3> color{-1,-1,-1};auto *surface=LoadPng(file);
            if(surface){auto *p=static_cast<unsigned char*>(surface->pixels)+y*surface->pitch+x*4;
                for(int c=0;c<3;++c)color[c]=p[c];SDL_FreeSurface(surface);}return color;
        };
        for(int skin:{1,0}) {
            if(!VisualPreview("settings") || visible_.empty())return failed(__LINE__);
            preferences_.skin=skin;settingsCategory_=3;settingsRow_=7;settingsSidebar_=false;
            sourceManager_=false;audioSettings_=false;preferences_.theme=0;
            games_[visible_[selected_]].platform=Platform::GameCube;
            SettingsInput(0,0,true);
            for(int theme=0;theme<AccentThemes::Count;++theme) {
                if(preferences_.theme!=theme || !settingsEditing_)return failed(__LINE__);
                const auto file=output+(skin?"-glass-":"-pixel-")+AccentThemes::Presets[theme].name+".png";
                if(!Render(0,file))return failed(__LINE__);
                const auto expected=AccentFor(Platform::GameCube,theme);
                if(pixel(file,840,524)!=std::array<int,3>{expected.r,expected.g,expected.b})return failed(__LINE__);
                SettingsInput(1,0);
            }
            if(preferences_.theme!=0)return failed(__LINE__);
            SettingsInput(-1,0);if(preferences_.theme!=AccentThemes::Count-1)return failed(__LINE__);
            SettingsInput(0,0,false,true);SettingsTouch(1040,507);
            if(preferences_.theme!=0 || settingsEditing_)return failed(__LINE__);
            // Distinct choices reach the library, Details, focused glass buttons,
            // scan progress and Showcase while retaining the shared clear material.
            std::array<int,3> focused[2];int sample=0;
            for(int theme:{6,12}) {
                VisualPreview("library");preferences_.skin=skin;preferences_.theme=theme;
                const auto base=output+(skin?"-glass-":"-pixel-")+AccentThemes::Presets[theme].name;
                if(!Render(0,base+"-library.png"))return failed(__LINE__);
                ToggleDetails();detailsProgress_=1;detailsBackdropReady_=false;
                if(!Render(0,base+"-details.png"))return failed(__LINE__);
                if(skin)focused[sample++]=pixel(base+"-details.png",450,230);
                ToggleDetails();detailsProgress_=0;scanOpen_=true;scanApplied_=true;
                scanState_.progress={"MATCHING ARTWORK","sd:/fixture",100,50,100};
                if(!Render(0,base+"-scan.png"))return failed(__LINE__);
                scanOpen_=false;detailsBackdropReady_=false;
                if(SDL_SetRenderTarget(renderer_,sceneTexture_))return failed(__LINE__);
                SetColor(renderer_,{56,81,98,255});SDL_RenderClear(renderer_);
                showcaseProgress_=1;gGlassBackdrop=nullptr;
                DrawShowcase(*Selected(),AccentFor(Selected()->platform,theme));
                if(!SaveVisualFrame(renderer_,base+"-showcase.png"))return failed(__LINE__);
                SDL_SetRenderTarget(renderer_,nullptr);showcaseProgress_=0;
            }
            if(skin && focused[0]==focused[1])return failed(__LINE__);
        }
        SDL_Log("PASS: 17 accent choices, Platform follows GameCube, both skins, controller wrap, touch, library/Details/scan/Showcase and focused glass color");
        return true;
    }
    bool CheckBackgroundImages(const std::string &output) {
        const auto failed=[](int line){SDL_Log("Background image check failed at line %d",line);return false;};
        const auto path=output+"-background-source.png";
        png_image image{};image.version=PNG_IMAGE_VERSION;image.width=2560;image.height=1440;image.format=PNG_FORMAT_RGBA;
        std::vector<unsigned char> source(2560*1440*4);
        for(std::size_t i=0;i<source.size();i+=4){source[i]=24;source[i+1]=138;source[i+2]=203;source[i+3]=255;}
        if(!png_image_write_to_file(&image,path.c_str(),0,source.data(),0,nullptr))return failed(__LINE__);
        source.clear();source.shrink_to_fit();
        auto pixel=[&](const std::string &capture){
            std::array<int,3> rgb{-1,-1,-1};auto *surface=LoadPng(capture);
            if(surface){auto *p=static_cast<unsigned char*>(surface->pixels)+350*surface->pitch+20*4;
                for(int c=0;c<3;++c)rgb[c]=p[c];SDL_FreeSurface(surface);}return rgb;
        };
        for(int skin:{1,0}) {
            if(!VisualPreview("library") || visible_.empty())return failed(__LINE__);
            preferences_.skin=skin;preferences_.backgroundMotion=false;preferences_.usePreviews=false;
            for(auto &game:games_)game.backgroundPath=path;
            const auto suffix=output+(skin?"-glass":"-pixel");
            std::array<int,3> samples[3];int n=0;
            for(int transparency:{0,50,100}) {
                preferences_.backgroundTransparency=transparency;
                const auto file=suffix+"-transparency-"+std::to_string(transparency)+".png";
                if(!Render(0,file))return failed(__LINE__);samples[n++]=pixel(file);
            }
            if(samples[0]!=std::array<int,3>{24,138,203})return failed(__LINE__);
            for(int c=0;c<3;++c)if(std::abs(samples[1][c]-(samples[0][c]+samples[2][c])/2)>3)return failed(__LINE__);
            if(selectedBackground_.width!=1280 || selectedBackground_.height!=720)return failed(__LINE__);
            // The visible image stays at the same opacity outside the panels.
            preferences_.backgroundTransparency=50;details_=true;
            for(float progress:{.25f,.5f,1.f}){
                detailsProgress_=progress;detailsBackdropReady_=false;
                const auto file=suffix+"-details-"+std::to_string(int(progress*100))+".png";
                if(!Render(0,file) || pixel(file)!=samples[1])return failed(__LINE__);
                if(!Render(0,file) || pixel(file)!=samples[1])return failed(__LINE__); // cached scene
            }
            details_=false;detailsProgress_=0;detailsBackdropReady_=false;
            for(bool back:{false,true}){
                OpenCoverInspect(back);coverInspectProgress_=1;coverInspectBackProgress_=back?1.f:0.f;
                const auto file=suffix+(back?"-zoom-back.png":"-zoom-front.png");
                if(!Render(0,file) || pixel(file)!=samples[1])return failed(__LINE__);
                CloseCoverInspect();coverInspectProgress_=coverInspectBackProgress_=0;
            }
            // Opening Y before an asynchronous decode finishes must not freeze
            // the old themed scene indefinitely. No second full-size buffer.
            textures_.Clear();textures_.EnableAsync();selectedBackground_={};selectedBackgroundPath_.clear();
            details_=true;detailsProgress_=1;detailsBackdropReady_=false;
            if(!Render(0) || selectedBackground_.texture)return failed(__LINE__);
            const auto start=SDL_GetTicks();
            while(!selectedBackground_.texture && SDL_GetTicks()-start<5000){Render(0);SDL_Delay(1);}
            const auto file=suffix+"-details-delayed-background.png";
            if(!selectedBackground_.texture || !Render(0,file) || pixel(file)!=samples[1])return failed(__LINE__);
            if(textures_.Bytes()>48*1024*1024+textures_.ResidentBytes())return failed(__LINE__);
            // Exercise the actual controller edit and touch arrows at the last row.
            settings_=true;settingsProgress_=1;details_=false;detailsProgress_=0;
            settingsCategory_=3;settingsSidebar_=false;settingsRow_=15;sourceManager_=false;
            preferences_.backgroundTransparency=50;
            if(SettingsRowCount()!=16 || SettingsFirstRow()!=10 || !SettingsSelectorSelected())return failed(__LINE__);
            SettingsInput(0,0,true);SettingsInput(1,0);
            if(preferences_.backgroundTransparency!=55)return failed(__LINE__);
            SettingsInput(0,0,false,true);SettingsTouch(900,187+5*62+10);
            if(preferences_.backgroundTransparency!=50)return failed(__LINE__);
            SettingsAdjust(100);if(preferences_.backgroundTransparency!=100)return failed(__LINE__);
            SettingsAdjust(-100);if(preferences_.backgroundTransparency!=0)return failed(__LINE__);
            preferences_.backgroundTransparency=50;
            if(!Render(0,suffix+"-setting.png"))return failed(__LINE__);
        }
        SDL_Log("PASS: backgrounds decode at screen size, opacity endpoints/midpoint, both skins, Details entry/reuse, front/back zoom, delayed asynchronous arrival and transparency controls");
        return true;
    }
    bool CheckGlassBackground(const std::string &output) {
        if(!std::getenv("UFLOW_GLASS_BACKGROUND_VIDEO"))return false;
        VisualPreview("library");preferences_.backgroundMotion=true;
        tab_=0;visualTab_=0;RebuildVisible(); // leave the previous empty Favorites tab
        for(auto &game:games_)game.backgroundPath.clear();
        const auto spin=[&](auto condition,Uint32 limit=5000){const auto at=SDL_GetTicks();
            while(!condition()&&SDL_GetTicks()-at<limit){Render(0);SDL_Delay(5);}return condition();};
        if(!spin([&]{return glassBackground_.Statistics().presented>20;}))return false;
        if(glassBackground_.Statistics().audioFrames!=0||!Render(0,output+"-library.png"))return false;
        preferences_.backgroundMotion=false;Render(0);
        const auto frozen=glassBackground_.Statistics().presented;SDL_Delay(300);Render(0);
        if(glassBackground_.Statistics().presented!=frozen)return false;
        settings_=true;settingsProgress_=1;Render(0);
        if(!Render(0,output+"-settings.png")||video_.Active())return false;
        settings_=false;settingsProgress_=0;preferences_.backgroundMotion=true;
        if(!spin([&]{return glassBackground_.Statistics().presented>frozen+5;}))return false;
        OpenCoverInspect(false);coverInspectProgress_=1;const auto inspecting=glassBackground_.Statistics().presented;
        if(!spin([&]{return glassBackground_.Statistics().presented>inspecting+5;}))return false;
        CloseCoverInspect();coverInspectProgress_=0;
        // Real preview ownership: stop theme, wait for retiring decoder, then
        // start preview; returning from preview reopens the theme automatically.
        if(visible_.empty())return false;
        videoTest_=true;games_[visible_[selected_]].previewPath=std::getenv("UFLOW_GLASS_BACKGROUND_VIDEO");
        mediaSources_.push_back({MediaType::Preview,-1,"/project/app/content",true,3,"",{0,VideoMode::Background,0}});
        videoSelection_=nullptr;
        if(!spin([&]{return video_.Ready();})||glassBackground_.Active()||!glassBackground_.Released()||!glassBackground_.Ready())return false;
        if(!Render(0,output+"-preview.png"))return false;
        DismissVideo();
        if(!spin([&]{return glassBackground_.Ready();})||video_.Active())return false;
        preferences_.skin=0;Render(0);
        if(glassBackground_.Active()||glassBackground_.Ready())return false;
        preferences_.skin=1;
        if(!spin([&]{return glassBackground_.Ready();}))return false;
        PrepareSystemBackground();
        if(glassBackground_.Active()||glassBackground_.Ready()||video_.Active())return false;
        ResumeSystemForeground();
        if(!spin([&]{return glassBackground_.Ready();}))return false;
        StopMusic();
        if(glassBackground_.Active())return false;
        SDL_Log("PASS Glass background: silent playback, motion pause/resume, settings, inspection, preview handoff, Pixel isolation, system release and shutdown");
        return true;
    }
    bool CheckVideoSettings(const std::string &output) {
        const auto failed=[](int line){SDL_Log("Video UI check failed at line %d",line);return false;};
        for (const int skin : {1,0}) {
            VisualPreview("mediaedit"); preferences_.skin=skin;
            sourceEditorRow_=1;
            if(SourceEditorCount()!=8) return failed(__LINE__);
            SourceEditorAdjust(4);
            if(!VideoSourceEditing() || SourceEditorCount()!=11 || draftMediaSource_.video.delayMs!=1000) return failed(__LINE__);
            settingsSidebar_=false;
            sourceEditorRow_=5; SettingsInput(0,0,true); SettingsInput(1,0);
            if(draftMediaSource_.video.delayMs!=1250 || !settingsEditing_) return failed(__LINE__);
            SettingsInput(-1,0); SettingsInput(0,0,false,true);
            sourceEditorRow_=6; SettingsInput(0,0,true); SettingsInput(1,0);
            if(draftMediaSource_.video.mode!=VideoMode::Background) return failed(__LINE__);
            SettingsInput(0,0,false,true);
            const std::string suffix=skin==1?"-glass":"-pixel";
            if(SourceEditorCount()!=12) return failed(__LINE__);
            if(!Render(0,output+suffix+"-settings.png")) return failed(__LINE__);
            SourceEditorMove(1); // volume scrolls the viewport by one row
            if(SourceEditorFirst()!=1) return failed(__LINE__);
            const int volume=draftMediaSource_.video.volume;
            SettingsTouch(1040,278+6*38);
            if(sourceEditorRow_!=7 || draftMediaSource_.video.volume!=volume+10) return failed(__LINE__);
            SourceEditorMove(1);
            SettingsInput(0,0,true); SettingsInput(-1,0);
            if(draftMediaSource_.video.foregroundOpacity!=95 || !settingsEditing_) return failed(__LINE__);
            SettingsInput(0,0,false,true);
            SettingsTouch(900,278+6*38);
            if(draftMediaSource_.video.foregroundOpacity!=90) return failed(__LINE__);
            if(!Render(0,output+suffix+"-opacity-setting.png")) return failed(__LINE__);
            sourceEditorRow_=6; SourceEditorAdjust(1);
            if(SourceEditorCount()!=11 || draftMediaSource_.video.foregroundOpacity!=90) return failed(__LINE__);
            SourceEditorAdjust(1);
            if(SourceEditorCount()!=11 || draftMediaSource_.video.mode!=VideoMode::Showcase || draftMediaSource_.video.foregroundOpacity!=90) return failed(__LINE__);
            SourceEditorAdjust(1);
            if(!BackgroundVideoEditing()) return failed(__LINE__);
            sourceEditorRow_=11;
            if(SourceEditorFirst()!=5) return failed(__LINE__);
            if(!Render(0,output+suffix+"-settings-bottom.png")) return failed(__LINE__);
            sourceEditorRow_=1; SourceEditorAdjust(1);
            if(VideoSourceEditing() || SourceEditorCount()!=8) return failed(__LINE__);
        }
        // Render the same actual video path in both layers, then exercise leaving it.
        VisualPreview("library"); videoTest_=true;
        textures_.EnableAsync();
        const auto coverPath=output+"-video-cover.png";
        png_image cover{};cover.version=PNG_IMAGE_VERSION;cover.width=cover.height=512;cover.format=PNG_FORMAT_RGBA;
        std::vector<unsigned char> coverPixels(512*512*4,255);
        if(!png_image_write_to_file(&cover,coverPath.c_str(),0,coverPixels.data(),0,nullptr)) return failed(__LINE__);
        games_[visible_[selected_]].fullCoverPath=coverPath;
        textures_.PrepareIcons({coverPath});
        const auto warmUntil=SDL_GetTicks()+3000;
        while(textures_.PreparingIcons() && !SDL_TICKS_PASSED(SDL_GetTicks(),warmUntil)) {
            textures_.BeginFrame();textures_.PumpOne();SDL_Delay(1);
        }
        if(textures_.Get(coverPath).width!=128 || textures_.DetailKnown(coverPath)) return failed(__LINE__);
        games_[visible_[selected_]].previewPath="/fixture/Disney-Bolt.mp4";
        mediaSources_.push_back({MediaType::Preview,-1,"/fixture",true,0,"",{0,VideoMode::Foreground,0}});
        // Manual browsing must wait for the same settled selection as demo mode,
        // even when the configured preview delay is zero.
        visualSelected_=float(selected_)+.5f;
        UpdateVideo(Selected());
        if(video_.Active() || videoPending_)return failed(__LINE__);
        visualSelected_=float(selected_);
        auto ready = [&]() {
            const Uint32 start=SDL_GetTicks();
            do { Render(0); SDL_Delay(5); } while(!video_.Ready() && SDL_GetTicks()-start<6000);
            return video_.Ready();
        };
        if(!ready() || !Render(0,output+"-foreground.png")) return failed(__LINE__);
        OpenCoverInspect(false); coverInspectProgress_=1; Render(0);
        if(video_.Active()) return failed(__LINE__);
        CloseCoverInspect(); coverInspectProgress_=0;
        mediaSources_.back().video.mode=VideoMode::Background; videoSelection_=nullptr;
        if(!ready() || !Render(0,output+"-background.png")) return failed(__LINE__);
        const auto sharpUntil=SDL_GetTicks()+3000;
        while(textures_.GetCover(coverPath,true).width!=512 && !SDL_TICKS_PASSED(SDL_GetTicks(),sharpUntil)) {Render(0);SDL_Delay(1);}
        if(!video_.Active() || textures_.GetCover(coverPath,true).width!=512) return failed(__LINE__);
        // Keep one decoded frame fixed for exact group-compositing checks.
        const auto playbackStart=SDL_GetTicks();
        while(video_.Statistics().presented<60 && SDL_GetTicks()-playbackStart<4000) { Render(0);SDL_Delay(5); }
        video_.Suspend(true);
        auto readScene=[&]() {
            std::vector<unsigned char> pixels(kWidth*kHeight*4);
            SDL_SetRenderTarget(renderer_,sceneTexture_);
            const int result=SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),kWidth*4);
            SDL_SetRenderTarget(renderer_,nullptr);
            if(result) pixels.clear();
            return pixels;
        };
        for(int skin:{1,0}) {
            preferences_.skin=skin;
            std::vector<unsigned char> samples[3];
            int sample=0;
            for(int opacity:{0,50,100}) {
                videoOptions_.foregroundOpacity=opacity;
                if(!Render(0,output+(skin==1?"-glass":"-pixel")+"-opacity-"+std::to_string(opacity)+".png")) return failed(__LINE__);
                samples[sample++]=readScene();
                if(samples[sample-1].empty()) return failed(__LINE__);
            }
            int changed=0;
            for(std::size_t p=0;p<samples[0].size();p+=68) for(int channel=0;channel<3;++channel) {
                const int base=samples[0][p+channel],half=samples[1][p+channel],full=samples[2][p+channel];
                if(std::abs(full-base)>10) ++changed;
                if(std::abs(half-(full*128+base*127)/255)>3) return failed(__LINE__);
            }
            if(changed<1000) return failed(__LINE__);
            // At zero UI opacity, the composed scene must be exactly video-only.
            videoOptions_.foregroundOpacity=0; Render(0);
            auto zero=readScene();
            SDL_SetRenderTarget(renderer_,lensTexture_);
            std::vector<unsigned char> baseline(kWidth*kHeight*4);
            const int result=SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_RGBA32,baseline.data(),kWidth*4);
            SDL_SetRenderTarget(renderer_,nullptr);
            if(result || zero!=baseline) return failed(__LINE__);
        }
        preferences_.skin=1;videoOptions_.foregroundOpacity=100;video_.Suspend(false);
        SDL_Log("Background opacity PASS: conditional settings/controller/touch, 0/50/100 percent exact video/UI composition, both skins");
        // Background video continues underneath inspection; foreground video stops.
        OpenCoverInspect(false); coverInspectProgress_=1;
        const auto inspectionFrames=video_.Statistics().decoded;
        Render(0,output+"-inspection.png");
        if(!video_.Active() || !video_.Ready() || video_.Statistics().decoded<inspectionFrames) return failed(__LINE__);
        CloseCoverInspect(); coverInspectProgress_=0;
        ToggleSettings(); Render(0);
        if(video_.Active()) return failed(__LINE__);
        settings_=false; settingsProgress_=0;
        if(!ready()) return failed(__LINE__);
        ToggleDetails(); Render(0);
        if(video_.Active()) return failed(__LINE__);
        ToggleDetails(); detailsProgress_=0;
        if(!ready()) return failed(__LINE__);
        injectionDialog_.Ask("Video suppression test"); Render(0);
        if(video_.Active()) return failed(__LINE__);
        injectionDialog_.Reset();
        if(!ready()) return failed(__LINE__);
        VideoInput(true); Render(0);
        if(video_.Active()) return failed(__LINE__);
        VideoInput(false);
        if(!ready()) return failed(__LINE__);
        PrepareSystemBackground();
        if(video_.Active() || video_.Ready()) return failed(__LINE__);
        const Uint32 backgroundStop=SDL_GetTicks();
        while(!video_.CanStart() && SDL_GetTicks()-backgroundStop<2000) SDL_Delay(2);
        if(!video_.CanStart()) return failed(__LINE__); // Cancellation unwinds, never pauses cleanup.
        ResumeSystemForeground();
        if(!ready()) return failed(__LINE__);
        DismissVideo(); Render(0);
        if(video_.Active()) return failed(__LINE__);
        Move(1); Render(0); if(video_.Active()) return failed(__LINE__);
        // Exercise the same player with the new case/panel presentation.
        mediaSources_.back().video.mode=VideoMode::Showcase;videoSelection_=nullptr;
        visualSelected_=static_cast<float>(selected_);
        games_[visible_[selected_]].previewPath="/fixture/Disney-Bolt.mp4";
        games_[visible_[selected_]].description="Explore a world of adventure, solve puzzles and discover hidden paths. A long synopsis stays inside the panel while the video continues above it.";
        games_[visible_[selected_]].fullCoverPath.clear();
        for(int skin:{1,0}) {
            preferences_.skin=skin;
            if(!ready())return failed(__LINE__);
            for(float progress:{.0f,.25f,.5f,.75f,1.f}) {
                showcaseProgress_=progress;
                if(!Render(0,output+(skin?"-glass":"-pixel")+"-showcase-"+std::to_string(int(progress*100))+".png"))return failed(__LINE__);
                if(showcaseProgress_!=progress || !video_.Active())return failed(__LINE__);
            }
            DismissVideo();Render(.1f);
            if(video_.Active() || showcaseProgress_<=0.f || showcaseProgress_>=1.f)return failed(__LINE__);
            for(int i=0;i<50;++i)Render(.02f);
            if(showcaseProgress_!=0.f)return failed(__LINE__);
            videoSelection_=nullptr;videoTrigger_=VideoTrigger{};
        }
        if(!ready())return failed(__LINE__);
        Render(.2f);ToggleDetails();Render(.016f);
        if(video_.Active() || showcaseProgress_!=0.f)return failed(__LINE__);
        ToggleDetails();detailsProgress_=0;
        if(!ready())return failed(__LINE__);
        Render(.2f);Move(1);Render(.016f);
        if(video_.Active() || showcaseProgress_!=0.f)return failed(__LINE__);
        videoTest_=false;
        SDL_Log("Showcase PASS: both themes, rotating entry, dismissal reversal, details and navigation interruption");
        SDL_Log("Video UI PASS: dynamic rows, delay/layer/volume editing, touch after scrolling, both skins, foreground/background rendering, settings/details/injection/input suppression, system-background stop/resume, B dismiss, navigation stop");
        return true;
    }
    bool CheckContentTypeMediaUI(const std::string &output){
        const auto failed=[](int line){SDL_Log("Content media check failed at %d",line);return false;};
        for(int skin:{0,1}){
            VisualPreview("settings");preferences_.skin=skin;settingsCategory_=1;settingsSidebar_=false;
            sourceManager_=false;typeManager_=typeEditor_=true;typeRow_=9;
            typeDraft_={"snes","SNES",Platform::Files,GameLauncher::WiiFlow,".sfc",true,1,"snes-retroarch",178,125,25};
            GameEntry styled;styled.mediaLibraryId="snes";
            const auto shape=CaseProfileForGame(styled,{typeDraft_});
            if(shape.widthMm!=178||shape.heightMm!=125||shape.thicknessMm!=25)return failed(__LINE__);
            if(TypeRowCount()!=12)return failed(__LINE__);
            SettingsInput(0,0,true);if(!settingsEditing_||typeDraft_.mediaByContentType)return failed(__LINE__);
            SettingsInput(1,0);if(!typeDraft_.mediaByContentType)return failed(__LINE__);
            if(!Render(0,output+"-"+std::to_string(skin)+".png"))return failed(__LINE__);
            SettingsInput(-1,0);if(typeDraft_.mediaByContentType)return failed(__LINE__);
            SettingsInput(0,1);SettingsInput(1,0);
            if(typeRow_!=9||settingsAction_!=1||settingsEditing_||SettingsActions()[1]!=SettingsAction::Save)return failed(__LINE__);
        }
        std::puts("PASS: content type media selector, both skins, edit/cancel and fixed Save button navigation");return true;
    }
    bool CheckVCSortSettings(const std::string &output) {
        for(int skin:{0,1}){
            VisualPreview("settings");preferences_.skin=skin;settingsCategory_=0;settingsSidebar_=false;sourceManager_=false;audioSettings_=false;
            settingsRow_=4;preferences_.autoSortInstalledVC=false;
            SettingsInput(0,0,true);if(!settingsEditing_||preferences_.autoSortInstalledVC)return false;
            SettingsInput(1,0);if(!preferences_.autoSortInstalledVC||!preferences_.showInstalled)return false;
            if(!Render(0,output+"-installed-"+std::to_string(skin)+".png"))return false;
            SettingsInput(-1,0);if(preferences_.autoSortInstalledVC)return false;
            SettingsInput(0,0,false,true);
            sourceManager_=sourceEditor_=true;mediaMode_=false;settingsCategory_=1;
            draftSource_={Platform::WiiU,"fs:/vol/external01/wiiu/raw-games",true,3,GameLauncher::WiiFlow,"wiiu"};
            sourceEditorRow_=5;
            if(SourceEditorCount()!=8||!SourceEditorSelector(5))return false;
            SettingsInput(0,0,true);SettingsInput(1,0);if(!draftSource_.autoSortVC||preferences_.autoSortInstalledVC)return false;
            if(!Render(0,output+"-raw-"+std::to_string(skin)+".png"))return false;
            SettingsInput(-1,0);if(draftSource_.autoSortVC)return false;
            draftSource_.libraryId="install";if(SourceEditorCount()!=7||RawVCSortEditing())return false;
            draftSource_.platform=Platform::GameCube;draftSource_.libraryId="gamecube";if(RawVCSortEditing())return false;
        }
        std::puts("PASS: independent installed/raw VC sort settings, both skins, selector navigation and excluded sources");return true;
    }
    bool CheckSettingsControls() {
        const auto failed=[](int line){SDL_Log("Settings control check failed at line %d",line);return false;};
#ifdef UFLOW_HOST_PREVIEW
        std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
#endif
        VisualPreview("settings");
        SettingsInput(0,1); if(settingsCategory_!=1 || !settingsSidebar_) return failed(__LINE__);
        SettingsInput(1,0,true); if(settingsSidebar_ || sourceEditor_) return failed(__LINE__);
        SettingsInput(0,1); // row zero manages content types; the first directory is row one
        SettingsInput(0,0,true); if(!sourceEditor_) return failed(__LINE__);
        SettingsInput(0,1); const auto platform=draftSource_.platform;
        SettingsInput(-1,0); SettingsInput(1,0);
        if(draftSource_.platform!=platform || !sourceEditor_) return failed(__LINE__);
        SettingsInput(0,0,true); if(!settingsEditing_ || draftSource_.platform!=platform) return failed(__LINE__);
        SettingsInput(1,0); if(draftSource_.platform==platform || settingsSidebar_) return failed(__LINE__);
        SettingsInput(-1,0); if(draftSource_.platform!=platform || settingsSidebar_) return failed(__LINE__);
        SettingsInput(0,0,false,true); if(settingsEditing_ || !sourceEditor_) return failed(__LINE__);
        SettingsInput(0,0,true); SettingsInput(0,1);
        if(settingsEditing_ || sourceEditorRow_!=2 || settingsSidebar_) return failed(__LINE__);
        const int depth=draftSource_.depth;
        SettingsInput(0,0,true); SettingsInput(-1,0);
        if(draftSource_.depth!=std::max(0,depth-1) || settingsSidebar_) return failed(__LINE__);
        SettingsInput(0,-1); if(settingsEditing_ || sourceEditorRow_!=1) return failed(__LINE__);
        draftSource_.platform=Platform::Wii;
        draftSource_.libraryId="wii";draftSource_.launcherId="wiiflow";
        sourceEditorRow_=4;
        SettingsInput(0,0,true); if(!settingsEditing_) return failed(__LINE__);
        SettingsInput(1,0); if(draftSource_.launcherId!="usbloadergx") return failed(__LINE__);
        SettingsInput(-1,0); if(draftSource_.launcherId!="wiiflow") return failed(__LINE__);
        SettingsInput(0,0,false,true);
        SettingsInput(0,0,false,true); if(sourceEditor_ || !sourceManager_) return failed(__LINE__);
        // Touch a section, open its path, change a value, then cancel.
        SettingsTouch(200,330); if(settingsCategory_!=2 || !settingsSidebar_) return failed(__LINE__);
        FocusMediaSource(0);SettingsInput(1,0);SettingsInput(0,0,true);
        if(!sourceEditor_ || settingsSidebar_) return failed(__LINE__);
        const auto mediaType=draftMediaSource_.type;
        SettingsTouch(1030,320); if(draftMediaSource_.type==mediaType) return failed(__LINE__);
        SettingsTouch(900,320); if(draftMediaSource_.type!=mediaType) return failed(__LINE__);
        SettingsTouch(1040,600); if(sourceEditor_) return failed(__LINE__);
        const int previousMediaRow=sourceRow_;
        SettingsTouch(500,290,-70); if(sourceRow_!=previousMediaRow+1 || sourceEditor_) return failed(__LINE__);
        SettingsTouch(200,210); if(settingsCategory_!=0) return failed(__LINE__);
        SettingsInput(1,0);
        for(int i=0;i<5;++i) SettingsInput(0,1);
        SettingsInput(0,0,true); if(!audioSettings_ || settingsCategory_!=0) return failed(__LINE__);
        // Empty music library must not leave the switch falsely ON.
        musicTracks_.clear(); preferences_.musicTrack.clear(); preferences_.musicEnabled=false;
        SettingsInput(0,0,true); if(!settingsEditing_ || preferences_.musicEnabled) return failed(__LINE__);
        SettingsInput(1,0); if(preferences_.musicEnabled || musicStatus_.empty()) return failed(__LINE__);
        musicTracks_={"fixture.wav"};
        SettingsInput(1,0); if(!preferences_.musicEnabled || musicStatus_.rfind("PLAYING:",0)!=0) return failed(__LINE__);
        SettingsInput(-1,0); if(preferences_.musicEnabled || musicStatus_!="MUSIC OFF" || settingsSidebar_) return failed(__LINE__);
        SettingsInput(0,1); if(settingsEditing_ || settingsRow_!=1) return failed(__LINE__);
        musicTracks_={"fixture.wav","second.ogg","third.mp3"};
        preferences_.musicTrack=musicTracks_[0];
        SettingsInput(0,0,true); SettingsInput(-1,0);
        if(preferences_.musicTrack!=musicTracks_[2] || settingsSidebar_) return failed(__LINE__);
        SettingsInput(1,0); if(preferences_.musicTrack!=musicTracks_[0]) return failed(__LINE__);
        SettingsInput(0,1);
        const auto volume=preferences_.musicVolume;
        SettingsInput(0,0,true); if(preferences_.musicVolume!=volume || !settingsEditing_) return failed(__LINE__);
        SettingsInput(1,0); if(preferences_.musicVolume!=volume+5) return failed(__LINE__);
        SettingsInput(-1,0); if(preferences_.musicVolume!=volume || settingsSidebar_) return failed(__LINE__);
        SettingsInput(0,0,false,true); if(settingsEditing_ || !audioSettings_ || settingsRow_!=2) return failed(__LINE__);
        SettingsInput(-1,0); if(!settingsSidebar_) return failed(__LINE__);
        SettingsInput(1,0); if(settingsSidebar_ || settingsEditing_) return failed(__LINE__);
        SettingsInput(0,0,false,true); if(!settings_ || audioSettings_ || settingsRow_!=5) return failed(__LINE__);
        SettingsInput(0,1); if(settingsRow_!=6) return failed(__LINE__);
        SettingsInput(0,0,true); if(!settingsEditing_) return failed(__LINE__);
        SettingsInput(1,0); if(!preferences_.startOnBoot) return failed(__LINE__);
        SettingsInput(-1,0); if(preferences_.startOnBoot) return failed(__LINE__);
        SettingsInput(0,0,false,true);
        SettingsTouch(1020,515); if(!preferences_.startOnBoot) return failed(__LINE__);
        SettingsTouch(900,515); if(preferences_.startOnBoot) return failed(__LINE__);
        SettingsInput(0,0,false,true); if(settings_) return failed(__LINE__);
        SDL_Log("Settings controls PASS: A edit, left/right values, B/up/down end edit, sidebar focus, path type/depth, touch arrows, General/Audio, ON/OFF, track wrap, volume");
        return true;
    }
    // Japanese titles: rendered with the system-font fallback in both skins.
    bool CheckJapaneseText(const std::string &path) {
        const std::string name = "\xE3\x82\xBC\xE3\x83\xAB\xE3\x83\x80\xE3\x81\xAE\xE4\xBC\x9D\xE8\xAA\xAC"
                                 " \xE3\x83\x96\xE3\x83\xAC\xE3\x82\xB9 BotW"; // Zelda no Densetsu Buresu BotW
        for (const int skin : {1, 0}) {
            if (!VisualPreview("library") || visible_.empty()) return false;
            preferences_.skin = skin;
            games_[visible_[selected_]].name = name;
            if (!Render(0, path + (skin ? "-glass.png" : "-pixel.png"))) return false;
            if (TextWidth(name, 3) <= TextWidth("BotW", 3)) return false; // Japanese glyphs have width
        }
        std::puts("PASS: Japanese title rendered with system-font fallback, both skins");
        return true;
    }
    bool CheckControlHints(const std::string &output) {
        for (const int skin : {1,0}) {
            if (!VisualPreview("library")) return false;
            preferences_.skin = skin;
            auto bind = [&](ControlAction action, std::uint32_t button) {
                preferences_.bindings[static_cast<int>(action)] = button;
            };
            const std::string suffix = skin == 1 ? "-glass" : "-pixel";
            if (!Render(0)) return false;
            if (footerHints_[0].button != "A" || footerHints_[1].button != "Y" ||
                footerHints_[2].button != "X" || footerHints_[3].button != "L/R" ||
                footerHints_[4].button != "+" || footerHints_[5].button != "B" || favoriteHint_ != "MARK A GAME WITH X") return false;
            bind(ControlAction::Sort, 0x0080);
            if(!ButtonPressed(ControlAction::Sort,0x0080)||ButtonPressed(ControlAction::Sort,0x4000))return false;
            bind(ControlAction::Launch, 0x1000);
            bind(ControlAction::Details, 0x8000);
            if (!Render(0, output + suffix + ".png")) return false;
            if (footerHints_[0].button != "Y" || footerHints_[1].button != "A" || footerHints_[5].button != "ZL" ||
                !ButtonPressed(ControlAction::Launch, 0x1000) || ButtonPressed(ControlAction::Launch, 0x8000) ||
                !ButtonPressed(ControlAction::Details, 0x8000) || ButtonPressed(ControlAction::Details, 0x1000)) return false;
            bind(ControlAction::Favorite, 0x0040);
            bind(ControlAction::Settings, 0x0004);
            bind(ControlAction::PreviousLibrary, 0x0800);
            bind(ControlAction::NextLibrary, 0x0400);
            bind(ControlAction::ZoomFront, 0x0080);
            bind(ControlAction::ZoomBack, 0x2000);
            if (!Render(0, output + suffix + "-long.png")) return false;
            if (footerHints_[2].button != "ZR" || footerHints_[3].button != "DPAD LEFT/DPAD RIGHT" ||
                footerHints_[4].button != "-" || favoriteHint_ != "MARK A GAME WITH ZR" ||
                coverInspectHint_ != "HOLD ZL  FRONT    HOLD X  BACK    B  RETURN") return false;
            // Every supported name must fit without obscuring a direction or its action.
            for (const std::uint32_t button : {0x8000,0x4000,0x2000,0x1000,0x0008,0x0004,
                                               0x0020,0x0010,0x0080,0x0040,0x0800,0x0400,0x0200,0x0100}) {
                for (auto &binding : preferences_.bindings) binding = button;
                UpdateControlHints();
                for (const auto &hint : footerHints_) {
                    if (hint.stacked) {
                        if (TextWidth(hint.button,1) > hint.width || TextWidth(hint.action,2) > hint.width) return false;
                    } else if (hint.buttonWidth + hint.gap + TextWidth(hint.action,2) > hint.width) return false;
                }
            }
            ResetControlBindings(preferences_);
            bind(ControlAction::Favorite, 0x0040);
            games_.clear(); RebuildVisible();
            const auto tabs = LibraryTabs();
            const auto favorites = std::find(tabs.begin(), tabs.end(), kFavoritesLibraryId);
            if (favorites == tabs.end()) return false;
            tab_ = static_cast<int>(favorites - tabs.begin()); visualTab_ = static_cast<float>(tab_);
            RebuildVisible();
            if (!Render(0, output + suffix + "-favorites.png") || favoriteHint_ != "MARK A GAME WITH ZR") return false;
            // Exercise the actual settings reset action, not just a new fixture.
            settings_ = true; settingsCategory_ = 4; settingsSidebar_ = false;
            settingsRow_ = static_cast<int>(ControlAction::Count);
            SettingsInput(0,0,true);
            if (!Render(0)) return false;
            if (footerHints_[0].button != "A" || footerHints_[1].button != "Y" ||
                footerHints_[2].button != "X" || footerHints_[3].button != "L/R" || footerHints_[4].button != "+") return false;
        }
        SDL_Log("Control hints PASS: defaults, live A/Y swap, favorite/settings/filter/zoom remaps, all button widths, reset, both skins");
        return true;
    }
    bool CheckScrolling() {
        if(!CheckSettingsControls()) return false;
        VisualPreview("library");
        games_.resize(4); selected_ = 0; visualSelected_ = 0; RebuildVisible();
        preferences_.wrapNavigation = true;
        Move(4); if (selected_ != 1) return false;
        Move(-4); if (selected_ != 0) return false;
        Move(8); if (selected_ != 1) return false;
        visualSelected_ = static_cast<float>(selected_);
        caseDraws_ = 0; Render(0); const auto cold = caseDraws_;
        caseDraws_ = 0; Render(0); const auto warm = caseDraws_;
        SDL_Log("Scrolling regression: four-title wrap PASS; mesh draws cold=%u warm=%u",cold,warm);
        if (cold <= warm || warm != 1) return false;
        preview_ = false;
        for (int frame=0;frame<120;++frame) {
            if (frame%12 == 0) Move(frame < 60 ? 1 : -1);
            Render(1.f/60.f);
            if (sideCases_.size() > kMaxSideCases || sideCaseBytes_ > kSideCaseBudget) return false;
        }
        for (int frame=0;frame<300;++frame) Render(1.f/60.f); // settle and warm
        const auto casesBefore=sideCases_.size(), artBefore=textures_.Count(), textBefore=gSmoothTextCache.size();
        SDL_Log("Idle baseline: %s",PerformanceStats().c_str());
        for (int frame=0;frame<3600;++frame) Render(1.f/60.f);
        SDL_Log("Idle after 3600 frames: %s",PerformanceStats().c_str());
        if (casesBefore != sideCases_.size() || artBefore != textures_.Count() || textBefore != gSmoothTextCache.size()) return false;
        return true;
    }
    bool CheckWrapDisplay(const std::string &output) {
        VisualPreview("library");preferences_.backgroundMotion=false;
        const auto seed=games_.front();
        while(games_.size()<40) {auto g=seed;g.name="TITLE "+std::to_string(games_.size());games_.push_back(g);}
        RebuildVisible();
        const int count=static_cast<int>(visible_.size());
        const auto has=[&](int index,float side){return std::any_of(carouselCards_.begin(),carouselCards_.end(),
            [&](const auto &card){return card.first==index && std::abs(card.second-side)<.01f;});};
        for(int layout=0;layout<4;++layout) for(int wrap=0;wrap<2;++wrap) {
            preferences_.coverLayout=layout;preferences_.wrapNavigation=wrap;
            for(int end:{0,count-1}) {
                selected_=end;visualSelected_=float(end);
                if(!Render(0,output+"-"+std::to_string(layout)+"-"+std::to_string(wrap)+"-"+std::to_string(end)+".png"))return false;
                if(has(end==0?count-1:0,end==0?-1.f:1.f)!=bool(wrap))return false;
            }
        }
        preferences_.wrapNavigation=true; selected_=0;visualSelected_=0;
        Move(-1);if(selected_!=std::size_t(count-1) || visualSelected_!=count)return false;
        visualSelected_=count-1;Move(1);if(selected_!=0 || visualSelected_!=-1)return false;
        visualSelected_=-.5f;if(!Render(0,output+"-seam.png"))return false;
        if(!has(count-1,-.5f) || !has(0,.5f))return false;
        games_.resize(1);RebuildVisible();selected_=0;visualSelected_=0;Render(0);
        if(carouselCards_.size()!=1 || carouselCards_[0].first!=0)return false;
        VisualPreview("settings");settingsCategory_=3;sourceManager_=false;settingsSidebar_=false;
        settingsRow_=12;preferences_.preloadBuiltInCovers=true;
        SettingsTouch(1100,520);if(preferences_.preloadBuiltInCovers || settingsRow_!=12)return false;
        if(!Render(0,output+"-display.png"))return false;
        SDL_Log("Wrap/display PASS: first/last neighbors on/off, all layouts, seam animation, singleton, scrolled touch mapping");
        return true;
    }
    // ZL/ZR on a 14-title library (the user's): never backwards, stop at the
    // ends, wrap only on a fresh press, not on a held repeat.
    bool CheckPaging() {
        // Prefetch in either direction must stay in bounds, including libraries
        // smaller than the lookahead window and either wrapping preference.
        VisualPreview("library");const auto prefetchSeed=games_.front();
        for(int count:{1,2,7,33})for(bool wrap:{false,true})for(int direction:{-1,1}) {
            games_.assign(count,prefetchSeed);RebuildVisible();preferences_.wrapNavigation=wrap;
            browseDirection_=direction;
            for(int end:{0,count-1}) {selected_=end;visualSelected_=float(end);PrefetchArtwork();}
        }
        VisualPreview("library");
        for (int index = static_cast<int>(games_.size()); index < 14; ++index) {
            GameEntry game = games_[0];
            game.name = "Sample title " + std::to_string(index + 1);
            games_.push_back(std::move(game));
        }
        RebuildVisible();
        if (visible_.size() != 14) return false;
        preferences_.wrapNavigation = true;
        selected_ = 6; visualSelected_ = 6.f;
        const auto at = [&](std::size_t expected) { return selected_ == expected; };
        Page(1, true); if (!at(13) || visualSelected_ != 6.f) return false;   // forward, animated
        Page(1, false); if (!at(13)) return false;                            // held: stops at the end
        Page(1, true); if (!at(0) || visualSelected_ != 0.f) return false;    // fresh press wraps
        Page(-1, true); if (!at(13)) return false;
        Page(-1, true); if (!at(3)) return false;
        Page(-1, false); if (!at(0)) return false;
        Page(-1, false); if (!at(0)) return false;
        preferences_.wrapNavigation = false;
        Page(-1, true); if (!at(0)) return false;
        Page(1, true); if (!at(10)) return false;
        Page(1, true); if (!at(13)) return false;
        Page(1, true); if (!at(13)) return false;
        SDL_Log("Paging PASS: 14 titles, forward/back never reversed, ends stop, wrap only on a fresh press");
        return true;
    }
    // Every COVER LAYOUT x COVER SPACING on a mixed shelf: each system's default
    // sleeve and case size (thick NES boxes, short SNES/DS boxes, DVD cases)
    // plus the fixture's full cover, at rest and mid-scroll.
    bool CheckCaseTranslation() {
        VisualPreview("library");
        const auto game=*Selected();
        // Match a real focused library entry: distant-cover lookup alone can
        // resolve only the nearby mip, which intentionally cannot seed this
        // full-detail case cache.
        textures_.PinCover(game.fullCoverPath);
        textures_.GetCover(game.fullCoverPath,true);
        const auto capture=[&](float x,float y) {
            SDL_SetRenderTarget(renderer_,nullptr);
            SetColor(renderer_,{255,255,255,255});SDL_RenderClear(renderer_);
            const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
            DrawBox(game,{ix,iy,180,270},-2.f,false,{40,170,210,255},.82f,0.f,0.f,true,
                    CaseView::kCamera,x-ix,y-iy);
            std::vector<Uint32> pixels(kWidth*kHeight);
            if(SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),kWidth*4)!=0)pixels.clear();
            return pixels;
        };
        capture(400.125f,180.25f);capture(400.125f,180.25f);
        const auto first=capture(400.125f,180.25f);
        const auto shifted=capture(401.125f,181.25f);
        if(first.empty() || shifted.empty() || sideCases_.empty())return false;
        for(int y=0;y<kHeight-1;++y)for(int x=0;x<kWidth-1;++x)
            if(first[y*kWidth+x]!=shifted[(y+1)*kWidth+x+1])return false;
        const auto quarter=capture(400.375f,180.25f);
        if(first==quarter)return false;
        ClearSideCases();
        const auto fresh=capture(400.375f,180.25f);
        if(quarter!=fresh)return false;
        // The resting showcase is selected, unlike shelf cards. Its cached
        // image must retain the selected lighting/reflection and match a fresh
        // mesh, while repeat frames perform no mesh drawing.
        const auto showcase=[&](bool cached) {
            SDL_SetRenderTarget(renderer_,nullptr);
            SetColor(renderer_,{255,255,255,255});SDL_RenderClear(renderer_);
            DrawBox(game,{56,416,170,242},-1.f,true,{40,170,210,255},.2f,0.f,-.1f,true,
                    CaseView::kCamera,0.f,0.f,cached);
            std::vector<Uint32> pixels(kWidth*kHeight);
            if(SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),kWidth*4)!=0)pixels.clear();
            return pixels;
        };
        showcase(false);showcase(false); // Resolve selected-detail artwork first.
        const auto directShowcase=showcase(false);
        const auto cachedShowcase=showcase(true);
        const auto draws=caseDraws_;
        const auto repeatedShowcase=showcase(true);
        // Different target extents can round a bilinear sample by one byte;
        // the repeated cached draw itself must remain bit-identical.
        const auto equivalent=[](const auto &a,const auto &b) {
            if(a.empty() || a.size()!=b.size())return false;
            for(std::size_t i=0;i<a.size();++i)for(unsigned shift=0;shift<32;shift+=8)
                if(std::abs(int((a[i]>>shift)&255)-int((b[i]>>shift)&255))>1)return false;
            return true;
        };
        if(!equivalent(directShowcase,cachedShowcase) || cachedShowcase!=repeatedShowcase ||
           caseDraws_!=draws || sideCaseBytes_>kSideCaseBudget) {
            SDL_Log("Showcase cache mismatch: fresh=%d repeated=%d mesh=%u/%u entries=%zu",
                directShowcase==cachedShowcase,cachedShowcase==repeatedShowcase,draws,caseDraws_,sideCases_.size());
            return false;
        }
        SDL_Log("PASS: cached cover translates exactly without resizing; fractional movement changes pixels; fresh/cached rendering agrees");
        return true;
    }
    bool CheckCoverZoom(const std::string &output) {
        for(int skin=0;skin<2;++skin) {
            VisualPreview("settings");preferences_.skin=skin;preferences_.coverZoom=120;
            settingsCategory_=3;settingsRow_=3;settingsSidebar_=false;settingsEditing_=false;
            sourceManager_=false;audioSettings_=false;
            SettingsInput(0,1);if(settingsRow_!=4 || SettingsRowCount()!=16)return false;
            SettingsInput(0,0,true);SettingsInput(1,0);
            if(preferences_.coverZoom!=125)return false;
            SettingsAdjust(100);if(preferences_.coverZoom!=200)return false;
            SettingsAdjust(-100);if(preferences_.coverZoom!=40)return false;
            SettingsInput(0,0,false,true);
            if(!SettingsZoomTouch(870,459) || preferences_.coverZoom!=40)return false;
            if(!SettingsZoomTouch(1050,459,true) || preferences_.coverZoom!=200)return false;
            if(!SettingsZoomTouch(960,459,true) || preferences_.coverZoom!=120)return false;
            if(!SettingsZoomTouch(1300,459,true) || preferences_.coverZoom!=200)return false;
            if(!SettingsZoomTouch(500,459,true) || preferences_.coverZoom!=40)return false;
            settingsRow_=15;
            if(SettingsZoomTouch(999,459) || SettingsFirstRow()!=10)return false;
            settingsRow_=4;preferences_.coverZoom=120;
            if(!Render(0,output+"-settings-"+std::to_string(skin)+".png"))return false;
            VisualPreview("library");preferences_.skin=skin;preferences_.backgroundMotion=false;
            const auto selected=selected_;
            for(int layout=0;layout<4;++layout)for(int zoom:{40,120,200})for(bool box:{true,false}) {
                preferences_.coverLayout=layout;preferences_.coverZoom=zoom;preferences_.useBoxArt=box;
                const auto origin=FocusCoverRect(),inspection=InspectCoverRect();
                if(inspection.w<=origin.w || inspection.h<=origin.h)return false;
                if(!Render(0,output+"-"+std::to_string(skin)+"-"+std::to_string(layout)+"-"+
                    std::to_string(zoom)+(box?"-box":"-flat")+".png") || selected_!=selected || SDL_RenderIsClipEnabled(renderer_))return false;
            }
            for(bool back:{false,true}) {
                preferences_.coverLayout=0;preferences_.coverZoom=200;preferences_.useBoxArt=true;
                OpenCoverInspect(back);coverInspectProgress_=1;coverInspectBackProgress_=back?1.f:0.f;
                if(!Render(0,output+"-inspect-"+std::to_string(skin)+(back?"-back":"-front")+".png"))return false;
                CloseCoverInspect();coverInspectProgress_=coverInspectBackProgress_=0;
            }
        }
        SDL_Log("Cover zoom PASS: both skins, controller/touch drag, bounds, scrolling, all layouts and flat/3D covers");
        return true;
    }
    bool CheckStartView(const std::string &output) {
        for(int skin:{0,1}) {
            VisualPreview("settings");preferences_.skin=skin;settingsSidebar_=false;settingsRow_=1;
            libraryTabs_={"__all","wiiu","wii","gamecube","ds","snes","nes","gba","__favorites"};
            preferences_.startLibrary="__all";preferences_.startTab=0;tab_=1;
            SettingsInput(0,0,true);SettingsInput(1,0);
            if(preferences_.startLibrary!="wiiu" || tab_!=1)return false;
            SettingsInput(1,0);
            if(preferences_.startLibrary!="wii" || tab_!=1)return false;
            SettingsInput(0,0,false,true);
            SettingsTouch(1030,267);
            if(preferences_.startLibrary!="gamecube" || tab_!=1)return false;
            SettingsTouch(900,267);
            if(preferences_.startLibrary!="wii")return false;
            SettingsAdjust(5);
            if(preferences_.startLibrary!="gba" || StartupLibraryIndex(LibraryTabs(),preferences_)!=7)return false;
            auto reordered=LibraryTabs();std::swap(reordered[1],reordered[7]);
            if(StartupLibraryIndex(reordered,preferences_)!=1)return false;
            if(StartupLibraryIndex({"__all","wiiu","__favorites"},preferences_)!=0 || preferences_.startLibrary!="gba")return false;
            SettingsAdjust(1);if(preferences_.startLibrary!="__favorites")return false;
            SettingsAdjust(1);if(preferences_.startLibrary!="__all")return false;
            SettingsAdjust(-1);if(preferences_.startLibrary!="__favorites")return false;
            preferences_.startLibrary.clear();preferences_.startTab=7;
            if(StartupLibraryIndex(LibraryTabs(),preferences_)!=7)return false;
            preferences_.startLibrary="__favorites";
            if(!Render(0,output+"-"+std::to_string(skin)+".png"))return false;
        }
        SDL_Log("Start view PASS: controller/touch, independent browsing, startup ID, reordering, missing library, legacy index, wrap and more than five tabs");
        return true;
    }
    bool CheckSettingsActions(const std::string &output) {
        using A=SettingsAction;
        const auto fail=[](int line){SDL_Log("Settings actions failed at %d",line);return false;};
        for(int skin=0;skin<4;++skin) {
            for(const char *page:{"settings","sources","sourceedit","media","mediaedit","directories","launchers","launcheredit","mediaguide","audio"}) {
                VisualPreview(page);preferences_.skin=skin;settingsSidebar_=false;
                auto &row=SettingsContentRow();row=std::max(0,SettingsContentCount()-1);
                const int last=row;
                SettingsInput(0,1);
                if(settingsAction_<0 || settingsEditing_)return fail(__LINE__);
                SettingsInput(0,-1);
                if(settingsAction_!=-1 || row!=last)return fail(__LINE__);
                SettingsInput(0,1);
                for(int n=0;n<4;++n)SettingsInput(1,0);
                if(settingsAction_!=3)return fail(__LINE__);
                if(!Render(0,output+"-"+page+"-"+std::to_string(skin)+".png"))return false;
                SettingsInput(0,0,true);
                if(settingsAction_!=-1)return fail(__LINE__);
            }
            // Save/Delete have identical slots in every editor, without duplicate rows.
            for(const char *page:{"sourceedit","mediaedit","launcheredit"}) {
                VisualPreview(page);preferences_.skin=skin;settingsSidebar_=false;
                if(SettingsActions()[1]!=A::Save)return fail(__LINE__);
                if(page==std::string("launcheredit"))launcherIndex_=0;else editingSourceIndex_=0;
                if(SettingsActions()[2]!=A::Delete)return fail(__LINE__);
                SettingsContentRow()=SettingsContentCount()-1;SettingsInput(0,1);SettingsInput(1,0);
                if(settingsAction_!=1)return fail(__LINE__);
                SettingsInput(1,0);if(settingsAction_!=2)return fail(__LINE__);
                SettingsTouch(390+3*185+80,594);
                if(sourceEditor_ || launcherEditor_)return fail(__LINE__);
            }
            VisualPreview("sources");preferences_.skin=skin;settingsSidebar_=false;sourceRow_=0;SourceActivate();
            typeRow_=1;TypeActivate();
            if(SettingsActions()[1]!=A::Save || SettingsActions()[2]!=A::Delete || SettingsContentCount()!=10)return fail(__LINE__);
            typeRow_=9;SettingsInput(0,1);SettingsInput(1,0);
            if(settingsAction_!=1)return fail(__LINE__);
            if(!Render(0,output+"-type-"+std::to_string(skin)+".png"))return false;
            SettingsInput(0,-1);typeRow_=0;TypeActivate();
            typeKey_=static_cast<int>(std::strlen(TypeKeys))-1;
            SettingsInput(0,1);SettingsInput(1,0);
            if(settingsAction_!=1 || SettingsActions()[1]!=A::Done)return fail(__LINE__);
            if(!Render(0,output+"-keyboard-"+std::to_string(skin)+".png"))return false;
            SettingsInput(0,0,true);if(typeText_)return fail(__LINE__);
            // A activates the same action as touch; crossing the footer never saves/deletes.
            VisualPreview("sources");settingsSidebar_=false;sourceRow_=SourceListCount()-1;
            SettingsInput(0,1);SettingsInput(0,0,true);
            if(!sourceEditor_ || editingSourceIndex_!=-1 || SettingsActions()[2]!=A::None)return fail(__LINE__);
            SettingsTouch(390+3*185+80,594);if(sourceEditor_)return fail(__LINE__);
            SettingsTouch(450,594);if(!sourceEditor_ || editingSourceIndex_!=-1)return fail(__LINE__);
        }
        SDL_Log("Settings actions PASS: all four skins, every page, controller/touch, footer reachability, fixed save/delete, keyboard and disabled actions");
        return true;
    }
    bool CheckHomeUpgrade(const std::string &output) {
        const auto fail=[](int n){SDL_Log("HOME UPGRADE failure %d",n);return false;};
        VisualPreview("library");settings_=true;SetControlHeld(0,0);
        preferences_.combos[int(ControlAction::NextLayout)]={0x4800};
        SetControlHeld(0x4000,0x4000);FindOrClearSearch();if(sortOpen_)return fail(__LINE__);
        settings_=false;SetControlHeld(0,0);
        if(ButtonPressed(ControlAction::Sort,0))return fail(__LINE__);
        OpenSort();if(!sortOpen_)return fail(__LINE__);OpenSort();if(sortOpen_)return fail(__LINE__);
        SetControlHeld(0,0);preferences_.combos[int(ControlAction::NextLayout)].clear();
        for(const auto bit:{StickButton::RUp,StickButton::RDown}){
            const auto action=bit==StickButton::RUp?ControlAction::ZoomFront:ControlAction::ZoomBack;
            SetControlHeld(bit,bit);if(!ButtonHeld(action,bit))return fail(__LINE__);
            OpenCoverInspect(bit==StickButton::RDown);coverInspectProgress_=1;
            for(int frame=0;frame<60;++frame){SetControlHeld(bit,0);if(!ButtonHeld(action,bit))return fail(__LINE__);}
            SetControlHeld(0,0);if(ButtonHeld(action,0))return fail(__LINE__);
            CloseCoverInspect();coverInspectProgress_=0;SetControlHeld(0,0);
        }


        const int az=int(ControlAction::Alphabet);
        const auto oldAz=preferences_.bindings[az];const auto oldCombos=preferences_.combos[az];
        for(const uint32_t binding:{uint32_t(0x20000),uint32_t(0x20020)}){
            ClearControlBindings(preferences_,az);AddControlBinding(preferences_,az,binding);
            SetControlHeld(binding,binding);if(!ButtonPressed(ControlAction::Alphabet,binding))return fail(__LINE__);
            OpenAlphabet();alphabetProgress_=1;
            for(int frame=0;frame<60;++frame){
                SetControlHeld(binding,0);
                AlphabetInput(0,frame==0?1:0,frame*16,ButtonHeld(ControlAction::Alphabet,binding),false,false,false);
                if(!AlphabetOpen())return fail(__LINE__);
            }
            SetControlHeld(0,0);AlphabetInput(0,0,1000,ButtonHeld(ControlAction::Alphabet,0),false,false,false);
            if(AlphabetOpen())return fail(__LINE__);
            alphabetProgress_=0;SetControlHeld(0,0);
        }
        preferences_.bindings[az]=oldAz;preferences_.combos[az]=oldCombos;

        settingsCategory_=3;settingsRow_=13;SettingsAdjust(1);
        if(preferences_.previewDelaySeconds!=1)return fail(__LINE__);
        SettingsAdjust(-1);if(preferences_.previewDelaySeconds!=0)return fail(__LINE__);
        for(int skin:{2,3}) {
            VisualPreview("library");preferences_.skin=skin;preferences_.coverType=CoverPresentation::Thumbnails;
            preferences_.thumbnailLayout=0;preferences_.coverZoom=120;preferences_.usePreviews=false;
            batteryLevel_=5;networkConnected_=true;
            if(!Render(0,output+"-row-"+std::to_string(skin)+".png"))return fail(__LINE__);
            details_=true;detailsProgress_=1;detailsRow_=5;
            if(!Render(0,output+"-details-"+std::to_string(skin)+".png")||launchOrigin_.w!=launchOrigin_.h)return fail(__LINE__);
            details_=false;detailsProgress_=0;sortOpen_=true;
            if(!Render(0,output+"-sort-"+std::to_string(skin)+".png"))return fail(__LINE__);
            sortOpen_=false;showcaseProgress_=1;
            DrawShowcase(*Selected(),SwitchAccent());
            if(launchOrigin_.w!=launchOrigin_.h)return fail(__LINE__);
            SaveVisualFrame(renderer_,output+"-showcase-"+std::to_string(skin)+".png");showcaseProgress_=0;
            preferences_.thumbnailLayout=1;selected_=0;visualSelected_=0;gridCameraReady_=false;EnsureGridSelection();
            if(visible_.size()<6)return fail(__LINE__);
            while(visible_.size()<63){auto game=games_[visible_[visible_.size()%7]];game.name="Grid game "+std::to_string(visible_.size());game.absolutePath+="-grid-"+std::to_string(visible_.size());games_.push_back(game);visible_.push_back(games_.size()-1);}
            const auto first=GridCell(0);HomeNavigate(1,0,false,false);
            UpdateGridCamera(1.f);
            if(selected_!=1||GridCell(0).x>=first.x||std::abs(GridCell(1).x+GridCell(1).w/2-640)>1)return fail(__LINE__);
            HomeNavigate(0,1,false,false);if(selected_!=size_t(GridColumns()+1))return fail(__LINE__);
            UpdateGridCamera(1.f);if(std::abs(GridCell(int(selected_)).y+GridCell(int(selected_)).h/2-(GridViewport().y+GridViewport().h/2))>1)return fail(__LINE__);
            HomeNavigate(0,-1,false,false);if(selected_!=1)return fail(__LINE__);UpdateGridCamera(1.f);
            const auto beforeSwipe=selected_;
            HomeSwipeInput(true,true,true,600,300);HomeSwipeInput(true,true,true,410,303);HomeSwipeInput(true,true,false,0,0);
            if(selected_!=beforeSwipe+1||dockFocus_>=0)return fail(__LINE__);
            HomeSwipeInput(true,true,true,600,350);HomeSwipeInput(true,true,true,604,170);HomeSwipeInput(true,true,false,0,0);
            if(selected_!=beforeSwipe+1+GridColumns())return fail(__LINE__);
            const auto afterSwipe=selected_;HomeSwipeInput(true,true,true,600,300);CancelHomeSwipe();HomeSwipeInput(true,true,false,0,0);
            if(selected_!=afterSwipe)return fail(__LINE__);
            selected_=visible_.size()-1;EnsureGridSelection();HomeNavigate(0,1,false,false);
            if(selected_!=size_t(GridColumns()-1)||dockFocus_>=0)return fail(__LINE__);
            selected_=0;EnsureGridSelection();HomeNavigate(-1,0,false,false);
            if(selected_!=visible_.size()-1)return fail(__LINE__);
            HomeNavigate(1,0,false,false);if(selected_!=0)return fail(__LINE__);
            HomeNavigate(0,-1,false,false);if(selected_!=visible_.size()-GridColumns())return fail(__LINE__);
            selected_=GridColumns()-1;EnsureGridSelection();HomeNavigate(1,0,false,false);
            if(selected_!=size_t(GridColumns()))return fail(__LINE__);
            FocusSystemButtons();
            HomeNavigate(1,0,false,false);if(dockFocus_!=1)return fail(__LINE__);
            HomeNavigate(0,-1,false,false);if(dockFocus_!=-1)return fail(__LINE__);
            selected_=31;visualSelected_=31;gridCameraReady_=false;FocusSystemButtons();if(dockFocus_!=0)return fail(__LINE__);HomeNavigate(0,-1,false,false);EnsureGridSelection();
            if(!Render(0,output+"-grid-"+std::to_string(skin)+".png"))return fail(__LINE__);
            preferences_.coverType=CoverPresentation::Box;preferences_.coverLayout=7;caseDraws_=0;
            if(GridViewport().x!=0||GridViewport().y!=0||GridViewport().w!=1280||GridViewport().h!=720)return fail(__LINE__);
            if(!Render(0,output+"-box-grid-"+std::to_string(skin)+".png")||caseDraws_==0)return fail(__LINE__);
            const auto wallHits=bookshelfCacheHits_;DrawWall(SwitchAccent());DrawWall(SwitchAccent());
            if(bookshelfCacheHits_<=wallHits)return fail(__LINE__);
            preferences_.coverLayout=9;selected_=GridColumns()*2+GridColumns()/2;visualSelected_=float(selected_);gridCameraReady_=false;EnsureGridSelection();
            const auto curvedStart=selected_;
            if(!WallLayout() || !Render(0,output+"-curved-wall-"+std::to_string(skin)+".png"))return fail(__LINE__);
            HomeNavigate(1,0,false,false);HomeNavigate(0,1,false,false);UpdateGridCamera(1.f);
            if(selected_!=curvedStart+1+GridColumns())return fail(__LINE__);
            preferences_.coverType=CoverPresentation::Thumbnails;
            dockFocus_=3;if(!Render(0,output+"-dock-"+std::to_string(skin)+".png"))return fail(__LINE__);
            dockFocus_=-1;ActivateDock(5);if(!powerPrompt_||ConsumeShutdown())return fail(__LINE__);
            if(!Render(0,output+"-power-"+std::to_string(skin)+".png"))return fail(__LINE__);
            HomeNavigate(0,0,true,false);if(ConsumeShutdown()||powerPrompt_)return fail(__LINE__);
            settings_=true;settingsProgress_=1;settingsCategory_=3;settingsSidebar_=false;settingsRow_=5;
            if(SettingsRowCount()!=18)return fail(__LINE__);
            for(int zoom=2;zoom<=8;++zoom){preferences_.inspectionZoom=zoom;const auto origin=FocusCoverRect(),rect=InspectCoverRect();if(rect.w!=int(std::ceil(origin.w*zoom*.5f))||rect.w!=rect.h)return fail(__LINE__);}
            if(!Render(0,output+"-display-"+std::to_string(skin)+".png"))return fail(__LINE__);
            settingsCategory_=4;settingsRow_=int(ControlAction::NavigateUp);settingsAction_=-1;
            preferences_.bindings[settingsRow_]=0x0200;
            SettingsInput(0,0,false,false,false,false,true);if(!CapturingBinding()||!bindingAppend_)return fail(__LINE__);
            CaptureBinding(0);CaptureBinding(StickButton::Up);CaptureBinding(0);if(preferences_.bindings[settingsRow_]!=(0x0200|StickButton::Up))return fail(__LINE__);
            BeginBindingCapture(settingsRow_,true);CaptureBinding(0);CaptureBinding(0x20);CaptureBinding(0x8020);
            if(!CapturingBinding())return fail(__LINE__);CaptureBinding(0x20);CaptureBinding(0);
            if(CapturingBinding()||preferences_.combos[settingsRow_]!=std::vector<uint32_t>{0x8020}||settingsSidebar_)return fail(__LINE__);
            if(!Render(0,output+"-controls-"+std::to_string(skin)+".png"))return fail(__LINE__);
            SettingsInput(0,0,false,false,true);if(preferences_.bindings[settingsRow_]||!preferences_.combos[settingsRow_].empty())return fail(__LINE__);
            ResetControlBindings(preferences_);settings_=false;settingsProgress_=0;
        }
        VisualPreview("library");preferences_.coverType=CoverPresentation::Logo;
        for(int layout:{1,2}){preferences_.logoLayout=layout;const auto above=PresentationCard(-1),below=PresentationCard(1),center=PresentationCard(0);
            if(center.roll!=0||above.roll*below.roll>=0||std::abs(above.roll)<.1f)return fail(__LINE__);
            if(!Render(0,output+"-wheel-"+std::to_string(layout)+".png"))return fail(__LINE__);
        }
        VisualPreview("library");preferences_.coverType=CoverPresentation::Box;preferences_.coverLayout=4;
        const auto left=PresentationCard(-2),right=PresentationCard(2),focus=PresentationCard(0);
        if(left.y<=focus.y||right.y<=focus.y||left.roll<=0||right.roll>=0||left.yaw<=right.yaw||focus.yaw<=0||left.h>=focus.h)return fail(__LINE__);
        for(int skin:{0,1,2,3}){preferences_.skin=skin;for(int zoom:{80,120,200}){
            preferences_.coverZoom=zoom;
            for(int slot=-3;slot<=3;++slot){const auto p=PresentationCard(float(slot));const CaseView v({int(p.x-p.w/2),int(p.y-p.h/2),int(p.w),int(p.h)},CaseProfile{},p.yaw,-.07f,p.roll);const auto bounds=v.Bounds();
                if(p.x>=0 && p.x<=1280 && bounds.y<100)return fail(__LINE__);
                if(SwitchSkin())for(int button=0;button<6;++button){const float cx=SkinSpec().dockX+button*SkinSpec().dockGap;
                    if(bounds.x<cx+42 && bounds.x+bounds.w>cx-42 && bounds.y+bounds.h>SkinSpec().dockY-42)return fail(__LINE__);}
                else if(bounds.y+bounds.h>620)return fail(__LINE__);}
            if(!Render(0,output+"-fan-"+std::to_string(skin)+"-"+std::to_string(zoom)+".png"))return fail(__LINE__);
        }}
        for(int type=0;type<CoverPresentation::TypeCount;++type){preferences_.coverType=type;PresentationLayout()=0;const auto original=selected_;
            for(int layout=1;layout<=CoverPresentation::LayoutCount(type);++layout){NextLayout();if(PresentationLayout()!=layout%CoverPresentation::LayoutCount(type)||selected_!=original)return fail(__LINE__);}
        }
        SDL_Log("PASS: square details/showcase, center-following 2D grid navigation, dock and safe power prompt, inspection zoom, additive/clear mappings, radial logo tilt and curved 3D box fan");return true;
    }
    bool CheckSwitch(const std::string &output) {
        for(int skin:{2,3}) {
            VisualPreview("library");preferences_.skin=skin;preferences_.coverType=CoverPresentation::Thumbnails;
            preferences_.coverZoom=120;preferences_.thumbnailLayout=0;preferences_.usePreviews=false;
            if(!Render(0,output+"-home-"+std::to_string(skin)+".png"))return false;
            const auto card=PresentationCard(0),next=PresentationCard(1);
            if(card.w!=card.h || card.yaw || card.roll || next.w!=card.w || next.y!=card.y)return false;
            preferences_.thumbnailLayout=1;
            if(!Render(0,output+"-wall-"+std::to_string(skin)+".png"))return false;
            settings_=true;settingsProgress_=1;settingsCategory_=3;settingsSidebar_=false;settingsRow_=0;
            detailsBackdropReady_=false;
            if(!Render(0,output+"-settings-"+std::to_string(skin)+".png"))return false;
            SettingsAdjust(1);if(preferences_.coverType!=CoverPresentation::Box)return false;
            SettingsAdjust(-1);if(preferences_.coverType!=CoverPresentation::Thumbnails)return false;
            settingsCategory_=0;settingsRow_=0;
            if(!Render(0,output+"-general-"+std::to_string(skin)+".png"))return false;
        }
        SDL_Log("Switch PASS: both skins, square flat tiles, row/wall, display type cycling and settings");return true;
    }
    bool CheckDemo(const std::string &output) {
        const auto fail=[](int line){SDL_Log("Demo FAILED at %d",line);return false;};
        for(int skin:{0,1}) {
            VisualPreview("settings");preferences_.skin=skin;settingsSidebar_=false;
            preferences_.demoMode=false;settingsRow_=8;
            if(SettingsRowCount()!=9)return fail(__LINE__);
            SettingsAdjust(1);
            if(!preferences_.demoMode || SettingsRowCount()!=10)return fail(__LINE__);
            SettingsMoveRow(1);preferences_.demoIdleSeconds=30;SettingsAdjust(1);
            if(settingsRow_!=9 || preferences_.demoIdleSeconds!=35)return fail(__LINE__);
            if(!Render(0,output+"-settings-"+std::to_string(skin)+".png"))return fail(__LINE__);
            SettingsInput(0,1);if(settingsAction_<0)return fail(__LINE__);
            settingsAction_=-1;settingsRow_=8;SettingsAdjust(1);
            if(preferences_.demoMode || SettingsRowCount()!=9)return fail(__LINE__);
            if(!Render(0,output+"-off-"+std::to_string(skin)+".png"))return fail(__LINE__);
        }
        VisualPreview("library");demoTest_=videoTest_=true;startupIntroPending_=false;
        games_.resize(3);
        for(int i=0;i<3;++i){games_[i].absolutePath="demo-"+std::to_string(i);games_[i].name="Demo title "+std::to_string(i);games_[i].previewPath=i?"/fixture/demo-"+std::to_string(i)+".mp4":"";}
        mediaSources_.push_back({MediaType::Preview,-1,"/fixture",true,0,"",{0,VideoMode::Foreground,0}});
        RebuildVisible();selected_=0;visualSelected_=0;
        preferences_.demoMode=true;preferences_.demoIdleSeconds=5;preferences_.usePreviews=false;
        demo_.Reset(SDL_GetTicks()-5000);VideoInput(false);Render(0);
        if(!demo_.Active() || Selected()->previewPath.empty())return fail(__LINE__);
        const auto first=selected_;bool presented=false,ended=false,advanced=false;
        const auto started=SDL_GetTicks();
        while(SDL_GetTicks()-started<15000) {
            const bool allowed=demoNext_;
            if(video_.Ready())presented=true;
            Render(.05f);
            if(demoNext_)ended=true;
            if(selected_!=first){if(!allowed || !ended || !presented)return fail(__LINE__);advanced=true;break;}
            SDL_Delay(10);
        }
        if(!advanced || Selected()->previewPath.empty())return fail(__LINE__);
        // Restore by identity even if the visible list changed during the demo.
        std::reverse(visible_.begin(),visible_.end());
        if(!DemoInput(true) || demo_.Active() || video_.Active() ||
           Selected()->absolutePath!="demo-0" || visualSelected_!=float(selected_))return fail(__LINE__);
        std::reverse(visible_.begin(),visible_.end());selected_=2;visualSelected_=2;
        Render(0);if(demo_.Active())return fail(__LINE__);
        demo_.Reset(SDL_GetTicks()-5000);UpdateDemo();settings_=true;UpdateDemo();
        if(demo_.Active() || Selected()->absolutePath!="demo-2")return fail(__LINE__);settings_=false;
        for(auto &game:games_)game.previewPath="";
        games_[1].previewPath="/fixture/missing-demo.mp4";
        demo_.Reset(SDL_GetTicks()-5000);Render(0);
        const auto missingAt=SDL_GetTicks();
        while(demo_.Active() && SDL_GetTicks()-missingAt<8000){Render(.1f);SDL_Delay(10);}
        if(demo_.Active())return fail(__LINE__);
        games_[1].previewPath.clear();demo_.Reset(SDL_GetTicks()-5000);Render(0);
        if(demo_.Active())return fail(__LINE__);
        demoTest_=videoTest_=false;preferences_.demoMode=false;StopVideo();
        SDL_Log("Demo PASS: both-skin conditional settings/footer, video-only random selection, complete playback before advancing, input/menu exit restores original game by identity, stale/empty video library, preview-toggle independence");return true;
    }
    bool CheckBookshelf(const std::string &output) {
        VisualPreview("library");
        // Every corner stays in the original thickness slot during insertion;
        // every rolling corner clears the front of the entire shelf.
        constexpr float shelfYaw=1.f,shelfPitch=.65f,camera=2.5f;
        for(const char *id:{"wiiu","nes","snes","ds","n64","gba","gamecube"}) {
            auto game=games_.front();game.libraryId=id;
            const auto profile=CaseProfileForGame(game,libraries_);
            for(float slot:{-.2f,0.f,.2f})for(int frame=0;frame<=100;++frame) {
                const float focus=frame/100.f;
                const auto pose=BookshelfCasePose(profile,slot,focus,198.3333f,750,450,{310,193,220,314},314);
                const CaseView view({0,0,220,314},profile,pose.yaw,pose.pitch,pose.roll,camera,&pose.scene);
                for(float x:{-.5f,.5f})for(float y:{-.5f,.5f})for(float z:{-.5f,.5f}) {
                    const auto p=view(x,y,z);const float scale=(camera-p.depth)/(198.3333f*camera);
                    const float cx=(p.point.x-750)*scale,cy=(450-p.point.y)*scale,cz=p.depth;
                    const float depth=cz*std::cos(shelfPitch)-cy*std::sin(shelfPitch);
                    const float wx=cx*std::cos(shelfYaw)-depth*std::sin(shelfYaw);
                    const float wz=cx*std::sin(shelfYaw)+depth*std::cos(shelfYaw);
                    const float wy=cy*std::cos(shelfPitch)+cz*std::sin(shelfPitch);
                    if(pose.clear) {if(wx>=0)return false;}
                    else if(std::abs(wz-(slot+z*profile.thicknessMm/190.f))>.0001f ||
                            std::abs(wy-(y+.5f)*profile.heightMm/190.f)>.0001f)return false;
                }
                if(frame==100 && pose.roll<6.28f)return false;
            }
        }
        for(int speed:{0,1,2})for(int fps:{15,30,60})for(int direction:{-1,1}) {
            const float duration=speed==0?1.6f:speed==1?1.3f:1.f,dt=1.f/fps;
            float position=1333,elapsed=0,visibleSpin=0,previousRoll=0;
            for(int frame=0;frame<int(std::ceil(duration*fps))+2;++frame) {
                position=AdvanceBookshelfPosition(position,1333.f+direction,dt,speed);elapsed+=dt;
                const float focus=SmoothFade(1.f-std::abs(1333.f+direction-position));
                const auto pose=BookshelfCasePose(CaseProfile{},0,focus,198.3333f,750,450,{310,193,220,314},314);
                if(pose.roll>.2f && pose.roll<6.2f)visibleSpin+=dt;
                if(pose.roll-previousRoll>3.141593f)return false;
                previousRoll=pose.roll;
            }
            if(position!=1333.f+direction || visibleSpin<.24f || elapsed>duration+3*dt)return false;
        }
        for(int skin:{0,1})for(bool mixed:{false,true}) {
            VisualPreview("library");preferences_.skin=skin;preferences_.coverLayout=8;
            const auto seed=games_.front();games_.assign(180,seed);
            const char *ids[]={"wiiu","nes","snes","ds","n64","gba","gamecube"};
            for(int i=0;i<180;++i){auto &game=games_[i];game.name="Shelf title "+std::to_string(1000+i);game.logoPath.clear();
                if(mixed){game.libraryId=ids[i%7];game.fullCoverPath="/project/native-installer/assets/default-covers/"+game.libraryId+".png";}}
            RebuildVisible();selected_=90;visualSelected_=90;
            const auto name=output+"-"+std::to_string(skin)+(mixed?"-mixed":"-dvd");
            if(!Render(0,name+".png"))return false;
            if(carouselCards_.size()<20)return false;
            if(bookshelfTexture_) {
                Render(0);Render(0);const auto builds=bookshelfBuilds_,hits=bookshelfCacheHits_;
                for(int frame=0;frame<40;++frame)if(!Render(0))return false;
                if(bookshelfBuilds_!=builds || bookshelfCacheHits_<hits+40 || bookshelfVertices_!=0)return false;
                textures_.Clear();if(!Render(0) || bookshelfBuilds_==builds)return false;
            }
            for(float move:{.10f,.25f,.4f,.5f,.6f,.75f,.90f}){visualSelected_=90+move;if(!Render(0,name+"-move"+std::to_string(int(move*100))+".png"))return false;
                if(bookshelfVertices_>12000)return false;}
            if(skin==1 && !mixed)for(int direction:{-1,1}) {
                selected_=90+direction;visualSelected_=90;
                for(int frame=0;frame<=20;++frame) {
                    if(frame)visualSelected_=AdvanceBookshelfPosition(visualSelected_,float(selected_),1.3f/20,1);
                    if(!Render(0,name+(direction>0?"-forward-":"-backward-")+std::to_string(frame)+".png"))return false;
                }
                selected_=90;
            }
            visualSelected_=90;
            for(int zoom:{40,200}){preferences_.coverZoom=zoom;if(!Render(0,name+"-zoom"+std::to_string(zoom)+".png"))return false;}
            preferences_.coverZoom=120;
            for(int spacing:{50,200}){preferences_.coverSpacing=spacing;if(!Render(0,name+"-spacing"+std::to_string(spacing)+".png"))return false;}
            preferences_.coverSpacing=100;
            for(bool wrap:{false,true})for(int end:{0,179}){preferences_.wrapNavigation=wrap;selected_=end;visualSelected_=float(end);if(!Render(0))return false;}
            games_.resize(1);RebuildVisible();selected_=0;visualSelected_=0;Render(0);
            if(carouselCards_.size()!=1)return false;
        }
        // Coincident physical boundaries remain coincident under the shared
        // camera; independently projected billboards cannot satisfy this.
        const CaseProfile p{};constexpr float yaw=.9f,pitch=.42f,z=.3f;
        const auto scene=[&](float depth){const float d=depth*std::cos(yaw)-p.widthMm/380.f*std::sin(yaw);return CaseScene{
            depth*std::sin(yaw)+p.widthMm/380.f*std::cos(yaw),p.heightMm/380.f*std::cos(pitch)-d*std::sin(pitch),
            p.heightMm/380.f*std::sin(pitch)+d*std::cos(pitch),324,760,440};};
        const auto a=scene(z),b=scene(z+p.thicknessMm/190.f);
        CaseView av({0,0,220,314},p,yaw,pitch,0,2.5f,&a),bv({0,0,220,314},p,yaw,pitch,0,2.5f,&b);
        for(float y:{-.5f,.5f}){const auto left=av(-.5f,y,.5f).point,right=bv(-.5f,y,-.5f).point;
            if(std::hypot(left.x-right.x,left.y-right.y)>.01f)return false;}
        SDL_Log("Bookshelf PASS: physical slot insertion, roll clears shelf, full visible turn at 15/30/60 fps in both directions, shared perspective, both skins, mixed sizes, 40 idle cached frames, artwork invalidation, bounded geometry, zoom/spacing, ends and singleton");return true;
    }
    bool CheckPresentations(const std::string &output) {
        const auto check=[&](bool ok,int line){if(!ok)SDL_Log("Presentation FAIL line %d",line);return ok;};
        for(int skin:{0,1}) {
            VisualPreview("settings");preferences_.skin=skin;settingsCategory_=3;sourceManager_=false;
            settingsSidebar_=false;settingsRow_=0;
            if(!check(SettingsRowCount()==16 && SettingsSelectorRow(0),__LINE__))return false;
            preferences_.coverLayout=8;SettingsAdjust(1);
            if(!check(preferences_.coverType==CoverPresentation::Logo && PresentationLayout()==0,__LINE__))return false;
            settingsRow_=1;SettingsAdjust(-1);
            if(!check(preferences_.logoLayout==6,__LINE__))return false;
            settingsRow_=0;SettingsAdjust(1);settingsRow_=1;SettingsAdjust(-1);
            if(!check(preferences_.mediaLayout==3,__LINE__))return false;
            settingsRow_=0;SettingsAdjust(1);
            if(!check(preferences_.coverType==CoverPresentation::Box && PresentationLayout()==8,__LINE__))return false;
            if(!check(SettingsZoomTouch(870,400) && preferences_.coverSpacing==50,__LINE__))return false;
            if(!check(SettingsZoomTouch(1050,400,true) && preferences_.coverSpacing==200,__LINE__))return false;
            if(!check(SettingsZoomTouch(930,400,true) && preferences_.coverSpacing==100,__LINE__))return false;
            if(!Render(0,output+"-settings-"+std::to_string(skin)+".png"))return false;
            for(int type=0;type<CoverPresentation::TypeCount;++type)for(int layout=0;layout<CoverPresentation::LayoutCount(type);++layout) {
                VisualPreview("library");preferences_.skin=skin;preferences_.coverType=type;PresentationLayout()=layout;
                auto seed=games_.front();games_.assign(29,seed);
                for(int i=0;i<29;++i){games_[i].name="Arcade title "+std::to_string(i+1);games_[i].logoPath="/project/logo.png";}
                RebuildVisible();selected_=14;visualSelected_=14;
                const auto focus=FocusCoverRect();const auto pose=PresentationCard(0);
                if(!check(std::abs(pose.x-(focus.x+focus.w*.5f))<.01f,__LINE__))return false;
                caseDraws_=0;
                const auto name=output+"-"+std::to_string(skin)+"-"+std::to_string(type)+"-"+std::to_string(layout);
                if(!Render(0,name+".png"))return false;
                if(type!=CoverPresentation::Box || layout==2)if(!check(caseDraws_==0,__LINE__))return false;
                for(float pos:{14.25f,14.5f,14.75f}){visualSelected_=pos;if(!Render(0))return false;}
                visualSelected_=14;
                if(type!=CoverPresentation::Box) {
                    caseDraws_=0;OpenCoverInspect(true);coverInspectProgress_=1;coverInspectBackProgress_=1;
                    if(!Render(0))return false;
                    CloseCoverInspect();coverInspectProgress_=coverInspectBackProgress_=0;
                    if(!BeginLaunchAnimation() || !DrawLaunchAnimation(.3f))return false;
                    EndLaunchAnimation();if(!check(caseDraws_==0,__LINE__))return false;
                }
            }
        }
        for(const auto *id:{"wiiu","gamecube","nes","snes","n64","gba","ds","pce","msx","ps1"}) {
            VisualPreview("library");preferences_.coverType=CoverPresentation::Media;
            for(auto &game:games_){game.libraryId=id;game.logoPath="/project/logo.png";}
            if(!Render(0,output+"-media-"+id+".png"))return false;
        }
        SDL_Log("Presentation PASS: type/layout memory, spacing touch, both skins, all layouts, flat geometry, inspection and launch, ten system media defaults");
        return true;
    }
    bool CheckCoverLayouts(const std::string &output) {
        // Sleeves that fit the artwork cache together (Wii/Wii U/DS defaults are
        // 17-27 MB decoded); the fixture's full cover stands in for DVD cases.
        static const char *kSystems[] = {"gamecube","nes","snes","gamecube","n64","gba","pce","nes","snes","gba","n64"};
        static const char *kLayouts[] = {"flow","carousel","flat","stacked"};
        static const char *kSpacings[] = {"compact","balanced","wide"};
        const bool supersampled = std::none_of(caseTextures_.begin(), caseTextures_.end(), [](auto *p){ return !p; });
        for (int layout = 0; layout < 4; ++layout) for (int spacing = 0; spacing < 3; ++spacing) {
            VisualPreview("library");
            games_.clear();
            for (int index = 0; index < 44; ++index) {
                GameEntry game;
                const std::string id = kSystems[index % 11];
                game.name = "Shelf title " + std::to_string(index + 1);
                if (index % 3 == 1) {
                    game.coverPath = "/fixture/cover.png"; game.fullCoverPath = "/fixture/fullcover.png";
                } else {
                    const std::string root = "/project/native-installer/assets/default-covers/" + id;
                    game.libraryId = id; game.defaultSleevePath = root + ".png";
                    std::ifstream sleeve(root + ".layout");
                    std::string magic; std::uint64_t bytes = 0;
                    sleeve >> magic >> bytes >> game.defaultBackEnd >> game.defaultFrontStart;
                    if (magic != "UFLOW-SLEEVE-1") return false;
                }
                game.favorite = index == 20 || index == 17;
                games_.push_back(std::move(game));
            }
            RebuildVisible();
            if (visible_.size() != 44) return false;
            selected_ = 20; visualSelected_ = 20.f;
            preferences_.coverType=CoverPresentation::Box;preferences_.coverLayout = layout; preferences_.coverSpacing = 75+spacing*25;
            ClearSideCases();
            const std::string name = output + "-" + kLayouts[layout] + "-" + kSpacings[spacing];
            if (!Render(0, name + ".png")) return false;
            // Once artwork has loaded, every resting side case fits the cache
            // and only the selected case is drawn again.
            if (!Render(0) || !Render(0)) return false;
            caseDraws_ = 0;
            if (!Render(0)) return false;
            if (supersampled && ((caseDraws_ != (layout==2?0u:1u)) || sideCaseBytes_ > kSideCaseBudget)) {
                SDL_Log("Cover layouts FAIL %s: warm draws=%u side bytes=%zu cases=%zu", name.c_str(), caseDraws_,
                        sideCaseBytes_, sideCases_.size());
                return false;
            }
            SDL_Log("Cover layout %s: side cases=%zu bytes=%zu", name.c_str(), sideCases_.size(), sideCaseBytes_);
            for (const float step : {.25f, .5f}) {
                visualSelected_ = 20.f + step;
                if (!Render(0, spacing == 0 ? name + "-moving" + std::to_string(static_cast<int>(step * 100)) + ".png" : ""))
                    return false;
            }
        }
        SDL_Log("Cover layouts PASS: 4 layouts x 3 spacings, mixed case sizes, resting and moving%s",
                supersampled ? ", full shelf cached" : "");
        return true;
    }
#endif
    void SetStatus(std::string status, Uint32 lifetime = 3500) {
        status_ = std::move(status); statusUntil_ = SDL_GetTicks() + lifetime;
    }
    bool InjectionDialogOpen() const {return injectionDialog_.Open();}
    // Wii VC preparation is not a launcher injection, but it is still a
    // potentially long-running foreground operation.  Show the same modal
    // progress surface so the render/input loop remains visibly alive and the
    // user can cancel before the prepared copy is published.
    void BeginWiiVCPreparation(const std::string &name){
        DismissVideo(); status_.clear(); detailsBackdropReady_=false;
        injectionDialog_.Start(name);
    }
    bool GateInjection(const GameEntry &game) {
        const auto id=pendingInjection_.empty()?LaunchLauncher(game):pendingInjection_;
        const auto launcher=std::find_if(launchers_.begin(),launchers_.end(),[&](const auto &l){return l.id==id;});
        if(launcher==launchers_.end()||!launcher->enabled||launcher->kind!=LauncherKind::DSInjection||
           !LauncherAccepts(*launcher,game.platform)||!LauncherMatchesFile(*launcher,game.absolutePath))return false;
        DismissVideo();status_.clear();detailsBackdropReady_=false;injectionGame_=game;
        if(pendingInjection_.empty()&&!VCInject::HasSaved(game)){injectionDialog_.Ask(game.name);return true;}
        injectionDialog_.Start(game.name);return false;
    }
    bool ConsumeInjectionLaunch(GameEntry &game){
        if(!injectionDialog_.Consume())return false;
        game=injectionGame_;
        return true;
    }
    void InjectionInput(int direction,bool accept,bool back){
        injectionDialog_.Input(direction,accept,back);detailsBackdropReady_=false;
    }
    void InjectionTouch(int x,int y){injectionDialog_.Touch(x,y);detailsBackdropReady_=false;}
    bool InjectionCancelled() const {return injectionDialog_.cancel;}
    void InjectionProgress(const std::string &phase,uint64_t bytes,uint64_t total){injectionDialog_.Progress(phase,bytes,total);}
    void FinishInjection(const std::string &error){
        if(injectionDialog_.Open())injectionDialog_.Finish(error);
        else if(!error.empty())SetStatus(error,10000);
        detailsBackdropReady_=false;
    }
    bool CheckLaunchOptions(const std::string &path){
        for(int skin:{0,1}){
            VisualPreview("library");preferences_.skin=skin;
            if(OpenLaunchOptions()||LaunchOptionsOpen())return false; // preview titles have one method
            if(status_!="NO OTHER LAUNCH METHODS FOR THIS TITLE")return false;
            status_.clear(); // OpenLaunchOptions clears it when the popup does open
            LaunchMethod method=LaunchMethod::Default;
            launchOptions_=WiiVCLaunchOptions();
            launchOptionsOpen_=true;launchOptionsChoice_=0;launchOptionsName_="Donkey Kong Country Returns";
            if(!OverlayOpen()||!Render(0,path+"-"+std::to_string(skin)+".png"))return false;
            if(LaunchOptionsInput(1,false,false)!=LaunchMethod::Default||ConsumeLaunchOption(method))return false;
            if(!Render(0,path+"-injection-"+std::to_string(skin)+".png"))return false;
            if(LaunchOptionsInput(0,true,false)!=LaunchMethod::Default||!LaunchOptionsOpen()||
               ConsumeLaunchOption(method)||status_.find("Unavailable")!=0)return false;
            status_.clear();
            launchOptionsOpen_=true;
            if(LaunchOptionsInput(0,false,false,640,340)!=LaunchMethod::NintendoVC||
               !ConsumeLaunchOption(method)||method!=LaunchMethod::NintendoVC)return false;
            launchOptionsOpen_=true;
            if(LaunchOptionsInput(0,false,true)!=LaunchMethod::Default||LaunchOptionsOpen()||ConsumeLaunchOption(method))return false;
            // A DS title with three launchers: the pick applies to this title, once.
            const GameEntry *game=Selected();if(!game)return false;
            const auto fallback=LaunchLauncher(*game);
            launchOptions_={{"NooDS","Emulator  (default)",LaunchMethod::Launcher,"noods"},
                            {"Nintendo DS - VC injector","Virtual Console injection - first launch prepares it",LaunchMethod::Launcher,"ds-vc"},
                            {"Other emulator","Emulator",LaunchMethod::Launcher,"other"}};
            launchOptionsKey_=LauncherGameKey(*game);launchOptionsOpen_=true;launchOptionsChoice_=0;launchOptionsName_="Mario Kart DS";
            if(LaunchOptionsInput(1,false,false)!=LaunchMethod::Default||!Render(0,path+"-launchers-"+std::to_string(skin)+".png"))return false;
            if(LaunchOptionsInput(0,true,false)!=LaunchMethod::Launcher||!ConsumeLaunchOption(method)||method!=LaunchMethod::Launcher)return false;
            if(LaunchLauncher(*game)!="ds-vc")return false;
            GameEntry other=*game;other.absolutePath+=".other";
            if(LaunchLauncher(other)=="ds-vc")return false; // never leaks to another title
            ClearLaunchOverride();
            if(LaunchLauncher(*game)!=fallback)return false;
            const auto box=LaunchOptionsLayout();launchOptionsOpen_=true;
            if(LaunchOptionsInput(0,false,false,640,box.rows+2*box.step+30)!=LaunchMethod::Launcher||LaunchLauncher(*game)!="other")return false;
            ConsumeLaunchOption(method);ClearLaunchOverride();
            // + saves the highlighted launcher as this title's default and keeps
            // the popup open; the content type's own launcher clears the pin.
#ifdef UFLOW_HOST_PREVIEW
            std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
#endif
            const auto savedLaunchers=gameLaunchers_;const auto savedLibraries=libraries_;
            GameEntry &files=games_[visible_[selected_]];files.platform=Platform::Files;files.libraryId="ds";
            files.absolutePath="fs:/vol/external01/wiiu/roms/ds/Mario Kart DS.nds";
            LibraryDefinition ds;ds.id="ds";ds.name="Nintendo DS";ds.parser=Platform::Files;ds.launcherId="noods";
            libraries_.push_back(ds);
            launchOptions_={{"NooDS","Emulator  (default)",LaunchMethod::Launcher,"noods"},
                            {"Nintendo DS - VC injector","Virtual Console injection - first launch prepares it",LaunchMethod::Launcher,"ds-vc"}};
            launchOptionsKey_=LauncherGameKey(files);launchOptionsOpen_=true;launchOptionsChoice_=0;status_.clear();
            if(!LaunchOptionsCanSetDefault())return false;
            LaunchOptionsInput(1,false,false);
            if(LaunchOptionsInput(0,false,false,-1,-1,true)!=LaunchMethod::Default||!LaunchOptionsOpen()||ConsumeLaunchOption(method))return false;
            if(gameLaunchers_[launchOptionsKey_]!="ds-vc"||LoadLauncherOverrides()[launchOptionsKey_]!="ds-vc")return false;
            if(launchOptions_[1].note.find("(default)")==std::string::npos||launchOptions_[0].note.find("(default)")!=std::string::npos)return false;
            if(status_.find("DEFAULT FOR THIS GAME")!=0||!Render(0,path+"-default-"+std::to_string(skin)+".png"))return false;
            LaunchOptionsInput(-1,false,false);LaunchOptionsInput(0,false,false,-1,-1,true);
            if(gameLaunchers_.count(launchOptionsKey_)||LoadLauncherOverrides().count(launchOptionsKey_))return false;
            if(launchOptions_[0].note.find("(default)")==std::string::npos||status_.find("(CONTENT TYPE)")==std::string::npos)return false;
            launchOptionsOpen_=false;status_.clear();
            // Wii VC methods have no saved default: + changes nothing there.
            launchOptions_=WiiVCLaunchOptions();launchOptionsOpen_=true;launchOptionsChoice_=1;
            if(LaunchOptionsCanSetDefault()||LaunchOptionsInput(0,false,false,-1,-1,true)!=LaunchMethod::Default||!status_.empty())return false;
            launchOptionsOpen_=false;gameLaunchers_=savedLaunchers;libraries_=savedLibraries;
            if(!SaveLauncherOverrides(gameLaunchers_))return false;
            // More than five compatible launchers must remain reachable. The
            // viewport scrolls; neither the list nor a later default is cut off.
            const auto savedDefinitions=launchers_;
            for(int i=0;i<7;++i)launchers_.push_back({"extra_"+std::to_string(i),"Extra "+std::to_string(i),LauncherKind::Emulator,"DS",".nds","",true});
            launchOptions_=LaunchOptionsFor(files);
            if(launchOptions_.size()<7||launchOptions_.back().launcher!="extra_6")return false;
            launchOptionsOpen_=true;launchOptionsChoice_=0;launchOptionsKey_=LauncherGameKey(files);
            for(std::size_t i=1;i<launchOptions_.size();++i)LaunchOptionsInput(1,false,false);
            const auto scrolled=LaunchOptionsLayout();
            if(LaunchOptionsFirst()==0||scrolled.height>720||!Render(0,path+"-scroll-"+std::to_string(skin)+".png"))return false;
            if(LaunchOptionsInput(0,false,false,640,scrolled.rows+4*scrolled.step+20)!=LaunchMethod::Launcher||LaunchLauncher(files)!="extra_6")return false;
            ConsumeLaunchOption(method);ClearLaunchOverride();launchers_=savedDefinitions;
        }
        std::puts("PASS: launch options popup, Wii VC and launcher lists, both skins, D-pad/touch/cancel, one-shot per-title choice, + saves the title default");return true;
    }
    bool CheckInjectionDialog(const std::string &path){
        for(int skin:{0,1}){
            VisualPreview("library");preferences_.skin=skin;
            injectionDialog_.Ask("Bonk's Adventure (USA)");
            if(!OverlayOpen()||injectionDialog_.Consume())return false;
            if(!Render(0,path+"-confirm-"+std::to_string(skin)+".png"))return false;
            InjectionInput(1,true,false);if(!injectionDialog_.Consume()||injectionDialog_.Consume())return false;
            InjectionProgress("COPYING VC BASE",63*1024*1024,189*1024*1024);
            if(!Render(0,path+"-progress-"+std::to_string(skin)+".png"))return false;
            InjectionInput(0,false,true);if(!InjectionCancelled())return false;
            FinishInjection("INJECTION CANCELLED - ORIGINAL FILES UNCHANGED");
            if(!Render(0,path+"-cancel-"+std::to_string(skin)+".png"))return false;
            InjectionTouch(640,480);if(InjectionDialogOpen())return false;
        }
        std::puts("PASS: injection confirmation/progress/message popup, both skins, one-shot Yes and cancellation");return true;
    }
    const GameEntry *Selected() const { return visible_.empty() ? nullptr : &games_[visible_[selected_]]; }
    int AnimationSpeed() const {return preferences_.animationSpeed;}
    void Move(int amount) {
        DemoInput(true);
        if (visible_.empty() || libraryFadePhase_) return;
        detailsBackdropReady_ = false;
        if(amount) browseDirection_ = amount > 0 ? 1 : -1;
        int next = static_cast<int>(selected_) + amount;
        if (preferences_.wrapNavigation && visible_.size() > 1) {
            const int count = static_cast<int>(visible_.size());
            // Accelerated steps of four must not stall a four-title library.
            if (amount != 0 && amount % count == 0) next = static_cast<int>(selected_) + (amount > 0 ? 1 : -1);
            const int logicalNext=next;
            next = (next % count + count) % count;
            visualSelected_ += next-logicalNext;
        } else {
            next = std::clamp(next, 0, static_cast<int>(visible_.size()) - 1);
        }
        if(selected_!=static_cast<std::size_t>(next))MenuSound(MenuSounds::Move);
        selected_ = static_cast<std::size_t>(next);
    }
    // ZL/ZR: ten titles at a time, stopping at either end. Wrapping a jump past
    // the end of a short library reads as moving backwards (+10 of 14 is -4).
    // With wrap navigation a fresh press at an end continues from the other
    // end; held repeats stop there.
    static constexpr int kPageSize = 10;
    void Page(int direction, bool wrapAtEnd) {
        DemoInput(true);
        if (visible_.empty() || libraryFadePhase_ || direction == 0) return;
        detailsBackdropReady_ = false;
        const int last = static_cast<int>(visible_.size()) - 1, current = static_cast<int>(selected_);
        int next = std::clamp(current + (direction > 0 ? kPageSize : -kPageSize), 0, last);
        if (next == current && wrapAtEnd && preferences_.wrapNavigation && last > 0) {
            next = direction > 0 ? 0 : last;
            visualSelected_ = static_cast<float>(next); // jump, as a wrapping Move does
        }
        if(selected_!=static_cast<std::size_t>(next))MenuSound(MenuSounds::Tab);
        selected_ = static_cast<std::size_t>(next);
    }
    void ChangeTab(int amount) {
        const int count = static_cast<int>(LibraryTabs().size());
        if (count<2) return;
        if(amount)MenuSound(MenuSounds::Tab);
        pendingTab_ = ((pendingTab_>=0?pendingTab_:tab_) + amount + count) % count;
        // Keep the old library visible until its covers have completely faded out.
        // Further presses update the destination without resetting opacity.
        libraryFadePhase_=-1;
        details_ = false; detailsBackdropReady_=false;
    }
    void AdvanceLibraryFade(float seconds) {
        const float duration=preferences_.animationSpeed==0?.24f:preferences_.animationSpeed==1?.18f:.13f;
        const float amount=std::max(0.f,seconds)/duration;
        if(libraryFadePhase_<0) {
            libraryFade_=std::max(0.f,libraryFade_-amount);
            if(libraryFade_==0.f) {
                tab_=pendingTab_;pendingTab_=-1;
                selected_=0;visualSelected_=0;tabSlide_=0;
                RebuildVisible();libraryFadePhase_=1;
            }
        } else if(libraryFadePhase_>0) {
            libraryFade_=std::min(1.f,libraryFade_+amount);
            if(libraryFade_==1.f)libraryFadePhase_=0;
        }
    }
    bool CheckLibraryFade() {
        if(!VisualPreview("library"))return false;
        const int oldTab=tab_;const auto oldVisible=visible_;
        ChangeTab(1);
        if(tab_!=oldTab || visible_!=oldVisible || libraryFade_!=1.f)return false;
        AdvanceLibraryFade(.04f);
        if(tab_!=oldTab || libraryFade_<=0 || libraryFade_>=1)return false;
        ChangeTab(1); const int target=pendingTab_;
        AdvanceLibraryFade(1.f);
        if(tab_!=target || libraryFade_!=0 || libraryFadePhase_!=1)return false;
        AdvanceLibraryFade(.04f);
        if(libraryFade_<=0 || libraryFade_>=1)return false;
        ChangeTab(-1);AdvanceLibraryFade(1.f);AdvanceLibraryFade(1.f);
        if(libraryFade_!=1 || libraryFadePhase_ || pendingTab_!=-1)return false;
        for(int i=0;i<static_cast<int>(LibraryTabs().size())+1;++i) {
            ChangeTab(1);AdvanceLibraryFade(1.f);AdvanceLibraryFade(1.f);
            if(libraryFadePhase_ || libraryFade_!=1)return false;
        }
        std::puts("PASS: old library retained through fade-out, swap at zero, fade-in, rapid retarget, empty libraries and wrap");
        return true;
    }
    void ToggleFavorite() {
        if (!Selected()) return;
        detailsBackdropReady_ = false;
        auto &game = games_[visible_[selected_]];
        game.favorite = !game.favorite;
        if(!preview_) SaveFavorites(games_);
        SetStatus(game.favorite ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
        if (CurrentLibraryId() == kFavoritesLibraryId && !game.favorite) RebuildVisible();
    }
    void OpenCoverInspect(bool back) {
        if (!Selected()) return;
        if (coverInspectOpen_ && coverInspectBack_ == back) return;
        coverInspectOpen_ = true;
        coverInspectBack_ = back;
        detailsBackdropReady_ = false;
        if (back && Selected()->fullCoverPath.empty() && Selected()->backCoverPath.empty() && Selected()->defaultSleevePath.empty())
            SetStatus("BACK ART IS UNAVAILABLE - ADD A BACK OR DEFAULT SLEEVE");
    }
    void CloseCoverInspect() {
        coverInspectOpen_ = false;
        coverInspectBack_ = false;
        detailsBackdropReady_ = false;
    }
    bool CoverInspectOpen() const { return coverInspectOpen_ || coverInspectProgress_ > .01f; }
    bool InstallBusy() const {return titleInstallJob_.Poll().busy;}
    bool IsInstall(const GameEntry *g)const{return g && !g->installPackages.empty();}
    bool RefreshInstallStatus(){
        std::vector<TitleInstall::Package> all;
        for(auto &g:games_)for(auto &p:g.installPackages)all.push_back(p);
        if(all.empty())return false;
        if(!TitleInstall::Refresh(all))return false;size_t n=0;
        for(auto &g:games_)for(auto &p:g.installPackages)p.installedVersion[0]=all[n].installedVersion[0],p.installedVersion[1]=all[n++].installedVersion[1];
        return true;
    }
    void InstallSpace(){uint64_t bytes=0;std::string error;installSpace_=TitleInstall::FreeSpace(installTarget_,bytes,error)?TitleInstall::Size(bytes)+" free":error;}
    bool GateTitleInstall(const GameEntry &game){
        if(game.installPackages.empty())return false;
        if(!preferences_.titleInstallerEnabled || !TitleInstall::Available()){SetStatus("TITLE INSTALLER IS DISABLED");return true;}
        if(!details_)ToggleDetails();
        return true;
    }
    const GameEntry *InstalledLaunchGame() const {
        const auto *source=Selected();if(!IsInstall(source))return nullptr;
        const auto prefix=std::string("fs:/vol/storage_")+(installTarget_?"usb01":"mlc01")+"/";
        const auto found=std::find_if(games_.begin(),games_.end(),[&](const auto &g){
            return g.installed&&g.installPackages.empty()&&g.titleId==source->titleId&&g.absolutePath.starts_with(prefix);
        });
        return found==games_.end()?nullptr:&*found;
    }
    bool ConsumeInstalledLaunch(GameEntry &game) {
        if(!installLaunchRequested_)return false;
        installLaunchRequested_=false;
        const auto *installed=InstalledLaunchGame();
        if(!installed||InstallBusy())return false;
        game=*installed;return true;
    }
    void InstallInput(int horizontal,int vertical,bool accept,bool back){
        if(InstallBusy())return;
        auto *g=visible_.empty()?nullptr:&games_[visible_[selected_]];if(!IsInstall(g))return;
        if(back){details_=false;return;}
        const int count=static_cast<int>(g->installPackages.size());
        installRow_=std::clamp(installRow_+vertical,0,count+1+(InstalledLaunchGame()?1:0));
        if(installRow_==0&&(horizontal||accept)){installTarget_=1-installTarget_;installMessage_.clear();InstallSpace();return;}
        if(accept&&installRow_>0&&installRow_<=count){auto &p=g->installPackages[installRow_-1];
            if(p.error.empty()&&!p.Installed(installTarget_)){p.selected=!p.selected;if(p.selected)for(auto &other:g->installPackages)if(&other!=&p&&other.kind==p.kind)other.selected=false;}
            return;}
        if(accept&&installRow_==count+2&&InstalledLaunchGame()){installLaunchRequested_=true;return;}
        if(accept&&installRow_==count+1){
            if(!preferences_.titleInstallerEnabled||!TitleInstall::Available()){installMessage_="Title installer is disabled";return;}
            DismissVideo();StopMusic();
            if(titleInstallJob_.Start(g->installPackages,installTarget_)){installWasBusy_=true;installMessage_.clear();
#ifndef UFLOW_HOST_PREVIEW
                OSEnableHomeButtonMenu(false);
#endif
            }else installMessage_="Select an available package to install";
        }
    }
    void PumpTitleInstall(){
        const auto state=titleInstallJob_.Poll();
        if(installWasBusy_&&!state.busy){installWasBusy_=false;installMessage_=state.message;
            const auto key=Selected()?Selected()->absolutePath:std::string{};
            // Also reconcile partial jobs: a base game remains installed if a later DLC fails.
            if(RefreshInstallStatus()) {
                MergeInstalledPackages(games_);ClearSideCases();RebuildVisible();
                for(size_t i=0;i<visible_.size();++i)if(games_[visible_[i]].absolutePath==key){selected_=i;break;}
                visualSelected_=selected_;
                if(!SaveLibraryCache(games_))installMessage_+=" Library cache could not be saved.";
            }
#ifndef UFLOW_HOST_PREVIEW
            OSEnableHomeButtonMenu(true);
#endif
        }
    }
    void DrawInstallBadge(const GameEntry &g,SDL_Rect rect){
        if(g.installPackages.empty())return;
        const bool installed=std::all_of(g.installPackages.begin(),g.installPackages.end(),[](const auto&p){return p.Installed(0)||p.Installed(1);});
        const int side=std::clamp(rect.w/5,22,44),x=rect.x+rect.w-side-5,y=rect.y+5;
        FillRoundedRect(renderer_,{x,y,side,side},side/4,installed?Color{44,153,103,245}:Color{37,139,188,245});
        SetColor(renderer_,{255,255,255,255});
        if(installed){SDL_RenderDrawLine(renderer_,x+side/4,y+side/2,x+side/2,y+side*3/4);SDL_RenderDrawLine(renderer_,x+side/2,y+side*3/4,x+side*4/5,y+side/4);}
        else{SDL_RenderDrawLine(renderer_,x+side/2,y+side/5,x+side/2,y+side*3/5);SDL_RenderDrawLine(renderer_,x+side/4,y+side*2/5,x+side/2,y+side*3/5);SDL_RenderDrawLine(renderer_,x+side*3/4,y+side*2/5,x+side/2,y+side*3/5);SDL_RenderDrawLine(renderer_,x+side/4,y+side*4/5,x+side*3/4,y+side*4/5);}
    }
    void DrawInstallDetails(const GameEntry &g,Color accent,float progress){
        const auto pose=CoverTransition(progress);
        const int shift=int((1-pose.travel)*1200);const bool glass=preferences_.skin==1;
        const Color ink=glass?Color{43,63,74,255}:Color{232,241,249,255};
        const auto interpolate=[&](int from,int to){return int(from+(to-from)*pose.travel);};
        DrawBox(g,{interpolate(FocusCoverRect().x,48),interpolate(FocusCoverRect().y,98),interpolate(FocusCoverRect().w,310),interpolate(FocusCoverRect().h,465)},
                -1.f,true,accent,pose.yaw,pose.pitch,pose.roll,true);
        if(glass)DrawGlassSurface(renderer_,{370+shift,102,850,520},28,accent,96,true);
        else FillRoundedRect(renderer_,{370+shift,102,850,520},20,{22,34,50,250});
        const int x=400+shift;
        const auto button=[&](SDL_Rect rect,bool active){
            if(glass)DrawDetailGlassButton(renderer_,rect,active,accent);
            else FillRoundedRect(renderer_,rect,8,active?accent:Color{28,42,60,255});
        };
        const auto buttonInk=[&](bool active){return glass?Color{43,63,74,255}:active?AccentInk(accent):ink;};
        DrawFittedText(renderer_,g.name,x,125,780,3,ink);
        DrawText(renderer_,"INSTALL CONTENT  /  "+g.titleId,x,174,1,ink);
        button({x,208,780,48},installRow_==0);
        DrawText(renderer_,std::string("DESTINATION   < ")+(installTarget_?"USB STORAGE":"INTERNAL STORAGE")+" >",x+16,225,2,buttonInk(installRow_==0));
        DrawFittedText(renderer_,installSpace_,x,259,780,1,ink);
        const auto state=titleInstallJob_.Poll();
        const int first=std::clamp(installRow_-4,0,std::max(0,int(g.installPackages.size())-4));
        for(int i=first;i<std::min(first+4,int(g.installPackages.size()));++i){const auto &p=g.installPackages[i];int y=274+(i-first)*53;
            button({x,y,780,48},installRow_==i+1);
            const std::string status=!p.error.empty()?p.error:p.Installed(installTarget_)?"Installed":(p.Installed(1-installTarget_)?"Installed on other storage":"Not installed");
            DrawText(renderer_,std::string(p.selected&&!p.Installed(installTarget_)?"[X] ":"[ ] ")+TitleInstall::Name(p.kind)+"   v"+std::to_string(p.version)+"   "+TitleInstall::Size(p.bytes),x+14,y+7,2,buttonInk(installRow_==i+1));
            DrawFittedText(renderer_,status,x+14,y+29,748,1,buttonInk(installRow_==i+1));
        }
        if(state.busy){
            double ratio=state.total?std::min(1.,double(state.done)/state.total):0;
            FillRoundedRect(renderer_,{x,493,780,13},6,{110,130,145,80});
            if(ratio>0)FillRoundedRect(renderer_,{x,493,int(780*ratio),13},6,accent);
            DrawText(renderer_,state.message+"  "+std::to_string(int(ratio*100))+"%",x,519,2,ink);
            DrawText(renderer_,TitleInstall::Size(state.done)+" / "+TitleInstall::Size(state.total),x,551,1,ink);
        }else{
            DrawFittedText(renderer_,installMessage_,x,497,780,1,ink);
            const bool canInstall=std::any_of(g.installPackages.begin(),g.installPackages.end(),[&](const auto&p){return p.selected&&p.error.empty()&&!p.Installed(installTarget_);});
            const bool alreadyInstalled=std::all_of(g.installPackages.begin(),g.installPackages.end(),[&](const auto&p){return p.Installed(installTarget_);});
            const bool launchable=InstalledLaunchGame()!=nullptr;
            const int width=launchable?380:780;
            button({x,535,width,48},canInstall&&installRow_==int(g.installPackages.size())+1);
            DrawFittedText(renderer_,canInstall?"INSTALL SELECTED CONTENT":alreadyInstalled?"ALREADY INSTALLED":"SELECT CONTENT ABOVE",x+16,550,width-32,2,canInstall?buttonInk(installRow_==int(g.installPackages.size())+1):Color{140,154,165,255});
            if(launchable){
                const bool active=installRow_==int(g.installPackages.size())+2;
                button({x+400,535,380,48},active);
                DrawFittedText(renderer_,"LAUNCH INSTALLED GAME",x+416,550,348,2,buttonInk(active));
            }
        }
        DrawText(renderer_,state.busy?"INSTALLING - KEEP POWER CONNECTED":"UP / DOWN SELECT   A TOGGLE / INSTALL   B BACK",x,599,1,ink);
    }
    void ToggleDetails() {
        if (!Selected() || InstallBusy()) return;
        if(!details_&&IsInstall(Selected())){RefreshInstallStatus();installRow_=0;installMessage_.clear();InstallSpace();}
        coverInspectOpen_ = false;
        detailsBackdropReady_ = false;
        details_ = !details_; detailsRow_ = 0; detailsPage_ = 0; detailsLauncherEditing_=false;detailsPanelFocus_=false;detailsPanelRow_=0;
        MenuSound(details_?MenuSounds::Confirm:MenuSounds::Back);
        savedInjectionState_.erase(LauncherGameKey(*Selected()));
    }
    int DetailsGameRows() const {return Selected()&&!InjectionModule(*Selected()).empty()?3:2;}
    const char *DetailsAction() const {
        if(detailsRow_==2&&detailsPanelFocus_)return detailsPanelRow_==0?"SELECT LAUNCHER":detailsPanelRow_==DetailsGameRows()-1?"LAUNCHER SETTINGS":"INJECT / RE-INJECT";
        if(detailsRow_==4)return detailsPanelFocus_?(detailsPanelRow_==0?"PREVIOUS":detailsPanelRow_==2?"NEXT":detailsVideo_.Active()?"STOP VIDEO":"PLAY / VIEW"):"BROWSE MEDIA";
        static constexpr const char *actions[]={"TOGGLE FAVORITE","LAUNCH","EDIT LAUNCHER","LIBRARY SETTINGS","BROWSE MEDIA","RATE GAME"};
        return actions[detailsRow_];
    }
    void DetailsInput(int horizontal, int vertical, bool accept, bool back, bool more, bool toggle=false) {
        if (!details_ || !Selected()) return;
        if(toggle){ToggleDetails();return;}
        if(back)MenuSound(MenuSounds::Back);
        else if(accept || more)MenuSound(MenuSounds::Confirm);
        else if(horizontal || vertical)MenuSound(MenuSounds::Move);
        if(IsInstall(Selected())){InstallInput(horizontal,vertical,accept,back);return;}
        if(accept&&detailsRow_==5){if(!userStats_.Rate(*Selected()))SetStatus("COULD NOT SAVE RATING");return;}
        if(detailsLauncherEditing_) {
            if(back||vertical||accept||more) { detailsLauncherEditing_=false; if(back||accept)return; }
            else if(horizontal) { ChangeGameLauncher(horizontal); return; }
        }
        if(back) {if(detailsPanelFocus_){detailsPanelFocus_=false;return;}details_=false;return;}
        if(horizontal){
            if(horizontal<0&&detailsPanelFocus_){detailsPanelFocus_=false;return;}
            if(horizontal>0&&(detailsRow_==2||detailsRow_==4)){detailsPanelFocus_=true;return;}
            if(detailsRow_==0||detailsRow_==3||detailsRow_==5)detailsPage_+=horizontal;
            return;
        }
        if(vertical) {
            if(detailsPanelFocus_) {
                if(detailsRow_==2)detailsPanelRow_=(detailsPanelRow_+vertical+DetailsGameRows())%DetailsGameRows();
                else if(detailsRow_==4)detailsPanelRow_=(detailsPanelRow_+vertical+3)%3;
            } else {detailsRow_=(detailsRow_+vertical+6)%6;detailsPage_=0;detailsPanelRow_=0;}
        }
        if(accept&&detailsRow_==4&&!more){
            if(!detailsPanelFocus_){detailsPanelFocus_=true;detailsPanelRow_=1;return;}
            if(detailsPanelRow_==0)--detailsPage_;
            else if(detailsPanelRow_==2)++detailsPage_;
            else if(((detailsPage_%8)+8)%8==0)ToggleDetailsVideo();
            else ++detailsPage_;
            return;
        }
        if(accept&&detailsRow_==2&&!more) {
            if(!detailsPanelFocus_){detailsPanelFocus_=true;return;}
            if(detailsPanelRow_==0){detailsLauncherEditing_=true;return;}
            if(detailsPanelRow_==1&&DetailsGameRows()==3) {
                pendingInjection_=InjectionModule(*Selected());
                if(pendingInjection_.empty()){SetStatus("NO INJECTION MODULE FOR THIS GAME");return;}
                detailsLaunchRequested_=true;return;
            }
            settingsCategory_=5;ToggleSettings();return;
        }
        if (more || (accept && (detailsRow_==3 || detailsRow_==5))) {
            settingsCategory_=more?0:detailsRow_==3?1:5;
            ToggleSettings(); return;
        }
        if (!accept) return;
        if (detailsRow_==0) { ToggleFavorite(); if(!Selected()) details_=false; }
        else if (detailsRow_==1) detailsLaunchRequested_=true;
        else ++detailsPage_;
    }
    bool ConsumeDetailsLaunch() {
        const bool result=detailsLaunchRequested_; detailsLaunchRequested_=false; return result;
    }
    bool CheckTitleInstaller(){
        for(int skin=0;skin<2;++skin){
            if(!VisualPreview("detailsinstall"))return false;preferences_.skin=skin;
            auto &g=games_[visible_[selected_]];installRow_=0;
            InstallInput(1,0,false,false);if(installTarget_!=0)return false;
            InstallInput(0,1,true,false);if(g.installPackages[0].selected)return false;
            InstallInput(0,0,true,false);if(!g.installPackages[0].selected)return false;
            g.installPackages[0].installedVersion[0]=0;InstallInput(0,0,true,false);if(!g.installPackages[0].selected)return false;
            detailsProgress_=1;DetailsTouch(550,340,0);if(installRow_!=2||g.installPackages[1].selected)return false;
            DetailsInput(0,0,false,true,false);if(details_)return false;
            settings_=true;settingsCategory_=0;settingsSidebar_=false;settingsRow_=7;
            if(!SettingsSelectorRow(7)||SettingsFirstRow()!=2)return false;
            const auto before=preferences_.titleInstallerEnabled;SettingsAdjust(1);
            if(preferences_.titleInstallerEnabled==before)return false;
        }
        for(int skin=0;skin<2;++skin){
            VisualPreview("detailsinstall");preferences_.skin=skin;
            GameEntry launched;if(InstalledLaunchGame()||ConsumeInstalledLaunch(launched))return false;
            VisualPreview("detailsinstalled");preferences_.skin=skin;
            if(!InstalledLaunchGame())return false;InstallInput(0,0,true,false);
            if(!ConsumeInstalledLaunch(launched)||!launched.installed||!launched.installPackages.empty())return false;
            if(launched.absolutePath!="fs:/vol/storage_usb01/usr/title/00050000/10180600"||ConsumeInstalledLaunch(launched))return false;
            installTarget_=0;if(InstalledLaunchGame())return false;installTarget_=1;
            detailsProgress_=1;DetailsTouch(1000,555,0);if(!ConsumeInstalledLaunch(launched))return false;
        }
        std::puts("PASS: installer details selection, destination, installed lockout, touch, back and settings toggle in both skins");return true;
    }
    bool CheckDetailsMediaVideo(const std::string &output){
        const char *fixture=std::getenv("UFLOW_DETAILS_VIDEO_TEST");if(!fixture)return false;
        for(int skin:{0,1}){
            VisualPreview("detailsmedia");preferences_.skin=skin;games_[visible_[selected_]].previewPath=fixture;
            detailsPage_=0;detailsPanelFocus_=true;detailsPanelRow_=1;Render(0);ToggleDetailsVideo();
            const auto start=SDL_GetTicks();
            while(!detailsVideo_.Ready()&&SDL_GetTicks()-start<6000){Render(0);SDL_Delay(5);}
            if(!detailsVideo_.Ready()||video_.Active())return false;
            Render(0,output+(skin?"-glass.png":"-pixel.png"));
            detailsRow_=0;Render(0);if(detailsVideo_.Active())return false;
            const auto stop=SDL_GetTicks();while(!detailsVideo_.CanStart()&&SDL_GetTicks()-stop<3000)SDL_Delay(5);
            if(!detailsVideo_.CanStart())return false;
            detailsRow_=4;ToggleDetailsVideo();
            const auto again=SDL_GetTicks();while(!detailsVideo_.Ready()&&SDL_GetTicks()-again<6000){Render(0);SDL_Delay(5);}
            if(!detailsVideo_.Ready())return false;
            PrepareSystemBackground();if(detailsVideo_.Active()||detailsVideoPending_)return false;
        }
        std::puts("PASS: embedded media video, both skins, library decoder exclusion, tab exit, restart and HOME cleanup");return true;
    }
    bool CheckDetails() {
        if(!VisualPreview("details"))return false;
        if(detailsRow_!=0)return false;
        DetailsInput(0,0,true,false,false); if(!Selected()->favorite)return false;
        DetailsInput(0,0,true,false,false); if(Selected()->favorite)return false;
        DetailsInput(0,-1,false,false,false); if(detailsRow_!=5)return false;
        DetailsInput(0,1,false,false,false); if(detailsRow_!=0)return false;
        DetailsInput(0,1,false,false,false); DetailsInput(0,0,true,false,false);
        if(!ConsumeDetailsLaunch()||ConsumeDetailsLaunch())return false;
        DetailsInput(0,1,false,false,false);DetailsInput(-1,0,false,false,false);
        if(detailsPage_!=0||detailsPanelFocus_)return false;
        DetailsInput(1,0,false,false,false);if(!detailsPanelFocus_)return false;
        DetailsInput(0,0,true,false,false);if(!detailsLauncherEditing_)return false;
        DetailsInput(0,0,false,true,false);if(detailsLauncherEditing_||!detailsPanelFocus_)return false;
        DetailsInput(-1,0,false,false,false);if(detailsPanelFocus_)return false;
        DetailsInput(0,1,false,false,false);DetailsInput(0,0,true,false,false);
        if(!settings_||details_||settingsCategory_!=1)return false;
        VisualPreview("details");DetailsTouch(430,430,0);
        if(detailsRow_!=4||detailsPage_!=0||detailsPanelFocus_)return false;
        DetailsTouch(430,193,0);if(detailsRow_!=4)return false; // rail gap
        DetailsInput(0,0,false,true,false);if(details_)return false;
        VisualPreview("detailsinfo");DetailsInput(0,0,true,false,false);
        if(!settings_||settingsCategory_!=5)return false;
        for(int skin:{0,1}) {
            VisualPreview("detailslauncher");preferences_.skin=skin;
            if(DetailsGameRows()!=2)return false;
            DetailsInput(1,0,false,false,false);DetailsInput(0,1,false,false,false);
            if(detailsPanelRow_!=1||std::string(DetailsAction())!="LAUNCHER SETTINGS")return false;
            DetailsInput(0,0,true,false,false);
            if(ConsumeDetailsLaunch()||!pendingInjection_.empty())return false;
            if(!settings_||details_||settingsCategory_!=5)return false;
            VisualPreview("detailslauncher");preferences_.skin=skin;
            DetailsTouch(600,410,0); // No invisible third action on native games.
            if(detailsRow_!=2||detailsPanelFocus_||ConsumeDetailsLaunch())return false;
            VisualPreview("detailslauncher");DetailsInput(1,0,false,false,false);if(detailsPanelRow_!=0)return false;
            VisualPreview("detailsinject");preferences_.skin=skin;
            if(DetailsGameRows()!=3||std::string(DetailsAction())!="INJECT / RE-INJECT")return false;
            DetailsInput(0,0,true,false,false);
            if(!ConsumeDetailsLaunch()||pendingInjection_!="ds-vc")return false;
            pendingInjection_.clear();
            auto &g=games_[visible_[selected_]];
            for(auto platform:{Platform::WiiU,Platform::Wii,Platform::GameCube}) {
                g.platform=platform;if(DetailsGameRows()!=2)return false;
            }
            g.platform=Platform::Files;g.absolutePath="fs:/vol/external01/other/test.txt";
            if(DetailsGameRows()!=2)return false;
        }
        std::puts("PASS: injection action only on supported VC ROMs; native/Wii/GC/unsupported files excluded; touch and controller row bounds");
        for(const auto &system:VCSystems::all){
            VisualPreview("detailsinject");auto &g=games_[visible_[selected_]];
            g.absolutePath="fs:/vol/external01/roms/test"+std::string(system.extensions).substr(0,std::string(system.extensions).find(','));
            launchers_.push_back({std::string(system.id)+"-vc",system.name,LauncherKind::DSInjection,system.name,system.extensions,"",true,std::string(system.id)+"-vc"});
            if(DetailsGameRows()!=3)return false;
            savedInjectionState_[LauncherGameKey(g)]=false;
            const auto choices=GameLauncherChoices(g);
            if(std::find(choices.begin(),choices.end(),std::string(system.id)+"-vc")==choices.end())return false;
            if(GameLauncherLabel(g,std::string(system.id)+"-vc")!="AUTO-INJECT ON PLAY")return false;
            VisualPreview(std::string("launcher")+system.id);
            // Settings rows only: name, method, system, extensions, base, enabled,
            // DS options or the cache limit, save, remove.
            if(LauncherRows()!=(std::string(system.id)=="ds"?12:9))return false;
            launcherRow_=LauncherPhysicalRow(InjectionCacheRow());settingsEditing_=true;const int old=injectionDraft_.cacheMiB;
            LauncherInput(1,0,false,false);if(injectionDraft_.cacheMiB!=old+512)return false;
        }
        std::puts("PASS: all seven VC systems expose injection and system-appropriate profile rows");
        for(int skin:{0,1})for(const char *screen:{"details","detailsinstall"}) {
            if(!VisualPreview(screen))return false;preferences_.skin=skin;
            detailsPanelFocus_=true;detailsLauncherEditing_=true;
            DetailsInput(0,0,false,false,false,true);if(details_)return false;
            ToggleDetails();if(!details_)return false;
        }
        for(int skin:{0,1}){
            VisualPreview("details");preferences_.skin=skin;
            for(int tab:{0,1,3,5}){detailsRow_=tab;detailsPanelFocus_=false;DetailsInput(1,0,false,false,false);if(detailsPanelFocus_||detailsRow_!=tab)return false;}
            detailsRow_=4;detailsPage_=0;DetailsInput(1,0,false,false,false);if(!detailsPanelFocus_)return false;
            DetailsInput(-1,0,false,false,false);if(detailsPanelFocus_)return false;
            const int rail=skin==1?430:410;DetailsTouch(rail,220,0);if(detailsRow_!=1||ConsumeDetailsLaunch())return false;
        }
        std::puts("PASS: details navigation, wrap, favorite, launch request, pages, touch and settings routes");
        return true;
    }
    void DetailsTouch(int x,int y,int drag) {
        if (!details_ || detailsProgress_<.98f || std::abs(drag)>20) return;
        if(IsInstall(Selected())){
            if(InstallBusy())return;
            if(x<400||x>1180)return;
            if(y>=208&&y<256)installRow_=0;
            else if(y>=274&&y<486){const int first=std::clamp(installRow_-4,0,std::max(0,int(Selected()->installPackages.size())-4));installRow_=first+(y-274)/53+1;if(installRow_>int(Selected()->installPackages.size()))return;}
            else if(y>=535&&y<583){if(InstalledLaunchGame()&&x>=780&&x<800)return;installRow_=Selected()->installPackages.size()+1+(InstalledLaunchGame()&&x>=800?1:0);}
            else return;
            InstallInput(0,0,true,false);return;
        }
        const int contentX=preferences_.skin==1?512:490;
        if(detailsRow_==4&&x>=contentX&&x<contentX+620&&y>=490&&y<528){
            const int column=(x-contentX)/210;if((x-contentX)%210>=200)return;
            detailsPanelFocus_=true;detailsPanelRow_=column;DetailsInput(0,0,true,false,false);return;
        }
        if(detailsRow_==2&&x>=contentX&&x<contentX+620&&y>=283&&y<449) {
            const int row=(y-283)/56;
            if(row>=DetailsGameRows()||(y-283)%56>=50)return;
            detailsPanelFocus_=true;detailsPanelRow_=row;
            if(row==0&&detailsLauncherEditing_){ChangeGameLauncher(x<822?-1:1);return;}
            detailsLauncherEditing_=false;DetailsInput(0,0,true,false,false);return;
        }
        if(preferences_.skin!=1) {
            if(x>=372 && x<448 && y>=133 && y<541 && (y-133)%70<58) {
                detailsPanelFocus_=false;detailsLauncherEditing_=false;detailsRow_=(y-133)/70;detailsPage_=0;
            } else if(y>=545 && y<585) {
                if(x>=490 && x<830)DetailsInput(0,0,true,false,false);
                else if(x>=840 && x<965)DetailsInput(0,0,false,true,false);
                else if(x>=965 && x<1170)DetailsInput(0,0,false,false,true);
            }
            return;
        }
        if(x>=402 && x<462 && y>=133 && y<541) {
            const int row=(y-133)/70;
            if((y-133)%70>=58) return;
            detailsPanelFocus_=false;detailsLauncherEditing_=false;detailsRow_=row; detailsPage_=0;
        } else if(y>=550 && y<594) {
            if(x>=512 && x<800) DetailsInput(0,0,true,false,false);
            else if(x>=805 && x<960) DetailsInput(0,0,false,true,false);
            else if(x>=970 && x<1185) DetailsInput(0,0,false,false,true);
        }
    }
    void ToggleSettings() {
        if(settings_ && ConfirmDraftExit(DraftExit::Settings))return;
        settingsAction_=-1;
        detailsBackdropReady_ = false;
        settings_=!settings_; details_=false;
        MenuSound(settings_?MenuSounds::Confirm:MenuSounds::Back);
        settingsSidebar_=true;
        settingsEditing_=false;
        audioSettings_=false;
        sourceManager_=settings_ && (settingsCategory_==1 || settingsCategory_==2);
        mediaMode_=settingsCategory_==2; mediaHelp_=sourceEditor_=directoryBrowser_=false;
    }
    bool WelcomeOpen() const { return welcomeOpen_; }
    void DismissWelcome(bool openSettings=false) {
        if(!welcomeOpen_)return;
        welcomeOpen_=false;detailsBackdropReady_=false;preferences_.welcomeDismissed=true;
        if(!preview_) { settingsDirty_=true;FlushSettingsSave(true); }
        if(openSettings)ToggleSettings();
    }
    bool CheckFirstRun(const std::string &path) {
        VisualPreview("library");games_.clear();preferences_=UserSettings{};
        RebuildVisible();welcomeOpen_=true;
        if(preferences_.skin!=1 || LibraryTabs()!=std::vector<std::string>{kFavoritesLibraryId} || !OverlayOpen())return false;
        if(!Render(0,path+"-welcome.png"))return false;
        DismissWelcome();
        if(WelcomeOpen()||!preferences_.welcomeDismissed || !Render(0,path+"-empty.png"))return false;
        GameEntry game;game.platform=Platform::Wii;game.libraryId="wii";game.name="Test Wii game";games_.push_back(game);RebuildVisible();
        if(LibraryTabs()!=std::vector<std::string>{kAllLibraryId,"wii",kFavoritesLibraryId})return false;
        tab_=1;preferences_.showWii=false;RebuildVisible();
        if(CurrentLibraryId()!=kFavoritesLibraryId || tab_!=0 || !visible_.empty())return false;
        std::puts("PASS: Glass Flow default, one-time welcome, empty/fill/hide tabs and removed-tab clamping");return true;
    }
    bool OverlayOpen(bool includeInspection=true) const { return galleryOpen_ || powerPrompt_ || alphabetOpen_ || alphabetProgress_>.01f || searchOpen_ || sortOpen_ || launchOptionsOpen_ || scanOpen_ || updateOpen_ || WelcomeOpen() || InjectionDialogOpen() || (includeInspection && CoverInspectOpen()) || details_ || settings_ || sourceManager_ || sourceEditor_ || directoryBrowser_; }
    bool ScanOpen() const {return scanOpen_;}
    void StartScan() {
        if(scanOpen_)return;
        Trace("scan-request-before-stop");
        mediaRefresh_=false;
        sourceMediaRefreshPending_=false;
        FlushSettingsSave(true);BrowserCancel();DismissVideo();StopMusic();
        scanState_={};scanState_.progress.stage="STARTING SCAN";scanApplied_=false;scanCancelling_=false;
        scanOpen_=true;scanStarted_=SDL_GetTicks();scanElapsed_=0;detailsBackdropReady_=false;
#ifndef UFLOW_HOST_PREVIEW
        USBStorage::LogReadStats("scan-start");
#endif
        scanStartPending_=true;
        scanFirstFrame_=true;
        Trace("scan-request-ready");
    }
    void StopScan(){scanStartPending_=false;scanWorker_.Stop();}
    void CancelScan(){
        if(scanOpen_ && !scanState_.finished && scanState_.progress.stage!="SAVING LIBRARY"){
            if(scanStartPending_){
                scanStartPending_=false;scanApplied_=true;scanState_.finished=true;
                scanState_.progress.stage="SCAN CANCELLED";return;
            }
            scanCancelling_=true;scanWorker_.RequestCancel();
        }
    }
    void ScanInput(bool close) {
        if(scanOpen_ && scanState_.finished && close){scanOpen_=false;detailsBackdropReady_=false;ApplyMusic();}
    }
    void PollScan() {
        if((mediaRefreshPending_||sourceMediaRefreshPending_)&&!scanOpen_&&!settings_&&!details_&&!InstallBusy()&&!preview_){
            mediaRefreshPending_=false;mediaRefresh_=true;
            mediaRefreshSelection_=Selected()?Selected()->absolutePath:"";
            FlushSettingsSave(true);DismissVideo();StopMusic();
            scanState_={};scanState_.progress.stage=sourceMediaRefreshPending_?"UPDATING MEDIA":"UPDATING CONTENT TYPE MEDIA";scanApplied_=false;scanCancelling_=false;
            scanOpen_=true;scanStarted_=SDL_GetTicks();scanElapsed_=0;detailsBackdropReady_=false;
            scanRefreshSources_=sourceMediaRefreshPending_;scanStartPending_=true;
            scanFirstFrame_=true;
            sourceMediaRefreshPending_=false;
        }
        if(!scanOpen_ || scanApplied_)return;
        scanElapsed_=(SDL_GetTicks()-scanStarted_)/1000;
        if(scanStartPending_){
            // Stop is asynchronous. Keep drawing/cancelling the scan dialog
            // while all decoder workers release their queues and frame leases.
            if(scanFirstFrame_ && !preview_)return;
            const bool videoReady=video_.CanStart();
            const bool detailsReady=detailsVideo_.CanStart();
            const bool backgroundReady=glassBackground_.Released();
            if(!videoReady || !detailsReady || !backgroundReady){
                scanState_.progress.stage="FINISHING VIDEO PLAYBACK";return;
            }
            scanStartPending_=false;
            Trace("scan-worker-start");
            if(mediaRefresh_)scanWorker_.StartMedia(games_,libraries_,scanRefreshSources_);
            else scanWorker_.Start(games_,scanAddOnly_);
        }
        scanState_=scanWorker_.Poll();scanElapsed_=(SDL_GetTicks()-scanStarted_)/1000;
        if(scanState_.finished){
#ifndef UFLOW_HOST_PREVIEW
            USBStorage::LogReadStats("scan-end");
#endif
            scanApplied_=true;
            if(scanState_.error.empty()){
                auto result=scanWorker_.Take();
                scanInvalid_=result.invalidWiiU;
                ReplaceGames(std::move(result.games));
                if(mediaRefresh_){
                    for(size_t i=0;i<visible_.size();++i)if(games_[visible_[i]].absolutePath==mediaRefreshSelection_){selected_=i;visualSelected_=float(i);break;}
                    scanOpen_=false;ApplyMusic();
                    if(!scanState_.cacheSaved)SetStatus("MEDIA UPDATED - CACHE SAVE PENDING",5000);
                }
            }
        }
    }
    // Every way this title can start: the compatible launchers (the same list
    // as Details > Game launcher), or Nintendo VC / injection for Wii VC titles.
    static std::vector<LaunchOption> WiiVCLaunchOptions() {
        return {{"NINTENDO VC","Unchanged title - GamePad works in games that support it",LaunchMethod::NintendoVC,{}},
                {"INJECTION","Unavailable - Wii VC conversion needs repair",LaunchMethod::Injection,{},false}};
    }
    std::string DefaultLauncherFor(const GameEntry &game,const LauncherOverrides &overrides) const {
        auto current=EffectiveLauncher(game,overrides,sources_,libraries_);
        if(current.empty()&&game.platform!=Platform::Files)
            current=game.platform==Platform::WiiU?"native":game.launcher==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
        return current;
    }
    std::vector<LaunchOption> LaunchOptionsFor(const GameEntry &game) const {
        if(WiiVC::IsRaw(game))return WiiVCLaunchOptions();
        const std::string current=DefaultLauncherFor(game,gameLaunchers_);
        std::vector<LaunchOption> options;
        for(const auto &id:GameLauncherChoices(game)) {
            const auto l=std::find_if(launchers_.begin(),launchers_.end(),[&](const auto &x){return x.id==id;});
            if(id.empty()||l==launchers_.end())continue;
            std::string note;
            switch(l->kind) {
                case LauncherKind::Native: note="Direct launch"; break;
                case LauncherKind::WiiFlow: note=l->id=="nintendont"?"GameCube through Nintendont":"Wii / GameCube loader in vWii"; break;
                case LauncherKind::USBLoaderGX: note="Wii / GameCube loader in vWii"; break;
                case LauncherKind::Emulator: note="Emulator"; break;
                case LauncherKind::DSInjection:
                    note=SavedInjection(game)?"Saved Virtual Console injection":"Virtual Console injection - first launch prepares it"; break;
                case LauncherKind::VirtualConsole: note="Prepared Virtual Console injection"; break;
            }
            if(id==current)note+="  (default)";
            options.push_back({l->name,note,LaunchMethod::Launcher,id});
        }
        return options;
    }
    bool OpenLaunchOptions() {
        const GameEntry *game=Selected();
        if(!game||OverlayOpen())return false;
        auto options=LaunchOptionsFor(*game);
        if(options.size()<2){SetStatus("NO OTHER LAUNCH METHODS FOR THIS TITLE",4000);return false;}
        launchOptionsChoice_=0;
        for(std::size_t i=0;i<options.size();++i)if(options[i].note.find("(default)")!=std::string::npos)launchOptionsChoice_=static_cast<int>(i);
        launchOptions_=std::move(options);
        launchOptionsOpen_=true;launchOptionsName_=game->name;launchOptionsKey_=LauncherGameKey(*game);
        status_.clear(); // the hint row shows popup messages; drop an older one
        DismissVideo();detailsBackdropReady_=false;
        return true;
    }
    bool LaunchOptionsOpen() const {return launchOptionsOpen_;}
    int LaunchOptionsFirst() const {
        return std::max(0,launchOptionsChoice_-static_cast<int>(kMaxLaunchOptions)+1);
    }
    struct LaunchOptionsBox {int top,height,rows,step,row;};
    LaunchOptionsBox LaunchOptionsLayout() const {
        const int count=std::max(1,static_cast<int>(std::min(launchOptions_.size(),kMaxLaunchOptions))),step=84,height=170+count*step;
        const int top=360-height/2;
        return {top,height,top+130,step,72};
    }
    // Returns the chosen method, or Default while open / after cancel.
    // `makeDefault` (+) saves the highlighted launcher as this title's default
    // and keeps the popup open.
    LaunchMethod LaunchOptionsInput(int direction,bool accept,bool back,int x=-1,int y=-1,bool makeDefault=false){
        if(!launchOptionsOpen_)return LaunchMethod::Default;
        const int count=static_cast<int>(launchOptions_.size());
        if(direction)launchOptionsChoice_=std::clamp(launchOptionsChoice_+(direction>0?1:-1),0,std::max(0,count-1));
        const auto box=LaunchOptionsLayout();
        const int first=LaunchOptionsFirst();
        for(int row=0;row<static_cast<int>(kMaxLaunchOptions)&&first+row<count;++row)
            if(x>=340&&x<940&&y>=box.rows+row*box.step&&y<box.rows+row*box.step+box.row){launchOptionsChoice_=first+row;accept=true;}
        if(back){launchOptionsOpen_=false;detailsBackdropReady_=false;return LaunchMethod::Default;}
        if(makeDefault&&!accept){SetLaunchOptionDefault();return LaunchMethod::Default;}
        if(!accept||launchOptionsChoice_>=count)return LaunchMethod::Default;
        const auto &option=launchOptions_[launchOptionsChoice_];
        if(!option.available){SetStatus(option.note,6000);return LaunchMethod::Default;}
        launchOptionsOpen_=false;detailsBackdropReady_=false;
        chosenLaunch_=option.method;
        if(option.method==LaunchMethod::Launcher){launchOverrideKey_=launchOptionsKey_;launchOverrideId_=option.launcher;}
        return chosenLaunch_;
    }
    // Only launcher lists have a saved default; Wii VC titles always start
    // through Nintendo VC unless Injection is picked for that launch.
    bool LaunchOptionsCanSetDefault() const {
        return std::any_of(launchOptions_.begin(),launchOptions_.end(),[](const auto &o){return o.method==LaunchMethod::Launcher;});
    }
    // The same per-title choice as Details > Game launcher. Picking the
    // launcher the title would use anyway clears its pin, so it follows the
    // content type again.
    void SetLaunchOptionDefault() {
        if(launchOptionsChoice_<0||launchOptionsChoice_>=static_cast<int>(launchOptions_.size()))return;
        const LaunchOption option=launchOptions_[launchOptionsChoice_];
        const GameEntry *game=Selected();
        if(option.method!=LaunchMethod::Launcher||!game||LauncherGameKey(*game)!=launchOptionsKey_)return;
        auto next=gameLaunchers_;
        next.erase(launchOptionsKey_);
        const bool inherited=option.launcher==DefaultLauncherFor(*game,next);
        if(!inherited)next[launchOptionsKey_]=option.launcher;
        if(!SaveLauncherOverrides(next)){SetStatus("COULD NOT SAVE DEFAULT LAUNCHER",2500);return;}
        gameLaunchers_=std::move(next);
        const auto current=DefaultLauncherFor(*game,gameLaunchers_);
        for(auto &o:launchOptions_) {
            const auto marker=o.note.find("  (default)");
            if(marker!=std::string::npos)o.note.erase(marker);
            if(o.method==LaunchMethod::Launcher&&o.launcher==current)o.note+="  (default)";
        }
        SetStatus(inherited?"DEFAULT: "+option.title+" (CONTENT TYPE)":"DEFAULT FOR THIS GAME: "+option.title,2500);
    }
    // One-shot: the next main-loop pass launches the selected title this way.
    bool ConsumeLaunchOption(LaunchMethod &method){
        if(chosenLaunch_==LaunchMethod::Default)return false;
        method=chosenLaunch_;chosenLaunch_=LaunchMethod::Default;return true;
    }
    // The launcher for this launch: a Launch Options pick for this title, else its default.
    std::string LaunchLauncher(const GameEntry &g) const {
        if(!launchOverrideId_.empty()&&launchOverrideKey_==LauncherGameKey(g))return launchOverrideId_;
        return EffectiveLauncher(g,gameLaunchers_,sources_,libraries_);
    }
    void ClearLaunchOverride(){launchOverrideKey_.clear();launchOverrideId_.clear();}
    bool UpdateOpen() const {return updateOpen_;}
    void UpdateState(const UFlowUpdate::Snapshot &s){
        const bool wants=s.state==UFlowUpdate::State::Available||UFlowUpdate::Busy(s.state)||s.state==UFlowUpdate::State::Success||s.state==UFlowUpdate::State::Error;
        if(!updateOpen_&&wants&&!OverlayOpen()&&!PreviewActive()){
            updateOpen_=true;updateYes_=false;updateScroll_=0;DismissVideo();StopMusic();detailsBackdropReady_=false;
        }
        updateState_=s;
    }
    // 1 = accept, -1 = decline/close; busy input never leaks into the library.
    int UpdateInput(int direction,bool accept,bool back,int x=-1,int y=-1){
        if(!updateOpen_||UFlowUpdate::Busy(updateState_.state))return 0;
        if(updateState_.state==UFlowUpdate::State::Available){
            if(direction)updateYes_=direction>0;
            if(y>=475&&y<530){if(x>=380&&x<620){updateYes_=false;accept=true;}else if(x>=660&&x<900){updateYes_=true;accept=true;}}
            if(accept&&updateYes_&&!back)return 1;
        }else{
            if(direction)updateScroll_=std::clamp(updateScroll_+direction,0,std::max(0,int(updateLines_.size())-6));
            if(y>=475&&y<530&&x>=500&&x<780)accept=true;
        }
        if(accept||back){updateOpen_=false;detailsBackdropReady_=false;ApplyMusic();return -1;}return 0;
    }
    bool CheckUpdateDialog(const std::string &path) {
        VisualPreview("library");welcomeOpen_=false;preview_=false;
        UFlowUpdate::Snapshot s;s.state=UFlowUpdate::State::Available;s.version="2026.09.20-test1";
        s.notes="Added signed software updates.\nAdded download and installation progress.\nAdded recovery for interrupted installations.";
        settings_=true;UpdateState(s);if(UpdateOpen())return false;settings_=false;
        UpdateState(s);if(!UpdateOpen()||!Render(0,path+"-available.png"))return false;
        if(UpdateInput(0,true,false)!=-1||UpdateOpen())return false;
        UpdateState(s);if(UpdateInput(1,true,false)!=1)return false;
        s.state=UFlowUpdate::State::Downloading;s.done=40;s.total=100;s.message="Downloading update...";UpdateState(s);
        if(UpdateInput(0,true,true)!=0||!UpdateOpen()||!Render(0,path+"-progress.png"))return false;
        s.state=UFlowUpdate::State::Handoff;UpdateState(s);
        if(UpdateInput(0,true,true)!=0||!UpdateOpen())return false;
        s.state=UFlowUpdate::State::Success;UpdateState(s);
        if(!Render(0,path+"-success.png")||gPopupTextGlow)return false;
        preferences_.skin=0;if(!Render(0,path+"-pixel-success.png")||gPopupTextGlow)return false;
        preferences_.skin=1;
        if(UpdateInput(0,true,false)!=-1)return false;
        s.state=UFlowUpdate::State::Error;s.message="Downloaded update failed verification";UpdateState(s);
        if(!Render(0,path+"-error.png")||UpdateInput(0,false,true)!=-1)return false;
        std::puts("PASS: update prompt, default No, accept, busy input, success and failure dialogs");return true;
    }
    bool CheckBrowserNavigation(const std::string &path){
        VisualPreview("directories");directoryBrowser_=true;defaultFileBrowser_=false;
        browserPath_="fs:/vol/external01";RefreshBrowser();
        if(browserDirectories_.empty()||browserDirectories_[0]!="../")return false;
        BrowserOpenSelected();if(browserPath_!="fs:/vol"||!directoryBrowser_)return false;
        BrowserBack();if(browserPath_!="fs:/"||!directoryBrowser_)return false;
        if(browserDirectories_.empty()||browserDirectories_[0]!="vol")return false;
        BrowserOpenSelected();if(browserPath_!="fs:/vol")return false;
        BrowserBack();
        BrowserBack();if(browserPath_!="fs:/"||!directoryBrowser_)return false;
        const auto before=DraftPath();BrowserSelectCurrent();if(!directoryBrowser_||DraftPath()!=before)return false;
        browserPath_="fs:/vol";browserDirectories_={"../","external01","external02"};browserRow_=2;
        BrowserOpenSelected();if(browserPath_!="fs:/vol/external02"||!directoryBrowser_)return false;
        if(!Render(0,path+"-parent.png"))return false;
        defaultFileBrowser_=true;draftMediaSource_.type=MediaType::FullCover;draftMediaSource_.defaultFile="keep.png";RefreshBrowser();
        BrowserOpenSelected();if(browserPath_!="fs:/vol"||draftMediaSource_.defaultFile!="keep.png"||!directoryBrowser_)return false;
        browserRow_=1;BrowserOpenSelected();if(directoryBrowser_||!draftMediaSource_.defaultFile.empty())return false;
        directoryBrowser_=true;BrowserCancel();if(directoryBrowser_)return false;
        std::puts("PASS: parent entry, Back at volume/root, sibling drive, root selection guard, default-file selection, cancel");return true;
    }
    bool SettingsOpen() const { return settings_; }
    bool ButtonPressed(ControlAction action, std::uint32_t trigger) const {
        if(controlTransitionBlocked_)return false;
        if(libraryFadePhase_ && action!=ControlAction::PreviousLibrary && action!=ControlAction::NextLibrary)return false;
        const auto routed=controlChordInput_.initialized?(trigger&controlChordInput_.liveTrigger):trigger;
        return ControlPressed(preferences_,action,controlHeld_,routed)||
            (preferences_.bindings[int(action)]&controlChordInput_.releasedSingles)!=0;
    }
    bool ButtonHeld(ControlAction action, std::uint32_t hold) const {
        // The active held selector owns its activation binding across the menu transition.
        if(action==ControlAction::Alphabet && alphabetOpen_ && alphabetHeldMode_)
            return ControlHeld(preferences_,action,hold);
        if(controlTransitionBlocked_)return false;
        if(libraryFadePhase_ && action!=ControlAction::PreviousLibrary && action!=ControlAction::NextLibrary)return false;
        const auto routed=controlChordInput_.initialized?(hold&controlChordInput_.liveHold):hold;
        return ControlHeld(preferences_,action,routed)||
            (preferences_.bindings[int(action)]&controlChordInput_.releasedSingles)!=0;
    }
    bool CapturingBinding() const { return bindingCapture_ >= 0; }
    void BeginBindingCapture(int action,bool append) {
        bindingCapture_=action;bindingAppend_=append;bindingCaptureState_.Begin();
        SetStatus("HOLD BUTTONS TOGETHER, RELEASE TO ASSIGN");
    }
    void CaptureBinding(std::uint32_t held) {
        const auto choice=bindingCaptureState_.Update(held);if(!choice)return;
        if(!bindingAppend_)ClearControlBindings(preferences_,bindingCapture_);
        AddControlBinding(preferences_,bindingCapture_,choice);
        SetStatus(std::string(ControlActionName(static_cast<ControlAction>(bindingCapture_)))+" SET TO "+ControlBindingLabel(preferences_,static_cast<ControlAction>(bindingCapture_)));
        bindingCapture_=-1;hintSkin_=-1;QueueSettingsSave();
    }
    void SetControlHeld(uint32_t held,uint32_t pressed){
        controlHeld_=held;
        // Inspection is held input, not a modal input-context transition.
        const bool overlay=OverlayOpen(false);
        if(overlay!=controlOverlayLast_){controlChordInput_.Reset();controlTransitionBlocked_=true;controlOverlayLast_=overlay;}
        if(controlTransitionBlocked_){controlChordInput_.Reset();if(!held&&!pressed)controlTransitionBlocked_=false;return;}
        if(CapturingBinding())controlChordInput_.Reset();else controlChordInput_.Update(preferences_,held,pressed);
    }
    bool DetailsOpen() const { return details_; }
    bool SourceManagerOpen() const { return sourceManager_ && !sourceEditor_ && !directoryBrowser_ && !mediaHelp_; }
    bool MediaHelpOpen() const { return mediaHelp_; }
    bool SourceEditorOpen() const { return sourceEditor_ && !directoryBrowser_; }
    bool DirectoryBrowserOpen() const { return directoryBrowser_; }
    bool ConsumeScanRequest() { const bool result = scanRequested_; scanRequested_ = false; return result; }
    void CloseOverlay() {
        if(draftExit_!=DraftExit::None){draftExit_=DraftExit::None;return;}
        settingsAction_=-1;
        if(alphabetOpen_){alphabetOpen_=false;return;}
        if(searchOpen_){searchOpen_=false;return;}
        if(sortOpen_){sortOpen_=false;return;}
        if(CoverInspectOpen()) { CloseCoverInspect(); return; }
        if(typeText_) { TypeTextInput(0,0,false,true,false,false); return; }
        if(settingsEditing_) { settingsEditing_=false; return; }
        if(vcMapMode_) { vcMapMode_=0;launcherRow_=7;return; }
        if(launcherBrowse_) { launcherBrowse_=false; return; }
        if(launcherEditor_) { launcherEditor_=false; launcherRow_=0; settingsEditing_=false; return; }
        if(settings_&&settingsCategory_==5) { if(settingsEditing_)settingsEditing_=false;else settings_=false; return; }
        if(typeEditor_) { if(!ConfirmDraftExit(DraftExit::Editor))typeEditor_=false; return; }
        if(typeManager_) { typeManager_=false; return; }
        if(settingsEditing_) { settingsEditing_=false; return; }
        if(audioSettings_) { audioSettings_=false; settingsRow_=5; return; }
        if (mediaHelp_) { mediaHelp_=false; return; }
        if (directoryBrowser_) { BrowserBack(); return; }
        if (sourceEditor_) { if(!ConfirmDraftExit(DraftExit::Editor))sourceEditor_=false; return; }
        if (sourceManager_) { sourceManager_ = false; settings_=false; return; }
        if (settings_ && settingsCategory_==2) { sourceManager_=true; return; }
        details_ = settings_ = false;
    }

    void SettingsChangeCategory(int amount) {
        if(amount && ConfirmDraftExit(DraftExit::Category)){draftExitCategory_=amount;return;}
        settingsAction_=-1;
        typeManager_=typeEditor_=typeText_=false;
        launcherEditor_=launcherBrowse_=false;launcherRow_=vcMapMode_=0;
        launcherStatusCache_.clear();
        settingsEditing_=false;
        settingsCategory_ = (settingsCategory_ + amount + 6) % 6;
        audioSettings_=false;
        settingsRow_ = 0;
        sourceManager_=settingsCategory_==1 || settingsCategory_==2;
        mediaMode_=settingsCategory_==2;
        sourceRow_=0; sourceEditor_=directoryBrowser_=mediaHelp_=false;
    }
    void SettingsMoveRow(int amount) {
        const int rows = SettingsRowCount();
        if (rows == 0) return;
        settingsRow_ = (settingsRow_ + amount + rows) % rows;
    }
    void SettingsAdjust(int amount) {
        if (amount == 0 || settingsCategory_ == 4) return;
        detailsBackdropReady_ = false;
        if(audioSettings_) { AdjustMusic(amount); QueueSettingsSave(); return; }
        if (settingsCategory_ == 0) {
            if (settingsRow_ == 0) {ChangeSkin(amount);}
            else if (settingsRow_ == 1) {
                const auto &tabs=LibraryTabs();
                if(!tabs.empty()) {
                    const int count=static_cast<int>(tabs.size());
                    preferences_.startTab=(StartupLibraryIndex(tabs,preferences_)+amount%count+count)%count;
                    preferences_.startLibrary=tabs[preferences_.startTab];
                }
            }
            else if (settingsRow_==2) preferences_.wrapNavigation = !preferences_.wrapNavigation;
            else if(settingsRow_==3) { preferences_.showInstalled=!preferences_.showInstalled; RebuildVisible(); }
            else if(settingsRow_==4) {preferences_.autoSortInstalledVC=!preferences_.autoSortInstalledVC;RebuildVisible();}
            else if(settingsRow_==7) {preferences_.titleInstallerEnabled=!preferences_.titleInstallerEnabled;RebuildVisible();}
            else if(settingsRow_==5) { audioSettings_=true; settingsRow_=0; }
            else if(settingsRow_==8) {preferences_.demoMode=!preferences_.demoMode;DemoInput(true);}
            else if(settingsRow_==9) {preferences_.demoIdleSeconds=std::clamp(preferences_.demoIdleSeconds+amount*5,DemoMode::MinSeconds,DemoMode::MaxSeconds);DemoInput(true);}
            else if(settingsRow_==6) {
                const bool enabled=!preferences_.startOnBoot;
                if(SetStartOnBoot(enabled)) {
                    preferences_.startOnBoot=enabled;
                    SetStatus(enabled?"STARTUP ON - HOLD + AT BOOT TO BYPASS":"STARTUP OFF");
                } else SetStatus("COULD NOT SAVE STARTUP SETTING",6000);
            }
        } else if (settingsCategory_ == 1) {
            if (settingsRow_ == 0) {
                preferences_.showInstalled = !preferences_.showInstalled;
                RebuildVisible();
            }
        } else if (settingsCategory_ == 3) {
            if(settingsRow_==17) preferences_.backgroundTransparency=std::clamp(preferences_.backgroundTransparency+amount*5,0,100);
            else if (settingsRow_ == 16) {
                ApplySort((preferences_.sortMode+amount+LibrarySort::Count)%LibrarySort::Count);
            }
            else if(settingsRow_==0) {preferences_.coverType=(preferences_.coverType+amount%CoverPresentation::TypeCount+CoverPresentation::TypeCount)%CoverPresentation::TypeCount;ClearSideCases();}
            else if(settingsRow_==1) {auto &layout=PresentationLayout();const int count=CoverPresentation::LayoutCount(preferences_.coverType);layout=(layout+amount%count+count)%count;}
            else if (settingsRow_ == 2) preferences_.animationSpeed = (preferences_.animationSpeed + amount + 3) % 3;
            else if (settingsRow_ == 3) preferences_.coverSpacing = std::clamp(preferences_.coverSpacing+amount*CoverSpacing::Step,CoverSpacing::Min,CoverSpacing::Max);
            else if (settingsRow_ == 4) preferences_.coverZoom=std::clamp(preferences_.coverZoom+amount*CoverZoom::Step,CoverZoom::Min,CoverZoom::Max);
            else if(settingsRow_==5) preferences_.inspectionZoom=std::clamp(preferences_.inspectionZoom+amount,2,8);
            else if (settingsRow_ == 6) preferences_.backgroundMotion = !preferences_.backgroundMotion;
            else if (settingsRow_ == 7) preferences_.reflections = !preferences_.reflections;
            else if(settingsRow_==8) preferences_.theme = (preferences_.theme + amount%AccentThemes::Count + AccentThemes::Count) % AccentThemes::Count;
            else if(settingsRow_==13){preferences_.previewDelaySeconds=std::clamp(preferences_.previewDelaySeconds+amount,0,30);videoTrigger_.Reset();}
            else {
                if(settingsRow_==9 && preferences_.coverType!=CoverPresentation::Box)return;
                bool *values[]={&preferences_.useBoxArt,&preferences_.useBackgrounds,&preferences_.useLogos,&preferences_.usePreviews,&preferences_.preloadBuiltInCovers,&preferences_.startupVideo};
                const int index=settingsRow_-9-(settingsRow_>13?1:0);
                *values[index]=!*values[index];
            }
        }
        QueueSettingsSave();
    }
    void AdjustMusic(int amount) {
            if(settingsRow_==0) { preferences_.musicEnabled=!preferences_.musicEnabled; ApplyMusic(); }
            else if(settingsRow_==1) {
                if(musicTracks_.empty()) { SetStatus("ADD MP3 / OGG / WAV TO UFLOW/MUSIC, THEN RELOAD",6000); return; }
                const auto found=std::find(musicTracks_.begin(),musicTracks_.end(),preferences_.musicTrack);
                int index=found==musicTracks_.end()?(amount>0?-1:0):static_cast<int>(found-musicTracks_.begin());
                index=(index+amount+static_cast<int>(musicTracks_.size()))%static_cast<int>(musicTracks_.size());
                preferences_.musicTrack=musicTracks_[index]; ApplyMusic();
            } else if(settingsRow_==2) {
                preferences_.musicVolume=std::clamp(preferences_.musicVolume+amount*5,0,100);
                music_.Volume(preferences_.musicVolume);
            } else if(settingsRow_==4)preferences_.menuSoundsEnabled=!preferences_.menuSoundsEnabled;
            else if(settingsRow_==5)preferences_.menuSoundsVolume=std::clamp(preferences_.menuSoundsVolume+amount*5,0,100);
            else { musicTracks_=ListMusicTracks(); SetStatus("MUSIC LIST RELOADED"); }
            if(settingsRow_==4 || settingsRow_==5)MenuSound(MenuSounds::Confirm);
    }

    void ApplySort(int mode) {
        const size_t current=visible_.empty()?games_.size():visible_[selected_];
        preferences_.sortMode=std::clamp(mode,0,LibrarySort::Count-1);
        RebuildVisible();
        const auto found=std::find(visible_.begin(),visible_.end(),current);
        selected_=found==visible_.end()?0:static_cast<size_t>(found-visible_.begin());
        visualSelected_=static_cast<float>(selected_);
        PrefetchArtwork(); QueueSettingsSave();
    }
    bool SortOpen() const { return sortOpen_; }
    void FindOrClearSearch() {
        if(sortOpen_){sortOpen_=false;return;}if(OverlayOpen())return;
        if(alphabetFilter_){alphabetFilter_=0;searchBuffer_=searchQuery_;ApplySearch();return;}
        if(!searchQuery_.empty()){searchBuffer_.clear();ApplySearch();return;}
        OpenSort();
    }
    void OpenSort() { if(sortOpen_){sortOpen_=false;return;}if(OverlayOpen())return;DismissVideo();sortChoice_=preferences_.sortMode;sortOpen_=true; }
    void SortInput(int direction,bool accept,bool cancel,int x=-1,int y=-1,bool search=false,bool alphabet=false) {
        if(cancel){sortOpen_=false;return;}
        if(alphabet || (x>=640 && x<870 && y>=540 && y<590)){sortOpen_=false;OpenAlphabet(false);return;}
        if(search || (x>=410 && x<640 && y>=540 && y<590)){OpenSearch();return;}
        if(cancel){sortOpen_=false;return;}
        sortChoice_=(sortChoice_+direction+LibrarySort::Count)%LibrarySort::Count;
        if(x>=410 && x<870 && y>=190 && y<190+LibrarySort::Count*48){
            sortChoice_=(y-190)/48;accept=true;
        }
        if(accept){ApplySort(sortChoice_);sortOpen_=false;}
    }
    static int AlphabetBucket(const std::string &name) {
        const auto first=name.find_first_not_of(" \t\r\n");
        if(first==std::string::npos)return 1;
        unsigned char c=static_cast<unsigned char>(name[first]);
        if(c>='a'&&c<='z')c-=32;
        return c>='A'&&c<='Z'?2+c-'A':1;
    }
    static std::string AlphabetLabel(int index) {
        return index==0?"ALL":index==1?"#":std::string(1,static_cast<char>('A'+index-2));
    }
    int AlphabetTouchIndex(int x) const {
        const int first=preferences_.alphabetFilterMode?0:1,start=108+(first?19:0);
        return x>=start && x<start+(28-first)*38?first+(x-start)/38:-1;
    }
    std::uint32_t AlphabetConsumedBinding() const {
        if(!alphabetHeldMode_)return 0;const int action=int(ControlAction::Alphabet);
        uint32_t mask=preferences_.bindings[action];for(auto combo:preferences_.combos[action])if((controlHeld_&combo)==combo)mask|=combo;return mask;
    }
    bool AlphabetOpen() const { return alphabetOpen_; }
    bool CanOpenAlphabet() const { return !OverlayOpen() && !libraryFadePhase_; }
    void OpenAlphabet(bool held=true) {
        DismissVideo();alphabetOpen_=true;alphabetHeldMode_=held;alphabetMoved_=false;
        alphabetDirection_=0;alphabetNext_=0;
        alphabetChoice_=std::clamp(alphabetChoice_,preferences_.alphabetFilterMode?0:1,27);
        alphabetVisual_=static_cast<float>(alphabetChoice_);RefreshAlphabetCounts();
    }
    void RefreshAlphabetCounts() {
        alphabetCounts_.fill(0);
        for(size_t i=0;i<games_.size();++i){
            const auto &game=games_[i];
            if(!MatchesLibrarySearch(game))continue;
            if(!preferences_.alphabetFilterMode && alphabetFilter_ && AlphabetBucket(game.name)!=alphabetFilter_)continue;
            ++alphabetCounts_[0];++alphabetCounts_[AlphabetBucket(game.name)];
        }
    }
    bool MatchesLibrarySearch(const GameEntry &game,const std::string &library) const {
        if(!GameAvailableInNavigation(game,preferences_,libraries_))return false;
        if(!searchFolded_.empty() && SearchFold(game.name).find(searchFolded_)==std::string::npos)return false;
        return GameMatchesLibrary(game,library);
    }
    bool MatchesLibrarySearch(const GameEntry &game) const {return MatchesLibrarySearch(game,CurrentLibraryId());}
    void AlphabetInput(float stick,int digital,Uint32 now,bool held,bool toggle,bool accept,bool cancel,int touch=-1) {
        if(cancel){alphabetOpen_=false;return;}
        if(toggle){
            preferences_.alphabetFilterMode=!preferences_.alphabetFilterMode;QueueSettingsSave();
            alphabetChoice_=std::max(alphabetChoice_,preferences_.alphabetFilterMode?0:1);
            RefreshAlphabetCounts();SetStatus(preferences_.alphabetFilterMode?"A-Z FILTER MODE":"A-Z BROWSE MODE",5000);
        }
        const int direction=alphabetHeldMode_&&!held?0:SearchAxis(stick,digital,alphabetDirection_,alphabetNext_,now,250,90);
        const int first=preferences_.alphabetFilterMode?0:1,count=28-first;
        if(direction){
            const int previous=alphabetChoice_;
            alphabetChoice_=first+(alphabetChoice_-first+direction+count)%count;
            if(std::abs(alphabetChoice_-previous)>1)alphabetVisual_=static_cast<float>(alphabetChoice_);
            alphabetMoved_=true;
        }
        if(touch>=first && touch<28){alphabetChoice_=touch;alphabetMoved_=true;}
        if((alphabetHeldMode_&&!held) || (!alphabetHeldMode_&&accept)) {
            alphabetOpen_=false;
            if(!alphabetMoved_)return;
            if(preferences_.alphabetFilterMode){
                alphabetFilter_=alphabetChoice_;searchBuffer_=searchQuery_;ApplySearch();
            }else{
                const auto found=std::find_if(visible_.begin(),visible_.end(),[&](size_t i){return AlphabetBucket(games_[i].name)==alphabetChoice_;});
                if(found!=visible_.end()){
                    selected_=static_cast<size_t>(found-visible_.begin());visualSelected_=static_cast<float>(selected_);
                    detailsBackdropReady_=false;DismissVideo();PrefetchArtwork();
                }else SetStatus("NO TITLES UNDER "+AlphabetLabel(alphabetChoice_),2500);
            }
        }
    }
    bool SearchOpen() const { return searchOpen_; }
    void OpenSearch() { DismissVideo(); sortOpen_=false; searchOpen_=true; searchBuffer_=searchQuery_; searchKey_=0; searchHorizontal_=searchVertical_=0; searchNextH_=searchNextV_=0; }
    static std::string SearchFold(std::string value) {
        for(auto &c:value) if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
        return value;
    }
    void ApplySearch() {
        const auto current=Selected();
        const auto index=current?static_cast<size_t>(current-games_.data()):games_.size();
        searchQuery_=searchBuffer_;
        const auto first=searchQuery_.find_first_not_of(" ");
        searchQuery_=first==std::string::npos?"":searchQuery_.substr(first,searchQuery_.find_last_not_of(" ")-first+1);
        searchFolded_=SearchFold(searchQuery_);
        hintSkin_=-1;
        searchOpen_=false; DismissVideo(); RebuildVisible();
        const auto found=std::find(visible_.begin(),visible_.end(),index);
        selected_=found==visible_.end()?0:static_cast<size_t>(found-visible_.begin());
        visualSelected_=static_cast<float>(selected_); PrefetchArtwork();
    }
    static int SearchAxis(float stick,int digital,int &held,Uint32 &next,Uint32 now,Uint32 delay=350,Uint32 repeat=120) {
        const float threshold=held?.28f:.48f;
        const int direction=digital?digital:stick>threshold?1:stick<-threshold?-1:0;
        if(direction!=held){held=direction;next=now+delay;return direction;}
        if(direction && SDL_TICKS_PASSED(now,next)){next=now+repeat;return direction;}
        return 0;
    }
    void SearchNavigate(float stickX,float stickY,int digitalH,int digitalV,Uint32 now) {
        const int horizontal=SearchAxis(stickX,digitalH,searchHorizontal_,searchNextH_,now);
        const int vertical=SearchAxis(-stickY,digitalV,searchVertical_,searchNextV_,now);
        SearchInput(horizontal,vertical,false,false,false,false);
    }
    void SearchInput(int horizontal,int vertical,bool accept,bool cancel,bool erase,bool done,bool clear=false,int x=-1,int y=-1) {
        if(cancel){searchOpen_=false;return;}
        constexpr const char *keys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 '-.";
        const int count=static_cast<int>(std::strlen(keys));
        searchKey_=(searchKey_+horizontal+vertical*10+count*10)%count;
        if(x>=340 && x<940 && y>=300 && y<460){
            const int key=(y-300)/40*10+(x-340)/60;
            if(key<count){searchKey_=key;accept=true;}
        }
        if(x>=340 && x<940 && y>=490 && y<534){
            const int action=(x-340)/150;
            erase=action==0;clear=action==1;done=action==2;
            if(action==3){searchOpen_=false;return;}
        }
        if(clear)searchBuffer_.clear();
        if(erase&&!searchBuffer_.empty())searchBuffer_.pop_back();
        if(accept&&searchBuffer_.size()<64)searchBuffer_+=keys[searchKey_];
        if(done)ApplySearch();
    }
    void RecordPlayed(const GameEntry &game) {
        if(!preview_&&!userStats_.Launch(game))Trace("user-stats-save-failed");
        if(!preview_ && !history_.Record(game,kHistoryPath))
            Trace("play-history-save-failed");
    }

    void SettingsActivate() {
        SettingsAdjust(1);
    }
    void ApplyMusic() {
#ifndef UFLOW_HOST_PREVIEW
        if(!menuSounds_.Available()) {
            menuSounds_.Load("fs:/vol/external01/wiiu/apps/uFlow/sounds");
            if(!menuSounds_.Available())menuSounds_.Load("fs:/vol/content/sounds");
        }
#endif
        if(startupIntroPending_)return;
        if(!preferences_.musicEnabled) { music_.Stop(); musicStatus_="MUSIC OFF"; return; }
        if(musicTracks_.empty()) musicTracks_=ListMusicTracks();
        if(preferences_.musicTrack.empty() && !musicTracks_.empty()) preferences_.musicTrack=musicTracks_.front();
        if(preferences_.musicTrack.empty()) {
            music_.Stop(); preferences_.musicEnabled=false;
            musicStatus_="NO TRACKS FOUND - ADD MUSIC FILES, THEN RELOAD";
            SetStatus(musicStatus_,6000); return;
        }
        const auto error=music_.Play(std::string(kMusicDirectory)+"/"+preferences_.musicTrack,preferences_.musicVolume);
        if(!error.empty()) { preferences_.musicEnabled=false; musicStatus_=error; SetStatus(error,6000); }
        else musicStatus_="PLAYING: "+preferences_.musicTrack;
    }
    void PauseMusic(bool pause) { music_.Pause(pause); }
    void StopMusic() { StopVideo(false); glassBackground_.Stop(); music_.Stop(); menuSounds_.Stop(); }
    void MenuSound(MenuSounds::Cue cue) {
        if(!preview_)menuSounds_.Play(cue,preferences_.menuSoundsEnabled,preferences_.menuSoundsVolume);
    }
    void PrepareSystemBackground() {
        CancelHomeSwipe();
        DemoInput(true);
        alphabetOpen_=false;alphabetProgress_=0;alphabetMoved_=false;
        startupIntroPending_=false; // returning from HOME must not replay the reveal
        if(injectionDialog_.state==InjectionDialog::State::Working)injectionDialog_.cancel=true;
        else injectionDialog_.Reset();
        // SDL Wii U queues WILLENTERBACKGROUND before ProcUIDrawDoneRelease.
        // Close mixer references while AX and the audio thread still run; doing
        // this on the later QUIT event can wait on a suspended audio callback.
        restoreMusicAfterBackground_=preferences_.musicEnabled || menuSounds_.Available();
        Trace("system-background-video-stop-begin");
        StopVideo(false);
        glassBackground_.Stop();
        Trace("system-background-video-stop-complete");
        music_.Stop();
        menuSounds_.Stop();
        Trace("system-background-music-stop-complete");
        // Cancellation must not leave the decoder paused: it needs to unwind.
        videoTrigger_.Reset();
    }
    void ResumeSystemForeground() {
        DemoInput(true);
        SuspendVideo(false);
        if (restoreMusicAfterBackground_) {
            restoreMusicAfterBackground_=false;
            ApplyMusic();
        }
    }
    void StopVideo(bool resumeMusic = true) {
        const bool detailsPlaying=detailsVideo_.Active();detailsVideoPending_=false;detailsVideo_.Stop();
        if(detailsPlaying&&resumeMusic)music_.Pause(false);
        videoPending_=false;
        const bool wasActive = video_.Active();
        if (wasActive) ReportVideo(true);
        video_.Stop(resumeMusic && videoOptions_.mode==VideoMode::Showcase &&
            showcaseProgress_>0.f && !OverlayOpen());
        if (wasActive && resumeMusic) music_.Pause(false);
    }
    void DismissVideo() { videoTrigger_.Dismiss(); StopVideo(); }
    void ReportVideo(bool force = false) {
        if (preview_ || !video_.Active()) return;
        const Uint32 now=SDL_GetTicks(), elapsed=now-videoReportAt_;
        if (elapsed < (force ? 250u : 5000u)) return;
        const auto stats=video_.Statistics();
        if (!force && videoReports_>=6 && stats.loops==videoReportStats_.loops && elapsed<30000) return;
        char message[640];
        std::snprintf(message,sizeof(message),
            "video-perf dt=%u hw=%d decoded=%u shown=%u converted=%u late=%u read_ms=%.1f decode_ms=%.1f convert_ms=%.1f upload_ms=%.1f audio_frames=%u output=%dx%d max_gap_ms=%u gaps_over50=%u audio_missing=%u lag_ms=%d handoff_ms=%.1f cores=%u/%u reordered=%u pts_fixed=%u loops=%u native_ms=%.1f skip_nonref=%u read_ahead_peak=%u",
            elapsed,stats.hardware,stats.decoded-videoReportStats_.decoded,stats.presented-videoReportStats_.presented,
            stats.converted-videoReportStats_.converted,stats.lateDropped-videoReportStats_.lateDropped,
            (stats.readUs-videoReportStats_.readUs)/1000.,(stats.decodeUs-videoReportStats_.decodeUs)/1000.,
            (stats.convertUs-videoReportStats_.convertUs)/1000.,(stats.uploadUs-videoReportStats_.uploadUs)/1000.,
            stats.audioFrames-videoReportStats_.audioFrames,stats.outputWidth,stats.outputHeight,
            stats.maxPresentGapMs,stats.longPresentGaps-videoReportStats_.longPresentGaps,
            stats.audioMissingFrames-videoReportStats_.audioMissingFrames,stats.videoLagMs,
            (stats.handoffUs-videoReportStats_.handoffUs)/1000.,stats.decoderCores,stats.converterCores,
            stats.reorderedFrames-videoReportStats_.reorderedFrames,stats.timestampCorrections-videoReportStats_.timestampCorrections,stats.loops,
            (stats.nativeDecodeUs-videoReportStats_.nativeDecodeUs)/1000.,stats.skippedNonreference-videoReportStats_.skippedNonreference,stats.packetQueuePeak);
        Trace(message); videoReportStats_=stats; videoReportAt_=now; ++videoReports_;
    }
    bool VideoBehindInspection() const { return videoOptions_.mode==VideoMode::Background; }
    void VideoInput(bool active) {
        videoInputActive_=active;
        if(active) { videoTrigger_.Reset(); if(video_.Active()) StopVideo(); }
    }
    void RestoreDemoSelection() {
        StopVideo();videoSelection_=nullptr;videoTrigger_.Reset();
        if(!demoOriginPath_.empty()) {
            for(std::size_t i=0;i<visible_.size();++i) {
                if(games_[visible_[i]].absolutePath!=demoOriginPath_)continue;
                selected_=i;visualSelected_=static_cast<float>(i);bookshelfMotion_={};
                detailsBackdropReady_=false;PrefetchArtwork();break;
            }
        }
        demoOriginPath_.clear();
    }
    bool DemoInput(bool active) {
        if(!active)return false;
        const bool wasActive=demo_.Active();
        if(wasActive)RestoreDemoSelection();
        demo_.Reset(SDL_GetTicks());demoNext_=false;demoFailed_.clear();
        return wasActive;
    }
    void SuspendVideo(bool suspended) {
        video_.Suspend(suspended);detailsVideo_.Suspend(suspended);
        if (!suspended) { StopVideo(); videoTrigger_.Reset(); }
    }

    bool SettingsSelectorRow(int row) const {
        if(audioSettings_) return row>=0 && row<6 && row!=3;
        if(settingsCategory_==0) return (row>=0 && row<5) || (row>=6 && row<SettingsRowCount());
        return settingsCategory_==3 && row>=0 && row<18 && (row!=9 || preferences_.coverType==CoverPresentation::Box);
    }
    bool SettingsSelectorSelected() const {
        if(settingsSidebar_ || directoryBrowser_ || mediaHelp_) return false;
        if(sourceEditor_) return SourceEditorSelector(sourceEditorRow_);
        return !sourceManager_ && SettingsSelectorRow(settingsRow_);
    }
    enum class SettingsAction { None, Select, Save, Delete, Back, Add, Scan, Guide, Open, Use, Parent, Cancel, Space, Done, Erase, BindReplace, BindAdd, BindClear };
    std::array<SettingsAction,4> SettingsActions() const {
        using A=SettingsAction;
        if(settingsCategory_==4 && !sourceManager_ && settingsRow_<int(ControlAction::Count))return {A::BindReplace,A::BindAdd,A::BindClear,A::Back};
        if(typeText_)return {A::Space,A::Done,A::Erase,A::Cancel};
        if(directoryBrowser_)return {A::Open,A::Use,A::Parent,A::Cancel};
        if(mediaHelp_)return {A::None,A::None,A::None,A::Back};
        if(launcherBrowse_ || vcMapMode_)return {launcherBrowse_&&launcherFiles_.empty()?A::None:A::Select,A::None,A::None,A::Back};
        if(typeEditor_ || launcherEditor_ || sourceEditor_) {
            const bool saved=typeEditor_?typeIndex_>=0:launcherEditor_?launcherIndex_>=0:editingSourceIndex_>=0;
            return {A::Select,A::Save,saved?A::Delete:A::None,A::Back};
        }
        if(typeManager_ || settingsCategory_==5)return {A::Add,A::None,A::None,A::Back};
        if(sourceManager_)return {A::Add,A::Scan,mediaMode_?A::Guide:A::None,A::Back};
        return {A::Select,A::Save,A::None,A::Back};
    }
    int SettingsContentCount() const {
        if(typeText_)return static_cast<int>(std::strlen(TypeKeys));
        if(typeManager_)return TypeRowCount()-(typeEditor_?2:0);
        if(settingsCategory_==5)return LauncherRows()-(launcherEditor_&&!launcherBrowse_&&!vcMapMode_?2:0);
        if(directoryBrowser_)return static_cast<int>(browserDirectories_.size());
        if(mediaHelp_)return 0;
        if(sourceEditor_)return SourceEditorCount()-2;
        if(sourceManager_)return SourceListCount();
        return SettingsRowCount();
    }
    int &SettingsContentRow() {
        if(typeText_)return typeKey_;
        if(typeManager_)return typeRow_;
        if(settingsCategory_==5)return launcherRow_;
        if(directoryBrowser_)return browserRow_;
        if(sourceEditor_)return sourceEditorRow_;
        if(sourceManager_)return sourceRow_;
        return settingsRow_;
    }
    void FocusSettingsActions() {
        settingsEditing_=false;settingsSidebar_=false;settingsAction_=0;
        const auto actions=SettingsActions();
        while(settingsAction_<3 && actions[settingsAction_]==SettingsAction::None)++settingsAction_;
    }
    void ActivateSettingsAction(int index) {
        const auto action=SettingsActions()[index];
        if(action==SettingsAction::None)return;
        settingsAction_=-1;
        using A=SettingsAction;
        if(action==A::BindReplace||action==A::BindAdd){BeginBindingCapture(settingsRow_,action==A::BindAdd);return;}
        if(action==A::BindClear){ClearControlBindings(preferences_,settingsRow_);hintSkin_=-1;QueueSettingsSave();return;}
        if(action==A::Select){SettingsInput(0,0,true);return;}
        settingsEditing_=false;
        if(action==A::Back){SettingsInput(0,0,false,true);return;}
        if(action==A::Open){BrowserOpenSelected();return;}
        if(action==A::Use){BrowserSelectCurrent();return;}
        if(action==A::Parent){BrowserBack();return;}
        if(action==A::Cancel){if(typeText_)TypeTextInput(0,0,false,true,false,false);else BrowserCancel();return;}
        if(action==A::Space){if(typeBuffer_.size()<(launcherEditor_?240u:80u))typeBuffer_+=' ';FocusSettingsActions();return;}
        if(action==A::Done || action==A::Erase){TypeTextInput(0,0,false,false,action==A::Erase,action==A::Done);if(typeText_){FocusSettingsActions();settingsAction_=2;}return;}
        if(action==A::Add){if(typeManager_){typeRow_=0;TypeActivate();}else if(settingsCategory_==5){launcherRow_=0;LauncherActivate();}else SourceAdd();return;}
        if(action==A::Scan){SourceScan();return;}
        if(action==A::Guide){mediaHelp_=true;return;}
        const bool save=action==A::Save;
        if(typeEditor_) {
            const int row=typeRow_;typeRow_=save?10:11;TypeActivate();
            if(typeEditor_)typeRow_=row;
        } else if(launcherEditor_) {
            const int row=launcherRow_;launcherRow_=LauncherPhysicalRow(save?8:9);LauncherActivate();
            if(launcherEditor_)launcherRow_=row;
        } else if(sourceEditor_) {if(save)SaveDraftSource();else SourceRemove();}
        else if(save){QueueSettingsSave();FlushSettingsSave(true);}
    }
    // A enters value editing; only then do left/right adjust instead of moving focus.
    // B ends editing in place; up/down ends editing and moves to the adjacent row.
    void SettingsInput(int horizontal,int vertical,bool accept=false,bool back=false,
                       bool previous=false,bool useFolder=false,bool plus=false) {
        if (!settings_) return;
        if(draftExit_!=DraftExit::None){
            if(back)ResolveDraftExit(false);
            else {if(horizontal || vertical)draftExitYes_=!draftExitYes_;if(accept)ResolveDraftExit(draftExitYes_);}
            return;
        }
        if(back)MenuSound(MenuSounds::Back);
        else if(accept)MenuSound(MenuSounds::Confirm);
        else if(horizontal || vertical)MenuSound(MenuSounds::Move);
        if(settingsCategory_==4 && !settingsSidebar_ && settingsAction_<0 && settingsRow_<int(ControlAction::Count)) {
            if(previous){ClearControlBindings(preferences_,settingsRow_);hintSkin_=-1;QueueSettingsSave();SetStatus("ALL MAPPINGS CLEARED");return;}
            if(plus){BeginBindingCapture(settingsRow_,true);return;}
        }
        const auto actions=SettingsActions();
        for(int i=0;i<4;++i) {
            const auto action=actions[i];
            if((plus && (action==SettingsAction::Save || action==SettingsAction::Add || action==SettingsAction::Done)) ||
               (previous && (action==SettingsAction::Delete || action==SettingsAction::Erase || (directoryBrowser_ && action==SettingsAction::Cancel))) ||
               (useFolder && (action==SettingsAction::Scan || action==SettingsAction::Use))) {
                ActivateSettingsAction(i);return;
            }
        }
        if(settingsAction_>=0) {
            if(back){settingsAction_=-1;SettingsInput(0,0,false,true);return;}
            if(vertical<0){settingsAction_=-1;return;}
            if(horizontal) {
                int next=settingsAction_+horizontal;
                while(next>=0 && next<4 && actions[next]==SettingsAction::None)next+=horizontal;
                if(next<0){settingsAction_=-1;settingsSidebar_=true;}
                else if(next<4)settingsAction_=next;
            }
            if(accept && settingsAction_>=0)ActivateSettingsAction(settingsAction_);
            return;
        }
        if(vertical>0 && settingsSidebar_ && settingsCategory_==5){FocusSettingsActions();return;}
        if(vertical && !settingsSidebar_ && settingsAction_<0) {
            auto &row=SettingsContentRow();const int count=SettingsContentCount();
            if(vertical>0 && (count==0 || row>=(typeText_?count-12:count-1))) {FocusSettingsActions();return;}
            if(!typeText_ && vertical<0 && row<=0){settingsEditing_=false;return;}
        }
        if(typeText_) { TypeTextInput(horizontal,vertical,accept,back,previous,plus); return; }
        if(settingsCategory_==5&&!settingsSidebar_&&settingsAction_<0) { LauncherInput(horizontal,vertical,accept,back);return; }
        if (back) { CloseOverlay(); return; }
        if(typeManager_&&!settingsSidebar_&&settingsAction_<0) {
            if(vertical) { settingsEditing_=false;typeRow_=(typeRow_+vertical+TypeRowCount())%TypeRowCount(); return; }
            if(horizontal&&typeEditor_&&settingsEditing_) { TypeAdjust(horizontal); return; }
            if(horizontal<0) { settingsSidebar_=true; return; }
            if(accept) TypeActivate();
            return;
        }
        if(settingsEditing_) {
            if(vertical || plus) settingsEditing_=false;
            else {
                if(horizontal || previous) {
                    const int amount=horizontal?horizontal:-1;
                    if(sourceEditor_) SourceEditorAdjust(amount); else SettingsAdjust(amount);
                } else if(accept) settingsEditing_=false;
                return;
            }
        }
        if (horizontal) { settingsSidebar_=horizontal<0; return; }
        if (settingsSidebar_) {
            if (vertical) SettingsChangeCategory(vertical);
            if (accept) settingsSidebar_=false;
            return;
        }
        if (accept && settingsCategory_==4 && !sourceManager_) {
            if (settingsRow_ == static_cast<int>(ControlAction::Count)) {
                ResetControlBindings(preferences_);hintSkin_=-1; QueueSettingsSave(); SetStatus("CONTROLS RESET");
            } else { BeginBindingCapture(settingsRow_,false); }
            return;
        }
        if(accept && SettingsSelectorSelected()) { settingsEditing_=true; return; }
        if (directoryBrowser_) {
            if(vertical) BrowserMove(vertical);
            else if(accept) BrowserOpenSelected();
            else if(useFolder) BrowserSelectCurrent();
            else if(previous) BrowserCancel();
        } else if(sourceEditor_) {
            if(vertical) SourceEditorMove(vertical);
            else if(accept) SourceEditorActivate();
            else if(plus) SaveDraftSource();
        } else if(mediaHelp_) {
            if(accept) CloseOverlay();
        } else if(sourceManager_) {
            if(vertical) SourceMove(vertical);
            else if(accept) SourceActivate();
            else if(plus) SourceAdd();
            else if(useFolder) SourceScan();
        } else {
            if(vertical) SettingsMoveRow(vertical);
            else if(accept) SettingsActivate();
            else if(plus) CloseOverlay();
        }
    }

    bool SettingsZoomTouch(int x,int y,bool dragging=false) {
        if(!settings_ || settingsProgress_<.95f || settingsCategory_!=3 || sourceManager_ || audioSettings_)return false;
        int row=dragging?settingsRow_:SettingsFirstRow()+(y-187)/62;
        if((row!=3 && row!=4) || row<SettingsFirstRow() || row>=SettingsFirstRow()+6)return false;
        const int top=187+(row-SettingsFirstRow())*62;
        if(!dragging && (x<860 || x>1060 || y<top || y>=top+57))return false;
        settingsSidebar_=false;settingsRow_=row;settingsEditing_=false;settingsAction_=-1;
        const bool zoom=row==4;
        const int low=zoom?CoverZoom::Min:CoverSpacing::Min,high=zoom?CoverZoom::Max:CoverSpacing::Max;
        const int step=zoom?CoverZoom::Step:CoverSpacing::Step;
        const float position=std::clamp((x-870)/180.f,0.f,1.f);
        const int value=low+int(std::lround(position*((high-low)/step)))*step;
        int &setting=zoom?preferences_.coverZoom:preferences_.coverSpacing;
        if(value!=setting){setting=value;ClearSideCases();detailsBackdropReady_=false;QueueSettingsSave();}
        return true;
    }
    void SettingsTouch(int x,int y,int dragY=0) {
        if(!settings_ || settingsProgress_<.95f) return;
        if(draftExit_!=DraftExit::None){
            if(std::abs(dragY)<30 && y>=414 && y<470){
                if(x>=350 && x<630)ResolveDraftExit(false);
                else if(x>=650 && x<930)ResolveDraftExit(true);
            }
            return;
        }
        if(std::abs(dragY)<30 && x>=390 && x<1121 && y>=574 && y<614) {
            const int action=(x-390)/185;
            if((x-390)%185<176){settingsSidebar_=false;ActivateSettingsAction(action);}
            return;
        }
        settingsAction_=-1;
        if(std::abs(dragY)<30 && SettingsZoomTouch(x,y))return;
        const bool wasEditing=settingsEditing_;
        settingsEditing_=false;
        if(x>=108 && x<348 && y>=182 && y<572) {
            settingsSidebar_=true;
            if(std::abs(dragY)>=30) { SettingsChangeCategory(dragY<0?1:-1); return; }
            for(int row=0;row<6;++row) if(y>=192+row*61 && y<238+row*61) {
                if(row!=settingsCategory_) SettingsChangeCategory(row-settingsCategory_);
                return;
            }
            return;
        }
        if(x<372 || x>=1132) return;
        settingsSidebar_=false;
        if(!typeText_ && std::abs(dragY)>=30){SettingsInput(0,dragY<0?1:-1);return;}
        if(typeManager_ || typeText_) {
            if(typeText_) {
            if(y>=320&&y<512) { int col=(x-390)/60, row=(y-320)/32; if(col>=0&&col<12&&row>=0&&row<6&&row*12+col<static_cast<int>(std::strlen(TypeKeys))){typeKey_=row*12+col; TypeTextInput(0,0,true,false,false,false);} }
                return;
            }
            if(y>=260&&y<548) { const int first=std::max(0,typeRow_-5); int row=first+(y-260)/40;
                if(row<TypeRowCount()-(typeEditor_?2:0)){
                    if(typeEditor_&&wasEditing&&row==typeRow_&&(row==1||(row>=3&&row<=9))){
                        TypeAdjust(x<760?-1:1);settingsEditing_=true;
                    }else{typeRow_=row;TypeActivate();}
                } }
            return;
        }
        if(settingsCategory_==5) {
            if(std::abs(dragY)>=30){SettingsInput(0,dragY<0?1:-1);return;}
            if(y>=260&&y<540){const int row=std::max(0,launcherRow_-5)+(y-260)/40;
                if(row<SettingsContentCount()){
                    if(launcherEditor_&&!launcherBrowse_&&!vcMapMode_&&wasEditing&&row==launcherRow_&&(row==1||row==5||(launcherDraft_.kind==LauncherKind::DSInjection&&row>=10&&row<=14))){
                        settingsEditing_=true;LauncherInput(x<760?-1:1,0,false,false);
                    }else{launcherRow_=row;LauncherActivate();}
                }}
            return;
        }
        if(std::abs(dragY)>=30) {
            SettingsInput(0,dragY<0?1:-1); return;
        }

        if(directoryBrowser_) {
            const int total=static_cast<int>(browserDirectories_.size());
            const int first=std::max(0,std::min(browserRow_-5,total-7));
            for(int row=0;row<7 && first+row<total;++row) if(y>=271+row*37 && y<305+row*37) {
                browserRow_=first+row; BrowserOpenSelected(); return;
            }
        } else if(sourceEditor_) {
            const int first = SourceEditorFirst();
            for(int row=first;row<std::min(first+7,SourceEditorCount()-2);++row) if(y>=272+(row-first)*38 && y<309+(row-first)*38) {
                sourceEditorRow_=row;
                if(SourceEditorSelector(row) && x>=860 && x<990) SourceEditorAdjust(-1);
                else if(SourceEditorSelector(row) && x>=990) SourceEditorAdjust(1);
                else SettingsInput(0,0,true);
                return;
            }
        } else if(sourceManager_ && !mediaHelp_) {
            const int first=std::max(0,std::min(sourceRow_-4,SourceListCount()-6));
            for(int row=0;row<6 && first+row<SourceListCount();++row) if(y>=271+row*48 && y<317+row*48) {
                sourceRow_=first+row; SourceActivate(); return;
            }
        } else if(!mediaHelp_) {
            const bool controls=settingsCategory_==4;
            const int pitch=controls?26:audioSettings_?48:(settingsCategory_==3 || settingsCategory_==2 || settingsCategory_==0?62:76);
            const int top=controls?233:187, bottomOffset=controls?26:audioSettings_?46:57;
            for(int row=SettingsFirstRow();row<std::min(SettingsRowCount(),SettingsFirstRow()+ (controls?12:6));++row) if(y>=top+(row-SettingsFirstRow())*pitch && y<top+bottomOffset+(row-SettingsFirstRow())*pitch) {
                settingsRow_=row;
                if(SettingsSelectorSelected() && x>=860) SettingsAdjust(x<990?-1:1);
                else SettingsInput(0,0,true);
                return;
            }
        }
    }

    int SourceCount() const { return static_cast<int>(mediaMode_?mediaSources_.size():sources_.size()); }
    bool CheckLauncherSettings() {
        settings_=true;settingsSidebar_=true;settingsCategory_=4;settingsProgress_=1;
        SettingsInput(0,1);if(settingsCategory_!=5)return false;
        SettingsInput(1,0);SettingsInput(0,0,true);
        if(!launcherEditor_||launcherDraft_.kind!=LauncherKind::Emulator)return false;
        SettingsInput(0,1);SettingsInput(-1,0);
        if(!settingsSidebar_||launcherDraft_.kind!=LauncherKind::Emulator)return false;
        SettingsInput(1,0);SettingsInput(0,0,true);SettingsInput(-1,0);
        if(!settingsEditing_||launcherDraft_.kind!=LauncherKind::USBLoaderGX)return false;
        SettingsInput(0,0,false,true);if(settingsEditing_||!launcherEditor_)return false;
        SettingsInput(0,0,false,true);if(launcherEditor_||!settings_)return false;
        SettingsTouch(200,210);if(settingsCategory_!=0||!settingsSidebar_)return false;
        SettingsTouch(200,515);if(settingsCategory_!=5)return false;
        SettingsTouch(450,266);if(!launcherEditor_)return false;
        SettingsTouch(450,306);if(!settingsEditing_)return false;
        SettingsTouch(950,306);if(launcherDraft_.kind!=LauncherKind::Native)return false;
        SettingsInput(0,0,false,true);SettingsInput(0,0,false,true);
        GameEntry native;native.platform=Platform::WiiU;std::string executable;
        if(!ConfigureLaunch(native,executable).empty()||!executable.empty())return false;
        native.platform=Platform::Files;native.libraryId="unknown";
        if(ConfigureLaunch(native,executable).empty())return false;
        return true;
    }
    std::string LauncherLabel(const std::string &id) const {
        if(id.empty())return "AUTOMATIC / DIRECTORY DEFAULT";
        for(const auto &l:launchers_)if(l.id==id)return l.name+(l.enabled?"":" (DISABLED)");
        return "MISSING LAUNCHER";
    }
    std::string SourceLauncherLabel() const {
        if(!draftSource_.launcherId.empty())return LauncherLabel(draftSource_.launcherId);
        const auto id=draftSource_.libraryId.empty()?DefaultLibraryId(draftSource_.platform):draftSource_.libraryId;
        for(const auto &l:libraries_)if(l.id==id&&!l.launcherId.empty())return LauncherLabel(l.launcherId)+" (TYPE)";
        return "USE DEFAULT";
    }
    std::vector<std::string> GameLauncherChoices(const GameEntry &g) const {
        std::vector<std::string> choices={""};
        for(const auto &l:launchers_)if(l.enabled&&l.kind!=LauncherKind::VirtualConsole&&
            LauncherAccepts(l,g.platform)&&
            (g.platform!=Platform::Files||LauncherMatchesFile(l,g.absolutePath)))choices.push_back(l.id);
        return choices;
    }
    void ChangeGameLauncher(int amount) {
        const auto *g=Selected();if(!g)return;
        const auto choices=GameLauncherChoices(*g);
        const auto key=LauncherGameKey(*g);auto current=gameLaunchers_.find(key);
        const auto id=current==gameLaunchers_.end()?std::string{}:current->second;
        const auto found=std::find(choices.begin(),choices.end(),id);
        const int pos=found==choices.end()?0:static_cast<int>(found-choices.begin());
        auto next=gameLaunchers_;const auto selected=choices[(pos+amount+choices.size())%choices.size()];
        if(selected.empty())next.erase(key);else next[key]=selected;
        if(SaveLauncherOverrides(next)){gameLaunchers_=std::move(next);SetStatus("GAME LAUNCHER SAVED");}
        else SetStatus("COULD NOT SAVE GAME LAUNCHER");
    }
    bool SavedInjection(const GameEntry &g) const {
        const auto key=LauncherGameKey(g);auto it=savedInjectionState_.find(key);
        if(it==savedInjectionState_.end())it=savedInjectionState_.emplace(key,g.platform==Platform::Files&&VCInject::HasSaved(g)).first;
        return it->second;
    }
    std::string InjectionModule(const GameEntry &g) const {
        if(VCSystems::File(g.absolutePath).empty())return {};
        const auto preferred=EffectiveLauncher(g,gameLaunchers_,sources_,libraries_);
        std::string fallback;
        for(const auto &l:launchers_)if(l.enabled&&l.kind==LauncherKind::DSInjection&&LauncherAccepts(l,g.platform)&&LauncherMatchesFile(l,g.absolutePath)) {
            if(l.id==preferred)return l.id;
            if(fallback.empty())fallback=l.id;
        }
        return fallback;
    }
    std::string GameLauncherLabel(const GameEntry &g,const std::string &id) const {
        for(const auto &l:launchers_)if(l.id==id&&l.kind==LauncherKind::DSInjection)
            return SavedInjection(g)?"Saved injected ROM":"AUTO-INJECT ON PLAY";
        return id.empty()?"USE DEFAULT":LauncherLabel(id);
    }
    bool ConsumeInjectionFinished(){const bool done=injectionFinished_;injectionFinished_=false;return done;}
    std::string CachedLauncherStatus(const LauncherDefinition &l) {
        if(!l.enabled)return "DISABLED";
        const std::string key=l.id+":"+std::to_string(static_cast<int>(l.kind))+":"+l.executable+":"+l.contentBundle+":"+l.moduleId;
        const auto found=launcherStatusCache_.find(key);
        if(found!=launcherStatusCache_.end())return found->second;
        return launcherStatusCache_[key]=LauncherReadiness(l);
    }
    // Resolve at launch, not scan time: changing an adapter needs no library rescan.
    std::string ConfigureLaunch(GameEntry &game,std::string &homebrew,std::string *requestFile=nullptr,VCInject::Progress progress={}) {
        launchPlan_={};
        if(!pendingInjection_.empty()) {
            ClearLaunchOverride();
            const auto module=std::move(pendingInjection_);pendingInjection_.clear();
            const auto launcher=std::find_if(launchers_.begin(),launchers_.end(),[&](const auto &l){return l.id==module;});
            if(launcher==launchers_.end()||!launcher->enabled)return "VC INJECTION LAUNCHER MISSING / DISABLED";
            const GameEntry rom=game;VCInject::Settings settings;
            const auto profileError=VCInjectionProfile(FindLauncherModule(launcher->moduleId),rom,module,settings);
            if(!profileError.empty())return profileError;
            settings.reuse=!VCInject::HasSaved(rom);
            auto error=VCInject::Prepare(game,settings,progress);if(!error.empty())return error;
            error=VCInject::SavePrepared(rom,game,progress);if(!error.empty())return error;
            savedInjectionState_[LauncherGameKey(rom)]=true;
            auto next=gameLaunchers_;next[LauncherGameKey(rom)]=module;
            if(!SaveLauncherOverrides(next))return "INJECTION SAVED - SELECT SAVED INJECTED ROM; DEFAULT SAVE FAILED";
            gameLaunchers_=std::move(next);injectionFinished_=true;return {};
        }
        std::string id=LaunchLauncher(game);
        ClearLaunchOverride();
        if(id.empty()) {
            if(game.platform==Platform::Files)return "SELECT A LAUNCHER IN LIBRARY / MANAGE CONTENT TYPES";
            id=game.platform==Platform::WiiU?"native":game.launcher==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
        }
        const auto it=std::find_if(launchers_.begin(),launchers_.end(),[&](const auto &l){return l.id==id;});
        if(game.platform==Platform::Files)
            Trace(("launcher-resolved id="+id+" module="+(it==launchers_.end()?std::string("?"):it->moduleId)).c_str());
        if(it==launchers_.end())return "LAUNCHER MISSING - CHECK SETTINGS / LAUNCHERS";
        if(!it->enabled)return "LAUNCHER DISABLED - CHECK SETTINGS / LAUNCHERS";
        if(!LauncherAccepts(*it,game.platform))return "LAUNCHER DOES NOT SUPPORT THIS CONTENT SCANNER";
        if(game.platform==Platform::Files&&!LauncherMatchesFile(*it,game.absolutePath))return "ROM EXTENSION NOT SUPPORTED BY THIS LAUNCHER";
        if(it->kind==LauncherKind::DSInjection) {
            const auto key=LauncherGameKey(game);
            const auto error=ConfigureVCLaunch(game,FindLauncherModule(it->moduleId),it->id,progress);
            if(error.empty())savedInjectionState_[key]=true;else savedInjectionState_.erase(key);
            return error; // Automatic preparation continues into the normal raw launch.
        }
        if(it->kind==LauncherKind::VirtualConsole)return ResolveVCInjection(game,games_);
        if(it->kind==LauncherKind::Emulator) {
            if(!it->moduleId.empty()) {
                const auto *m=FindLauncherModule(it->moduleId);if(!m)return "LAUNCH MODULE NOT INSTALLED";
                if(!m->error.empty())return m->error;
                if(m->protocol==kRetroArchProtocol) {
                    const auto problem=RetroArchLauncherProblem(*it);
                    if(!problem.empty())return "RETROARCH "+problem;
                    // RetroArch takes the ROM as a native argument; the core is
                    // the executable and RetroArch's bundle supplies its assets.
                    const auto rom=UFlowRom::RelativeROM(game.absolutePath);
                    if(rom.empty()||!UFlowRom::File(game.absolutePath))return "MODULE REQUIRES A READABLE SD ROM";
                    std::string why;
                    auto bytes=RetroArchArgs::Pack(RetroArchArgs::CafePath(it->executable),
                                                   RetroArchArgs::CafePath(rom),&why);
                    if(bytes.empty())return "CANNOT BUILD LAUNCH ARGUMENTS - "+why;
                    launchPlan_={HomebrewLaunchPlan::Transport::RetroArchArgs,it->executable,it->contentBundle,{},std::move(bytes)};
                    Trace(("retroarch-plan core="+it->executable+" bundle="+it->contentBundle+" rom="+rom).c_str());
                    homebrew=it->executable;return {};
                }
                const auto error=ModuleReadiness(*m);if(!error.empty())return error;
                const auto rom=UFlowRom::RelativeROM(game.absolutePath);
                if(rom.empty()||!UFlowRom::File(game.absolutePath))return "MODULE REQUIRES A READABLE SD ROM";
                if(!requestFile)return "LAUNCH REQUEST OUTPUT REQUIRED";
                *requestFile="fs:/vol/external01/"+m->request;
                homebrew=m->executable;return {};
            }
            if(!LauncherExecutableExists(it->executable))return "EMULATOR NOT FOUND - SET ITS EXECUTABLE IN LAUNCHERS";
            homebrew=LauncherSDPath(it->executable);return {};
        }
        if(it->kind==LauncherKind::WiiFlow)game.launcher=GameLauncher::WiiFlow;
        if(it->kind==LauncherKind::USBLoaderGX)game.launcher=GameLauncher::USBLoaderGX;
        return {};
    }
    std::string ConsumeHomebrewRequest(){auto result=std::move(pendingHomebrew_);pendingHomebrew_.clear();return result;}
    HomebrewLaunchPlan ConsumeLaunchPlan(){auto plan=std::move(launchPlan_);launchPlan_={};return plan;}
    int InjectionCacheRow() const {return injectionDraft_.system=="ds"?14:10;}
    int LauncherRows() const {return vcMapMode_?static_cast<int>(vcMapGames_.size())+1:launcherBrowse_?static_cast<int>(launcherFiles_.size()):launcherEditor_?static_cast<int>(LauncherEditorRows().size()):static_cast<int>(launchers_.size())+1;}
    void RefreshVCMap(int mode) {
        vcMapMode_=mode;vcMapGames_.clear();launcherRow_=0;
        for(std::size_t i=0;i<games_.size();++i) {
            const auto &g=games_[i];
            if(mode==1?(g.platform==Platform::Files&&LauncherMatchesFile(launcherDraft_,g.absolutePath)):g.platform==Platform::WiiU&&g.titleId.rfind("00050000",0)==0)vcMapGames_.push_back(i);
        }
    }
    void RefreshLauncherFiles() {
        launcherFiles_={".."};
        const bool injection=launcherDraft_.kind==LauncherKind::DSInjection;
        if(injection)launcherFiles_.push_back("[USE THIS FOLDER]");
        DIR *dir=opendir(launcherBrowsePath_.c_str());
        if(!dir){SetStatus("DIRECTORY NOT AVAILABLE");launcherRow_=0;return;}
        while(auto *entry=readdir(dir)) {
            const std::string name=entry->d_name;if(name=="."||name=="..")continue;
            const auto path=launcherBrowsePath_+"/"+name;
            if(DirectoryExists(path))launcherFiles_.push_back(name+"/");
            else if(!injection&&!LauncherSDPath(path).empty())launcherFiles_.push_back(name);
        }
        closedir(dir);std::sort(launcherFiles_.begin()+(injection?2:1),launcherFiles_.end());launcherRow_=0;
    }
    // The edit page lists settings only, as logical row ids (the numbers the
    // handlers use): 0 name, 1 method, 2 system, 3 extensions, 4 executable or
    // VC base, 5 enabled, 8 save, 9 remove, 10 launch module (emulators) or
    // injector options (10-14), 11 RetroArch bundle. Each launcher's status is
    // shown in the launcher list, not here.
    std::vector<int> LauncherEditorRows() const {
        std::vector<int> rows={0,1,2,3};
        if(launcherDraft_.kind==LauncherKind::DSInjection) {
            rows.insert(rows.end(),{4,5});
            if(injectionDraft_.system=="ds")rows.insert(rows.end(),{11,12,13,14});
            else rows.push_back(10);
        } else {
            if(launcherDraft_.kind==LauncherKind::Emulator)rows.push_back(4);
            rows.push_back(5);
            if(launcherDraft_.kind==LauncherKind::Emulator)rows.push_back(10);
            if(IsRetroArchLauncher(launcherDraft_))rows.push_back(11);
        }
        rows.insert(rows.end(),{8,9});
        return rows;
    }
    int LauncherLogicalRow() const {
        const auto rows=LauncherEditorRows();
        return launcherRow_>=0&&launcherRow_<static_cast<int>(rows.size())?rows[launcherRow_]:-1;
    }
    int LauncherPhysicalRow(int logical) const {
        const auto rows=LauncherEditorRows();
        const auto found=std::find(rows.begin(),rows.end(),logical);
        return found==rows.end()?0:static_cast<int>(found-rows.begin());
    }
    std::pair<std::string,std::string> LauncherRowText(int row) {
        const bool injector=launcherDraft_.kind==LauncherKind::DSInjection;
        const bool fromModule=!launcherDraft_.moduleId.empty();
        const std::string moduleNote=fromModule?"  (FROM MODULE)":"";
        const std::string injectorNote=injector?moduleNote:"";
        switch(row) {
            case 0: return {"NAME",launcherDraft_.name};
            case 1: return {"METHOD",LauncherKindName(launcherDraft_.kind)};
            case 2: return {"SYSTEM",launcherDraft_.system+injectorNote};
            case 3: return {"ROM EXTENSIONS",launcherDraft_.extensions+injectorNote};
            case 4:
                if(injector)return {"VC BASE FOLDER",injectionDraft_.base.empty()?std::string("SELECT CODE / CONTENT / META BASE"):injectionDraft_.base};
                return {IsRetroArchLauncher(launcherDraft_)?"CORE (.RPX)":"EXECUTABLE",launcherDraft_.executable};
            case 5: return {"ENABLED",OnOff(launcherDraft_.enabled)};
            case 8: return {"SAVE LAUNCHER","A  SAVE"};
            case 9: return {"REMOVE ENTRY","FILES ARE NOT DELETED"};
            default: break;
        }
        if(injector) {
            if(row==InjectionCacheRow())return {"CACHE LIMIT",std::to_string(injectionDraft_.cacheMiB)+" MiB"};
            if(row==11)return {"ZIP COMPRESSION",injectionDraft_.compression==0?"STORED":injectionDraft_.compression==1?"FAST DEFLATE":"SMALLER DEFLATE"};
            if(row==12)return {"BRIGHTNESS",injectionDraft_.brightness<0?std::string("BASE DEFAULT"):std::to_string(injectionDraft_.brightness)};
            if(row==13)return {"PIXEL ART UPSCALER",injectionDraft_.upscale<0?std::string("BASE DEFAULT"):std::string(OnOff(injectionDraft_.upscale!=0))};
        }
        if(row==10) {
            const auto *module=fromModule?FindLauncherModule(launcherDraft_.moduleId):nullptr;
            if(module)return {"LAUNCH MODULE",module->file+" - ONE-PRESS LAUNCH"};
            return {"LAUNCH MODULE",fromModule?launcherDraft_.moduleId+" - MODULE FILE MISSING":"NONE - OPENS THE EMULATOR"};
        }
        if(row==11)return {"RETROARCH (.WUHB)",launcherDraft_.contentBundle};
        return {"",""};
    }
    void LauncherInput(int horizontal,int vertical,bool accept,bool back) {
        if(back) {
            if(vcMapMode_){if(vcMapMode_==2)RefreshVCMap(1);else{vcMapMode_=0;launcherRow_=7;}return;}
            if(launcherBrowse_){launcherBrowse_=false;launcherRow_=LauncherPhysicalRow(launcherBrowseBundle_?11:4);return;}
            if(settingsEditing_){settingsEditing_=false;return;}
            CloseOverlay();return;
        }
        if(vertical){settingsEditing_=false;const int count=LauncherRows();launcherRow_=(launcherRow_+vertical+count)%count;return;}
        if(horizontal) {
            if(settingsEditing_&&launcherEditor_) {
                const int row=LauncherLogicalRow();
                if(row==1) {do{launcherDraft_.kind=static_cast<LauncherKind>((static_cast<int>(launcherDraft_.kind)+horizontal+5)%5);}while(launcherDraft_.kind==LauncherKind::VirtualConsole);}
                if(row==5)launcherDraft_.enabled=!launcherDraft_.enabled;
                if(row==10&&launcherDraft_.kind==LauncherKind::Emulator)CycleLauncherModule(horizontal);
                if(launcherDraft_.kind==LauncherKind::DSInjection) {
                    if(injectionDraft_.system!="ds") {
                        if(row==10)injectionDraft_.cacheMiB=((injectionDraft_.cacheMiB/512-1+horizontal+16)%16+1)*512;
                        return;
                    }
                    if(row==10)SetStatus("USE INJECT / RE-INJECT IN GAME DETAILS");
                    if(row==11){const int levels[]={0,1,6};int i=injectionDraft_.compression==0?0:injectionDraft_.compression==1?1:2;injectionDraft_.compression=levels[(i+horizontal+3)%3];}
                    if(row==12){int i=injectionDraft_.brightness<0?0:injectionDraft_.brightness/10+1;i=(i+horizontal+12)%12;injectionDraft_.brightness=i==0?-1:(i-1)*10;}
                    if(row==13)injectionDraft_.upscale=(injectionDraft_.upscale+1+horizontal+3)%3-1;
                    if(row==14)injectionDraft_.cacheMiB=((injectionDraft_.cacheMiB/512-1+horizontal+16)%16+1)*512;
                }
            } else if(horizontal<0)settingsSidebar_=true;
            return;
        }
        if(accept)LauncherActivate();
    }
    // LAUNCH MODULE: NONE, or a module that can hand this launcher a game.
    void CycleLauncherModule(int direction) {
        std::vector<std::string> choices={""};
        for(const auto &m:InstalledLauncherModules())if(CompatibleLauncherModule(m,launcherDraft_))choices.push_back(m.id);
        const auto found=std::find(choices.begin(),choices.end(),launcherDraft_.moduleId);
        const int count=static_cast<int>(choices.size());
        int pos=found==choices.end()?0:static_cast<int>(found-choices.begin());
        pos=(pos+direction+count)%count;
        ApplyLauncherModule(choices[pos]);
        launcherRow_=LauncherPhysicalRow(10);
    }
    void ApplyLauncherModule(const std::string &id) {
        const auto *m=id.empty()?nullptr:FindLauncherModule(id);
        if(!m) {
            // One-press off. RetroArch then opens normally through its bundle.
            if(IsRetroArchLauncher(launcherDraft_)&&RetroArchBundlePath(launcherDraft_.contentBundle))
                launcherDraft_.executable=launcherDraft_.contentBundle;
            launcherDraft_.moduleId.clear();launcherDraft_.contentBundle.clear();launcherDraft_.moduleOptOut=true;
            return;
        }
        launcherDraft_.moduleId=m->id;launcherDraft_.moduleOptOut=false;
        if(m->protocol==kRetroArchProtocol) {
            if(!RetroArchCorePath(launcherDraft_.executable))launcherDraft_.executable=m->executable;
            if(!RetroArchBundlePath(launcherDraft_.contentBundle))launcherDraft_.contentBundle=m->contentBundle;
        } else launcherDraft_.executable=m->executable;
        if(launcherDraft_.extensions.empty())launcherDraft_.extensions=m->extensions;
    }
    void LauncherActivate() {
        if(vcMapMode_) {
            if(launcherRow_==0){LauncherInput(0,0,false,true);return;}
            const auto index=vcMapGames_[launcherRow_-1];
            if(vcMapMode_==1){vcMapRom_=index;RefreshVCMap(2);return;}
            if(!SaveVCMapping(games_[vcMapRom_],games_[index])){SetStatus("COULD NOT SAVE ROM MAPPING - CHECK SD AND TITLE");return;}
            vcMapMode_=0;launcherRow_=7;SetStatus("MAPPED TO EXISTING INJECTION - NO GAME FILES MODIFIED",7000);return;
        }
        if(launcherBrowse_) {
            const auto name=launcherFiles_[launcherRow_];
            if(name=="[USE THIS FOLDER]") {
                injectionDraft_.base=UFlowRom::RelativeROM(launcherBrowsePath_);
                launcherBrowse_=false;launcherRow_=LauncherPhysicalRow(4);return;
            }
            if(name=="..") {
                if(launcherBrowsePath_=="fs:/vol/external01"){launcherBrowse_=false;launcherRow_=LauncherPhysicalRow(launcherBrowseBundle_?11:4);return;}
                launcherBrowsePath_.erase(launcherBrowsePath_.find_last_of('/'));RefreshLauncherFiles();
            } else if(name.back()=='/') {launcherBrowsePath_+="/"+name.substr(0,name.size()-1);RefreshLauncherFiles();}
            else {
                const auto picked=LauncherSDPath(launcherBrowsePath_+"/"+name);
                if(IsRetroArchLauncher(launcherDraft_)) {
                    // The core is an .rpx; RetroArch itself is its .wuhb.
                    if(launcherBrowseBundle_&&!RetroArchBundlePath(picked)){SetStatus("CHOOSE RETROARCH'S .WUHB FILE");return;}
                    if(!launcherBrowseBundle_&&!RetroArchCorePath(picked)){SetStatus("CHOOSE A CORE .RPX FILE");return;}
                }
                if(launcherBrowseBundle_)launcherDraft_.contentBundle=picked;else launcherDraft_.executable=picked;
                launcherBrowse_=false;launcherRow_=LauncherPhysicalRow(launcherBrowseBundle_?11:4);
            }
            return;
        }
        if(!launcherEditor_) {
            launcherIndex_=launcherRow_-1;
            if(launcherIndex_<0) {
                launcherDraft_={"","New Emulator",LauncherKind::Emulator,"Custom",".nds","",true};
                unsigned serial=1;do{launcherDraft_.id="launcher_"+std::to_string(serial++);}
                while(std::any_of(launchers_.begin(),launchers_.end(),[&](const auto &l){return l.id==launcherDraft_.id;}));
            }else launcherDraft_=launchers_[launcherIndex_];
            injectionDraft_=VCInject::Load(launcherDraft_.id);
            if(const auto *module=FindLauncherModule(launcherDraft_.moduleId))if(!VCSystems::Protocol(module->protocol).empty())injectionDraft_.system=VCSystems::Protocol(module->protocol);
            launcherEditor_=true;launcherRow_=0;return;
        }
        const int row=LauncherLogicalRow();
        if(row==0||row==2||row==3) {
            if(launcherDraft_.kind==LauncherKind::DSInjection&&row!=0){SetStatus("SET BY THE VC INJECTOR - IT ONLY INJECTS THIS SYSTEM'S ROMS");return;}
            launcherTextField_=row;typeBuffer_=row==0?launcherDraft_.name:row==2?launcherDraft_.system:launcherDraft_.extensions;
            typeText_=true;typeKey_=0;return;
        }
        if(row==1||row==5) {
            if(row==1&&!launcherDraft_.moduleId.empty()){SetStatus("SET LAUNCH MODULE TO NONE TO CHANGE THE METHOD");return;}
            if(row==1&&(launcherDraft_.id=="native"||launcherDraft_.id=="wiiflow"||launcherDraft_.id=="usbloadergx")){SetStatus("BUILT-IN ADAPTER TYPE IS FIXED");return;}
            settingsEditing_=!settingsEditing_;return;
        }
        if(row==4) {
            if(launcherDraft_.kind==LauncherKind::DSInjection) {
                launcherBrowseBundle_=false;
                launcherBrowse_=true;launcherBrowsePath_="fs:/vol/external01";RefreshLauncherFiles();return;
            }
            if(!launcherDraft_.moduleId.empty()&&!IsRetroArchLauncher(launcherDraft_)){SetStatus("ONE-PRESS NEEDS THIS EMULATOR'S OWN BUILD - SET LAUNCH MODULE TO NONE TO CHOOSE ANOTHER FILE",8000);return;}
            if(launcherDraft_.kind!=LauncherKind::Emulator){SetStatus("BUILT-IN ADAPTER - NO EXECUTABLE SELECTION NEEDED");return;}
            launcherBrowseBundle_=false;
            launcherBrowse_=true;launcherBrowsePath_="fs:/vol/external01";RefreshLauncherFiles();return;
        }
        if(row==10&&launcherDraft_.kind==LauncherKind::Emulator){settingsEditing_=!settingsEditing_;return;}
        if(row==11&&IsRetroArchLauncher(launcherDraft_)) {
            launcherBrowseBundle_=true;
            launcherBrowse_=true;launcherBrowsePath_="fs:/vol/external01";RefreshLauncherFiles();return;
        }
        if(row==8) {
            if(launcherDraft_.kind==LauncherKind::DSInjection&&!VCInject::Save(launcherDraft_.id,injectionDraft_)){SetStatus("INVALID VC SETTINGS OR SD WRITE ERROR");return;}
            for(const auto &l:libraries_)if(l.launcherId==launcherDraft_.id&&!LauncherAccepts(launcherDraft_,l.parser)){SetStatus("REASSIGN INCOMPATIBLE CONTENT TYPES FIRST");return;}
            auto next=launchers_;if(launcherIndex_<0)next.push_back(launcherDraft_);else next[launcherIndex_]=launcherDraft_;
            if(!SaveLaunchers(next)){SetStatus("COULD NOT SAVE - CHECK NAME, EXECUTABLE AND SD");return;}
            launchers_=std::move(next);launcherEditor_=false;launcherRow_=launcherIndex_<0?static_cast<int>(launchers_.size()):launcherIndex_+1;
            if(launcherDraft_.kind==LauncherKind::DSInjection) {
                const auto error=VCInject::CheckBase(injectionDraft_);
                SetStatus(error.empty()?"LAUNCHER SAVED - VC BASE STRUCTURE OK":"LAUNCHER SAVED - "+error,10000);return;
            }
            SetStatus("LAUNCHER SAVED - ASSIGN IT IN CONTENT TYPES");return;
        }
        if(row==9) {
            if(!launcherDraft_.moduleId.empty()){SetStatus("DISABLE HERE; REMOVE THE .ufmod FILE TO UNINSTALL");return;}
            if(launcherIndex_<0){launcherEditor_=false;launcherRow_=0;return;}
            if(launcherDraft_.id=="native"||launcherDraft_.id=="wiiflow"||launcherDraft_.id=="usbloadergx"){SetStatus("BUILT-IN ADAPTER - DISABLE INSTEAD");return;}
            for(const auto &l:libraries_)if(l.launcherId==launcherDraft_.id){SetStatus("REASSIGN CONTENT TYPES USING THIS LAUNCHER FIRST");return;}
            for(const auto &s:sources_)if(s.launcherId==launcherDraft_.id){SetStatus("REASSIGN DIRECTORIES USING THIS LAUNCHER FIRST");return;}
            for(const auto &[key,id]:gameLaunchers_)if(id==launcherDraft_.id){SetStatus("REASSIGN GAMES USING THIS LAUNCHER FIRST");return;}
            auto next=launchers_;next.erase(next.begin()+launcherIndex_);
            if(!SaveLaunchers(next)){SetStatus("COULD NOT SAVE LAUNCHERS");return;}
            launchers_=std::move(next);launcherEditor_=false;launcherRow_=0;SetStatus("ENTRY REMOVED - EXECUTABLE AND GAMES UNCHANGED");
        }
        if(launcherDraft_.kind==LauncherKind::DSInjection&&row>=10&&row<=InjectionCacheRow())settingsEditing_=!settingsEditing_;
    }
    void DrawLaunchers(int x,Color accent,bool glass) {
        const Color text=glass?Color{43,54,63,255}:Color{238,244,252,255};
        DrawText(renderer_,vcMapMode_==1?"SELECT ROM TO MAP":vcMapMode_==2?"SELECT ITS PREPARED INJECTION":launcherBrowse_?(launcherDraft_.kind==LauncherKind::DSInjection?"SELECT VC BASE FOLDER":"SELECT EMULATOR EXECUTABLE"):launcherEditor_?"LAUNCHER / EDIT":"LAUNCHERS",x,205,3,text);
        std::vector<std::pair<std::string,std::string>> rows;
        if(vcMapMode_) {
            DrawFittedText(renderer_,vcMapMode_==2?games_[vcMapRom_].name:"ROM FILES FROM YOUR SCANNED LIBRARIES",x,237,720,1,text);
            rows.push_back({"BACK",vcMapGames_.empty()?"NO MATCHES - ADD CONTENT AND SCAN":""});
            for(auto index:vcMapGames_)rows.push_back({games_[index].name,vcMapMode_==2?games_[index].titleId:games_[index].libraryId});
        }
        else if(launcherBrowse_){DrawFittedText(renderer_,launcherBrowsePath_,x,237,720,1,text);for(const auto &f:launcherFiles_)rows.push_back({f,""});}
        else if(launcherEditor_){for(int row:LauncherEditorRows())if(row!=8 && row!=9)rows.push_back(LauncherRowText(row));}
        else {rows.push_back({"+  ADD LAUNCHER / EMULATOR",""});for(const auto &l:launchers_)rows.push_back({l.name,CachedLauncherStatus(l)});}
        const int first=std::max(0,launcherRow_-5);
        DrawText(renderer_,std::to_string(launcherRow_+1)+" / "+std::to_string(rows.size()),x+665,237,1,text);
        for(int i=first;i<std::min(first+7,static_cast<int>(rows.size()));++i) {
            int y=260+(i-first)*40;
            if(i==launcherRow_&&!settingsSidebar_&&settingsAction_<0)FillRoundedRect(renderer_,{x-12,y-5,750,36},10,{accent.r,accent.g,accent.b,70});
            DrawFittedText(renderer_,rows[i].first,x,y,launcherBrowse_?720:320,2,text);
            if(!launcherBrowse_)DrawFittedText(renderer_,rows[i].second,x+340,y,380,2,text);
        }
        DrawSettingsActions(x,accent);
    }
    int TypeRowCount() const { return typeEditor_?12:static_cast<int>(libraries_.size())+1; }
    void TypeAdjust(int amount) {
        const auto previousSize=CaseProfiles::For(typeDraft_.id,typeDraft_.launcherId,typeDraft_.extensions);
        const bool autoSize=typeDraft_.caseWidthMm==previousSize.width && typeDraft_.caseHeightMm==previousSize.height && typeDraft_.caseThicknessMm==previousSize.thickness;
        if(typeRow_==1) {typeDraft_.parser=static_cast<Platform>((static_cast<int>(typeDraft_.parser)+amount+4)%4);typeDraft_.launcherId.clear();}
        if(typeRow_==3) {
            std::vector<std::string> choices={""};for(const auto &l:launchers_)if(l.enabled&&LauncherAccepts(l,typeDraft_.parser)) {
                bool compatible=l.kind!=LauncherKind::DSInjection;
                if(!compatible){std::istringstream extensions(typeDraft_.extensions);std::string ext;while(std::getline(extensions,ext,','))if(LauncherMatchesFile(l,"game"+ext))compatible=true;}
                if(compatible)choices.push_back(l.id);
            }
            auto it=std::find(choices.begin(),choices.end(),typeDraft_.launcherId);
            int pos=it==choices.end()?0:static_cast<int>(it-choices.begin());
            typeDraft_.launcherId=choices[(pos+amount+choices.size())%choices.size()];
        }
        if(typeRow_==4) typeDraft_.enabled=!typeDraft_.enabled;
        if(typeRow_==5) typeDraft_.order=std::clamp(typeDraft_.order+amount,0,static_cast<int>(libraries_.size()));
        if(typeRow_==6) typeDraft_.caseWidthMm=std::clamp(typeDraft_.caseWidthMm+amount,50,200);
        if(typeRow_==7) typeDraft_.caseHeightMm=std::clamp(typeDraft_.caseHeightMm+amount,50,250);
        if(typeRow_==8) typeDraft_.caseThicknessMm=std::clamp(typeDraft_.caseThicknessMm+amount,2,40);
        if(typeRow_==9) typeDraft_.mediaByContentType=!typeDraft_.mediaByContentType;
        if(autoSize && (typeRow_==1 || typeRow_==3)) {
            const auto size=CaseProfiles::For(typeDraft_.id,typeDraft_.launcherId,typeDraft_.extensions);
            typeDraft_.caseWidthMm=size.width;typeDraft_.caseHeightMm=size.height;typeDraft_.caseThicknessMm=size.thickness;
        }
    }
    void TypeActivate() {
        if(!typeEditor_) {
            typeIndex_=typeRow_-1;
            if(typeIndex_<0) {
                typeDraft_={"","New Content Type",Platform::Files,GameLauncher::WiiFlow,".nds",true,static_cast<int>(libraries_.size())};
                typeDraft_.caseHeightMm=122;typeDraft_.caseThicknessMm=15;
                unsigned serial=1;
                do { typeDraft_.id="custom_"+std::to_string(serial++); }
                while(std::any_of(libraries_.begin(),libraries_.end(),[&](const auto&l){return l.id==typeDraft_.id;}));
            } else typeDraft_=libraries_[typeIndex_];
            initialTypeDraft_=typeDraft_;typeEditor_=true;typeRow_=0;return;
        }
        if(typeRow_==0||typeRow_==2) { typeName_=typeRow_==0;typeBuffer_=typeName_?typeDraft_.name:typeDraft_.extensions;typeText_=true;typeKey_=0;return; }
        if(typeRow_==10) {
            if(typeDraft_.name.find_first_not_of(' ')==std::string::npos) {SetStatus("ENTER A NAME");return;}
            if(typeDraft_.parser==Platform::Files&&typeDraft_.extensions.empty()){SetStatus("ENTER FILE EXTENSIONS");return;}
            auto next=libraries_;
            if(typeIndex_>=0) next.erase(next.begin()+typeIndex_);
            const int position=std::clamp(typeDraft_.order,0,static_cast<int>(next.size()));
            next.insert(next.begin()+position,typeDraft_);
            for(std::size_t i=0;i<next.size();++i) next[i].order=static_cast<int>(i);
            if(!SaveLibraries(next,false)){SetStatus("COULD NOT SAVE CONTENT TYPES");return;}
            libraries_=std::move(next);typeEditor_=false;settingsEditing_=false;typeRow_=position+1;tab_=0;visualTab_=0;RebuildVisible();
            SetStatus("CONTENT TYPE SAVED",4000);return;
        }
        if(typeRow_==11) {
            if(typeIndex_<0){typeEditor_=false;typeRow_=0;return;}
            if(std::any_of(sources_.begin(),sources_.end(),[&](const auto&s){return s.libraryId==typeDraft_.id;}) ||
               std::any_of(mediaSources_.begin(),mediaSources_.end(),[&](const auto&s){return s.libraryId==typeDraft_.id;})) {SetStatus("REASSIGN THIS TYPE'S CONTENT AND MEDIA DIRECTORIES FIRST",6000);return;}
            if(typeDraft_.id=="wiiu"){SetStatus("DISABLE THE BUILT-IN WII U TYPE INSTEAD");return;}
            auto next=libraries_;next.erase(next.begin()+typeIndex_);
            if(!SaveLibraries(next,false)){SetStatus("COULD NOT SAVE CONTENT TYPES");return;}
            libraries_=std::move(next);typeEditor_=false;typeRow_=0;tab_=0;visualTab_=0;RebuildVisible();return;
        }
        settingsEditing_=!settingsEditing_;
    }
    static constexpr const char *TypeKeys="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,_-/:";
    void TypeTextInput(int horizontal,int vertical,bool accept,bool back,bool erase,bool done) {
        if(back){if(!ConfirmDraftExit(DraftExit::Text))typeText_=false;return;}
        const int count=static_cast<int>(std::strlen(TypeKeys));
        typeKey_=(typeKey_+horizontal+vertical*12+count*12)%count;
        if(erase&&!typeBuffer_.empty()) typeBuffer_.pop_back();
        if(accept&&typeBuffer_.size()<(launcherEditor_?240u:80u)) typeBuffer_+=TypeKeys[typeKey_];
        if(done){
            if(launcherEditor_) {
                if(launcherTextField_==0)launcherDraft_.name=typeBuffer_;
                if(launcherTextField_==2)launcherDraft_.system=typeBuffer_;
                if(launcherTextField_==3)launcherDraft_.extensions=typeBuffer_;
            } else if(typeName_)typeDraft_.name=typeBuffer_;else {
                const auto old=CaseProfiles::For(typeDraft_.id,typeDraft_.launcherId,typeDraft_.extensions);
                const bool autoSize=typeDraft_.caseWidthMm==old.width && typeDraft_.caseHeightMm==old.height && typeDraft_.caseThicknessMm==old.thickness;
                typeDraft_.extensions=typeBuffer_;
                if(autoSize) {
                    const auto size=CaseProfiles::For(typeDraft_.id,typeDraft_.launcherId,typeDraft_.extensions);
                    typeDraft_.caseWidthMm=size.width;typeDraft_.caseHeightMm=size.height;typeDraft_.caseThicknessMm=size.thickness;
                }
            }
            typeText_=false;
        }
    }
    void DrawTypes(int x,Color accent,bool glass) {
        const Color text=glass?Color{43,54,63,255}:Color{238,244,252,255};
        DrawText(renderer_,typeText_?(launcherEditor_?"EDIT LAUNCHER FIELD":typeName_?"EDIT NAME":"EDIT EXTENSIONS"):typeEditor_?"CONTENT TYPE / EDIT":"MANAGE CONTENT TYPES",x,205,3,text);
        if(typeText_) {
            DrawFittedText(renderer_,typeBuffer_+"_",x,260,720,2,text);
            for(int i=0;TypeKeys[i];++i) {int px=x+(i%12)*60,py=320+(i/12)*32;
                if(i==typeKey_) FillRoundedRect(renderer_,{px-5,py-4,42,30},8,{accent.r,accent.g,accent.b,100});
                DrawText(renderer_,std::string(1,TypeKeys[i]),px,py,2,text);}
            DrawSettingsActions(x,accent);return;
        }
        std::vector<std::pair<std::string,std::string>> rows;
        if(typeEditor_) rows={{"NAME",typeDraft_.name},{"SCANNER",PlatformName(typeDraft_.parser)},
            {"EXTENSIONS",typeDraft_.extensions},{"LAUNCHER",LauncherLabel(typeDraft_.launcherId)},
            {"ENABLED",OnOff(typeDraft_.enabled)},{"RAIL POSITION",std::to_string(typeDraft_.order+1)},
            {"CASE WIDTH",std::to_string(typeDraft_.caseWidthMm)+" mm"},
            {"CASE HEIGHT",std::to_string(typeDraft_.caseHeightMm)+" mm"},
            {"CASE THICKNESS",std::to_string(typeDraft_.caseThicknessMm)+" mm"},
            {"MEDIA BY CONTENT TYPE",OnOff(typeDraft_.mediaByContentType)}};
        else {rows.push_back({"+  ADD CONTENT TYPE",""});for(const auto&l:libraries_) rows.push_back({l.name,OnOff(l.enabled)});}
        const int first=std::max(0,typeRow_-5);
        for(int i=first;i<std::min(first+7,static_cast<int>(rows.size()));++i){const int y=260+(i-first)*40;
            if(i==typeRow_&&!settingsSidebar_&&settingsAction_<0)FillRoundedRect(renderer_,{x-12,y-5,750,36},10,{accent.r,accent.g,accent.b,70});
            DrawFittedText(renderer_,rows[i].first,x,y,320,2,text);DrawFittedText(renderer_,rows[i].second,x+340,y,380,2,text);}
        DrawSettingsActions(x,accent);
    }
    std::vector<MediaDirectoryRow> MediaRows() const { return MediaDirectoryRows(mediaSources_,libraries_,expandedMediaGroups_); }
    int SourceListCount() const { return mediaMode_?static_cast<int>(MediaRows().size()):SourceCount()+4; }
    void FocusMediaSource(int index) {
        if(index<0 || index>=static_cast<int>(mediaSources_.size())) return;
        expandedMediaGroups_.insert(MediaGroupKey(mediaSources_[index]));
        const auto rows=MediaRows();
        for(int row=0;row<static_cast<int>(rows.size());++row)
            if(rows[row].kind==MediaDirectoryRow::Directory && rows[row].source==index) { sourceRow_=row; return; }
    }
    void SourceMove(int amount) {
        const int count = SourceListCount();
        sourceRow_ = (sourceRow_ + amount + count) % count;
    }

    void SourceActivate() {
        if(!mediaMode_ && sourceRow_==0) { typeManager_=true; typeEditor_=false; typeRow_=0; return; }
        int actual=sourceRow_-1;
        if(mediaMode_) {
            const auto rows=MediaRows();
            sourceRow_=std::clamp(sourceRow_,0,static_cast<int>(rows.size())-1);
            const auto &row=rows[sourceRow_];
            if(row.kind==MediaDirectoryRow::Header) {
                if(!expandedMediaGroups_.erase(row.key)) expandedMediaGroups_.insert(row.key);
                return;
            }
            if(row.kind==MediaDirectoryRow::Scan) { SourceScan(); return; }
            if(row.kind==MediaDirectoryRow::Help) { mediaHelp_=true; return; }
            actual=row.kind==MediaDirectoryRow::Directory?row.source:SourceCount();
        }
        if (actual==SourceCount()+1) { SourceScan(); return; }
        if (!mediaMode_ && actual==SourceCount()+2) { SourceScan(false); return; }
        editingSourceIndex_=actual<SourceCount() ? actual : -1;
        if (mediaMode_) draftMediaSource_=editingSourceIndex_>=0 ? mediaSources_[editingSourceIndex_] :
            MediaSource{MediaType::Cover,-1,"fs:/vol/external01",true,0};
        else draftSource_=editingSourceIndex_>=0 ? sources_[editingSourceIndex_] :
            ContentSource{Platform::WiiU,"fs:/vol/external01",true,3,GameLauncher::WiiFlow,"wiiu"};
        sourceEditorRow_ = 0;
        defaultFileChosen_=false;
        AutoSelectDraftDefault();
        CaptureSourceDraft();
        sourceEditor_ = true;
    }

    void CaptureSourceDraft() {
        initialSourceDraft_=draftSource_;initialMediaDraft_=draftMediaSource_;
    }
    bool CheckDraftExit(const std::string &path) {
        const auto fail=[](int line){std::printf("FAIL: unsaved editor line %d\n",line);return false;};
        for(const bool media:{false,true})for(const bool adding:{false,true}){
            VisualPreview(media?"mediaedit":"sourceedit");
            if(adding)SourceAdd();
            const auto sourceBefore=sources_;const auto mediaBefore=mediaSources_;
            SettingsInput(0,0,false,true);
            if(sourceEditor_ || draftExit_!=DraftExit::None)return fail(__LINE__);
            if(adding)SourceAdd();else {if(!media)sourceRow_=1;SourceActivate();}
            const auto original=DraftPath();DraftPath()+="/changed";
            SettingsInput(0,0,false,true);
            if(!sourceEditor_ || draftExit_==DraftExit::None || draftExitYes_)return fail(__LINE__);
            SettingsInput(0,0,true); // No is initially selected.
            if(!sourceEditor_ || draftExit_!=DraftExit::None || DraftPath()!=original+"/changed")return fail(__LINE__);
            DraftPath()=original;SettingsInput(0,0,false,true);
            if(sourceEditor_ || draftExit_!=DraftExit::None)return fail(__LINE__);
            if(adding)SourceAdd();else {if(!media)sourceRow_=1;SourceActivate();}
            if(media)draftMediaSource_.video.volume++;else draftSource_.autoSortVC=!draftSource_.autoSortVC;
            SettingsInput(0,0,false,true);
            if(draftExit_==DraftExit::None)return fail(__LINE__);
            if(!Render(0,path+(media?"-media":"-library")+(adding?"-new.png":"-existing.png")))return false;
            SettingsTouch(200,210); // Underlying sidebar must not receive a tap.
            if(draftExit_==DraftExit::None)return fail(__LINE__);
            SettingsTouch(790,440);
            if(sourceEditor_ || sources_!=sourceBefore || mediaSources_!=mediaBefore)return fail(__LINE__);
        }
        for(const bool adding:{false,true}){
            VisualPreview("sources");sourceRow_=0;SourceActivate();typeRow_=adding?0:1;TypeActivate();
            SettingsInput(0,0,false,true);
            if(typeEditor_ || draftExit_!=DraftExit::None)return fail(__LINE__);
            TypeActivate();const auto original=typeDraft_;typeDraft_.name+=" changed";
            SettingsInput(0,0,false,true);SettingsInput(0,0,false,true); // B keeps edits.
            if(!typeEditor_ || typeDraft_.name==original.name || draftExit_!=DraftExit::None)return fail(__LINE__);
            typeDraft_=original;SettingsInput(0,0,false,true);
            if(typeEditor_ || draftExit_!=DraftExit::None)return fail(__LINE__);
            TypeActivate();typeRow_=0;TypeActivate();typeBuffer_+='X';
            SettingsInput(0,0,false,true);
            if(draftExit_!=DraftExit::Text)return fail(__LINE__);
            SettingsInput(0,0,true);
            if(!typeText_ || typeBuffer_.back()!='X')return fail(__LINE__);
            SettingsInput(0,0,false,true);SettingsInput(1,0,true);
            if(typeText_ || typeDraft_!=original)return fail(__LINE__);
            typeDraft_.caseWidthMm++;SettingsInput(0,0,false,true);
            if(!Render(0,path+(adding?"-type-new.png":"-type-existing.png")))return false;
            SettingsInput(1,0,true);
            if(typeEditor_)return fail(__LINE__);
        }
        VisualPreview("sourceedit");draftSource_.depth++;
        SettingsChangeCategory(1);
        if(settingsCategory_!=1 || draftExit_!=DraftExit::Category)return fail(__LINE__);
        SettingsInput(1,0,true);
        if(settingsCategory_!=2 || sourceEditor_)return fail(__LINE__);
        VisualPreview("mediaedit");draftMediaSource_.defaultFile="chosen.png";ToggleSettings();
        if(!settings_ || draftExit_!=DraftExit::Settings)return fail(__LINE__);
        SettingsInput(1,0,true);if(settings_)return fail(__LINE__);
        // Saving the actual editor still commits, then exits without a warning.
        VisualPreview("sourceedit");std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
        draftSource_.depth=2;SaveDraftSource();
        if(sourceEditor_ || sources_[0].depth!=2 || draftExit_!=DraftExit::None)return fail(__LINE__);
        std::puts("PASS: new/existing library, media and content-type dirty exits, reverting, No/B, Yes/touch, field cancel, category switch and save");
        return true;
    }
    bool TypeTextChanged() const {
        return typeEditor_ && typeText_ && typeBuffer_!=(typeName_?typeDraft_.name:typeDraft_.extensions);
    }
    bool DraftChanged() const {
        if(typeEditor_)return typeDraft_!=initialTypeDraft_ || TypeTextChanged();
        return sourceEditor_ && (mediaMode_?draftMediaSource_!=initialMediaDraft_:draftSource_!=initialSourceDraft_);
    }
    bool ConfirmDraftExit(DraftExit action) {
        if(draftExit_!=DraftExit::None)return true;
        if(!(action==DraftExit::Text?TypeTextChanged():DraftChanged()))return false;
        draftExit_=action;draftExitYes_=false;
        return true;
    }
    void ResolveDraftExit(bool discard) {
        const auto action=draftExit_;draftExit_=DraftExit::None;
        if(!discard)return;
        if(action==DraftExit::Text){typeText_=false;return;}
        if(directoryBrowser_)BrowserCancel();
        sourceEditor_=typeEditor_=typeText_=settingsEditing_=false;
        if(action==DraftExit::Category)SettingsChangeCategory(draftExitCategory_);
        else if(action==DraftExit::Settings)ToggleSettings();
    }
    void DrawDraftExit(Color accent) {
        const bool light=preferences_.skin==1 || preferences_.skin==2;
        const PopupTextGlowScope glow(light);
        const Color ink=light?Color{38,53,64,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{0,0,kWidth,kHeight},0,{0,0,0,130});
        FillRoundedRect(renderer_,{300,225,680,280},24,light?Color{239,246,248,255}:Color{20,31,49,255});
        if(light)DrawGlassSurface(renderer_,{300,225,680,280},24,accent,100,true);
        DrawText(renderer_,"UNSAVED CHANGES",640,260,3,ink,true);
        DrawText(renderer_,"Exit without saving your changes?",640,324,2,ink,true);
        DrawText(renderer_,"Choose No to keep editing.",640,365,1,ink,true);
        for(int i=0;i<2;++i){
            const SDL_Rect rect{350+i*300,414,280,56};
            if(draftExitYes_==(i==1))FillRoundedRect(renderer_,{rect.x-3,rect.y-3,rect.w+6,rect.h+6},15,accent);
            FillRoundedRect(renderer_,rect,12,light?Color{225,239,245,255}:Color{32,52,73,255});
            DrawText(renderer_,i?"YES - DISCARD":"NO - KEEP EDITING",490+i*300,433,1,ink,true);
        }
    }

    void SourceAdd() {
        if(mediaMode_) {
            const auto rows=MediaRows();
            const auto selected=rows[std::clamp(sourceRow_,0,static_cast<int>(rows.size())-1)];
            sourceRow_=static_cast<int>(std::find_if(rows.begin(),rows.end(),[](const auto &row){return row.kind==MediaDirectoryRow::Add;})-rows.begin()); SourceActivate();
            if(selected.kind==MediaDirectoryRow::Header || selected.kind==MediaDirectoryRow::Directory)
                draftMediaSource_.libraryId=selected.key;
            CaptureSourceDraft();
            return;
        }
        sourceRow_=SourceCount()+(mediaMode_?0:1); SourceActivate();
    }

    void SourceRemove() {
        if (editingSourceIndex_<0) { SetStatus("NEW PATH NOT SAVED"); return; }
        bool saved=false;
        if (mediaMode_) {
            auto next=mediaSources_; next.erase(next.begin()+editingSourceIndex_);
            saved=SaveMediaSources(next); if(saved) mediaSources_=std::move(next);
            if(saved) { StopVideo(); videoSelection_=nullptr; videoTrigger_.Reset();sourceMediaRefreshPending_=MediaRefreshNeeded(); }
        } else {
            auto next=sources_; next.erase(next.begin()+editingSourceIndex_);
            saved=SaveContentSources(next,false); if(saved) sources_=std::move(next);
        }
        if (!saved) { SetStatus("COULD NOT SAVE PATHS",5000); return; }
        sourceEditor_=false; sourceRow_=std::min(sourceRow_,SourceListCount()-1);
        SetStatus("PATH REMOVED - FILES UNCHANGED");
    }

    void SourceScan(bool addOnly=true) {
        if(!mediaMode_){scanAddOnly_=addOnly;scanRequested_=true;return;}
        if(!RequestMediaRefresh()){SetStatus("COULD NOT START MEDIA UPDATE",5000);return;}
        sourceMediaRefreshPending_=true;
        settings_=sourceManager_=sourceEditor_=false;
    }

    bool VideoSourceEditing() const { return mediaMode_ && draftMediaSource_.type == MediaType::Preview; }
    void AutoSelectDraftDefault() {
        if(mediaMode_ && editingSourceIndex_<0 && !defaultFileChosen_)
            draftMediaSource_.defaultFile=FindDefaultMediaFile(draftMediaSource_);
    }
    bool BackgroundVideoEditing() const { return VideoSourceEditing() && draftMediaSource_.video.mode==VideoMode::Background; }
    bool RawVCSortEditing() const {return !mediaMode_&&draftSource_.platform==Platform::WiiU&&draftSource_.libraryId!="install";}
    int SourceEditorCount() const { return mediaMode_ ? (VideoSourceEditing() ? (BackgroundVideoEditing()?12:11) : 8) : RawVCSortEditing()?8:7; }
    bool DefaultFileRow(int row) const { return mediaMode_ && row==SourceEditorCount()-3; }
    bool SourceEditorSelector(int row) const { return row>0 && row<SourceEditorCount()-2 && !DefaultFileRow(row); }
    int SourceEditorFirst() const { return std::max(0,sourceEditorRow_-6); }
    std::string &DraftPath() { return mediaMode_?draftMediaSource_.path:draftSource_.path; }
    void SourceEditorMove(int amount) { const int count=SourceEditorCount(); sourceEditorRow_ = (sourceEditorRow_ + amount + count) % count; }
    void SourceEditorAdjust(int amount) {
        if (!amount) return;
        if (sourceEditorRow_==1) {
            if(mediaMode_) {
                draftMediaSource_.type=static_cast<MediaType>((static_cast<int>(draftMediaSource_.type)+amount+kMediaTypeCount)%kMediaTypeCount);
                if(!MediaFileMatches(draftMediaSource_.type,draftMediaSource_.defaultFile)) draftMediaSource_.defaultFile.clear();
                AutoSelectDraftDefault();
            }
            else if(!libraries_.empty()) {
                auto it=std::find_if(libraries_.begin(),libraries_.end(),[&](const auto&l){return l.id==draftSource_.libraryId;});
                int index=it==libraries_.end()?0:static_cast<int>(it-libraries_.begin());
                index=(index+amount+static_cast<int>(libraries_.size()))%static_cast<int>(libraries_.size());
                draftSource_.libraryId=libraries_[index].id; draftSource_.platform=libraries_[index].parser; draftSource_.launcher=libraries_[index].launcher;
                draftSource_.launcherId.clear();
            }
        } else if(mediaMode_ && sourceEditorRow_==2) {
            int index=-1; for(std::size_t i=0;i<libraries_.size();++i) if(libraries_[i].id==draftMediaSource_.libraryId ||
                (draftMediaSource_.libraryId.empty()&&draftMediaSource_.library>=0&&libraries_[i].id==DefaultLibraryId(static_cast<Platform>(draftMediaSource_.library)))) index=static_cast<int>(i);
            index=(index+1+amount+static_cast<int>(libraries_.size())+1)%(static_cast<int>(libraries_.size())+1)-1;
            draftMediaSource_.library=-1; draftMediaSource_.libraryId=index<0?"":libraries_[index].id;
        }
        else if(sourceEditorRow_==(mediaMode_?3:2)) {
            auto &depth=mediaMode_?draftMediaSource_.depth:draftSource_.depth;
            depth=std::clamp(depth+amount,0,8);
        } else if(sourceEditorRow_==(mediaMode_?4:3)) {
            auto &enabled=mediaMode_?draftMediaSource_.enabled:draftSource_.enabled; enabled=!enabled;
        } else if(RawVCSortEditing() && sourceEditorRow_==5) {
            draftSource_.autoSortVC=!draftSource_.autoSortVC;
        } else if(VideoSourceEditing() && sourceEditorRow_==5) {
            draftMediaSource_.video.delayMs=std::clamp(draftMediaSource_.video.delayMs+amount*250,0,10000);
        } else if(VideoSourceEditing() && sourceEditorRow_==6) {
            draftMediaSource_.video.mode=static_cast<VideoMode>((static_cast<int>(draftMediaSource_.video.mode)+amount%3+3)%3);
        } else if(VideoSourceEditing() && sourceEditorRow_==7) {
            draftMediaSource_.video.volume=std::clamp(draftMediaSource_.video.volume+amount*10,0,100);
        } else if(BackgroundVideoEditing() && sourceEditorRow_==8) {
            draftMediaSource_.video.foregroundOpacity=std::clamp(draftMediaSource_.video.foregroundOpacity+amount*5,0,100);
        } else if(!mediaMode_ && sourceEditorRow_==4) {
            std::vector<std::string> choices={""};
            for(const auto &l:launchers_)if(l.enabled&&LauncherAccepts(l,draftSource_.platform)) {
                bool compatible=draftSource_.platform!=Platform::Files;
                for(const auto &type:libraries_)if(type.id==draftSource_.libraryId) {
                    std::istringstream extensions(type.extensions);std::string ext;
                    while(std::getline(extensions,ext,','))if(LauncherMatchesFile(l,"game"+ext))compatible=true;
                }
                if(compatible)choices.push_back(l.id);
            }
            const auto it=std::find(choices.begin(),choices.end(),draftSource_.launcherId);
            const int pos=it==choices.end()?0:static_cast<int>(it-choices.begin());
            draftSource_.launcherId=choices[(pos+amount+choices.size())%choices.size()];
        }
    }
    void SourceEditorActivate() {
        if (sourceEditorRow_ == 0 || DefaultFileRow(sourceEditorRow_)) {
            defaultFileBrowser_=DefaultFileRow(sourceEditorRow_);
            browserPath_ = DirectoryExists(DraftPath()) ? DraftPath() : "fs:/vol/external01";
            if(defaultFileBrowser_ && !draftMediaSource_.defaultFile.empty()) {
                const auto &file=draftMediaSource_.defaultFile;
                const auto slash=file.find_last_of('/');
                if(slash!=std::string::npos && DirectoryExists(file.substr(0,slash))) browserPath_=file.substr(0,slash);
            }
            if (browserPath_.rfind("fs:/", 0) != 0 && browserPath_.rfind("usb1:/",0)!=0) browserPath_ = "fs:/vol";
            browserPath_=BrowserPath::Normalize(browserPath_);
            RefreshBrowser();
            directoryBrowser_ = true;
        } else if(sourceEditorRow_==SourceEditorCount()-2) SaveDraftSource();
        else if(sourceEditorRow_==SourceEditorCount()-1) SourceRemove();
        else SourceEditorAdjust(1);
    }
    void SaveDraftSource() {
        if (DraftPath().empty() || (editingSourceIndex_<0 && !DirectoryExists(DraftPath()))) {
            SetStatus("SELECT A VALID DIRECTORY", 5000);
            return;
        }
        bool saved=false;
        if (mediaMode_) {
            AutoSelectDraftDefault();
            auto next=mediaSources_;
            if(editingSourceIndex_>=0) next[editingSourceIndex_]=draftMediaSource_; else next.push_back(draftMediaSource_);
            saved=SaveMediaSources(next); if(saved) mediaSources_=std::move(next);
            if(saved) { StopVideo(); videoSelection_=nullptr; videoTrigger_.Reset();sourceMediaRefreshPending_=MediaRefreshNeeded(); }
        } else {
            auto next=sources_;
            bool categoryOnly=false;
            if(editingSourceIndex_>=0){const auto &old=sources_[editingSourceIndex_];
                categoryOnly=old.path==draftSource_.path&&old.platform==draftSource_.platform&&old.enabled==draftSource_.enabled&&old.depth==draftSource_.depth&&old.libraryId==draftSource_.libraryId&&old.launcherId==draftSource_.launcherId&&old.launcher==draftSource_.launcher;}
            if(editingSourceIndex_>=0) next[editingSourceIndex_]=draftSource_; else next.push_back(draftSource_);
            saved=SaveContentSources(next,false); if(saved) sources_=std::move(next);
            if(saved){RebuildVisible();if(categoryOnly){sourceEditor_=false;sourceRow_=0;SetStatus("VC SORT SETTING SAVED");return;}}
        }
        if(!saved) { SetStatus("COULD NOT SAVE PATHS",5000); return; }
        sourceEditor_ = false;
        sourceRow_ = 0;
        if(mediaMode_) FocusMediaSource(editingSourceIndex_>=0?editingSourceIndex_:static_cast<int>(mediaSources_.size())-1);
        SetStatus(mediaMode_?(sourceMediaRefreshPending_?"SAVED - MEDIA UPDATES WHEN SETTINGS CLOSE":"MEDIA SETTINGS SAVED"):"PATH SAVED - USE ADD NEW GAMES OR COMPLETE SCAN");
    }

    void BrowserMove(int amount) {
        if (browserDirectories_.empty()) return;
        const int count = static_cast<int>(browserDirectories_.size());
        browserRow_ = (browserRow_ + amount + count) % count;
    }
    void BrowserOpenSelected() {
        if (browserDirectories_.empty()) return;
        const auto &name=browserDirectories_[browserRow_];
        if(name=="../"){BrowserBack();return;}
        if(name=="usb1:/"){browserPath_=name;RefreshBrowser();return;}
        if(defaultFileBrowser_) {
            if(name=="AUTO (DEFAULT.PNG)"||name=="NONE (NO DEFAULT)") { draftMediaSource_.defaultFile.clear(); defaultFileChosen_=true; BrowserCancel(); return; }
            if(name.back()!='/') {
                draftMediaSource_.defaultFile=BrowserPath::Child(browserPath_,name);
                defaultFileChosen_=true;
                BrowserCancel(); return;
            }
        }
        browserPath_ = BrowserPath::Child(browserPath_,defaultFileBrowser_?name.substr(0,name.size()-1):name);
        RefreshBrowser();
    }
    void BrowserSelectCurrent() {
        if(defaultFileBrowser_) { BrowserOpenSelected(); return; }
        if(BrowserPath::Container(browserPath_)){SetStatus("OPEN A DRIVE OR FOLDER FIRST");return;}
        DraftPath() = browserPath_;
        AutoSelectDraftDefault();
        BrowserCancel();
    }
    void BrowserCancel() {
        if(browserWorker_)browserWorker_->Cancel();
        browserLoading_=false;directoryBrowser_=false;
    }
    void BrowserBack() {
        browserPath_=BrowserPath::Parent(browserPath_);
        RefreshBrowser();
    }

    void DrawIconPreparation(std::size_t done,std::size_t total,float seconds) {
        const bool glass=preferences_.skin==1;
        gSmoothText=glass; gGlassTextHalo=glass; gGlassBackdrop=nullptr; gGlassOverVideo=false;
        const Color accent=AccentFor(Selected()?Selected()->platform:Platform::WiiU,preferences_.theme);
        const Color ink=glass?Color{38,57,72,255}:Color{236,244,255,255};
        const Color muted=glass?Color{78,99,113,255}:Color{154,176,199,255};
        const float phase=preferences_.backgroundMotion?seconds:0.f;
        const float progress=total?std::clamp(float(done)/total,0.f,1.f):1.f;
        for(int y=0;y<kHeight;++y) {
            const float t=float(y)/kHeight;
            SetColor(renderer_,glass?Color{Uint8(237-23*t),Uint8(246-18*t),Uint8(251-14*t),255}:
                Color{Uint8(12+7*t),Uint8(19+9*t),Uint8(33+12*t),255});
            SDL_RenderDrawLine(renderer_,0,y,kWidth,y);
        }
        // Lightweight waves and a traveling ring; no video decoder or artwork queue.
        for(int wave=0;wave<3;++wave) {
            SetColor(renderer_,{accent.r,accent.g,accent.b,Uint8(glass?24:30)});
            int previous=0;
            for(int x=0;x<=kWidth;x+=8) {
                const int y=520+wave*35+int(std::sin(x/260.f+phase*.35f+wave*.8f)*65);
                if(x) SDL_RenderDrawLine(renderer_,x-8,previous,x,y);
                previous=y;
            }
        }
        FillRoundedRect(renderer_,{350,72,580,542},glass?32:8,
                        glass?Color{255,255,255,155}:Color{21,34,53,225});
        // Subpixel geometry with a one-pixel alpha fringe on both edges.
        // Cache the mesh: no per-frame allocation or hundreds of line draw calls.
        constexpr int ringSegments=1024;
        static auto ringVertices=[] {
            std::array<SDL_Vertex,(ringSegments+1)*4> vertices{};
            for(int i=0;i<=ringSegments;++i) {
                const float a=(i%ringSegments)*6.2831853f/ringSegments-1.5707963f;
                for(int edge=0;edge<4;++edge) {
                    constexpr float radii[]={164.f,165.f,167.f,168.f};
                    const float radius=radii[edge];
                    vertices[i*4+edge].position={640.f+std::cos(a)*radius,264.f+std::sin(a)*radius};
                }
            }
            return vertices;
        }();
        static const auto ringIndices=[] {
            std::array<int,ringSegments*18> indices{};
            int n=0;
            for(int i=0;i<ringSegments;++i) for(int edge=0;edge<3;++edge) {
                const int v=i*4+edge;
                for(int index:{v,v+4,v+1,v+1,v+4,v+5})indices[n++]=index;
            }
            return indices;
        }();
        for(int i=0;i<=ringSegments;++i) {
            const float filled=progress<=0.f?0.f:progress>=1.f?1.f:
                std::clamp((progress-float(i)/ringSegments)*6.2831853f*166.f+.5f,0.f,1.f);
            for(int edge=0;edge<4;++edge)
                ringVertices[i*4+edge].color={accent.r,accent.g,accent.b,
                    Uint8(edge==0||edge==3?0:35+155*filled)};
        }
        SDL_RenderGeometry(renderer_,nullptr,ringVertices.data(),ringVertices.size(),
                           ringIndices.data(),ringIndices.size());
        // The marker belongs to the progress endpoint, independent of time or
        // decorative background motion. Keep its center on the ring at subpixel precision.
        const float markerAngle=progress*6.2831853f-1.5707963f;
        const float markerX=640.f+std::cos(markerAngle)*166.f;
        const float markerY=264.f+std::sin(markerAngle)*166.f;
        FillSmoothDot(renderer_,markerX,markerY,10,{accent.r,accent.g,accent.b,45});
        FillSmoothDot(renderer_,markerX,markerY,4,{accent.r,accent.g,accent.b,230});
        if(!preparationLogoTried_) {
            preparationLogoTried_=true;
#ifdef UFLOW_HOST_PREVIEW
            auto *surface=LoadPng("/project/app/content/ui/logo.png");
#else
            auto *surface=LoadPng("/vol/content/ui/logo.png");
#endif
            if(surface) {
                surface=FilterUiImage(surface,310);
                preparationLogoGlow_=UiOutlineGlow(renderer_,surface);
                preparationLogo_=SDL_CreateTextureFromSurface(renderer_,surface); SDL_FreeSurface(surface);
                if(preparationLogo_) { SDL_SetTextureBlendMode(preparationLogo_,SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(preparationLogo_,SDL_ScaleModeLinear); }
            }
        }
        if(preparationLogo_) {
            int w=1,h=1; SDL_QueryTexture(preparationLogo_,nullptr,nullptr,&w,&h);
            const float scale=310.f/std::max(w,h);
            SDL_FRect logo{640.f-w*scale*.5f,264.f-h*scale*.5f,w*scale,h*scale};
            if(preparationLogoGlow_) {
                SDL_FRect glow{logo.x-4*scale,logo.y-4*scale,logo.w+8*scale,logo.h+8*scale};
                SDL_RenderCopyF(renderer_,preparationLogoGlow_,nullptr,&glow);
            }
            SDL_RenderCopyF(renderer_,preparationLogo_,nullptr,&logo);
        } else DrawText(renderer_,"uFlow",640,245,5,ink,true);
        DrawText(renderer_,total?"PREPARING COVERS":"OPENING YOUR LIBRARY",640,456,2,ink,true);
        DrawText(renderer_,total?std::to_string(done)+" / "+std::to_string(total)+"   "+
                 std::to_string(int(progress*100))+"%":"READY TO PLAY",640,492,glass?1:2,muted,true);
        FillRoundedRect(renderer_,{450,526,380,6},3,{accent.r,accent.g,accent.b,40});
        if(progress>0) FillRoundedRect(renderer_,{450,526,std::max(1,int(380*progress)),6},3,accent);
        DrawText(renderer_,total?"GETTING YOUR LIBRARY READY FOR SMOOTH SCROLLING":"YOUR GAMES, READY TO EXPLORE",640,560,glass?1:2,muted,true);
        DrawText(renderer_,total?"B  CONTINUE WHILE COVERS LOAD":"B  SKIP INTRO",640,654,glass?1:2,muted,true);
    }
    static float SmoothFade(float amount) { amount=std::clamp(amount,0.f,1.f);return amount*amount*(3.f-2.f*amount); }
    void FinishStartupIntro() {
        startupIntroPending_=false;video_.Stop();ApplyMusic();
    }
    void DrawStartupIntro() {
        if(!startupIntroPending_)return;
        const auto now=SDL_GetTicks();
        if(!startupIntroRequested_) {
            glassBackground_.Stop();
            if(!glassBackground_.Released())return;
            startupIntroRequested_=true;startupRequestAt_=now;
#ifdef UFLOW_HOST_PREVIEW
            const char *path="/project/app/content/ui/startup.mp4";
#else
            const char *path="fs:/vol/content/ui/startup.mp4";
#endif
            music_.Stop();
            if(!sceneTexture_ || !video_.Start(path,100,false)) {FinishStartupIntro();return;}
        }
        video_.Update();
        if(!video_.TakeError().empty() || (!video_.Ready() && now-startupRequestAt_>3000)) {
            FinishStartupIntro();return;
        }
        if(video_.Ready() && !startupIntroReady_) {startupIntroReady_=true;startupVideoAt_=now;}
        const float elapsed=startupIntroReady_?(now-startupVideoAt_)/1000.f:0.f;
        if(elapsed>=7.2f) {FinishStartupIntro();return;}
        // Fade through the clip's dark background so two different-sized logos
        // never overlap. The second half reveals the selected theme smoothly.
        if(elapsed<6.8f) {SetColor(renderer_,{2,8,13,255});SDL_RenderClear(renderer_);}
        else {
            SetColor(renderer_,{2,8,13,Uint8((1.f-SmoothFade((elapsed-6.8f)/.4f))*255)});
            SDL_RenderFillRect(renderer_,nullptr);return;
        }
        if(!startupIntroReady_)return;
        SDL_Texture *target=SDL_GetRenderTarget(renderer_);
        if(SDL_SetRenderTarget(renderer_,sceneTexture_)!=0) {FinishStartupIntro();return;}
        SetColor(renderer_,{2,8,13,255});SDL_RenderClear(renderer_);video_.Draw(kWidth,kHeight);
        SDL_SetRenderTarget(renderer_,target);
        const float alpha=SmoothFade(elapsed/.35f)*(1.f-SmoothFade((elapsed-6.4f)/.4f));
        SDL_SetTextureBlendMode(sceneTexture_,SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(sceneTexture_,Uint8(alpha*255));
        SDL_RenderCopy(renderer_,sceneTexture_,nullptr,nullptr);
        SDL_SetTextureAlphaMod(sceneTexture_,255);
        SDL_SetTextureBlendMode(sceneTexture_,SDL_BLENDMODE_NONE);
    }
    std::string LibraryPosition() const {
        return std::to_string(visible_.empty()?0:selected_+1)+"/"+std::to_string(visible_.size());
    }
    unsigned LaunchAnimationDuration() const {
        return preferences_.animationSpeed==0?1050:preferences_.animationSpeed==1?850:650;
    }
#ifdef UFLOW_HOST_PREVIEW
    bool CheckLaunchAnimation(const std::string &path) {
        for(int skin:{0,1})for(int mode:{0,1,2,3}) {
            if(!VisualPreview("library"))return false;
            preferences_.skin=skin;preferences_.useBoxArt=mode!=3;
            details_=mode==1;detailsProgress_=details_?1.f:0.f;
            if(!Render(0))return false;
            if(mode==2) {
                showcaseProgress_=1.f;
                DrawShowcase(*Selected(),AccentFor(Selected()->platform,preferences_.theme));
            }
            const auto origin=launchOrigin_;
            const auto index=selected_;
            auto *scene=sceneTexture_;auto *lens=lensTexture_;
            SDL_BlendMode blend;SDL_GetTextureBlendMode(scene,&blend);
            if(!BeginLaunchAnimation())return false;
            if(launchOrigin_.x!=origin.x || launchOrigin_.y!=origin.y || launchOrigin_.w!=origin.w)return false;
            const std::string prefix=path+"-"+std::to_string(skin)+"-"+std::to_string(mode);
            int frame=0;
            for(float progress:{0.f,.2f,.45f,.7f,1.f})
                if(!DrawLaunchAnimation(progress,prefix+"-"+std::to_string(frame++)+".png"))return false;
            SDL_BlendMode restored;SDL_GetTextureBlendMode(scene,&restored);
            Uint8 alpha,r,g,b;SDL_GetTextureAlphaMod(scene,&alpha);SDL_GetTextureColorMod(scene,&r,&g,&b);
            if(sceneTexture_!=scene || lensTexture_!=lens || selected_!=index || blend!=restored ||
               alpha!=255 || r!=255 || g!=255 || b!=255)return false;
            EndLaunchAnimation();
            if(launchAnimation_ || !Render(0,prefix+"-restored.png"))return false;
            // Cancellation restores a normal scene without an outgoing title.
            if(!BeginLaunchAnimation() || !DrawLaunchAnimation(.35f))return false;
            EndLaunchAnimation();
            if(!Render(0))return false;
        }
        VisualPreview("library");preferences_.skin=1;
        const auto sample=games_.front();games_.assign(1349,sample);RebuildVisible();
        selected_=31;visualSelected_=31.f;
        if(LibraryPosition()!="32/1349" || !Render(0,path+"-counter.png"))return false;
        games_.clear();RebuildVisible();
        if(LibraryPosition()!="0/0" || BeginLaunchAnimation())return false;
        puts("PASS: launch spin/zoom/fade from library, Details, Showcase and flat covers in both skins; reusable targets, cancellation, renderer state, header position and empty library");
        return true;
    }
#endif
    bool BeginLaunchAnimation() {
        const auto *game=Selected();
        if(!game || !sceneTexture_ || !lensTexture_)return false;
        auto *target=SDL_GetRenderTarget(renderer_);
        if(SDL_SetRenderTarget(renderer_,lensTexture_)!=0)return false;
        // Freeze the surrounding scene once, without a readback or allocation.
        // Existing case drawing records the selected case's on-screen pose.
        launchCapture_=true;gGlassBackdrop=nullptr;
        const auto accent=AccentFor(game->platform,preferences_.theme);
        DrawBackground(accent,game);
        const bool fullVideo=video_.Ready() && videoOptions_.mode!=VideoMode::Background;
        if(video_.Ready())video_.Draw(kWidth,kHeight);
        if(!fullVideo) {DrawCarousel(accent);DrawFooter(accent);DrawHeader(accent);}
        if(detailsProgress_>.01f)DrawDetails(accent,detailsProgress_);
        if(showcaseProgress_>0.f)DrawShowcase(*game,accent);
        launchCapture_=false;
        SDL_SetRenderTarget(renderer_,target);
        launchAnimation_=true;
        MenuSound(MenuSounds::Confirm);
        return true;
    }
    void EndLaunchAnimation() {
        launchAnimation_=false;detailsBackdropReady_=false;
    }
    bool DrawLaunchAnimation(float progress,const std::string &capturePath={}) {
        const auto *game=Selected();
        if(!launchAnimation_ || !game)return false;
        const float p=std::clamp(progress,0.f,1.f),ease=SmoothFade(p);
        const auto accent=AccentFor(game->platform,preferences_.theme);
        SDL_SetRenderTarget(renderer_,nullptr);
        SetColor(renderer_,{0,0,0,255});SDL_RenderClear(renderer_);
        SDL_SetTextureAlphaMod(lensTexture_,Uint8(255*(1.f-ease)));
        SDL_RenderCopy(renderer_,lensTexture_,nullptr,nullptr);
        SDL_SetTextureAlphaMod(lensTexture_,255);
        const float opacity=1.f-SmoothFade((p-.22f)/.78f);
        if(opacity>0.f && SDL_SetRenderTarget(renderer_,sceneTexture_)==0) {
            SetColor(renderer_,{0,0,0,0});SDL_RenderClear(renderer_);
            const float scale=1.f+3.2f*p*p;
            const float cx=launchOrigin_.x+launchOrigin_.w*.5f;
            const float cy=launchOrigin_.y+launchOrigin_.h*.5f;
            const int w=static_cast<int>(launchOrigin_.w*scale),h=static_cast<int>(launchOrigin_.h*scale);
            const SDL_Rect rect{static_cast<int>(cx+(640-cx)*ease-w*.5f),
                static_cast<int>(cy+(340-cy)*ease-h*.5f),w,h};
            DrawBoxDirect(*game,rect,0.f,true,accent,
                launchYaw_+6.2831853f*ease,launchPitch_*(1-ease)-.12f*std::sin(p*3.14159265f),
                launchRoll_*(1-ease),CaseView::kCamera,CaseProfileForGame(*game,libraries_));
            SDL_SetRenderTarget(renderer_,nullptr);
            SDL_BlendMode original;SDL_GetTextureBlendMode(sceneTexture_,&original);
            const auto blend=SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
            const bool premultiplied=SDL_SetTextureBlendMode(sceneTexture_,blend)==0;
            if(!premultiplied)SDL_SetTextureBlendMode(sceneTexture_,SDL_BLENDMODE_BLEND);
            const Uint8 alpha=static_cast<Uint8>(255*opacity);
            SDL_SetTextureAlphaMod(sceneTexture_,alpha);
            if(premultiplied)SDL_SetTextureColorMod(sceneTexture_,alpha,alpha,alpha);
            SDL_RenderCopy(renderer_,sceneTexture_,nullptr,nullptr);
            SDL_SetTextureColorMod(sceneTexture_,255,255,255);SDL_SetTextureAlphaMod(sceneTexture_,255);
            SDL_SetTextureBlendMode(sceneTexture_,original);
        }
        const bool captured=capturePath.empty() || SaveVisualFrame(renderer_,capturePath);
        SDL_RenderPresent(renderer_);
#ifndef UFLOW_HOST_PREVIEW
        GX2WaitForFlip();
#endif
        return captured;
    }
    bool Render(float deltaSeconds, const std::string &capturePath = {}) {
        UpdateGridCamera(deltaSeconds);
        const Uint64 performanceStart=SDL_GetPerformanceCounter();
        // Also advance cache retirement while full video bypasses the shelf.
        // Otherwise all cards from its last draw stay marked in-use forever.
        sideCaseFrame_=sideCaseUse_;
        textures_.BeginFrame();
        PumpBrowser();
        PollScan();
        PumpTitleInstall();
        UpdateDemo();
        if (PreparingIcons() && !scanOpen_) {
            // A cheap preparation frame lets decoding run without competing with
            // animated case rendering. Events/HOME still pump in the normal loop.
            if(!startupFrame_) {
                startupFrame_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,kWidth,kHeight);
                if(startupFrame_)SDL_SetTextureBlendMode(startupFrame_,SDL_BLENDMODE_BLEND);
            }
            if(SDL_SetRenderTarget(renderer_,startupFrame_)!=0) {
                SDL_DestroyTexture(startupFrame_);startupFrame_=nullptr;SDL_SetRenderTarget(renderer_,nullptr);
            }
            DrawIconPreparation(textures_.IconsDone(),textures_.IconsTotal(),SDL_GetTicks()/1000.f);
            DrawStartupIntro();
            SDL_SetRenderTarget(renderer_,nullptr);
            if(startupFrame_) {SDL_SetTextureAlphaMod(startupFrame_,255);SDL_RenderCopy(renderer_,startupFrame_,nullptr,nullptr);}
            startupFadeAt_=0;
            const bool captured=capturePath.empty() || SaveVisualFrame(renderer_,capturePath);
            SDL_RenderPresent(renderer_);
#ifndef UFLOW_HOST_PREVIEW
            GX2WaitForFlip();
#endif
            textures_.SetCoverDetail(false);
            PrefetchArtwork();
            textures_.PumpOne();
            return captured;
        }
        if (preview_
#ifdef UFLOW_HOST_PREVIEW
            && !videoTest_
#endif
        ) deltaSeconds = 0; // deterministic transition samples, not performance measurements
        SetSwitchFont(SwitchSkin());
        gSmoothText = preferences_.skin != 0;
        gGlassTextHalo = preferences_.skin == 1;
        gGlassOverVideo=false;
        UpdateControlHints();
        const float response = preferences_.animationSpeed == 0 ? 6.5f : preferences_.animationSpeed == 1 ? 11.0f : 17.0f;
        const float step = 1.0f - std::exp(-std::max(0.f,deltaSeconds) * response);
        const bool bookshelf = preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8;
        if(bookshelf)visualSelected_=bookshelfMotion_.Advance(visualSelected_,float(selected_),deltaSeconds,preferences_.animationSpeed);
        else {bookshelfMotion_={};visualSelected_ += (static_cast<float>(selected_) - visualSelected_) * step;}
        visualTab_ += (static_cast<float>(pendingTab_>=0?pendingTab_:tab_) - visualTab_) * step;
        AdvanceLibraryFade(deltaSeconds);
        alphabetProgress_=AdvanceCoverTransition(alphabetProgress_,alphabetOpen_,deltaSeconds,.16f);
        alphabetVisual_+=(static_cast<float>(alphabetChoice_)-alphabetVisual_)*step;
        tabSlide_ += (0.0f - tabSlide_) * step;
        const float duration = CoverTransitionDuration(preferences_.animationSpeed);
        detailsProgress_ = AdvanceCoverTransition(detailsProgress_, details_, deltaSeconds, duration);
        const float inspectDuration = CoverTransitionDuration(preferences_.animationSpeed) * .82f;
        // Release reverses zoom and rotation together, so the back cover
        // rotates toward the front while it retreats into the carousel.
        coverInspectProgress_ = AdvanceCoverTransition(coverInspectProgress_,coverInspectOpen_,deltaSeconds,inspectDuration);
        coverInspectBackProgress_ = AdvanceCoverTransition(coverInspectBackProgress_,coverInspectBack_,deltaSeconds,inspectDuration);
        if(!coverInspectOpen_ && coverInspectProgress_ == 0.f) {
            coverInspectBackProgress_=0.f;
        }
        settingsProgress_ += ((settings_ ? 1.0f : 0.0f) - settingsProgress_) * step;
        const GameEntry *selected = Selected();
        textures_.SetCoverDetail(CoverInspectOpen() || details_ || detailsProgress_>.01f);
        textures_.PinCover(selected?(selected->fullCoverPath.empty()?selected->coverPath:selected->fullCoverPath):std::string());
        if(visible_.size()>1)for(int offset:{-1,1,-2,2,-3,3}) {
            int index=static_cast<int>(selected_)+offset,count=static_cast<int>(visible_.size());
            if(preferences_.wrapNavigation)index=(index+count)%count;
            if(index>=0 && index<count) {
                const auto &nearby=games_[visible_[index]];
                textures_.PinNearbyCover(nearby.fullCoverPath.empty()?nearby.coverPath:nearby.fullCoverPath);
            }
        }
        // Put the focused cover ahead of backgrounds and neighbor requests,
        // including while a video or Details overlay owns the screen.
        if(selected) {
            textures_.PrefetchCover(selected->fullCoverPath,true);
            if(selected->fullCoverPath.empty() ||
               (textures_.CoverDetailKnown(selected->fullCoverPath,true) && !textures_.HasCover(selected->fullCoverPath,true)))
                textures_.PrefetchCover(selected->coverPath,true);
        }
        const bool obscuredByVideo=video_.Ready() && (videoOptions_.mode==VideoMode::Foreground ||
            (videoOptions_.mode==VideoMode::Showcase && showcaseProgress_>=.55f));
        if(!OverlayOpen() && !obscuredByVideo)PrefetchArtwork();
        const std::string backgroundPath=preferences_.useBackgrounds && selected?selected->backgroundPath:std::string();
        textures_.PinBackground(backgroundPath);
        const auto background=textures_.GetBackground(backgroundPath);
        // A background arriving after Y was pressed must refresh the cached
        // scene too. Keep requesting it during overlays, and pin just this title.
        if(backgroundPath!=selectedBackgroundPath_ || background.texture!=selectedBackground_.texture)
            detailsBackdropReady_=false;
        selectedBackgroundPath_=backgroundPath;selectedBackground_=background;
        UpdateVideo(selected);
        UpdateDetailsVideo();
        const bool showcase=video_.Active() && video_.Ready() && videoOptions_.mode==VideoMode::Showcase;
        showcaseProgress_=AdvanceCoverTransition(showcaseProgress_,showcase,deltaSeconds,
            CoverTransitionDuration(preferences_.animationSpeed)*1.35f);
        // A menu owns its own cover animation. Never draw two moving cases.
        if(OverlayOpen() || CoverInspectOpen()) showcaseProgress_=0.f;
        if(showcaseProgress_==0.f && !video_.Active() && video_.Ready())video_.Stop();
        glassBackground_.Update(preferences_.skin==1,
            !preferences_.backgroundMotion||scanOpen_||updateOpen_||WelcomeOpen()||InjectionDialogOpen()||settings_||details_||
            detailsProgress_>.01f||settingsProgress_>.01f,
            videoPending_||video_.Active()||detailsVideoPending_||detailsVideo_.Active()||!detailsVideo_.CanStart(),
            !video_.Active()&&video_.CanStart()&&detailsVideo_.CanStart(),
            scanOpen_||updateOpen_||WelcomeOpen()||InjectionDialogOpen()||settings_||details_||
            detailsProgress_>.01f||settingsProgress_>.01f);
        if(videoIsolationUntil_ && (SDL_TICKS_PASSED(SDL_GetTicks(),videoIsolationUntil_) || !video_.Active() || OverlayOpen()))
            videoIsolationUntil_=0;
        if(videoIsolationUntil_ && video_.Ready()) {
            SDL_SetRenderTarget(renderer_,nullptr);
            SetColor(renderer_,{0,0,0,255});SDL_RenderClear(renderer_);
            video_.Draw(kWidth,kHeight);
            SDL_RenderPresent(renderer_);
#ifndef UFLOW_HOST_PREVIEW
            GX2WaitForFlip();
#endif
            return true;
        }
        Color accent = selected ? AccentFor(selected->platform, preferences_.theme)
                                : AccentFor(Platform::WiiU, preferences_.theme);

        const float overlayProgress = std::max({detailsProgress_, settingsProgress_,(scanOpen_||InjectionDialogOpen()||WelcomeOpen()||updateOpen_)?1.f:0.f});
        const bool stickyBackground=selectedBackground_.texture && preferences_.backgroundTransparency<100 &&
            detailsProgress_>.01f && settingsProgress_<=.01f && !scanOpen_ && !InjectionDialogOpen() && !WelcomeOpen() && !updateOpen_;
        const bool fullVideo = video_.Ready() && (videoOptions_.mode==VideoMode::Foreground || (videoOptions_.mode==VideoMode::Showcase && showcaseProgress_>=.55f));
        if (overlayProgress <= .01f && !fullVideo) PrefetchArtwork();
        // Overlay content changes every frame, but the obscured library does not.
        // Freeze it after the first blurred frame for both skins; settings changes
        // explicitly invalidate this cache before their next render.
        const bool freezeBackdrop = overlayProgress > .01f;
        const bool reuseBackdrop = freezeBackdrop && detailsBackdropReady_;
        const bool offscreen = !fullVideo && (reuseBackdrop || (sceneTexture_ && SDL_SetRenderTarget(renderer_, sceneTexture_) == 0));
        if (!reuseBackdrop && !fullVideo) {
            const bool backgroundVideo=videoOptions_.mode==VideoMode::Background && video_.Draw(kWidth,kHeight);
            if (!backgroundVideo) DrawBackground(accent, selected);
            // Reuse the existing lens scratch as an immutable video-only layer.
            // It also supplies live scene samples to the glass, never its own target.
            const bool videoLayer=backgroundVideo && offscreen && overlayProgress<=.01f && lensTexture_ &&
                SDL_SetRenderTarget(renderer_,lensTexture_)==0;
            if(videoLayer) {
                SDL_SetTextureAlphaMod(lensTexture_,255);
                SDL_RenderCopy(renderer_,sceneTexture_,nullptr,nullptr);
                SDL_SetRenderTarget(renderer_,sceneTexture_);
                gGlassBackdrop=preferences_.skin==1?lensTexture_:nullptr;
                gGlassOverVideo=preferences_.skin==1;
            }
            // Reuse the full-size lens scratch while no overlay owns it. Capture
            // background only, then blend it over covers: genuine opacity fading
            // without a colored veil, extra allocations or GPU readback.
            const bool fadeLayer=!videoLayer && libraryFadePhase_ && offscreen && overlayProgress<=.01f && lensTexture_ &&
                SDL_SetRenderTarget(renderer_,lensTexture_)==0;
            if(fadeLayer) {
                SDL_SetTextureAlphaMod(lensTexture_,255);
                SDL_RenderCopy(renderer_,sceneTexture_,nullptr,nullptr);
                SDL_SetRenderTarget(renderer_,sceneTexture_);
            }
            if(!fadeLayer || libraryFade_>0.f) {
                if (visible_.empty()) DrawEmpty(accent); else DrawCarousel(accent);
            }
            if(fadeLayer) {
                const float opacity=libraryFade_*libraryFade_*(3.f-2.f*libraryFade_);
                SDL_SetTextureAlphaMod(lensTexture_,static_cast<Uint8>((1.f-opacity)*255.f));
                SDL_RenderCopy(renderer_,lensTexture_,nullptr,nullptr);
                SDL_SetTextureAlphaMod(lensTexture_,255);
            }
            if(!settings_ && settingsProgress_<=.01f)DrawFooter(accent);
            if(coverInspectProgress_>.01f) DrawCoverInspect(accent);
            DrawHeader(accent);
            if(!SwitchSkin()){const auto &skin=SkinSpec();DrawProfile(skin.avatarX,skin.avatarY,skin.avatarSize,false);DrawFittedText(renderer_,profileName_,skin.avatarX-160,skin.avatarY+14,145,1,preferences_.skin==1?Color{48,61,71,255}:Color{237,244,255,255},true);}
            gGlassBackdrop=nullptr;
            gGlassOverVideo=false;
            if(videoLayer && videoOptions_.foregroundOpacity<100) {
                SDL_SetTextureAlphaMod(lensTexture_,static_cast<Uint8>((100-std::clamp(videoOptions_.foregroundOpacity,0,100))*255/100));
                SDL_RenderCopy(renderer_,lensTexture_,nullptr,nullptr);
                SDL_SetTextureAlphaMod(lensTexture_,255);
            }
        }
        bool blurReady = reuseBackdrop;
        if (offscreen) {
            SDL_SetRenderTarget(renderer_, nullptr);
            SDL_RenderCopy(renderer_, sceneTexture_, nullptr, nullptr);
            if (!reuseBackdrop && overlayProgress > .01f && blurTexture_ &&
                SDL_SetRenderTarget(renderer_, blurTexture_) == 0) {
                SetColor(renderer_, {0,0,0,255});
                SDL_RenderClear(renderer_);
                SDL_RenderCopy(renderer_, sceneTexture_, nullptr, nullptr);
                blurReady = BlurBackdropToLens();
                SDL_SetRenderTarget(renderer_, nullptr);
            }
            if (overlayProgress > .01f && blurReady && !stickyBackground) {
                SDL_SetTextureAlphaMod(blurTexture_, static_cast<Uint8>(245.0f * overlayProgress));
                SDL_RenderCopy(renderer_, blurTexture_, nullptr, nullptr);
                SDL_SetTextureAlphaMod(blurTexture_, 255);
                SetColor(renderer_, {207,225,234,static_cast<Uint8>(34.0f*overlayProgress)});
                SDL_RenderFillRect(renderer_, nullptr);
            }
        } else if (overlayProgress > .01f && !stickyBackground) {
            SetColor(renderer_, {203,220,230,static_cast<Uint8>(58.0f*overlayProgress)});
            SDL_RenderFillRect(renderer_, nullptr);
        }
        detailsBackdropReady_ = freezeBackdrop && offscreen && blurReady;
        gGlassBackdrop = offscreen && blurTexture_ && overlayProgress > .01f ? lensTexture_ : nullptr;
        if (detailsProgress_ > .01f) DrawDetails(accent, detailsProgress_);
        if (settingsProgress_ > .01f) DrawSettings(accent, settingsProgress_);
        if(sortOpen_)DrawSort(accent);
        if(searchOpen_)DrawSearch(accent);
        if(alphabetProgress_>.001f)DrawAlphabet(accent);
        if (InjectionDialogOpen()) DrawInjectionDialog(accent);
        if (WelcomeOpen()) DrawWelcome(accent);
        if (updateOpen_) DrawUpdate(accent);
        if (launchOptionsOpen_) DrawLaunchOptions(accent);
        if (scanOpen_) {
            if(scanFirstFrame_)Trace("scan-first-dialog-draw");
            DrawScan(accent);
            if(scanFirstFrame_)Trace("scan-first-dialog-drawn");
        }
        gGlassBackdrop = nullptr;
        if (videoOptions_.mode!=VideoMode::Background)
            video_.Draw(kWidth,kHeight,videoOptions_.mode==VideoMode::Showcase?
                static_cast<Uint8>(SmoothFade(showcaseProgress_/.55f)*255.f):255);
        if(showcaseProgress_>0.f && selected) {
            // Sample the current video at low resolution, without a readback or
            // another full-size texture. Only the Glass Flow panel uses the blur.
            if(preferences_.skin==1 && showcaseProgress_>.55f && blurTexture_ && lensTexture_) {
                const auto frame=video_.Statistics().presented;
                // An unchanged video image needs no second YUV draw or blur.
                // During transitions other overlays share this scratch, so
                // reuse only after the showcase has fully settled.
                if(!showcaseLensReady_ || showcaseProgress_!=1.f || frame!=showcaseLensFrame_) {
                    showcaseLensReady_=false;
                    if(SDL_SetRenderTarget(renderer_,blurTexture_)==0) {
                        SetColor(renderer_,{0,0,0,255});SDL_RenderClear(renderer_);
                        showcaseLensReady_=video_.Draw(192,108) && BlurBackdropToLens();
                        showcaseLensFrame_=frame;
                        SDL_SetRenderTarget(renderer_,nullptr);
                    }
                }
                if(showcaseLensReady_)gGlassBackdrop=lensTexture_;
            } else {
                showcaseLensReady_=false;
            }
            DrawShowcase(*selected,accent);
            gGlassBackdrop=nullptr;
        } else showcaseLensReady_=false;
        if (!status_.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), statusUntil_)) status_.clear();
        if (!status_.empty()&&!scanOpen_&&!InjectionDialogOpen()&&!WelcomeOpen()&&!updateOpen_&&!launchOptionsOpen_) DrawToast(accent);
        if(startupFrame_) {
            if(!startupFadeAt_)startupFadeAt_=SDL_GetTicks();
            const float fade=SmoothFade((SDL_GetTicks()-startupFadeAt_)/650.f);
            SDL_SetTextureAlphaMod(startupFrame_,Uint8((1.f-fade)*255));
            SDL_RenderCopy(renderer_,startupFrame_,nullptr,nullptr);
            if(fade>=1.f) {SDL_DestroyTexture(startupFrame_);startupFrame_=nullptr;}
        }
        DrawGallery();DrawPowerPrompt();
        if(draftExit_!=DraftExit::None)DrawDraftExit(accent);
        const bool captured = capturePath.empty() || SaveVisualFrame(renderer_, capturePath);
        if(scanOpen_ && scanFirstFrame_)Trace("scan-first-present");
        SDL_RenderPresent(renderer_);
        if(scanOpen_ && scanFirstFrame_){Trace("scan-first-presented");scanFirstFrame_=false;}
#ifndef UFLOW_HOST_PREVIEW
        // SDL waits for older GPU work, not necessarily the queued display flip.
        // Pace against the submitted flip rather than queueing another scan-buffer
        // copy while the display is still presenting it. Only rendered in foreground.
        GX2WaitForFlip();
#endif
        FlushSettingsSave(false);
        // Only upload a completed CPU decode here; storage and decompression
        // run on the artwork worker. Include upload time in frame statistics.
        if (!fullVideo&&!scanOpen_&&!InjectionDialogOpen()) textures_.PumpOne(!video_.Active() && !OverlayOpen());
        const double elapsedMs=1000.0*(SDL_GetPerformanceCounter()-performanceStart)/SDL_GetPerformanceFrequency();
        ++performanceFrames_; performanceTotalMs_+=elapsedMs;
        performanceMaxMs_=std::max(performanceMaxMs_,elapsedMs);
        return captured;
    }

private:
    void ToggleDetailsVideo(){
        if(detailsVideo_.Active()||detailsVideoPending_){StopDetailsVideo();detailsVideoMessage_.clear();return;}
        const auto *game=Selected();if(!game||game->previewPath.empty()){detailsVideoMessage_="No video assigned.";return;}
        detailsVideoMessage_="Loading video...";detailsVideoPending_=true;
        StopVideo(); // Release the library decoder before this player starts.
        detailsVideoPending_=true;
    }
    void StopDetailsVideo(){
        const bool playing=detailsVideo_.Active();detailsVideoPending_=false;
        detailsVideo_.Stop();if(playing)music_.Pause(false);
    }
    void UpdateDetailsVideo(){
        if(!details_||detailsRow_!=4||((detailsPage_%8)+8)%8!=0||settings_||InstallBusy()){
            StopDetailsVideo();detailsVideoMessage_.clear();return;
        }
        if(detailsVideoPending_){
            glassBackground_.Stop(true);
            if(video_.CanStart()&&detailsVideo_.CanStart()&&glassBackground_.Released()){
                detailsVideoPending_=false;music_.Pause(true);
                if(!detailsVideo_.Start(Selected()->previewPath,videoOptions_.volume)){
                    detailsVideoMessage_="Video could not start.";music_.Pause(false);
                }
            }
        }
        if(detailsVideo_.Active())detailsVideo_.Update();
        if(detailsVideo_.Ready())detailsVideoMessage_.clear();
        if(const auto error=detailsVideo_.TakeError();!error.empty()){detailsVideoMessage_=error;StopDetailsVideo();}
    }
    void UpdateVideo(const GameEntry *selected) {
        if(InstallBusy())return;
        if (selected != videoSelection_) {
            showcaseProgress_=0.f;
            StopVideo(); videoSelection_ = selected;
            videoTrigger_.Focus(selected ? selected->absolutePath + "|" + selected->previewPath : "");
            videoTrigger_.Reset();
            videoOptions_ = {};
            videoConfigured_ = false;
            if (selected) if (const auto *source = VideoSourceFor(mediaSources_,*selected)) {
                videoOptions_ = source->video; videoConfigured_ = true;
            }
            if(demo_.Active() && selected && !selected->previewPath.empty())videoConfigured_=true;
        }
        bool realLibrary = !preview_;
#ifdef UFLOW_HOST_PREVIEW
        realLibrary = realLibrary || videoTest_;
#endif
        const bool decoderAvailable = video_.Active() || video_.CanStart();
        // A released direction is not a settled cover. Use the demo-mode gate
        // for manual browsing too, so decoder startup does not race the scroll.
        const bool eligible = decoderAvailable && !detailsVideo_.Active() && detailsVideo_.CanStart() && realLibrary && !videoInputActive_ && selected && videoConfigured_ && !selected->previewPath.empty() &&
            (preferences_.usePreviews || demo_.Active()) && std::abs(visualSelected_-float(selected_))<.03f && !alphabetOpen_ && alphabetProgress_<=.01f && !searchOpen_ && !sortOpen_ && !scanOpen_ && !updateOpen_ && !WelcomeOpen() && !InjectionDialogOpen() && !settings_ && !details_ && !sourceManager_ && !sourceEditor_ &&
            !directoryBrowser_ && detailsProgress_<=.01f && settingsProgress_<=.01f &&
            (!CoverInspectOpen() || videoOptions_.mode==VideoMode::Background) &&
            !libraryFadePhase_;
        if (!eligible && (video_.Active()||videoPending_)) StopVideo();
        if (videoTrigger_.Tick(eligible,SDL_GetTicks(),demo_.Active()?0:videoOptions_.delayMs+preferences_.previewDelaySeconds*1000)) {
            videoPending_=true;
        }
        if(videoPending_)glassBackground_.Stop(true);
        if(videoPending_&&glassBackground_.Released()&&video_.CanStart()) {
            videoPending_=false;
            Trace(("video-start path="+selected->previewPath).c_str());
            music_.Pause(true);
            Trace("video-music-paused");
            videoReportAt_=SDL_GetTicks(); videoReportStats_={}; videoReports_=0;
            if (!video_.Start(selected->previewPath,videoOptions_.volume,!demo_.Active())) {
                music_.Pause(false); SetStatus("VIDEO PLAYER COULD NOT START",6000);
                if(demo_.Active()){demoFailed_.insert(selected->previewPath);demoNext_=true;}
            }
        }
        if (!video_.Active()) return;
        video_.Update();
        ReportVideo();
        if (const auto error = video_.TakeError(); !error.empty()) {
            Trace(("video-error " + error).c_str());
            videoTrigger_.Dismiss();
            StopVideo(); SetStatus(error,6000);
            if(demo_.Active()){demoFailed_.insert(selected->previewPath);demoNext_=true;}
        }
        else if(demo_.Active() && video_.Finished()){demoNext_=true;StopVideo();}
    }
    void UpdateDemo() {
        bool realLibrary=!preview_;
#ifdef UFLOW_HOST_PREVIEW
        realLibrary=realLibrary || demoTest_;
#endif
        const auto now=SDL_GetTicks();
        if(!demoSeeded_){demo_.Seed(std::uint32_t(SDL_GetPerformanceCounter()));demoSeeded_=true;}
        const bool eligible=realLibrary && !OverlayOpen() && !PreparingIcons() && !InstallBusy() &&
            !startupIntroPending_ && !libraryFadePhase_ && settingsProgress_<=.01f && detailsProgress_<=.01f;
        const bool wasActive=demo_.Active();
        const bool begin=demo_.Tick(preferences_.demoMode,eligible,now,preferences_.demoIdleSeconds);
        if(!demo_.Active()){if(wasActive)RestoreDemoSelection();return;}
        if(!begin && !demoNext_) {
            if(!video_.Ready() && now-demoSelectedAt_>30000) {
                if(Selected())demoFailed_.insert(Selected()->previewPath);
                demoNext_=true;
            } else return;
        }
        if(begin) {
            demoFailed_.clear();
            demoOriginPath_=Selected()?Selected()->absolutePath:std::string{};
        }
        std::vector<std::size_t> candidates;
        for(std::size_t i=0;i<visible_.size();++i) {
            const auto &path=games_[visible_[i]].previewPath;
            if(!path.empty() && !demoFailed_.count(path))candidates.push_back(i);
        }
        const auto next=demo_.Choose(candidates,selected_);
        if(next==DemoMode::None){DemoInput(true);return;}
        StopVideo();videoSelection_=nullptr;videoTrigger_.Focus("");
        selected_=next;detailsBackdropReady_=false;demoNext_=false;demoSelectedAt_=now;
        PrefetchArtwork();
    }
    void QueueSettingsSave() {
        if (preview_) return;
        settingsDirty_ = true;
        settingsSaveAfter_ = SDL_GetTicks() + 750;
    }
    void FlushSettingsSave(bool force) {
        if (!settingsDirty_ || (!force && !SDL_TICKS_PASSED(SDL_GetTicks(), settingsSaveAfter_))) return;
        SaveSettings(preferences_);
        settingsDirty_ = false;
    }
    void ClearSideCases() {
        bookshelfCached_=false;
        for (auto &entry : sideCases_) SDL_DestroyTexture(entry.texture);
        sideCases_.clear();
        sideCaseBytes_ = 0;
    }
    int SettingsFirstRow() const {
        if(settingsCategory_==4)return std::clamp(settingsRow_-11,0,std::max(0,static_cast<int>(ControlAction::Count)+1-12));
        if(settingsCategory_==0)return std::clamp(settingsRow_-5,0,std::max(0,SettingsRowCount()-6));
        return settingsCategory_==3?std::clamp(settingsRow_-5,0,12):0;
    }
    int SettingsRowCount() const {
        if(audioSettings_) return 6;
        if(settingsCategory_==0)return preferences_.demoMode?10:9;
        if(settingsCategory_==4)return static_cast<int>(ControlAction::Count)+1;
        static constexpr int counts[] = {8, 0, 0, 18, 0, 0};
        return counts[settingsCategory_];
    }
    void RefreshBrowser() {
        browserError_.clear();
#ifdef UFLOW_HOST_PREVIEW
        // Existing visual fixtures are deterministic; CheckAsyncBrowser exercises
        // the production asynchronous route explicitly.
        if(!browserWorker_){
            browserDirectories_=defaultFileBrowser_?ListMediaFiles(browserPath_,draftMediaSource_.type):ListDirectories(browserPath_);
            AddBrowserActions();browserRow_=0;return;
        }
#endif
        if(!browserWorker_)browserWorker_=std::make_unique<BrowserWorker>();
        browserDirectories_.clear();AddBrowserActions();browserRow_=0;
        browserLoading_=browserWorker_->Available();
        if(browserLoading_)browserWorker_->Start({browserPath_,defaultFileBrowser_,draftMediaSource_.type});
        else browserError_="FOLDER READER UNAVAILABLE";
    }
    void AddBrowserActions() {
        if(defaultFileBrowser_) browserDirectories_.insert(browserDirectories_.begin(),IsCoverMedia(draftMediaSource_.type)?"AUTO (DEFAULT.PNG)":"NONE (NO DEFAULT)");
        if(BrowserPath::Normalize(browserPath_)!="fs:/")browserDirectories_.insert(browserDirectories_.begin(),"../");
    }
    void PumpBrowser() {
        if(!directoryBrowser_ || !browserLoading_ || !browserWorker_)return;
        auto result=browserWorker_->Take();if(!result.available)return;
        browserLoading_=false;browserError_=std::move(result.error);
        browserDirectories_=std::move(result.entries);AddBrowserActions();
        browserRow_=std::clamp(browserRow_,0,std::max(0,static_cast<int>(browserDirectories_.size())-1));
    }
    void RebuildVisible() {
        ++bookshelfEpoch_;bookshelfCached_=false;bookshelfMotion_={};
        ApplyVCCategories(games_,preferences_,sources_);
        mediaRefreshPending_=std::any_of(games_.begin(),games_.end(),[&](const auto &g){return DesiredMediaLibrary(g,libraries_)!=(g.mediaLibraryId.empty()?g.libraryId:g.mediaLibraryId);});
        detailsBackdropReady_ = false;
        visible_.clear();
        const auto oldLibrary=CurrentLibraryId();
        auto tabs=AvailableLibraryTabs(games_,libraries_,preferences_);
        if(tabs!=libraryTabs_) {
            libraryTabs_=std::move(tabs);
            const auto found=std::find(libraryTabs_.begin(),libraryTabs_.end(),oldLibrary);
            tab_=found==libraryTabs_.end()?0:static_cast<int>(found-libraryTabs_.begin());
            visualTab_=static_cast<float>(tab_);pendingTab_=-1;libraryFadePhase_=0;libraryFade_=1.f;
        }
        for (std::size_t index = 0; index < games_.size(); ++index) {
            const auto &game = games_[index];
            if(!MatchesLibrarySearch(game))continue;
            if(alphabetFilter_ && AlphabetBucket(game.name)!=alphabetFilter_)continue;
            visible_.push_back(index);
        }
        LibrarySort::Apply(visible_,games_,preferences_.sortMode,history_);
        selected_ = visible_.empty() ? 0 : std::min(selected_, visible_.size() - 1);
        adjacentLibraryCovers_.clear();
        if(libraryTabs_.size()>1)for(int step:{-1,1}) {
            const auto &library=libraryTabs_[(tab_+step+int(libraryTabs_.size()))%libraryTabs_.size()];
            std::vector<std::size_t> next;
            for(std::size_t i=0;i<games_.size();++i)
                if(MatchesLibrarySearch(games_[i],library) && (!alphabetFilter_ || AlphabetBucket(games_[i].name)==alphabetFilter_))next.push_back(i);
            LibrarySort::Apply(next,games_,preferences_.sortMode,history_);
            for(std::size_t i=0;i<std::min<std::size_t>(12,next.size());++i)adjacentLibraryCovers_.push_back({next[i],i>=4});
            if(preferences_.wrapNavigation)for(std::size_t i=0;i<std::min<std::size_t>(3,next.size());++i)
                adjacentLibraryCovers_.push_back({next[next.size()-1-i],false});
        }
    }
    void PrefetchArtwork() {
        if (visible_.empty()) return;
        if(preferences_.coverType!=CoverPresentation::Box) {
            const int count=int(visible_.size());
            const auto queue=[&](int index) {
                if(!preferences_.wrapNavigation && (index<0 || index>=count))return;
                const auto &game=games_[visible_[(index%count+count)%count]];
                textures_.Prefetch(game.logoPath);
                if(preferences_.coverType==CoverPresentation::Media)textures_.Prefetch(game.physicalMediaPath);
            };
            queue(int(selected_));
            for(int distance=1;distance<=std::min(8,count-1);++distance){queue(int(selected_)+distance);queue(int(selected_)-distance);}
            if(preferences_.useBackgrounds)textures_.PrefetchBackground(Selected()->backgroundPath);
            return;
        }
        const auto queueCover = [&](const GameEntry &game,bool detail,bool distant=false) {
            textures_.PrefetchCover(game.fullCoverPath,detail,distant);
            if(game.fullCoverPath.empty() || (textures_.CoverKnown(game.fullCoverPath,detail) && !textures_.HasCover(game.fullCoverPath,detail)))
                textures_.PrefetchCover(game.coverPath,detail,distant);
        };
        const auto queueFront = [&](const GameEntry &game) {queueCover(game,&game==Selected(),&game!=Selected());};
        const auto neighbors = [&](int center,int first,int last,auto queue) {
            for(int distance=first;distance<=last;++distance) {
                const int before=center+browseDirection_*distance;
                const int after=center-browseDirection_*distance;
                const int count=static_cast<int>(visible_.size());
                if(preferences_.wrapNavigation && distance<count) {
                    queue(games_[visible_[(before%count+count)%count]]);
                    queue(games_[visible_[(after%count+count)%count]]);
                } else if(!preferences_.wrapNavigation) {
                    if(before>=0 && before<count) queue(games_[visible_[before]]);
                    if(after>=0 && after<count) queue(games_[visible_[after]]);
                }
            }
        };
        if(preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8) {
            queueCover(*Selected(),true);
            neighbors(int(selected_),1,1,[&](const GameEntry &game){queueCover(game,true);});
            if(preferences_.useBackgrounds)textures_.PrefetchBackground(Selected()->backgroundPath);
            if(preferences_.useLogos)textures_.Prefetch(Selected()->logoPath);
            return;
        }
        // Queue the focused full-resolution case before neighboring display-size art.
        const int count=static_cast<int>(visible_.size()),rounded=static_cast<int>(std::round(visualSelected_));
        const int shown=preferences_.wrapNavigation?(rounded%count+count)%count:std::clamp(rounded,0,count-1);
        queueFront(games_[visible_[selected_]]);
        queueFront(games_[visible_[shown]]);
        neighbors(shown,1,3,[&](const GameEntry &game){queueCover(game,true);});
        // Ordinary SDL textures use four bytes per pixel. Keep the immediate
        // neighbors at original resolution, with sharper browse mips beyond,
        // so three 2048px originals leave room within the 96 MiB budget.
        neighbors(static_cast<int>(selected_),1,1,[&](const GameEntry &game){queueCover(game,true);});
        for(int distance=4;distance<=textures_.SharpLookAhead();++distance) {
            const int ahead=static_cast<int>(selected_)+browseDirection_*distance;
            if(preferences_.wrapNavigation && distance<count)queueFront(games_[visible_[(ahead%count+count)%count]]);
            else if(ahead>=0 && ahead<count)queueFront(games_[visible_[ahead]]);
        }
        neighbors(static_cast<int>(selected_),1,4,queueFront);
        // Once the current window is ready, warm the entry window of adjacent
        // content types. Switching tabs keeps these textures in the same LRU.
        if(textures_.NativeCovers() && textures_.Pending()<8)
            for(const auto &[index,distant]:adjacentLibraryCovers_)queueCover(games_[index],false,distant);
        const auto queueBuiltIn = [&](const GameEntry &game) {
            const auto &path=game.coverPath;
            if(game.fullCoverPath.empty() && (path.find("/cache/icons/")!=std::string::npos ||
                path.find("/cache/rom-art/")!=std::string::npos || path.ends_with("/meta/iconTex.tga")))
                textures_.PrefetchCover(path,false);
        };
        // Beyond the artwork window, look ahead only for small built-in icons.
        neighbors(static_cast<int>(selected_),17,24,queueBuiltIn);
        const auto queueGame = [&](const GameEntry &game, bool selected) {
            textures_.PrefetchCover(game.fullCoverPath,selected);
            if(game.fullCoverPath.empty() || (textures_.CoverKnown(game.fullCoverPath,selected) && !textures_.HasCover(game.fullCoverPath,selected))) {
                textures_.PrefetchCover(game.coverPath,selected);
                textures_.PrefetchCover(game.backCoverPath,selected);
                textures_.PrefetchCover(game.spinePath,selected);
                if(game.coverPath.empty() || game.backCoverPath.empty() || game.spinePath.empty())textures_.PrefetchCover(game.defaultSleevePath,selected);
            }
            if (selected) {
                if(preferences_.useBackgrounds)textures_.PrefetchBackground(game.backgroundPath);
                if (preferences_.useLogos || preferences_.coverType!=CoverPresentation::Box) textures_.Prefetch(game.logoPath);
            }
        };
        queueGame(games_[visible_[selected_]],true);
        if(preferences_.coverType!=CoverPresentation::Box)neighbors(static_cast<int>(selected_),0,8,[&](const GameEntry &game){
            textures_.Prefetch(game.logoPath);if(preferences_.coverType==CoverPresentation::Media)textures_.Prefetch(game.physicalMediaPath);
        });
        if(preferences_.useBackgrounds)
            neighbors(static_cast<int>(selected_),1,1,[&](const GameEntry &game){textures_.PrefetchBackground(game.backgroundPath);});
        // The centre cover is followed by the cards that can enter view next.
        neighbors(shown,1,3,[&](const GameEntry &game){queueGame(game,false);});
    }
    const std::vector<std::string> &LibraryTabs() const { return libraryTabs_; }
    std::string CurrentLibraryId() const {
        const auto tabs = LibraryTabs();
        if (tabs.empty()) return kAllLibraryId;
        return tabs[std::clamp(tab_, 0, static_cast<int>(tabs.size()) - 1)];
    }
    std::string LibraryLabel(std::string id) const {
        if (id == kAllLibraryId) return "ALL";
        if (id == kFavoritesLibraryId) return "FAVORITES";
        for(const auto &library:libraries_) if(library.id==id) return library.name;
        for (char &ch : id) {
            if (ch == '_' || ch == '-') ch = ' ';
            else ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        return id;
    }
public:
#include "switch_skin_members.hpp"
#include "home_navigation.hpp"
#include "control_binding_icons.hpp"
private:
    void DrawBackground(Color accent, const GameEntry *game) {
        if(SwitchSkin()){SetColor(renderer_,SwitchBackground());SDL_RenderClear(renderer_);const auto &path=SkinSpec().backgroundImage;if(!path.empty()){const auto image=textures_.GetBackground(path);if(image.texture)SDL_RenderCopy(renderer_,image.texture,nullptr,nullptr);}return;}
        if (preferences_.skin == 1) {
            const bool themeVideo=glassBackground_.Draw(kWidth,kHeight);
            if(!themeVideo)FillVerticalGradient(renderer_, {0,0,kWidth,kHeight},
                                 {246,248,248,255},{214,225,229,255});
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            if(themeVideo) { DrawSelectedBackground(game);return; }
            FillVerticalGradient(renderer_, {0,0,kWidth,kHeight},
                                 {250,253,253,150},{219,230,235,80});
            DrawStockMenuBackgroundLoop(renderer_,preferences_.backgroundMotion);
            const float phase = preferences_.backgroundMotion ? SDL_GetTicks() / 2200.0f : 0.0f;
            const float drift = preferences_.backgroundMotion ? std::sin(phase) * 34.0f : 0.0f;
            FillEllipse(renderer_, {-150.0f+drift,70.0f,590.0f,250.0f},{255,255,255,12});
            FillEllipse(renderer_, {850.0f-drift,255.0f,520.0f,260.0f},{112,190,216,4});
            FillEllipse(renderer_, {300.0f-drift*.35f,535.0f,680.0f,190.0f},{255,255,255,17});
            FillVerticalGradient(renderer_, {0,430,kWidth,290},
                                 {255,255,255,2},{76,122,142,22});
            DrawSelectedBackground(game);
            return;
        }
        SetColor(renderer_, Mix({5,10,20,255}, accent, .07f));
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const float phase = preferences_.backgroundMotion ? SDL_GetTicks() / 1800.0f : 0.0f;
        const int drift = preferences_.backgroundMotion ? static_cast<int>(std::sin(phase) * 35.0f) : 0;
        SetColor(renderer_, {accent.r,accent.g,accent.b,18});
        SDL_Rect glowA = {-180+drift,115,720,250};
        SDL_Rect glowB = {760-drift,360,700,230};
        SDL_RenderFillRect(renderer_, &glowA);
        SDL_RenderFillRect(renderer_, &glowB);
        SetColor(renderer_, {255,255,255,5});
        SDL_Rect horizon = {0,520,1280,2};
        SDL_RenderFillRect(renderer_, &horizon);
        DrawSelectedBackground(game);
    }
    void DrawSelectedBackground(const GameEntry *game) {
        if(!preferences_.useBackgrounds || !game || game->backgroundPath!=selectedBackgroundPath_ || !selectedBackground_.texture)return;
        auto *texture=selectedBackground_.texture;
        SDL_SetTextureAlphaMod(texture,static_cast<Uint8>((100-preferences_.backgroundTransparency)*255/100));
        SDL_RenderCopy(renderer_,texture,nullptr,nullptr);
        SDL_SetTextureAlphaMod(texture,255);
    }
    void DrawHeader(Color accent) {
        if(SwitchSkin()){DrawSwitchHeader();return;}

        if(!searchQuery_.empty() || alphabetFilter_)DrawText(renderer_,
            (alphabetFilter_?"A-Z: "+AlphabetLabel(alphabetFilter_)+"   ":"")+
            (!searchQuery_.empty()?"SEARCH: "+Ellipsize(searchQuery_,38):""),640,105,2,accent,true);
        const auto tabs = LibraryTabs();
        // The library picker is a clipped rail: large libraries never overlap the title/count controls.
        // The selected library stays centered; labels slide at full opacity inside the frame.
        auto drawLibraryRail = [&](int left, int width, int y, bool glass) {
            if (tabs.empty()) return;
            std::vector<int> tabWidths;
            tabWidths.reserve(tabs.size());
            for (const auto &id : tabs) tabWidths.push_back(TextWidth(LibraryLabel(id), 2) + 34);
            const float railFocus = std::clamp(visualTab_, 0.0f, static_cast<float>(tabs.size()-1));
            const int lower = static_cast<int>(std::floor(railFocus));
            const int upper = std::min(lower + 1, static_cast<int>(tabs.size()) - 1);
            const float fraction = railFocus - lower;
            int lowerCenter = 0, upperCenter = 0;
            for (int i=0;i<lower;++i) lowerCenter += tabWidths[i] + 12;
            lowerCenter += tabWidths[lower] / 2;
            for (int i=0;i<upper;++i) upperCenter += tabWidths[i] + 12;
            upperCenter += tabWidths[upper] / 2;
            const float focusedCenter = lowerCenter + (upperCenter-lowerCenter) * fraction;
            const int baseX = static_cast<int>(left + width/2 - focusedCenter);
            SDL_Rect clip{left, y-8, width, 55};
            SDL_RenderSetClipRect(renderer_, &clip);
            int x = baseX;
            for (int index=0; index<static_cast<int>(tabs.size()); ++index) {
                const int tabWidth = tabWidths[index];
                const float focus = std::max(0.0f, 1.0f - std::abs(static_cast<float>(index) - railFocus));
                const bool selected = focus > .03f;
                if (selected) {
                    DrawSoftShadow(renderer_, {x,y-5,tabWidth,38}, 17, 4, glass ? 54 : 36);
                    FillRoundedRect(renderer_, {x,y-5,tabWidth,38}, 17,
                                    {accent.r,accent.g,accent.b,static_cast<Uint8>((glass ? 116 : 48) * focus)});
                    if (glass) FillRoundedRect(renderer_, {x+3,y-2,tabWidth-6,32}, 14,{232,250,255,static_cast<Uint8>(48 * focus)});
                }
                // Selection changes color, never label opacity. Fading the selected
                // label by focus made it disappear as it crossed between positions.
                const Color label = glass ? Mix({117,129,138,255},{43,54,63,255},focus)
                                          : Mix({139,151,171,255},{255,255,255,255},focus);
                const int textHeight=glass && gSmoothFonts[2]?TTF_FontHeight(gSmoothFonts[2]):14;
                DrawText(renderer_, LibraryLabel(tabs[index]), x+tabWidth/2, y-5+(38-textHeight)/2, 2,label,true);
                x += tabWidth + 12;
            }
            SDL_RenderSetClipRect(renderer_, nullptr);
            // Clip at the frame boundary without an edge-fade overlay.
        };
        if (preferences_.skin == 1) {
            DrawGlassSurface(renderer_, {32,20,1216,70}, 25, accent, 92, true);
            DrawText(renderer_, "uFlow", 58, 38, 4, {46,55,63,255});
            DrawText(renderer_, LibraryPosition(), 165, 52, 1, {104,117,127,255});
            DrawGlassSurface(renderer_, {398,31,600,52}, 19, accent, 42, false);
            drawLibraryRail(408,580,42,true);
            return;
        }
        FillRoundedRect(renderer_, {38,26,1204,74}, 22, {5,10,20,190});
        DrawText(renderer_, "UFLOW", 66, 46, 4, {245,249,255,255});
        DrawText(renderer_, LibraryPosition(), 198, 55, 2, accent);
        FillRoundedRect(renderer_, {414,38,590,48}, 17, {255,255,255,8});
        drawLibraryRail(424,570,48,false);
    }
    void DrawEmpty(Color accent) {
        DrawText(renderer_, (!searchQuery_.empty()||alphabetFilter_)?"NO MATCHING GAMES":CurrentLibraryId() == kFavoritesLibraryId ? "NO FAVORITES YET" : "NO GAMES FOUND", 640, 300, 5,
                 (preferences_.skin==1 || preferences_.skin==2)?Color{54,64,72,255}:Color{235,241,250,255}, true);
        DrawText(renderer_, (!searchQuery_.empty()||alphabetFilter_)?std::string("PRESS ")+ActionButton(ControlAction::Sort)+" TO CLEAR FILTER":CurrentLibraryId() == kFavoritesLibraryId ? favoriteHint_ : "ADD DIRECTORIES IN SETTINGS > LIBRARY",
                 640, 365, 2, (preferences_.skin==1 || preferences_.skin==2)?Color{105,118,128,255}:accent, true);
    }
    void DrawPresentation(const GameEntry &game,SDL_Rect rect,bool selected,Color accent,float opacity=1.f,float roll=0.f) {
        if(preferences_.coverType==CoverPresentation::Thumbnails){DrawThumbnail(game,rect,selected,SwitchSkin()?SwitchAccent():accent,opacity);return;}
        if(selected && !launchAnimation_) {
            if(launchCapture_)return;
            launchOrigin_=rect;launchYaw_=launchPitch_=launchRoll_=0;
        }
        const auto drawImage=[&](TextureInfo image,SDL_Rect area) {
            if(!image.texture || image.width<=0 || image.height<=0)return false;
            const float scale=std::min(float(area.w)/image.width,float(area.h)/image.height);
            SDL_FRect fit{area.x+(area.w-image.width*scale)*.5f,area.y+(area.h-image.height*scale)*.5f,image.width*scale,image.height*scale};
            SDL_SetTextureAlphaMod(image.texture,Uint8(255*opacity));
            SDL_RenderCopyExF(renderer_,image.texture,nullptr,&fit,roll*180.0/3.141592653589793,nullptr,SDL_FLIP_NONE);SDL_SetTextureAlphaMod(image.texture,255);return true;
        };
        const auto drawLabel=[&](SDL_Rect area) {
            if(!drawImage(textures_.Get(game.logoPath),area)) {
                auto ink=preferences_.coverType==CoverPresentation::Logo?
                    (preferences_.skin==1?Color{36,53,65,255}:Color{239,246,251,255}):Color{42,51,60,255};
                ink.a=Uint8(ink.a*opacity);
                DrawFittedText(renderer_,game.name,area.x,area.y+area.h/2-10,area.w,selected?3:2,ink);
            }
        };
        if(preferences_.coverType==CoverPresentation::Logo) {
            drawLabel(rect);return;
        }
        const auto id=!game.mediaLibraryId.empty()?game.mediaLibraryId:game.vcSystem.empty()||game.vcSystem=="wiiu"?GameLibraryId(game):game.vcSystem;
        const auto shape=CoverPresentation::Shape(id,game.absolutePath);
        using S=CoverPresentation::MediaShape;
        const bool disc=shape==S::Disc || shape==S::MiniDisc;
        float ratio=disc?1.f:shape==S::NES?.85f:shape==S::DS?.93f:shape==S::HuCard?.65f:shape==S::GBA?1.65f:1.5f;
        const float scale=std::min(float(rect.w)/ratio,float(rect.h))*(shape==S::MiniDisc?.82f:1.f);
        const int w=int(scale*ratio),h=int(scale);
        SDL_Rect body{rect.x+(rect.w-w)/2,rect.y+(rect.h-h)/2,w,h};
        const auto part=[&](float x,float y,float width,float height){return SDL_Rect{body.x+int(x*w),body.y+int(y*h),int(width*w),int(height*h)};};
        SDL_Rect label=disc?part(.18f,.19f,.64f,.23f):part(.2f,.22f,.6f,.46f);
        const auto artwork=textures_.Get(game.physicalMediaPath);
        if(artwork.texture) {
            drawImage(artwork,body);
            const auto slash=game.physicalMediaPath.find_last_of('/');
            auto name=game.physicalMediaPath.substr(slash==std::string::npos?0:slash+1);
            for(auto &c:name)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(name=="default.png")drawLabel(label);
            return;
        }
        if(disc) {
            const float cx=body.x+w*.5f,cy=body.y+h*.5f,r=w*.5f;
            FillSmoothAnnulus(renderer_,cx,cy,r,r*.12f,{157,172,184,255});
            FillSmoothAnnulus(renderer_,cx,cy,r-2.f,r*.21f,{224,231,235,255});
            FillSmoothAnnulus(renderer_,cx,cy,r*.19f,r*.13f,{185,203,213,245});
            FillSmoothAnnulus(renderer_,cx,cy,r*.97f,r*.95f,{255,255,255,180});
            FillSmoothAnnulus(renderer_,cx,cy,r*.9f,r*.895f,{accent.r,accent.g,accent.b,90});
            DrawFittedText(renderer_,LibraryLabel(id),body.x+int(w*.27f),body.y+int(h*.73f),int(w*.46f),1,{88,107,118,255});
        } else {
            const bool dark=shape==S::DS||shape==S::GBA||shape==S::Cartridge;
            const Color plastic=dark?Color{54,62,72,255}:Color{177,181,187,255};
            FillRoundedRect(renderer_,{body.x+4,body.y+6,w,h},std::max(3,w/22),{0,0,0,80});
            if(shape==S::N64) {
                FillRoundedRect(renderer_,part(0,.13f,1,.87f),std::max(3,w/16),plastic);
                FillRoundedRect(renderer_,part(.13f,0,.74f,.98f),std::max(3,w/10),plastic);
            } else FillRoundedRect(renderer_,body,std::max(3,w/22),plastic);
            FillRoundedRect(renderer_,part(.18f,.18f,.64f,.55f),std::max(2,w/35),{28,35,43,230});
            FillRoundedRect(renderer_,part(.2f,.2f,.6f,.5f),std::max(2,w/45),{223,224,213,255});
            label=part(.23f,.24f,.54f,.4f);
            if(shape==S::DS || shape==S::HuCard) {
                FillRoundedRect(renderer_,part(.12f,.79f,.76f,.16f),2,{28,33,38,255});
                for(int i=0;i<9;++i)FillRoundedRect(renderer_,part(.15f+i*.079f,.82f,.052f,.1f),1,{189,153,76,255});
            } else {
                for(int i=0;i<4;++i) {
                    FillRoundedRect(renderer_,part(.04f,.28f+i*.09f,.09f,.025f),1,{dark?Uint8(30):Uint8(136),dark?Uint8(37):Uint8(143),dark?Uint8(46):Uint8(149),255});
                    FillRoundedRect(renderer_,part(.87f,.28f+i*.09f,.09f,.025f),1,{dark?Uint8(30):Uint8(136),dark?Uint8(37):Uint8(143),dark?Uint8(46):Uint8(149),255});
                }
                FillRoundedRect(renderer_,part(.24f,.87f,.52f,.055f),2,{31,39,46,220});
            }
        }
        drawLabel(label);
    }
    void DrawCover(const GameEntry &game, SDL_Rect rect, bool selected, Color accent) {
        if(preferences_.coverType!=CoverPresentation::Box){DrawPresentation(game,rect,selected,accent);return;}
        if(selected) {
            if(launchCapture_)return;
            launchOrigin_=rect;launchYaw_=launchPitch_=launchRoll_=0.f;
        }
        const auto profile=CaseProfileForGame(game,libraries_);
        const float scale=std::min(rect.w/std::max(135.f,profile.widthMm),rect.h/std::max(190.f,profile.heightMm));
        const int width=static_cast<int>(profile.widthMm*scale),height=static_cast<int>(profile.heightMm*scale);
        rect.x+=(rect.w-width)/2;rect.y+=(rect.h-height)/2;rect.w=width;rect.h=height;
        const bool flat=preferences_.coverLayout==2;
        SDL_Rect shadow = {rect.x+12,rect.y+15,rect.w,rect.h};
        if(!flat)FillRoundedRect(renderer_, shadow, 18, {0,0,0,105});
        FillRoundedRect(renderer_, rect, 18, selected ? Color{235,241,250,255} : Color{27,37,54,255});
        const int border = flat?1:selected ? 5 : 2;
        SDL_Rect inner = {rect.x+border,rect.y+border,rect.w-border*2,rect.h-border*2};
        FillRoundedRect(renderer_, inner, 14, selected ? Color{10,18,31,255} : Color{15,24,39,255});
        const float panel=profile.sidewaysSleeve?profile.heightMm:profile.widthMm;
        const float total=panel*2+profile.thicknessMm;
        const auto front=CoverFaces::Resolve(game,CoverFaces::Face::Front,panel/total,
            (panel+profile.thicknessMm)/total,[&](const auto &path){return textures_.GetCover(path,selected).texture!=nullptr;});
        const TextureInfo cover = textures_.GetCover(front.path,selected);
        if (cover.texture) {
            const int padding = flat?0:selected ? 16 : 11;
            SDL_Rect area = {inner.x+padding,inner.y+padding,inner.w-padding*2,inner.h-padding*2};
            SDL_Rect crop{static_cast<int>(front.u0*cover.width),0,static_cast<int>((front.u1-front.u0)*cover.width),cover.height};
            const bool rotated=CoverFaces::TurnSleeve(profile.sidewaysSleeve,profile.sidewaysDefaultSleeve,front.sleeve,front.path==game.defaultSleevePath,cover.width,cover.height);
            const float sourceRatio = rotated?float(cover.height)/std::max(1,crop.w):static_cast<float>(crop.w) / cover.height;
            const float targetRatio = static_cast<float>(area.w) / area.h;
            SDL_Rect destination = area;
            if (sourceRatio > targetRatio) { destination.h = static_cast<int>(area.w/sourceRatio); destination.y += (area.h-destination.h)/2; }
            else { destination.w = static_cast<int>(area.h*sourceRatio); destination.x += (area.w-destination.w)/2; }
            const auto drawFace=[&](SDL_Rect area,bool reflection) {
                SDL_Vertex vertices[4]{};const int indices[]={0,1,2,0,2,3};
                for(int i=0;i<4;++i) {
                    const float x=(i==1||i==2)?1.f:0.f,y=i<2?0.f:1.f;
                    const float v=reflection?1.f-y*area.h/destination.h:y;
                    vertices[i].position={area.x+x*area.w,area.y+y*area.h};
                    vertices[i].color={255,255,255,reflection?(i<2?Uint8(75):Uint8(0)):Uint8(255)};
                    if(rotated) {
                        const auto uv=CoverFaces::SidewaysUV(front.u0,front.u1,x,v,CoverFaces::Face::Front,front.path==game.defaultSleevePath && profile.sidewaysDefaultSleeve);
                        vertices[i].tex_coord={uv.u,uv.v};
                    } else vertices[i].tex_coord={front.u0+x*(front.u1-front.u0),v};
                }
                SDL_RenderGeometry(renderer_,cover.texture,vertices,4,indices,6);
            };
            drawFace(destination,false);
            if(flat && preferences_.reflections)drawFace({destination.x,destination.y+destination.h+3,destination.w,std::min(destination.h/3,100)},true);
        } else {
            const Color platformAccent = AccentFor(game.platform, preferences_.theme);
            FillRoundedRect(renderer_, {inner.x+12,inner.y+12,inner.w-24,inner.h-24}, 12,
                            {platformAccent.r,platformAccent.g,platformAccent.b,selected ? Uint8(160) : Uint8(92)});
            DrawText(renderer_, game.mediaLibraryId.empty()?PlatformName(game.platform):LibraryLabel(game.mediaLibraryId), rect.x+rect.w/2, rect.y+rect.h/2-45,
                     selected ? 3 : 2, {255,255,255,255}, true);
            DrawText(renderer_, Ellipsize(game.name, selected ? 18 : 14), rect.x+rect.w/2, rect.y+rect.h/2+12,
                     selected ? 2 : 1, {255,255,255,255}, true);
        }
        if (selected) {
            SetColor(renderer_, {accent.r,accent.g,accent.b,220});
            for (int line=0; line<(flat?1:4); ++line) { SDL_Rect outline={rect.x-line,rect.y-line,rect.w+line*2,rect.h+line*2}; SDL_RenderDrawRect(renderer_,&outline); }
        }
        if (game.favorite) DrawFavoriteBadge(rect.x+rect.w-34,rect.y+8);
        DrawInstallBadge(game,rect);
    }
    void DrawFavoriteBadge(int x,int y,int size=28) {
        FillRoundedRect(renderer_,{x+1,y+2,size,size},size/3,{0,0,0,65});
        FillRoundedRect(renderer_,{x,y,size,size},size/3,{28,38,49,225});
        constexpr float pi=3.14159265358979323846f;
        SDL_Vertex vertices[11]{};int indices[30]{};SDL_FPoint outline[11]{};
        const float cx=x+size*.5f,cy=y+size*.5f,outer=size*.36f,inner=outer*.46f;
        vertices[0]={{cx,cy},{255,220,107,255},{0,0}};
        for(int i=0;i<10;++i){const float angle=-pi*.5f+i*pi/5.f,r=i%2?inner:outer;
            const SDL_FPoint point{cx+std::cos(angle)*r,cy+std::sin(angle)*r};
            vertices[i+1]={point,point.y<cy?SDL_Color{255,232,144,255}:SDL_Color{242,177,48,255},{0,0}};
            outline[i]=point;indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=(i+1)%10+1;
        }
        SDL_RenderGeometry(renderer_,nullptr,vertices,11,indices,30);
        outline[10]=outline[0];SetColor(renderer_,{255,235,161,235});SDL_RenderDrawLinesF(renderer_,outline,11);
    }
    // The selected case at rest keeps a slight, breathing turn.
    float CaseIdleYaw() const {
        return .055f + (preview_ ? 0.f : std::sin(SDL_GetTicks() / 1250.0f) * .018f);
    }
    // Side cases turned nearly edge-on (shelf books) cast a narrow shadow and
    // no floor reflection: a reflected sliver would show on one side only.
    float CaseShadowWidth(SDL_Rect rect, bool selected, const CaseView &view) const {
        if (selected) return rect.w * 1.02f;
        return std::min(rect.w * .82f, view.Bounds().w + 16.f);
    }
    bool CaseReflects(bool selected, const CaseView &view) const {
        return preferences_.skin == 1 && preferences_.reflections &&
               view.yawCosine * view.pitchCosine > .015f && (selected || std::abs(view.yawCosine) >= .3f);
    }
    // Favorite marker at the case's top-right corner, inside what is drawn.
    static SDL_Point CaseStar(SDL_Rect rect, const CaseView &view, bool carousel) {
        if (!carousel) return {rect.x + rect.w - 32, rect.y + 8};
        const SDL_FRect box = view.Bounds();
        return {static_cast<int>(std::max(box.x, box.x + box.w - 32.f)), static_cast<int>(box.y) + 8};
    }
    void DrawBox(const GameEntry &game, SDL_Rect rect, float relative, bool selected, Color accent,
                 float turnScale = .82f, float pitch = 0.0f, float roll = 0.0f,
                 bool absoluteRotation = false, float camera = CaseView::kCamera,
                 float subpixelX = 0.f, float subpixelY = 0.f, bool settledPreview = false) {
        if(preferences_.coverType!=CoverPresentation::Box){DrawPresentation(game,rect,selected,accent);return;}
        const auto profile = CaseProfileForGame(game,libraries_);
        const float yaw = absoluteRotation ? turnScale :
            -std::clamp(relative, -1.0f, 1.0f) * turnScale + (selected ? CaseIdleYaw() : 0.0f);
        if(selected) {
            if(launchCapture_)return;
            launchOrigin_=rect;launchYaw_=yaw;launchPitch_=pitch;launchRoll_=roll;
        }
        // Render each model at 2x using the smallest preallocated target that fits.
        SDL_Rect bounds;
        const SDL_Rect modelRect{0,0,rect.w,rect.h};
        if (pitch == 0.0f && roll == 0.0f) {
            // Carousel poses bound what is actually drawn: case, shadow, floor
            // reflection and favorite star. Edge-on shelf cases are narrow, so
            // a full shelf fits the side-case budget.
            const CaseView view(modelRect, profile, yaw, 0.f,0.f, camera);
            const SDL_FRect box = view.Bounds();
            const float centerX = rect.w * .5f, shadow = CaseShadowWidth(modelRect, selected, view) * .5f + 12.f;
            float left = std::min(box.x, centerX - shadow), right = std::max(box.x + box.w, centerX + shadow);
            float bottom = std::max(box.y + box.h, rect.h + (selected ? 30.f : 21.f));
            if (CaseReflects(selected, view)) bottom = std::max(bottom, box.y + box.h * (selected ? 1.34f : 1.24f) + 8.f);
            if (game.favorite) {
                const SDL_Point star = CaseStar(modelRect, view, true);
                left = std::min(left, static_cast<float>(star.x)); right = std::max(right, star.x + 30.f);
            }
            bounds.x = static_cast<int>(std::floor(left)) - 2;
            bounds.y = static_cast<int>(std::floor(box.y)) - 2;
            bounds.w = static_cast<int>(std::ceil(right)) + 2 - bounds.x;
            bounds.h = static_cast<int>(std::ceil(bottom)) + 2 - bounds.y;
            // Round in model space: screen translation must never resize the
            // cached image or change its anchor through floating-point rounding.
            bounds.x += rect.x; bounds.y += rect.y;
        } else {
            const int half = static_cast<int>(std::ceil(rect.h * .65f)) + 4;
            bounds = {rect.x + rect.w/2 - half, rect.y + rect.h/2 - half,
                      half*2, half + static_cast<int>(std::ceil(rect.h*.90f)) + 4};
        }
        // Offscreen cards do not need transforms, texture decoding or GPU work.
        if (bounds.x >= kWidth || bounds.x + bounds.w <= 0) return;
        const SDL_FRect destination{bounds.x+subpixelX,bounds.y+subpixelY,
                                    static_cast<float>(bounds.w),static_cast<float>(bounds.h)};
        // Away from the centre, a case only translates. Keep its AA result and
        // composite it directly: no repeated mesh sorting, target switch or clear.
        // Do not cache a temporary placeholder while its cover is still queued.
        // A known decode failure may be cached, but a pending decode must render
        // directly once more so the completed artwork replaces the placeholder.
        const bool artworkResolved = (textures_.CoverDetailKnown(game.fullCoverPath,selected) && textures_.HasCover(game.fullCoverPath,selected)) ||
            (textures_.CoverDetailKnown(game.fullCoverPath,selected) && textures_.CoverDetailKnown(game.coverPath,selected) &&
             textures_.CoverDetailKnown(game.backCoverPath,selected) && textures_.CoverDetailKnown(game.spinePath,selected) && textures_.CoverDetailKnown(game.defaultSleevePath,selected));
        // Resting side cases only translate; the carousel passes their fixed yaw.
        bool cacheable = (!selected && std::abs(relative) >= 1.f) || settledPreview;
        std::string cacheKey;
        if (cacheable) {
            relative = relative < 0 ? -1.f : 1.f;
            cacheKey = game.fullCoverPath + "\n" + game.coverPath + "\n" + game.name + "\n" +
                game.backCoverPath + "\n" + game.spinePath + "\n" + game.defaultSleevePath + "\n" +
                std::to_string(game.defaultBackEnd) + ":" + std::to_string(game.defaultFrontStart) + ":" +
                std::to_string(static_cast<int>(game.platform)) + ":" + std::to_string(game.favorite) + ":" +
                std::to_string(rect.w) + ":" + std::to_string(rect.h) + ":" + std::to_string(relative) + ":" +
                std::to_string(yaw) + ":" + std::to_string(camera) + ":" + std::to_string(pitch) + ":" + std::to_string(roll) + ":" +
                std::to_string(profile.widthMm) + ":" + std::to_string(profile.heightMm) + ":" +
                std::to_string(profile.thicknessMm) + ":" + std::to_string(profile.sidewaysSleeve) + ":" +
                std::to_string(preferences_.skin) + ":" + std::to_string(preferences_.reflections) + ":" + std::to_string(selected) + ":" +
                std::to_string(accent.r) + ":" + std::to_string(accent.g) + ":" + std::to_string(accent.b);
            for (auto &entry : sideCases_) if (entry.key == cacheKey) {
                entry.use = ++sideCaseUse_;
                SDL_RenderCopyF(renderer_,entry.texture,nullptr,&destination);
                return;
            }
        }
        // A completed side-card stays usable after its source textures are
        // evicted. Only creation of a NEW cached card requires resolved faces.
        if(!artworkResolved)cacheable=false;
        SDL_Texture *caseTexture = nullptr;
        for (std::size_t i=0; i<caseTextures_.size(); ++i) {
            if (caseTextures_[i] && bounds.w*2 <= kCaseTargetSizes[i].x && bounds.h*2 <= kCaseTargetSizes[i].y) {
                caseTexture = caseTextures_[i]; break;
            }
        }
        if (!caseTexture) {
#ifdef UFLOW_HOST_PREVIEW
            ++caseDirectFallbacks_;
#endif
            DrawBoxDirect(game,rect,relative,selected,accent,yaw,pitch,roll,camera,profile);
            return;
        }
        SDL_Texture *cachedTarget = nullptr;
        const std::size_t cachedBytes = static_cast<std::size_t>(bounds.w) * 2 * static_cast<std::size_t>(bounds.h) * 2 * 4;
        if (cacheable) {
            // Least recently used first, but never a case already shown this
            // frame: a view larger than the budget renders directly, not thrash.
            while (!sideCases_.empty() &&
                   (sideCases_.size() >= kMaxSideCases || sideCaseBytes_ + cachedBytes > kSideCaseBudget)) {
                auto oldest = std::min_element(sideCases_.begin(),sideCases_.end(),
                    [](const auto &a,const auto &b){return a.use < b.use;});
                if (oldest->use > sideCaseFrame_) break;
                sideCaseBytes_ -= oldest->bytes;
                SDL_DestroyTexture(oldest->texture); sideCases_.erase(oldest);
            }
            if (sideCases_.size() < kMaxSideCases && sideCaseBytes_ + cachedBytes <= kSideCaseBudget)
                cachedTarget = SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,
                    SDL_TEXTUREACCESS_TARGET,bounds.w*2,bounds.h*2);
            if (cachedTarget) {
                const auto blend = SDL_ComposeCustomBlendMode(
                    SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,
                    SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
                if (SDL_SetTextureBlendMode(cachedTarget,blend) != 0) {
                    SDL_DestroyTexture(cachedTarget); cachedTarget = nullptr;
                } else {
                    SDL_SetTextureScaleMode(cachedTarget,SDL_ScaleModeLinear);
                    caseTexture = cachedTarget;
                }
            }
        }
        SDL_Texture *target = SDL_GetRenderTarget(renderer_);
        int logicalW,logicalH; SDL_RenderGetLogicalSize(renderer_,&logicalW,&logicalH);
        float scaleX,scaleY; SDL_RenderGetScale(renderer_,&scaleX,&scaleY);
        SDL_Rect viewport,clip; SDL_RenderGetViewport(renderer_,&viewport);
        SDL_RenderGetClipRect(renderer_,&clip);
        const bool clipped = SDL_RenderIsClipEnabled(renderer_);
        if (SDL_SetRenderTarget(renderer_,caseTexture) != 0) {
#ifdef UFLOW_HOST_PREVIEW
            ++caseDirectFallbacks_;
#endif
            if (cachedTarget) SDL_DestroyTexture(cachedTarget);
            DrawBoxDirect(game,rect,relative,selected,accent,yaw,pitch,roll,camera,profile);
            return;
        }
        SDL_RenderSetLogicalSize(renderer_,0,0);
        SDL_RenderSetViewport(renderer_,nullptr); SDL_RenderSetClipRect(renderer_,nullptr);
        SDL_RenderSetScale(renderer_,2.f,2.f);
        // GX2's SDL renderer enables alpha-test > 0: a transparent FillRect is
        // discarded even with blending disabled. Only a real target clear removes
        // old cards/alpha from this reusable texture (SDL_RenderClear -> GX2ClearColor).
        SetColor(renderer_,{0,0,0,0});
        SDL_RenderClear(renderer_);
        SDL_Rect local=rect; local.x-=bounds.x; local.y-=bounds.y;
        DrawBoxDirect(game,local,relative,selected,accent,yaw,pitch,roll,camera,profile);
        SDL_SetRenderTarget(renderer_,target);
        SDL_RenderSetLogicalSize(renderer_,logicalW,logicalH);
        SDL_RenderSetScale(renderer_,scaleX,scaleY); SDL_RenderSetViewport(renderer_,&viewport);
        SDL_RenderSetClipRect(renderer_,clipped?&clip:nullptr);
        const SDL_Rect source{0,0,bounds.w*2,bounds.h*2};
        SDL_RenderCopyF(renderer_,caseTexture,&source,&destination);
        if (cachedTarget) {
            sideCases_.push_back({cacheKey,cachedTarget,++sideCaseUse_,cachedBytes});
            sideCaseBytes_ += cachedBytes;
        }
    }
    void DrawBoxDirect(const GameEntry &game, SDL_Rect rect, float relative, bool selected, Color accent,
                       float yaw, float pitch, float roll, float camera, CaseProfile profile) {
        if(preferences_.coverType!=CoverPresentation::Box){DrawPresentation(game,rect,selected,accent);return;}
#ifdef UFLOW_HOST_PREVIEW
        ++caseDraws_;
#endif
        const float geometrySleeveWidth = profile.widthMm * 2.f + profile.thicknessMm;
        const float geometryBackEnd = profile.widthMm / geometrySleeveWidth;
        const float geometryFrontStart = (profile.widthMm + profile.thicknessMm) / geometrySleeveWidth;
        // A sideways carton scan stores portrait back/spine/front panels. Its
        // panel width becomes the landscape case's visual height after the
        // front/back region is turned, so use height for the source cuts.
        const float sourcePanel = profile.sidewaysSleeve ? profile.heightMm : profile.widthMm;
        const float sourceSleeveWidth = sourcePanel * 2.f + profile.thicknessMm;
        const float backEnd = sourcePanel / sourceSleeveWidth;
        const float frontStart = (sourcePanel + profile.thicknessMm) / sourceSleeveWidth;
        const auto selectFace=[&](CoverFaces::Face face) {
            if(!game.fullCoverPath.empty() && !textures_.CoverKnown(game.fullCoverPath,selected)) {
                textures_.PrefetchCover(game.fullCoverPath,selected);
                return CoverFaces::Selection{}; // Pending is not a failed decode; don't queue unused faces.
            }
            return CoverFaces::Resolve(game,face,backEnd,frontStart,[&](const auto &path){return textures_.GetCover(path,selected).texture!=nullptr;});
        };
        const auto front=selectFace(CoverFaces::Face::Front);
        const TextureInfo artwork=textures_.GetCover(front.path,selected);
        const float frontU=front.u0;
        const auto rotateSleeve=[&](const CoverFaces::Selection &face){
            const auto texture=textures_.GetCover(face.path,selected);
            return CoverFaces::TurnSleeve(profile.sidewaysSleeve,profile.sidewaysDefaultSleeve,
                face.sleeve,face.path==game.defaultSleevePath,texture.width,texture.height);
        };
        const auto reverseDefault=[&](const CoverFaces::Selection &face,CoverFaces::Face side){
            // SNES generic faces reverse retail authoring; the new N64 generic
            // front follows retail, while its back reverses it.
            return face.path==game.defaultSleevePath &&
                (profile.sidewaysDefaultSleeve || side==CoverFaces::Face::Back);
        };
        const bool rotateFront=rotateSleeve(front);

        using Transformed = CaseView::Point;
        struct Face { std::uint16_t a, b, c; float depth; };
        const CaseView view(rect, profile, yaw, pitch, roll, camera);
        const float yawSine = view.yawSine, yawCosine = view.yawCosine;
        const float pitchSine = view.pitchSine, pitchCosine = view.pitchCosine;
        const float rollSine = view.rollSine, rollCosine = view.rollCosine;
        const auto transform = [&](float x, float y, float z) { return view(x, y, z); };

        const auto topLeft = transform(-.478f, .482f, .515f);
        const auto topRight = transform(.478f, .482f, .515f);
        const auto bottomRight = transform(.478f, -.482f, .515f);
        const auto bottomLeft = transform(-.478f, -.482f, .515f);
        const bool frontFacing = yawCosine * pitchCosine > .015f;

        const float shadowWidth = CaseShadowWidth(rect, selected, view);
        DrawSoftEllipseShadow(renderer_,
                              {rect.x + rect.w*.5f-shadowWidth*.5f,
                               rect.y + rect.h-2.0f,shadowWidth,selected?24.0f:15.0f},
                              selected ? 104 : 58);

        if (frontFacing && artwork.texture && CaseReflects(selected, view)) {
            float sourceAspect=artwork.width*(front.u1-front.u0)/std::max(1,artwork.height);
            if(rotateFront && sourceAspect>0)sourceAspect=1.f/sourceAspect;
            const auto fit=CoverFaces::Contain(sourceAspect,profile.widthMm/profile.heightMm);
            const auto topLeft=transform(-.478f+.956f*fit.x0,.482f-.964f*fit.y0,.515f);
            const auto topRight=transform(-.478f+.956f*fit.x1,.482f-.964f*fit.y0,.515f);
            const auto bottomLeft=transform(-.478f+.956f*fit.x0,.482f-.964f*fit.y1,.515f);
            const auto bottomRight=transform(-.478f+.956f*fit.x1,.482f-.964f*fit.y1,.515f);
            const float reflectionScale = selected ? .34f : .24f;
            SDL_Vertex reflection[4]{};
            reflection[0].position = {bottomLeft.point.x, bottomLeft.point.y + 7.0f};
            reflection[1].position = {bottomRight.point.x, bottomRight.point.y + 7.0f};
            reflection[2].position = {bottomRight.point.x + (bottomRight.point.x - topRight.point.x) * reflectionScale,
                                      bottomRight.point.y + (bottomRight.point.y - topRight.point.y) * reflectionScale};
            reflection[3].position = {bottomLeft.point.x + (bottomLeft.point.x - topLeft.point.x) * reflectionScale,
                                      bottomLeft.point.y + (bottomLeft.point.y - topLeft.point.y) * reflectionScale};
            reflection[0].tex_coord = {frontU,1}; reflection[1].tex_coord = {1,1};
            reflection[2].tex_coord = {1,0}; reflection[3].tex_coord = {frontU,0};
            if(rotateFront){
                const bool fallback=reverseDefault(front,CoverFaces::Face::Front);
                for(int i=0;i<4;++i){
                    const float x=(i==1||i==2)?1.f:0.f,y=i<2?1.f:0.f;
                    const auto uv=CoverFaces::SidewaysUV(front.u0,front.u1,x,y,CoverFaces::Face::Front,fallback);
                    reflection[i].tex_coord={uv.u,uv.v};
                }
            }
            const Uint8 nearAlpha = selected ? 72 : 42;
            reflection[0].color = {225,238,245,nearAlpha};
            reflection[1].color = {225,238,245,nearAlpha};
            reflection[2].color = {225,238,245,0};
            reflection[3].color = {225,238,245,0};
            const int reflectionIndices[] = {0,1,2,0,2,3};
            SDL_RenderGeometry(renderer_, artwork.texture, reflection, 4, reflectionIndices, 6);
        }

        std::array<SDL_Vertex, CaseModel::kPositions.size()> vertices{};
        std::array<float, CaseModel::kPositions.size()> depths{};
        for (std::size_t index = 0; index < CaseModel::kPositions.size(); ++index) {
            const auto &position = CaseModel::kPositions[index];
            const auto projected = transform(position.x, position.y, position.z);
            vertices[index].position = projected.point;
            depths[index] = projected.depth;
            const auto &normal = CaseModel::kNormals[index];
            const float yawNormalX = normal.x * yawCosine + normal.z * yawSine;
            const float yawNormalZ = -normal.x * yawSine + normal.z * yawCosine;
            const float pitchNormalY = normal.y * pitchCosine - yawNormalZ * pitchSine;
            const float pitchNormalZ = normal.y * pitchSine + yawNormalZ * pitchCosine;
            const float normalX = yawNormalX * rollCosine - pitchNormalY * rollSine;
            const float normalY = yawNormalX * rollSine + pitchNormalY * rollCosine;
            const float light = std::clamp(.34f + std::max(0.0f, normalX * -.28f + normalY * -.20f + pitchNormalZ * .95f) * .66f,
                                           .30f, 1.0f);
            const Color plastic = preferences_.skin == 1
                                      ? Mix({218,232,239,255}, accent, selected ? .22f : .10f)
                                      : Mix({15,31,50,255}, accent, selected ? .68f : .48f);
            vertices[index].color = {static_cast<Uint8>(plastic.r * light),
                                     static_cast<Uint8>(plastic.g * light),
                                     static_cast<Uint8>(plastic.b * light), 255};
        }

        std::array<Face, CaseModel::kIndices.size() / 3> faces{};
        for (std::size_t face = 0; face < faces.size(); ++face) {
            const auto a = CaseModel::kIndices[face * 3];
            const auto b = CaseModel::kIndices[face * 3 + 1];
            const auto c = CaseModel::kIndices[face * 3 + 2];
            faces[face] = {a, b, c, (depths[a] + depths[b] + depths[c]) / 3.0f};
        }
        std::sort(faces.begin(), faces.end(), [](const Face &left, const Face &right) {
            return left.depth < right.depth;
        });
        std::array<int, CaseModel::kIndices.size()> sortedIndices{};
        for (std::size_t face = 0; face < faces.size(); ++face) {
            sortedIndices[face * 3] = faces[face].a;
            sortedIndices[face * 3 + 1] = faces[face].b;
            sortedIndices[face * 3 + 2] = faces[face].c;
        }

        SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                           sortedIndices.data(), static_cast<int>(sortedIndices.size()));

        SDL_Vertex sleeve[4]{};
        sleeve[0].position = topLeft.point; sleeve[0].tex_coord = {frontU, 0};
        sleeve[1].position = topRight.point; sleeve[1].tex_coord = {1, 0};
        sleeve[2].position = bottomRight.point; sleeve[2].tex_coord = {1, 1};
        sleeve[3].position = bottomLeft.point; sleeve[3].tex_coord = {frontU, 1};
        if(rotateFront){
            for(int i=0;i<4;++i){
                const float x=(i==1||i==2)?1.f:0.f,y=i<2?0.f:1.f;
                const auto uv=CoverFaces::SidewaysUV(front.u0,front.u1,x,y,CoverFaces::Face::Front,reverseDefault(front,CoverFaces::Face::Front));
                sleeve[i].tex_coord={uv.u,uv.v};
            }
        }
        const Uint8 brightness = selected ? 255 : static_cast<Uint8>(225 - std::min(70.0f, std::abs(relative) * 22.0f));
        if (artwork.texture) {
            for (auto &vertex : sleeve) vertex.color = {brightness, brightness, brightness, 255};
        } else {
            const Color placeholder = Mix({100,135,153,255}, AccentFor(game.platform,preferences_.theme),.46f);
            sleeve[0].color = sleeve[1].color = {static_cast<Uint8>(std::min(255,placeholder.r+38)),
                                                 static_cast<Uint8>(std::min(255,placeholder.g+38)),
                                                 static_cast<Uint8>(std::min(255,placeholder.b+38)),255};
            sleeve[2].color = sleeve[3].color = {placeholder.r,placeholder.g,placeholder.b,255};
        }
        const int sleeveIndices[] = {0, 1, 2, 0, 2, 3};
        // Never paste the front cover through the case when its back faces us.
        if (frontFacing && !artwork.texture) SDL_RenderGeometry(renderer_, nullptr, sleeve, 4, sleeveIndices, 6);
        {
            const auto drawSide = [&](std::array<Transformed,4> corners,float u0,float u1,Uint8 shade,
                                      SDL_Texture *texture,bool rotateTexture=false,
                                      float sourceU0=0.f,float sourceU1=1.f,
                                      CoverFaces::Face sourceFace=CoverFaces::Face::Front,bool defaultSleeve=false) {
                // Projected winding handles the hinge panels and perspective at
                // grazing angles; hidden artwork must not draw through the case.
                const auto a=corners[0].point,b=corners[1].point,c=corners[2].point;
                if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<=0.f) return;
                std::array<CoverProjection::Point,4> projected{};
                for(int i=0;i<4;++i)
                    projected[i]={corners[i].point.x,corners[i].point.y,camera-corners[i].depth};
                std::array<SDL_Vertex,CoverProjection::kMaxVertices> side;
                std::array<int,CoverProjection::kMaxIndices> indices;
                int count=0;
                const int indexCount=CoverProjection::Build(projected,u0,u1,side.data(),indices.data(),count);
                for(int i=0;i<count;++i){
                    side[i].color={shade,shade,shade,255};
                    if(rotateTexture){const auto old=side[i].tex_coord;
                        const auto uv=CoverFaces::SidewaysUV(sourceU0,sourceU1,old.x,old.y,sourceFace,defaultSleeve);
                        side[i].tex_coord={uv.u,uv.v};}
                }
                SDL_RenderGeometry(renderer_,texture,side.data(),count,indices.data(),indexCount);
            };
            const auto rails=CaseSleeve::Rails(profile.widthMm,profile.thicknessMm);
            for(std::size_t panel=0;panel+1<rails.size();++panel) {
                const auto &left=rails[panel],&right=rails[panel+1];
                const auto face=panel==0?CoverFaces::Face::Back:panel==4?CoverFaces::Face::Front:CoverFaces::Face::Spine;
                const auto selectedFace=selectFace(face);
                const auto texture=textures_.GetCover(selectedFace.path,selected);
                if(!texture.texture)continue;
                const bool rotateTexture=rotateSleeve(selectedFace) && face!=CoverFaces::Face::Spine;
                const float start=panel==0?0.f:panel==4?geometryFrontStart:geometryBackEnd;
                const float end=panel==0?geometryBackEnd:panel==4?1.f:geometryFrontStart;
                constexpr float h=CaseSleeve::kHalfHeight;
                const float aspect=(face==CoverFaces::Face::Spine?profile.thicknessMm:profile.widthMm)/profile.heightMm;
                float sourceAspect=texture.width*(selectedFace.u1-selectedFace.u0)/std::max(1,texture.height);
                if(rotateTexture && sourceAspect>0)sourceAspect=1.f/sourceAspect;
                const auto fit=CoverFaces::Contain(sourceAspect,aspect);
                if(fit.x0>0 || fit.y0>0)
                    drawSide({transform(left.x,h,left.z),transform(right.x,h,right.z),
                              transform(right.x,-h,right.z),transform(left.x,-h,left.z)},0,1,28,nullptr);
                const float partLeft=(left.u-start)/(end-start),partRight=(right.u-start)/(end-start);
                const float a=std::max(partLeft,fit.x0),b=std::min(partRight,fit.x1);
                if(b<=a)continue;
                const auto point=[&](float x,float y) {
                    const float t=(x-partLeft)/(partRight-partLeft);
                    return transform(left.x+(right.x-left.x)*t,h*(1-2*y),left.z+(right.z-left.z)*t);
                };
                const auto uv=[&](float u){return selectedFace.u0+(u-fit.x0)/(fit.x1-fit.x0)*(selectedFace.u1-selectedFace.u0);};
                if(rotateTexture) {
                    const float localA=(a-fit.x0)/(fit.x1-fit.x0),localB=(b-fit.x0)/(fit.x1-fit.x0);
                    drawSide({point(a,fit.y0),point(b,fit.y0),point(b,fit.y1),point(a,fit.y1)},
                             localA,localB,brightness,texture.texture,true,selectedFace.u0,selectedFace.u1,
                             face,reverseDefault(selectedFace,face));
                } else {
                    drawSide({point(a,fit.y0),point(b,fit.y0),point(b,fit.y1),point(a,fit.y1)},
                             uv(a),uv(b),brightness,texture.texture);
                }
            }
        }
        if (frontFacing && artwork.texture && preferences_.skin == 1 && preferences_.reflections) {
            const auto lerpPoint=[](SDL_FPoint a,SDL_FPoint b,float amount){
                return SDL_FPoint{a.x+(b.x-a.x)*amount,a.y+(b.y-a.y)*amount};
            };
            SDL_Vertex gloss[4]{};
            gloss[0].position=topLeft.point; gloss[1].position=topRight.point;
            gloss[2].position=lerpPoint(topRight.point,bottomRight.point,.48f);
            gloss[3].position=lerpPoint(topLeft.point,bottomLeft.point,.48f);
            gloss[0].color={255,255,255,selected?Uint8(70):Uint8(38)};
            gloss[1].color={255,255,255,selected?Uint8(45):Uint8(25)};
            gloss[2].color={255,255,255,0}; gloss[3].color={255,255,255,0};
            SDL_RenderGeometry(renderer_,nullptr,gloss,4,sleeveIndices,6);
        }
        if (selected && frontFacing) {
            SetColor(renderer_, preferences_.skin==1?Color{242,253,255,235}:accent);
            const SDL_FPoint outline[] = {topLeft.point, topRight.point, bottomRight.point, bottomLeft.point, topLeft.point};
            SDL_RenderDrawLinesF(renderer_, outline, 5);
        }
        if (game.favorite) {
            const SDL_Point star = CaseStar(rect, view, pitch == 0.0f && roll == 0.0f);
            DrawFavoriteBadge(star.x,star.y);
        }
        DrawInstallBadge(game,rect);
    }
    void DrawCoverInspect(Color accent) {
        const GameEntry *game=Selected();
        if(!game)return;
        const CoverInspectPose pose=CoverInspect(coverInspectProgress_,coverInspectBackProgress_);
        const auto interpolate=[&](int from,int to){return static_cast<int>(from+(to-from)*pose.travel);};
        if(!selectedBackground_.texture || preferences_.backgroundTransparency==100) {
            SetColor(renderer_,{20,31,39,static_cast<Uint8>(72.f*pose.travel)});
            SDL_Rect focusArea{0,96,kWidth,532};SDL_RenderFillRect(renderer_,&focusArea);
        }
        const auto origin=FocusCoverRect(),target=InspectCoverRect();
        const SDL_Rect rect{interpolate(origin.x,target.x),interpolate(origin.y,target.y),
                            interpolate(origin.w,target.w),interpolate(origin.h,target.h)};
        DrawBox(*game,rect,0.f,true,accent,pose.yaw,pose.pitch,pose.roll,true);
        const Uint8 panelAlpha=static_cast<Uint8>(210.f*pose.travel);
        const int hintWidth = std::max(580, TextWidth(coverInspectHint_,1) + 32);
        FillRoundedRect(renderer_,{640-hintWidth/2,590,hintWidth,36},16,
                        preferences_.skin==1?Color{236,246,250,panelAlpha}:Color{7,15,27,panelAlpha});
        DrawText(renderer_,preferences_.coverType==CoverPresentation::Box?coverInspectHint_:"HOLD TO ZOOM    B  RETURN",640,601,1,
                 preferences_.skin==1?Color{48,64,73,static_cast<Uint8>(255.f*pose.travel)}:
                                      Color{235,242,250,static_cast<Uint8>(255.f*pose.travel)},true);
    }
    // How covers sit around the selected one, from COVER LAYOUT and COVER SPACING.
    struct CoverStyle {
        float sideWidth, sideHeight; // rect of a resting side case
        float sideYaw;               // turn of the left side; the right mirrors it (or, on a shelf, shows its spine too)
        bool shelf;                  // both sides turn their spines to the viewer, like books
        float camera;                // perspective of a resting side case
        float gap;                   // between the selected case and its neighbours
        float reveal;                // share of a case's face the next case leaves uncovered
        float push, pushPower;       // resting depth: downward offset over the first four steps
    };
    float CoverZoomScale() const { return std::clamp(preferences_.coverZoom,CoverZoom::Min,CoverZoom::Max)/100.f; }
    int &PresentationLayout() {
        return preferences_.coverType==CoverPresentation::Thumbnails?preferences_.thumbnailLayout:preferences_.coverType==CoverPresentation::Logo?preferences_.logoLayout:
            preferences_.coverType==CoverPresentation::Media?preferences_.mediaLayout:preferences_.coverLayout;
    }
    int PresentationLayout() const {
        return preferences_.coverType==CoverPresentation::Thumbnails?preferences_.thumbnailLayout:preferences_.coverType==CoverPresentation::Logo?preferences_.logoLayout:
            preferences_.coverType==CoverPresentation::Media?preferences_.mediaLayout:preferences_.coverLayout;
    }
    float CoverCenterY() const { return std::max(312.f,314.f*CoverZoomScale()*.5f+10.f); }
    SDL_Rect FocusCoverRect() const {
        if(WallLayout())return GridFocusRect();
        if(preferences_.coverType==CoverPresentation::Thumbnails) {
            const int size=int(SkinSpec().tileSize*CoverZoomScale()/1.2f);
            return {SwitchSkin()?SkinSpec().rowX:640-size/2,SkinSpec().rowY-size/2,size,size};
        }
        if(preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==4){
            const int h=int(std::clamp(340.f*CoverZoomScale()/1.2f,220.f,345.f));
            return {640-int(h*.71f)/2,290-h/2,int(h*.71f),h};
        }
        const bool logo=preferences_.coverType==CoverPresentation::Logo;
        const bool wheel=logo && preferences_.logoLayout>=1 && preferences_.logoLayout<=4;
        const int w=int((logo?(wheel?250.f:340.f):preferences_.coverType==CoverPresentation::Media?314.f:220.f)*CoverZoomScale());
        const int h=int((logo?(wheel?110.f:150.f):314.f)*CoverZoomScale());
        const int cx=preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8?420:wheel&&preferences_.logoLayout<=2?(preferences_.logoLayout==1?1020:260):640;
        const int cy=preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8?350:wheel?(preferences_.logoLayout==3?190:preferences_.logoLayout==4?440:355):int(CoverCenterY());
        return {cx-w/2,cy-h/2,w,h};
    }
    SDL_Rect InspectCoverRect() const {
        const auto origin=FocusCoverRect();
        // Inspection must still move closer when the library is already enlarged.
        const float zoom=std::clamp(preferences_.inspectionZoom,2,8)*.5f;
        const int w=int(std::ceil(origin.w*zoom));
        const int h=int(std::ceil(origin.h*zoom));
        return {640-w/2,345-h/2,w,h};
    }
    CoverStyle CurrentCoverStyle() const {
        const int layout = std::clamp(preferences_.coverLayout, 0, 3);
        const float spacing = std::clamp((preferences_.coverSpacing-75)/25.f,0.f,2.f);
        const int lo=int(spacing),hi=std::min(2,lo+1);
        const auto spaced=[&](const float (&values)[4][3]){return values[layout][lo]+(values[layout][hi]-values[layout][lo])*(spacing-lo);};
        // Rows: CLASSIC FLOW, CAROUSEL, FLAT ROW, STACKED. Columns: COMPACT,
        // BALANCED, WIDE. COMPACT packs cases like books; WIDE leaves gaps.
        static constexpr float kYaw[4][3] = {{1.15f,.98f,.82f},{1.f,.75f,.55f},{.52f,.45f,.38f},{1.36f,1.3f,1.22f}};
        static constexpr float kGap[4][3] = {{12.f,22.f,32.f},{10.f,20.f,30.f},{10.f,20.f,30.f},{34.f,38.f,42.f}};
        static constexpr float kReveal[4][3] = {{.45f,.85f,1.3f},{.5f,.9f,1.3f},{.35f,.7f,1.1f},{.1f,.3f,.55f}};
        // A shelf case is sized by height alone (wide enough for SNES boxes), so
        // books keep their physical proportions standing side by side.
        static constexpr float kSize[4][2] = {{126.f,180.f},{112.f,160.f},{135.f,193.f},{260.f,280.f}};
        static constexpr float kPush[4][2] = {{9.f,1.f},{18.f,1.35f},{3.f,1.f},{0.f,1.f}};
        const bool shelf = layout == 3;
        CoverStyle style{kSize[layout][0], kSize[layout][1], spaced(kYaw), shelf,
                         shelf ? 6.f : CaseView::kCamera, spaced(kGap), spaced(kReveal),
                         kPush[layout][0], kPush[layout][1]};
        // GlassFlow's reflective cases read better turned a little further.
        if (preferences_.skin == 1 && !shelf) style.sideYaw = std::max(style.sideYaw, .52f);
        const float zoom=CoverZoomScale();
        style.sideWidth*=zoom;style.sideHeight*=zoom;style.gap*=zoom*preferences_.coverSpacing/100.f;style.push*=zoom;
        style.reveal*=preferences_.coverSpacing/100.f;
        return style;
    }
    // Horizontal extent of a case around its pivot, and the width of its side
    // face (spine or opening edge) turned toward the viewer.
    struct CoverSpan { float left, right, outer; };
    CoverSpan MeasureCover(const GameEntry &game, float width, float height, float yaw, float camera) const {
        const auto profile = CaseProfileForGame(game, libraries_);
        if (!preferences_.useBoxArt) { // DrawCover's flat fit
            const float half = profile.widthMm * .5f * std::min(width / std::max(135.f, profile.widthMm),
                                                                height / std::max(190.f, profile.heightMm));
            return {-half, half, 0.f};
        }
        const CaseView view({0, 0, static_cast<int>(width), static_cast<int>(height)}, profile, yaw, 0.f, 0.f, camera);
        const SDL_FRect box = view.Bounds();
        return {box.x - view.centerX, box.x + box.w - view.centerX, view.SideFaceWidth()};
    }
    struct PresentationPose { float x,y,w,h,yaw=0,roll=0; };
    PresentationPose PresentationCard(float relative) const {
        const auto origin=FocusCoverRect();
        const bool logo=preferences_.coverType==CoverPresentation::Logo,box=preferences_.coverType==CoverPresentation::Box;
        const int layout=PresentationLayout();
        const float d=std::abs(relative),side=relative<0?-1.f:1.f;
        const float focus=1.f-SmoothFade(std::min(1.f,d));
        const float spacing=preferences_.coverSpacing/100.f,zoom=CoverZoomScale();
        float scale=.65f+.35f*focus,x=relative*(logo?295.f:225.f)*spacing*zoom,y=0,yaw=0,roll=0;
        if(preferences_.coverType==CoverPresentation::Thumbnails) {
            scale=1; x=relative*(origin.w+16*spacing);y=0;
            if(layout==1 && d>=1) {scale=.63f;x=side*(origin.w*.85f+int((d-1)/2)*(origin.w*.65f+16*spacing));y=(std::fmod(d-1,2.f)-.5f)*(origin.h*.65f+16);}
        } else if(box && layout==2) {
            scale=.68f+.32f*focus;
            x=side*(origin.w*.5f+origin.w*.34f+8.f*zoom+(d-1.f)*(origin.w*.68f+8.f*zoom))*spacing;
            if(d<1.f)x*=d;
        } else if(logo && layout>=1 && layout<=4) {
            // The selected logo faces forward. Neighbours follow a 2D arc,
            // shrinking toward the ends without box perspective or back faces.
            const float angle=relative*.30f;
            scale=std::max(.15f,1.f-d*.22f);
            const float along=std::sin(angle)*300.f*spacing;
            const float curve=(1.f-std::cos(angle))*(layout<=2?220.f:140.f);
            if(layout<=2){x=(layout==1?1.f:-1.f)*curve;y=along;roll=(layout==1?-angle:angle);}
            else {x=along*1.8f;y=(layout==3?-1.f:1.f)*curve;}
        } else {
            const bool wall=(box&&layout==7)||(logo&&layout==6)||(!box&&!logo&&layout==3);
            const bool arc=(box&&layout==5)||(logo&&layout==5)||(!box&&!logo&&layout==1);
            const bool fan=(box&&layout==4)||(!box&&!logo&&layout==2);
            if(wall && d>=1.f) {
                const float slot=d-1.f;const int column=int(slot/2.f);
                const float row=std::fmod(slot,2.f);
                x=side*((logo?325.f:255.f)+column*(logo?220.f:180.f))*spacing*zoom;
                y=(row-.5f)*(logo?150.f:210.f)*zoom;scale=logo?.62f:.56f;
            } else if(arc) {
                x=std::sin(relative*.34f)*570.f*spacing*zoom;
                y=(1.f-std::cos(relative*.34f))*135.f*zoom;
                scale=std::max(.36f,1.f-d*.16f);yaw=box?-relative*.22f:0;
            } else if(fan) {
                // One radial row, as in the video: cases turn with the arc,
                // while focus pulls a case forward and enlarges it in its slot.
                const float angle=relative*.095f*std::clamp(spacing,.65f,1.5f);
                const float radius=2000.f*origin.w/241.f;
                scale=.80f+.20f*focus;
                x=std::sin(angle)*radius;
                y=(1.f-std::cos(angle))*radius+16.f*(1.f-focus);
                roll=box?-angle:0;
                yaw=box?std::clamp(.18f-relative*.025f,.05f,.30f):0;
            } else if(box&&layout==6){scale=.88f+.12f*focus;x=relative*255.f*spacing*zoom;}
        }
        return {origin.x+origin.w*.5f+x,origin.y+origin.h*.5f+y,origin.w*scale,origin.h*scale,yaw,roll};
    }
    // A real stack of cases in one world/camera. The translation between cases
    // is their physical thickness, so faces occlude each other and only the
    // spines form the diagonal shelf. Every case stands on the same floor.
    void DrawBookshelf(Color accent) {
        bookshelfFaces_=bookshelfVertices_=0;
        const int count=int(visible_.size());if(!count)return;
        constexpr float yaw=1.f,pitch=.65f,camera=2.5f;
        const float cy=std::cos(yaw),cp=std::cos(pitch);
        const float pixels=198.3333f*CoverZoomScale(),screenX=750.f+tabSlide_,screenY=450.f;
        const float position=preferences_.wrapNavigation?visualSelected_:std::clamp(visualSelected_,0.f,float(count-1));
        const int base=int(std::floor(position));const float fraction=position-base;
        const bool hidden=detailsProgress_>.015f || coverInspectProgress_>.015f || showcaseProgress_>0 || launchCapture_;
        const bool resting=std::abs(position-std::round(position))<.00001f && tabSlide_==0;
        const std::string cacheKey=std::to_string(bookshelfEpoch_)+":"+std::to_string(position)+":"+std::to_string(selected_)+":"+
            std::to_string(preferences_.coverZoom)+":"+std::to_string(preferences_.coverSpacing)+":"+
            std::to_string(preferences_.reflections)+":"+std::to_string(preferences_.skin)+":"+std::to_string(preferences_.theme)+":"+
            std::to_string(hidden)+":"+std::to_string(preferences_.wrapNavigation)+":"+std::to_string(Selected()->favorite);
        if(resting && bookshelfCached_ && bookshelfTexture_ && bookshelfKey_==cacheKey &&
           bookshelfRevision_==textures_.Revision() && !bookshelfNeedsArtwork_) {
            SDL_RenderCopy(renderer_,bookshelfTexture_,nullptr,nullptr);++bookshelfCacheHits_;return;
        }
        if(!bookshelfTexture_) {
            bookshelfTexture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,kWidth,kHeight);
            if(bookshelfTexture_) {
                const auto blend=SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,
                    SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
                if(SDL_SetTextureBlendMode(bookshelfTexture_,blend)!=0){SDL_DestroyTexture(bookshelfTexture_);bookshelfTexture_=nullptr;}
            }
        }
        SDL_Texture *previousTarget=SDL_GetRenderTarget(renderer_);
        SDL_Rect oldViewport,oldClip;SDL_RenderGetViewport(renderer_,&oldViewport);SDL_RenderGetClipRect(renderer_,&oldClip);
        const bool wasClipped=SDL_RenderIsClipEnabled(renderer_);
        const bool capture=resting && bookshelfTexture_ && SDL_SetRenderTarget(renderer_,bookshelfTexture_)==0;
        if(capture){SDL_RenderSetViewport(renderer_,nullptr);SDL_RenderSetClipRect(renderer_,nullptr);SetColor(renderer_,{0,0,0,0});SDL_RenderClear(renderer_);}
        ++bookshelfBuilds_;
        const auto actual=[&](int i){return (i%count+count)%count;};
        const auto profile=[&](int i){return CaseProfileForGame(games_[visible_[actual(i)]],libraries_);};
        // Keep actual cartons/disc cases touching at normal spacing. The slider
        // adds a small physical gap instead of spreading independent billboards.
        const float gap=(preferences_.coverSpacing-50)/50.f*.006f+.001f;
        const auto step=[&](int a,int b){return (profile(a).thicknessMm+profile(b).thicknessMm)/380.f+gap;};
        struct Book {int index;float relative,z,focus;CaseProfile profile;CaseScene scene;float yaw,pitch,roll;bool clear;};
        std::vector<Book> books;
        const int first=std::max(base-64,preferences_.wrapNavigation?int(std::ceil(position-count*.5f)):0);
        const int last=std::min(base+48,preferences_.wrapNavigation?first+count-1:count-1);
        float z=-fraction*step(base,base+1);
        for(int i=base;i>first;--i)z-=step(i-1,i);
        const auto origin=FocusCoverRect();
        for(int i=first;i<=last;++i) {
            const auto p=profile(i);const float relative=i-position;
            const float focus=SmoothFade(std::max(0.f,1.f-std::abs(relative)));
            const auto pose=BookshelfCasePose(p,z,focus,pixels,screenX,screenY,origin,314.f*CoverZoomScale());
            const auto &scene=pose.scene;const float turn=pose.yaw,tilt=pose.pitch,roll=pose.roll;
            if(scene.z+p.widthMm/190.f<camera-.15f) {
                const CaseView view({0,0,220,314},p,turn,tilt,roll,camera,&scene);const auto bounds=view.Bounds();
                if(bounds.x<kWidth+180 && bounds.x+bounds.w>0 && bounds.y<630 && bounds.y+bounds.h>95)
                    books.push_back({actual(i),relative,z,focus,p,scene,turn,tilt,roll,pose.clear});
            }
            if(i<last)z+=step(i,i+1);
        }
#ifdef UFLOW_HOST_PREVIEW
        carouselCards_.clear();for(const auto &book:books)carouselCards_.push_back({book.index,book.relative});
#endif
        // Cases on the near side of the camera must reverse painter order.
        // These are disjoint slabs along the shelf axis, not screen-space cards.
        const float eyeZ=camera*cy*cp;
        std::stable_sort(books.begin(),books.end(),[&](const Book&a,const Book&b){
            // A returning case participates in shelf occlusion as soon as it
            // is aligned with its slot, not only after its motion has stopped.
            if(a.clear!=b.clear)return !a.clear;
            return a.clear?a.scene.z<b.scene.z:std::abs(a.z-eyeZ)>std::abs(b.z-eyeZ);
        });
        const auto draw=[&](const Book &book,bool reflection) {
            const auto &game=games_[visible_[book.index]];const auto &p=book.profile;
            const bool selected=book.index==int(selected_);
            if(selected && (detailsProgress_>.015f || coverInspectProgress_>.015f || showcaseProgress_>0 || launchCapture_))return;
            const CaseView view({0,0,220,314},p,book.yaw,book.pitch,book.roll,camera,&book.scene);
            const float panel=p.sidewaysSleeve?p.heightMm:p.widthMm,total=panel*2+p.thicknessMm;
            const auto face=[&](CoverFaces::Face side){return CoverFaces::Resolve(game,side,panel/total,(panel+p.thicknessMm)/total,
                [&](const auto &path){return textures_.GetCover(path,selected).texture!=nullptr;});};
            struct Face {float xyz[4][3];CoverFaces::Face side;int shade;bool art;};
            const Face faces[]={
                {{{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},CoverFaces::Face::Spine,195,false},
                {{{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f}},CoverFaces::Face::Spine,160,false},
                {{{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f}},CoverFaces::Face::Back,225,true},
                {{{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f}},CoverFaces::Face::Spine,245,true},
                {{{.5f,.5f,.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f}},CoverFaces::Face::Spine,160,false},
                {{{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}},CoverFaces::Face::Front,255,true}
            };
            for(const auto &f:faces) {
                if(reflection && !f.art)continue;
                std::array<CoverProjection::Point,4> projected;
                for(int j=0;j<4;++j) {
                    auto point=view(f.xyz[j][0],f.xyz[j][1],f.xyz[j][2]);
                    if(reflection) {
                        const float y=(screenY-point.point.y)*(camera-point.depth)/(pixels*camera),z=point.depth;
                        const float ry=-std::cos(2*pitch)*y-std::sin(2*pitch)*z;
                        const float rz=-std::sin(2*pitch)*y+std::cos(2*pitch)*z;
                        const float x=(point.point.x-screenX)*(camera-z)/(pixels*camera);
                        point={{screenX+x*pixels*camera/(camera-rz),screenY-ry*pixels*camera/(camera-rz)},rz};
                    }
                    projected[j]={point.point.x,point.point.y,camera-point.depth};
                }
                const auto &a=projected[0],&b=projected[1],&c=projected[2];
                const float cross=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
                if(reflection?cross>=0:cross<=0)continue;
                const auto art=f.art?face(f.side):CoverFaces::Selection{};const auto texture=f.art?textures_.GetCover(art.path,selected):TextureInfo{};
                std::array<SDL_Vertex,81> vertices;std::array<int,384> indices;
                int n=0;const int ni=CoverProjection::Build(projected,0.f,1.f,vertices.data(),indices.data(),n,reflection?1:book.focus>0?8:f.art?4:1);
                ++bookshelfFaces_;bookshelfVertices_+=n;
                const bool turn=texture.texture && f.side!=CoverFaces::Face::Spine &&
                    CoverFaces::TurnSleeve(p.sidewaysSleeve,p.sidewaysDefaultSleeve,art.sleeve,art.path==game.defaultSleevePath,texture.width,texture.height);
                for(int j=0;j<n;++j) {
                    const auto uv=vertices[j].tex_coord;
                    const Uint8 alpha=reflection?Uint8(28.f*uv.y*uv.y):255;
                    vertices[j].color=texture.texture?SDL_Color{Uint8(f.shade),Uint8(f.shade),Uint8(f.shade),alpha}:SDL_Color{35,66,78,alpha};
                    if(turn) {const auto t=CoverFaces::SidewaysUV(art.u0,art.u1,uv.x,uv.y,f.side,art.path==game.defaultSleevePath && (p.sidewaysDefaultSleeve || f.side==CoverFaces::Face::Back));vertices[j].tex_coord={t.u,t.v};}
                    else vertices[j].tex_coord={art.u0+uv.x*(art.u1-art.u0),uv.y};
                }
                SDL_RenderGeometry(renderer_,texture.texture,vertices.data(),n,indices.data(),ni);
            }
            if(selected && !reflection){
                launchOrigin_=origin;launchYaw_=book.yaw;launchPitch_=book.pitch;launchRoll_=book.roll;
                if(game.favorite){const auto b=view.Bounds();DrawFavoriteBadge(int(b.x+b.w)-34,int(b.y)+8);}
                DrawInstallBadge(game,origin);
            }
        };
        if(preferences_.reflections)for(const auto &book:books)draw(book,true);
        for(const auto &book:books)draw(book,false);
        if(capture) {
            SDL_SetRenderTarget(renderer_,previousTarget);SDL_RenderSetViewport(renderer_,&oldViewport);SDL_RenderSetClipRect(renderer_,wasClipped?&oldClip:nullptr);
            SDL_RenderCopy(renderer_,bookshelfTexture_,nullptr,nullptr);
            bookshelfKey_=cacheKey;bookshelfRevision_=textures_.Revision();bookshelfNeedsArtwork_=textures_.Pending()!=0;bookshelfCached_=true;
        }
    }
    void DrawPresentationCarousel(Color accent) {
        if(WallLayout()){DrawWall(accent);return;}
        const int count=static_cast<int>(visible_.size());if(!count)return;
        struct Card{int index;float relative;PresentationPose pose;};std::vector<Card> cards;
        const float position=preferences_.wrapNavigation?visualSelected_:std::clamp(visualSelected_,0.f,float(count-1));
        const int base=int(std::floor(position));
        const bool fan=preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==4;
        const int reach=fan?7:preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8?24:8;
        for(int i=base-reach;i<=base+reach;++i) {
            if(!preferences_.wrapNavigation && (i<0||i>=count))continue;
            const float relative=i-position;
            if(preferences_.coverType==CoverPresentation::Logo && preferences_.logoLayout>=1 && preferences_.logoLayout<=4 && std::abs(relative)>=4.f)continue;
            if(preferences_.wrapNavigation && (relative< -count*.5f || relative>=count*.5f))continue;
            const auto a=PresentationCard(float(i-base)),b=PresentationCard(float(i-base-1));
            const float t=position-base;const auto mix=[&](float x,float y){return x+(y-x)*t;};
            cards.push_back({(i%count+count)%count,relative,{mix(a.x,b.x),mix(a.y,b.y),mix(a.w,b.w),mix(a.h,b.h),mix(a.yaw,b.yaw),mix(a.roll,b.roll)}});
        }
        std::sort(cards.begin(),cards.end(),[](const Card &a,const Card &b){return std::abs(a.relative)>std::abs(b.relative);});
#ifdef UFLOW_HOST_PREVIEW
        carouselCards_.clear();for(const auto &card:cards)carouselCards_.push_back({card.index,card.relative});
#endif
        SDL_Rect previousClip{};const bool wasClipped=SDL_RenderIsClipEnabled(renderer_);SDL_RenderGetClipRect(renderer_,&previousClip);
        if(fan){SDL_Rect clip{0,105,1280,515};if(wasClipped)SDL_IntersectRect(&clip,&previousClip,&clip);SDL_RenderSetClipRect(renderer_,&clip);}
        sideCaseFrame_=sideCaseUse_;
        for(const auto &card:cards) {
            const bool focused=card.index==int(selected_);
            if(focused && (detailsProgress_>.015f || coverInspectProgress_>.015f || showcaseProgress_>0.f))continue;
            const auto &p=card.pose;if(fan&&(p.x < -100.f||p.x>1380.f))continue;SDL_Rect rect{int(p.x+tabSlide_-p.w*.5f),int(p.y-p.h*.5f),int(p.w),int(p.h)};
            const auto &game=games_[visible_[card.index]];
            if(preferences_.coverType!=CoverPresentation::Box)DrawPresentation(game,rect,focused,accent,preferences_.coverType==CoverPresentation::Logo && preferences_.logoLayout>=1 && preferences_.logoLayout<=4?std::clamp(4.f-std::abs(card.relative),0.f,1.f):1.f,p.roll);
            else if(preferences_.useBoxArt && preferences_.coverLayout!=2)DrawBox(game,rect,card.relative,focused,accent,p.yaw,preferences_.coverLayout==4?-.07f:0.f,p.roll,true);
            else DrawCover(game,rect,focused,accent);
        }
        if(fan)SDL_RenderSetClipRect(renderer_,wasClipped?&previousClip:nullptr);
    }
    void DrawCarousel(Color accent) {
        if(preferences_.coverType==CoverPresentation::Box && preferences_.coverLayout==8){DrawBookshelf(accent);DrawCarouselTitle(accent);return;}
        if(preferences_.coverType!=CoverPresentation::Box || preferences_.coverLayout==2 || preferences_.coverLayout>=4) {
            DrawPresentationCarousel(accent);DrawCarouselTitle(accent);return;
        }
        constexpr float kPi = 3.14159265358979323846f;
        const float focusWidth = 220.f*CoverZoomScale(), focusHeight = 314.f*CoverZoomScale();
        const CoverStyle style = CurrentCoverStyle();
        const float rightYaw = style.shelf ? kPi - style.sideYaw : -style.sideYaw;
        const int count = static_cast<int>(visible_.size());
        if(!count)return;
        const auto actual=[&](int index){return (index%count+count)%count;};
        const float reach = kWidth * .5f + std::abs(tabSlide_);
        // Resting offsets from the screen centre with `center` selected. Cases are
        // placed outward, each leaving its side face plus `reveal` of its face
        // uncovered by the nearer one, so mixed case sizes pack like books.
        struct Rest { int first; std::vector<float> offsets; };
        const auto rest = [&](int center) {
            const CoverSpan focus = MeasureCover(games_[visible_[actual(center)]], focusWidth, focusHeight, 0.f, CaseView::kCamera);
            std::vector<float> sides[2];
            for (int side = 0; side < 2; ++side) {
                const int direction = side == 0 ? -1 : 1;
                const float yaw = side == 0 ? style.sideYaw : rightYaw;
                CoverSpan previous{};
                float x = 0.f;
                // Two past the screen edge, so neighbouring rests share every visible case.
                for (int index = center + direction, beyond = 0; (preferences_.wrapNavigation || (index >= 0 && index < count)) && beyond < 2 &&
                     sides[side].size() < static_cast<std::size_t>(std::min(64,count-1)); index += direction) {
                    const CoverSpan span = MeasureCover(games_[visible_[actual(index)]], style.sideWidth, style.sideHeight, yaw, style.camera);
                    const float reveal = span.outer + style.reveal * (span.right - span.left - span.outer);
                    if (sides[side].empty()) x = side == 0 ? focus.left - style.gap - span.right : focus.right + style.gap - span.left;
                    else x += side == 0 ? previous.left - span.left - reveal : previous.right - span.right + reveal;
                    sides[side].push_back(x);
                    previous = span;
                    if (side == 0 ? x + span.right < -reach : x + span.left > reach) ++beyond;
                }
            }
            Rest result{center - static_cast<int>(sides[0].size()), std::vector<float>(sides[0].rbegin(), sides[0].rend())};
            result.offsets.push_back(0.f);
            result.offsets.insert(result.offsets.end(), sides[1].begin(), sides[1].end());
            return result;
        };
        // Between two resting views every case moves straight to its next place.
        const float position = preferences_.wrapNavigation?visualSelected_:std::clamp(visualSelected_, 0.f, static_cast<float>(count - 1));
        const int base = static_cast<int>(std::floor(position));
        const float blend = position - base;
        const Rest from = rest(base), to = blend > 0.f && (preferences_.wrapNavigation || base + 1 < count) ? rest(base + 1) : from;
        struct Card { int index; float relative, offset; };
        std::vector<Card> cards;
        const int last = std::min(from.first + static_cast<int>(from.offsets.size()),
                                  to.first + static_cast<int>(to.offsets.size())) - 1;
        for (int index = std::max(from.first, to.first); index <= last; ++index) {
            const float start = from.offsets[index - from.first], end = to.offsets[index - to.first];
            cards.push_back({index, static_cast<float>(index) - position, start + (end - start) * blend});
        }
        std::sort(cards.begin(), cards.end(), [](const Card &left, const Card &right) {
            return std::abs(left.relative) > std::abs(right.relative);
        });
        sideCaseFrame_ = sideCaseUse_;
#ifdef UFLOW_HOST_PREVIEW
        carouselCards_.clear();
        for(const auto &card:cards) carouselCards_.push_back({actual(card.index),card.relative});
#endif
        // Keep the entire zoomed case and its reflection; a fixed shelf clip
        // cuts off the top and bottom as soon as zoom exceeds the normal size.
        for (const Card &card : cards) {
            if (actual(card.index) == static_cast<int>(selected_) && (detailsProgress_ > .015f || coverInspectProgress_ > .015f || showcaseProgress_>0.f)) continue;
            const GameEntry &game = games_[visible_[actual(card.index)]];
            const float distance = std::abs(card.relative);
            float focus = std::max(0.0f, 1.0f - distance);
            focus = focus * focus * (3.0f - 2.0f * focus);
            const int width = static_cast<int>(style.sideWidth + (focusWidth - style.sideWidth) * focus);
            const int height = static_cast<int>(style.sideHeight + (focusHeight - style.sideHeight) * focus);
            const float depthPush = std::pow(std::min(4.0f, distance), style.pushPower) * style.push;
            const float smoothX = 640.0f + tabSlide_ + card.offset - width / 2.0f;
            const int x = static_cast<int>(std::floor(smoothX));
            const bool selected = actual(card.index) == static_cast<int>(selected_) && focus > .78f;
            if (!preferences_.useBoxArt) {
                DrawCover(game, {x, static_cast<int>(CoverCenterY() - height / 2.0f + depthPush), width, height}, selected, accent);
                continue;
            }
            const float yaw = std::min(1.0f, distance) * (card.relative < 0 ? style.sideYaw : rightYaw) +
                              (selected ? CaseIdleYaw() : 0.0f);
            const float camera = style.camera + (CaseView::kCamera - style.camera) * focus;
            float settle = 0.0f;
            if (style.shelf) {
                // Books stand on one line, where a DVD case's bottom would be,
                // instead of short boxes floating at mid-height.
                const auto bottom = [&](const CaseProfile &profile) {
                    const SDL_FRect box = CaseView({0, 0, width, height}, profile, yaw, 0.f, 0.f, camera).Bounds();
                    return box.y + box.h;
                };
                settle = bottom(CaseProfile{}) - bottom(CaseProfileForGame(game, libraries_));
            }
            const float smoothY = CoverCenterY() - height / 2.0f + depthPush + settle;
            const int y = static_cast<int>(std::floor(smoothY));
            DrawBox(game, {x, y, width, height}, card.relative, selected, accent, yaw, 0.0f, 0.0f, true, camera,
                    smoothX-x,smoothY-y);
        }
        DrawCarouselTitle(accent);
    }
    void DrawCarouselTitle(Color accent) {
        if(SwitchSkin())return;
        if(visible_.empty())return;
        const auto &game = games_[visible_[selected_]];
        if (video_.Ready() && videoOptions_.mode==VideoMode::Background) {
            if(preferences_.skin==1) DrawGlassSurface(renderer_,{242,511,796,88},18,accent,92,true);
            else FillRoundedRect(renderer_,{242,511,796,88},18,{5,10,20,205});
        }
        FillRoundedRect(renderer_, {250,505,780,3}, 1, {accent.r,accent.g,accent.b,preferences_.skin==1?Uint8(72):Uint8(105)});
        const TextureInfo logo=preferences_.useLogos && preferences_.coverType!=CoverPresentation::Logo?textures_.Get(game.logoPath):TextureInfo{};
        if(logo.texture){
            const float ratio=static_cast<float>(logo.width)/std::max(1,logo.height);
            SDL_Rect logoRect={490,516,300,54};
            if(ratio>static_cast<float>(logoRect.w)/logoRect.h){logoRect.h=static_cast<int>(logoRect.w/ratio);logoRect.y+=(54-logoRect.h)/2;}
            else{logoRect.w=static_cast<int>(logoRect.h*ratio);logoRect.x+=(300-logoRect.w)/2;}
            SDL_RenderCopy(renderer_,logo.texture,nullptr,&logoRect);
            DrawText(renderer_, Ellipsize(game.name,64), 640, 574, 2,
                     preferences_.skin==1?Color{48,58,66,255}:Color{246,249,255,255}, true);
        }else DrawText(renderer_, Ellipsize(game.name,50), 640, 525, 3,
                       preferences_.skin==1?Color{48,58,66,255}:Color{246,249,255,255}, true);
    }
    const char *ActionButton(ControlAction action) const {
        static std::array<std::string,16> labels;static unsigned next=0;auto &label=labels[next++%labels.size()];label=ControlBindingLabel(preferences_,action);return label.c_str();
    }
    void UpdateControlHints() {
        // Compare the live bindings so capture, reset and loaded settings all
        // refresh immediately. Do not rebuild strings/measure text each frame.
        if (hintSkin_ == preferences_.skin &&
            std::equal(hintBindings_.begin(), hintBindings_.end(), std::begin(preferences_.bindings))) return;
        detailsBackdropReady_ = false;
        hintSkin_ = preferences_.skin;
        std::copy(std::begin(preferences_.bindings), std::end(preferences_.bindings), hintBindings_.begin());
        constexpr ControlAction actions[] = {ControlAction::Launch, ControlAction::Details,
            ControlAction::Favorite, ControlAction::PreviousLibrary, ControlAction::Settings, ControlAction::Sort};
        constexpr const char *labels[] = {"LAUNCH", "DETAILS", "FAVORITE", "FILTER", "SETTINGS", "FIND"};
        constexpr int positions[] = {58,238,428,638,918,1100,1248};
        for (std::size_t i = 0; i < footerHints_.size(); ++i) {
            auto &hint = footerHints_[i];
            hint.button = ActionButton(actions[i]);
            if (actions[i] == ControlAction::PreviousLibrary)
                hint.button += std::string("/") + ActionButton(ControlAction::NextLibrary);
            hint.action = actions[i]==ControlAction::Sort && (!searchQuery_.empty()||alphabetFilter_)?"CLEAR":labels[i]; hint.x = positions[i]; hint.width = positions[i+1] - positions[i] - 16;
            hint.buttonWidth = TextWidth(hint.button,2); hint.gap = TextWidth("  ",2);
            hint.stacked = hint.buttonWidth + hint.gap + TextWidth(hint.action,2) > hint.width;
        }
        favoriteHint_ = std::string("MARK A GAME WITH ") + ActionButton(ControlAction::Favorite);
        coverInspectHint_ = std::string("HOLD ") + ActionButton(ControlAction::ZoomFront) +
            "  FRONT    HOLD " + ActionButton(ControlAction::ZoomBack) + "  BACK    B  RETURN";
    }
    void DrawAlphabet(Color accent) {
        const float opacity=alphabetProgress_;
        const auto fade=[&](Color c){c.a=static_cast<Uint8>(c.a*opacity);return c;};
        SetColor(renderer_,fade({0,0,0,165}));SDL_RenderFillRect(renderer_,nullptr);
        const bool glass=preferences_.skin==1;
        if(glass)DrawGlassSurface(renderer_,{80,305,1120,110},24,accent,static_cast<int>(80*opacity),true);
        else FillRoundedRect(renderer_,{80,305,1120,110},22,fade({10,19,32,230}));
        const int first=preferences_.alphabetFilterMode?0:1;
        const int start=108+(preferences_.alphabetFilterMode?0:19);
        const int highlight=start+static_cast<int>((alphabetVisual_-first)*38);
        FillRoundedRect(renderer_,{highlight,331,38,54},12,fade({accent.r,accent.g,accent.b,180}));
        for(int i=first;i<28;++i){
            const bool active=i==alphabetChoice_;
            const auto ink=active?Color{255,255,255,255}:alphabetCounts_[i]?Color{235,244,250,240}:Color{154,164,174,150};
            DrawText(renderer_,AlphabetLabel(i),start+(i-first)*38+19,active?340:346,i==0?1:active?4:3,fade(ink),true);
        }
        DrawText(renderer_,preferences_.alphabetFilterMode?"FILTER BY LETTER":"BROWSE BY LETTER",640,264,3,fade({245,249,255,255}),true);
        DrawText(renderer_,AlphabetLabel(alphabetChoice_)+" / "+std::to_string(alphabetCounts_[alphabetChoice_])+" GAMES",640,434,2,fade({245,249,255,255}),true);
        DrawFittedText(renderer_,alphabetHeldMode_?std::string("LEFT STICK / D-PAD MOVE   RELEASE ")+ActionButton(ControlAction::Alphabet)+" APPLY   "+(AlphabetConsumedBinding()==0x40000?"X":"L3")+" MODE":"LEFT STICK / D-PAD MOVE   A APPLY   X MODE   B CANCEL",110,474,1060,2,fade({245,249,255,255}));
        DrawText(renderer_,"NO MOVEMENT: CANCEL",640,508,1,fade({220,230,240,255}),true);
    }
    void DrawSearch(Color accent) {
        const bool glass=preferences_.skin==1;
        SetColor(renderer_,{0,0,0,135});SDL_RenderFillRect(renderer_,nullptr);
        FillRoundedRect(renderer_,{300,160,680,430},24,glass?Color{235,243,246,248}:Color{10,19,32,250});
        if(glass)DrawGlassSurface(renderer_,{300,160,680,430},24,accent,100,true);
        const Color ink=glass?Color{45,56,64,255}:Color{238,244,252,255};
        DrawText(renderer_,"SEARCH / "+LibraryLabel(CurrentLibraryId()),340,185,3,ink);
        FillRoundedRect(renderer_,{340,240,600,44},10,{accent.r,accent.g,accent.b,55});
        DrawText(renderer_,Ellipsize(searchBuffer_,38)+"_",355,250,2,ink);
        constexpr const char *keys="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 '-.";
        for(int i=0;keys[i];++i){
            const int x=340+i%10*60,y=300+i/10*40;
            if(i==searchKey_)FillRoundedRect(renderer_,{x,y,54,34},8,{accent.r,accent.g,accent.b,130});
            DrawText(renderer_,keys[i]==' '?"SP":std::string(1,keys[i]),x+27,y+8,2,ink,true);
        }
        constexpr const char *actions[]={"X ERASE","Y CLEAR","+ SEARCH","B CANCEL"};
        for(int i=0;i<4;++i){FillRoundedRect(renderer_,{340+i*150,490,144,44},10,{accent.r,accent.g,accent.b,65});DrawText(renderer_,actions[i],412+i*150,504,1,ink,true);}
        DrawText(renderer_,"STICK / D-PAD MOVE   A TYPE   + SEARCH",640,553,1,ink,true);
    }
    void DrawSort(Color accent) {
        if(SwitchSkin()) {
            accent=SwitchAccent();SetColor(renderer_,SwitchBackground());SDL_RenderFillRect(renderer_,nullptr);
            const auto ink=SwitchInk();DrawText(renderer_,"Sort Games",60,70,4,ink);
            SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,40,125,1240,125);SDL_RenderDrawLine(renderer_,40,630,1240,630);
            for(int row=0;row<LibrarySort::Count;++row){const int y=190+row*48;SDL_Rect r{410,y,460,42};
                SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,410,y+43,870,y+43);
                if(row==sortChoice_){SetColor(renderer_,accent);for(int j=0;j<3;++j){SDL_RenderDrawRect(renderer_,&r);++r.x;++r.y;r.w-=2;r.h-=2;}}
                DrawText(renderer_,LibrarySort::Name(row),430,y+10,2,row==sortChoice_?accent:ink);
                if(row==preferences_.sortMode)DrawText(renderer_,"*",838,y+10,2,accent);
            }
            DrawText(renderer_,"X  Search",430,550,2,ink);DrawText(renderer_,"Y  A-Z",680,550,2,ink);
            DrawText(renderer_,"B  Back     A  Apply",940,663,2,ink);return;
        }
        const bool glass=preferences_.skin==1;
        SetColor(renderer_,{0,0,0,135});SDL_RenderFillRect(renderer_,nullptr);
        if(glass){FillRoundedRect(renderer_,{390,110,500,490},24,{235,243,246,248});DrawGlassSurface(renderer_,{390,110,500,490},24,accent,100,true);}
        else FillRoundedRect(renderer_,{390,110,500,490},24,{10,19,32,250});
        const Color ink=glass?Color{45,56,64,255}:Color{238,244,252,255};
        DrawText(renderer_,"FIND / SORT",430,140,3,ink);
        for(int row=0;row<LibrarySort::Count;++row){
            const int y=190+row*48;
            if(row==sortChoice_)FillRoundedRect(renderer_,{410,y,460,42},10,{accent.r,accent.g,accent.b,95});
            DrawText(renderer_,LibrarySort::Name(row),430,y+10,2,ink);
            if(row==preferences_.sortMode)DrawText(renderer_,"*",838,y+10,2,ink);
        }
        DrawText(renderer_,"X SEARCH       Y A-Z",430,550,2,ink);
        DrawText(renderer_,std::string("A SORT   B BACK   LIBRARY: HOLD ")+ActionButton(ControlAction::Alphabet)+" FOR A-Z",430,580,1,ink);
    }
    void DrawFooter(Color accent) {
        if(SwitchSkin()){DrawSwitchFooter();return;}
        const bool glass = preferences_.skin == 1;
        if (glass)
            DrawGlassSurface(renderer_, {32,638,1216,52}, 23, accent, 88, true);
        else
            FillRoundedRect(renderer_,{38,626,1204,64},20,{5,10,20,205});
        for (std::size_t i = 0; i < footerHints_.size(); ++i) {
            const auto &hint = footerHints_[i];
            const Color ink = glass ? (i == 0 ? Color{68,78,87,255} : Color{104,115,124,255}) :
                                      (i == 0 ? Color{235,241,250,255} : Color{173,184,201,255});
            const int y = glass ? 653 : 648;
            // Long D-pad names use two lines, keeping the whole binding visible.
            DrawText(renderer_,hint.button,hint.x,hint.stacked ? (glass ? 641 : 636) : y,
                     hint.stacked ? 1 : 2,glass ? accent : ink);
            DrawText(renderer_,hint.action,hint.stacked ? hint.x : hint.x+hint.buttonWidth+hint.gap,
                     hint.stacked ? (glass ? 663 : 657) : y,2,ink);
        }
    }
    void DrawPanelBase(Color accent, int x, int width, float progress) {
        if(SwitchSkin()) {SetColor(renderer_,SwitchBackground());SDL_RenderFillRect(renderer_,nullptr);SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,40,160,1240,160);SDL_RenderDrawLine(renderer_,40,630,1240,630);return;}
        if (preferences_.skin == 1) {
            DrawGlassSurface(renderer_,{x,92,width,536},30,accent,102,true);
            return;
        }
        if(!selectedBackground_.texture || preferences_.backgroundTransparency==100 || detailsProgress_<=.01f || settingsProgress_>.01f) {
            SetColor(renderer_,{0,0,0,static_cast<Uint8>(190.0f*progress)}); SDL_RenderFillRect(renderer_,nullptr);
        }
        FillRoundedRect(renderer_,{x,92,width,536},28,{10,17,29,250});
        FillRoundedRect(renderer_,{x,92,8,536},4,accent);
    }
    bool BlurBackdropToLens() {
        // The caller seeds blurTexture_. Reuse the overlay scratch textures.
        SDL_Texture *target=SDL_GetRenderTarget(renderer_);
        bool ready=false;
        // Separable nine-tap blur at 192x108; no GPU readback in normal rendering.
        if (blurScratch_) {
            SDL_Texture *inputs[] = {blurTexture_, blurScratch_};
            SDL_Texture *outputs[] = {blurScratch_, blurTexture_};
            const Uint8 weights[] = {4,14,31,49,59,49,31,14,4};
            for (int pass = 0; pass < 2; ++pass) {
                if (SDL_SetRenderTarget(renderer_, outputs[pass])) break;
                SetColor(renderer_, {0,0,0,255}); SDL_RenderClear(renderer_);
                SDL_SetTextureBlendMode(inputs[pass], SDL_BLENDMODE_ADD);
                for (int tap = -4; tap <= 4; ++tap) {
                    SDL_SetTextureAlphaMod(inputs[pass], weights[tap+4]);
                    // Overscan keeps every sample covering the edges (no dark border).
                    SDL_Rect shifted = {-4 + (pass == 0 ? tap : 0), -4 + (pass == 1 ? tap : 0), 200, 116};
                    SDL_RenderCopy(renderer_, inputs[pass], nullptr, &shifted);
                }
                SDL_SetTextureAlphaMod(inputs[pass], 255);
                SDL_SetTextureBlendMode(inputs[pass], SDL_BLENDMODE_BLEND);
            }
        }
        if (lensTexture_ && SDL_SetRenderTarget(renderer_, lensTexture_) == 0) {
            SetColor(renderer_, {0,0,0,255}); SDL_RenderClear(renderer_);
            SDL_RenderCopy(renderer_, blurTexture_, nullptr, nullptr);
            ready = true;
        }
        SDL_SetRenderTarget(renderer_,target);
        return ready;
    }
    void DrawShowcase(const GameEntry &game, Color accent) {
        if(SwitchSkin())accent=SwitchAccent();
        const auto pose=CoverTransition(showcaseProgress_);
        const auto lerp=[&](int from,int to){return static_cast<int>(from+(to-from)*pose.travel);};
        // Match the details page's complete turn, with a smaller landing point
        // below the action. The decoder and its queues are shared by all modes.
        const SDL_Rect movingCase{lerp(FocusCoverRect().x,56),lerp(FocusCoverRect().y,416),lerp(FocusCoverRect().w,170),lerp(FocusCoverRect().h,242)};
        // Wait until the library has faded away so its title never overlaps
        // the arriving synopsis. Reversing the same timeline also stays clean.
        const float reveal=SmoothFade((showcaseProgress_-.55f)/.45f);
        const bool glass=preferences_.skin==1;
        if(reveal>0.f) {
            const int x=258+static_cast<int>((1.f-reveal)*48.f), y=448;
            const auto alpha=[&](int value){return static_cast<Uint8>(value*reveal);};
            const bool previousGlassVideo=gGlassOverVideo;
            gGlassOverVideo=glass;
            if(SwitchSkin()) {
                const auto bg=SwitchBackground();
                FillRoundedRect(renderer_,{x+2,y+5,960,216},8,{0,0,0,alpha(40)});
                FillRoundedRect(renderer_,{x,y,960,216},8,{bg.r,bg.g,bg.b,alpha(248)});
            } else if(glass) {
                // Match Settings' selected category lens: refracted backdrop,
                // clear material and a light accent wash, with no opaque base.
                DrawGlassSurface(renderer_,{x,y,960,216},24,accent,88,true,reveal);
                FillRoundedRect(renderer_,{x+4,y+4,952,208},21,{accent.r,accent.g,accent.b,alpha(62)});
            } else {
                FillRoundedRect(renderer_,{x+2,y+5,960,216},24,{0,0,0,alpha(54)});
                FillRoundedRect(renderer_,{x,y,960,216},24,{9,17,30,alpha(232)});
            }
            FillRoundedRect(renderer_,{x+22,y+20,4,170},2,{accent.r,accent.g,accent.b,alpha(200)});
            SetColor(renderer_,{255,255,255,alpha(glass?130:25)});
            SDL_RenderDrawLine(renderer_,x+28,y+1,x+930,y+1);
            const auto skinInk=SwitchInk();
            const Color ink=SwitchSkin()?Color{skinInk.r,skinInk.g,skinInk.b,alpha(255)}:glass?Color{241,248,253,alpha(255)}:Color{239,245,252,alpha(255)};
            const Color muted=SwitchSkin()?Color{skinInk.r,skinInk.g,skinInk.b,alpha(205)}:glass?Color{211,229,239,alpha(255)}:Color{171,190,209,alpha(255)};
            DrawFittedText(renderer_,Ellipsize(game.name,120),x+42,y+19,876,3,ink);
            std::string facts=LibraryLabel(GameLibraryId(game));
            if(!game.releaseYear.empty())facts+="  /  "+game.releaseYear;
            if(!game.genre.empty())facts+="  /  "+game.genre;
            if(!game.players.empty())facts+="  /  "+game.players+" PLAYERS";
            DrawFittedText(renderer_,Ellipsize(facts,120),x+42,y+57,876,2,muted);
            const auto creator=!game.developer.empty()?game.developer:game.publisher;
            DrawFittedText(renderer_,Ellipsize(creator,140),x+42,y+85,876,1,muted);
            // Wrap at measured word boundaries once per title/theme, keeping
            // long titles and metadata within the panel in either font.
            if(showcaseDescription_!=game.description || showcaseTextSkin_!=preferences_.skin) {
                showcaseDescription_=game.description;showcaseTextSkin_=preferences_.skin;
                showcaseLines_={};
                std::istringstream words(game.description.empty()?"No synopsis available.":game.description);
                std::string word;int line=0;
                while(words>>word) {
                    word=Ellipsize(word,120);
                    auto next=showcaseLines_[line]+(showcaseLines_[line].empty()?"":" ")+word;
                    if(line==1 && TextWidth(next,2)>876) {showcaseLines_[1]+="...";break;}
                    if(line==0 && !showcaseLines_[0].empty() && TextWidth(next,2)>876) {line=1;showcaseLines_[line]=word;}
                    else showcaseLines_[line]=next;
                }
            }
            for(int line=0;line<2;++line)DrawFittedText(renderer_,showcaseLines_[line],x+42,y+113+line*25,876,2,ink);
            DrawFittedText(renderer_,std::string(ActionButton(ControlAction::Launch))+"  LAUNCH     "+
                ActionButton(ControlAction::Details)+"  DETAILS     B  BACK",x+42,y+186,876,1,muted);
            gGlassOverVideo=previousGlassVideo;
        }
        // Once the case has landed its geometry is static. Reuse the bounded
        // AA case cache rather than redrawing its mesh and reflection while
        // UVD is decoding. The animated transition still renders every pose.
        DrawBox(game,movingCase,-1.f,true,accent,pose.yaw,pose.pitch,pose.roll,true,
                CaseView::kCamera,0.f,0.f,showcaseProgress_==1.f);
    }
    void DrawDetailsButton(SDL_Rect rect,bool selected,Color accent){
        if(SwitchSkin()) {
            SetColor(renderer_,SwitchBackground());SDL_RenderFillRect(renderer_,&rect);
            SetColor(renderer_,selected?SwitchAccent():SwitchMuted());
            for(int i=0;i<(selected?3:1);++i){SDL_RenderDrawRect(renderer_,&rect);++rect.x;++rect.y;rect.w-=2;rect.h-=2;}
        }
        else if(preferences_.skin==1)DrawDetailGlassButton(renderer_,rect,selected,accent);
        else FillRoundedRect(renderer_,rect,5,selected?accent:Color{29,45,62,255});
    }
    void DrawDetailsIcon(int row,SDL_Rect r,Color ink){
        const int cx=r.x+r.w/2,cy=r.y+r.h/2;
        SetColor(renderer_,ink);
        const auto line=[&](int x1,int y1,int x2,int y2){for(int o=0;o<2;++o)SDL_RenderDrawLine(renderer_,cx+x1,cy+y1+o,cx+x2,cy+y2+o);};
        const auto box=[&](int x,int y,int w,int h){SDL_Rect b={cx+x,cy+y,w,h};SDL_RenderDrawRect(renderer_,&b);++b.x;++b.y;b.w-=2;b.h-=2;SDL_RenderDrawRect(renderer_,&b);};
        if(row==0){
            SDL_Vertex vertices[11]{};int indices[30]{};
            const SDL_Color color={ink.r,ink.g,ink.b,ink.a};
            vertices[0]={{float(cx),float(cy)},color,{0,0}};
            for(int i=0;i<10;++i){const float angle=-1.5707963f+i*.6283185f,radius=i%2?7.5f:18.f;
                vertices[i+1]={{cx+cosf(angle)*radius,cy+sinf(angle)*radius},color,{0,0}};
                indices[i*3]=0;indices[i*3+1]=i+1;indices[i*3+2]=(i+1)%10+1;
            }
            SDL_RenderGeometry(renderer_,nullptr,vertices,11,indices,30);
        }else if(row==1){for(int x=-11;x<=15;++x)line(x,-(15-x)*2/3,x,(15-x)*2/3);}
        else if(row==2){box(-19,-10,38,23);line(-14,0,-4,0);line(-9,-5,-9,5);box(7,-5,4,4);box(12,2,4,4);}
        else if(row==3){box(-18,-10,36,25);line(-17,-11,-17,-16);line(-17,-16,-2,-16);line(-2,-16,3,-10);}
        else if(row==4){box(-18,-13,36,27);line(-6,-7,7,0);line(7,0,-6,7);line(-6,7,-6,-7);}
        else{box(-14,-16,28,32);line(-2,-8,2,-8);line(0,-1,0,9);line(-5,10,5,10);}
    }
    void DrawGlassDetails(const GameEntry &game, Color accent, float progress) {
        const CoverTransitionPose pose = CoverTransition(progress);
        const float eased = pose.travel;

        const auto interpolate = [eased](int from, int to) {
            return static_cast<int>(from + (to - from) * eased);
        };
        const SDL_Rect movingCase = {interpolate(FocusCoverRect().x,48), interpolate(FocusCoverRect().y,98),
                                     interpolate(FocusCoverRect().w,310), interpolate(FocusCoverRect().h,465)};
        DrawBox(game,movingCase,-1.0f,true,accent,pose.yaw,pose.pitch,pose.roll,true);

        const int shift = static_cast<int>((1.0f - eased) * 760.0f);
        const int panelX = 382 + shift;
        DrawGlassSurface(renderer_, {panelX,102,838,510}, 30, accent, 96, true);
        FillRoundedRect(renderer_,{panelX+113,120,700,470},20,{244,248,250,200});
        SetColor(renderer_, {255,255,255,105}); SDL_Rect divider={panelX+99,126,1,450}; SDL_RenderFillRect(renderer_,&divider);

        for(int row=0;row<6;++row){
            const SDL_Rect tab={panelX+20,133+row*70,60,58};
            const bool selected=row==detailsRow_;
            DrawDetailsButton(tab,selected&&!detailsPanelFocus_,accent);
            if(selected)FillRoundedRect(renderer_,{panelX+10,tab.y+17,3,24},1,accent);
            DrawDetailsIcon(row,tab,selected?Color{33,58,69,255}:Color{85,103,114,255});
        }

        const int contentX = panelX + 130;
        const TextureInfo logo = preferences_.useLogos ? textures_.Get(game.logoPath) : TextureInfo{};
        if (logo.texture) {
            const float ratio = static_cast<float>(logo.width) / std::max(1,logo.height);
            SDL_Rect logoRect={contentX,126,520,52};
            if(ratio>static_cast<float>(logoRect.w)/logoRect.h){logoRect.h=static_cast<int>(logoRect.w/ratio);logoRect.y+=(52-logoRect.h)/2;}
            else logoRect.w=static_cast<int>(logoRect.h*ratio);
            SDL_RenderCopy(renderer_,logo.texture,nullptr,&logoRect);
            DrawText(renderer_,Ellipsize(game.name,52),contentX,182,2,{48,58,66,255});
        } else DrawFittedText(renderer_,game.name,contentX,139,666,3,{48,58,66,255});
        const std::string creator=!game.developer.empty()?game.developer:game.publisher;
        DrawText(renderer_,creator.empty()?PlatformName(game.platform):Ellipsize(creator,52),contentX,204,2,{105,117,127,255});
        DrawDetailsContent(game,contentX,accent);

        if(detailsRow_!=4){
            const auto metadata=std::string(PlatformName(game.platform))+" / "+
                (game.genre.empty()?"Unknown genre":game.genre)+" / "+
                (game.releaseYear.empty()?"----":game.releaseYear)+" / "+
                (game.players.empty()?"Players --":"Players "+game.players);
            DrawFittedText(renderer_,metadata,contentX,506,650,1,{80,96,108,255});
        }

        const auto footer=[&](int offset,int width,const std::string &text){
            DrawText(renderer_,text,contentX+offset+width/2,563,1,{43,63,74,255},true);
        };
        footer(0,288,std::string("A  ")+DetailsAction());
        footer(294,150,"B  BACK"); footer(456,210,"+  SETTINGS");
    }
    void DrawDetailsContent(const GameEntry &game,int x,Color accent) {
        const Color ink=SwitchSkin()?SwitchInk():preferences_.skin==1?Color{55,67,77,255}:Color{225,235,243,255};
        if(detailsRow_==4) {
            const std::string paths[]={game.previewPath,game.coverPath,game.backCoverPath,game.spinePath,game.fullCoverPath,game.defaultSleevePath,game.backgroundPath,game.logoPath};
            const char *names[]={"Video","Front cover","Back cover","Spine","Full sleeve","Default sleeve","Background","Logo"};
            const int index=((detailsPage_%8)+8)%8;
            DrawText(renderer_,std::string("Media / ")+names[index],x,242,3,ink);
            if(index==0){
                SDL_Rect frame={x+135,278,350,197};FillRoundedRect(renderer_,frame,4,{5,10,16,255});
                if(detailsVideo_.Ready()){
                    SDL_Rect old;SDL_RenderGetViewport(renderer_,&old);SDL_RenderSetViewport(renderer_,&frame);
                    detailsVideo_.Draw(frame.w,frame.h);SDL_RenderSetViewport(renderer_,&old);
                }else DrawFittedText(renderer_,game.previewPath.empty()?"No video assigned":detailsVideoPending_?"Loading video...":"Press Play to preview",x+150,365,320,1,ink);
                if(!detailsVideoMessage_.empty()&&!detailsVideo_.Ready())DrawFittedText(renderer_,detailsVideoMessage_,x,477,620,1,ink);
            }else{
                const auto art=textures_.Get(paths[index]);
                if(art.texture&&art.width>0&&art.height>0){
                    const float scale=std::min(620.f/art.width,190.f/art.height);
                    SDL_Rect r={x+(620-int(art.width*scale))/2,280,int(art.width*scale),int(art.height*scale)};
                    SDL_RenderCopy(renderer_,art.texture,nullptr,&r);
                }else DrawText(renderer_,"No artwork assigned.",x,340,2,ink);
            }
            const char *labels[]={"PREVIOUS",index==0?(detailsVideo_.Active()||detailsVideoPending_?"STOP":"PLAY"):"NEXT IMAGE","NEXT"};
            for(int i=0;i<3;++i){bool active=detailsPanelFocus_&&detailsPanelRow_==i;
                DrawDetailsButton({x+i*210,490,200,38},active,accent);
                DrawText(renderer_,labels[i],x+i*210+100,501,1,active&&!SwitchSkin()&&preferences_.skin!=1?AccentInk(accent):ink,true);
            }
            return;
        }
        const char *heading=detailsRow_==0?"Synopsis":detailsRow_==1?"Launch game":detailsRow_==3?"Content source":detailsRow_==5?"Title information":"Synopsis";
        std::string body=game.description.empty()?"No synopsis available. Add a description through this title's metadata file.":game.description;
        if(detailsRow_==1) {
            std::string launcherId=EffectiveLauncher(game,gameLaunchers_,sources_,libraries_);
            if(launcherId.empty() && game.platform!=Platform::Files)
                launcherId=game.platform==Platform::WiiU?"native":game.launcher==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
            body="Press A to launch this title.\n\nConfigured launcher: "+
                (launcherId.empty()?"Not selected":GameLauncherLabel(game,launcherId))+
                "\nContent: "+(game.installed?"Installed title":game.platform==Platform::WiiU?"Raw Wii U game":"Game file")+
                "";
        }

        if(detailsRow_==2) {
            const auto found=gameLaunchers_.find(LauncherGameKey(game));
            DrawText(renderer_,"Game launcher",x,242,3,ink);
            std::vector<std::string> rows={"Launcher: "+(found==gameLaunchers_.end()?std::string("Use default"):GameLauncherLabel(game,found->second))};
            if(DetailsGameRows()==3)rows.push_back(SavedInjection(game)?"Re-inject ROM":"Inject ROM");
            rows.push_back("Launcher settings");
            for(int i=0;i<static_cast<int>(rows.size());++i){DrawDetailsButton({x,283+i*56,620,50},detailsPanelFocus_&&detailsPanelRow_==i,accent);
                DrawText(renderer_,rows[i],x+18,298+i*56,2,!SwitchSkin()&&preferences_.skin!=1&&detailsPanelFocus_&&detailsPanelRow_==i?AccentInk(accent):ink);}
            DrawText(renderer_,detailsLauncherEditing_?"LEFT / RIGHT CHANGE   A / B DONE":"LEFT / RIGHT FOCUS   UP / DOWN SELECT   A ACTIVATE",x,461,1,ink);
            DrawText(renderer_,("Effective: "+GameLauncherLabel(game,EffectiveLauncher(game,gameLaunchers_,sources_,libraries_))).substr(0,60),x,480,1,ink);
            return;
        }
        if(detailsRow_==3) body="Location: "+game.absolutePath+"\nStorage: "+(game.storage.empty()?"Not reported":game.storage)+
            "\nContent type: "+GameLibraryId(game)+"\nFormat: "+(game.installed?"Installed title":"File / raw game")+"\nPress A to manage library directories.";
        if(detailsRow_==5) body=StatisticsText(game)+"\nTitle ID: "+(game.titleId.empty()?"Not available":game.titleId)+
            "\nRegion: "+(game.region.empty()?"Unknown":game.region)+"\nDeveloper: "+(game.developer.empty()?"Unknown":game.developer)+
            "\nPublisher: "+(game.publisher.empty()?"Unknown":game.publisher)+"\nRating: "+(game.rating.empty()?"Not rated":game.rating)+
            "";
        // Fixed-width pages also split long paths. No filesystem access while navigating text.
        std::vector<std::string> lines;
        while(!body.empty()) {
            auto end=body.find('\n');
            if(end==std::string::npos || end>52) {
                end=std::min<std::size_t>(52,body.size());
                if(end<body.size()) {const auto space=body.rfind(' ',end);if(space!=std::string::npos&&space>0)end=space;}
            }
            lines.push_back(body.substr(0,end));body.erase(0,end);
            if(!body.empty()&&(body.front()==' '||body.front()=='\n'))body.erase(0,1);
        }
        const int pages=std::max(1,static_cast<int>((lines.size()+6)/7));
        const int page=((detailsPage_%pages)+pages)%pages;
        DrawText(renderer_,heading,x,242,3,ink);
        for(int i=0;i<7 && page*7+i<static_cast<int>(lines.size());++i)
            DrawText(renderer_,lines[page*7+i],x,282+i*25,2,ink);
        DrawText(renderer_,pages>1?"LEFT / RIGHT  PAGE "+std::to_string(page+1)+" / "+std::to_string(pages):"UP / DOWN  TABS",x,470,1,accent);
    }
    void DrawSwitchDetails(const GameEntry &game,float progress) {
        const auto accent=SwitchAccent(),ink=SwitchInk();const auto pose=CoverTransition(progress);
        const int x=int(80+(1.f-pose.travel)*1240.f);
        DrawPanelBase(accent,x,1120,progress);
        DrawText(renderer_,"Title Details",x+40,119,3,ink);
        const auto from=FocusCoverRect();const auto lerp=[&](int a,int b){return int(a+(b-a)*pose.travel);};
        DrawBox(game,{lerp(from.x,110),lerp(from.y,176),lerp(from.w,250),lerp(from.h,357)},-1.f,true,accent,pose.yaw,pose.pitch,pose.roll,true);
        for(int i=0;i<6;++i){SDL_Rect tab{x+292,133+i*70,76,58};DrawDetailsButton(tab,i==detailsRow_&&!detailsPanelFocus_,accent);
            DrawDetailsIcon(i,tab,i==detailsRow_?accent:ink);}
        SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,x+385,184,x+385,536);
        DrawFittedText(renderer_,game.name,x+410,190,650,3,ink);
        DrawDetailsContent(game,x+410,accent);
        DrawFittedText(renderer_,std::string("A  ")+DetailsAction(),x+410,555,330,1,ink);
        DrawText(renderer_,"B  Back    +  Settings",x+760,555,1,ink);
    }
    void DrawDetails(Color accent, float progress) {
        const GameEntry *game=Selected(); if(!game)return;
        if(IsInstall(game)){DrawInstallDetails(*game,accent,progress);return;}
        if(SwitchSkin()) {DrawSwitchDetails(*game,progress);return;}
        if(preferences_.skin==1) { DrawGlassDetails(*game,accent,progress); return; }
        const auto pose=CoverTransition(progress);
        const int x=static_cast<int>(80+(1.f-pose.travel)*1240.f);
        DrawPanelBase(accent,x,1120,progress);
        DrawText(renderer_,"TITLE DETAILS",x+40,120,2,{142,157,179,255});
        const auto interpolate=[&](int from,int to){return int(from+(to-from)*pose.travel);};
        DrawBox(*game,{interpolate(FocusCoverRect().x,110),interpolate(FocusCoverRect().y,176),interpolate(FocusCoverRect().w,250),interpolate(FocusCoverRect().h,357)},
                -1.f,true,accent,pose.yaw,pose.pitch,pose.roll,true);
        for(int i=0;i<6;++i) {
            FillRoundedRect(renderer_,{x+292,133+i*70,76,58},8,i==detailsRow_&&!detailsPanelFocus_?accent:Color{28,42,60,255});
            DrawDetailsIcon(i,{x+292,133+i*70,76,58},i==detailsRow_?(detailsPanelFocus_?accent:AccentInk(accent)):Color{190,207,224,255});
            if(i==detailsRow_)FillRoundedRect(renderer_,{x+282,150+i*70,3,24},1,accent);
        }
        FillRoundedRect(renderer_,{x+385,120,700,470},5,{16,28,42,255});
        DrawFittedText(renderer_,game->name,x+410,155,650,3,{232,242,249,255});
        DrawDetailsContent(*game,x+410,accent);
        DrawText(renderer_,std::string("A  ")+DetailsAction(),x+410,555,1,accent);
        DrawText(renderer_,"B  BACK    +  SETTINGS",x+760,555,1,accent);
    }
    void DrawSettings(Color accent, float progress) {
        static constexpr const char *categories[]={"GENERAL","LIBRARY","MEDIA","DISPLAY","CONTROLS","LAUNCHERS"};
        const int x=static_cast<int>(70+(1.0f-progress)*1260.0f);
        DrawPanelBase(accent,x,1140,progress);
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        if(SwitchSkin())accent=SwitchAccent();
        // A frosted reading layer keeps dark cover art from swallowing menu text.
        // Leave the outer glass bevel exposed; the backdrop remains softly visible.
        if (glass && !SwitchSkin()) FillRoundedRect(renderer_,{x+18,110,1104,508},22,{244,248,250,138});
        DrawText(renderer_,"SETTINGS",x+48,126,4,glass?Color{48,58,66,255}:Color{248,250,255,255});
        DrawFittedText(renderer_,audioSettings_?"GENERAL / AUDIO":"UFLOW CONTROL CENTER",x+760,139,330,2,glass?Color{70,84,95,255}:Color{116,133,158,255},true);
        if(!SwitchSkin() && glass) FillRoundedRect(renderer_,{x+38,182,240,374},18,{244,248,250,50});
        else if(!SwitchSkin()) FillRoundedRect(renderer_,{x+38,182,240,390},18,{5,11,21,220});
        for(int index=0;index<6;++index){
            const int y=208+index*61;
            if(index==settingsCategory_){
                if(glass) {
                    FillRoundedRect(renderer_,{x+56,y-12,204,38},11,{accent.r,accent.g,accent.b,62});
                }
                else FillRoundedRect(renderer_,{x+52,y-16,212,46},14,{accent.r,accent.g,accent.b,48});
            }
            if(index==settingsCategory_ && settingsSidebar_)
                FillRoundedRect(renderer_,{x+43,y-12,5,38},2,accent);
            DrawText(renderer_,categories[index],x+74,y-5,2,
                     index==settingsCategory_?(glass?Color{50,61,69,255}:Color{255,255,255,255}):
                                              (glass?Color{65,79,90,255}:Color{128,143,166,255}));
        }
        const int contentX=x+320;
        if(typeManager_||typeText_) { DrawTypes(contentX,accent,glass); return; }
        if(settingsCategory_==5) {DrawLaunchers(contentX,accent,glass);return;}
        // Source navigation replaces the content pane inside this same frame.
        // Keep the category sidebar and window position stable at every level.
        if (sourceManager_) {
            if (mediaHelp_) DrawMediaHelp(contentX);
            else if (directoryBrowser_) DrawDirectoryBrowser(accent, contentX);
            else if (sourceEditor_) DrawSourceEditor(accent, contentX);
            else DrawSourceManager(accent, contentX);
            return;
        }
        const int rowPitch=audioSettings_?48:settingsCategory_==3 || settingsCategory_==2 || settingsCategory_==0?62:76;
        const int rowHeight=audioSettings_?44:57;
        const auto drawRow=[&](int row,const std::string &label,const std::string &value){
            if(row<SettingsFirstRow() || row>=SettingsFirstRow()+6)return;
            const int y=205+(row-SettingsFirstRow())*rowPitch;
            if(SwitchSkin()){SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,contentX-18,y+37,contentX+742,y+37);}
            if(row==settingsRow_ && !settingsSidebar_ && settingsAction_<0){
                if(SwitchSkin()){SetColor(renderer_,accent);SDL_Rect focus{contentX-18,y-18,760,rowHeight};for(int i=0;i<3;++i){SDL_RenderDrawRect(renderer_,&focus);++focus.x;++focus.y;focus.w-=2;focus.h-=2;}}
                else if(glass) FillRoundedRect(renderer_,{contentX-18,y-18,760,rowHeight},14,{accent.r,accent.g,accent.b,65});
                else FillRoundedRect(renderer_,{contentX-18,y-18,760,rowHeight},14,{accent.r,accent.g,accent.b,42});
            }
            DrawText(renderer_,label,contentX,y,2,row==settingsRow_?(glass?Color{45,56,64,255}:Color{255,255,255,255}):
                                                                (glass?Color{65,79,90,255}:Color{188,199,216,255}));
            const Color valueColor=glass?AccentInk(accent):(row==settingsRow_?accent:Color{126,142,166,255});
            if(settingsEditing_ && row==settingsRow_ && !settingsSidebar_ && settingsAction_<0)
                FillRoundedRect(renderer_,{contentX+468,y-9,268,36},10,{accent.r,accent.g,accent.b,95});
            if(settingsCategory_==3 && (row==3 || row==4)) {
                const int width=180;
                const int low=row==4?CoverZoom::Min:CoverSpacing::Min, high=row==4?CoverZoom::Max:CoverSpacing::Max;
                const int normal=row==4?CoverZoom::Normal:CoverSpacing::Normal;
                const int sliderValue=row==4?preferences_.coverZoom:preferences_.coverSpacing;
                const int filled=int(width*(std::clamp(sliderValue,low,high)-low)/float(high-low));
                FillRoundedRect(renderer_,{contentX+480,y+6,width,6},3,glass?Color{136,162,177,110}:Color{64,85,109,255});
                if(filled)FillRoundedRect(renderer_,{contentX+480,y+6,filled,6},3,accent);
                // A reference mark identifies the normal size or spacing.
                FillRoundedRect(renderer_,{contentX+480+width*(normal-low)/(high-low),y+3,2,12},1,valueColor);
                FillSmoothDot(renderer_,float(contentX+480+filled),float(y+9),7,accent);
                DrawFittedText(renderer_,value,contentX+670,y,64,2,valueColor,true);
                return;
            }
            if(SettingsSelectorRow(row)) DrawText(renderer_,"<",contentX+480,y,2,valueColor);
            DrawFittedText(renderer_,value,contentX+505,y,192,2,valueColor,true);
            if(SettingsSelectorRow(row)) DrawText(renderer_,">",contentX+718,y,2,valueColor);
        };
        if(settingsCategory_==0 && !audioSettings_){
            static constexpr const char *skins[]={"PIXEL DECK","GLASSFLOW","SWITCH LIGHT","SWITCH DARK"};
            drawRow(0,"SKIN",SkinSpec().name);
            drawRow(1,"START VIEW",LibraryLabel(preferences_.startLibrary.empty()?
                (LibraryTabs().empty()?kAllLibraryId:LibraryTabs()[StartupLibraryIndex(LibraryTabs(),preferences_)]):preferences_.startLibrary));
            drawRow(2,"WRAP NAVIGATION",OnOff(preferences_.wrapNavigation));
            drawRow(3,"SHOW INSTALLED WII U",OnOff(preferences_.showInstalled));
            drawRow(4,"AUTO SORT INSTALLED VC",OnOff(preferences_.autoSortInstalledVC));
            drawRow(5,"AUDIO","A  OPEN");
            drawRow(6,"START UFLOW ON BOOT",OnOff(preferences_.startOnBoot));
            drawRow(7,"TITLE INSTALLER",TitleInstall::Available()?OnOff(preferences_.titleInstallerEnabled):"MODULE NOT INCLUDED");
            drawRow(8,"DEMO MODE",OnOff(preferences_.demoMode));
            if(preferences_.demoMode)drawRow(9,"IDLE TIME",std::to_string(preferences_.demoIdleSeconds)+" SECONDS");
        }else if(settingsCategory_==3){

            static constexpr const char *speeds[]={"RELAXED","SMOOTH","FAST"};

            drawRow(0,"COVER TYPE",CoverPresentation::Types[preferences_.coverType]);
            drawRow(1,"COVER LAYOUT",CoverPresentation::LayoutName(preferences_.coverType,PresentationLayout()));
            drawRow(2,"ANIMATION",speeds[preferences_.animationSpeed]);
            drawRow(3,"COVER SPACING",std::to_string(preferences_.coverSpacing)+"%");
            drawRow(4,"COVER ZOOM",std::to_string(preferences_.coverZoom)+"%");
            drawRow(5,"INSPECTION ZOOM",std::to_string(preferences_.inspectionZoom/2)+(preferences_.inspectionZoom%2?".5x":".0x"));
            drawRow(6,"BACKGROUND MOTION",OnOff(preferences_.backgroundMotion));
            drawRow(7,"REFLECTIONS",OnOff(preferences_.reflections));
            drawRow(8,"ACCENT COLOR",AccentThemes::Presets[preferences_.theme].name);
            if(SettingsFirstRow()<=8 && SettingsFirstRow()+6>8) {
                const int y=205+(8-SettingsFirstRow())*62;
                FillRoundedRect(renderer_,{contentX+438,y-3,24,24},7,{255,255,255,220});
                FillRoundedRect(renderer_,{contentX+440,y-1,20,20},5,accent);
            }
            drawRow(9,"3D BOX ART",preferences_.coverType==CoverPresentation::Box?OnOff(preferences_.useBoxArt):"BOX ONLY");
            drawRow(10,"BACKGROUNDS",OnOff(preferences_.useBackgrounds));
            drawRow(11,"TITLE LOGOS",OnOff(preferences_.useLogos));
            drawRow(12,"GAMEPLAY PREVIEWS",OnOff(preferences_.usePreviews));
            drawRow(13,"PREVIEW DELAY",std::to_string(preferences_.previewDelaySeconds)+" SECONDS");
            drawRow(14,"PRELOAD COVERS",OnOff(preferences_.preloadBuiltInCovers));
            drawRow(15,"STARTUP VIDEO",OnOff(preferences_.startupVideo));
            drawRow(16,"SORT BY",LibrarySort::Name(preferences_.sortMode));
            drawRow(17,"BACKGROUND TRANSPARENCY",std::to_string(preferences_.backgroundTransparency)+"%");
        }else if(audioSettings_){
            drawRow(0,"MENU MUSIC",OnOff(preferences_.musicEnabled));
            drawRow(1,"TRACK",preferences_.musicTrack.empty()?"NO TRACK":preferences_.musicTrack);
            drawRow(2,"VOLUME",std::to_string(preferences_.musicVolume)+"%");
            drawRow(3,"RELOAD MUSIC LIST","A  RELOAD");
            drawRow(4,"MENU SOUND EFFECTS",OnOff(preferences_.menuSoundsEnabled));
            drawRow(5,"EFFECTS VOLUME",std::to_string(preferences_.menuSoundsVolume)+"%");
            DrawFittedText(renderer_,musicStatus_,contentX,500,730,1,
                glass?Color{62,73,82,255}:Color{188,199,216,255});
            DrawFittedText(renderer_,"/wiiu/apps/uFlow/music/  |  MP3, OGG, WAV  |  MAX 32 MIB",contentX,536,730,1,
                glass?Color{62,73,82,255}:Color{188,199,216,255});
        }else if(settingsCategory_==4){
            const Color controlText=glass?Color{43,54,63,255}:Color{238,244,252,255};
            const Color controlMuted=glass?Color{65,79,90,255}:Color{130,147,171,255};
            DrawFittedText(renderer_,bindingCapture_>=0?"HOLD BUTTONS TOGETHER, RELEASE TO ASSIGN":"BUTTON MAPPINGS / COMBINATIONS",contentX,205,735,2,controlText);
            for(int row=SettingsFirstRow();row<std::min(SettingsRowCount(),SettingsFirstRow()+12);++row){
                const int y=238+(row-SettingsFirstRow())*26; const bool selected=row==settingsRow_&&!settingsSidebar_&&settingsAction_<0;
                if(selected) FillRoundedRect(renderer_,{contentX-12,y-4,750,25},9,{accent.r,accent.g,accent.b,65});
                if(row==static_cast<int>(ControlAction::Count)) DrawText(renderer_,"RESET DEFAULTS",contentX,y,2,selected?controlText:controlMuted);
                else { DrawText(renderer_,ControlActionName(static_cast<ControlAction>(row)),contentX,y,2,selected?controlText:controlMuted);
                    DrawControlBindingIcons(row,{contentX+345,y-2,388,24},selected?accent:controlMuted); }
            }
        }
        FillRoundedRect(renderer_,{x+38,564,1064,1},1,{accent.r,accent.g,accent.b,80});
        DrawSettingsActions(contentX,accent);

    }
    void DrawSourceManager(Color accent, int x) {
        const bool glass = preferences_.skin == 1 || preferences_.skin == 2;
        const Color text = glass ? Color{43,54,63,255} : Color{238,244,252,255};
        const Color muted = glass ? Color{62,77,88,255} : Color{130,147,171,255};
        DrawText(renderer_,mediaMode_?"MEDIA / DIRECTORIES":"LIBRARY / DIRECTORIES",x,202,3,text);
        DrawText(renderer_,mediaMode_?"EXPAND A CONTENT TYPE / SELECT A DIRECTORY TO EDIT":"SELECT A PATH TO EDIT ITS SCAN OPTIONS",x,240,1,muted);
        const auto mediaRows=mediaMode_?MediaRows():std::vector<MediaDirectoryRow>{};
        const int total=SourceListCount();
        const int first = std::max(0, std::min(sourceRow_ - 4, total - 6));
        for (int visible = 0; visible < 6 && first + visible < total; ++visible) {
            const int row = first + visible;
            const int y = 274 + visible * 48;
            const bool selected = row == sourceRow_ && !settingsSidebar_ && settingsAction_<0;
            if (selected) FillRoundedRect(renderer_, {x-12,y-3,750,46}, 12, {accent.r,accent.g,accent.b,72});
            if(!mediaMode_&&row==0) { DrawText(renderer_,"MANAGE CONTENT TYPES",x+10,y+8,2,selected?text:muted); continue; }
            int sourceIndex=row-1;
            if(mediaMode_) {
                const auto &entry=mediaRows[row];
                if(entry.kind==MediaDirectoryRow::Header) {
                    if(!selected) FillRoundedRect(renderer_,{x-12,y-3,750,44},10,{accent.r,accent.g,accent.b,28});
                    DrawText(renderer_,entry.expanded?"-":"+",x+10,y+8,2,text);
                    DrawFittedText(renderer_,entry.label,x+42,y+7,510,2,text);
                    DrawFittedText(renderer_,std::to_string(entry.directories)+(entry.directories==1?" DIRECTORY":" DIRECTORIES"),x+560,y+13,160,1,muted,true);
                    continue;
                }
                sourceIndex=entry.kind==MediaDirectoryRow::Directory?entry.source:
                    SourceCount()+static_cast<int>(entry.kind)-static_cast<int>(MediaDirectoryRow::Add);
            }
            if (sourceIndex>=SourceCount()) {
                constexpr const char *mediaActions[]={"+  ADD DIRECTORY","REFRESH MEDIA","MEDIA SETUP GUIDE"};
                constexpr const char *libraryActions[]={"+  ADD DIRECTORY","ADD NEW GAMES","COMPLETE SCAN"};
                const int action=sourceIndex-SourceCount();
                DrawText(renderer_,(mediaMode_?mediaActions:libraryActions)[action],x+10,y+(action&& !mediaMode_?0:8),2,selected?text:muted);
                if(!mediaMode_ && action)DrawFittedText(renderer_,action==1?"Keeps existing games, including disconnected drives.":"Replaces catalog. Large libraries may take a long time.",x+10,y+26,715,1,muted);
                continue;
            }
            const auto &path=mediaMode_?mediaSources_[sourceIndex].path:sources_[sourceIndex].path;
            const int depth=mediaMode_?mediaSources_[sourceIndex].depth:sources_[sourceIndex].depth;
            const bool enabled=mediaMode_?mediaSources_[sourceIndex].enabled:sources_[sourceIndex].enabled;
            const std::string kind=mediaMode_?MediaTypeName(mediaSources_[sourceIndex].type):LibraryLabel(sources_[sourceIndex].libraryId);
            const int indent=mediaMode_?38:10, width=mediaMode_?687:715;
            DrawFittedText(renderer_,path,x+indent,y,width,2,selected?text:muted);
            DrawFittedText(renderer_,kind+"  |  DEPTH "+std::to_string(depth)+"  |  "+OnOff(enabled),x+indent,y+24,width,1,muted);
        }
        DrawSettingsActions(x,accent);
    }
    void DrawSourceEditor(Color accent, int x) {
        const bool glass = preferences_.skin == 1 || preferences_.skin == 2;
        const Color text = glass ? Color{43,54,63,255} : Color{238,244,252,255};
        const Color muted = glass ? Color{62,77,88,255} : Color{130,147,171,255};
        DrawText(renderer_,std::string(mediaMode_?"MEDIA":"LIBRARY")+(editingSourceIndex_>=0?" / EDIT PATH":" / ADD PATH"),x,202,3,text);
        DrawFittedText(renderer_,DraftPath(),x,240,725,1,muted);
        std::vector<std::pair<std::string,std::string>> rows{{"DIRECTORY","A  BROWSE"},
            {mediaMode_?"MEDIA TYPE":"CONTENT TYPE",mediaMode_?MediaTypeName(draftMediaSource_.type):LibraryLabel(draftSource_.libraryId)}};
        if(mediaMode_) rows.push_back({"LIBRARY",draftMediaSource_.libraryId.empty()?MediaLibraryName(draftMediaSource_.library):LibraryLabel(draftMediaSource_.libraryId)});
        rows.push_back({"SCAN DEPTH",std::to_string(mediaMode_?draftMediaSource_.depth:draftSource_.depth)});
        rows.push_back({"ENABLED",OnOff(mediaMode_?draftMediaSource_.enabled:draftSource_.enabled)});
        if(!mediaMode_)
            rows.push_back({"LAUNCHER",SourceLauncherLabel()});
        if(RawVCSortEditing())rows.push_back({"AUTO SORT VC BY SYSTEM",OnOff(draftSource_.autoSortVC)});
        if(VideoSourceEditing()) {
            char delay[24]; std::snprintf(delay,sizeof(delay),"%.2f SECONDS",draftMediaSource_.video.delayMs/1000.0);
            rows.push_back({"VIDEO START DELAY",delay});
            rows.push_back({"VIDEO MODE",VideoModeName(draftMediaSource_.video.mode)});
            rows.push_back({"VIDEO VOLUME",draftMediaSource_.video.volume ? std::to_string(draftMediaSource_.video.volume)+"%" : "MUTED"});
            if(BackgroundVideoEditing()) rows.push_back({"FOREGROUND OPACITY",std::to_string(draftMediaSource_.video.foregroundOpacity)+"%"});
        }
        if(mediaMode_) {
            const auto &file=draftMediaSource_.defaultFile;
            const auto slash=file.find_last_of('/');
            rows.push_back({"DEFAULT FILE",file.empty()?(IsCoverMedia(draftMediaSource_.type)?"AUTO DEFAULT.PNG / BROWSE":"NONE / BROWSE"):file.substr(slash==std::string::npos?0:slash+1)});
        }
        const int first = SourceEditorFirst();
        for (int row = first; row < std::min(first+7,static_cast<int>(rows.size())); ++row) {
            const int y = 278 + (row-first) * 38;
            if (row == sourceEditorRow_ && !settingsSidebar_ && settingsAction_<0) FillRoundedRect(renderer_, {x-12,y-6,750,37}, 13, {accent.r,accent.g,accent.b,70});
            DrawText(renderer_, rows[row].first, x+10, y, 2, row == sourceEditorRow_ ? text : muted);
            const Color valueColor=glass?AccentInk(accent):(row == sourceEditorRow_ ? accent : muted);
            const bool editable=SourceEditorSelector(row);
            if(settingsEditing_ && row==sourceEditorRow_ && !settingsSidebar_ && settingsAction_<0)
                FillRoundedRect(renderer_,{x+468,y-7,268,32},10,{accent.r,accent.g,accent.b,95});
            if(editable) { DrawText(renderer_,"<",x+480,y,2,valueColor); DrawText(renderer_,">",x+718,y,2,valueColor); }
            DrawFittedText(renderer_,rows[row].second,x+505,y,editable?192:220,2,valueColor,true);
        }
        if(DefaultFileRow(sourceEditorRow_))
            DrawFittedText(renderer_,draftMediaSource_.defaultFile.empty()?"OPTIONAL FALLBACK WHEN NO GAME FILE EXISTS; A TO CHOOSE":draftMediaSource_.defaultFile,x,560,730,1,muted);

        DrawSettingsActions(x,accent);
    }
    void DrawMediaHelp(int x) {
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const Color text=glass?Color{43,54,63,255}:Color{238,244,252,255};
        DrawText(renderer_,"MEDIA / SETUP GUIDE",x,202,3,text);
        const char *lines[]={
            "1. ADD A DIRECTORY; SELECT ITS MEDIA TYPE AND LIBRARY.",
            "2. NAME EACH FILE BY TITLE ID OR THE GAME NAME.",
            "   EXAMPLE: 00050000101fa800.png OR 140.png",
            "3. FULL COVER / FRONT / BACK / SPINE: PNG, TGA, JPG.",
            "   FULL SLEEVE WINS; MISSING FACES USE THE DEFAULT SLEEVE.",
            "4. METADATA: MATCHING .ini FILE; description=, genre=, etc.",
            "5. VIDEOS: MP4 / AVI; SET IDLE DELAY, MODE AND VOLUME.",
            "6. OPTIONAL DEFAULT FILE FOR MISSING MEDIA; SAVE AND SCAN.",
            "TITLE ID WINS OVER NAME. LIBRARY-SPECIFIC WINS OVER ALL.",
            "FULL GUIDE: docs/CONTENT_AND_MEDIA.md IN THE PROJECT."};
        for(int i=0;i<10;++i) DrawFittedText(renderer_,lines[i],x,251+i*30,730,1,text);
        DrawSettingsActions(x,AccentFor(Selected()?Selected()->platform:Platform::WiiU,preferences_.theme));
    }
    void DrawDirectoryBrowser(Color accent, int x) {
        const bool glass = preferences_.skin == 1 || preferences_.skin == 2;
        const Color text = glass ? Color{42,53,62,255} : Color{238,244,252,255};
        const Color muted = glass ? Color{62,77,88,255} : Color{130,147,171,255};
        DrawText(renderer_,defaultFileBrowser_?"MEDIA / SELECT DEFAULT FILE":mediaMode_?"MEDIA / SELECT DIRECTORY":"LIBRARY / SELECT DIRECTORY",x,202,3,text);
        DrawFittedText(renderer_, browserPath_, x, 240, 725, 1, glass?AccentInk(accent):accent);
        if (browserDirectories_.empty()) {
            DrawText(renderer_, "NO SUBDIRECTORIES", x, 290, 2, muted);
        } else {
            const int total = static_cast<int>(browserDirectories_.size());
            const int first = std::max(0, std::min(browserRow_ - 5, total - 7));
            for (int visible = 0; visible < 7 && first + visible < total; ++visible) {
                const int row = first + visible;
                const int y = 280 + visible * 37;
                if (row == browserRow_ && !settingsSidebar_ && settingsAction_<0) FillRoundedRect(renderer_, {x-12,y-9,750,34}, 10, {accent.r,accent.g,accent.b,75});
                DrawFittedText(renderer_, ">  "+browserDirectories_[row], x+10, y, 710, 2,
                         row == browserRow_ ? text : muted);
            }
        }
        const std::string browserStatus=browserLoading_?"READING FOLDER"+std::string((SDL_GetTicks()/300)%4,'.'):
            !browserError_.empty()?browserError_:"";
        DrawFittedText(renderer_,browserStatus,x,560,730,1,muted);
        DrawSettingsActions(x,accent);
    }
    void DrawSettingsActions(int x,Color accent) {
        const auto actions=SettingsActions();
        const auto label=[&](SettingsAction action,int slot) -> const char * {
            using A=SettingsAction;
            switch(action) {
                case A::BindReplace:return "A REPLACE";case A::BindAdd:return "+ ADD";case A::BindClear:return "X CLEAR ALL";
                case A::Select:return settingsEditing_?"A  DONE":"A  SELECT";
                case A::Save:return "+  SAVE";
                case A::Delete:return "X  DELETE";
                case A::Back:return "B  BACK";
                case A::Add:return "+  ADD";
                case A::Scan:return mediaMode_?"Y  REFRESH MEDIA":"Y  ADD NEW GAMES";
                case A::Guide:return "GUIDE";
                case A::Open:return "A  OPEN";
                case A::Use:return defaultFileBrowser_?"Y  SELECT":"Y  USE FOLDER";
                case A::Parent:return "B  PARENT";
                case A::Cancel:return directoryBrowser_?"X  CANCEL":"B  CANCEL";
                case A::Space:return "SPACE";
                case A::Done:return "+  DONE";
                case A::Erase:return "X  ERASE";
                default:return slot==1?"SAVE":slot==2?"DELETE":"";
            }
        };
        for(int i=0;i<4;++i) {
            const bool enabled=actions[i]!=SettingsAction::None,focused=enabled && settingsAction_==i;
            const SDL_Rect rect{x+i*185,574,176,40};
            if(focused)FillRoundedRect(renderer_,{rect.x-3,rect.y-3,rect.w+6,rect.h+6},12,accent);
            FillRoundedRect(renderer_,rect,9,(preferences_.skin==1 || preferences_.skin==2)?
                (focused?Color{237,248,252,255}:Color{230,242,247,static_cast<Uint8>(enabled?205:75)}):
                (focused?Color{32,58,79,255}:Color{26,42,59,static_cast<Uint8>(enabled?240:90)}));
            const auto ink=(preferences_.skin==1 || preferences_.skin==2)?Color{43,65,78,static_cast<Uint8>(enabled?255:100)}:Color{236,242,250,static_cast<Uint8>(enabled?255:100)};
            const int textHeight=gSmoothText&&gSmoothFonts[1]?TTF_FontHeight(gSmoothFonts[1]):7;
            if(settingsCategory_==4&&!sourceManager_&&settingsRow_<int(ControlAction::Count)) {
                const uint32_t buttons[]={0x8000,8,0x2000,0x4000};
                const char *names[]={"REPLACE","ADD","CLEAR ALL","BACK"};
                DrawControlButton(buttons[i],{rect.x+12,rect.y+8,28,24},ink);
                DrawFittedText(renderer_,names[i],rect.x+46,rect.y+(rect.h-textHeight)/2,rect.w-52,1,ink,true);
            }else DrawText(renderer_,label(actions[i],i),rect.x+88,rect.y+(rect.h-textHeight)/2,1,ink,true);
        }
    }
    void DrawInjectionDialog(Color accent) {
        const PopupTextGlowScope glow(preferences_.skin==1);
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{300,185,680,350},24,glass?Color{239,246,248,235}:Color{20,31,49,245});
        if(glass)DrawGlassSurface(renderer_,{300,185,680,350},24,accent,100,true);
        const auto &d=injectionDialog_;
        const bool confirm=d.state==InjectionDialog::State::Confirm;
        const bool working=d.state==InjectionDialog::State::Working;
        DrawText(renderer_,confirm?"NOT INJECTED YET":working?"PREPARING GAME":"INJECTION",640,216,3,ink,true);
        DrawFittedText(renderer_,d.title,338,269,604,2,ink);
        if(confirm){
            DrawText(renderer_,"Create an injection and launch this game?",640,326,1,ink,true);
            DrawText(renderer_,"Your original ROM and VC base will not be changed.",640,366,1,ink,true);
        }else if(working){
            DrawFittedText(renderer_,d.cancel?"CANCELLING...":d.phase,338,323,604,2,ink);
            FillRoundedRect(renderer_,{340,369,600,16},8,{accent.r,accent.g,accent.b,45});
            if(d.total){
                const int width=static_cast<int>(600.0*double(d.bytes)/double(d.total));
                if(width)FillRoundedRect(renderer_,{340,369,width,16},8,accent);
                const auto line=std::to_string(d.bytes*100/d.total)+"% of this step   /   "+std::to_string(d.bytes/(1024*1024))+" / "+std::to_string(d.total/(1024*1024))+" MiB";
                DrawText(renderer_,line,640,405,1,ink,true);
            }else{
                const int x=340+static_cast<int>((SDL_GetTicks()/8)%480);
                FillRoundedRect(renderer_,{x,369,120,16},8,accent);
                DrawText(renderer_,"Please wait...",640,405,1,ink,true);
            }
        }else{
            std::vector<std::string> lines;std::istringstream words(d.message);std::string word,line;
            while(words>>word){if(!line.empty()&&line.size()+word.size()+1>62){lines.push_back(line);line.clear();}if(!line.empty())line+=' ';line+=word;}
            if(!line.empty())lines.push_back(line);
            for(size_t i=0;i<std::min(size_t(4),lines.size());++i)DrawFittedText(renderer_,lines[i],338,319+int(i)*27,604,1,ink);
        }
        if(confirm)for(int i=0;i<2;++i){
            const bool selected=d.yes==(i==1);
            FillRoundedRect(renderer_,{380+i*280,456,240,52},14,{accent.r,accent.g,accent.b,static_cast<Uint8>(selected?140:45)});
            DrawText(renderer_,i?"YES - INJECT":"NO - BACK",500+i*280,475,2,ink,true);
        }else{
            FillRoundedRect(renderer_,{500,456,280,52},14,{accent.r,accent.g,accent.b,90});
            DrawText(renderer_,working?"B  CANCEL":"A / B  CLOSE",640,475,2,ink,true);
        }
    }
    void DrawScan(Color accent) {
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{270,160,740,410},24,glass?Color{239,246,248,245}:Color{20,31,49,250});
        if(glass)DrawGlassSurface(renderer_,{270,160,740,410},24,accent,100,true);
        const auto &p=scanState_.progress;
        DrawText(renderer_,!scanState_.error.empty()?"UPDATE STOPPED":mediaRefresh_?"UPDATING MEDIA":scanState_.finished?"SCAN COMPLETE":scanAddOnly_?"ADDING NEW GAMES":"COMPLETE SCAN",640,190,3,ink,true);
        DrawFittedText(renderer_,!scanState_.error.empty()?scanState_.error:scanCancelling_&&!scanState_.finished?"CANCELLING SCAN...":p.stage,310,240,660,2,ink);
        // Keep the end of long paths visible so the changing file/folder is clear.
        std::string path=p.path;
        while(!path.empty() && TextWidth("..."+path,1)>660){
            std::size_t next=1;while(next<path.size() && (static_cast<unsigned char>(path[next])&0xc0)==0x80)++next;
            path.erase(0,next);
        }
        if(path!=p.path)path="..."+path;
        DrawFittedText(renderer_,path,310,284,660,1,ink);
        FillRoundedRect(renderer_,{320,330,640,20},8,{accent.r,accent.g,accent.b,45});
        if(p.total){
            const int width=static_cast<int>(640.0*std::min(p.completed,p.total)/p.total);
            if(width)FillRoundedRect(renderer_,{320,330,width,20},8,accent);
        }else if(!scanState_.finished)FillRoundedRect(renderer_,{320+int((SDL_GetTicks()/8)%500),330,140,20},8,accent);
        DrawText(renderer_,std::to_string(p.games)+(mediaRefresh_?" TITLES  |  ":" GAMES FOUND  |  ")+std::to_string(scanElapsed_/60)+"m "+std::to_string(scanElapsed_%60)+"s",640,374,2,ink,true);
        if(!scanState_.finished){
            const std::string detail=p.total?std::to_string(p.completed)+" / "+std::to_string(p.total)+" GAMES PROCESSED":mediaRefresh_?"MATCHING CONTENT TYPE ARTWORK":"DISCOVERING FILES - TOTAL NOT YET KNOWN";
            DrawText(renderer_,detail,640,417,1,ink,true);
            if(!mediaRefresh_)DrawText(renderer_,scanAddOnly_?"EXISTING GAMES WILL BE KEPT":"LARGE LIBRARIES MAY TAKE A LONG TIME",640,451,1,ink,true);
            DrawText(renderer_,p.stage=="SAVING LIBRARY"?"SAVING - PLEASE KEEP THE CONSOLE ON":scanCancelling_?"WAITING FOR CURRENT READ TO FINISH":"B  CANCEL SCAN",640,507,1,ink,true);
        }else{
            const std::string detail=!scanState_.error.empty()?"Your previous library has been kept.":
                !scanState_.cacheSaved?"LIBRARY UPDATED; CACHE COULD NOT BE SAVED":
                scanInvalid_?std::to_string(scanInvalid_)+" INCOMPLETE WII U FOLDERS SKIPPED":"Library saved successfully.";
            DrawFittedText(renderer_,detail,310,417,660,1,ink);
            FillRoundedRect(renderer_,{490,482,300,52},14,{accent.r,accent.g,accent.b,90});
            DrawText(renderer_,"A / B  CLOSE",640,502,2,ink,true);
        }
    }
    void DrawUpdate(Color accent) {
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{290,160,700,390},24,glass?Color{239,246,248,245}:Color{20,31,49,250});
        if(glass)DrawGlassSurface(renderer_,{290,160,700,390},24,accent,100,true);
        const auto &s=updateState_;const bool ask=s.state==UFlowUpdate::State::Available,busy=UFlowUpdate::Busy(s.state);
        const char *title=ask?"UPDATE AVAILABLE":busy?"UPDATING UFLOW":s.state==UFlowUpdate::State::Success?"UPDATE INSTALLED":"UPDATE FAILED";
        DrawText(renderer_,title,640,187,3,ink,true);
        DrawFittedText(renderer_,"Version "+s.version,330,234,620,2,ink);
        if(ask){
            DrawText(renderer_,"A new update is available. Update now?",640,318,1,ink,true);
        }else if(busy){
            DrawFittedText(renderer_,s.message,330,306,620,2,ink);
            FillRoundedRect(renderer_,{340,362,600,18},8,{accent.r,accent.g,accent.b,45});
            if(s.state==UFlowUpdate::State::Downloading&&s.total){
                int width=int(600.0*std::min(s.done,s.total)/s.total);if(width)FillRoundedRect(renderer_,{340,362,width,18},8,accent);
                DrawText(renderer_,std::to_string(s.done*100/s.total)+"%",640,405,2,ink,true);
            }else FillRoundedRect(renderer_,{340+int((SDL_GetTicks()/8)%480),362,120,18},8,accent);
            DrawText(renderer_,"Please keep the console on.",640,492,1,ink,true);
        }else{
            updateLines_.clear();std::istringstream text(s.state==UFlowUpdate::State::Success?s.notes:s.message);std::string paragraph;
            while(std::getline(text,paragraph)){std::istringstream words(paragraph);std::string word,line;
                while(words>>word){if(!line.empty()&&line.size()+word.size()+1>64){updateLines_.push_back(line);line.clear();}if(!line.empty())line+=' ';line+=word;}
                if(!line.empty())updateLines_.push_back(line);}
            for(int i=0;i<6&&i+updateScroll_<int(updateLines_.size());++i)DrawFittedText(renderer_,updateLines_[i+updateScroll_],330,280+i*25,620,1,ink);
            if(s.state==UFlowUpdate::State::Success)DrawText(renderer_,"Update installed successfully.",640,447,1,ink,true);
            if(updateLines_.size()>6)DrawText(renderer_,"UP / DOWN SCROLL",640,428,1,ink,true);
        }
        if(ask)for(int i=0;i<2;++i){FillRoundedRect(renderer_,{380+i*280,475,240,52},14,{accent.r,accent.g,accent.b,Uint8(updateYes_==(i==1)?140:45)});
            DrawText(renderer_,i?"YES":"NO",500+i*280,494,2,ink,true);}
        else if(!busy){FillRoundedRect(renderer_,{500,475,280,52},14,{accent.r,accent.g,accent.b,90});DrawText(renderer_,"A / B  CLOSE",640,494,2,ink,true);}
    }
    void DrawLaunchOptions(Color accent) {
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        const auto box=LaunchOptionsLayout();
        const SDL_Rect panel{290,box.top,700,box.height};
        FillRoundedRect(renderer_,panel,24,glass?Color{239,246,248,245}:Color{20,31,49,250});
        if(glass)DrawGlassSurface(renderer_,panel,24,accent,100,true);
        DrawText(renderer_,"LAUNCH OPTIONS",640,box.top+27,3,ink,true);
        DrawFittedText(renderer_,launchOptionsName_,330,box.top+76,launchOptions_.size()>kMaxLaunchOptions?550:620,2,ink);
        const int first=LaunchOptionsFirst();
        for(int i=first;i<std::min(first+static_cast<int>(kMaxLaunchOptions),static_cast<int>(launchOptions_.size()));++i){
            const bool selected=launchOptionsChoice_==i;const int y=box.rows+(i-first)*box.step;
            FillRoundedRect(renderer_,{340,y,600,box.row},16,{accent.r,accent.g,accent.b,Uint8(selected?140:45)});
            DrawText(renderer_,launchOptions_[i].title,640,y+9,2,ink,true);
            DrawFittedText(renderer_,launchOptions_[i].note,360,y+43,560,1,ink);
        }
        if(launchOptions_.size()>kMaxLaunchOptions)
            DrawText(renderer_,std::to_string(launchOptionsChoice_+1)+" / "+std::to_string(launchOptions_.size()),920,box.top+76,1,ink,true);
        const std::string hints=LaunchOptionsCanSetDefault()?"A  LAUNCH      +  SET AS DEFAULT      B  CANCEL":"A  LAUNCH      B  CANCEL";
        DrawText(renderer_,status_.empty()?hints:Ellipsize(status_,60),640,box.top+box.height-34,1,ink,true);
    }
    void DrawWelcome(Color accent) {
        const bool glass=preferences_.skin==1 || preferences_.skin==2;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{240,220,800,280},24,glass?Color{239,246,248,242}:Color{20,31,49,245});
        if(glass)DrawGlassSurface(renderer_,{240,220,800,280},24,accent,100,true);
        DrawText(renderer_,"WELCOME TO UFLOW",640,252,3,ink,true);
        const auto settingsButton=ActionButton(ControlAction::Settings);
        DrawText(renderer_,std::string("Press ")+settingsButton+" for Settings > Library > Add Directory.",640,323,1,ink,true);
        DrawText(renderer_,"Save a folder, then choose Add New Games.",640,361,1,ink,true);
        FillRoundedRect(renderer_,{500,420,280,50},14,{accent.r,accent.g,accent.b,90});
        DrawText(renderer_,"A  GOT IT",640,437,2,ink,true);
    }
    void DrawToast(Color accent) {
        if(settings_&&settingsCategory_==4){
            DrawFittedText(renderer_,status_,80,655,1120,1,SwitchSkin()?SwitchInk():preferences_.skin==1?Color{45,56,64,255}:Color{235,245,255,255});return;
        }
        const int width=std::min(820,TextWidth(status_,2)+64);
        FillRoundedRect(renderer_,{(kWidth-width)/2,568,width,42},16,{accent.r,accent.g,accent.b,235});
        DrawText(renderer_,Ellipsize(status_,62),kWidth/2,581,2,{4,12,22,255},true);
    }

    bool launchCapture_=false,launchAnimation_=false;
    SDL_Rect launchOrigin_{530,155,220,314};
    float launchYaw_=.055f,launchPitch_=0.f,launchRoll_=0.f;
    bool launchOptionsOpen_=false;
    int launchOptionsChoice_=0;
    static constexpr std::size_t kMaxLaunchOptions=5;
    std::vector<LaunchOption> launchOptions_;
    std::string launchOptionsKey_,launchOverrideKey_,launchOverrideId_;
    LaunchMethod chosenLaunch_=LaunchMethod::Default;
    HomebrewLaunchPlan launchPlan_;
    bool launcherBrowseBundle_=false; // the file picker is choosing RetroArch's .wuhb
    std::string launchOptionsName_;
    bool updateOpen_=false,updateYes_=false;
    int updateScroll_=0;
    UFlowUpdate::Snapshot updateState_;
    std::vector<std::string> updateLines_;
    SDL_Renderer *renderer_;
    std::vector<GameEntry> &games_;
    TitleInstall::Job titleInstallJob_;
    int installRow_=0,installTarget_=1;
    bool installWasBusy_=false;
    std::string installMessage_;
    std::string installSpace_;
    TextureCache textures_;
    bool iconPreparationSkipped_=false;
    SDL_Texture *preparationLogo_=nullptr;
    SDL_Texture *preparationLogoGlow_=nullptr;
    SDL_Texture *startupFrame_=nullptr;
    bool startupIntroPending_=false,startupIntroRequested_=false,startupIntroReady_=false;
    Uint32 startupRequestAt_=0,startupVideoAt_=0,startupFadeAt_=0;
    bool preparationLogoTried_=false;
    UserSettings preferences_;
    static constexpr const char *kHistoryPath="fs:/vol/external01/wiiu/apps/uFlow/play_history.cfg";
    LibrarySort::History history_;
    bool alphabetOpen_=false,alphabetHeldMode_=true,alphabetMoved_=false;
    int alphabetChoice_=1,alphabetFilter_=0,alphabetDirection_=0;
    Uint32 alphabetNext_=0;
    float alphabetProgress_=0,alphabetVisual_=1;
    std::array<int,28> alphabetCounts_{};
    bool searchOpen_=false;
    int searchKey_=0,searchHorizontal_=0,searchVertical_=0;
    Uint32 searchNextH_=0,searchNextV_=0;
    std::string searchQuery_,searchFolded_,searchBuffer_;
    bool sortOpen_=false;
    int sortChoice_=0;
    bool welcomeOpen_=false;
    std::vector<std::string> libraryTabs_{kFavoritesLibraryId};
    std::vector<std::pair<std::size_t,bool>> adjacentLibraryCovers_;
    struct FooterHint {
        std::string button;
        const char *action = "";
        int x = 0, width = 0, buttonWidth = 0, gap = 0;
        bool stacked = false;
    };
    std::array<FooterHint,6> footerHints_{};
    std::array<std::uint32_t,static_cast<int>(ControlAction::Count)> hintBindings_{};
    int hintSkin_ = -1;
    std::string favoriteHint_, coverInspectHint_;
    std::vector<ContentSource> sources_;
    std::vector<LibraryDefinition> libraries_=LoadLibraries();
    std::vector<LauncherDefinition> launchers_=LoadLaunchers();
    LauncherOverrides gameLaunchers_=LoadLauncherOverrides();
    bool detailsLauncherEditing_=false;
    bool detailsPanelFocus_=false,injectionFinished_=false;
    int detailsPanelRow_=0;
    std::string pendingInjection_;
    InjectionDialog injectionDialog_;
    GameEntry injectionGame_;
    mutable std::map<std::string,bool> savedInjectionState_;
    LauncherDefinition launcherDraft_;
    bool launcherEditor_=false,launcherBrowse_=false;
    VCInject::Settings injectionDraft_;
    int launcherRow_=0,launcherIndex_=-1,launcherTextField_=0;
    std::string launcherBrowsePath_="fs:/vol/external01",pendingHomebrew_;
    std::vector<std::string> launcherFiles_;
    std::unordered_map<std::string,std::string> launcherStatusCache_;
    int vcMapMode_=0;
    std::size_t vcMapRom_=0;
    std::vector<std::size_t> vcMapGames_;
    bool typeManager_=false,typeEditor_=false,typeText_=false;
    int typeRow_=0,typeIndex_=-1,typeKey_=0;
    LibraryDefinition typeDraft_;
    std::string typeBuffer_;
    bool typeName_=true;
    std::vector<MediaSource> mediaSources_;
    std::set<std::string> expandedMediaGroups_;
    MediaSource draftMediaSource_;
    bool mediaMode_=false, mediaHelp_=false;
    bool settingsSidebar_=true;
    bool settingsEditing_=false;
    int settingsAction_=-1;
    bool audioSettings_=false;
    MenuMusic music_;
    MenuSounds menuSounds_;
    VideoPlayer video_{renderer_};
    VideoPlayer detailsVideo_{renderer_};
    bool detailsVideoPending_=false;
    std::string detailsVideoMessage_;
    GlassBackground glassBackground_{renderer_};
    bool videoPending_=false;
    VideoPlayer::Stats videoReportStats_{};
    Uint32 videoReportAt_=0;
    unsigned videoReports_=0;
    bool restoreMusicAfterBackground_=false;
    VideoTrigger videoTrigger_;
    DemoMode demo_;
    SDL_Texture *profileTexture_=nullptr;
    std::string profileName_="Player";
    std::string demoOriginPath_;
    bool demoNext_=false,demoSeeded_=false;
    Uint32 demoSelectedAt_=0;
    std::unordered_set<std::string> demoFailed_;
#ifdef UFLOW_HOST_PREVIEW
    bool demoTest_=false;
#endif
    const GameEntry *videoSelection_ = nullptr;
    VideoOptions videoOptions_;
    Uint32 videoIsolationUntil_=0;
    bool showcaseLensReady_=false;
    std::uint32_t showcaseLensFrame_=0;
    bool videoConfigured_ = false;
    float showcaseProgress_=0.f;
    int showcaseTextSkin_=-1;
    std::string showcaseDescription_;
    std::array<std::string,2> showcaseLines_;
    bool videoInputActive_ = false;
#ifdef UFLOW_HOST_PREVIEW
    bool videoTest_ = false;
#endif
    std::string musicStatus_="MUSIC OFF";
    std::vector<std::string> musicTracks_;
    std::vector<std::size_t> visible_;
    int browseDirection_ = 1;
    std::size_t selected_ = 0;
    float visualSelected_ = 0.0f;
    float visualTab_ = 0.0f;
    float libraryFade_ = 1.0f;
    int libraryFadePhase_=0, pendingTab_=-1;
    float tabSlide_ = 0.0f;
    float detailsProgress_ = 0.0f;
    float coverInspectProgress_ = 0.0f;
    float coverInspectBackProgress_ = 0.0f;
    float settingsProgress_ = 0.0f;
    TextureInfo selectedBackground_{}; // borrowed from the single pinned cache entry
    std::string selectedBackgroundPath_;
    int tab_ = 0;
    int settingsCategory_ = 0;
    int settingsRow_ = 0;
    int bindingCapture_ = -1;
    bool bindingAppend_=false;
    BindingCapture bindingCaptureState_;
    uint32_t controlHeld_=0;
    ControlChordInput controlChordInput_;
    bool controlOverlayLast_=false,controlTransitionBlocked_=false;
    int sourceRow_ = 0;
    int sourceEditorRow_ = 0;
    int editingSourceIndex_ = -1;
    ContentSource draftSource_;
    ContentSource initialSourceDraft_;
    MediaSource initialMediaDraft_;
    LibraryDefinition initialTypeDraft_;
    DraftExit draftExit_=DraftExit::None;
    bool draftExitYes_=false;
    int draftExitCategory_=0;
    std::string browserPath_ = "fs:/vol";
    std::vector<std::string> browserDirectories_;
    std::unique_ptr<BrowserWorker> browserWorker_;
    bool browserLoading_=false;
    std::string browserError_;
    int browserRow_ = 0;
    bool details_ = false, settings_ = false;
    bool coverInspectOpen_ = false, coverInspectBack_ = false;
    int detailsRow_ = 1, detailsPage_ = 0;
    bool detailsLaunchRequested_ = false;
    bool installLaunchRequested_ = false;
    bool sourceManager_ = false, sourceEditor_ = false, directoryBrowser_ = false;
    bool defaultFileBrowser_ = false;
    bool defaultFileChosen_ = false;
    bool scanRequested_ = false;
    bool scanAddOnly_ = true;
    ScanWorker scanWorker_;
    ScanWorker::Status scanState_;
    bool scanOpen_=false,scanApplied_=false,scanCancelling_=false;
    bool scanStartPending_=false,scanRefreshSources_=false;
    bool scanFirstFrame_=false;
    bool mediaRefreshPending_=false,mediaRefresh_=false;
    bool sourceMediaRefreshPending_=false;
    std::string mediaRefreshSelection_;
    Uint32 scanStarted_=0,scanElapsed_=0;
    std::size_t scanInvalid_=0;
    std::string status_;
    Uint32 statusUntil_ = 0;
    SDL_Texture *bookshelfTexture_=nullptr;
    BrowseMotion::Bookshelf bookshelfMotion_;
    std::string bookshelfKey_;
    std::uint64_t bookshelfRevision_=0,bookshelfEpoch_=0;
    bool bookshelfCached_=false,bookshelfNeedsArtwork_=false;
    unsigned bookshelfBuilds_=0,bookshelfCacheHits_=0,bookshelfFaces_=0,bookshelfVertices_=0;
    SDL_Texture *sceneTexture_ = nullptr;
    SDL_Texture *blurTexture_ = nullptr;
    SDL_Texture *blurScratch_ = nullptr;
    SDL_Texture *lensTexture_ = nullptr;
    // Full inspection needs 1292x1536; align width up to 32 pixels. The old
    // 1280-wide bucket silently sent inspection through the non-AA fallback.
    inline static constexpr std::array<SDL_Point,3> kCaseTargetSizes{{{640,768},{896,1024},{1312,1536}}};
    std::array<SDL_Texture *,3> caseTextures_{};
    struct SideCase { std::string key; SDL_Texture *texture; std::uint64_t use; std::size_t bytes; };
    // Resting side cases, least recently used out. A full shelf of edge-on
    // cases is ~40 narrow textures; the byte budget keeps the old ceiling of
    // 16 square ones.
    static constexpr std::size_t kMaxSideCases = 64;
    static constexpr std::size_t kSideCaseBudget = 26u * 1024u * 1024u;
    std::vector<SideCase> sideCases_;
    std::uint64_t sideCaseUse_ = 0, sideCaseFrame_ = 0;
    std::size_t sideCaseBytes_ = 0;
#ifdef UFLOW_HOST_PREVIEW
    std::vector<std::pair<int,float>> carouselCards_;
    unsigned caseDraws_ = 0;
    unsigned caseDirectFallbacks_ = 0;
#endif
    bool detailsBackdropReady_ = false;
    bool settingsDirty_ = false;
    Uint32 settingsSaveAfter_ = 0;
    unsigned performanceFrames_ = 0;
    double performanceTotalMs_ = 0, performanceMaxMs_ = 0;
    bool preview_ = false;
    std::vector<GameEntry> liveGames_;
    std::vector<ContentSource> liveSources_;
    std::vector<MediaSource> liveMediaSources_;
    UserSettings livePreferences_;
};

#ifndef UFLOW_HOST_PREVIEW
LaunchLooseFn LoadLaunchFunction(OSDynLoad_Module *outModule) {
    if (outModule) *outModule = nullptr;
    OSDynLoad_Module module=nullptr;
    if (OSDynLoad_Acquire("homebrew_rpx_loader",&module)!=OS_DYNLOAD_OK) {
        Trace("raw-loader-module-unavailable");
        return nullptr;
    }
    LaunchLooseFn function=nullptr;
    if (OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,"RL_LaunchLooseDirectory",
                             reinterpret_cast<void **>(&function))!=OS_DYNLOAD_OK) {
        OSDynLoad_Release(module);
        Trace("raw-loader-export-missing-standard-aroma-loader");
        return nullptr;
    }
    if (outModule) *outModule = module;
    else OSDynLoad_Release(module);
    return function;
}

using LaunchWithArgsFn = RPXLoaderStatus (*)(const char *, const char *, const char *, uint32_t);

// Native RetroArch arguments need the newer loader export; without it uFlow
// says so instead of silently opening RetroArch's browser.
LaunchWithArgsFn LoadRetroArchLaunchFunction(OSDynLoad_Module *outModule) {
    if (outModule) *outModule = nullptr;
    OSDynLoad_Module module=nullptr;
    if (OSDynLoad_Acquire("homebrew_rpx_loader",&module)!=OS_DYNLOAD_OK) return nullptr;
    LaunchWithArgsFn function=nullptr;
    if (OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,"RL_LaunchHomebrewWithArgsAndContent",
                             reinterpret_cast<void **>(&function))!=OS_DYNLOAD_OK) {
        OSDynLoad_Release(module);
        return nullptr;
    }
    if (outModule) *outModule = module; else OSDynLoad_Release(module);
    return function;
}

// Older loaders would start the HAI wrapper without its storage handoff (160-0791).
bool LoaderSupportsWiiVCHai() {
    OSDynLoad_Module module=nullptr;
    if (OSDynLoad_Acquire("homebrew_rpx_loader",&module)!=OS_DYNLOAD_OK) return false;
    bool (*supported)()=nullptr;
    const bool found=OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,"RL_SupportsWiiVCHai",
                                          reinterpret_cast<void **>(&supported))==OS_DYNLOAD_OK&&supported;
    const bool result=found&&supported();
    OSDynLoad_Release(module);
    return result;
}
#endif

} // namespace

#ifndef UFLOW_HOST_PREVIEW
int main(int, char **) {
    // SDL owns ProcUI: its event pump handles foreground release, and VideoQuit
    // requests the Menu and drains the exit messages after our renderer is gone.
    // Initializing WHB ProcUI here disables that SDL shutdown path.
    OSReport("uFlow: main-entry-startup-probe-v4\n");
    const bool mounted=WHBMountSdCard()==1;
    // A previous aborted transition must not launch later from WiiFlow's icon.
    CancelWiiLaunchRequest();
    UFlowReturn::Clear(); // Explicitly exiting uFlow must still reach Wii U Menu.
    OSReport("uFlow: sd-mount-returned mounted=%d\n", mounted);
    // File checkpoint before SDL: a missing post-SDL marker alone cannot
    // distinguish a loader failure from a graphics-initialization stall.
    if (FILE *file=std::fopen(kTracePath,"ab")) {
        std::fprintf(file,"event=main-entry-startup-probe-v4 mounted=%d\n",mounted);
        std::fclose(file);
    }
    if (SDL_Init(SDL_INIT_VIDEO)!=0) {
        OSReport("uFlow: SDL_Init failed: %s\n",SDL_GetError());
        if (mounted) WHBUnmountSdCard();
        return 1;
    }
    Trace("sdl-init-complete-v4");
    KPADInit();
    // KPAD reads existing controllers, but this explicitly enables the Wii U
    // radio controller connection service so a Pro Controller can pair after
    // uFlow has already launched.
    WPADEnableURCC(true);
    // Clear the system launch jingle even when the user's music setting is OFF.
    // Do this before the library scan so slow storage cannot prolong that sound.
    const auto startupAudioError=MenuMusic::SilenceStartup();
    if(!startupAudioError.empty()) OSReport("uFlow: %s\n",startupAudioError.c_str());
    SDL_Window *window=SDL_CreateWindow("uFlow",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,kWidth,kHeight,SDL_WINDOW_SHOWN);
    Trace(window?"window-created":"window-create-failed");
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):nullptr;
    Trace(renderer?"renderer-created":"renderer-create-failed");
    if (!renderer) { if(window)SDL_DestroyWindow(window); SDL_Quit(); if(mounted)WHBUnmountSdCard(); return 2; }
    SDL_RenderSetVSync(renderer,1);
    SDL_RenderSetLogicalSize(renderer,kWidth,kHeight);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    InitSmoothFonts();
    Trace("fonts-ready");

    USBStorage::Mount();
    std::vector<GameEntry> games;
    const bool loadedLibraryCache = mounted && LoadLibraryCache(games);
    if (loadedLibraryCache && RefreshInstalledGames(games,true)) SaveLibraryCache(games);
    const bool initialScanNeeded=!loadedLibraryCache && mounted;
    Trace(loadedLibraryCache ? "library-cache-loaded" : "library-scan-pending");
    bool launchRequested=false;
    bool running=true;
    bool foreground=true;
    RPXLoaderStatus loaderStatus=RPX_LOADER_RESULT_NOT_AVAILABLE;
    OSDynLoad_Module launchLooseModule=nullptr;
    OSDynLoad_Module launchArgsModule=nullptr;
    {
    Dashboard dashboard(renderer,games);
    UFlowUpdate::Client updater;
    bool updateChecked=false,updateTouchHeld=false,updateHomeBlocked=false,launchOptionsTouchHeld=false;
    // Popups step once per D-pad press or left-stick push (same thresholds as the library).
    int popupStickHeld=0;
    const auto popupDirection=[&](const auto &input) {
        const auto &stick=input.leftStick;
        const int now=stick.y>.48f||stick.x<-.42f?-1:stick.y<-.48f||stick.x>.42f?1:0;
        const int pushed=now!=popupStickHeld?now:0;
        popupStickHeld=now;
        if(input.trigger&(VPAD_BUTTON_LEFT|VPAD_BUTTON_UP))return -1;
        if(input.trigger&(VPAD_BUTTON_RIGHT|VPAD_BUTTON_DOWN))return 1;
        return pushed;
    };
    VisualRemote visualRemote;
    static ThreadDiagnostics::OwnerWatch heapOwnerWatch;
    heapOwnerWatch.Start(visualRemote.Enabled());
    dashboard.SetStatus(mounted ? (loadedLibraryCache ? "LIBRARY CACHE  " : "LIBRARY READY  ")+
        std::to_string(games.size())+" GAMES" : "SOURCE DRIVE COULD NOT BE MOUNTED", mounted ? 3500 : 10000);
    loaderStatus=mounted?RPXLoader_InitLibrary():RPX_LOADER_RESULT_NOT_AVAILABLE;
    LaunchLooseFn launchLoose=loaderStatus==RPX_LOADER_RESULT_SUCCESS?LoadLaunchFunction(&launchLooseModule):nullptr;
    LaunchWithArgsFn launchWithArgs=loaderStatus==RPX_LOADER_RESULT_SUCCESS?LoadRetroArchLaunchFunction(&launchArgsModule):nullptr;
    Trace(launchWithArgs?"retroarch-export-ready":"retroarch-export-missing-at-startup");
    if (visualRemote.Enabled()) ThreadDiagnostics::Dump();
    if(initialScanNeeded)dashboard.StartScan();
    Uint64 previousCounter=SDL_GetPerformanceCounter();
    const double counterFrequency=static_cast<double>(SDL_GetPerformanceFrequency());
    int heldHorizontal=0;
    int heldVertical=0;
    int heldPage=0;
    Uint32 horizontalStarted=0;
    Uint32 horizontalNext=0;
    Uint32 verticalNext=0;
    Uint32 pageNext=0;
    bool touchHeld=false;
    bool zoomSliderHeld=false;
    WiiRemoteInput wiiRemote;
    InputResumeGate inputResume;
    int touchStartX=0,touchStartY=0,touchLastX=0,touchLastY=0;

    gPersistTrace=false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                heapOwnerWatch.Stop();
                dashboard.StopScan();
                updater.Stop();if(updateHomeBlocked){OSEnableHomeButtonMenu(true);updateHomeBlocked=false;}
                Trace("sdl-quit-event");
                running = false;
                dashboard.StopMusic();
                Trace("system-quit-media-stop-complete");
            } else if (event.type == SDL_APP_WILLENTERBACKGROUND) {
                heapOwnerWatch.Stop();
                dashboard.StopScan();
                updater.Stop();
                Trace("sdl-background-event");
                dashboard.PrepareSystemBackground();
                foreground = false;
                touchHeld=false;
                wiiRemote.Reset();
                inputResume.Arm();
                heldHorizontal=heldVertical=0;
                // Audio is already closed before the next SDL pump releases AX.
                Trace("background-input-reset-complete-v2");
            } else if (event.type == SDL_APP_DIDENTERFOREGROUND) {
                heapOwnerWatch.Start(visualRemote.Enabled() && !launchRequested);
                foreground = true;
                dashboard.ResumeSystemForeground();
                wiiRemote.Reset();
                inputResume.Arm();
                heldHorizontal=heldVertical=0;
                touchHeld=false;
                Trace("foreground-input-reset-complete-v2");
                SDL_RenderSetVSync(renderer,1);
                previousCounter=SDL_GetPerformanceCounter();
            }
        }
        if (!running) break;
        // No drawing or storage access once a title transition has started.
        // Keep pumping events so SDL can release the foreground to the next app.
        if (!foreground || launchRequested) {
            if (launchRequested) { heapOwnerWatch.Stop(); visualRemote.Stop(); }
            OSSleepTicks(OSMillisecondsToTicks(4));
            continue;
        }
        const Uint32 ticks=SDL_GetTicks();
        heapOwnerWatch.PollForeground();
        const Uint64 counter=SDL_GetPerformanceCounter();
        const float delta=std::min(.1f,static_cast<float>((counter-previousCounter)/counterFrequency)); previousCounter=counter;
        const std::string visualCommand = visualRemote.Poll();
        if (visualCommand == "stats") visualRemote.ReplyStats(dashboard.PerformanceStats());
        else if (visualCommand == "videostats") visualRemote.ReplyStats(dashboard.VideoStats());
        else if (visualCommand == "videoisolateon" || visualCommand == "videoisolateoff") {
            dashboard.IsolateVideo(visualCommand == "videoisolateon");
            visualRemote.ReplyStats(dashboard.VideoStats());
        }
        else if (visualCommand == "videowakeon" || visualCommand == "videowakeoff") {
            VideoDecoderWakePriority(visualCommand == "videowakeon");
            visualRemote.ReplyStats("{\"decoder_wake_priority\":" + std::string(visualCommand == "videowakeon"?"true":"false") + "}");
        }
        else if (visualCommand == "next" || visualCommand == "previous") {
            if (!dashboard.OverlayOpen()) dashboard.Move(visualCommand == "next" ? 1 : -1);
            visualRemote.ReplyStats(dashboard.VideoStats());
        }
        else if (visualCommand == "threads") {
            const unsigned count = ThreadDiagnostics::Dump();
            visualRemote.ReplyStats("{\"threads_logged\":" + std::to_string(count) + "}");
        }
        else if (visualCommand == "pageforward" || visualCommand == "pageback") {
            if (!dashboard.OverlayOpen()) dashboard.Page(visualCommand == "pageforward" ? 1 : -1, false);
            visualRemote.ReplyStats(dashboard.PerformanceStats());
        }
        else if (!visualCommand.empty()) {
            const bool accepted = visualCommand == "capture" || dashboard.VisualPreview(visualCommand);
            visualRemote.Reply(accepted && dashboard.Render(0, "fs:/vol/external01/wiiu/apps/uFlow/visual.png"));
            continue;
        }
        VPADStatus input{}; VPADReadError error;
        const bool gamePadReady=VPADRead(VPAD_CHAN_0,&input,1,&error)>0&&error==VPAD_READ_SUCCESS;
        if(!gamePadReady) input={};
        dashboard.UpdateSystemIndicators(gamePadReady,input.battery);
        input.hold&=0x0006fffcu;input.trigger&=0x0006fffcu; // Rebuild stick directions from analog axes, not VPAD emulation bits.
        wiiRemote.Merge(input,ticks);
        static uint32_t previousStickDirections=0;
        const uint32_t stickDirections=StickButton::Directions(input.leftStick.x,input.leftStick.y)|StickButton::RightDirections(input.rightStick.x,input.rightStick.y);
        input.hold|=stickDirections;input.trigger|=stickDirections&~previousStickDirections;
        previousStickDirections=stickDirections;
        dashboard.SetControlHeld(input.hold|input.trigger,input.trigger);
        if(dashboard.OverlayOpen()||input.trigger)dashboard.CancelHomeSwipe();
        if(dashboard.CapturingBinding()) {
            dashboard.CaptureBinding(input.hold|input.trigger);
            // The assignment gesture belongs only to capture, including its hold.
            inputResume.Arm();heldHorizontal=heldVertical=heldPage=0;touchHeld=false;
            dashboard.Render(delta);continue;
        }
        if(dashboard.DemoInput(input.hold || input.trigger || input.tpNormal.touched ||
            std::abs(input.leftStick.x)>.28f || std::abs(input.leftStick.y)>.32f ||
            std::abs(input.rightStick.x)>.28f || std::abs(input.rightStick.y)>.32f)) {
            // The wake-up press exits the demo; it must not also launch a game.
            dashboard.CancelHomeSwipe(true);
            inputResume.Arm();heldHorizontal=heldVertical=heldPage=0;touchHeld=false;
            dashboard.VideoInput(true);dashboard.Render(delta);continue;
        }
        if(!updateChecked&&mounted&&ticks>5000){updater.Check();updateChecked=true;}
        const auto updateStatus=updater.Poll();dashboard.UpdateState(updateStatus);
        if(updateStatus.state==UFlowUpdate::State::Handoff){
            updater.Stop();dashboard.StopMusic();dashboard.Render(delta);
            auto result=RPXLoader_PrepareLaunchFromSD(UFlowUpdate::HelperRelativePath);
            if(result==RPX_LOADER_RESULT_SUCCESS)result=RPXLoader_LaunchPreparedHomebrew();
            if(result==RPX_LOADER_RESULT_SUCCESS){launchRequested=true;continue;}
            updater.HandoffFailed(std::string("Cannot start update helper: ")+RPXLoader_GetStatusStr(result));
            continue;
        }
        if(updateHomeBlocked&&!UFlowUpdate::Busy(updateStatus.state)){OSEnableHomeButtonMenu(true);updateHomeBlocked=false;}
        if(dashboard.ScanOpen()){
            bool tapped=false;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                tapped=point.touched && point.validity==VPAD_VALID && !touchHeld && point.x>=490 && point.x<790 && point.y>=482 && point.y<534;
                touchHeld=point.touched;}
            if(input.trigger&VPAD_BUTTON_B)dashboard.CancelScan();
            dashboard.ScanInput((input.trigger&(VPAD_BUTTON_A|VPAD_BUTTON_B)) || tapped);
            heldHorizontal=heldVertical=0;dashboard.Render(delta);continue;
        }
        if(dashboard.PreparingIcons()) {
            if(input.trigger&VPAD_BUTTON_B) dashboard.SkipIconPreparation();
            heldHorizontal=heldVertical=heldPage=0; touchHeld=false;
            dashboard.Render(delta);continue;
        }
        if(dashboard.UpdateOpen()){
            int x=-1,y=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!updateTouchHeld){x=point.x;y=point.y;}}
            updateTouchHeld=gamePadReady&&point.touched;
            int direction=popupDirection(input);
            int action=dashboard.UpdateInput(direction,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,x,y);
            if(action==1){OSEnableHomeButtonMenu(false);updateHomeBlocked=true;updater.Accept();}
            else if(action==-1)updater.Dismiss();
            heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        if(dashboard.PowerPromptOpen()||dashboard.GalleryOpen()) {
            const int dx=(dashboard.ButtonPressed(ControlAction::NavigateRight,input.trigger)?1:0)-(dashboard.ButtonPressed(ControlAction::NavigateLeft,input.trigger)?1:0);
            const int dy=((dashboard.ButtonPressed(ControlAction::NavigateDown,input.trigger)||(input.trigger&StickButton::Down))?1:0)-((dashboard.ButtonPressed(ControlAction::NavigateUp,input.trigger)||(input.trigger&StickButton::Up))?1:0);
            if(gamePadReady){VPADTouchData point{};VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!touchHeld)dashboard.SystemOverlayTouch(point.x,point.y);touchHeld=point.touched;}
            if(dashboard.GalleryOpen())dashboard.GalleryInput(dx,dy,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B);
            else dashboard.HomeNavigate(dx,dy,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B);
            if(dashboard.ConsumeShutdown()){dashboard.StopMusic();OSShutdown();running=false;continue;}
            dashboard.Render(delta);continue;
        }
        if(dashboard.CanOpenAlphabet() && dashboard.ButtonPressed(ControlAction::Alphabet,input.trigger))dashboard.OpenAlphabet();
        if(dashboard.AlphabetOpen()){
            int touch=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!launchOptionsTouchHeld && point.y>=331&&point.y<385)
                    touch=dashboard.AlphabetTouchIndex(point.x);}
            launchOptionsTouchHeld=gamePadReady&&point.touched;
            const auto alphabetHold=input.hold&~dashboard.AlphabetConsumedBinding();
            const auto alphabetTrigger=input.trigger&~dashboard.AlphabetConsumedBinding();
            const int direction=(alphabetHold&VPAD_BUTTON_RIGHT?1:0)-(alphabetHold&VPAD_BUTTON_LEFT?1:0);
            dashboard.AlphabetInput(input.leftStick.x,direction,ticks,dashboard.ButtonHeld(ControlAction::Alphabet,input.hold),
                alphabetTrigger&(VPAD_BUTTON_STICK_L|VPAD_BUTTON_X),alphabetTrigger&VPAD_BUTTON_A,alphabetTrigger&VPAD_BUTTON_B,touch);
            heldHorizontal=heldVertical=heldPage=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        if(dashboard.SearchOpen()){
            int x=-1,y=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!launchOptionsTouchHeld){x=point.x;y=point.y;}}
            launchOptionsTouchHeld=gamePadReady&&point.touched;
            const int horizontal=(input.hold&VPAD_BUTTON_RIGHT?1:0)-(input.hold&VPAD_BUTTON_LEFT?1:0);
            const int vertical=(input.hold&VPAD_BUTTON_DOWN?1:0)-(input.hold&VPAD_BUTTON_UP?1:0);
            dashboard.SearchNavigate(input.leftStick.x,input.leftStick.y,horizontal,vertical,ticks);
            dashboard.SearchInput(0,0,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,
                input.trigger&VPAD_BUTTON_X,input.trigger&VPAD_BUTTON_PLUS,input.trigger&VPAD_BUTTON_Y,x,y);
            heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        if(dashboard.SortOpen()){
            int x=-1,y=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!launchOptionsTouchHeld){x=point.x;y=point.y;}}
            launchOptionsTouchHeld=gamePadReady&&point.touched;
            dashboard.SortInput(popupDirection(input),input.trigger&VPAD_BUTTON_A,(input.trigger&VPAD_BUTTON_B)||dashboard.ButtonPressed(ControlAction::Sort,input.trigger),x,y,input.trigger&VPAD_BUTTON_X,input.trigger&VPAD_BUTTON_Y);
            heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        if(dashboard.LaunchOptionsOpen()){
            int x=-1,y=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!launchOptionsTouchHeld){x=point.x;y=point.y;}}
            launchOptionsTouchHeld=gamePadReady&&point.touched;
            const int direction=popupDirection(input);
            dashboard.LaunchOptionsInput(direction,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,x,y,
                                         input.trigger&VPAD_BUTTON_PLUS);
            heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        std::uint32_t previewHold=input.hold;

        const bool inspectionInput=dashboard.CoverInspectOpen() ||
            dashboard.ButtonHeld(ControlAction::ZoomFront,previewHold) || dashboard.ButtonHeld(ControlAction::ZoomBack,previewHold);
        dashboard.VideoInput(!(inspectionInput && dashboard.VideoBehindInspection()) && (input.hold || input.trigger || input.tpNormal.touched ||
            std::abs(input.leftStick.x)>.28f || std::abs(input.leftStick.y)>.32f ||
            std::abs(input.rightStick.x)>.28f || std::abs(input.rightStick.y)>.32f));
        const bool neutral = input.hold==0 && input.trigger==0 &&
            std::abs(input.leftStick.x)<.28f && std::abs(input.leftStick.y)<.32f &&
            std::abs(input.rightStick.x)<.28f && std::abs(input.rightStick.y)<.32f && !input.tpNormal.touched;
        if(!inputResume.Allows(ticks,neutral)) {
            heldHorizontal=heldVertical=0;
            touchHeld=false;
            dashboard.Render(delta);
            continue;
        }
        // Remote input must work even when the GamePad is off or has no sample.
        if (!launchRequested) {
            if(dashboard.WelcomeOpen()) {
                bool tapped=false;
                if(gamePadReady){VPADTouchData point{};VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                    tapped=point.touched&&point.validity==VPAD_VALID&&point.x>=500&&point.x<=780&&point.y>=420&&point.y<=470;}
                const bool settings=dashboard.ButtonPressed(ControlAction::Settings,input.trigger);
                if(tapped || (input.trigger&(VPAD_BUTTON_A|VPAD_BUTTON_B)) || settings)dashboard.DismissWelcome(settings);
                heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
            }
            if (dashboard.PreviewActive()) {
                if (input.trigger & VPAD_BUTTON_B) dashboard.VisualPreview("live");
                dashboard.Render(delta);
                continue;
            }
            const bool menuNavigation=dashboard.OverlayOpen();
            const int horizontal=menuNavigation?(((input.hold&(VPAD_BUTTON_LEFT|StickButton::Left))||input.leftStick.x<-.42f)?-1:((input.hold&(VPAD_BUTTON_RIGHT|StickButton::Right))||input.leftStick.x>.42f)?1:0):
                dashboard.ButtonHeld(ControlAction::NavigateLeft,input.hold)?-1:dashboard.ButtonHeld(ControlAction::NavigateRight,input.hold)?1:0;
            const int vertical=menuNavigation?(((input.hold&(VPAD_BUTTON_UP|StickButton::Up))||input.leftStick.y>.48f)?-1:((input.hold&(VPAD_BUTTON_DOWN|StickButton::Down))||input.leftStick.y<-.48f)?1:0):
                dashboard.ButtonHeld(ControlAction::NavigateUp,input.hold)?-1:dashboard.ButtonHeld(ControlAction::NavigateDown,input.hold)?1:0;
            const bool horizontalEdge=horizontal!=heldHorizontal;
            const bool verticalEdge=vertical!=heldVertical;
            if(horizontalEdge){
                heldHorizontal=horizontal;
                horizontalStarted=ticks;
                horizontalNext=ticks+BrowseMotion::InitialDelay(dashboard.AnimationSpeed());
            }
            if(verticalEdge) { heldVertical=vertical; verticalNext=ticks+360; }
            const bool verticalRepeat=vertical && !verticalEdge && SDL_TICKS_PASSED(ticks,verticalNext);
            if(verticalRepeat) verticalNext=ticks+125;
            std::uint32_t actionTrigger=input.trigger;
            std::uint32_t actionHold=input.hold;
            // Holding ZL/ZR repeats the page jump.
            const int page=dashboard.ButtonHeld(ControlAction::PageForward,actionHold)?1:
                           dashboard.ButtonHeld(ControlAction::PageBack,actionHold)?-1:0;
            if(page!=heldPage){heldPage=page;pageNext=ticks+400;}
            const bool pageRepeat=page!=0&&SDL_TICKS_PASSED(ticks,pageNext);
            if(pageRepeat)pageNext=ticks+180;
            VPADTouchData touch{};
            const bool modalInput=dashboard.InjectionDialogOpen();
            if(gamePadReady) VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&input.tpNormal);
            else touchHeld=false; // a missing sample is not a touch release/click
            if(!dashboard.OverlayOpen()&&dashboard.HomeSwipeInput(gamePadReady,touch.validity==VPAD_VALID,touch.touched,touch.x,touch.y)) {
                heldHorizontal=heldVertical=heldPage=0;touchHeld=false;
                dashboard.Render(delta);continue;
            }
            if(touch.touched && touch.validity==VPAD_VALID && (modalInput || dashboard.SettingsOpen() || dashboard.DetailsOpen())) {
                if(!touchHeld) {
                    touchStartX=touch.x; touchStartY=touch.y;
                    zoomSliderHeld=!modalInput && !dashboard.DetailsOpen() && dashboard.SettingsZoomTouch(touch.x,touch.y);
                } else if(zoomSliderHeld)dashboard.SettingsZoomTouch(touch.x,touch.y,true);
                touchLastX=touch.x; touchLastY=touch.y; touchHeld=true;
            } else if(!touch.touched && touchHeld) {
                touchHeld=false;
                if(!zoomSliderHeld && std::abs(touchLastX-touchStartX)<60) {
                    if(modalInput)dashboard.InjectionTouch(touchStartX,touchStartY);
                    else if(dashboard.DetailsOpen()) dashboard.DetailsTouch(touchStartX,touchStartY,touchLastY-touchStartY);
                    else dashboard.SettingsTouch(touchStartX,touchStartY,touchLastY-touchStartY);
                }
            } else if(touch.touched && touch.validity!=VPAD_VALID) touchHeld=false;
            if(!modalInput&&!dashboard.OverlayOpen()) {
                if(dashboard.ButtonPressed(ControlAction::NextLayout,actionTrigger)){
                    dashboard.NextLayout();heldHorizontal=heldVertical=heldPage=0;
                    dashboard.Render(delta);continue;
                }
                int move=horizontalEdge?horizontal:0;
                if(horizontal && !horizontalEdge && SDL_TICKS_PASSED(ticks,horizontalNext)) {
                    const auto repeat=BrowseMotion::HeldRepeat(ticks-horizontalStarted,dashboard.AnimationSpeed());
                    move=horizontal*repeat.stride;horizontalNext=ticks+repeat.interval;
                }
                if(dashboard.ButtonPressed(ControlAction::FocusSystemButtons,actionTrigger))dashboard.FocusSystemButtons();
                const bool consumed=dashboard.HomeNavigate(move,(verticalEdge||verticalRepeat)?vertical:0,
                    dashboard.ButtonPressed(ControlAction::Launch,actionTrigger),input.trigger&VPAD_BUTTON_B);
                if(dashboard.ConsumeShutdown()){dashboard.StopMusic();OSShutdown();running=false;continue;}
                if(consumed){dashboard.Render(delta);continue;}
            }
            const bool libraryInput=!dashboard.OverlayOpen();
            const bool inspectFrontHeld=dashboard.ButtonHeld(ControlAction::ZoomFront,actionHold);
            const bool inspectBackHeld=dashboard.ButtonHeld(ControlAction::ZoomBack,actionHold);
            if(modalInput)dashboard.InjectionInput(horizontalEdge?horizontal:0,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B);
            else if (dashboard.CoverInspectOpen()) {
                if(input.trigger&VPAD_BUTTON_B)dashboard.CloseCoverInspect();
                else if(inspectFrontHeld)dashboard.OpenCoverInspect(false);
                else if(inspectBackHeld)dashboard.OpenCoverInspect(true);
                else dashboard.CloseCoverInspect();
            }
            else if (dashboard.SettingsOpen()) {
                dashboard.SettingsInput(horizontalEdge?horizontal:0,(verticalEdge||verticalRepeat)?vertical:0,
                    input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,input.trigger&VPAD_BUTTON_X,
                    input.trigger&VPAD_BUTTON_Y,input.trigger&VPAD_BUTTON_PLUS);
            }
            else if (dashboard.DetailsOpen()) {
                dashboard.DetailsInput(horizontalEdge?horizontal:0,(verticalEdge||verticalRepeat)?vertical:0,
                    input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,input.trigger&VPAD_BUTTON_PLUS,
                    dashboard.ButtonPressed(ControlAction::Details,actionTrigger));
            }
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::Sort,actionTrigger))dashboard.FindOrClearSearch();
            else if (input.trigger&VPAD_BUTTON_B) {
                dashboard.DismissVideo();
                if(dashboard.OverlayOpen()) dashboard.CloseOverlay();
            }
            else if(inspectFrontHeld)dashboard.OpenCoverInspect(false);
            else if(inspectBackHeld)dashboard.OpenCoverInspect(true);
            else if(dashboard.ButtonPressed(ControlAction::Settings,actionTrigger))dashboard.ToggleSettings();
            else if(dashboard.ButtonPressed(ControlAction::Details,actionTrigger))dashboard.ToggleDetails();
            else if(dashboard.ButtonPressed(ControlAction::Favorite,actionTrigger))dashboard.ToggleFavorite();
            else if(dashboard.ButtonPressed(ControlAction::PreviousLibrary,actionTrigger))dashboard.ChangeTab(-1);
            else if(dashboard.ButtonPressed(ControlAction::NextLibrary,actionTrigger))dashboard.ChangeTab(1);
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::PageBack,actionTrigger))dashboard.Page(-1,true);
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::PageForward,actionTrigger))dashboard.Page(1,true);
            else if(!dashboard.OverlayOpen()&&pageRepeat)dashboard.Page(heldPage,false);
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::LaunchOptions,actionTrigger))dashboard.OpenLaunchOptions();
            GameEntry game=dashboard.Selected()?*dashboard.Selected():GameEntry{};
            const bool dialogLaunch=dashboard.ConsumeInjectionLaunch(game);
            const bool installedLaunch=dashboard.ConsumeInstalledLaunch(game);
            LaunchMethod launchMethod=LaunchMethod::Default;
            const bool optionLaunch=dashboard.ConsumeLaunchOption(launchMethod);
            if((installedLaunch || dialogLaunch || optionLaunch || (!modalInput&&(dashboard.ConsumeDetailsLaunch() || (libraryInput && !dashboard.OverlayOpen() &&
                dashboard.ButtonPressed(ControlAction::Launch,actionTrigger))))) && dashboard.Selected()) {
                if(dashboard.GateTitleInstall(game)){dashboard.Render(delta);continue;}
                const GameEntry historyGame=game;
                dashboard.DismissVideo();
                if(!optionLaunch&&!dialogLaunch)dashboard.ClearLaunchOverride(); // plain A uses the default
                if(!dialogLaunch&&dashboard.GateInjection(game)){dashboard.Render(delta);continue;}
                if(dashboard.InjectionDialogOpen())dashboard.Render(delta);
                std::string emulatorPath,romRequest;
                PreparationProgress preparationProgress;bool progressTouch=false;
                // Share the live event/input pump with Wii VC conversion. Merely
                // redrawing its popup never delivers B/touch to the cancel flag.
                const auto prepareProgress=[&](const std::string &phase,unsigned long long bytes,unsigned long long total){
                    const Uint32 now=SDL_GetTicks();
                    return preparationProgress.Tick(now,[&]{
                        SDL_Event pending;
                        while(SDL_PollEvent(&pending)) {
                            if(pending.type==SDL_QUIT){running=false;dashboard.StopMusic();return false;}
                            if(pending.type==SDL_APP_WILLENTERBACKGROUND){dashboard.PrepareSystemBackground();foreground=false;touchHeld=false;wiiRemote.Reset();inputResume.Arm();heldHorizontal=heldVertical=0;Trace("vc-background-input-reset-complete-v2");return false;}
                        }
                        VPADStatus pad{};VPADReadError error;
                        const bool ready=VPADRead(VPAD_CHAN_0,&pad,1,&error)>0&&error==VPAD_READ_SUCCESS;
                        if(!ready)pad={};
                        wiiRemote.Merge(pad,now);
                        if((pad.trigger|pad.hold)&VPAD_BUTTON_B)dashboard.InjectionInput(0,false,true);
                        VPADTouchData point{};if(ready)VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&pad.tpNormal);
                        if(point.touched&&point.validity==VPAD_VALID&&!progressTouch)dashboard.InjectionTouch(point.x,point.y);
                        progressTouch=point.touched;
                        return running&&foreground&&!dashboard.InjectionCancelled();
                    },[&]{
                        if(running&&foreground){dashboard.InjectionProgress(phase,bytes,total);dashboard.Render(.08f);}
                    });
                };
                const auto adapterError=dashboard.ConfigureLaunch(game,emulatorPath,&romRequest,prepareProgress);
                if(!adapterError.empty())Trace(("launch-adapter-error "+adapterError).c_str());
                dashboard.FinishInjection(adapterError);
                inputResume.Arm();heldHorizontal=heldVertical=0;touchHeld=false;
                if(!adapterError.empty())continue;
                if(dashboard.ConsumeInjectionFinished()){dashboard.SetStatus("INJECTION SAVED - PLAY NOW USES SAVED INJECTED ROM",10000);continue;}
                // Complete the visual transition before dispatching any title.
                // Keep SDL/ProcUI events alive: HOME/background or B cancels it.
                const auto animateLaunch=[&] {
                    if(!dashboard.BeginLaunchAnimation())return true;
                    const Uint32 started=SDL_GetTicks();
                    bool completed=false;
                    while(running&&foreground) {
                        SDL_Event event;
                        while(SDL_PollEvent(&event)) {
                            if(event.type==SDL_QUIT)running=false;
                            if(event.type==SDL_APP_WILLENTERBACKGROUND) {
                                dashboard.PrepareSystemBackground();foreground=false;
                                heapOwnerWatch.Stop();
                            }
                        }
                        if(!running||!foreground)break;
                        VPADStatus pad{};VPADReadError error;
                        VPADRead(VPAD_CHAN_0,&pad,1,&error);
                        if(error!=VPAD_READ_SUCCESS)pad={};
                        wiiRemote.Merge(pad,SDL_GetTicks());
                        if(pad.trigger&VPAD_BUTTON_B)break;
                        heapOwnerWatch.PollForeground();
                        const float progress=(SDL_GetTicks()-started)/static_cast<float>(dashboard.LaunchAnimationDuration());
                        dashboard.DrawLaunchAnimation(progress);
                        if(progress>=1.f){completed=true;break;}
                    }
                    dashboard.EndLaunchAnimation();
                    inputResume.Arm();heldHorizontal=heldVertical=0;touchHeld=false;
                    return completed;
                };
                const auto launchPlan=dashboard.ConsumeLaunchPlan();
                if(launchPlan.transport==HomebrewLaunchPlan::Transport::RetroArchArgs) {
                    Trace(("retroarch-launch-selected core="+launchPlan.executable+" content="+game.absolutePath).c_str());
                    if(!launchWithArgs) {
                        dashboard.SetStatus("UPDATE THE UFLOW LOADER MODULE FOR RETROARCH LAUNCHING",10000);
                        Trace("retroarch-export-missing");
                        continue;
                    }
                    if(!animateLaunch())continue;
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto result=launchWithArgs(launchPlan.executable.c_str(),launchPlan.contentBundle.c_str(),
                                                     launchPlan.arguments.data(),
                                                     static_cast<uint32_t>(launchPlan.arguments.size()));
                    Trace((std::string("retroarch-launch-result ")+RPXLoader_GetStatusStr(result)).c_str());
                    if(result==RPX_LOADER_RESULT_SUCCESS){dashboard.RecordPlayed(historyGame);launchRequested=true;}
                    else{UFlowReturn::Clear();dashboard.ApplyMusic();
                         dashboard.SetStatus(std::string("RETROARCH LAUNCH FAILED  ")+RPXLoader_GetStatusStr(result),10000);}
                } else if(!emulatorPath.empty()) {
                    if(!animateLaunch())continue;
                    if(!romRequest.empty()&&!UFlowRom::Write(romRequest,UFlowRom::RelativeROM(game.absolutePath))){dashboard.SetStatus("CANNOT WRITE MODULE LAUNCH REQUEST",10000);continue;}
                    const auto prepared=RPXLoader_PrepareLaunchFromSD(emulatorPath.c_str());
                    if(prepared!=RPX_LOADER_RESULT_SUCCESS){if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.SetStatus(RPXLoader_GetStatusStr(prepared),10000);continue;}
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto result=RPXLoader_LaunchPreparedHomebrew();
                    if(result==RPX_LOADER_RESULT_SUCCESS){if(!romRequest.empty())dashboard.RecordPlayed(historyGame);launchRequested=true;}
                    else{UFlowReturn::Clear();if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.ApplyMusic();dashboard.SetStatus(RPXLoader_GetStatusStr(result),10000);}
                } else if(game.installed) {
                    if(!animateLaunch())continue;
                    std::string launchPath=game.absolutePath;
                    if(launchPath.rfind("fs:",0)==0)launchPath.erase(0,3);
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    dashboard.RecordPlayed(historyGame);
                    _SYSLaunchTitleByPathFromLauncher(launchPath.c_str(),static_cast<uint32_t>(launchPath.size()));
                    launchRequested=true;
                } else if(game.platform==Platform::Wii || game.platform==Platform::GameCube) {
                    WiiRequest::Request request;
                    const bool gc = game.platform==Platform::GameCube;
                    const auto error = gc ? PrepareGameCubeLaunch(game, request) : PrepareWiiLaunch(game, request);
                    if (!error.empty()) {
                        Trace(("wii-preflight-failed " + error).c_str());
                        dashboard.SetStatus(error, 10000);
                        continue;
                    }
                    Trace((std::string(gc ? "gamecube-launch " : "wii-launch ") + request.id + " " + request.path).c_str());
                    if(!animateLaunch())continue;
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto launchError = LaunchWiiGame(request);
                    if (launchError.empty()) {
                        Trace("wii-forwarder-launch-accepted");
                        dashboard.RecordPlayed(historyGame);
                        launchRequested = true;
                    } else {
                        UFlowReturn::Clear();
                        Trace(("wii-launch-failed " + launchError).c_str());
                        dashboard.ApplyMusic(); dashboard.SetStatus(launchError,10000);
                    }
                } else if(game.platform!=Platform::WiiU) dashboard.SetStatus(std::string(PlatformName(game.platform))+" LAUNCH ADAPTER IS NEXT");
                else {
                    // Authored Wii VC titles launch through Nintendo's HAI wrapper
                    // (the raw backend gives it the USB sectors of hif_*.nfs).
                    // Injection - a WBFS copy for WiiFlow - only runs when chosen
                    // explicitly from Launch Options; the title is never modified.
                    const bool wiiVC=WiiVC::IsRaw(game);
                    if (wiiVC && launchMethod==LaunchMethod::Injection) {
                        Trace(("wii-vc-prepare-selected path=" + game.absolutePath + " title=" + game.titleId).c_str());
                        GameEntry prepared = game;
                        dashboard.BeginWiiVCPreparation(game.name);
                        dashboard.Render(.08f);
                        const auto prepareError = WiiVC::Prepare(prepared, prepareProgress);
                        dashboard.FinishInjection(prepareError);
                        inputResume.Arm();heldHorizontal=heldVertical=0;touchHeld=false;
                        if (!prepareError.empty()) { Trace(("wii-vc-prepare-failed " + prepareError).c_str()); continue; }
                        WiiRequest::Request request;
                        const auto error = PrepareWiiLaunch(prepared, request);
                        if (!error.empty()) { Trace(("wii-vc-preflight-failed " + error).c_str()); dashboard.SetStatus(error, 10000); continue; }
                        if(!animateLaunch())continue;
                        dashboard.StopMusic();
                        if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                        const auto launchError = LaunchWiiGame(request);
                        if (launchError.empty()) { Trace("wii-vc-injection-launch-accepted"); dashboard.RecordPlayed(historyGame); launchRequested = true; }
                        else { UFlowReturn::Clear(); Trace(("wii-vc-launch-failed " + launchError).c_str()); dashboard.ApplyMusic(); dashboard.SetStatus(launchError, 10000); }
                        continue;
                    }
                    Trace(((wiiVC?"wii-vc-hai-launch-selected path=":"raw-launch-selected path=") + game.absolutePath + " title=" + game.titleId).c_str());
                    const auto validation = ValidateRawGame(game);
                    if (!validation.empty()) {
                        Trace(("raw-launch-preflight-failed " + validation).c_str());
                        dashboard.SetStatus(validation, 10000);
                        continue;
                    }
                    Trace("raw-launch-preflight-passed");
                    if (!launchLoose) {
                        Trace("raw-launch-blocked-missing-backend");
                        dashboard.SetStatus("RAW LAUNCH BACKEND IS NOT INSTALLED", 10000);
                        continue;
                    }
                    if (wiiVC && !LoaderSupportsWiiVCHai()) {
                        Trace("wii-vc-hai-backend-missing");
                        dashboard.SetStatus("WII VC NEEDS THE UPDATED RAW LAUNCH BACKEND", 10000);
                        continue;
                    }
                    if(!animateLaunch())continue;
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const RPXLoaderStatus result=launchLoose(game.rpxRelative.c_str(),game.contentRelative.c_str(),game.codeRelative.c_str(),
                            game.saveRelative.c_str(),game.name.c_str(),game.name.c_str(),game.publisher.c_str());
                    Trace((std::string("raw-launch-result ")+RPXLoader_GetStatusStr(result)).c_str());
                    if(result==RPX_LOADER_RESULT_SUCCESS){dashboard.RecordPlayed(historyGame);launchRequested=true;}
                    else { UFlowReturn::Clear();dashboard.ApplyMusic(); dashboard.SetStatus(std::string("LAUNCH FAILED  ")+RPXLoader_GetStatusStr(result),10000); }
                }
            }
            const auto requestedHomebrew=dashboard.ConsumeHomebrewRequest();
            if(running&&!launchRequested&&!requestedHomebrew.empty()) {
                dashboard.SetStatus("OPENING EMULATOR ROM BROWSER",10000);dashboard.Render(delta);
                const auto prepared=RPXLoader_PrepareLaunchFromSD(requestedHomebrew.c_str());
                if(prepared==RPX_LOADER_RESULT_SUCCESS) {
                    if(!UFlowReturn::Arm()){dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    dashboard.StopMusic();const auto result=RPXLoader_LaunchPreparedHomebrew();
                    if(result==RPX_LOADER_RESULT_SUCCESS)launchRequested=true;
                    else{UFlowReturn::Clear();dashboard.ApplyMusic();dashboard.SetStatus(RPXLoader_GetStatusStr(result),10000);}
                } else dashboard.SetStatus(RPXLoader_GetStatusStr(prepared),10000);
            }
            if (running && !launchRequested && dashboard.ConsumeScanRequest()) {
                dashboard.StartScan();
            }
        }
        if (running && !launchRequested) dashboard.Render(delta);
        // Render is already paced by the display flip. An additional sleep here
        // steals 4ms from the next frame's budget. Background event-pumping above
        // still sleeps, and does not submit any rendering work.
    }
    heapOwnerWatch.Stop();
    Trace("render-loop-stopped");
    }
    Trace("dashboard-destroyed");
    ShutdownSmoothFonts();
    Trace("fonts-destroyed");
    SDL_DestroyRenderer(renderer);
    Trace("renderer-destroyed");
    SDL_DestroyWindow(window);
    Trace("window-destroyed");
    if(launchArgsModule) {
        OSDynLoad_Release(launchArgsModule);
        Trace("retroarch-loader-module-released");
    }
    if(launchLooseModule) {
        OSDynLoad_Release(launchLooseModule);
        Trace("loose-loader-module-released");
    }
    if(loaderStatus==RPX_LOADER_RESULT_SUCCESS) {
        RPXLoader_DeInitLibrary();
        Trace("rpx-loader-deinitialized");
    }
    // Keep the SD mounted through SDL shutdown for the final clean-exit
    // checkpoint. Earlier teardown events stay in the live system log.
    Trace("sdl-quit-begin");
    USBStorage::Unmount();
    SDL_Quit();
    Trace("sdl-quit-complete");
    if(mounted) {
        Trace("sd-unmount-request",true);
        WHBUnmountSdCard();
    }
    OSReport("uFlow: returning-from-main\n");
    return 0;
}
#else
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const bool gpuPreview = SDL_getenv("UFLOW_PREVIEW_GPU") != nullptr;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, gpuPreview ? "opengl" : "software");
    if (SDL_Init(SDL_INIT_VIDEO)) return 3;
    SDL_Window *window = SDL_CreateWindow("uFlow visual preview", 0, 0, kWidth, kHeight, SDL_WINDOW_HIDDEN);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1,
        (gpuPreview ? SDL_RENDERER_ACCELERATED : SDL_RENDERER_SOFTWARE) | SDL_RENDERER_TARGETTEXTURE) : nullptr;
    if (!renderer) return 4;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); InitSmoothFonts();
    bool ok;
    { std::vector<GameEntry> games; Dashboard dashboard(renderer, games);
      if(std::string(argv[1])=="draftchecks")ok=dashboard.CheckDraftExit(argv[2]);
      else if(std::string(argv[1])=="scanchecks")ok=dashboard.CheckScanDialog(argv[2]);
      else if(std::string(argv[1])=="asyncbrowserchecks")ok=dashboard.CheckAsyncBrowser(argv[2]);
      else if(std::string(argv[1])=="asyncartworkchecks")ok=dashboard.CheckAsyncArtwork(argv[2]);
      else if(std::string(argv[1])=="artworkmemorychecks")ok=dashboard.CheckArtworkWorkingSet(argv[2]);
      else if(std::string(argv[1])=="detailsvideochecks")ok=dashboard.CheckDetailsMediaVideo(argv[2]);
      else if(std::string(argv[1])=="homeupgradechecks")ok=dashboard.CheckHomeUpgrade(argv[2]);
      else if(std::string(argv[1])=="switchchecks")ok=dashboard.CheckSwitch(argv[2]);
      else if(std::string(argv[1])=="demochecks")ok=dashboard.CheckDemo(argv[2]);
      else if(std::string(argv[1])=="titleinstallchecks")ok=dashboard.CheckTitleInstaller();
      else if(std::string(argv[1])=="alphabetchecks")ok=dashboard.CheckAlphabet(argv[2]);
      else if(std::string(argv[1])=="searchchecks")ok=dashboard.CheckSearch(argv[2]);
      else if(std::string(argv[1])=="sortingchecks")ok=dashboard.CheckSorting(argv[2]);
      else if(std::string(argv[1])=="wrapdisplaychecks")ok=dashboard.CheckWrapDisplay(argv[2]);
      else if(std::string(argv[1])=="startupanimationchecks")ok=dashboard.CheckStartupAnimation(argv[2]);
      else if(std::string(argv[1])=="iconpreparationchecks")ok=dashboard.CheckIconPreparation(argv[2]);
      else if(std::string(argv[1])=="glassbackgroundchecks")ok=dashboard.CheckGlassBackground(argv[2]);
      else if(std::string(argv[1])=="backgroundchecks")ok=dashboard.CheckBackgroundImages(argv[2]);
      else if(std::string(argv[1])=="accentchecks")ok=dashboard.CheckAccentThemes(argv[2]);
      else if(std::string(argv[1])=="launchanimationchecks")ok=dashboard.CheckLaunchAnimation(argv[2]);
      else if(std::string(argv[1])=="coverfacechecks")ok=dashboard.CheckCoverFaces(argv[2]);
      else if(std::string(argv[1])=="firstrunchecks")ok=dashboard.CheckFirstRun(argv[2]);
      else if(std::string(argv[1])=="updatechecks")ok=dashboard.CheckUpdateDialog(argv[2]);
      else if(std::string(argv[1])=="browserchecks")ok=dashboard.CheckBrowserNavigation(argv[2]);
      else if(std::string(argv[1])=="vcsortchecks")ok=dashboard.CheckVCSortSettings(argv[2]);
      else if(std::string(argv[1])=="contentmediachecks")ok=dashboard.CheckContentTypeMediaUI(argv[2]);
      else if(std::string(argv[1])=="translationchecks")ok=dashboard.CheckCaseTranslation();
      else if(std::string(argv[1])=="coverzoomchecks")ok=dashboard.CheckCoverZoom(argv[2]);
      else if(std::string(argv[1])=="settingsactionchecks")ok=dashboard.CheckSettingsActions(argv[2]);
      else if(std::string(argv[1])=="startviewchecks")ok=dashboard.CheckStartView(argv[2]);
      else if(std::string(argv[1])=="bookshelfchecks")ok=dashboard.CheckBookshelf(argv[2]);
      else if(std::string(argv[1])=="presentationchecks")ok=dashboard.CheckPresentations(argv[2]);
      else if(std::string(argv[1])=="layoutchecks")ok=dashboard.CheckCoverLayouts(argv[2]);
      else if(std::string(argv[1])=="pagingchecks")ok=dashboard.CheckPaging();
      else
      // Warm the backdrop first, then capture the reuse path as well as live UI.
      ok = std::string(argv[1]) == "injectionchecks" ? dashboard.CheckInjectionDialog(argv[2]) : std::string(argv[1]) == "launchoptionchecks" ? dashboard.CheckLaunchOptions(argv[2]) : std::string(argv[1]) == "artworkchecks" ? dashboard.CheckArtworkPolish(argv[2]) : std::string(argv[1]) == "casechecks" ? dashboard.CheckCaseRendering(argv[2]) : std::string(argv[1]) == "mediadefaultchecks" ? dashboard.CheckMediaDefaults(argv[2]) : std::string(argv[1]) == "mediagroupchecks" ? dashboard.CheckMediaAccordion(argv[2]) : std::string(argv[1]) == "videochecks" ? dashboard.CheckVideoSettings(argv[2]) : std::string(argv[1]) == "hintchecks" ? dashboard.CheckControlHints(argv[2]) : std::string(argv[1]) == "japanesechecks" ? dashboard.CheckJapaneseText(argv[2]) : std::string(argv[1]) == "fadechecks" ? dashboard.CheckLibraryFade() : std::string(argv[1]) == "detailschecks" ? dashboard.CheckDetails() : std::string(argv[1]) == "launcherchecks" ? dashboard.CheckLauncherSettings() : std::string(argv[1]) == "checks" ? dashboard.CheckScrolling() :
          dashboard.VisualPreview(argv[1]) && dashboard.Render(0) && dashboard.Render(0, argv[2]); }
    ShutdownSmoothFonts(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return ok ? 0 : 5;
}
#endif
