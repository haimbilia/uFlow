#pragma once
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>

// Tiny cached nine-slice materials: antialiased edges and analytic soft shadow.
// Generated once on the render thread, no artwork loading or per-frame rasterization.
class GlassMaterial {
    SDL_Texture *surface_ = nullptr, *shadow_ = nullptr;
    static SDL_Texture *Create(SDL_Renderer *renderer, bool shadow) {
        const int size = shadow ? 128 : 96;
        SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
        if (!image) return nullptr;
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
            const float half = shadow ? 48.f : 48.f;
            const float qx = std::abs(x + .5f - size / 2.f) - (half - 24.f);
            const float qy = std::abs(y + .5f - size / 2.f) - (half - 24.f);
            const float distance = std::sqrt(std::max(qx,0.f)*std::max(qx,0.f) + std::max(qy,0.f)*std::max(qy,0.f))
                                   + std::min(std::max(qx,qy),0.f) - 24.f;
            Uint8 r = 31, g = 46, b = 53, a;
            if (shadow) {
                const float outside = std::max(0.f, distance);
                a = static_cast<Uint8>(21.f * std::exp(-outside * outside / 65.f));
            } else {
                const float coverage = std::clamp(.5f - distance, 0.f, 1.f);
                const float rim = std::exp(-std::pow((distance+1.0f)/.85f,2.f));
                const float innerRim = std::exp(-std::pow((distance+5.0f)/1.5f,2.f));
                const float light = std::clamp(1.1f-(x+y)*.003f,.55f,1.f);
                const float sheen = std::exp(-std::pow((y-12.f)/8.f,2.f));
                const float glint = std::exp(-((x-13.f)*(x-13.f)+(y-10.f)*(y-10.f))/55.f);
                // Clear centre, bright outer rim and an inner caustic describe
                // thickness without the old opaque grey fill/dark lower border.
                r=245; g=250; b=253;
                a=static_cast<Uint8>(coverage*std::min(255.f,32+188*rim*light+62*innerRim+37*sheen+65*glint));
            }
            auto *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(image->pixels) + y * image->pitch);
            row[x] = SDL_MapRGBA(image->format, r, g, b, a);
        }
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, image);
        SDL_FreeSurface(image);
        if (texture) { SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear); }
        return texture;
    }
    static void NineSlice(SDL_Renderer *renderer, SDL_Texture *texture, SDL_Rect rect, int size, int edge) {
        if (!texture || rect.w <= 0 || rect.h <= 0) return;
        const int border = std::min({edge, rect.w / 2, rect.h / 2});
        int src[] = {0, edge, size-edge, size};
        int dx[] = {rect.x, rect.x+border, rect.x+rect.w-border, rect.x+rect.w};
        int dy[] = {rect.y, rect.y+border, rect.y+rect.h-border, rect.y+rect.h};
        for (int y=0; y<3; ++y) for (int x=0; x<3; ++x) {
            SDL_Rect from{src[x],src[y],src[x+1]-src[x],src[y+1]-src[y]};
            SDL_Rect to{dx[x],dy[y],dx[x+1]-dx[x],dy[y+1]-dy[y]};
            if (to.w && to.h) SDL_RenderCopy(renderer, texture, &from, &to);
        }
    }
public:
    // Small lenses use the whole curved material, not a stretched flat centre.
    void DrawButton(SDL_Renderer *renderer, SDL_Rect rect, bool focused) {
        if (!surface_) surface_ = Create(renderer, false);
        DrawShadow(renderer, rect);
        if (!surface_) return;
        SDL_SetTextureColorMod(surface_, focused ? 167 : 255, focused ? 235 : 255, 255);
        if(rect.w>rect.h*2) NineSlice(renderer,surface_,rect,96,32);
        else SDL_RenderCopy(renderer, surface_, nullptr, &rect);
        SDL_SetTextureColorMod(surface_, 255, 255, 255);
    }
    void DrawShadow(SDL_Renderer *renderer, SDL_Rect rect) {
        if (!shadow_) shadow_ = Create(renderer, true);
        NineSlice(renderer, shadow_, {rect.x-16,rect.y-8,rect.w+32,rect.h+32},128,48);
    }
    void Draw(SDL_Renderer *renderer, SDL_Rect rect, bool elevated, Uint8 strength=255) {
        if (!surface_) surface_ = Create(renderer, false);
        if (elevated) DrawShadow(renderer, rect);
        if(surface_) SDL_SetTextureAlphaMod(surface_,strength);
        NineSlice(renderer, surface_, rect, 96, 32);
        if(surface_) SDL_SetTextureAlphaMod(surface_,255);
        if(rect.w>100 && rect.h>40) {
            const float inset=std::min(26.f,rect.h*.35f);
            SDL_Vertex sheen[4]{};
            sheen[0].position={rect.x+inset,rect.y+7.f};
            sheen[1].position={rect.x+rect.w-inset,rect.y+7.f};
            sheen[2].position={rect.x+rect.w*.62f,rect.y+rect.h*.39f};
            sheen[3].position={rect.x+7.f,rect.y+rect.h*.48f};
            sheen[0].color={255,255,255,static_cast<Uint8>(strength*32/255)};
            sheen[1].color={255,255,255,static_cast<Uint8>(strength*17/255)};
            sheen[2].color=sheen[3].color={255,255,255,0};
            constexpr int indices[]={0,1,2,0,2,3};
            SDL_RenderGeometry(renderer,nullptr,sheen,4,indices,6);
        }
    }
    void Clear() { if (surface_) SDL_DestroyTexture(surface_); if (shadow_) SDL_DestroyTexture(shadow_); surface_ = shadow_ = nullptr; }
};
