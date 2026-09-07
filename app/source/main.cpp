#include "library.hpp"

#include <SDL2/SDL.h>
#include <coreinit/dynload.h>
#include <coreinit/time.h>
#include <png.h>
#include <rpxloader/rpxloader.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/sdcard.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;
constexpr std::array<const char *, 5> kTabNames = {"ALL", "WII U", "WII", "GAMECUBE", "FAVORITES"};

using LaunchLooseFn = RPXLoaderStatus (*)(const char *, const char *, const char *, const char *,
                                          const char *, const char *, const char *);

struct Color { Uint8 r, g, b, a; };
struct TextureInfo { SDL_Texture *texture = nullptr; int width = 0; int height = 0; };
struct CachedTexture { TextureInfo info; Uint32 lastUse = 0; };

void SetColor(SDL_Renderer *renderer, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

Color Mix(Color a, Color b, float amount) {
    const auto channel = [amount](Uint8 left, Uint8 right) {
        return static_cast<Uint8>(left + (right - left) * amount);
    };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a)};
}

Color AccentFor(Platform platform) {
    switch (platform) {
        case Platform::WiiU: return {0, 190, 236, 255};
        case Platform::Wii: return {80, 197, 236, 255};
        case Platform::GameCube: return {132, 102, 238, 255};
    }
    return {0, 190, 236, 255};
}

