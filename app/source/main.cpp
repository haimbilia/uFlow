#include "library.hpp"
#include "browser_path.hpp"
#ifndef UFLOW_HOST_PREVIEW
#include "usb_storage.hpp"
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
#include "case_model.hpp"
#include "case_sleeve.hpp"
#include "case_profiles.hpp"
#include "cover_faces.hpp"
#include "cover_projection.hpp"
#include "glass_ttf.h"
#include "visual_check.hpp"
#include "glass_material.hpp"
#include "cover_transition.hpp"
#include "artwork_worker.hpp"
#include "browser_worker.hpp"
#include "scan_worker.hpp"
#include "music.hpp"
#include "video.hpp"
#include "glass_background.hpp"
#include "media_groups.hpp"
#include "library_tabs.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
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
#include <unordered_map>
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

#ifndef UFLOW_HOST_PREVIEW
using LaunchLooseFn = RPXLoaderStatus (*)(const char *, const char *, const char *, const char *,
                                          const char *, const char *, const char *);
#endif

struct Color { Uint8 r, g, b, a; };
struct TextureInfo { SDL_Texture *texture = nullptr; int width = 0; int height = 0; };
struct CachedTexture { TextureInfo info; Uint32 lastUse = 0; std::size_t bytes = 0; };

struct SmoothTextEntry { SDL_Texture *texture = nullptr; int width = 0; int height = 0; Uint32 lastUse = 0; };
std::array<TTF_Font *, 6> gSmoothFonts{};
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
SDL_Texture *gEllipseShadow = nullptr;
SDL_Texture *gStockMenuSymbols = nullptr;

bool InitSmoothFonts() {
    if (TTF_Init() != 0) return false;
    constexpr int sizes[] = {0, 17, 20, 25, 33, 42};
    const int fontSize = static_cast<int>(glass_ttf_size);
    for (int index = 1; index <= 5; ++index) {
        SDL_RWops *stream = SDL_RWFromConstMem(glass_ttf, fontSize);
        gSmoothFonts[index] = stream ? TTF_OpenFontRW(stream, 1, sizes[index]) : nullptr;
        if (gSmoothFonts[index] && index <= 2) TTF_SetFontStyle(gSmoothFonts[index], TTF_STYLE_BOLD);
    }
    return gSmoothFonts[1] != nullptr;
}

void ShutdownSmoothFonts() {
    gGlassMaterial.Clear();
    if (gStockMenuSymbols) SDL_DestroyTexture(gStockMenuSymbols);
    if (gEllipseShadow) SDL_DestroyTexture(gEllipseShadow);
    gStockMenuSymbols = gEllipseShadow = nullptr;
    for (auto &[unused, entry] : gSmoothTextCache) if (entry.texture) SDL_DestroyTexture(entry.texture);
    gSmoothTextCache.clear();
    for (TTF_Font *&font : gSmoothFonts) { if (font) TTF_CloseFont(font); font = nullptr; }
    TTF_Quit();
}

void Trace(const char *event) {
#ifndef UFLOW_HOST_PREVIEW
    OSReport("uFlow: %s\n", event);
    FILE *file = std::fopen(kTracePath, "ab");
    if (!file) return;
    std::fprintf(file, "ticks=%u event=%s\n", SDL_GetTicks(), event);
    std::fclose(file);
#else
    (void)event;
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
    if (theme == 1) return {0, 208, 230, 255};
    if (theme == 2) return {168, 112, 255, 255};
    if (theme == 3) return {255, 166, 58, 255};
    switch (platform) {
        case Platform::WiiU: return {0, 190, 236, 255};
        case Platform::Wii: return {80, 197, 236, 255};
        case Platform::GameCube: return {132, 102, 238, 255};
        case Platform::Files: return {45,174,222,255};
    }
    return {0, 190, 236, 255};
}

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
};

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
    for(const auto &library:libraries) if(library.id==game.libraryId)
        return {static_cast<float>(library.caseWidthMm),static_cast<float>(library.caseHeightMm),static_cast<float>(library.caseThicknessMm)};
    const auto size=CaseProfiles::For(game.libraryId,{},ExtensionLower(game.absolutePath));
    return {static_cast<float>(size.width),static_cast<float>(size.height),static_cast<float>(size.thickness)};
}

void DrawGlassSurface(SDL_Renderer *renderer, SDL_Rect rect, int radius, Color accent,
                      Uint8 opacity = 150, bool elevated = true) {
    (void)accent;
    if (elevated) gGlassMaterial.DrawShadow(renderer, rect);
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
                v.position = {px,py}; v.color = {255,255,255,rect.h>200?Uint8(210):Uint8(150)};
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
    gGlassMaterial.Draw(renderer, rect, false,static_cast<Uint8>(std::min(255,160+opacity)));
}

void DrawDetailGlassButton(SDL_Renderer *renderer, SDL_Rect rect, bool focused) {
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
    gGlassMaterial.DrawButton(renderer,rect,focused);
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
        case '\'': return {4,4,2,0,0,0,0}; case '"': return {10,10,5,0,0,0,0};
        default: return {0,0,0,0,0,0,0};
    }
}

int TextWidth(const std::string &text, int scale) {
    if (gSmoothText && scale >= 1 && scale <= 5 && gSmoothFonts[scale]) {
        int width = 0;
        TTF_SizeUTF8(gSmoothFonts[scale], text.c_str(), &width, nullptr);
        return width;
    }
    return text.empty() ? 0 : static_cast<int>(text.size()) * 6 * scale - scale;
}

std::string Ellipsize(std::string text, std::size_t maximum) {
    for (char &c : text) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = ' ';
    if (text.size() <= maximum) return text;
    if (maximum <= 3) return text.substr(0, maximum);
    return text.substr(0, maximum - 3) + "...";
}