void FillRoundedRect(SDL_Renderer *renderer, SDL_Rect rect, int radius, Color color) {
    radius = std::min(radius, std::min(rect.w, rect.h) / 2);
    SetColor(renderer, color);
    SDL_Rect horizontal = {rect.x + radius, rect.y, rect.w - radius * 2, rect.h};
    SDL_Rect vertical = {rect.x, rect.y + radius, rect.w, rect.h - radius * 2};
    SDL_RenderFillRect(renderer, &horizontal);
    SDL_RenderFillRect(renderer, &vertical);
    for (int y = 0; y < radius; ++y) {
        const int dy = radius - y;
        const int dx = static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy)));
        SDL_Rect top = {rect.x + radius - dx, rect.y + y, rect.w - 2 * (radius - dx), 1};
        SDL_Rect bottom = {top.x, rect.y + rect.h - 1 - y, top.w, 1};
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);
    }
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
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png) png_destroy_read_struct(&png, info ? &info : nullptr, nullptr);
        std::fclose(file);
        return nullptr;
    }
    png_init_io(png, file);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);
    const png_uint_32 width = png_get_image_width(png, info);
    const png_uint_32 height = png_get_image_height(png, info);
    const int colorType = png_get_color_type(png, info);
    const int bitDepth = png_get_bit_depth(png, info);
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (!(colorType & PNG_COLOR_MASK_ALPHA)) png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png, info);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, static_cast<int>(width), static_cast<int>(height), 32,
                                                          SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        std::vector<png_bytep> rows(height);
        for (png_uint_32 y = 0; y < height; ++y) rows[y] = static_cast<png_bytep>(surface->pixels) + y * surface->pitch;
        png_read_image(png, rows.data());
    }
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
    if (width <= 0 || height <= 0 || (bytes != 3 && bytes != 4) || width > 4096 || height > 4096) {
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

class TextureCache {
public:
    explicit TextureCache(SDL_Renderer *renderer) : renderer_(renderer) {}
    ~TextureCache() { Clear(); }
    TextureInfo Get(const std::string &path) {
        if (path.empty()) return {};
        if (const auto found = textures_.find(path); found != textures_.end()) {
            found->second.lastUse = SDL_GetTicks();
            return found->second.info;
        }
        if (textures_.size() >= 32) {
            auto oldest = std::min_element(textures_.begin(), textures_.end(), [](const auto &left, const auto &right) {
                return left.second.lastUse < right.second.lastUse;
            });
            if (oldest != textures_.end()) {
                if (oldest->second.info.texture) SDL_DestroyTexture(oldest->second.info.texture);
                textures_.erase(oldest);
            }
        }
        SDL_Surface *surface = nullptr;
        const auto dot = path.find_last_of('.');
        const std::string extension = dot == std::string::npos ? std::string() : path.substr(dot + 1);
        if (extension == "png" || extension == "PNG") surface = LoadPng(path);
        else if (extension == "tga" || extension == "TGA") surface = LoadTga(path);
        TextureInfo result;
        if (surface) {
            result.width = surface->w; result.height = surface->h;
            result.texture = SDL_CreateTextureFromSurface(renderer_, surface);
            if (result.texture) SDL_SetTextureBlendMode(result.texture, SDL_BLENDMODE_BLEND);
            SDL_FreeSurface(surface);
        }
        textures_[path] = {result, SDL_GetTicks()};
        return result;
    }
    void Clear() {
        for (auto &[unused, cached] : textures_) if (cached.info.texture) SDL_DestroyTexture(cached.info.texture);
        textures_.clear();
    }
private:
    SDL_Renderer *renderer_;
    std::unordered_map<std::string, CachedTexture> textures_;
};

class Dashboard {
public:
    Dashboard(SDL_Renderer *renderer, std::vector<GameEntry> &games)
        : renderer_(renderer), games_(games), textures_(renderer) { RebuildVisible(); }

    void ReplaceGames(std::vector<GameEntry> games) {
        textures_.Clear(); games_ = std::move(games); selected_ = 0; visualSelected_ = 0.0f; RebuildVisible();
    }
    void SetStatus(std::string status, Uint32 lifetime = 3500) {
        status_ = std::move(status); statusUntil_ = SDL_GetTicks() + lifetime;
    }
    const GameEntry *Selected() const { return visible_.empty() ? nullptr : &games_[visible_[selected_]]; }
    void Move(int amount) {
        if (visible_.empty()) return;
        selected_ = static_cast<std::size_t>(std::clamp<int>(static_cast<int>(selected_) + amount, 0,
                                                              static_cast<int>(visible_.size()) - 1));
    }
    void ChangeTab(int amount) {
        tab_ = (tab_ + amount + static_cast<int>(kTabNames.size())) % static_cast<int>(kTabNames.size());
        selected_ = 0; visualSelected_ = 0.0f; details_ = false; RebuildVisible();
    }
    void ToggleFavorite() {
        if (!Selected()) return;
        auto &game = games_[visible_[selected_]];
        game.favorite = !game.favorite;
        SaveFavorites(games_);
        SetStatus(game.favorite ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
        if (tab_ == 4 && !game.favorite) RebuildVisible();
    }
    void ToggleDetails() { if (Selected()) details_ = !details_; }
    void ToggleSettings() { settings_ = !settings_; details_ = false; }
    bool OverlayOpen() const { return details_ || settings_; }
    void CloseOverlay() { details_ = settings_ = false; }

    void Render(float deltaSeconds) {
        visualSelected_ += (static_cast<float>(selected_) - visualSelected_) * std::min(1.0f, deltaSeconds * 11.0f);
        const GameEntry *selected = Selected();
        const Color accent = selected ? AccentFor(selected->platform) : Color{0, 190, 236, 255};
        DrawBackground(accent); DrawHeader(accent);
        if (visible_.empty()) DrawEmpty(accent); else DrawCarousel(accent);
        DrawFooter(accent);
        if (details_) DrawDetails(accent);
        if (settings_) DrawSettings(accent);
        if (!status_.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), statusUntil_)) status_.clear();
        if (!status_.empty()) DrawToast(accent);
        SDL_RenderPresent(renderer_);
    }

private:
    void RebuildVisible() {
        visible_.clear();
        for (std::size_t index = 0; index < games_.size(); ++index) {
            const auto &game = games_[index];
            if (tab_ == 0 || (tab_ == 1 && game.platform == Platform::WiiU) ||
                (tab_ == 2 && game.platform == Platform::Wii) ||
                (tab_ == 3 && game.platform == Platform::GameCube) || (tab_ == 4 && game.favorite)) visible_.push_back(index);
        }
        selected_ = visible_.empty() ? 0 : std::min(selected_, visible_.size() - 1);
    }
    void DrawBackground(Color accent) {
        SetColor(renderer_, Mix({5,10,20,255}, accent, .07f));
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const float phase = SDL_GetTicks() / 1800.0f;
        const int drift = static_cast<int>(std::sin(phase) * 35.0f);
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
        FillRoundedRect(renderer_, {38,26,1204,74}, 22, {5,10,20,190});
        DrawText(renderer_, "UFLOW", 66, 46, 4, {245,249,255,255});
        DrawText(renderer_, "LIBRARY", 198, 55, 2, accent);
        int x = 445;
        for (int index = 0; index < static_cast<int>(kTabNames.size()); ++index) {
            const int width = TextWidth(kTabNames[index], 2) + 34;
            if (index == tab_) FillRoundedRect(renderer_, {x-17,43,width,38}, 14, {accent.r,accent.g,accent.b,42});
            DrawText(renderer_, kTabNames[index], x, 55, 2,
                     index == tab_ ? Color{255,255,255,255} : Color{139,151,171,255});
            x += width + 14;
        }
        FillRoundedRect(renderer_, {1187,46,28,28}, 14, accent);
        DrawText(renderer_, std::to_string(visible_.size()), 1173, 82, 1, {170,181,199,255}, true);
    }
    void DrawEmpty(Color accent) {
        DrawText(renderer_, tab_ == 4 ? "NO FAVORITES YET" : "NO GAMES FOUND", 640, 300, 5, {235,241,250,255}, true);
        DrawText(renderer_, tab_ == 4 ? "MARK A GAME WITH X" : "PRESS MINUS TO RESCAN THE DRIVE",
                 640, 365, 2, accent, true);
    }
    void DrawCover(const GameEntry &game, SDL_Rect rect, bool selected, Color accent) {
        SDL_Rect shadow = {rect.x+12,rect.y+15,rect.w,rect.h};
        FillRoundedRect(renderer_, shadow, 18, {0,0,0,105});
        FillRoundedRect(renderer_, rect, 18, selected ? Color{235,241,250,255} : Color{27,37,54,255});
        const int border = selected ? 5 : 2;
        SDL_Rect inner = {rect.x+border,rect.y+border,rect.w-border*2,rect.h-border*2};
        FillRoundedRect(renderer_, inner, 14, selected ? Color{10,18,31,255} : Color{15,24,39,255});
        const TextureInfo cover = textures_.Get(game.coverPath);
        if (cover.texture) {
            const int padding = selected ? 16 : 11;
            SDL_Rect area = {inner.x+padding,inner.y+padding,inner.w-padding*2,inner.h-padding*2};
            const float sourceRatio = static_cast<float>(cover.width) / cover.height;
            const float targetRatio = static_cast<float>(area.w) / area.h;
            SDL_Rect destination = area;
            if (sourceRatio > targetRatio) { destination.h = static_cast<int>(area.w/sourceRatio); destination.y += (area.h-destination.h)/2; }
            else { destination.w = static_cast<int>(area.h*sourceRatio); destination.x += (area.w-destination.w)/2; }
            SDL_RenderCopy(renderer_, cover.texture, nullptr, &destination);
        } else {
            const Color platformAccent = AccentFor(game.platform);
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
    void DrawCarousel(Color accent) {
        const int selectedIndex = static_cast<int>(selected_);
        const float offset = visualSelected_ - selectedIndex;
        for (int distance=4; distance>=1; --distance) for (int direction : {-1,1}) {
            const int index=selectedIndex+distance*direction;
            if (index<0 || index>=static_cast<int>(visible_.size())) continue;
            const float relative=static_cast<float>(index)-visualSelected_;
            const float depth=std::min(1.0f,std::abs(relative)/4.0f);
            const int width=static_cast<int>(176-depth*34), height=static_cast<int>(250-depth*48);
            DrawCover(games_[visible_[index]], {static_cast<int>(445+relative*165-width/2),204+static_cast<int>(depth*25),width,height}, false, accent);
        }
        const int centerX=static_cast<int>(445-offset*165);
        DrawCover(games_[visible_[selectedIndex]], {centerX-127,144,254,362}, true, accent);
        const auto &game=games_[visible_[selected_]];
        DrawText(renderer_,Ellipsize(game.name,35),630,205,4,{246,249,255,255});
        DrawText(renderer_,PlatformName(game.platform),632,254,2,accent);
        if (!game.publisher.empty()) DrawText(renderer_,Ellipsize(game.publisher,38),632,292,2,{154,167,188,255});
        if (!game.titleId.empty()) DrawText(renderer_,"ID  "+Ellipsize(game.titleId,24),632,328,2,{105,122,148,255});
        FillRoundedRect(renderer_,{630,380,470,4},2,{accent.r,accent.g,accent.b,130});
        DrawText(renderer_,std::to_string(selected_+1)+" / "+std::to_string(visible_.size()),632,407,2,{183,194,211,255});
        DrawText(renderer_,"A  LAUNCH",632,453,2,{255,255,255,255});
        DrawText(renderer_,"Y  DETAILS",790,453,2,{255,255,255,255});
        DrawText(renderer_,game.favorite?"X  UNFAVORITE":"X  FAVORITE",948,453,2,{255,255,255,255});
    }
    void DrawFooter(Color accent) {
        FillRoundedRect(renderer_,{38,626,1204,64},20,{5,10,20,205});
        DrawText(renderer_,"L/R  PLATFORM",65,648,2,{173,184,201,255});
        DrawText(renderer_,"ZL/ZR  FAST",300,648,2,{173,184,201,255});
        DrawText(renderer_,"-  RESCAN",520,648,2,{173,184,201,255});
        DrawText(renderer_,"+  SETTINGS",700,648,2,{173,184,201,255});
        DrawText(renderer_,"B  EXIT",1058,648,2,accent);
    }
    void DrawPanelBase(Color accent) {
        SetColor(renderer_,{0,0,0,175}); SDL_RenderFillRect(renderer_,nullptr);
        FillRoundedRect(renderer_,{155,105,970,510},28,{10,17,29,250});
        FillRoundedRect(renderer_,{155,105,8,510},4,accent);
    }
    void DrawDetails(Color accent) {
        const GameEntry *game=Selected(); if (!game) return; DrawPanelBase(accent);
        DrawText(renderer_,"GAME DETAILS",205,150,4,{248,250,255,255});
        DrawText(renderer_,Ellipsize(game->name,48),205,218,3,accent);
        DrawText(renderer_,"PLATFORM",205,290,2,{113,130,156,255});
        DrawText(renderer_,PlatformName(game->platform),205,322,3,{235,241,250,255});
        DrawText(renderer_,"TITLE ID",560,290,2,{113,130,156,255});
        DrawText(renderer_,game->titleId.empty()?"NOT AVAILABLE":Ellipsize(game->titleId,28),560,322,3,{235,241,250,255});
        DrawText(renderer_,"SOURCE",850,290,2,{113,130,156,255});
        DrawText(renderer_,game->installed?(game->storage.empty()?"INSTALLED":Ellipsize(game->storage,12)):"FAT32",850,322,3,{235,241,250,255});
        DrawText(renderer_,"LOCATION",205,390,2,{113,130,156,255});
        DrawText(renderer_,Ellipsize(game->absolutePath,66),205,424,2,{202,212,226,255});
        DrawText(renderer_,"A  LAUNCH",205,530,2,{255,255,255,255});
        DrawText(renderer_,"X  FAVORITE",405,530,2,{255,255,255,255});
        DrawText(renderer_,"B  BACK",930,530,2,accent);
    }
    void DrawSettings(Color accent) {
        DrawPanelBase(accent);
        DrawText(renderer_,"SETTINGS",205,150,4,{248,250,255,255});
        DrawText(renderer_,"LIBRARY ROOTS",205,224,2,accent);
        DrawText(renderer_,"WII U   /WIIU/RAW-GAMES  +  /WIIU/GAMES",205,262,2,{210,220,233,255});
        DrawText(renderer_,"WII     /WBFS",205,300,2,{210,220,233,255});
        DrawText(renderer_,"GC      /GAMES",205,338,2,{210,220,233,255});
        DrawText(renderer_,"COVERS  /COVERS/GAMEID.PNG",205,390,2,{210,220,233,255});
        DrawText(renderer_,"AUTOBOOT AND LAUNCH OPTIONS WILL LIVE HERE",205,458,2,{131,146,169,255});
        DrawText(renderer_,"B  BACK",930,530,2,accent);
    }
    void DrawToast(Color accent) {
        const int width=std::min(820,TextWidth(status_,2)+64);
        FillRoundedRect(renderer_,{(kWidth-width)/2,568,width,42},16,{accent.r,accent.g,accent.b,235});
        DrawText(renderer_,Ellipsize(status_,62),kWidth/2,581,2,{4,12,22,255},true);
    }

    SDL_Renderer *renderer_;
    std::vector<GameEntry> &games_;
    TextureCache textures_;
    std::vector<std::size_t> visible_;
    std::size_t selected_ = 0;
    float visualSelected_ = 0.0f;
    int tab_ = 0;
    bool details_ = false, settings_ = false;
    std::string status_;
    Uint32 statusUntil_ = 0;
};

LaunchLooseFn LoadLaunchFunction() {
    OSDynLoad_Module module=nullptr;
    if (OSDynLoad_Acquire("homebrew_rpx_loader",&module)!=OS_DYNLOAD_OK) return nullptr;
    LaunchLooseFn function=nullptr;
    if (OSDynLoad_FindExport(module,OS_DYNLOAD_EXPORT_FUNC,"RL_LaunchLooseDirectory",
                             reinterpret_cast<void **>(&function))!=OS_DYNLOAD_OK) return nullptr;
    return function;
}

} // namespace

int main(int, char **) {
    WHBProcInit();
    const bool mounted=WHBMountSdCard()==1;
    if (SDL_Init(SDL_INIT_VIDEO)!=0) { if (mounted) WHBUnmountSdCard(); WHBProcShutdown(); return 1; }
    SDL_Window *window=SDL_CreateWindow("uFlow",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,kWidth,kHeight,SDL_WINDOW_SHOWN);
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):nullptr;
    if (!renderer) { if(window)SDL_DestroyWindow(window); SDL_Quit(); if(mounted)WHBUnmountSdCard(); WHBProcShutdown(); return 2; }
    SDL_RenderSetLogicalSize(renderer,kWidth,kHeight);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);

    ScanResult scan=mounted?ScanLibrary():ScanResult{};
    std::vector<GameEntry> games=std::move(scan.games);
    bool launchRequested=false;
    {
    Dashboard dashboard(renderer,games);
    dashboard.SetStatus(mounted?"LIBRARY READY  "+std::to_string(games.size())+" GAMES":"SOURCE DRIVE COULD NOT BE MOUNTED",mounted?3500:10000);
    const RPXLoaderStatus loaderStatus=mounted?RPXLoader_InitLibrary():RPX_LOADER_RESULT_NOT_AVAILABLE;
    LaunchLooseFn launchLoose=loaderStatus==RPX_LOADER_RESULT_SUCCESS?LoadLaunchFunction():nullptr;
    Uint32 previousTicks=SDL_GetTicks();

    while (WHBProcIsRunning()) {
        const Uint32 ticks=SDL_GetTicks();
        const float delta=std::min(.1f,(ticks-previousTicks)/1000.0f); previousTicks=ticks;
        VPADStatus input{}; VPADReadError error;
        if (VPADRead(VPAD_CHAN_0,&input,1,&error)>0&&error==VPAD_READ_SUCCESS) {
            if (input.trigger&VPAD_BUTTON_B) { if(dashboard.OverlayOpen())dashboard.CloseOverlay(); else break; }
            else if(input.trigger&VPAD_BUTTON_PLUS)dashboard.ToggleSettings();
            else if(input.trigger&VPAD_BUTTON_Y)dashboard.ToggleDetails();
            else if(input.trigger&VPAD_BUTTON_X)dashboard.ToggleFavorite();
            else if(input.trigger&VPAD_BUTTON_L)dashboard.ChangeTab(-1);
            else if(input.trigger&VPAD_BUTTON_R)dashboard.ChangeTab(1);
            else if(!dashboard.OverlayOpen()&&(input.trigger&VPAD_BUTTON_LEFT))dashboard.Move(-1);
            else if(!dashboard.OverlayOpen()&&(input.trigger&VPAD_BUTTON_RIGHT))dashboard.Move(1);
            else if(!dashboard.OverlayOpen()&&(input.trigger&VPAD_BUTTON_ZL))dashboard.Move(-10);
            else if(!dashboard.OverlayOpen()&&(input.trigger&VPAD_BUTTON_ZR))dashboard.Move(10);
            else if(!dashboard.OverlayOpen()&&(input.trigger&VPAD_BUTTON_MINUS)) {
                dashboard.SetStatus("SCANNING..."); dashboard.Render(delta);
                scan=ScanLibrary(); dashboard.ReplaceGames(std::move(scan.games));
                dashboard.SetStatus("SCAN COMPLETE  "+std::to_string(games.size())+" GAMES");
            } else if((input.trigger&VPAD_BUTTON_A)&&dashboard.Selected()) {
                const GameEntry &game=*dashboard.Selected();
                if(game.installed) {
                    dashboard.SetStatus("LAUNCHING "+Ellipsize(game.name,38),10000); dashboard.Render(delta);
                    SYSLaunchTitle(game.installedTitleId); launchRequested=true; break;
                } else if(game.platform!=Platform::WiiU) dashboard.SetStatus(std::string(PlatformName(game.platform))+" LAUNCH ADAPTER IS NEXT");
                else if(!launchLoose) dashboard.SetStatus("WII U LOADER MODULE IS NOT AVAILABLE",7000);
                else {
                    dashboard.SetStatus("LAUNCHING "+Ellipsize(game.name,38),10000); dashboard.Render(delta);
                    const RPXLoaderStatus result=launchLoose(game.rpxRelative.c_str(),game.contentRelative.c_str(),game.codeRelative.c_str(),
                            game.saveRelative.c_str(),game.name.c_str(),game.name.c_str(),game.publisher.c_str());
                    if(result==RPX_LOADER_RESULT_SUCCESS){launchRequested=true;break;}
                    dashboard.SetStatus(std::string("LAUNCH FAILED  ")+RPXLoader_GetStatusStr(result),10000);
                }
            }
        }
        dashboard.Render(delta);
        OSSleepTicks(OSMillisecondsToTicks(4));
    }
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    if(!launchRequested&&mounted)WHBUnmountSdCard();
    WHBProcShutdown();
    return 0;
}