void DrawText(SDL_Renderer *renderer, const std::string &text, int x, int y, int scale, Color color,
              bool centered = false) {
    if(!gPopupTextGlow && gGlassOverVideo && color.r<160 && color.g<175 && color.b<190)
        color={241,248,253,color.a};
    if (gSmoothText && scale >= 1 && scale <= 5 && gSmoothFonts[scale]) {
        const std::string key = std::to_string(scale) + ":" + std::to_string(color.r) + ":" +
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
            SDL_Surface *surface = TTF_RenderUTF8_Blended(gSmoothFonts[scale], text.c_str(),
                                                          SDL_Color{255,255,255,255});
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
            if (centered) x -= found->second.width / 2;
            SDL_Rect destination = {x, y, found->second.width, found->second.height};
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
int ArtworkDivisor(int width,int height) {
    // Bound source work separately from texture memory. Decode large scans to
    // a smaller surface; never allocate a full-size RGBA copy of them.
    if(width<=0 || height<=0 || width>8192 || height>8192 ||
       static_cast<std::uint64_t>(width)*height>16*1024*1024) return 0;
    int divisor=1;
    while(!ValidArtworkSize((width+divisor-1)/divisor,(height+divisor-1)/divisor)) divisor*=2;
    return divisor;
}

SDL_Surface *LoadPng(const std::string &path) {
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
    const int divisor=ArtworkDivisor(width,height);
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

SDL_Surface *LoadJpeg(const std::string &path) {
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
    const int divisor=ArtworkDivisor(width,height);
    if (!divisor) {
        tjDestroy(decoder); return nullptr;
    }
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

class TextureCache {
public:
#ifdef UFLOW_HOST_PREVIEW
    static constexpr bool DefaultAsync = false;
#else
    static constexpr bool DefaultAsync = true;
#endif
    explicit TextureCache(SDL_Renderer *renderer, bool asynchronous = DefaultAsync) : renderer_(renderer) {
        if (asynchronous) {
        worker_ = std::make_unique<ArtworkWorker>(Decode);
        if (!worker_->Available()) SDL_Log("Artwork worker unavailable; retaining placeholders: %s", SDL_GetError());
        }
    }
    ~TextureCache() { worker_.reset(); Clear(); }
    void BeginFrame() { pending_.clear(); }
    std::size_t Count() const { return textures_.size(); }
    std::size_t Bytes() const { return bytes_; }
    std::size_t Pending() const { return pending_.size(); }
    bool Known(const std::string &path) const { return path.empty() || textures_.contains(path); }
    bool HasTexture(const std::string &path) const {
        const auto found = textures_.find(path);
        return found != textures_.end() && found->second.info.texture;
    }
    TextureInfo Get(const std::string &path) {
        if (path.empty()) return {};
        if (const auto found = textures_.find(path); found != textures_.end()) {
            found->second.lastUse = SDL_GetTicks();
            return found->second.info;
        }
        if (!worker_) return Load(path);
        Queue(path);
        return {};
    }
    void Prefetch(const std::string &path) {
        if (path.empty() || textures_.contains(path)) return;
        if (!worker_) Load(path); else Queue(path);
    }
    void PumpOne() {
        if (!worker_) return;
        worker_->Request(pending_);
        auto ready = worker_->Take();
        if (ready.available) {
            if (!textures_.contains(ready.path)) Upload(ready.path, ready.surface);
            else if (ready.surface) SDL_FreeSurface(ready.surface);
        }
    }
    void Clear() {
        if (worker_) worker_->Reset();
        for (auto &[unused, cached] : textures_) if (cached.info.texture) SDL_DestroyTexture(cached.info.texture);
        textures_.clear();
        pending_.clear();
        bytes_ = 0;
    }
private:
    static constexpr std::size_t kMaxEntries = 32;
    static constexpr std::size_t kMaxBytes = 48 * 1024 * 1024;

    void Queue(const std::string &path) {
        if (std::find(pending_.begin(), pending_.end(), path) != pending_.end()) return;
        // A bounded queue keeps a rapid scroll from retaining obsolete covers.
        if (pending_.size() >= 24) return;
        pending_.push_back(path);
    }
    void RemoveOldest() {
        const auto oldest = std::min_element(textures_.begin(), textures_.end(), [](const auto &left, const auto &right) {
            return left.second.lastUse < right.second.lastUse;
        });
        if (oldest == textures_.end()) return;
        if (oldest->second.info.texture) SDL_DestroyTexture(oldest->second.info.texture);
        bytes_ -= oldest->second.bytes;
        textures_.erase(oldest);
    }
    void MakeRoom(std::size_t needed) {
        while (!textures_.empty() && (textures_.size() >= kMaxEntries || bytes_ + needed > kMaxBytes)) RemoveOldest();
    }
    static SDL_Surface *Decode(const std::string &path) {
        SDL_Surface *surface = nullptr;
        const auto dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension == "png") surface = LoadPng(path);
        else if (extension == "tga") surface = LoadTga(path);
        else if (extension == "jpg" || extension == "jpeg") surface = LoadJpeg(path);
        return surface;
    }
    TextureInfo Load(const std::string &path) { return Upload(path, Decode(path)); }
    TextureInfo Upload(const std::string &path, SDL_Surface *surface) {
        TextureInfo result;
        std::size_t bytes = 0;
        if (surface) {
            result.width = surface->w; result.height = surface->h;
            bytes = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 4;
            MakeRoom(bytes);
            result.texture = bytes <= kMaxBytes ? SDL_CreateTextureFromSurface(renderer_, surface) : nullptr;
            if (result.texture) {
                SDL_SetTextureBlendMode(result.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(result.texture, SDL_ScaleModeLinear);
                bytes_ += bytes;
            } else {
                bytes = 0;
            }
            SDL_FreeSurface(surface);
        }
        MakeRoom(0);
        textures_[path] = {result, SDL_GetTicks(), bytes};
        return result;
    }
    SDL_Renderer *renderer_;
    std::unique_ptr<ArtworkWorker> worker_;
    std::unordered_map<std::string, CachedTexture> textures_;
    std::vector<std::string> pending_;
    std::size_t bytes_ = 0;
};

class Dashboard {
public:
    Dashboard(SDL_Renderer *renderer, std::vector<GameEntry> &games)
        : renderer_(renderer), games_(games), textures_(renderer), preferences_(LoadSettings()),
          sources_(LoadContentSources()), mediaSources_(LoadMediaSources()) {
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
        libraryTabs_=AvailableLibraryTabs(games_,libraries_,preferences_);
        welcomeOpen_=!preferences_.welcomeDismissed;
#ifdef UFLOW_HOST_PREVIEW
        welcomeOpen_=false; // normal visual fixtures stay independent of first-run state
#endif
        tab_ = preferences_.startTab;
        const int initialTabCount = static_cast<int>(LibraryTabs().size());
        tab_ = std::clamp(tab_, 0, std::max(0, initialTabCount - 1));
        visualTab_ = static_cast<float>(tab_);
        RebuildVisible();
#ifndef UFLOW_HOST_PREVIEW
        musicTracks_=ListMusicTracks();
        ApplyMusic();
#endif
    }
    ~Dashboard() {
        scanWorker_.Stop();
        browserWorker_.reset(); // join directory I/O before storage teardown
        video_.Stop();
        glassBackground_.Stop();
        FlushSettingsSave(true);
        music_.Stop();
        ClearSideCases();
        for (auto *texture : caseTextures_) if (texture) SDL_DestroyTexture(texture);
        if (lensTexture_) SDL_DestroyTexture(lensTexture_);
        if (blurScratch_) SDL_DestroyTexture(blurScratch_);
        if (blurTexture_) SDL_DestroyTexture(blurTexture_);
        if (sceneTexture_) SDL_DestroyTexture(sceneTexture_);
    }

    void ReplaceGames(std::vector<GameEntry> games) {
        StopVideo(); videoSelection_ = nullptr; videoTrigger_.Focus("");
        ClearSideCases();
        detailsBackdropReady_ = false;
        coverInspectOpen_ = coverInspectBack_ = false;
        coverInspectProgress_ = coverInspectBackProgress_ = 0.f;
        textures_.Clear(); games_ = std::move(games); selected_ = 0; visualSelected_ = 0.0f; RebuildVisible();
    }
    // Reversible fixtures: never scan, launch, or save settings while previewing.
    bool VisualPreview(const std::string &screen) {
        welcomeOpen_=false;
        StopVideo(); videoSelection_ = nullptr; videoTrigger_.Focus("");
        ClearSideCases();
        detailsBackdropReady_ = false;
        if (screen == "live") {
            if (preview_) { games_ = std::move(liveGames_); preferences_ = livePreferences_; sources_ = std::move(liveSources_); mediaSources_=std::move(liveMediaSources_); }
            preview_ = false;
        } else {
            const bool vcProfile=screen.rfind("launcher",0)==0&&VCSystems::Find(screen.substr(8));
            if (screen != "coverfront" && screen != "coverback" && screen != "detailsinject" && screen != "detailsinjected" && screen != "detailslauncher" && screen != "library" && screen != "libraryfadehalf" && screen != "libraryfadezero" && screen != "navtransition" && screen != "details" && screen != "settings" && screen != "directories" &&
                !vcProfile && screen != "launchers" && screen != "launcheredit" && screen != "launchervc" && screen != "missing" && screen != "sources" && screen != "audio" && screen != "media" && screen != "mediaedit" && screen != "sourceedit" && screen != "mediaguide" && screen != "details25" && screen != "details50" && screen != "details75" && screen != "detailssource" && screen != "detailsmedia" && screen != "detailsinfo" && screen != "detailspixel") return false;
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
                // Stable artwork fixture installed alongside the development key, never a game scan.
#ifdef UFLOW_HOST_PREVIEW
                const std::string root = "/fixture/";
#else
                const std::string root = "fs:/vol/external01/wiiu/apps/uFlow/visual-fixture/";
#endif
                if (screen != "missing" && index != 5) game.coverPath = root + "cover.png";
                if (screen != "missing" && index != 5) game.fullCoverPath = root + "fullcover.png";
                game.backgroundPath = root + "background.png";
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
        detailsRow_=screen=="detailssource"?3:screen=="detailsmedia"?4:screen=="detailsinfo"?5:1; detailsPage_=0;
        detailsLauncherEditing_=false;
        detailsPanelFocus_=false;detailsPanelRow_=0;
        if(screen=="detailslauncher")detailsRow_=2;
        if(screen=="detailspixel") preferences_.skin=0;
        directoryBrowser_ = screen == "directories"; sourceManager_ = sourceEditor_ = directoryBrowser_;
        sourceManager_ = sourceManager_ || screen == "sources" || screen=="sourceedit" || mediaMode_;
        settingsCategory_ = mediaMode_ ? 2 : sourceManager_ ? 1 : 0; settingsRow_ = 0; sourceRow_ = 0;
        settingsSidebar_=screen=="settings";
        settingsEditing_=false;
        defaultFileBrowser_=false;
        launcherEditor_=launcherBrowse_=false;vcMapMode_=0;typeManager_=typeText_=false;
        if(screen.rfind("launcher",0)==0){settings_=true;settingsCategory_=5;launcherRow_=0;
            if(screen!="launchers"){launcherEditor_=true;launcherDraft_=launchers_[screen=="launchervc"?8:3];launcherRow_=5;}}
        for(const auto &system:VCSystems::all)if(screen==std::string("launcher")+system.id){
            launcherEditor_=true;launcherDraft_={std::string(system.id)+"-vc",std::string(system.name)+" - VC injector",LauncherKind::DSInjection,system.name,system.extensions,"",true,std::string(system.id)+"-vc"};
            injectionDraft_={};injectionDraft_.system=system.id;injectionDraft_.base=std::string("wiiu/vc-bases/")+system.id+"/default";launcherRow_=10;
        }
        audioSettings_=screen=="audio";
        if(mediaMode_) expandedMediaGroups_.clear();
        if(screen=="mediaedit") FocusMediaSource(0);
        if (screen=="sourceedit" || screen=="mediaedit") { if(!mediaMode_) sourceRow_=1; SourceActivate(); }
        browserPath_ = "fs:/vol/external01"; browserDirectories_ = {"install", "wiiu", "wbfs", "games"}; browserRow_ = 1;
        detailsProgress_ = details_ ? (screen == "details25" ? .25f : screen == "details50" ? .5f : screen == "details75" ? .75f : 1.f) : 0;
        settingsProgress_ = settings_ ? 1.f : 0;
        coverInspectOpen_ = screen == "coverfront" || screen == "coverback";
        coverInspectBack_ = screen == "coverback";
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
        return true;
    }
    bool PreviewActive() const { return preview_; }
    std::string PerformanceStats() {
        // On-demand CPU allocator statistics, NOT total RAM or free GPU memory.
        // No disk writes/readback; fixed counters cannot grow during idle.
        const auto heap = mallinfo();
        char result[512];
        std::snprintf(result,sizeof(result),
            "{\"uptime_ms\":%u,\"malloc_used_bytes\":%u,\"malloc_free_bytes\":%u,"
            "\"side_cases\":%zu,\"artwork_entries\":%zu,\"artwork_bytes\":%zu,\"artwork_pending\":%zu,\"text_entries\":%zu,"
            "\"frames\":%u,\"render_mean_ms\":%.3f,\"render_max_ms\":%.3f}",
            SDL_GetTicks(),static_cast<unsigned>(heap.uordblks),static_cast<unsigned>(heap.fordblks),
            sideCases_.size(),textures_.Count(),textures_.Bytes(),textures_.Pending(),gSmoothTextCache.size(),performanceFrames_,
            performanceFrames_ ? performanceTotalMs_/performanceFrames_ : 0.,performanceMaxMs_);
        performanceFrames_=0; performanceTotalMs_=0; performanceMaxMs_=0;
        return result;
    }
#ifdef UFLOW_HOST_PREVIEW
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
        cache.Clear();
        if (cache.Count() || cache.Bytes()) return false;
        SDL_Log("Async texture cache PASS: actual PNG decode, main-thread upload, cached hits, reset");
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
            SDL_Surface *decoded=LoadPng("artwork-check.png"); if(!decoded) return false;
            const int divisor=ArtworkDivisor(w,h);
            bool equal=decoded->w==(w+divisor-1)/divisor && decoded->h==(h+divisor-1)/divisor;
            for(int y=0;y<decoded->h && equal;++y) for(int x=0;x<decoded->w;++x) {
                const int sx=std::min(w-1,x*divisor+divisor/2),sy=std::min(h-1,y*divisor+divisor/2);
                if(std::memcmp(static_cast<unsigned char *>(decoded->pixels)+y*decoded->pitch+x*4,rows[sy]+sx*4,4)) { equal=false;break; }
            }
            SDL_FreeSurface(decoded); if(!equal) return false;
        }
        const std::string base="/project/artifacts/media-polish-20260913/";
        for(const auto &name:{"ds-cover.original.jpg","ds-default.original.png"}) {
            auto *surface=name==std::string("ds-cover.original.jpg")?LoadJpeg(base+name):LoadPng(base+name);
            if(!surface) return false;
            const bool reduced=surface->w==1630 && surface->h==685;
            SDL_FreeSurface(surface); if(!reduced) return false;
        }
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
        for(int skin:{1,0}) {
            VisualPreview("media"); preferences_.skin=skin; settingsSidebar_=false;
            libraries_={{"wiiu","Wii U"},{"wii","Wii"},{"gc","GameCube"},{"ds","Nintendo DS"}};
            mediaSources_={{MediaType::FullCover,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/fullcovers",true,0},
                {MediaType::Preview,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/Wii/Videos",true,0,"wii"},
                {MediaType::Cover,0,"fs:/vol/external01/wiiu/apps/uFlow/media/WiiU/Covers",true,0},
                {MediaType::Logo,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/Wii/Logos",false,0,"wii"},
                {MediaType::Cover,-1,"fs:/vol/external01/wiiu/apps/uFlow/media/DS/Covers",true,0,"ds"}};
            auto rows=MediaRows();
            if(rows.size()!=8 || rows[2].key!="wii" || rows[2].directories!=2) return false;
            const auto suffix=skin==1?"-glass":"-pixel";
            if(!Render(0,output+suffix+"-collapsed.png")) return false;
            sourceRow_=2; SettingsInput(0,0,true);
            if(sourceEditor_ || MediaRows().size()!=10 || sourceRow_!=2) return false;
            SourceMove(1); SettingsInput(0,0,true);
            if(!sourceEditor_ || editingSourceIndex_!=1 || draftMediaSource_.type!=MediaType::Preview) return false;
            CloseOverlay();
            SourceMove(1); SourceActivate();
            if(editingSourceIndex_!=3 || draftMediaSource_.enabled) return false;
            CloseOverlay(); sourceRow_=2;
            if(!Render(0,output+suffix+"-expanded.png")) return false;
            // Touch the header to collapse, then re-expand and touch its child.
            SettingsTouch(500,278+2*48);
            if(MediaRows().size()!=8 || sourceEditor_) return false;
            SettingsTouch(500,278+2*48); SettingsTouch(500,278+3*48);
            if(!sourceEditor_ || editingSourceIndex_!=1) return false;
            CloseOverlay(); sourceRow_=2; SourceAdd();
            if(!sourceEditor_ || editingSourceIndex_!=-1 || draftMediaSource_.libraryId!="wii") return false;
            CloseOverlay();
            // Simulate saved reassignment/add/remove; selection follows the saved source.
            mediaSources_[1].libraryId="ds"; FocusMediaSource(1);
            if(MediaRows()[sourceRow_].source!=1 || MediaRows()[sourceRow_].key!="ds") return false;
            for(int i=0;i<9;++i) mediaSources_.push_back({MediaType::Background,-1,"fs:/vol/external01/media/Wii/extra-"+std::to_string(i),true,0,"wii"});
            FocusMediaSource(static_cast<int>(mediaSources_.size())-1);
            const int expected=MediaRows()[sourceRow_].source;
            const int first=std::max(0,std::min(sourceRow_-4,SourceListCount()-6));
            if(first<=0) return false;
            if(!Render(0,output+suffix+"-scrolled.png")) return false;
            SettingsTouch(500,278+(sourceRow_-first)*48);
            if(!sourceEditor_ || editingSourceIndex_!=expected) return false;
            CloseOverlay();
            sourceRow_=0; SourceMove(-1);
            if(MediaRows()[sourceRow_].kind!=MediaDirectoryRow::Help) return false;
            SourceActivate(); if(!mediaHelp_) return false; CloseOverlay();
            sourceRow_=SourceListCount()-3; scanRequested_=false; SourceActivate();
            if(!scanRequested_) return false; scanRequested_=false;
            sourceRow_=SourceListCount()-2; SourceActivate();
            if(sourceManager_) return false;
            sourceManager_=true; mediaSources_.clear(); sourceRow_=0;
            if(SourceListCount()!=4 || !Render(0,output+suffix+"-empty.png")) return false;
            SourceActivate(); if(!sourceEditor_ || editingSourceIndex_!=-1) return false;
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
        mediaSources_.push_back({MediaType::Preview,-1,"/project/app/content",true,3,"",{0,false,0}});
        videoSelection_=nullptr;
        if(!spin([&]{return video_.Ready();})||glassBackground_.Active()||!glassBackground_.Released())return false;
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
        for (const int skin : {1,0}) {
            VisualPreview("mediaedit"); preferences_.skin=skin;
            sourceEditorRow_=1;
            if(SourceEditorCount()!=8) return false;
            SourceEditorAdjust(4);
            if(!VideoSourceEditing() || SourceEditorCount()!=11 || draftMediaSource_.video.delayMs!=1000) return false;
            settingsSidebar_=false;
            sourceEditorRow_=5; SettingsInput(0,0,true); SettingsInput(1,0);
            if(draftMediaSource_.video.delayMs!=1250 || !settingsEditing_) return false;
            SettingsInput(-1,0); SettingsInput(0,0,false,true);
            sourceEditorRow_=6; SettingsInput(0,0,true); SettingsInput(1,0);
            if(draftMediaSource_.video.foreground) return false;
            SettingsInput(0,0,false,true);
            const std::string suffix=skin==1?"-glass":"-pixel";
            if(SourceEditorCount()!=12) return false;
            if(!Render(0,output+suffix+"-settings.png")) return false;
            SourceEditorMove(1); // volume scrolls the viewport by one row
            if(SourceEditorFirst()!=1) return false;
            const int volume=draftMediaSource_.video.volume;
            SettingsTouch(1040,278+6*38);
            if(sourceEditorRow_!=7 || draftMediaSource_.video.volume!=volume+10) return false;
            SourceEditorMove(1);
            SettingsInput(0,0,true); SettingsInput(-1,0);
            if(draftMediaSource_.video.foregroundOpacity!=95 || !settingsEditing_) return false;
            SettingsInput(0,0,false,true);
            SettingsTouch(900,278+6*38);
            if(draftMediaSource_.video.foregroundOpacity!=90) return false;
            if(!Render(0,output+suffix+"-opacity-setting.png")) return false;
            sourceEditorRow_=6; SourceEditorAdjust(1);
            if(SourceEditorCount()!=11 || draftMediaSource_.video.foregroundOpacity!=90) return false;
            SourceEditorAdjust(1);
            if(SourceEditorCount()!=12 || draftMediaSource_.video.foregroundOpacity!=90) return false;
            sourceEditorRow_=11;
            if(SourceEditorFirst()!=5) return false;
            if(!Render(0,output+suffix+"-settings-bottom.png")) return false;
            sourceEditorRow_=1; SourceEditorAdjust(1);
            if(VideoSourceEditing() || SourceEditorCount()!=8) return false;
        }
        // Render the same actual video path in both layers, then exercise leaving it.
        VisualPreview("library"); videoTest_=true;
        games_[visible_[selected_]].previewPath="/fixture/Disney-Bolt.mp4";
        mediaSources_.push_back({MediaType::Preview,-1,"/fixture",true,0,"",{0,true,0}});
        auto ready = [&]() {
            const Uint32 start=SDL_GetTicks();
            do { Render(0); SDL_Delay(5); } while(!video_.Ready() && SDL_GetTicks()-start<6000);
            return video_.Ready();
        };
        if(!ready() || !Render(0,output+"-foreground.png")) return false;
        OpenCoverInspect(false); coverInspectProgress_=1; Render(0);
        if(video_.Active()) return false;
        CloseCoverInspect(); coverInspectProgress_=0;
        mediaSources_.back().video.foreground=false; videoSelection_=nullptr;
        if(!ready() || !Render(0,output+"-background.png")) return false;
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
                if(!Render(0,output+(skin==1?"-glass":"-pixel")+"-opacity-"+std::to_string(opacity)+".png")) return false;
                samples[sample++]=readScene();
                if(samples[sample-1].empty()) return false;
            }
            int changed=0;
            for(std::size_t p=0;p<samples[0].size();p+=68) for(int channel=0;channel<3;++channel) {
                const int base=samples[0][p+channel],half=samples[1][p+channel],full=samples[2][p+channel];
                if(std::abs(full-base)>10) ++changed;
                if(std::abs(half-(full*128+base*127)/255)>3) return false;
            }
            if(changed<1000) return false;
            // At zero UI opacity, the composed scene must be exactly video-only.
            videoOptions_.foregroundOpacity=0; Render(0);
            auto zero=readScene();
            SDL_SetRenderTarget(renderer_,lensTexture_);
            std::vector<unsigned char> baseline(kWidth*kHeight*4);
            const int result=SDL_RenderReadPixels(renderer_,nullptr,SDL_PIXELFORMAT_RGBA32,baseline.data(),kWidth*4);
            SDL_SetRenderTarget(renderer_,nullptr);
            if(result || zero!=baseline) return false;
        }
        preferences_.skin=1;videoOptions_.foregroundOpacity=100;video_.Suspend(false);
        SDL_Log("Background opacity PASS: conditional settings/controller/touch, 0/50/100 percent exact video/UI composition, both skins");
        // Background video continues underneath inspection; foreground video stops.
        OpenCoverInspect(false); coverInspectProgress_=1;
        const auto inspectionFrames=video_.Statistics().decoded;
        Render(0,output+"-inspection.png");
        if(!video_.Active() || !video_.Ready() || video_.Statistics().decoded<inspectionFrames) return false;
        CloseCoverInspect(); coverInspectProgress_=0;
        ToggleSettings(); Render(0);
        if(video_.Active()) return false;
        settings_=false; settingsProgress_=0;
        if(!ready()) return false;
        ToggleDetails(); Render(0);
        if(video_.Active()) return false;
        ToggleDetails(); detailsProgress_=0;
        if(!ready()) return false;
        injectionDialog_.Ask("Video suppression test"); Render(0);
        if(video_.Active()) return false;
        injectionDialog_.Reset();
        if(!ready()) return false;
        VideoInput(true); Render(0);
        if(video_.Active()) return false;
        VideoInput(false);
        if(!ready()) return false;
        PrepareSystemBackground();
        if(video_.Active() || video_.Ready()) return false;
        const Uint32 backgroundStop=SDL_GetTicks();
        while(!video_.CanStart() && SDL_GetTicks()-backgroundStop<2000) SDL_Delay(2);
        if(!video_.CanStart()) return false; // Cancellation unwinds, never pauses cleanup.
        ResumeSystemForeground();
        if(!ready()) return false;
        DismissVideo(); Render(0);
        if(video_.Active()) return false;
        Move(1); Render(0); if(video_.Active()) return false;
        videoTest_=false;
        SDL_Log("Video UI PASS: dynamic rows, delay/layer/volume editing, touch after scrolling, both skins, foreground/background rendering, settings/details/injection/input suppression, system-background stop/resume, B dismiss, navigation stop");
        return true;
    }
    bool CheckSettingsControls() {
        VisualPreview("settings");
        SettingsInput(0,1); if(settingsCategory_!=1 || !settingsSidebar_) return false;
        SettingsInput(1,0,true); if(settingsSidebar_ || sourceEditor_) return false;
        SettingsInput(0,0,true); if(!sourceEditor_) return false;
        SettingsInput(0,1); const auto platform=draftSource_.platform;
        SettingsInput(-1,0); SettingsInput(1,0);
        if(draftSource_.platform!=platform || !sourceEditor_) return false;
        SettingsInput(0,0,true); if(!settingsEditing_ || draftSource_.platform!=platform) return false;
        SettingsInput(1,0); if(draftSource_.platform==platform || settingsSidebar_) return false;
        SettingsInput(-1,0); if(draftSource_.platform!=platform || settingsSidebar_) return false;
        SettingsInput(0,0,false,true); if(settingsEditing_ || !sourceEditor_) return false;
        SettingsInput(0,0,true); SettingsInput(0,1);
        if(settingsEditing_ || sourceEditorRow_!=2 || settingsSidebar_) return false;
        const int depth=draftSource_.depth;
        SettingsInput(0,0,true); SettingsInput(-1,0);
        if(draftSource_.depth!=std::max(0,depth-1) || settingsSidebar_) return false;
        SettingsInput(0,-1); if(settingsEditing_ || sourceEditorRow_!=1) return false;
        draftSource_.platform=Platform::Wii;
        sourceEditorRow_=4;
        SettingsInput(0,0,true); if(!settingsEditing_) return false;
        SettingsInput(1,0); if(draftSource_.launcher!=GameLauncher::USBLoaderGX) return false;
        SettingsInput(-1,0); if(draftSource_.launcher!=GameLauncher::WiiFlow) return false;
        SettingsInput(0,0,false,true);
        SettingsInput(0,0,false,true); if(sourceEditor_ || !sourceManager_) return false;
        // Touch a section, open its path, change a value, then cancel.
        SettingsTouch(200,330); if(settingsCategory_!=2 || !settingsSidebar_) return false;
        SettingsTouch(500,290); if(!sourceEditor_ || settingsSidebar_) return false;
        const auto mediaType=draftMediaSource_.type;
        SettingsTouch(1030,320); if(draftMediaSource_.type==mediaType) return false;
        SettingsTouch(900,320); if(draftMediaSource_.type!=mediaType) return false;
        SettingsTouch(1040,600); if(sourceEditor_) return false;
        SettingsTouch(500,290,-70); if(sourceRow_!=1 || sourceEditor_) return false;
        SettingsTouch(200,210); if(settingsCategory_!=0) return false;
        SettingsInput(1,0);
        for(int i=0;i<4;++i) SettingsInput(0,1);
        SettingsInput(0,0,true); if(!audioSettings_ || settingsCategory_!=0) return false;
        // Empty music library must not leave the switch falsely ON.
        musicTracks_.clear(); preferences_.musicTrack.clear(); preferences_.musicEnabled=false;
        SettingsInput(0,0,true); if(!settingsEditing_ || preferences_.musicEnabled) return false;
        SettingsInput(1,0); if(preferences_.musicEnabled || musicStatus_.empty()) return false;
        musicTracks_={"fixture.wav"};
        SettingsInput(1,0); if(!preferences_.musicEnabled || musicStatus_.rfind("PLAYING:",0)!=0) return false;
        SettingsInput(-1,0); if(preferences_.musicEnabled || musicStatus_!="MUSIC OFF" || settingsSidebar_) return false;
        SettingsInput(0,1); if(settingsEditing_ || settingsRow_!=1) return false;
        musicTracks_={"fixture.wav","second.ogg","third.mp3"};
        preferences_.musicTrack=musicTracks_[0];
        SettingsInput(0,0,true); SettingsInput(-1,0);
        if(preferences_.musicTrack!=musicTracks_[2] || settingsSidebar_) return false;
        SettingsInput(1,0); if(preferences_.musicTrack!=musicTracks_[0]) return false;
        SettingsInput(0,1);
        const auto volume=preferences_.musicVolume;
        SettingsInput(0,0,true); if(preferences_.musicVolume!=volume || !settingsEditing_) return false;
        SettingsInput(1,0); if(preferences_.musicVolume!=volume+5) return false;
        SettingsInput(-1,0); if(preferences_.musicVolume!=volume || settingsSidebar_) return false;
        SettingsInput(0,0,false,true); if(settingsEditing_ || !audioSettings_ || settingsRow_!=2) return false;
        SettingsInput(-1,0); if(!settingsSidebar_) return false;
        SettingsInput(1,0); if(settingsSidebar_ || settingsEditing_) return false;
        SettingsInput(0,0,false,true); if(!settings_ || audioSettings_ || settingsRow_!=4) return false;
        SettingsInput(0,1); if(settingsRow_!=5) return false;
        SettingsInput(0,0,true); if(!settingsEditing_) return false;
        SettingsInput(1,0); if(!preferences_.startOnBoot) return false;
        SettingsInput(-1,0); if(preferences_.startOnBoot) return false;
        SettingsInput(0,0,false,true);
        SettingsTouch(1020,515); if(!preferences_.startOnBoot) return false;
        SettingsTouch(900,515); if(preferences_.startOnBoot) return false;
        SettingsInput(0,0,false,true); if(settings_) return false;
        SDL_Log("Settings controls PASS: A edit, left/right values, B/up/down end edit, sidebar focus, path type/depth, touch arrows, General/Audio, ON/OFF, track wrap, volume");
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
                footerHints_[4].button != "+" || favoriteHint_ != "MARK A GAME WITH X") return false;
            bind(ControlAction::Launch, 0x1000);
            bind(ControlAction::Details, 0x8000);
            if (!Render(0, output + suffix + ".png")) return false;
            if (footerHints_[0].button != "Y" || footerHints_[1].button != "A" ||
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
            if (sideCases_.size() > 16) return false;
        }
        for (int frame=0;frame<300;++frame) Render(1.f/60.f); // settle and warm
        const auto casesBefore=sideCases_.size(), artBefore=textures_.Count(), textBefore=gSmoothTextCache.size();
        SDL_Log("Idle baseline: %s",PerformanceStats().c_str());
        for (int frame=0;frame<3600;++frame) Render(1.f/60.f);
        SDL_Log("Idle after 3600 frames: %s",PerformanceStats().c_str());
        if (casesBefore != sideCases_.size() || artBefore != textures_.Count() || textBefore != gSmoothTextCache.size()) return false;
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
        const auto id=pendingInjection_.empty()?EffectiveLauncher(game,gameLaunchers_,sources_,libraries_):pendingInjection_;
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
    void Move(int amount) {
        if (visible_.empty() || libraryFadePhase_) return;
        detailsBackdropReady_ = false;
        int next = static_cast<int>(selected_) + amount;
        if (preferences_.wrapNavigation && visible_.size() > 1) {
            const int count = static_cast<int>(visible_.size());
            // Accelerated steps of four must not stall a four-title library.
            if (amount != 0 && amount % count == 0) next = static_cast<int>(selected_) + (amount > 0 ? 1 : -1);
            next = (next % count + count) % count;
            if (std::abs(next - static_cast<int>(selected_)) > count / 2) visualSelected_ = static_cast<float>(next);
        } else {
            next = std::clamp(next, 0, static_cast<int>(visible_.size()) - 1);
        }
        selected_ = static_cast<std::size_t>(next);
    }
    void ChangeTab(int amount) {
        const int count = static_cast<int>(LibraryTabs().size());
        if (count<2) return;
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
    void ToggleDetails() {
        if (!Selected()) return;
        coverInspectOpen_ = false;
        detailsBackdropReady_ = false;
        details_ = !details_; detailsRow_ = 1; detailsPage_ = 0; detailsLauncherEditing_=false;detailsPanelFocus_=false;detailsPanelRow_=0;
        savedInjectionState_.erase(LauncherGameKey(*Selected()));
    }
    int DetailsGameRows() const {return Selected()&&!InjectionModule(*Selected()).empty()?3:2;}
    const char *DetailsAction() const {
        if(detailsRow_==2&&detailsPanelFocus_)return detailsPanelRow_==0?"EDIT LAUNCHER":detailsPanelRow_==DetailsGameRows()-1?"SYNOPSIS":"INJECT / RE-INJECT";
        static constexpr const char *actions[]={"TOGGLE FAVORITE","LAUNCH","EDIT LAUNCHER","LIBRARY SETTINGS","NEXT ARTWORK","LAUNCHER SETTINGS"};
        return actions[detailsRow_];
    }
    void DetailsInput(int horizontal, int vertical, bool accept, bool back, bool more) {
        if (!details_ || !Selected()) return;
        if(detailsLauncherEditing_) {
            if(back||vertical||accept||more) { detailsLauncherEditing_=false; if(back||accept)return; }
            else if(horizontal) { ChangeGameLauncher(horizontal); return; }
        }
        if(back) {if(detailsPanelFocus_){detailsPanelFocus_=false;return;}details_=false;return;}
        if(horizontal){detailsPanelFocus_=horizontal>0;detailsPage_=0;return;}
        if(vertical) {
            if(detailsPanelFocus_) {
                if(detailsRow_==2)detailsPanelRow_=(detailsPanelRow_+vertical+DetailsGameRows())%DetailsGameRows();
                else detailsPage_+=vertical;
            } else {detailsRow_=(detailsRow_+vertical+6)%6;detailsPage_=0;detailsPanelRow_=0;}
        }
        if(accept&&detailsRow_==2&&!more) {
            if(!detailsPanelFocus_){detailsPanelFocus_=true;return;}
            if(detailsPanelRow_==0){detailsLauncherEditing_=true;return;}
            if(detailsPanelRow_==1&&DetailsGameRows()==3) {
                pendingInjection_=InjectionModule(*Selected());
                if(pendingInjection_.empty()){SetStatus("NO INJECTION MODULE FOR THIS GAME");return;}
                detailsLaunchRequested_=true;return;
            }
            ++detailsPage_;return;
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
    bool CheckDetails() {
        if(!VisualPreview("details"))return false;
        DetailsInput(0,-1,false,false,false); if(detailsRow_!=0)return false;
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
        if(detailsRow_!=4||detailsPage_!=1)return false;
        DetailsTouch(430,193,0);if(detailsRow_!=4)return false; // rail gap
        DetailsInput(0,0,false,true,false);if(details_)return false;
        VisualPreview("detailsinfo");DetailsInput(0,0,true,false,false);
        if(!settings_||settingsCategory_!=5)return false;
        for(int skin:{0,1}) {
            VisualPreview("detailslauncher");preferences_.skin=skin;
            if(DetailsGameRows()!=2)return false;
            DetailsInput(1,0,false,false,false);DetailsInput(0,1,false,false,false);
            if(detailsPanelRow_!=1||std::string(DetailsAction())!="SYNOPSIS")return false;
            DetailsInput(0,0,true,false,false);
            if(ConsumeDetailsLaunch()||!pendingInjection_.empty())return false;
            DetailsTouch(600,410,0); // No invisible third action on native games.
            if(detailsPanelRow_!=1||ConsumeDetailsLaunch())return false;
            DetailsInput(0,1,false,false,false);if(detailsPanelRow_!=0)return false;
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
            if(LauncherRows()!=(std::string(system.id)=="ds"?16:12))return false;
            launcherRow_=InjectionCacheRow();settingsEditing_=true;const int old=injectionDraft_.cacheMiB;
            LauncherInput(1,0,false,false);if(injectionDraft_.cacheMiB!=old+512)return false;
        }
        std::puts("PASS: all seven VC systems expose injection and system-appropriate profile rows");
        std::puts("PASS: details navigation, wrap, favorite, launch request, pages, touch and settings routes");
        return true;
    }
    void DetailsTouch(int x,int y,int drag) {
        if (!details_ || detailsProgress_<.98f || std::abs(drag)>20) return;
        if(detailsRow_==2&&x>=512&&x<1195&&y>=283&&y<449) {
            const int row=(y-283)/56;
            if(row>=DetailsGameRows()||(y-283)%56>=50)return;
            detailsPanelFocus_=true;detailsPanelRow_=row;
            if(row==0&&detailsLauncherEditing_){ChangeGameLauncher(x<822?-1:1);return;}
            detailsLauncherEditing_=false;DetailsInput(0,0,true,false,false);return;
        }
        if(preferences_.skin!=1) {
            if(x>=372 && x<448 && y>=133 && y<541 && (y-133)%70<58) {
                detailsPanelFocus_=false;detailsLauncherEditing_=false;detailsRow_=(y-133)/70;detailsPage_=0;DetailsInput(0,0,true,false,false);
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
            DetailsInput(0,0,true,false,false);
        } else if(y>=550 && y<594) {
            if(x>=512 && x<800) DetailsInput(0,0,true,false,false);
            else if(x>=805 && x<960) DetailsInput(0,0,false,true,false);
            else if(x>=970 && x<1185) DetailsInput(0,0,false,false,true);
        }
    }
    void ToggleSettings() {
        detailsBackdropReady_ = false;
        settings_=!settings_; details_=false;
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
    bool OverlayOpen() const { return scanOpen_ || updateOpen_ || WelcomeOpen() || InjectionDialogOpen() || CoverInspectOpen() || details_ || settings_ || sourceManager_ || sourceEditor_ || directoryBrowser_; }
    bool ScanOpen() const {return scanOpen_;}
    void StartScan() {
        if(scanOpen_)return;
        FlushSettingsSave(true);BrowserCancel();DismissVideo();StopMusic();
        scanState_={};scanState_.progress.stage="STARTING SCAN";scanApplied_=false;scanCancelling_=false;
        scanOpen_=true;scanStarted_=SDL_GetTicks();scanElapsed_=0;detailsBackdropReady_=false;
#ifndef UFLOW_HOST_PREVIEW
        USBStorage::LogReadStats("scan-start");
#endif
        scanWorker_.Start();
    }
    void StopScan(){scanWorker_.Stop();}
    void CancelScan(){
        if(scanOpen_ && !scanState_.finished && scanState_.progress.stage!="SAVING LIBRARY"){
            scanCancelling_=true;scanWorker_.RequestCancel();
        }
    }
    void ScanInput(bool close) {
        if(scanOpen_ && scanState_.finished && close){scanOpen_=false;detailsBackdropReady_=false;ApplyMusic();}
    }
    void PollScan() {
        if(!scanOpen_ || scanApplied_)return;
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
            }
        }
    }
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
        if(libraryFadePhase_ && action!=ControlAction::PreviousLibrary && action!=ControlAction::NextLibrary)return false;
        return (trigger & preferences_.bindings[static_cast<int>(action)]) != 0;
    }
    bool ButtonHeld(ControlAction action, std::uint32_t hold) const {
        if(libraryFadePhase_ && action!=ControlAction::PreviousLibrary && action!=ControlAction::NextLibrary)return false;
        return (hold & preferences_.bindings[static_cast<int>(action)]) != 0;
    }
    bool CapturingBinding() const { return bindingCapture_ >= 0; }
    void CaptureBinding(std::uint32_t trigger) {
#ifndef UFLOW_HOST_PREVIEW
        constexpr std::uint32_t allowed = VPAD_BUTTON_A|VPAD_BUTTON_B|VPAD_BUTTON_X|VPAD_BUTTON_Y|
            VPAD_BUTTON_PLUS|VPAD_BUTTON_MINUS|VPAD_BUTTON_L|VPAD_BUTTON_R|VPAD_BUTTON_ZL|VPAD_BUTTON_ZR|
            VPAD_BUTTON_LEFT|VPAD_BUTTON_RIGHT|VPAD_BUTTON_UP|VPAD_BUTTON_DOWN;
        const std::uint32_t choice = trigger & allowed;
        if (!choice) return;
        preferences_.bindings[bindingCapture_] = choice & (~choice + 1);
        SetStatus(std::string(ControlActionName(static_cast<ControlAction>(bindingCapture_)))+" SET TO "+ControlBindingName(preferences_.bindings[bindingCapture_]));
        bindingCapture_=-1; QueueSettingsSave();
#else
        (void)trigger; // Preview has no console input device.
#endif
    }
    bool DetailsOpen() const { return details_; }
    bool SourceManagerOpen() const { return sourceManager_ && !sourceEditor_ && !directoryBrowser_ && !mediaHelp_; }
    bool MediaHelpOpen() const { return mediaHelp_; }
    bool SourceEditorOpen() const { return sourceEditor_ && !directoryBrowser_; }
    bool DirectoryBrowserOpen() const { return directoryBrowser_; }
    bool ConsumeScanRequest() { const bool result = scanRequested_; scanRequested_ = false; return result; }
    void CloseOverlay() {
        if(CoverInspectOpen()) { CloseCoverInspect(); return; }
        if(typeText_) { typeText_=false; return; }
        if(settingsEditing_) { settingsEditing_=false; return; }
        if(vcMapMode_) { vcMapMode_=0;launcherRow_=7;return; }
        if(launcherBrowse_) { launcherBrowse_=false; return; }
        if(launcherEditor_) { launcherEditor_=false; launcherRow_=0; settingsEditing_=false; return; }
        if(settings_&&settingsCategory_==5) { if(settingsEditing_)settingsEditing_=false;else settings_=false; return; }
        if(typeEditor_) { typeEditor_=false; return; }
        if(typeManager_) { typeManager_=false; return; }
        if(settingsEditing_) { settingsEditing_=false; return; }
        if(audioSettings_) { audioSettings_=false; settingsRow_=4; return; }
        if (mediaHelp_) { mediaHelp_=false; return; }
        if (directoryBrowser_) { BrowserBack(); return; }
        if (sourceEditor_) { sourceEditor_ = false; return; }
        if (sourceManager_) { sourceManager_ = false; settings_=false; return; }
        if (settings_ && settingsCategory_==2) { sourceManager_=true; return; }
        details_ = settings_ = false;
    }

    void SettingsChangeCategory(int amount) {
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
            if (settingsRow_ == 0) preferences_.skin = (preferences_.skin + amount + 2) % 2;
            else if (settingsRow_ == 1) {
                const int tabCount = std::max(1, static_cast<int>(LibraryTabs().size()));
                preferences_.startTab = (preferences_.startTab + amount + tabCount) % tabCount;
            }
            else if (settingsRow_==2) preferences_.wrapNavigation = !preferences_.wrapNavigation;
            else if(settingsRow_==3) { preferences_.showInstalled=!preferences_.showInstalled; RebuildVisible(); }
            else if(settingsRow_==4) { audioSettings_=true; settingsRow_=0; }
            else {
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
        } else if (settingsCategory_ == 2) {
            bool *values[] = {&preferences_.useBoxArt, &preferences_.useBackgrounds,
                              &preferences_.useLogos, &preferences_.usePreviews};
            *values[settingsRow_] = !*values[settingsRow_];
        } else if (settingsCategory_ == 3) {
            if (settingsRow_ == 0) preferences_.coverLayout = (preferences_.coverLayout + amount + 4) % 4;
            else if (settingsRow_ == 1) preferences_.animationSpeed = (preferences_.animationSpeed + amount + 3) % 3;
            else if (settingsRow_ == 2) preferences_.coverSpacing = (preferences_.coverSpacing + amount + 3) % 3;
            else if (settingsRow_ == 3) preferences_.backgroundMotion = !preferences_.backgroundMotion;
            else if (settingsRow_ == 4) preferences_.reflections = !preferences_.reflections;
            else preferences_.theme = (preferences_.theme + amount + 4) % 4;
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
            } else { musicTracks_=ListMusicTracks(); SetStatus("MUSIC LIST RELOADED"); }
    }

    void SettingsActivate() {
        SettingsAdjust(1);
    }
    void ApplyMusic() {
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
    void StopMusic() { StopVideo(false); glassBackground_.Stop(); music_.Stop(); }
    void PrepareSystemBackground() {
        if(injectionDialog_.state==InjectionDialog::State::Working)injectionDialog_.cancel=true;
        else injectionDialog_.Reset();
        // SDL Wii U queues WILLENTERBACKGROUND before ProcUIDrawDoneRelease.
        // Close mixer references while AX and the audio thread still run; doing
        // this on the later QUIT event can wait on a suspended audio callback.
        restoreMusicAfterBackground_=preferences_.musicEnabled;
        Trace("system-background-video-stop-begin");
        StopVideo(false);
        glassBackground_.Stop();
        Trace("system-background-video-stop-complete");
        music_.Stop();
        Trace("system-background-music-stop-complete");
        // Cancellation must not leave the decoder paused: it needs to unwind.
        videoTrigger_.Reset();
    }
    void ResumeSystemForeground() {
        SuspendVideo(false);
        if (restoreMusicAfterBackground_) {
            restoreMusicAfterBackground_=false;
            ApplyMusic();
        }
    }
    void StopVideo(bool resumeMusic = true) {
        videoPending_=false;
        const bool wasActive = video_.Active();
        if (wasActive) ReportVideo(true);
        video_.Stop();
        if (wasActive && resumeMusic) music_.Pause(false);
    }
    void DismissVideo() { videoTrigger_.Dismiss(); StopVideo(); }
    void ReportVideo(bool force = false) {
        if (preview_ || !video_.Active()) return;
        const Uint32 now=SDL_GetTicks(), elapsed=now-videoReportAt_;
        if (elapsed < (force ? 250u : 5000u) || (!force && videoReports_>=6)) return;
        const auto stats=video_.Statistics();
        char message[512];
        std::snprintf(message,sizeof(message),
            "video-perf dt=%u hw=%d decoded=%u shown=%u converted=%u late=%u read_ms=%.1f decode_ms=%.1f convert_ms=%.1f upload_ms=%.1f audio_frames=%u output=%dx%d max_gap_ms=%u gaps_over50=%u audio_missing=%u lag_ms=%d handoff_ms=%.1f cores=%u/%u reordered=%u pts_fixed=%u",
            elapsed,stats.hardware,stats.decoded-videoReportStats_.decoded,stats.presented-videoReportStats_.presented,
            stats.converted-videoReportStats_.converted,stats.lateDropped-videoReportStats_.lateDropped,
            (stats.readUs-videoReportStats_.readUs)/1000.,(stats.decodeUs-videoReportStats_.decodeUs)/1000.,
            (stats.convertUs-videoReportStats_.convertUs)/1000.,(stats.uploadUs-videoReportStats_.uploadUs)/1000.,
            stats.audioFrames-videoReportStats_.audioFrames,stats.outputWidth,stats.outputHeight,
            stats.maxPresentGapMs,stats.longPresentGaps-videoReportStats_.longPresentGaps,
            stats.audioMissingFrames-videoReportStats_.audioMissingFrames,stats.videoLagMs,
            (stats.handoffUs-videoReportStats_.handoffUs)/1000.,stats.decoderCores,stats.converterCores,
            stats.reorderedFrames-videoReportStats_.reorderedFrames,stats.timestampCorrections-videoReportStats_.timestampCorrections);
        Trace(message); videoReportStats_=stats; videoReportAt_=now; ++videoReports_;
    }
    bool VideoBehindInspection() const { return !videoOptions_.foreground; }
    void VideoInput(bool active) {
        videoInputActive_=active;
        if(active) { videoTrigger_.Reset(); if(video_.Active()) StopVideo(); }
    }
    void SuspendVideo(bool suspended) {
        video_.Suspend(suspended);
        if (!suspended) { StopVideo(); videoTrigger_.Reset(); }
    }

    bool SettingsSelectorRow(int row) const {
        if(audioSettings_) return row>=0 && row<3;
        if(settingsCategory_==0) return (row>=0 && row<4) || row==5;
        if(settingsCategory_==2) return row>=0 && row<4;
        return settingsCategory_==3 && row>=0 && row<6;
    }
    bool SettingsSelectorSelected() const {
        if(settingsSidebar_ || directoryBrowser_ || mediaHelp_) return false;
        if(sourceEditor_) return SourceEditorSelector(sourceEditorRow_);
        return !sourceManager_ && SettingsSelectorRow(settingsRow_);
    }
    // A enters value editing; only then do left/right adjust instead of moving focus.
    // B ends editing in place; up/down ends editing and moves to the adjacent row.
    void SettingsInput(int horizontal,int vertical,bool accept=false,bool back=false,
                       bool previous=false,bool useFolder=false,bool plus=false) {
        if (!settings_) return;
        if(typeText_) { TypeTextInput(horizontal,vertical,accept,back,previous,plus); return; }
        if(settingsCategory_==5&&!settingsSidebar_) { LauncherInput(horizontal,vertical,accept,back);return; }
        if (back) { CloseOverlay(); return; }
        if(typeManager_&&!settingsSidebar_) {
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
                ResetControlBindings(preferences_); QueueSettingsSave(); SetStatus("CONTROLS RESET");
            } else { bindingCapture_=settingsRow_; SetStatus("PRESS A BUTTON TO BIND"); }
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

    void SettingsTouch(int x,int y,int dragY=0) {
        if(!settings_ || settingsProgress_<.95f) return;
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
        if(typeManager_ || typeText_) {
            if(typeText_) {
            if(y>=320&&y<512) { int col=(x-390)/60, row=(y-320)/32; if(col>=0&&col<12&&row>=0&&row<6&&row*12+col<static_cast<int>(std::strlen(TypeKeys))){typeKey_=row*12+col; TypeTextInput(0,0,true,false,false,false);} }
                else if(y>=578) {
                    const int action=std::clamp((x-390)/185,0,3);
                    if(action==0&&typeBuffer_.size()<240)typeBuffer_+=' ';
                    else TypeTextInput(0,0,false,action==3,action==1,action==2);
                }
                return;
            }
            if(y>=260&&y<548) { const int first=std::max(0,typeRow_-5); int row=first+(y-260)/40;
                if(row<TypeRowCount()){
                    if(typeEditor_&&wasEditing&&row==typeRow_&&(row==1||(row>=3&&row<=8))){
                        TypeAdjust(x<760?-1:1);settingsEditing_=true;
                    }else{typeRow_=row;TypeActivate();}
                } }
            else if(y>=578) {
                const int action=std::clamp((x-390)/185,0,3);
                if(action<2){settingsEditing_=false;typeRow_=(typeRow_+(action==0?-1:1)+TypeRowCount())%TypeRowCount();}
                else if(action==2)TypeActivate();else CloseOverlay();
            }
            return;
        }
        if(settingsCategory_==5) {
            if(std::abs(dragY)>=30){LauncherInput(0,dragY<0?1:-1,false,false);return;}
            if(y>=260&&y<540){const int row=std::max(0,launcherRow_-5)+(y-260)/40;
                if(row<LauncherRows()){
                    if(launcherEditor_&&!launcherBrowse_&&!vcMapMode_&&wasEditing&&row==launcherRow_&&(row==1||row==5||(launcherDraft_.kind==LauncherKind::DSInjection&&row>=10&&row<=14))){
                        settingsEditing_=true;LauncherInput(x<760?-1:1,0,false,false);
                    }else{launcherRow_=row;LauncherActivate();}
                }}
            else if(y>=578) {
                const int action=std::clamp((x-390)/185,0,3);
                LauncherInput(0,action<2?(action==0?-1:1):0,action==2,action==3);
            }
            return;
        }
        if(std::abs(dragY)>=30) {
            SettingsInput(0,dragY<0?1:-1); return;
        }
        if(y>=578 && y<620) {
            const int action=std::clamp((x-390)/185,0,3);
            if(action==3 && wasEditing) return;
            if(directoryBrowser_) {
                if(action==0) BrowserOpenSelected();
                else if(action==1) BrowserSelectCurrent();
                else if(action==2) BrowserBack();
                else BrowserCancel();
            } else if(sourceEditor_) {
                if(action==0) SourceEditorAdjust(-1);
                else if(action==1) SourceEditorActivate();
                else if(action==2) SaveDraftSource();
                else CloseOverlay();
            } else if(mediaHelp_) CloseOverlay();
            else if(sourceManager_) {
                if(action==0) SourceMove(-1);
                else if(action==1) SourceMove(1);
                else if(action==2) SourceAdd();
                else CloseOverlay();
            } else {
                if(action==0) settingsSidebar_=true;
                else if(action==1) SettingsAdjust(-1);
                else if(action==2) SettingsAdjust(1);
                else CloseOverlay();
            }
            return;
        }
        if(directoryBrowser_) {
            const int total=static_cast<int>(browserDirectories_.size());
            const int first=std::max(0,std::min(browserRow_-5,total-8));
            for(int row=0;row<8 && first+row<total;++row) if(y>=271+row*37 && y<305+row*37) {
                browserRow_=first+row; BrowserOpenSelected(); return;
            }
        } else if(sourceEditor_) {
            const int first = SourceEditorFirst();
            for(int row=first;row<std::min(first+7,SourceEditorCount());++row) if(y>=272+(row-first)*38 && y<309+(row-first)*38) {
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
            const int pitch=controls?29:(settingsCategory_==3 || (settingsCategory_==0 && !audioSettings_)?62:76);
            const int top=controls?233:187, bottomOffset=controls?28:57;
            for(int row=0;row<SettingsRowCount();++row) if(y>=top+row*pitch && y<top+bottomOffset+row*pitch) {
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
        const std::string key=l.id+":"+std::to_string(static_cast<int>(l.kind))+":"+l.executable;
        const auto found=launcherStatusCache_.find(key);
        if(found!=launcherStatusCache_.end())return found->second;
        return launcherStatusCache_[key]=LauncherReadiness(l);
    }
    // Resolve at launch, not scan time: changing an adapter needs no library rescan.
    std::string ConfigureLaunch(GameEntry &game,std::string &homebrew,std::string *requestFile=nullptr,VCInject::Progress progress={}) {
        if(!pendingInjection_.empty()) {
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
        std::string id=EffectiveLauncher(game,gameLaunchers_,sources_,libraries_);
        if(id.empty()) {
            if(game.platform==Platform::Files)return "SELECT A LAUNCHER IN LIBRARY / MANAGE CONTENT TYPES";
            id=game.platform==Platform::WiiU?"native":game.launcher==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
        }
        const auto it=std::find_if(launchers_.begin(),launchers_.end(),[&](const auto &l){return l.id==id;});
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
    int InjectionCacheRow() const {return injectionDraft_.system=="ds"?14:10;}
    int LauncherRows() const {return vcMapMode_?static_cast<int>(vcMapGames_.size())+1:launcherBrowse_?static_cast<int>(launcherFiles_.size()):launcherEditor_?(launcherDraft_.kind==LauncherKind::DSInjection?InjectionCacheRow()+2:10):static_cast<int>(launchers_.size())+1;}
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
    void LauncherInput(int horizontal,int vertical,bool accept,bool back) {
        if(back) {
            if(vcMapMode_){if(vcMapMode_==2)RefreshVCMap(1);else{vcMapMode_=0;launcherRow_=7;}return;}
            if(launcherBrowse_){launcherBrowse_=false;launcherRow_=4;return;}
            if(settingsEditing_){settingsEditing_=false;return;}
            CloseOverlay();return;
        }
        if(vertical){settingsEditing_=false;const int count=LauncherRows();launcherRow_=(launcherRow_+vertical+count)%count;return;}
        if(horizontal) {
            if(settingsEditing_&&launcherEditor_) {
                if(launcherRow_==1) {do{launcherDraft_.kind=static_cast<LauncherKind>((static_cast<int>(launcherDraft_.kind)+horizontal+5)%5);}while(launcherDraft_.kind==LauncherKind::VirtualConsole);}
                if(launcherRow_==5)launcherDraft_.enabled=!launcherDraft_.enabled;
                if(launcherDraft_.kind==LauncherKind::DSInjection) {
                    if(injectionDraft_.system!="ds") {
                        if(launcherRow_==10)injectionDraft_.cacheMiB=((injectionDraft_.cacheMiB/512-1+horizontal+16)%16+1)*512;
                        return;
                    }
                    if(launcherRow_==10)SetStatus("USE INJECT / RE-INJECT IN GAME DETAILS");
                    if(launcherRow_==11){const int levels[]={0,1,6};int i=injectionDraft_.compression==0?0:injectionDraft_.compression==1?1:2;injectionDraft_.compression=levels[(i+horizontal+3)%3];}
                    if(launcherRow_==12){int i=injectionDraft_.brightness<0?0:injectionDraft_.brightness/10+1;i=(i+horizontal+12)%12;injectionDraft_.brightness=i==0?-1:(i-1)*10;}
                    if(launcherRow_==13)injectionDraft_.upscale=(injectionDraft_.upscale+1+horizontal+3)%3-1;
                    if(launcherRow_==14)injectionDraft_.cacheMiB=((injectionDraft_.cacheMiB/512-1+horizontal+16)%16+1)*512;
                }
            } else if(horizontal<0)settingsSidebar_=true;
            return;
        }
        if(accept)LauncherActivate();
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
                launcherBrowse_=false;launcherRow_=4;return;
            }
            if(name=="..") {
                if(launcherBrowsePath_=="fs:/vol/external01"){launcherBrowse_=false;launcherRow_=4;return;}
                launcherBrowsePath_.erase(launcherBrowsePath_.find_last_of('/'));RefreshLauncherFiles();
            } else if(name.back()=='/') {launcherBrowsePath_+="/"+name.substr(0,name.size()-1);RefreshLauncherFiles();}
            else {launcherDraft_.executable=LauncherSDPath(launcherBrowsePath_+"/"+name);launcherBrowse_=false;launcherRow_=4;}
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
        if(launcherRow_==0||launcherRow_==2||launcherRow_==3) {
            if(!launcherDraft_.moduleId.empty()&&launcherRow_!=0){SetStatus("FIELD DEFINED BY THE INSTALLED MODULE");return;}
            launcherTextField_=launcherRow_;typeBuffer_=launcherRow_==0?launcherDraft_.name:launcherRow_==2?launcherDraft_.system:launcherDraft_.extensions;
            typeText_=true;typeKey_=0;return;
        }
        if(launcherRow_==1||launcherRow_==5) {
            if(launcherRow_==1&&!launcherDraft_.moduleId.empty()){SetStatus("METHOD DEFINED BY THE INSTALLED MODULE");return;}
            if(launcherRow_==1&&(launcherDraft_.id=="native"||launcherDraft_.id=="wiiflow"||launcherDraft_.id=="usbloadergx")){SetStatus("BUILT-IN ADAPTER TYPE IS FIXED");return;}
            settingsEditing_=!settingsEditing_;return;
        }
        if(launcherRow_==4) {
            if(launcherDraft_.kind==LauncherKind::DSInjection) {
                launcherBrowse_=true;launcherBrowsePath_="fs:/vol/external01";RefreshLauncherFiles();return;
            }
            if(!launcherDraft_.moduleId.empty()){SetStatus("EXECUTABLE DEFINED BY THE INSTALLED MODULE");return;}
            if(launcherDraft_.kind!=LauncherKind::Emulator){SetStatus("BUILT-IN ADAPTER - NO EXECUTABLE SELECTION NEEDED");return;}
            launcherBrowse_=true;launcherBrowsePath_="fs:/vol/external01";RefreshLauncherFiles();return;
        }
        if(launcherRow_==6) {
            if(launcherDraft_.kind==LauncherKind::DSInjection){const auto error=VCInject::CheckBase(injectionDraft_);SetStatus(error.empty()?"VC BASE STRUCTURE OK - GAME COMPATIBILITY REQUIRES TESTING":error,10000);return;}
            SetStatus(launcherDraft_.kind==LauncherKind::VirtualConsole?"USE PREPARED INJECTIONS; ROM MAPPINGS: SEE LAUNCHERS.md":LauncherReadiness(launcherDraft_),10000);return;
        }
        if(launcherRow_==7) {
            if(launcherDraft_.kind==LauncherKind::DSInjection){SetStatus("ASSIGN THIS LAUNCHER, THEN PLAY - MISSING INJECTIONS ARE CREATED AUTOMATICALLY",10000);return;}
            if(!launcherDraft_.enabled){SetStatus("LAUNCHER IS DISABLED");return;}
            if(launcherDraft_.kind==LauncherKind::VirtualConsole){RefreshVCMap(1);return;}
            if(launcherDraft_.kind!=LauncherKind::Emulator){SetStatus("SELECT A GAME IN THE LIBRARY TO USE THIS ADAPTER");return;}
            if(!LauncherExecutableExists(launcherDraft_.executable)){SetStatus("SELECT AN INSTALLED EMULATOR EXECUTABLE FIRST");return;}
            if(!launcherDraft_.moduleId.empty()) {
                const auto *m=FindLauncherModule(launcherDraft_.moduleId);
                if(!m||!m->error.empty()){SetStatus("MODULE MISSING OR INVALID");return;}
                if(!UFlowRom::Clear("fs:/vol/external01/"+m->request)){SetStatus("CANNOT CLEAR OLD ROM REQUEST");return;}
            }
            pendingHomebrew_=LauncherSDPath(launcherDraft_.executable);return;
        }
        if(launcherRow_==8) {
            if(launcherDraft_.kind==LauncherKind::DSInjection&&!VCInject::Save(launcherDraft_.id,injectionDraft_)){SetStatus("INVALID VC SETTINGS OR SD WRITE ERROR");return;}
            for(const auto &l:libraries_)if(l.launcherId==launcherDraft_.id&&!LauncherAccepts(launcherDraft_,l.parser)){SetStatus("REASSIGN INCOMPATIBLE CONTENT TYPES FIRST");return;}
            auto next=launchers_;if(launcherIndex_<0)next.push_back(launcherDraft_);else next[launcherIndex_]=launcherDraft_;
            if(!SaveLaunchers(next)){SetStatus("COULD NOT SAVE - CHECK NAME, EXECUTABLE AND SD");return;}
            launchers_=std::move(next);launcherEditor_=false;launcherRow_=launcherIndex_<0?static_cast<int>(launchers_.size()):launcherIndex_+1;
            SetStatus("LAUNCHER SAVED - ASSIGN IT IN CONTENT TYPES");return;
        }
        if(launcherRow_==9) {
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
        if(launcherDraft_.kind==LauncherKind::DSInjection&&launcherRow_>=10&&launcherRow_<=InjectionCacheRow())settingsEditing_=!settingsEditing_;
        if(launcherDraft_.kind==LauncherKind::DSInjection&&launcherRow_==InjectionCacheRow()+1)SetStatus(injectionDraft_.system=="n64"?"AUTO PROFILE BY ROM ID / REGION / REVISION. ROM.INI OVERRIDES. SAVES ISOLATED":"BASE EMULATOR OPTIONS PRESERVED; SAVES ISOLATED PER ROM",10000);
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
        else if(launcherEditor_)rows={{"NAME",launcherDraft_.name},{"METHOD",LauncherKindName(launcherDraft_.kind)},
            {"SYSTEM",launcherDraft_.system},{"ROM EXTENSIONS",launcherDraft_.extensions},
            {"EXECUTABLE",launcherDraft_.kind==LauncherKind::Emulator?launcherDraft_.executable:"BUILT-IN ADAPTER"},
            {"ENABLED",OnOff(launcherDraft_.enabled)},{"STATUS / HELP",CachedLauncherStatus(launcherDraft_)},
            {launcherDraft_.kind==LauncherKind::VirtualConsole?"MAP ROM TO INJECTION":"OPEN EMULATOR",launcherDraft_.kind==LauncherKind::VirtualConsole?"SELECT EXISTING TITLE":"OPENS ITS ROM BROWSER"},{"SAVE LAUNCHER","A  SAVE"},{"REMOVE ENTRY","FILES ARE NOT DELETED"}};
        else {rows.push_back({"+  ADD LAUNCHER / EMULATOR",""});for(const auto &l:launchers_)rows.push_back({l.name,CachedLauncherStatus(l)});}
        if(launcherEditor_&&!launcherBrowse_&&!vcMapMode_&&launcherDraft_.kind==LauncherKind::DSInjection) {
            rows[4]={"VC BASE FOLDER",injectionDraft_.base.empty()?"SELECT CODE / CONTENT / META BASE":injectionDraft_.base};
            rows[6]={"VALIDATE BASE","A  CHECK FILE STRUCTURE"};rows[7]={"SETUP HELP","ASSIGN LAUNCHER, THEN PLAY"};
            if(injectionDraft_.system=="ds"){
            rows.push_back({"PREPARATION","AUTOMATIC WHEN OUTPUT IS MISSING"});
            rows.push_back({"ZIP COMPRESSION",injectionDraft_.compression==0?"STORED":injectionDraft_.compression==1?"FAST DEFLATE":"SMALLER DEFLATE"});
            rows.push_back({"BRIGHTNESS",injectionDraft_.brightness<0?"BASE DEFAULT":std::to_string(injectionDraft_.brightness)});
            rows.push_back({"PIXEL ART UPSCALER",injectionDraft_.upscale<0?"BASE DEFAULT":OnOff(injectionDraft_.upscale!=0)});
            }
            rows.push_back({"CACHE LIMIT",std::to_string(injectionDraft_.cacheMiB)+" MiB"});
            rows.push_back({injectionDraft_.system=="n64"?"CONFIG / SAVES":"SCREENS / SAVES",injectionDraft_.system=="n64"?"AUTO PROFILE / ROM.INI OVERRIDE":"VC MENU / ISOLATED PER ROM"});
        }
        const int first=std::max(0,launcherRow_-5);
        DrawText(renderer_,std::to_string(launcherRow_+1)+" / "+std::to_string(rows.size()),x+665,237,1,text);
        for(int i=first;i<std::min(first+7,static_cast<int>(rows.size()));++i) {
            int y=260+(i-first)*40;
            if(i==launcherRow_&&!settingsSidebar_)FillRoundedRect(renderer_,{x-12,y-5,750,36},10,{accent.r,accent.g,accent.b,70});
            DrawFittedText(renderer_,rows[i].first,x,y,launcherBrowse_?720:320,2,text);
            if(!launcherBrowse_)DrawFittedText(renderer_,rows[i].second,x+340,y,380,2,text);
        }
        DrawText(renderer_,settingsEditing_?"LEFT/RIGHT  CHANGE    A / B  DONE":"A  SELECT / EDIT    B  BACK",x,556,1,text);
        DrawSettingsActions(x,accent,{"PREVIOUS","NEXT","SELECT","B BACK"});
    }
    int TypeRowCount() const { return typeEditor_?11:static_cast<int>(libraries_.size())+1; }
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
            typeEditor_=true;typeRow_=0;return;
        }
        if(typeRow_==0||typeRow_==2) { typeName_=typeRow_==0;typeBuffer_=typeName_?typeDraft_.name:typeDraft_.extensions;typeText_=true;typeKey_=0;return; }
        if(typeRow_==9) {
            if(typeDraft_.name.find_first_not_of(' ')==std::string::npos) {SetStatus("ENTER A NAME");return;}
            if(typeDraft_.parser==Platform::Files&&typeDraft_.extensions.empty()){SetStatus("ENTER FILE EXTENSIONS");return;}
            auto next=libraries_;
            if(typeIndex_>=0) next.erase(next.begin()+typeIndex_);
            const int position=std::clamp(typeDraft_.order,0,static_cast<int>(next.size()));
            next.insert(next.begin()+position,typeDraft_);
            for(std::size_t i=0;i<next.size();++i) next[i].order=static_cast<int>(i);
            if(!SaveLibraries(next)){SetStatus("COULD NOT SAVE CONTENT TYPES");return;}
            libraries_=std::move(next);typeEditor_=false;settingsEditing_=false;typeRow_=position+1;tab_=0;visualTab_=0;RebuildVisible();
            SetStatus("CONTENT TYPE SAVED",4000);return;
        }
        if(typeRow_==10) {
            if(typeIndex_<0){typeEditor_=false;typeRow_=0;return;}
            if(std::any_of(sources_.begin(),sources_.end(),[&](const auto&s){return s.libraryId==typeDraft_.id;}) ||
               std::any_of(mediaSources_.begin(),mediaSources_.end(),[&](const auto&s){return s.libraryId==typeDraft_.id;})) {SetStatus("REASSIGN THIS TYPE'S CONTENT AND MEDIA DIRECTORIES FIRST",6000);return;}
            if(typeDraft_.id=="wiiu"){SetStatus("DISABLE THE BUILT-IN WII U TYPE INSTEAD");return;}
            auto next=libraries_;next.erase(next.begin()+typeIndex_);
            if(!SaveLibraries(next)){SetStatus("COULD NOT SAVE CONTENT TYPES");return;}
            libraries_=std::move(next);typeEditor_=false;typeRow_=0;tab_=0;visualTab_=0;RebuildVisible();return;
        }
        settingsEditing_=!settingsEditing_;
    }
    static constexpr const char *TypeKeys="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,_-/:";
    void TypeTextInput(int horizontal,int vertical,bool accept,bool back,bool erase,bool done) {
        if(back){typeText_=false;return;}
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
            DrawText(renderer_,"A  TYPE    X  ERASE    +  DONE    B  CANCEL",x,556,1,text);
            DrawSettingsActions(x,accent,{"SPACE","ERASE","DONE","B CANCEL"});return;
        }
        std::vector<std::pair<std::string,std::string>> rows;
        if(typeEditor_) rows={{"NAME",typeDraft_.name},{"SCANNER",PlatformName(typeDraft_.parser)},
            {"EXTENSIONS",typeDraft_.extensions},{"LAUNCHER",LauncherLabel(typeDraft_.launcherId)},
            {"ENABLED",OnOff(typeDraft_.enabled)},{"RAIL POSITION",std::to_string(typeDraft_.order+1)},
            {"CASE WIDTH",std::to_string(typeDraft_.caseWidthMm)+" mm"},
            {"CASE HEIGHT",std::to_string(typeDraft_.caseHeightMm)+" mm"},
            {"CASE THICKNESS",std::to_string(typeDraft_.caseThicknessMm)+" mm"},
            {"SAVE TYPE","A  SAVE"},{"REMOVE TYPE",typeIndex_<0?"CANCEL NEW TYPE":"A  REMOVE"}};
        else {rows.push_back({"+  ADD CONTENT TYPE",""});for(const auto&l:libraries_) rows.push_back({l.name,OnOff(l.enabled)});}
        const int first=std::max(0,typeRow_-5);
        for(int i=first;i<std::min(first+7,static_cast<int>(rows.size()));++i){const int y=260+(i-first)*40;
            if(i==typeRow_&&!settingsSidebar_)FillRoundedRect(renderer_,{x-12,y-5,750,36},10,{accent.r,accent.g,accent.b,70});
            DrawFittedText(renderer_,rows[i].first,x,y,320,2,text);DrawFittedText(renderer_,rows[i].second,x+340,y,380,2,text);}
        DrawText(renderer_,settingsEditing_?"LEFT/RIGHT  CHANGE    A / B  DONE":"A  EDIT / SELECT    B  BACK",x,556,1,text);
        DrawSettingsActions(x,accent,{"PREVIOUS","NEXT","SELECT","B BACK"});
    }
    std::vector<MediaDirectoryRow> MediaRows() const { return MediaDirectoryRows(mediaSources_,libraries_,expandedMediaGroups_); }
    int SourceListCount() const { return mediaMode_?static_cast<int>(MediaRows().size()):SourceCount()+3; }
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
            if(row.kind==MediaDirectoryRow::Display) { sourceManager_=false; settingsRow_=0; return; }
            if(row.kind==MediaDirectoryRow::Help) { mediaHelp_=true; return; }
            actual=row.kind==MediaDirectoryRow::Directory?row.source:SourceCount();
        }
        if (actual==SourceCount()+1) { SourceScan(); return; }
        editingSourceIndex_=actual<SourceCount() ? actual : -1;
        if (mediaMode_) draftMediaSource_=editingSourceIndex_>=0 ? mediaSources_[editingSourceIndex_] :
            MediaSource{MediaType::Cover,-1,"fs:/vol/external01",true,0};
        else draftSource_=editingSourceIndex_>=0 ? sources_[editingSourceIndex_] :
            ContentSource{Platform::WiiU,"fs:/vol/external01",true,3,GameLauncher::WiiFlow,"wiiu"};
        sourceEditorRow_ = 0;
        defaultFileChosen_=false;
        AutoSelectDraftDefault();
        sourceEditor_ = true;
    }

    void SourceAdd() {
        if(mediaMode_) {
            const auto rows=MediaRows();
            const auto selected=rows[std::clamp(sourceRow_,0,static_cast<int>(rows.size())-1)];
            sourceRow_=static_cast<int>(rows.size())-4; SourceActivate();
            if(selected.kind==MediaDirectoryRow::Header || selected.kind==MediaDirectoryRow::Directory)
                draftMediaSource_.libraryId=selected.key;
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
            if(saved) { StopVideo(); videoSelection_=nullptr; videoTrigger_.Reset(); }
        } else {
            auto next=sources_; next.erase(next.begin()+editingSourceIndex_);
            saved=SaveContentSources(next); if(saved) sources_=std::move(next);
        }
        if (!saved) { SetStatus("COULD NOT SAVE PATHS",5000); return; }
        sourceEditor_=false; sourceRow_=std::min(sourceRow_,SourceListCount()-1);
        SetStatus("PATH REMOVED - FILES UNCHANGED");
    }

    void SourceScan() { scanRequested_ = true; }

    bool VideoSourceEditing() const { return mediaMode_ && draftMediaSource_.type == MediaType::Preview; }
    void AutoSelectDraftDefault() {
        if(mediaMode_ && editingSourceIndex_<0 && !defaultFileChosen_)
            draftMediaSource_.defaultFile=FindDefaultMediaFile(draftMediaSource_);
    }
    bool BackgroundVideoEditing() const { return VideoSourceEditing() && !draftMediaSource_.video.foreground; }
    int SourceEditorCount() const { return mediaMode_ ? (VideoSourceEditing() ? (BackgroundVideoEditing()?12:11) : 8) : 7; }
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
        } else if(VideoSourceEditing() && sourceEditorRow_==5) {
            draftMediaSource_.video.delayMs=std::clamp(draftMediaSource_.video.delayMs+amount*250,0,10000);
        } else if(VideoSourceEditing() && sourceEditorRow_==6) {
            draftMediaSource_.video.foreground=!draftMediaSource_.video.foreground;
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
            if(saved) { StopVideo(); videoSelection_=nullptr; videoTrigger_.Reset(); }
        } else {
            auto next=sources_;
            if(editingSourceIndex_>=0) next[editingSourceIndex_]=draftSource_; else next.push_back(draftSource_);
            saved=SaveContentSources(next); if(saved) sources_=std::move(next);
        }
        if(!saved) { SetStatus("COULD NOT SAVE PATHS",5000); return; }
        sourceEditor_ = false;
        sourceRow_ = 0;
        if(mediaMode_) FocusMediaSource(editingSourceIndex_>=0?editingSourceIndex_:static_cast<int>(mediaSources_.size())-1);
        SetStatus("PATH SAVED - USE SCAN NOW TO APPLY");
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

    bool Render(float deltaSeconds, const std::string &capturePath = {}) {
        const Uint64 performanceStart=SDL_GetPerformanceCounter();
        textures_.BeginFrame();
        PumpBrowser();
        PollScan();
        if (preview_) deltaSeconds = 0; // deterministic transition samples, not performance measurements
        gSmoothText = preferences_.skin == 1;
        gGlassOverVideo=false;
        UpdateControlHints();
        const float response = preferences_.animationSpeed == 0 ? 6.5f : preferences_.animationSpeed == 1 ? 11.0f : 17.0f;
        const float step = 1.0f - std::exp(-std::max(0.f,deltaSeconds) * response);
        visualSelected_ += (static_cast<float>(selected_) - visualSelected_) * step;
        visualTab_ += (static_cast<float>(pendingTab_>=0?pendingTab_:tab_) - visualTab_) * step;
        AdvanceLibraryFade(deltaSeconds);
        tabSlide_ += (0.0f - tabSlide_) * step;
        if (preferences_.skin == 1) {
            const float duration = CoverTransitionDuration(preferences_.animationSpeed);
            detailsProgress_ = AdvanceCoverTransition(detailsProgress_, details_, deltaSeconds, duration);
        } else detailsProgress_ += ((details_ ? 1.0f : 0.0f) - detailsProgress_) * step;
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
        UpdateVideo(selected);
        glassBackground_.Update(preferences_.skin==1,
            !preferences_.backgroundMotion||scanOpen_||updateOpen_||WelcomeOpen()||InjectionDialogOpen()||settings_||details_||
            detailsProgress_>.01f||settingsProgress_>.01f,
            videoPending_||video_.Active(),!video_.Active()&&video_.CanStart());
        Color accent = selected ? AccentFor(selected->platform, preferences_.theme)
                                : AccentFor(Platform::WiiU, preferences_.theme);
        if (preferences_.skin == 1) accent = {45, 174, 222, 255};

        const float overlayProgress = std::max({detailsProgress_, settingsProgress_,(scanOpen_||InjectionDialogOpen()||WelcomeOpen()||updateOpen_)?1.f:0.f});
        if (overlayProgress <= .01f && !video_.Active()) PrefetchArtwork();
        // Overlay content changes every frame, but the obscured library does not.
        // Freeze it after the first blurred frame for both skins; settings changes
        // explicitly invalidate this cache before their next render.
        const bool freezeBackdrop = overlayProgress > .01f;
        const bool reuseBackdrop = freezeBackdrop && detailsBackdropReady_;
        const bool fullVideo = video_.Ready() && videoOptions_.foreground;
        const bool offscreen = !fullVideo && (reuseBackdrop || (sceneTexture_ && SDL_SetRenderTarget(renderer_, sceneTexture_) == 0));
        if (!reuseBackdrop && !fullVideo) {
            const bool backgroundVideo=!videoOptions_.foreground && video_.Draw(kWidth,kHeight);
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
            DrawHeader(accent);
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
            DrawFooter(accent);
            if(coverInspectProgress_>.01f) DrawCoverInspect(accent);
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
                    blurReady = true;
                }
                SDL_SetRenderTarget(renderer_, nullptr);
            }
            if (overlayProgress > .01f && blurReady) {
                SDL_SetTextureAlphaMod(blurTexture_, static_cast<Uint8>(245.0f * overlayProgress));
                SDL_RenderCopy(renderer_, blurTexture_, nullptr, nullptr);
                SDL_SetTextureAlphaMod(blurTexture_, 255);
                SetColor(renderer_, {207,225,234,static_cast<Uint8>(34.0f*overlayProgress)});
                SDL_RenderFillRect(renderer_, nullptr);
            }
        } else if (overlayProgress > .01f) {
            SetColor(renderer_, {203,220,230,static_cast<Uint8>(58.0f*overlayProgress)});
            SDL_RenderFillRect(renderer_, nullptr);
        }
        detailsBackdropReady_ = freezeBackdrop && offscreen && blurReady;
        gGlassBackdrop = offscreen && blurTexture_ && overlayProgress > .01f ? lensTexture_ : nullptr;
        if (detailsProgress_ > .01f) DrawDetails(accent, detailsProgress_);
        if (settingsProgress_ > .01f) DrawSettings(accent, settingsProgress_);
        if (InjectionDialogOpen()) DrawInjectionDialog(accent);
        if (WelcomeOpen()) DrawWelcome(accent);
        if (updateOpen_) DrawUpdate(accent);
        if (scanOpen_) DrawScan(accent);
        gGlassBackdrop = nullptr;
        if (videoOptions_.foreground) video_.Draw(kWidth,kHeight);
        if (!status_.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), statusUntil_)) status_.clear();
        if (!status_.empty()&&!scanOpen_&&!InjectionDialogOpen()&&!WelcomeOpen()&&!updateOpen_) DrawToast(accent);
        const bool captured = capturePath.empty() || SaveVisualFrame(renderer_, capturePath);
        SDL_RenderPresent(renderer_);
#ifndef UFLOW_HOST_PREVIEW
        // SDL waits for older GPU work, not necessarily the queued display flip.
        // Pace against the submitted flip rather than queueing another scan-buffer
        // copy while the display is still presenting it. Only rendered in foreground.
        GX2WaitForFlip();
#endif
        FlushSettingsSave(false);
        // Only upload a completed CPU decode here; storage and decompression
        // run on the artwork worker. Include upload time in frame statistics.
        if (!video_.Active()&&!scanOpen_&&!InjectionDialogOpen()) textures_.PumpOne();
        const double elapsedMs=1000.0*(SDL_GetPerformanceCounter()-performanceStart)/SDL_GetPerformanceFrequency();
        ++performanceFrames_; performanceTotalMs_+=elapsedMs;
        performanceMaxMs_=std::max(performanceMaxMs_,elapsedMs);
        return captured;
    }

private:
    void UpdateVideo(const GameEntry *selected) {
        if (selected != videoSelection_) {
            StopVideo(); videoSelection_ = selected;
            videoTrigger_.Focus(selected ? selected->absolutePath + "|" + selected->previewPath : "");
            videoTrigger_.Reset();
            videoOptions_ = {};
            videoConfigured_ = false;
            if (selected) if (const auto *source = VideoSourceFor(mediaSources_,*selected)) {
                videoOptions_ = source->video; videoConfigured_ = true;
            }
        }
        bool realLibrary = !preview_;
#ifdef UFLOW_HOST_PREVIEW
        realLibrary = realLibrary || videoTest_;
#endif
        const bool decoderAvailable = video_.Active() || video_.CanStart();
        const bool eligible = decoderAvailable && realLibrary && !videoInputActive_ && selected && videoConfigured_ && !selected->previewPath.empty() &&
            preferences_.usePreviews && !scanOpen_ && !updateOpen_ && !WelcomeOpen() && !InjectionDialogOpen() && !settings_ && !details_ && !sourceManager_ && !sourceEditor_ &&
            !directoryBrowser_ && detailsProgress_<=.01f && settingsProgress_<=.01f &&
            (!CoverInspectOpen() || !videoOptions_.foreground) &&
            !libraryFadePhase_;
        if (!eligible && (video_.Active()||videoPending_)) StopVideo();
        if (videoTrigger_.Tick(eligible,SDL_GetTicks(),videoOptions_.delayMs)) {
            videoPending_=true;
        }
        if(videoPending_)glassBackground_.Stop();
        if(videoPending_&&glassBackground_.Released()&&video_.CanStart()) {
            videoPending_=false;
            Trace(("video-start path="+selected->previewPath).c_str());
            music_.Pause(true);
            videoReportAt_=SDL_GetTicks(); videoReportStats_={}; videoReports_=0;
            if (!video_.Start(selected->previewPath,videoOptions_.volume)) {
                music_.Pause(false); SetStatus("VIDEO PLAYER COULD NOT START",6000);
            }
        }
        if (!video_.Active()) return;
        video_.Update();
        ReportVideo();
        if (const auto error = video_.TakeError(); !error.empty()) {
            Trace(("video-error " + error).c_str());
            videoTrigger_.Dismiss();
            StopVideo(); SetStatus(error,6000);
        }
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
        for (auto &entry : sideCases_) SDL_DestroyTexture(entry.texture);
        sideCases_.clear();
    }
    int SettingsRowCount() const {
        if(audioSettings_) return 4;
        if(settingsCategory_==4)return static_cast<int>(ControlAction::Count)+1;
        static constexpr int counts[] = {6, 0, 4, 6, 0, 0};
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
        const std::string selectedLibrary = CurrentLibraryId();
        for (std::size_t index = 0; index < games_.size(); ++index) {
            const auto &game = games_[index];
            if (!GameAvailableInNavigation(game,preferences_,libraries_)) continue;
            if (selectedLibrary == kAllLibraryId ||
                (selectedLibrary == kFavoritesLibraryId && game.favorite) ||
                (selectedLibrary == GameLibraryId(game))) visible_.push_back(index);
        }
        selected_ = visible_.empty() ? 0 : std::min(selected_, visible_.size() - 1);
    }
    void PrefetchArtwork() {
        if (visible_.empty()) return;
        const auto queueGame = [&](const GameEntry &game, bool selected) {
            textures_.Prefetch(game.fullCoverPath);
            if(game.fullCoverPath.empty() || (textures_.Known(game.fullCoverPath) && !textures_.HasTexture(game.fullCoverPath))) {
                textures_.Prefetch(game.coverPath);
                textures_.Prefetch(game.backCoverPath);
                textures_.Prefetch(game.spinePath);
                if(game.coverPath.empty() || game.backCoverPath.empty() || game.spinePath.empty())textures_.Prefetch(game.defaultSleevePath);
            }
            if (selected) {
                textures_.Prefetch(game.backgroundPath);
                if (preferences_.useLogos) textures_.Prefetch(game.logoPath);
            }
        };
        queueGame(games_[visible_[selected_]],true);
        // The centre cover is followed by the cards that can enter view next.
        for (int distance = 1; distance <= 3; ++distance) {
            const int before = static_cast<int>(selected_) - distance;
            const int after = static_cast<int>(selected_) + distance;
            if (before >= 0) queueGame(games_[visible_[before]],false);
            if (after < static_cast<int>(visible_.size())) queueGame(games_[visible_[after]],false);
        }
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
    void DrawBackground(Color accent, const GameEntry *game) {
        if (preferences_.skin == 1) {
            const bool themeVideo=glassBackground_.Draw(kWidth,kHeight);
            if(!themeVideo)FillVerticalGradient(renderer_, {0,0,kWidth,kHeight},
                                 {246,248,248,255},{214,225,229,255});
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            if (preferences_.useBackgrounds && game && !game->backgroundPath.empty()) {
                const TextureInfo background = textures_.Get(game->backgroundPath);
                if (background.texture) {
                    SDL_SetTextureAlphaMod(background.texture, 122);
                    SDL_Rect destination = {0,0,kWidth,kHeight};
                    SDL_RenderCopy(renderer_, background.texture, nullptr, &destination);
                    SDL_SetTextureAlphaMod(background.texture, 255);
                }
            }
            if(themeVideo)return;
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
            return;
        }
        SetColor(renderer_, Mix({5,10,20,255}, accent, .07f));
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        if(preferences_.useBackgrounds&&game&&!game->backgroundPath.empty()){
            const TextureInfo background=textures_.Get(game->backgroundPath);
            if(background.texture){
                SDL_SetTextureAlphaMod(background.texture,72);
                SDL_Rect destination={0,0,kWidth,kHeight};
                SDL_RenderCopy(renderer_,background.texture,nullptr,&destination);
                SDL_SetTextureAlphaMod(background.texture,255);
                SetColor(renderer_,{3,7,14,118}); SDL_RenderFillRect(renderer_,nullptr);
            }
        }
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
    }
    void DrawHeader(Color accent) {
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
            DrawText(renderer_, "GLASSFLOW", 165, 52, 1, {104,117,127,255});
            DrawGlassSurface(renderer_, {398,31,750,52}, 19, accent, 42, false);
            drawLibraryRail(408,730,42,true);
            DrawGlassSurface(renderer_, {1184,38,40,40}, 20, accent, 105, false);
            DrawText(renderer_, std::to_string(visible_.size()), 1205, 49, 1, {56,78,86,255}, true);
            return;
        }
        FillRoundedRect(renderer_, {38,26,1204,74}, 22, {5,10,20,190});
        DrawText(renderer_, "UFLOW", 66, 46, 4, {245,249,255,255});
        DrawText(renderer_, "LIBRARY", 198, 55, 2, accent);
        FillRoundedRect(renderer_, {414,38,740,48}, 17, {255,255,255,8});
        drawLibraryRail(424,720,48,false);
        FillRoundedRect(renderer_, {1187,46,28,28}, 14, accent);
        DrawText(renderer_, std::to_string(visible_.size()), 1173, 82, 1, {170,181,199,255}, true);
    }
    void DrawEmpty(Color accent) {
        DrawText(renderer_, CurrentLibraryId() == kFavoritesLibraryId ? "NO FAVORITES YET" : "NO GAMES FOUND", 640, 300, 5,
                 preferences_.skin==1?Color{54,64,72,255}:Color{235,241,250,255}, true);
        DrawText(renderer_, CurrentLibraryId() == kFavoritesLibraryId ? favoriteHint_ : "ADD DIRECTORIES IN SETTINGS > LIBRARY",
                 640, 365, 2, preferences_.skin==1?Color{105,118,128,255}:accent, true);
    }
    void DrawCover(const GameEntry &game, SDL_Rect rect, bool selected, Color accent) {
        const auto profile=CaseProfileForGame(game,libraries_);
        const float scale=std::min(rect.w/std::max(135.f,profile.widthMm),rect.h/std::max(190.f,profile.heightMm));
        const int width=static_cast<int>(profile.widthMm*scale),height=static_cast<int>(profile.heightMm*scale);
        rect.x+=(rect.w-width)/2;rect.y+=(rect.h-height)/2;rect.w=width;rect.h=height;
        SDL_Rect shadow = {rect.x+12,rect.y+15,rect.w,rect.h};
        FillRoundedRect(renderer_, shadow, 18, {0,0,0,105});
        FillRoundedRect(renderer_, rect, 18, selected ? Color{235,241,250,255} : Color{27,37,54,255});
        const int border = selected ? 5 : 2;
        SDL_Rect inner = {rect.x+border,rect.y+border,rect.w-border*2,rect.h-border*2};
        FillRoundedRect(renderer_, inner, 14, selected ? Color{10,18,31,255} : Color{15,24,39,255});
        const float total=profile.widthMm*2+profile.thicknessMm;
        const auto front=CoverFaces::Resolve(game,CoverFaces::Face::Front,profile.widthMm/total,
            (profile.widthMm+profile.thicknessMm)/total,[&](const auto &path){return textures_.Get(path).texture!=nullptr;});
        const TextureInfo cover = textures_.Get(front.path);
        if (cover.texture) {
            const int padding = selected ? 16 : 11;
            SDL_Rect area = {inner.x+padding,inner.y+padding,inner.w-padding*2,inner.h-padding*2};
            SDL_Rect crop{static_cast<int>(front.u0*cover.width),0,static_cast<int>((front.u1-front.u0)*cover.width),cover.height};
            const float sourceRatio = static_cast<float>(crop.w) / cover.height;
            const float targetRatio = static_cast<float>(area.w) / area.h;
            SDL_Rect destination = area;
            if (sourceRatio > targetRatio) { destination.h = static_cast<int>(area.w/sourceRatio); destination.y += (area.h-destination.h)/2; }
            else { destination.w = static_cast<int>(area.h*sourceRatio); destination.x += (area.w-destination.w)/2; }
            SDL_RenderCopy(renderer_, cover.texture, &crop, &destination);
        } else {
            const Color platformAccent = AccentFor(game.platform, preferences_.theme);
            FillRoundedRect(renderer_, {inner.x+12,inner.y+12,inner.w-24,inner.h-24}, 12,
                            {platformAccent.r,platformAccent.g,platformAccent.b,selected ? Uint8(160) : Uint8(92)});
            DrawText(renderer_, PlatformName(game.platform), rect.x+rect.w/2, rect.y+rect.h/2-45,
                     selected ? 3 : 2, {255,255,255,255}, true);
            DrawText(renderer_, Ellipsize(game.name, selected ? 18 : 14), rect.x+rect.w/2, rect.y+rect.h/2+12,
                     selected ? 2 : 1, {255,255,255,255}, true);
        }
        if (selected) {
            SetColor(renderer_, {accent.r,accent.g,accent.b,220});
            for (int line=0; line<4; ++line) { SDL_Rect outline={rect.x-line,rect.y-line,rect.w+line*2,rect.h+line*2}; SDL_RenderDrawRect(renderer_,&outline); }
        }
        if (game.favorite) DrawText(renderer_, "*", rect.x+rect.w-(selected?38:26), rect.y+13, selected?4:3, {255,210,66,255});
    }
    void DrawBox(const GameEntry &game, SDL_Rect rect, float relative, bool selected, Color accent,
                 float turnScale = .82f, float pitch = 0.0f, float roll = 0.0f,
                 bool absoluteRotation = false) {
        const auto profile = CaseProfileForGame(game,libraries_);
        // Render each model at 2x using the smallest preallocated target that fits.
        const int half = static_cast<int>(std::ceil(rect.h * .65f)) + 4;
        SDL_Rect bounds{rect.x + rect.w/2 - half, rect.y + rect.h/2 - half,
                        half*2, half + static_cast<int>(std::ceil(rect.h*.90f)) + 4};
        // Offscreen cards do not need transforms, texture decoding or GPU work.
        if (bounds.x >= kWidth || bounds.x + bounds.w <= 0) return;
        // Away from the centre, a case only translates. Keep its AA result and
        // composite it directly: no repeated mesh sorting, target switch or clear.
        // Do not cache a temporary placeholder while its cover is still queued.
        // A known decode failure may be cached, but a pending decode must render
        // directly once more so the completed artwork replaces the placeholder.
        const bool artworkResolved = textures_.HasTexture(game.fullCoverPath) ||
            (textures_.Known(game.fullCoverPath) && textures_.Known(game.coverPath) &&
             textures_.Known(game.backCoverPath) && textures_.Known(game.spinePath) && textures_.Known(game.defaultSleevePath));
        bool cacheable = !selected && !absoluteRotation && std::abs(relative) >= 1.f;
        std::string cacheKey;
        if (cacheable) {
            relative = relative < 0 ? -1.f : 1.f;
            cacheKey = game.fullCoverPath + "\n" + game.coverPath + "\n" + game.name + "\n" +
                game.backCoverPath + "\n" + game.spinePath + "\n" + game.defaultSleevePath + "\n" +
                std::to_string(game.defaultBackEnd) + ":" + std::to_string(game.defaultFrontStart) + ":" +
                std::to_string(static_cast<int>(game.platform)) + ":" + std::to_string(game.favorite) + ":" +
                std::to_string(rect.w) + ":" + std::to_string(rect.h) + ":" + std::to_string(relative) + ":" +
                std::to_string(turnScale) + ":" + std::to_string(pitch) + ":" + std::to_string(roll) + ":" +
                std::to_string(profile.widthMm) + ":" + std::to_string(profile.heightMm) + ":" +
                std::to_string(profile.thicknessMm) + ":" +
                std::to_string(preferences_.skin) + ":" + std::to_string(preferences_.reflections) + ":" +
                std::to_string(accent.r) + ":" + std::to_string(accent.g) + ":" + std::to_string(accent.b);
            for (auto &entry : sideCases_) if (entry.key == cacheKey) {
                entry.use = ++sideCaseUse_;
                SDL_RenderCopy(renderer_,entry.texture,nullptr,&bounds);
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
            DrawBoxDirect(game,rect,relative,selected,accent,turnScale,pitch,roll,absoluteRotation,profile);
            return;
        }
        SDL_Texture *cachedTarget = nullptr;
        if (cacheable) {
            if (sideCases_.size() >= 16) {
                auto oldest = std::min_element(sideCases_.begin(),sideCases_.end(),
                    [](const auto &a,const auto &b){return a.use < b.use;});
                SDL_DestroyTexture(oldest->texture); sideCases_.erase(oldest);
            }
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
            DrawBoxDirect(game,rect,relative,selected,accent,turnScale,pitch,roll,absoluteRotation,profile);
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
        DrawBoxDirect(game,local,relative,selected,accent,turnScale,pitch,roll,absoluteRotation,profile);
        SDL_SetRenderTarget(renderer_,target);
        SDL_RenderSetLogicalSize(renderer_,logicalW,logicalH);
        SDL_RenderSetScale(renderer_,scaleX,scaleY); SDL_RenderSetViewport(renderer_,&viewport);
        SDL_RenderSetClipRect(renderer_,clipped?&clip:nullptr);
        const SDL_Rect source{0,0,bounds.w*2,bounds.h*2};
        SDL_RenderCopy(renderer_,caseTexture,&source,&bounds);
        if (cachedTarget) sideCases_.push_back({cacheKey,cachedTarget,++sideCaseUse_});
    }
    void DrawBoxDirect(const GameEntry &game, SDL_Rect rect, float relative, bool selected, Color accent,
                       float turnScale, float pitch, float roll, bool absoluteRotation,
                       CaseProfile profile) {
#ifdef UFLOW_HOST_PREVIEW
        ++caseDraws_;
#endif
        const float sleeveWidth = profile.widthMm * 2.f + profile.thicknessMm;
        const float backEnd = profile.widthMm / sleeveWidth;
        const float frontStart = (profile.widthMm + profile.thicknessMm) / sleeveWidth;
        const auto selectFace=[&](CoverFaces::Face face) {
            if(!game.fullCoverPath.empty() && !textures_.Known(game.fullCoverPath)) {
                textures_.Prefetch(game.fullCoverPath);
                return CoverFaces::Selection{}; // Pending is not a failed decode; don't queue unused faces.
            }
            return CoverFaces::Resolve(game,face,backEnd,frontStart,[&](const auto &path){return textures_.Get(path).texture!=nullptr;});
        };
        const auto front=selectFace(CoverFaces::Face::Front);
        const TextureInfo artwork=textures_.Get(front.path);
        const float frontU=front.u0;

        struct Transformed { SDL_FPoint point; float depth; };
        struct Face { std::uint16_t a, b, c; float depth; };
        // Share a physical reference across profiles so smaller cases stay smaller.
        constexpr float referenceHeightMm = 190.f;
        const float caseWidth = profile.widthMm / referenceHeightMm;
        const float caseHeight = profile.heightMm / referenceHeightMm;
        const float caseDepth = profile.thicknessMm / referenceHeightMm;
        const float turn = std::clamp(relative, -1.0f, 1.0f);
        const float idle = selected ? .055f + (preview_ ? 0.f : std::sin(SDL_GetTicks() / 1250.0f) * .018f) : 0.0f;
        const float yaw = absoluteRotation ? turnScale : -turn * turnScale + idle;
        const float yawSine = std::sin(yaw), yawCosine = std::cos(yaw);
        const float pitchSine = std::sin(pitch), pitchCosine = std::cos(pitch);
        const float rollSine = std::sin(roll), rollCosine = std::cos(roll);
        const float pixelScale = std::min(rect.w / std::max(135.f / referenceHeightMm,caseWidth),
                                         rect.h / (1.023f * std::max(1.f,caseHeight)));
        const float centerX = rect.x + rect.w * .5f;
        const float centerY = rect.y + rect.h * .5f;
        constexpr float camera = 2.45f;
        const auto transform = [&](float x, float y, float z) {
            x *= caseWidth;
            y *= caseHeight;
            z *= caseDepth;
            const float yawX = x * yawCosine + z * yawSine;
            const float yawZ = -x * yawSine + z * yawCosine;
            const float pitchY = y * pitchCosine - yawZ * pitchSine;
            const float pitchZ = y * pitchSine + yawZ * pitchCosine;
            const float rollX = yawX * rollCosine - pitchY * rollSine;
            const float rollY = yawX * rollSine + pitchY * rollCosine;
            const float perspective = camera / (camera - pitchZ);
            return Transformed{{centerX + rollX * pixelScale * perspective,
                                centerY - rollY * pixelScale * perspective}, pitchZ};
        };

        const auto topLeft = transform(-.478f, .482f, .515f);
        const auto topRight = transform(.478f, .482f, .515f);
        const auto bottomRight = transform(.478f, -.482f, .515f);
        const auto bottomLeft = transform(-.478f, -.482f, .515f);
        const bool frontFacing = yawCosine * pitchCosine > .015f;

        const float shadowWidth = rect.w * (selected ? 1.02f : .82f);
        DrawSoftEllipseShadow(renderer_,
                              {rect.x + rect.w*.5f-shadowWidth*.5f,
                               rect.y + rect.h-2.0f,shadowWidth,selected?24.0f:15.0f},
                              selected ? 104 : 58);

        if (frontFacing && artwork.texture && preferences_.skin == 1 && preferences_.reflections) {
            const auto fit=CoverFaces::Contain(artwork.width*(front.u1-front.u0)/std::max(1,artwork.height),profile.widthMm/profile.heightMm);
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
            const auto drawSide = [&](std::array<Transformed,4> corners,float u0,float u1,Uint8 shade,SDL_Texture *texture) {
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
                for(int i=0;i<count;++i)side[i].color={shade,shade,shade,255};
                SDL_RenderGeometry(renderer_,texture,side.data(),count,indices.data(),indexCount);
            };
            const auto rails=CaseSleeve::Rails(profile.widthMm,profile.thicknessMm);
            for(std::size_t panel=0;panel+1<rails.size();++panel) {
                const auto &left=rails[panel],&right=rails[panel+1];
                const auto face=panel==0?CoverFaces::Face::Back:panel==4?CoverFaces::Face::Front:CoverFaces::Face::Spine;
                const auto selectedFace=selectFace(face);
                const auto texture=textures_.Get(selectedFace.path);
                if(!texture.texture)continue;
                const float start=panel==0?0.f:panel==4?frontStart:backEnd;
                const float end=panel==0?backEnd:panel==4?1.f:frontStart;
                constexpr float h=CaseSleeve::kHalfHeight;
                const float aspect=(face==CoverFaces::Face::Spine?profile.thicknessMm:profile.widthMm)/profile.heightMm;
                const auto fit=CoverFaces::Contain(texture.width*(selectedFace.u1-selectedFace.u0)/std::max(1,texture.height),aspect);
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
                drawSide({point(a,fit.y0),point(b,fit.y0),point(b,fit.y1),point(a,fit.y1)},uv(a),uv(b),brightness,texture.texture);
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
        if (game.favorite) DrawText(renderer_, "*", rect.x + rect.w - 32, rect.y + 8, 3, {255,210,66,255});
    }
    void DrawCoverInspect(Color accent) {
        const GameEntry *game=Selected();
        if(!game)return;
        const CoverInspectPose pose=CoverInspect(coverInspectProgress_,coverInspectBackProgress_);
        const auto interpolate=[&](int from,int to){return static_cast<int>(from+(to-from)*pose.travel);};
        SetColor(renderer_,{20,31,39,static_cast<Uint8>(72.f*pose.travel)});
        SDL_Rect focusArea{0,96,kWidth,532};SDL_RenderFillRect(renderer_,&focusArea);
        const SDL_Rect rect{interpolate(530,400),interpolate(155,100),
                            interpolate(220,480),interpolate(314,490)};
        DrawBox(*game,rect,0.f,true,accent,pose.yaw,pose.pitch,pose.roll,true);
        const Uint8 panelAlpha=static_cast<Uint8>(210.f*pose.travel);
        const int hintWidth = std::max(580, TextWidth(coverInspectHint_,1) + 32);
        FillRoundedRect(renderer_,{640-hintWidth/2,590,hintWidth,36},16,
                        preferences_.skin==1?Color{236,246,250,panelAlpha}:Color{7,15,27,panelAlpha});
        DrawText(renderer_,coverInspectHint_,640,601,1,
                 preferences_.skin==1?Color{48,64,73,static_cast<Uint8>(255.f*pose.travel)}:
                                      Color{235,242,250,static_cast<Uint8>(255.f*pose.travel)},true);
    }
    void DrawCarousel(Color accent) {
        struct Card { int index; float relative; };
        std::vector<Card> cards;
        const int center = static_cast<int>(std::round(visualSelected_));
        for (int index = center - 5; index <= center + 5; ++index) {
            if (index >= 0 && index < static_cast<int>(visible_.size()))
                cards.push_back({index, static_cast<float>(index) - visualSelected_});
        }
        std::sort(cards.begin(), cards.end(), [](const Card &left, const Card &right) {
            return std::abs(left.relative) > std::abs(right.relative);
        });
        const float spacingValues[] = {154.0f, 184.0f, 216.0f};
        float spacing = spacingValues[preferences_.coverSpacing];
        if (preferences_.coverLayout == 1) spacing *= .86f;
        else if (preferences_.coverLayout == 2) spacing *= 1.05f;
        else if (preferences_.coverLayout == 3) spacing *= .48f;
        for (const Card &card : cards) {
            if (card.index == static_cast<int>(selected_) && (detailsProgress_ > .015f || coverInspectProgress_ > .015f)) continue;
            const GameEntry &game = games_[visible_[card.index]];
            const float distance = std::abs(card.relative);
            float focus = std::max(0.0f, 1.0f - distance);
            focus = focus * focus * (3.0f - 2.0f * focus);
            float sideWidth = 126.0f;
            float sideHeight = 180.0f;
            const float focusWidth = 220.0f;
            const float focusHeight = 314.0f;
            float turnScale = .82f;
            if (preferences_.coverLayout == 1) {
                sideWidth = 112.0f; sideHeight = 160.0f; turnScale = .55f;
            } else if (preferences_.coverLayout == 2) {
                sideWidth = 135.0f; sideHeight = 193.0f; turnScale = .38f;
            } else if (preferences_.coverLayout == 3) {
                sideWidth = 150.0f; sideHeight = 214.0f; turnScale = .28f;
            }
            if (preferences_.skin == 1) turnScale = std::max(turnScale,.52f);
            const int width = static_cast<int>(sideWidth + (focusWidth - sideWidth) * focus);
            const int height = static_cast<int>(sideHeight + (focusHeight - sideHeight) * focus);
            float depthPush = std::min(4.0f, distance) * 9.0f;
            if (preferences_.coverLayout == 1) depthPush = std::pow(std::min(4.0f, distance), 1.35f) * 18.0f;
            else if (preferences_.coverLayout == 2) depthPush = std::min(4.0f, distance) * 3.0f;
            else if (preferences_.coverLayout == 3) depthPush = std::min(4.0f, distance) * 5.0f;
            const int x = static_cast<int>(640.0f + tabSlide_ + card.relative * spacing - width / 2.0f);
            const int y = static_cast<int>(312.0f - height / 2.0f + depthPush);
            if(preferences_.useBoxArt)DrawBox(game, {x, y, width, height}, card.relative,
                    card.index == static_cast<int>(selected_) && focus > .78f, accent, turnScale);
            else DrawCover(game,{x,y,width,height},card.index==static_cast<int>(selected_)&&focus>.78f,accent);
        }
        const auto &game = games_[visible_[selected_]];
        if (video_.Ready() && !videoOptions_.foreground) {
            if(preferences_.skin==1) DrawGlassSurface(renderer_,{242,511,796,88},18,accent,92,true);
            else FillRoundedRect(renderer_,{242,511,796,88},18,{5,10,20,205});
        }
        FillRoundedRect(renderer_, {250,505,780,3}, 1, {accent.r,accent.g,accent.b,preferences_.skin==1?Uint8(72):Uint8(105)});
        const TextureInfo logo=preferences_.useLogos?textures_.Get(game.logoPath):TextureInfo{};
        if(logo.texture){
            const float ratio=static_cast<float>(logo.width)/std::max(1,logo.height);
            SDL_Rect logoRect={490,516,300,54};
            if(ratio>static_cast<float>(logoRect.w)/logoRect.h){logoRect.h=static_cast<int>(logoRect.w/ratio);logoRect.y+=(54-logoRect.h)/2;}
            else{logoRect.w=static_cast<int>(logoRect.h*ratio);logoRect.x+=(300-logoRect.w)/2;}
            SDL_RenderCopy(renderer_,logo.texture,nullptr,&logoRect);
        }else DrawText(renderer_, Ellipsize(game.name,50), 640, 525, 3,
                       preferences_.skin==1?Color{48,58,66,255}:Color{246,249,255,255}, true);
        std::string summary = std::string(PlatformName(game.platform)) + "  /  " +
                              (game.installed ? (game.storage.empty() ? "INSTALLED" : game.storage) : "FAT32") +
                              "  /  " + std::to_string(selected_+1) + " OF " + std::to_string(visible_.size());
        DrawText(renderer_, Ellipsize(summary,75), 640, 570, 2,
                 preferences_.skin==1?Color{95,108,118,255}:accent, true);
    }
    const char *ActionButton(ControlAction action) const {
        return ControlBindingName(preferences_.bindings[static_cast<int>(action)]);
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
            ControlAction::Favorite, ControlAction::PreviousLibrary, ControlAction::Settings};
        constexpr const char *labels[] = {"LAUNCH", "DETAILS", "FAVORITE", "FILTER", "SETTINGS"};
        constexpr int positions[] = {58,238,428,638,918,1148};
        for (std::size_t i = 0; i < footerHints_.size(); ++i) {
            auto &hint = footerHints_[i];
            hint.button = ActionButton(actions[i]);
            if (actions[i] == ControlAction::PreviousLibrary)
                hint.button += std::string("/") + ActionButton(ControlAction::NextLibrary);
            hint.action = labels[i]; hint.x = positions[i]; hint.width = positions[i+1] - positions[i] - 16;
            hint.buttonWidth = TextWidth(hint.button,2); hint.gap = TextWidth("  ",2);
            hint.stacked = hint.buttonWidth + hint.gap + TextWidth(hint.action,2) > hint.width;
        }
        favoriteHint_ = std::string("MARK A GAME WITH ") + ActionButton(ControlAction::Favorite);
        coverInspectHint_ = std::string("HOLD ") + ActionButton(ControlAction::ZoomFront) +
            "  FRONT    HOLD " + ActionButton(ControlAction::ZoomBack) + "  BACK    B  RETURN";
    }
    void DrawFooter(Color accent) {
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
        DrawText(renderer_,"HOME",1148,glass ? 653 : 648,2,glass ? Color{68,78,87,255} : accent);
    }
    void DrawPanelBase(Color accent, int x, int width, float progress) {
        if (preferences_.skin == 1) {
            DrawGlassSurface(renderer_,{x,92,width,536},30,accent,102,true);
            return;
        }
        SetColor(renderer_,{0,0,0,static_cast<Uint8>(190.0f*progress)}); SDL_RenderFillRect(renderer_,nullptr);
        FillRoundedRect(renderer_,{x,92,width,536},28,{10,17,29,250});
        FillRoundedRect(renderer_,{x,92,8,536},4,accent);
    }
    void DrawGlassDetails(const GameEntry &game, Color accent, float progress) {
        const CoverTransitionPose pose = CoverTransition(progress);
        const float eased = pose.travel;

        const auto interpolate = [eased](int from, int to) {
            return static_cast<int>(from + (to - from) * eased);
        };
        const SDL_Rect movingCase = {interpolate(530,48), interpolate(155,98),
                                     interpolate(220,310), interpolate(314,465)};
        DrawBox(game,movingCase,-1.0f,true,accent,pose.yaw,pose.pitch,pose.roll,true);

        const int shift = static_cast<int>((1.0f - eased) * 760.0f);
        const int panelX = 382 + shift;
        DrawGlassSurface(renderer_, {panelX,102,838,510}, 30, accent, 96, true);
        FillRoundedRect(renderer_,{panelX+113,120,700,470},20,{244,248,250,200});
        SetColor(renderer_, {255,255,255,105}); SDL_Rect divider={panelX+99,126,1,450}; SDL_RenderFillRect(renderer_,&divider);

        const auto action = [&](int row, const char *symbol, bool active) {
            const int y = 133 + row * 70;
            DrawDetailGlassButton(renderer_,{panelX+20,y,60,58},active);
            if(active) FillRoundedRect(renderer_,{panelX+10,y+17,3,24},1,accent);
            DrawText(renderer_, symbol, panelX+50, y+20, 1,
                     active ? Color{20,74,94,255} : Color{65,78,87,255}, true);
        };
        action(0,game.favorite?"FAV *":"FAV",detailsRow_==0);
        action(1,"PLAY",detailsRow_==1);
        action(2,"GAME",detailsRow_==2);
        action(3,"PATH",detailsRow_==3);
        action(4,"MEDIA",detailsRow_==4);
        action(5,"INFO",detailsRow_==5);

        const int contentX = panelX + 130;
        const TextureInfo logo = preferences_.useLogos ? textures_.Get(game.logoPath) : TextureInfo{};
        if (logo.texture) {
            const float ratio = static_cast<float>(logo.width) / std::max(1,logo.height);
            SDL_Rect logoRect={contentX,126,520,72};
            if(ratio>static_cast<float>(logoRect.w)/logoRect.h){logoRect.h=static_cast<int>(logoRect.w/ratio);logoRect.y+=(72-logoRect.h)/2;}
            else logoRect.w=static_cast<int>(logoRect.h*ratio);
            SDL_RenderCopy(renderer_,logo.texture,nullptr,&logoRect);
        } else DrawText(renderer_,Ellipsize(game.name,36),contentX,139,4,{48,58,66,255});
        const std::string creator=!game.developer.empty()?game.developer:game.publisher;
        DrawText(renderer_,creator.empty()?PlatformName(game.platform):Ellipsize(creator,52),contentX,204,2,{105,117,127,255});
        DrawDetailsContent(game,contentX,accent);

        const auto chip=[&](int offset,const std::string &text){
            DrawGlassSurface(renderer_,{contentX+offset,494,145,38},15,accent,68,false);
            DrawText(renderer_,Ellipsize(text,16),contentX+offset+72,504,1,{67,79,88,255},true);
        };
        chip(0,PlatformName(game.platform));
        chip(157,game.genre.empty()?"GENRE UNKNOWN":game.genre);
        chip(314,game.releaseYear.empty()?"YEAR ----":game.releaseYear);
        chip(471,game.players.empty()?"PLAYERS --":"PLAYERS "+game.players);

        const auto footer=[&](int offset,int width,const std::string &text){
            DrawDetailGlassButton(renderer_,{contentX+offset,550,width,40},false);
            DrawText(renderer_,text,contentX+offset+width/2,563,1,{43,63,74,255},true);
        };
        footer(0,288,std::string("A  ")+DetailsAction());
        footer(294,150,"B  BACK"); footer(456,210,"+  SETTINGS");
    }
    void DrawDetailsContent(const GameEntry &game,int x,Color accent) {
        const Color ink={55,67,77,255};
        if(detailsRow_==4) {
            const std::string paths[]={game.coverPath,game.backCoverPath,game.spinePath,game.fullCoverPath,game.defaultSleevePath,game.backgroundPath,game.logoPath};
            const char *names[]={"Front cover","Back cover","Spine","Full sleeve","Default sleeve","Background","Logo"};
            const int index=((detailsPage_%7)+7)%7;
            DrawText(renderer_,names[index],x,242,3,ink);
            const TextureInfo art=textures_.Get(paths[index]);
            if(art.texture && art.width>0 && art.height>0) {
                const float scale=std::min(620.f/art.width,174.f/art.height);
                SDL_Rect r={x+(620-static_cast<int>(art.width*scale))/2,282,
                    static_cast<int>(art.width*scale),static_cast<int>(art.height*scale)};
                SDL_RenderCopy(renderer_,art.texture,nullptr,&r);
            } else DrawText(renderer_,"No artwork assigned for this media type.",x,315,2,ink);
            DrawText(renderer_,"LEFT / RIGHT  ARTWORK  "+std::to_string(index+1)+" / 7",x,461,1,ink);
            if(!game.previewPath.empty()) DrawText(renderer_,"Video preview available in the library",x,478,1,ink);
            return;
        }
        const char *heading=detailsRow_==0?"Favorites":detailsRow_==1?"Launch game":detailsRow_==3?"Content source":detailsRow_==5?"Title information":"Synopsis";
        std::string body=game.description.empty()?"No synopsis available. Add a description through this title's metadata file.":game.description;
        if(detailsRow_==1) {
            std::string launcherId=EffectiveLauncher(game,gameLaunchers_,sources_,libraries_);
            if(launcherId.empty() && game.platform!=Platform::Files)
                launcherId=game.platform==Platform::WiiU?"native":game.launcher==GameLauncher::WiiFlow?"wiiflow":"usbloadergx";
            body="Press A to launch this title.\n\nConfigured launcher: "+
                (launcherId.empty()?"Not selected":GameLauncherLabel(game,launcherId))+
                "\nContent: "+(game.installed?"Installed title":game.platform==Platform::WiiU?"Raw Wii U game":"Game file")+
                "\n\nSelect GAME for the synopsis.";
        }
        if(detailsRow_==0) body=game.favorite?"This title is in your favorites. Press A to remove it.":"Press A to add this title to your favorites.";
        if(detailsRow_==2 && (detailsPanelRow_!=DetailsGameRows()-1||!detailsPanelFocus_)) {
            const auto found=gameLaunchers_.find(LauncherGameKey(game));
            DrawText(renderer_,"Game launcher",x,242,3,ink);
            std::vector<std::string> rows={found==gameLaunchers_.end()?"USE DEFAULT":GameLauncherLabel(game,found->second)};
            if(DetailsGameRows()==3)rows.push_back(SavedInjection(game)?"Re-inject ROM":"Inject ROM");
            rows.push_back("Synopsis");
            for(int i=0;i<static_cast<int>(rows.size());++i){DrawDetailGlassButton(renderer_,{x,283+i*56,620,50},detailsPanelFocus_&&detailsPanelRow_==i);
                DrawText(renderer_,rows[i],x+18,298+i*56,2,ink);}
            DrawText(renderer_,detailsLauncherEditing_?"LEFT / RIGHT CHANGE   A / B DONE":"LEFT / RIGHT FOCUS   UP / DOWN SELECT   A ACTIVATE",x,461,1,ink);
            DrawText(renderer_,("Effective: "+GameLauncherLabel(game,EffectiveLauncher(game,gameLaunchers_,sources_,libraries_))).substr(0,60),x,480,1,ink);
            return;
        }
        if(detailsRow_==3) body="Location: "+game.absolutePath+"\nStorage: "+(game.storage.empty()?"Not reported":game.storage)+
            "\nContent type: "+game.libraryId+"\nFormat: "+(game.installed?"Installed title":"File / raw game")+"\nPress A to manage library directories.";
        if(detailsRow_==5) body="Title ID: "+(game.titleId.empty()?"Not available":game.titleId)+
            "\nRegion: "+(game.region.empty()?"Unknown":game.region)+"\nDeveloper: "+(game.developer.empty()?"Unknown":game.developer)+
            "\nPublisher: "+(game.publisher.empty()?"Unknown":game.publisher)+"\nRating: "+(game.rating.empty()?"Not rated":game.rating)+
            "\nPress A to manage launchers.";
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
        DrawText(renderer_,pages>1?"LEFT / RIGHT  PAGE "+std::to_string(page+1)+" / "+std::to_string(pages):"UP / DOWN  SELECT ACTION",x,470,1,accent);
    }
    void DrawDetails(Color accent, float progress) {
        const GameEntry *game=Selected(); if(!game)return;
        if(preferences_.skin==1) { DrawGlassDetails(*game,accent,progress); return; }
        const int x=static_cast<int>(80+(1.f-progress)*1240.f);
        DrawPanelBase(accent,x,1120,progress);
        DrawText(renderer_,"TITLE DETAILS",x+40,120,2,{142,157,179,255});
        DrawBox(*game,{x+30,176,250,357},-.16f,true,accent,.38f);
        static const char *labels[]={"FAV","PLAY","GAME","SOURCE","MEDIA","INFO"};
        for(int i=0;i<6;++i) {
            FillRoundedRect(renderer_,{x+292,133+i*70,76,58},8,i==detailsRow_?accent:Color{28,42,60,255});
            DrawText(renderer_,labels[i],x+330,153+i*70,1,{255,255,255,255},true);
        }
        FillRoundedRect(renderer_,{x+385,120,700,470},16,{244,248,250,255});
        DrawText(renderer_,Ellipsize(game->name,40),x+410,155,3,{40,57,70,255});
        DrawDetailsContent(*game,x+410,accent);
        DrawText(renderer_,std::string("A  ")+DetailsAction(),x+410,555,1,accent);
        DrawText(renderer_,"B  BACK    +  SETTINGS",x+760,555,1,accent);
    }
    void DrawSettings(Color accent, float progress) {
        static constexpr const char *categories[]={"GENERAL","LIBRARY","MEDIA","DISPLAY","CONTROLS","LAUNCHERS"};
        const int x=static_cast<int>(70+(1.0f-progress)*1260.0f);
        DrawPanelBase(accent,x,1140,progress);
        const bool glass=preferences_.skin==1;
        // A frosted reading layer keeps dark cover art from swallowing menu text.
        // Leave the outer glass bevel exposed; the backdrop remains softly visible.
        if (glass) FillRoundedRect(renderer_,{x+18,110,1104,500},22,{244,248,250,218});
        DrawText(renderer_,"SETTINGS",x+48,126,4,glass?Color{48,58,66,255}:Color{248,250,255,255});
        DrawFittedText(renderer_,audioSettings_?"GENERAL / AUDIO":"UFLOW CONTROL CENTER",x+760,139,330,2,glass?Color{70,84,95,255}:Color{116,133,158,255},true);
        if(glass) DrawGlassSurface(renderer_,{x+38,182,240,390},20,accent,72,false);
        else FillRoundedRect(renderer_,{x+38,182,240,390},18,{5,11,21,220});
        for(int index=0;index<6;++index){
            const int y=208+index*61;
            if(index==settingsCategory_){
                if(glass) {
                    DrawGlassSurface(renderer_,{x+52,y-16,212,46},14,accent,88,true);
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
        const int rowPitch=settingsCategory_==3 || (settingsCategory_==0 && !audioSettings_)?62:76;
        const auto drawRow=[&](int row,const std::string &label,const std::string &value){
            const int y=205+row*rowPitch;
            if(row==settingsRow_ && !settingsSidebar_){
                if(glass) DrawGlassSurface(renderer_,{contentX-18,y-18,760,57},14,accent,74,true);
                else FillRoundedRect(renderer_,{contentX-18,y-18,760,57},14,{accent.r,accent.g,accent.b,42});
            }
            DrawText(renderer_,label,contentX,y,2,row==settingsRow_?(glass?Color{45,56,64,255}:Color{255,255,255,255}):
                                                                (glass?Color{65,79,90,255}:Color{188,199,216,255}));
            const Color valueColor=glass?Color{30,96,119,255}:(row==settingsRow_?accent:Color{126,142,166,255});
            if(settingsEditing_ && row==settingsRow_ && !settingsSidebar_)
                FillRoundedRect(renderer_,{contentX+468,y-9,268,36},10,{accent.r,accent.g,accent.b,95});
            if(SettingsSelectorRow(row)) DrawText(renderer_,"<",contentX+480,y,2,valueColor);
            DrawFittedText(renderer_,value,contentX+505,y,192,2,valueColor,true);
            if(SettingsSelectorRow(row)) DrawText(renderer_,">",contentX+718,y,2,valueColor);
        };
        if(settingsCategory_==0 && !audioSettings_){
            static constexpr const char *skins[]={"PIXEL DECK","GLASSFLOW"};
            drawRow(0,"SKIN",skins[preferences_.skin]);
            drawRow(1,"START VIEW",LibraryLabel(CurrentLibraryId()));
            drawRow(2,"WRAP NAVIGATION",OnOff(preferences_.wrapNavigation));
            drawRow(3,"SHOW INSTALLED WII U",OnOff(preferences_.showInstalled));
            drawRow(4,"AUDIO","A  OPEN");
            drawRow(5,"START UFLOW ON BOOT",OnOff(preferences_.startOnBoot));
        }else if(settingsCategory_==2){
            drawRow(0,"3D BOX ART",OnOff(preferences_.useBoxArt));
            drawRow(1,"BACKGROUNDS",OnOff(preferences_.useBackgrounds));
            drawRow(2,"TITLE LOGOS",OnOff(preferences_.useLogos));
            drawRow(3,"GAMEPLAY PREVIEWS",OnOff(preferences_.usePreviews));
            DrawText(renderer_,"B  BACK TO MEDIA DIRECTORIES",contentX,542,1,{65,79,90,255});
        }else if(settingsCategory_==3){
            static constexpr const char *layouts[]={"CLASSIC FLOW","CAROUSEL","FLAT ROW","STACKED"};
            static constexpr const char *speeds[]={"RELAXED","SMOOTH","FAST"};
            static constexpr const char *spacing[]={"COMPACT","BALANCED","WIDE"};
            static constexpr const char *themes[]={"PLATFORM","CYAN","VIOLET","AMBER"};
            drawRow(0,"COVER LAYOUT",layouts[preferences_.coverLayout]);
            drawRow(1,"ANIMATION",speeds[preferences_.animationSpeed]);
            drawRow(2,"COVER SPACING",spacing[preferences_.coverSpacing]);
            drawRow(3,"BACKGROUND MOTION",OnOff(preferences_.backgroundMotion));
            drawRow(4,"REFLECTIONS",OnOff(preferences_.reflections));
            drawRow(5,"ACCENT THEME",themes[preferences_.theme]);
        }else if(audioSettings_){
            drawRow(0,"MENU MUSIC",OnOff(preferences_.musicEnabled));
            drawRow(1,"TRACK",preferences_.musicTrack.empty()?"NO TRACK":preferences_.musicTrack);
            drawRow(2,"VOLUME",std::to_string(preferences_.musicVolume)+"%");
            drawRow(3,"RELOAD MUSIC LIST","A  RELOAD");
            DrawFittedText(renderer_,musicStatus_,contentX,500,730,1,
                glass?Color{62,73,82,255}:Color{188,199,216,255});
            DrawFittedText(renderer_,"/wiiu/apps/uFlow/music/  |  MP3, OGG, WAV  |  MAX 32 MIB",contentX,536,730,1,
                glass?Color{62,73,82,255}:Color{188,199,216,255});
        }else if(settingsCategory_==4){
            const Color controlText=glass?Color{43,54,63,255}:Color{238,244,252,255};
            const Color controlMuted=glass?Color{65,79,90,255}:Color{130,147,171,255};
            DrawText(renderer_,bindingCapture_>=0?"PRESS A BUTTON":"CONTROLLER BINDINGS",contentX,205,3,controlText);
            for(int row=0;row<=static_cast<int>(ControlAction::Count);++row){
                const int y=238+row*29; const bool selected=row==settingsRow_&&!settingsSidebar_;
                if(selected) FillRoundedRect(renderer_,{contentX-12,y-4,750,27},9,{accent.r,accent.g,accent.b,65});
                if(row==static_cast<int>(ControlAction::Count)) DrawText(renderer_,"RESET DEFAULTS",contentX,y,2,selected?controlText:controlMuted);
                else { DrawText(renderer_,ControlActionName(static_cast<ControlAction>(row)),contentX,y,2,selected?controlText:controlMuted);
                    DrawText(renderer_,bindingCapture_==row?"LISTENING...":ControlBindingName(preferences_.bindings[row]),contentX+620,y,2,selected?accent:controlMuted,true); }
            }
        }
        FillRoundedRect(renderer_,{x+38,582,1064,1},1,{accent.r,accent.g,accent.b,80});
        const Color help=glass?Color{82,94,103,255}:Color{211,220,233,255};
        DrawText(renderer_,settingsEditing_?"LEFT/RIGHT  VALUE":"LEFT/RIGHT  FOCUS",x+48,600,1,help);
        DrawSettingsActions(contentX,accent,{"SECTIONS","PREVIOUS","NEXT",settingsEditing_?"B  DONE":"B  BACK"});
        const char *footerHelp = settingsCategory_==4 ?
            (bindingCapture_>=0 ? "PRESS A BUTTON TO BIND    HOME STAYS SYSTEM-OWNED" : "A  CHANGE BINDING    RESET DEFAULTS AVAILABLE") :
            (settingsEditing_ ? "UP/DOWN  NEXT ROW    A / B  DONE" : "A  SELECT / EDIT");
        DrawText(renderer_,footerHelp,contentX,564,1,help);
    }
    void DrawSourceManager(Color accent, int x) {
        const bool glass = preferences_.skin == 1;
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
            const bool selected = row == sourceRow_ && !settingsSidebar_;
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
                constexpr const char *actions[]={"+  ADD DIRECTORY","SCAN NOW","DISPLAY OPTIONS","MEDIA SETUP GUIDE"};
                DrawText(renderer_,actions[sourceIndex-SourceCount()],x+10,y+8,2,selected?text:muted);
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
        DrawText(renderer_, "A  SELECT    +  ADD DIRECTORY    Y  SCAN NOW", x, 560, 1, muted);
        DrawSettingsActions(x,accent,{"UP","DOWN","+  ADD","B  BACK"});
    }
    void DrawSourceEditor(Color accent, int x) {
        const bool glass = preferences_.skin == 1;
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
        if(VideoSourceEditing()) {
            char delay[24]; std::snprintf(delay,sizeof(delay),"%.2f SECONDS",draftMediaSource_.video.delayMs/1000.0);
            rows.push_back({"VIDEO START DELAY",delay});
            rows.push_back({"VIDEO LAYER",draftMediaSource_.video.foreground?"FOREGROUND":"BACKGROUND"});
            rows.push_back({"VIDEO VOLUME",draftMediaSource_.video.volume ? std::to_string(draftMediaSource_.video.volume)+"%" : "MUTED"});
            if(BackgroundVideoEditing()) rows.push_back({"FOREGROUND OPACITY",std::to_string(draftMediaSource_.video.foregroundOpacity)+"%"});
        }
        if(mediaMode_) {
            const auto &file=draftMediaSource_.defaultFile;
            const auto slash=file.find_last_of('/');
            rows.push_back({"DEFAULT FILE",file.empty()?(IsCoverMedia(draftMediaSource_.type)?"AUTO DEFAULT.PNG / BROWSE":"NONE / BROWSE"):file.substr(slash==std::string::npos?0:slash+1)});
        }
        rows.push_back({"SAVE PATH","A  SAVE"});
        rows.push_back({"REMOVE PATH",editingSourceIndex_>=0?"A  REMOVE":"NOT SAVED"});
        const int first = SourceEditorFirst();
        for (int row = first; row < std::min(first+7,static_cast<int>(rows.size())); ++row) {
            const int y = 278 + (row-first) * 38;
            if (row == sourceEditorRow_ && !settingsSidebar_) FillRoundedRect(renderer_, {x-12,y-6,750,37}, 13, {accent.r,accent.g,accent.b,70});
            DrawText(renderer_, rows[row].first, x+10, y, 2, row == sourceEditorRow_ ? text : muted);
            const Color valueColor=glass?Color{30,96,119,255}:(row == sourceEditorRow_ ? accent : muted);
            const bool editable=SourceEditorSelector(row);
            if(settingsEditing_ && row==sourceEditorRow_ && !settingsSidebar_)
                FillRoundedRect(renderer_,{x+468,y-7,268,32},10,{accent.r,accent.g,accent.b,95});
            if(editable) { DrawText(renderer_,"<",x+480,y,2,valueColor); DrawText(renderer_,">",x+718,y,2,valueColor); }
            DrawFittedText(renderer_,rows[row].second,x+505,y,editable?192:220,2,valueColor,true);
        }
        if(DefaultFileRow(sourceEditorRow_))
            DrawFittedText(renderer_,draftMediaSource_.defaultFile.empty()?"OPTIONAL FALLBACK WHEN NO GAME FILE EXISTS; A TO CHOOSE":draftMediaSource_.defaultFile,x,560,730,1,muted);
        else DrawText(renderer_,settingsEditing_?"LEFT/RIGHT  VALUE    UP/DOWN  NEXT ROW    A / B  DONE":"A  SELECT / EDIT    DEPTH 0 = THIS FOLDER; 1 = SUBFOLDERS",x,560,1,muted);
        DrawSettingsActions(x,accent,{"PREVIOUS","NEXT / OPEN","+  SAVE",settingsEditing_?"B  DONE":"B  CANCEL"});
    }
    void DrawMediaHelp(int x) {
        const bool glass=preferences_.skin==1;
        const Color text=glass?Color{43,54,63,255}:Color{238,244,252,255};
        DrawText(renderer_,"MEDIA / SETUP GUIDE",x,202,3,text);
        const char *lines[]={
            "1. ADD A DIRECTORY; SELECT ITS MEDIA TYPE AND LIBRARY.",
            "2. NAME EACH FILE BY TITLE ID OR THE GAME NAME.",
            "   EXAMPLE: 00050000101fa800.png OR 140.png",
            "3. FULL COVER / FRONT / BACK / SPINE: PNG, TGA, JPG.",
            "   FULL SLEEVE WINS; MISSING FACES USE THE DEFAULT SLEEVE.",
            "4. METADATA: MATCHING .ini FILE; description=, genre=, etc.",
            "5. VIDEOS: MP4 / AVI; SET IDLE DELAY, LAYER AND VOLUME.",
            "6. OPTIONAL DEFAULT FILE FOR MISSING MEDIA; SAVE AND SCAN.",
            "TITLE ID WINS OVER NAME. LIBRARY-SPECIFIC WINS OVER ALL.",
            "FULL GUIDE: docs/CONTENT_AND_MEDIA.md IN THE PROJECT."};
        for(int i=0;i<10;++i) DrawFittedText(renderer_,lines[i],x,251+i*30,730,1,text);
        DrawSettingsActions(x,{45,174,222,255},{"BACK","BACK","BACK","B  BACK"});
    }
    void DrawDirectoryBrowser(Color accent, int x) {
        const bool glass = preferences_.skin == 1;
        const Color text = glass ? Color{42,53,62,255} : Color{238,244,252,255};
        const Color muted = glass ? Color{62,77,88,255} : Color{130,147,171,255};
        DrawText(renderer_,defaultFileBrowser_?"MEDIA / SELECT DEFAULT FILE":mediaMode_?"MEDIA / SELECT DIRECTORY":"LIBRARY / SELECT DIRECTORY",x,202,3,text);
        DrawFittedText(renderer_, browserPath_, x, 240, 725, 1, glass?Color{30,96,119,255}:accent);
        if (browserDirectories_.empty()) {
            DrawText(renderer_, "NO SUBDIRECTORIES", x, 290, 2, muted);
        } else {
            const int total = static_cast<int>(browserDirectories_.size());
            const int first = std::max(0, std::min(browserRow_ - 5, total - 8));
            for (int visible = 0; visible < 8 && first + visible < total; ++visible) {
                const int row = first + visible;
                const int y = 280 + visible * 37;
                if (row == browserRow_ && !settingsSidebar_) FillRoundedRect(renderer_, {x-12,y-9,750,34}, 10, {accent.r,accent.g,accent.b,75});
                DrawFittedText(renderer_, ">  "+browserDirectories_[row], x+10, y, 710, 2,
                         row == browserRow_ ? text : muted);
            }
        }
        const std::string browserStatus=browserLoading_?"READING FOLDER"+std::string((SDL_GetTicks()/300)%4,'.'):
            !browserError_.empty()?browserError_:"SWIPE UP/DOWN TO SCROLL";
        DrawFittedText(renderer_,browserStatus,x,560,730,1,muted);
        DrawSettingsActions(x,accent,{defaultFileBrowser_?"A  OPEN / SELECT":"A  OPEN",defaultFileBrowser_?"Y  SELECT":"Y  USE FOLDER","B  PARENT","X  CANCEL"});
    }
    void DrawSettingsActions(int x,Color accent,const std::array<const char *,4> &labels) {
        for(int i=0;i<4;++i) {
            FillRoundedRect(renderer_,{x+i*185,582,176,36},9,{accent.r,accent.g,accent.b,48});
            DrawText(renderer_,labels[i],x+i*185+88,594,1,
                preferences_.skin==1?Color{43,65,78,255}:Color{236,242,250,255},true);
        }
    }
    void DrawInjectionDialog(Color accent) {
        const PopupTextGlowScope glow(preferences_.skin==1);
        const bool glass=preferences_.skin==1;
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
        const bool glass=preferences_.skin==1;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{270,160,740,410},24,glass?Color{239,246,248,245}:Color{20,31,49,250});
        if(glass)DrawGlassSurface(renderer_,{270,160,740,410},24,accent,100,true);
        const auto &p=scanState_.progress;
        DrawText(renderer_,!scanState_.error.empty()?"SCAN STOPPED":scanState_.finished?"SCAN COMPLETE":"SCANNING LIBRARY",640,190,3,ink,true);
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
        DrawText(renderer_,std::to_string(p.games)+" GAMES FOUND  |  "+std::to_string(scanElapsed_/60)+"m "+std::to_string(scanElapsed_%60)+"s",640,374,2,ink,true);
        if(!scanState_.finished){
            const std::string detail=p.total?std::to_string(p.completed)+" / "+std::to_string(p.total)+" GAMES PROCESSED":"DISCOVERING FILES - TOTAL NOT YET KNOWN";
            DrawText(renderer_,detail,640,417,1,ink,true);
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
        const bool glass=preferences_.skin==1;
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
    void DrawWelcome(Color accent) {
        const bool glass=preferences_.skin==1;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:Color{238,244,252,255};
        FillRoundedRect(renderer_,{240,220,800,280},24,glass?Color{239,246,248,242}:Color{20,31,49,245});
        if(glass)DrawGlassSurface(renderer_,{240,220,800,280},24,accent,100,true);
        DrawText(renderer_,"WELCOME TO UFLOW",640,252,3,ink,true);
        const auto settingsButton=ControlBindingName(preferences_.bindings[static_cast<int>(ControlAction::Settings)]);
        DrawText(renderer_,std::string("Press ")+settingsButton+" for Settings > Library > Add Directory.",640,323,1,ink,true);
        DrawText(renderer_,"Choose a folder, save, then Scan Now.",640,361,1,ink,true);
        FillRoundedRect(renderer_,{500,420,280,50},14,{accent.r,accent.g,accent.b,90});
        DrawText(renderer_,"A  GOT IT",640,437,2,ink,true);
    }
    void DrawToast(Color accent) {
        const int width=std::min(820,TextWidth(status_,2)+64);
        FillRoundedRect(renderer_,{(kWidth-width)/2,568,width,42},16,{accent.r,accent.g,accent.b,235});
        DrawText(renderer_,Ellipsize(status_,62),kWidth/2,581,2,{4,12,22,255},true);
    }

    bool updateOpen_=false,updateYes_=false;
    int updateScroll_=0;
    UFlowUpdate::Snapshot updateState_;
    std::vector<std::string> updateLines_;
    SDL_Renderer *renderer_;
    std::vector<GameEntry> &games_;
    TextureCache textures_;
    UserSettings preferences_;
    bool welcomeOpen_=false;
    std::vector<std::string> libraryTabs_{kFavoritesLibraryId};
    struct FooterHint {
        std::string button;
        const char *action = "";
        int x = 0, width = 0, buttonWidth = 0, gap = 0;
        bool stacked = false;
    };
    std::array<FooterHint,5> footerHints_{};
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
    bool audioSettings_=false;
    MenuMusic music_;
    VideoPlayer video_{renderer_};
    GlassBackground glassBackground_{renderer_};
    bool videoPending_=false;
    VideoPlayer::Stats videoReportStats_{};
    Uint32 videoReportAt_=0;
    unsigned videoReports_=0;
    bool restoreMusicAfterBackground_=false;
    VideoTrigger videoTrigger_;
    const GameEntry *videoSelection_ = nullptr;
    VideoOptions videoOptions_;
    bool videoConfigured_ = false;
    bool videoInputActive_ = false;
#ifdef UFLOW_HOST_PREVIEW
    bool videoTest_ = false;
#endif
    std::string musicStatus_="MUSIC OFF";
    std::vector<std::string> musicTracks_;
    std::vector<std::size_t> visible_;
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
    int tab_ = 0;
    int settingsCategory_ = 0;
    int settingsRow_ = 0;
    int bindingCapture_ = -1;
    int sourceRow_ = 0;
    int sourceEditorRow_ = 0;
    int editingSourceIndex_ = -1;
    ContentSource draftSource_;
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
    bool sourceManager_ = false, sourceEditor_ = false, directoryBrowser_ = false;
    bool defaultFileBrowser_ = false;
    bool defaultFileChosen_ = false;
    bool scanRequested_ = false;
    ScanWorker scanWorker_;
    ScanWorker::Status scanState_;
    bool scanOpen_=false,scanApplied_=false,scanCancelling_=false;
    Uint32 scanStarted_=0,scanElapsed_=0;
    std::size_t scanInvalid_=0;
    std::string status_;
    Uint32 statusUntil_ = 0;
    SDL_Texture *sceneTexture_ = nullptr;
    SDL_Texture *blurTexture_ = nullptr;
    SDL_Texture *blurScratch_ = nullptr;
    SDL_Texture *lensTexture_ = nullptr;
    // Full inspection needs 1292x1536; align width up to 32 pixels. The old
    // 1280-wide bucket silently sent inspection through the non-AA fallback.
    inline static constexpr std::array<SDL_Point,3> kCaseTargetSizes{{{640,768},{896,1024},{1312,1536}}};
    std::array<SDL_Texture *,3> caseTextures_{};
    struct SideCase { std::string key; SDL_Texture *texture; std::uint64_t use; };
    std::vector<SideCase> sideCases_;
    std::uint64_t sideCaseUse_ = 0;
#ifdef UFLOW_HOST_PREVIEW
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
    if (loadedLibraryCache && RefreshInstalledGames(games)) SaveLibraryCache(games);
    const bool initialScanNeeded=!loadedLibraryCache && mounted;
    Trace(loadedLibraryCache ? "library-cache-loaded" : "library-scan-pending");
    bool launchRequested=false;
    bool running=true;
    bool foreground=true;
    RPXLoaderStatus loaderStatus=RPX_LOADER_RESULT_NOT_AVAILABLE;
    OSDynLoad_Module launchLooseModule=nullptr;
    {
    Dashboard dashboard(renderer,games);
    UFlowUpdate::Client updater;
    bool updateChecked=false,updateTouchHeld=false,updateHomeBlocked=false;
    VisualRemote visualRemote;
    dashboard.SetStatus(mounted ? (loadedLibraryCache ? "LIBRARY CACHE  " : "LIBRARY READY  ")+
        std::to_string(games.size())+" GAMES" : "SOURCE DRIVE COULD NOT BE MOUNTED", mounted ? 3500 : 10000);
    loaderStatus=mounted?RPXLoader_InitLibrary():RPX_LOADER_RESULT_NOT_AVAILABLE;
    LaunchLooseFn launchLoose=loaderStatus==RPX_LOADER_RESULT_SUCCESS?LoadLaunchFunction(&launchLooseModule):nullptr;
    if(initialScanNeeded)dashboard.StartScan();
    Uint64 previousCounter=SDL_GetPerformanceCounter();
    const double counterFrequency=static_cast<double>(SDL_GetPerformanceFrequency());
    int heldHorizontal=0;
    int heldVertical=0;
    Uint32 horizontalStarted=0;
    Uint32 horizontalNext=0;
    Uint32 verticalNext=0;
    bool touchHeld=false;
    WiiRemoteInput wiiRemote;
    InputResumeGate inputResume;
    int touchStartX=0,touchStartY=0,touchLastX=0,touchLastY=0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                dashboard.StopScan();
                updater.Stop();if(updateHomeBlocked){OSEnableHomeButtonMenu(true);updateHomeBlocked=false;}
                Trace("sdl-quit-event");
                running = false;
                dashboard.StopMusic();
                Trace("system-quit-media-stop-complete");
            } else if (event.type == SDL_APP_WILLENTERBACKGROUND) {
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
            if (launchRequested) visualRemote.Stop();
            OSSleepTicks(OSMillisecondsToTicks(4));
            continue;
        }
        const Uint32 ticks=SDL_GetTicks();
        const Uint64 counter=SDL_GetPerformanceCounter();
        const float delta=std::min(.1f,static_cast<float>((counter-previousCounter)/counterFrequency)); previousCounter=counter;
        const std::string visualCommand = visualRemote.Poll();
        if (visualCommand == "stats") visualRemote.ReplyStats(dashboard.PerformanceStats());
        else if (!visualCommand.empty()) {
            const bool accepted = visualCommand == "capture" || dashboard.VisualPreview(visualCommand);
            visualRemote.Reply(accepted && dashboard.Render(0, "fs:/vol/external01/wiiu/apps/uFlow/visual.png"));
            continue;
        }
        VPADStatus input{}; VPADReadError error;
        const bool gamePadReady=VPADRead(VPAD_CHAN_0,&input,1,&error)>0&&error==VPAD_READ_SUCCESS;
        if(!gamePadReady) input={};
        wiiRemote.Merge(input,ticks);
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
        if(dashboard.UpdateOpen()){
            int x=-1,y=-1;VPADTouchData point{};
            if(gamePadReady){VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&point,&input.tpNormal);
                if(point.touched&&point.validity==VPAD_VALID&&!updateTouchHeld){x=point.x;y=point.y;}}
            updateTouchHeld=gamePadReady&&point.touched;
            int direction=(input.trigger&(VPAD_BUTTON_LEFT|VPAD_BUTTON_UP))?-1:(input.trigger&(VPAD_BUTTON_RIGHT|VPAD_BUTTON_DOWN))?1:0;
            int action=dashboard.UpdateInput(direction,input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,x,y);
            if(action==1){OSEnableHomeButtonMenu(false);updateHomeBlocked=true;updater.Accept();}
            else if(action==-1)updater.Dismiss();
            heldHorizontal=heldVertical=0;touchHeld=false;dashboard.Render(delta);continue;
        }
        std::uint32_t previewHold=input.hold;
        if(input.leftStick.y>.48f) previewHold|=VPAD_BUTTON_UP;
        if(input.leftStick.y<-.48f) previewHold|=VPAD_BUTTON_DOWN;
        const bool inspectionInput=dashboard.CoverInspectOpen() ||
            dashboard.ButtonHeld(ControlAction::ZoomFront,previewHold) || dashboard.ButtonHeld(ControlAction::ZoomBack,previewHold);
        dashboard.VideoInput(!(inspectionInput && dashboard.VideoBehindInspection()) && (input.hold || input.trigger || input.tpNormal.touched ||
            std::abs(input.leftStick.x)>.28f || std::abs(input.leftStick.y)>.32f ||
            std::abs(input.rightStick.x)>.28f || std::abs(input.rightStick.y)>.32f));
        if (dashboard.CapturingBinding()) {
            dashboard.CaptureBinding(input.trigger);
            dashboard.Render(delta);
            continue;
        }
        const bool neutral = input.hold==0 && input.trigger==0 &&
            std::abs(input.leftStick.x)<.28f && std::abs(input.leftStick.y)<.32f;
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
            const int horizontal=((input.hold&VPAD_BUTTON_LEFT)||input.leftStick.x<-.42f)?-1:
                                 ((input.hold&VPAD_BUTTON_RIGHT)||input.leftStick.x>.42f)?1:0;
            const int vertical=((input.hold&VPAD_BUTTON_UP)||input.leftStick.y>.48f)?-1:
                               ((input.hold&VPAD_BUTTON_DOWN)||input.leftStick.y<-.48f)?1:0;
            const bool horizontalEdge=horizontal!=heldHorizontal;
            const bool verticalEdge=vertical!=heldVertical;
            if(horizontalEdge){
                heldHorizontal=horizontal;
                horizontalStarted=ticks;
                horizontalNext=ticks+330;
            }
            if(verticalEdge) { heldVertical=vertical; verticalNext=ticks+360; }
            const bool verticalRepeat=vertical && !verticalEdge && SDL_TICKS_PASSED(ticks,verticalNext);
            if(verticalRepeat) verticalNext=ticks+125;
            std::uint32_t actionTrigger=input.trigger;
            std::uint32_t actionHold=input.hold;
            if(verticalEdge&&vertical<0)actionTrigger|=VPAD_BUTTON_UP;
            else if(verticalEdge&&vertical>0)actionTrigger|=VPAD_BUTTON_DOWN;
            if(vertical<0)actionHold|=VPAD_BUTTON_UP;
            else if(vertical>0)actionHold|=VPAD_BUTTON_DOWN;
            VPADTouchData touch{};
            const bool modalInput=dashboard.InjectionDialogOpen();
            if(gamePadReady) VPADGetTPCalibratedPointEx(VPAD_CHAN_0,VPAD_TP_1280X720,&touch,&input.tpNormal);
            else touchHeld=false; // a missing sample is not a touch release/click
            if(touch.touched && touch.validity==VPAD_VALID && (modalInput || dashboard.SettingsOpen() || dashboard.DetailsOpen())) {
                if(!touchHeld) { touchStartX=touch.x; touchStartY=touch.y; }
                touchLastX=touch.x; touchLastY=touch.y; touchHeld=true;
            } else if(!touch.touched && touchHeld) {
                touchHeld=false;
                if(std::abs(touchLastX-touchStartX)<60) {
                    if(modalInput)dashboard.InjectionTouch(touchStartX,touchStartY);
                    else if(dashboard.DetailsOpen()) dashboard.DetailsTouch(touchStartX,touchStartY,touchLastY-touchStartY);
                    else dashboard.SettingsTouch(touchStartX,touchStartY,touchLastY-touchStartY);
                }
            } else if(touch.touched && touch.validity!=VPAD_VALID) touchHeld=false;
            if(!modalInput&&!dashboard.OverlayOpen()&&horizontal!=0){
                if(horizontalEdge)dashboard.Move(horizontal);
                else if(SDL_TICKS_PASSED(ticks,horizontalNext)){
                    const Uint32 elapsed=ticks-horizontalStarted;
                    const int stride=elapsed>2600?4:elapsed>1500?2:1;
                    dashboard.Move(horizontal*stride);
                    const Uint32 interval=elapsed>2600?38:elapsed>1500?62:105;
                    horizontalNext=ticks+interval;
                }
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
                    input.trigger&VPAD_BUTTON_A,input.trigger&VPAD_BUTTON_B,input.trigger&VPAD_BUTTON_PLUS);
            }
            else if (input.trigger&VPAD_BUTTON_B) {
                dashboard.DismissVideo();
                if(dashboard.OverlayOpen()) dashboard.CloseOverlay();
                // B is back-only. HOME/system events own exit to Wii U Menu.
            }
            else if(inspectFrontHeld)dashboard.OpenCoverInspect(false);
            else if(inspectBackHeld)dashboard.OpenCoverInspect(true);
            else if(dashboard.ButtonPressed(ControlAction::Settings,actionTrigger))dashboard.ToggleSettings();
            else if(dashboard.ButtonPressed(ControlAction::Details,actionTrigger))dashboard.ToggleDetails();
            else if(dashboard.ButtonPressed(ControlAction::Favorite,actionTrigger))dashboard.ToggleFavorite();
            else if(dashboard.ButtonPressed(ControlAction::PreviousLibrary,actionTrigger))dashboard.ChangeTab(-1);
            else if(dashboard.ButtonPressed(ControlAction::NextLibrary,actionTrigger))dashboard.ChangeTab(1);
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::PageBack,actionTrigger))dashboard.Move(-10);
            else if(!dashboard.OverlayOpen()&&dashboard.ButtonPressed(ControlAction::PageForward,actionTrigger))dashboard.Move(10);
            GameEntry game=dashboard.Selected()?*dashboard.Selected():GameEntry{};
            const bool dialogLaunch=dashboard.ConsumeInjectionLaunch(game);
            if((dialogLaunch || (!modalInput&&(dashboard.ConsumeDetailsLaunch() || (libraryInput && !dashboard.OverlayOpen() &&
                dashboard.ButtonPressed(ControlAction::Launch,actionTrigger))))) && dashboard.Selected()) {
                dashboard.DismissVideo();
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
                dashboard.FinishInjection(adapterError);
                inputResume.Arm();heldHorizontal=heldVertical=0;touchHeld=false;
                if(!adapterError.empty())continue;
                if(dashboard.ConsumeInjectionFinished()){dashboard.SetStatus("INJECTION SAVED - PLAY NOW USES SAVED INJECTED ROM",10000);continue;}
                if(!emulatorPath.empty()) {
                    dashboard.SetStatus(romRequest.empty()?"OPENING EMULATOR BROWSER - SELECT YOUR ROM THERE":"LAUNCHING "+Ellipsize(game.name,38),10000);dashboard.Render(delta);
                    if(!romRequest.empty()&&!UFlowRom::Write(romRequest,UFlowRom::RelativeROM(game.absolutePath))){dashboard.SetStatus("CANNOT WRITE MODULE LAUNCH REQUEST",10000);continue;}
                    const auto prepared=RPXLoader_PrepareLaunchFromSD(emulatorPath.c_str());
                    if(prepared!=RPX_LOADER_RESULT_SUCCESS){if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.SetStatus(RPXLoader_GetStatusStr(prepared),10000);continue;}
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto result=RPXLoader_LaunchPreparedHomebrew();
                    if(result==RPX_LOADER_RESULT_SUCCESS)launchRequested=true;
                    else{UFlowReturn::Clear();if(!romRequest.empty())UFlowRom::Clear(romRequest);dashboard.ApplyMusic();dashboard.SetStatus(RPXLoader_GetStatusStr(result),10000);}
                } else if(game.installed) {
                    dashboard.SetStatus("LAUNCHING "+Ellipsize(game.name,38),10000); dashboard.Render(delta);
                    std::string launchPath=game.absolutePath;
                    if(launchPath.rfind("fs:",0)==0)launchPath.erase(0,3);
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    _SYSLaunchTitleByPathFromLauncher(launchPath.c_str(),static_cast<uint32_t>(launchPath.size()));
                    launchRequested=true;
                } else if(WiiVC::IsRaw(game)) {
                    Trace(("wii-vc-prepare-selected path=" + game.absolutePath + " title=" + game.titleId).c_str());
                    GameEntry prepared = game;
                    dashboard.BeginWiiVCPreparation(game.name);
                    dashboard.Render(.08f);
                    const auto prepareError = WiiVC::Prepare(prepared, prepareProgress);
                    dashboard.FinishInjection(prepareError);
                    inputResume.Arm();heldHorizontal=heldVertical=0;touchHeld=false;
                    if (!prepareError.empty()) {
                        Trace(("wii-vc-prepare-failed " + prepareError).c_str());
                        continue;
                    }
                    WiiRequest::Request request;
                    const auto error = PrepareWiiLaunch(prepared, request);
                    if (!error.empty()) {
                        Trace(("wii-vc-preflight-failed " + error).c_str());
                        dashboard.SetStatus(error, 10000);
                        continue;
                    }
                    dashboard.SetStatus("LAUNCHING WII VC GAME...", 10000); dashboard.Render(delta);
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto launchError = LaunchWiiGame(request);
                    if (launchError.empty()) { Trace("wii-vc-forwarder-launch-accepted"); launchRequested = true; }
                    else { UFlowReturn::Clear(); Trace(("wii-vc-launch-failed " + launchError).c_str()); dashboard.ApplyMusic(); dashboard.SetStatus(launchError, 10000); }
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
                    dashboard.SetStatus(gc ? "LAUNCHING GAMECUBE GAME..." : "LAUNCHING WII GAME...",10000); dashboard.Render(delta);
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const auto launchError = LaunchWiiGame(request);
                    if (launchError.empty()) {
                        Trace("wii-forwarder-launch-accepted");
                        launchRequested = true;
                    } else {
                        UFlowReturn::Clear();
                        Trace(("wii-launch-failed " + launchError).c_str());
                        dashboard.ApplyMusic(); dashboard.SetStatus(launchError,10000);
                    }
                } else if(game.platform!=Platform::WiiU) dashboard.SetStatus(std::string(PlatformName(game.platform))+" LAUNCH ADAPTER IS NEXT");
                else {
                    Trace(("raw-launch-selected path=" + game.absolutePath + " title=" + game.titleId).c_str());
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
                    dashboard.SetStatus("LAUNCHING "+Ellipsize(game.name,38),10000); dashboard.Render(delta);
                    dashboard.StopMusic();
                    if(!UFlowReturn::Arm()){dashboard.ApplyMusic();dashboard.SetStatus("CANNOT SAVE RETURN-TO-UFLOW REQUEST",10000);continue;}
                    const RPXLoaderStatus result=launchLoose(game.rpxRelative.c_str(),game.contentRelative.c_str(),game.codeRelative.c_str(),
                            game.saveRelative.c_str(),game.name.c_str(),game.name.c_str(),game.publisher.c_str());
                    Trace((std::string("raw-launch-result ")+RPXLoader_GetStatusStr(result)).c_str());
                    if(result==RPX_LOADER_RESULT_SUCCESS)launchRequested=true;
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
    Trace("render-loop-stopped");
    }
    Trace("dashboard-destroyed");
    ShutdownSmoothFonts();
    Trace("fonts-destroyed");
    SDL_DestroyRenderer(renderer);
    Trace("renderer-destroyed");
    SDL_DestroyWindow(window);
    Trace("window-destroyed");
    if(launchLooseModule) {
        OSDynLoad_Release(launchLooseModule);
        Trace("loose-loader-module-released");
    }
    if(loaderStatus==RPX_LOADER_RESULT_SUCCESS) {
        RPXLoader_DeInitLibrary();
        Trace("rpx-loader-deinitialized");
    }
    // Keep the SD mounted through SDL shutdown so an incomplete exit leaves a
    // precise checkpoint in uflow.log. SDL owns the Menu request and ProcUI drain.
    Trace("sdl-quit-begin");
    USBStorage::Unmount();
    SDL_Quit();
    Trace("sdl-quit-complete");
    if(mounted) {
        Trace("sd-unmount-request");
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
      if(std::string(argv[1])=="scanchecks")ok=dashboard.CheckScanDialog(argv[2]);
      else if(std::string(argv[1])=="asyncbrowserchecks")ok=dashboard.CheckAsyncBrowser(argv[2]);
      else if(std::string(argv[1])=="asyncartworkchecks")ok=dashboard.CheckAsyncArtwork(argv[2]);
      else if(std::string(argv[1])=="glassbackgroundchecks")ok=dashboard.CheckGlassBackground(argv[2]);
      else if(std::string(argv[1])=="coverfacechecks")ok=dashboard.CheckCoverFaces(argv[2]);
      else if(std::string(argv[1])=="firstrunchecks")ok=dashboard.CheckFirstRun(argv[2]);
      else if(std::string(argv[1])=="updatechecks")ok=dashboard.CheckUpdateDialog(argv[2]);
      else if(std::string(argv[1])=="browserchecks")ok=dashboard.CheckBrowserNavigation(argv[2]);
      else
      // Warm the backdrop first, then capture the reuse path as well as live UI.
      ok = std::string(argv[1]) == "injectionchecks" ? dashboard.CheckInjectionDialog(argv[2]) : std::string(argv[1]) == "artworkchecks" ? dashboard.CheckArtworkPolish(argv[2]) : std::string(argv[1]) == "casechecks" ? dashboard.CheckCaseRendering(argv[2]) : std::string(argv[1]) == "mediadefaultchecks" ? dashboard.CheckMediaDefaults(argv[2]) : std::string(argv[1]) == "mediagroupchecks" ? dashboard.CheckMediaAccordion(argv[2]) : std::string(argv[1]) == "videochecks" ? dashboard.CheckVideoSettings(argv[2]) : std::string(argv[1]) == "hintchecks" ? dashboard.CheckControlHints(argv[2]) : std::string(argv[1]) == "fadechecks" ? dashboard.CheckLibraryFade() : std::string(argv[1]) == "detailschecks" ? dashboard.CheckDetails() : std::string(argv[1]) == "launcherchecks" ? dashboard.CheckLauncherSettings() : std::string(argv[1]) == "checks" ? dashboard.CheckScrolling() :
          dashboard.VisualPreview(argv[1]) && dashboard.Render(0) && dashboard.Render(0, argv[2]); }
    ShutdownSmoothFonts(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    return ok ? 0 : 5;
}
#endif
