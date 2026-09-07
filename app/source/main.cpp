#include "library.hpp"
#include "settings.hpp"
#include "case_model.hpp"

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

Color AccentFor(Platform platform, int theme = 0) {
    if (theme == 1) return {0, 208, 230, 255};
    if (theme == 2) return {168, 112, 255, 255};
    if (theme == 3) return {255, 166, 58, 255};
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

void DrawWrappedText(SDL_Renderer *renderer, const std::string &text, int x, int y, int scale,
                     Color color, std::size_t charactersPerLine, int maximumLines) {
    std::string line, word;
    std::vector<std::string> lines;
    const auto flushWord = [&]() {
        if (word.empty()) return;
        if (!line.empty() && line.size() + word.size() + 1 > charactersPerLine) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        word.clear();
    };
    for (char character : text) {
        if (character == ' ' || character == '\n') {
            flushWord();
            if (character == '\n' && !line.empty()) { lines.push_back(line); line.clear(); }
        } else word += character;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    if (static_cast<int>(lines.size()) > maximumLines) {
        lines.resize(maximumLines);
        lines.back() = Ellipsize(lines.back(), charactersPerLine);
    }
    for (std::size_t index = 0; index < lines.size(); ++index)
        DrawText(renderer, Ellipsize(lines[index], charactersPerLine), x,
                 y + static_cast<int>(index) * (9 * scale), scale, color);
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
        : renderer_(renderer), games_(games), textures_(renderer), preferences_(LoadSettings()) {
        tab_ = preferences_.startTab;
        RebuildVisible();
    }

    void ReplaceGames(std::vector<GameEntry> games) {
        textures_.Clear(); games_ = std::move(games); selected_ = 0; visualSelected_ = 0.0f; RebuildVisible();
    }
    void SetStatus(std::string status, Uint32 lifetime = 3500) {
        status_ = std::move(status); statusUntil_ = SDL_GetTicks() + lifetime;
    }
    const GameEntry *Selected() const { return visible_.empty() ? nullptr : &games_[visible_[selected_]]; }
    void Move(int amount) {
        if (visible_.empty()) return;
        int next = static_cast<int>(selected_) + amount;
        if (preferences_.wrapNavigation && visible_.size() > 1) {
            const int count = static_cast<int>(visible_.size());
            next = (next % count + count) % count;
            if (std::abs(next - static_cast<int>(selected_)) > count / 2) visualSelected_ = static_cast<float>(next);
        } else {
            next = std::clamp(next, 0, static_cast<int>(visible_.size()) - 1);
        }
        selected_ = static_cast<std::size_t>(next);
    }
    void ChangeTab(int amount) {
        tab_ = (tab_ + amount + static_cast<int>(kTabNames.size())) % static_cast<int>(kTabNames.size());
        selected_ = 0; visualSelected_ = 0.0f; tabSlide_ = amount * 150.0f; details_ = false; RebuildVisible();
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
    bool SettingsOpen() const { return settings_; }
    bool DetailsOpen() const { return details_; }
    void CloseOverlay() { details_ = settings_ = false; }

    void SettingsChangeCategory(int amount) {
        settingsCategory_ = (settingsCategory_ + amount + 5) % 5;
        settingsRow_ = 0;
    }
    void SettingsMoveRow(int amount) {
        const int rows = SettingsRowCount();
        if (rows == 0) return;
        settingsRow_ = (settingsRow_ + amount + rows) % rows;
    }
    void SettingsAdjust(int amount) {
        if (amount == 0 || settingsCategory_ == 4) return;
        if (settingsCategory_ == 0) {
            if (settingsRow_ == 0) preferences_.startTab = (preferences_.startTab + amount + 5) % 5;
            else preferences_.wrapNavigation = !preferences_.wrapNavigation;
        } else if (settingsCategory_ == 1) {
            bool *values[] = {&preferences_.showInstalled, &preferences_.showRawWiiU,
                              &preferences_.showWii, &preferences_.showGameCube};
            *values[settingsRow_] = !*values[settingsRow_];
            RebuildVisible();
        } else if (settingsCategory_ == 2) {
            bool *values[] = {&preferences_.useBoxArt, &preferences_.useBackgrounds,
                              &preferences_.useLogos, &preferences_.usePreviews};
            *values[settingsRow_] = !*values[settingsRow_];
        } else if (settingsCategory_ == 3) {
            if (settingsRow_ == 0) preferences_.coverLayout = (preferences_.coverLayout + amount + 4) % 4;
            else if (settingsRow_ == 1) preferences_.animationSpeed = (preferences_.animationSpeed + amount + 3) % 3;
            else if (settingsRow_ == 2) preferences_.coverSpacing = (preferences_.coverSpacing + amount + 3) % 3;
            else if (settingsRow_ == 3) preferences_.backgroundMotion = !preferences_.backgroundMotion;
            else preferences_.theme = (preferences_.theme + amount + 4) % 4;
        }
        SaveSettings(preferences_);
    }

    void Render(float deltaSeconds) {
        const float response = preferences_.animationSpeed == 0 ? 6.5f : preferences_.animationSpeed == 1 ? 11.0f : 17.0f;
        const float step = std::min(1.0f, deltaSeconds * response);
        visualSelected_ += (static_cast<float>(selected_) - visualSelected_) * step;
        tabSlide_ += (0.0f - tabSlide_) * step;
        detailsProgress_ += ((details_ ? 1.0f : 0.0f) - detailsProgress_) * step;
        settingsProgress_ += ((settings_ ? 1.0f : 0.0f) - settingsProgress_) * step;
        const GameEntry *selected = Selected();
        const Color accent = selected ? AccentFor(selected->platform, preferences_.theme)
                                      : AccentFor(Platform::WiiU, preferences_.theme);
        DrawBackground(accent, selected); DrawHeader(accent);
        if (visible_.empty()) DrawEmpty(accent); else DrawCarousel(accent);
        DrawFooter(accent);
        if (detailsProgress_ > .01f) DrawDetails(accent, detailsProgress_);
        if (settingsProgress_ > .01f) DrawSettings(accent, settingsProgress_);
        if (!status_.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), statusUntil_)) status_.clear();
        if (!status_.empty()) DrawToast(accent);
        SDL_RenderPresent(renderer_);
    }

private:
    int SettingsRowCount() const {
        static constexpr int counts[] = {2, 4, 4, 5, 0};
        return counts[settingsCategory_];
    }
    void RebuildVisible() {
        visible_.clear();
        for (std::size_t index = 0; index < games_.size(); ++index) {
            const auto &game = games_[index];
            if (game.installed && !preferences_.showInstalled) continue;
            if (!game.installed && game.platform == Platform::WiiU && !preferences_.showRawWiiU) continue;
            if (game.platform == Platform::Wii && !preferences_.showWii) continue;
            if (game.platform == Platform::GameCube && !preferences_.showGameCube) continue;
            if (tab_ == 0 || (tab_ == 1 && game.platform == Platform::WiiU) ||
                (tab_ == 2 && game.platform == Platform::Wii) ||
                (tab_ == 3 && game.platform == Platform::GameCube) || (tab_ == 4 && game.favorite)) visible_.push_back(index);
        }
        selected_ = visible_.empty() ? 0 : std::min(selected_, visible_.size() - 1);
    }
    void DrawBackground(Color accent, const GameEntry *game) {
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
                 float turnScale = .82f) {
        const TextureInfo artwork = textures_.Get(game.coverPath);
        if (!artwork.texture) { DrawCover(game, rect, selected, accent); return; }

        struct Transformed { SDL_FPoint point; float depth; };
        struct Face { std::uint16_t a, b, c; float depth; };
        constexpr float caseWidth = 13.5f / 19.0f;
        constexpr float caseDepth = 1.4f / 19.0f;
        const float turn = std::clamp(relative, -1.0f, 1.0f);
        const float idle = selected ? std::sin(SDL_GetTicks() / 1250.0f) * .025f : 0.0f;
        const float angle = -turn * turnScale + idle;
        const float sine = std::sin(angle), cosine = std::cos(angle);
        const float pixelScale = std::min(rect.w / caseWidth, rect.h / 1.023f);
        const float centerX = rect.x + rect.w * .5f;
        const float centerY = rect.y + rect.h * .5f;
        constexpr float camera = 2.45f;
        const auto transform = [&](float x, float y, float z) {
            x *= caseWidth; z *= caseDepth;
            const float rotatedX = x * cosine + z * sine;
            const float rotatedZ = -x * sine + z * cosine;
            const float perspective = camera / (camera - rotatedZ);
            return Transformed{{centerX + rotatedX * pixelScale * perspective,
                                centerY - y * pixelScale * perspective}, rotatedZ};
        };

        std::array<SDL_Vertex, CaseModel::kPositions.size()> vertices{};
        std::array<float, CaseModel::kPositions.size()> depths{};
        for (std::size_t index = 0; index < CaseModel::kPositions.size(); ++index) {
            const auto &position = CaseModel::kPositions[index];
            const auto projected = transform(position.x, position.y, position.z);
            vertices[index].position = projected.point;
            depths[index] = projected.depth;
            const auto &normal = CaseModel::kNormals[index];
            const float normalX = normal.x * cosine + normal.z * sine;
            const float normalZ = -normal.x * sine + normal.z * cosine;
            const float light = std::clamp(.38f + std::max(0.0f, normalX * -.25f + normal.y * -.18f + normalZ * .95f) * .62f,
                                           .30f, 1.0f);
            const Color plastic = Mix({15, 31, 50, 255}, accent, selected ? .68f : .48f);
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

        SDL_Rect shadow = {rect.x + 15, rect.y + rect.h - 4, rect.w - 12, 20};
        FillRoundedRect(renderer_, shadow, 9, {0, 0, 0, selected ? Uint8(115) : Uint8(70)});
        SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                           sortedIndices.data(), static_cast<int>(sortedIndices.size()));

        const auto topLeft = transform(-.478f, .482f, .515f);
        const auto topRight = transform(.478f, .482f, .515f);
        const auto bottomRight = transform(.478f, -.482f, .515f);
        const auto bottomLeft = transform(-.478f, -.482f, .515f);
        SDL_Vertex sleeve[4]{};
        sleeve[0].position = topLeft.point; sleeve[0].tex_coord = {0, 0};
        sleeve[1].position = topRight.point; sleeve[1].tex_coord = {1, 0};
        sleeve[2].position = bottomRight.point; sleeve[2].tex_coord = {1, 1};
        sleeve[3].position = bottomLeft.point; sleeve[3].tex_coord = {0, 1};
        const Uint8 brightness = selected ? 255 : static_cast<Uint8>(225 - std::min(70.0f, std::abs(relative) * 22.0f));
        for (auto &vertex : sleeve) vertex.color = {brightness, brightness, brightness, 255};
        const int sleeveIndices[] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, artwork.texture, sleeve, 4, sleeveIndices, 6);
        if (selected) {
            SetColor(renderer_, accent);
            const SDL_FPoint outline[] = {topLeft.point, topRight.point, bottomRight.point, bottomLeft.point, topLeft.point};
            SDL_RenderDrawLinesF(renderer_, outline, 5);
        }
        if (game.favorite) DrawText(renderer_, "*", rect.x + rect.w - 32, rect.y + 8, 3, {255,210,66,255});
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
                sideWidth = 135.0f; sideHeight = 193.0f; turnScale = 0.0f;
            } else if (preferences_.coverLayout == 3) {
                sideWidth = 150.0f; sideHeight = 214.0f; turnScale = .28f;
            }
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
        FillRoundedRect(renderer_, {250,505,780,3}, 1, {accent.r,accent.g,accent.b,105});
        const TextureInfo logo=preferences_.useLogos?textures_.Get(game.logoPath):TextureInfo{};
        if(logo.texture){
            const float ratio=static_cast<float>(logo.width)/std::max(1,logo.height);
            SDL_Rect logoRect={490,516,300,54};
            if(ratio>static_cast<float>(logoRect.w)/logoRect.h){logoRect.h=static_cast<int>(logoRect.w/ratio);logoRect.y+=(54-logoRect.h)/2;}
            else{logoRect.w=static_cast<int>(logoRect.h*ratio);logoRect.x+=(300-logoRect.w)/2;}
            SDL_RenderCopy(renderer_,logo.texture,nullptr,&logoRect);
        }else DrawText(renderer_, Ellipsize(game.name,50), 640, 525, 3, {246,249,255,255}, true);
        std::string summary = std::string(PlatformName(game.platform)) + "  /  " +
                              (game.installed ? (game.storage.empty() ? "INSTALLED" : game.storage) : "FAT32") +
                              "  /  " + std::to_string(selected_+1) + " OF " + std::to_string(visible_.size());
        DrawText(renderer_, Ellipsize(summary,75), 640, 570, 2, accent, true);
    }
    void DrawFooter(Color accent) {
        FillRoundedRect(renderer_,{38,626,1204,64},20,{5,10,20,205});
        DrawText(renderer_,"A  LAUNCH",65,648,2,{235,241,250,255});
        DrawText(renderer_,"Y  DETAILS",235,648,2,{173,184,201,255});
        DrawText(renderer_,"X  FAVORITE",430,648,2,{173,184,201,255});
        DrawText(renderer_,"L/R  FILTER",650,648,2,{173,184,201,255});
        DrawText(renderer_,"+  SETTINGS",860,648,2,{173,184,201,255});
        DrawText(renderer_,"B  EXIT",1083,648,2,accent);
    }
    void DrawPanelBase(Color accent, int x, int width, float progress) {
        SetColor(renderer_,{0,0,0,static_cast<Uint8>(190.0f*progress)}); SDL_RenderFillRect(renderer_,nullptr);
        FillRoundedRect(renderer_,{x,92,width,536},28,{10,17,29,250});
        FillRoundedRect(renderer_,{x,92,8,536},4,accent);
    }
    void DrawDetails(Color accent, float progress) {
        const GameEntry *game = Selected(); if (!game) return;
        const int x = static_cast<int>(80 + (1.0f - progress) * 1240.0f);
        DrawPanelBase(accent, x, 1120, progress);
        DrawText(renderer_, "TITLE DETAILS", x + 48, 120, 2, {142,157,179,255});

        const SDL_Rect cover = {x + 58, 166, 250, 357};
        if (preferences_.useBoxArt) DrawBox(*game, cover, -.16f, true, accent, .38f);
        else DrawCover(*game, cover, true, accent);

        const int infoX = x + 365;
        const TextureInfo logo = preferences_.useLogos ? textures_.Get(game->logoPath) : TextureInfo{};
        if (logo.texture) {
            const float ratio = static_cast<float>(logo.width) / std::max(1, logo.height);
            SDL_Rect logoRect = {infoX, 120, 480, 78};
            if (ratio > static_cast<float>(logoRect.w) / logoRect.h) {
                logoRect.h = static_cast<int>(logoRect.w / ratio); logoRect.y += (78 - logoRect.h) / 2;
            } else {
                logoRect.w = static_cast<int>(logoRect.h * ratio);
            }
            SDL_RenderCopy(renderer_, logo.texture, nullptr, &logoRect);
        } else DrawText(renderer_, Ellipsize(game->name, 38), infoX, 143, 4, {248,250,255,255});

        const std::string creator = !game->developer.empty() ? game->developer : game->publisher;
        if (!creator.empty()) DrawText(renderer_, Ellipsize(creator, 55), infoX, 217, 2, accent);
        const std::string source = game->installed ? (game->storage.empty() ? "INSTALLED" : game->storage) : "FAT32";
        DrawText(renderer_, Ellipsize(game->name, 56), infoX, 251, 2, {224,231,241,255});

        const auto badge = [&](int offset, const char *label, const std::string &value) {
            FillRoundedRect(renderer_, {infoX + offset, 285, 151, 58}, 12, {4,10,19,205});
            DrawText(renderer_, label, infoX + offset + 12, 296, 1, {99,116,142,255});
            DrawText(renderer_, Ellipsize(value.empty() ? "UNKNOWN" : value, 18), infoX + offset + 12, 316, 2,
                     {235,241,250,255});
        };
        badge(0, "PLATFORM", PlatformName(game->platform));
        badge(162, "YEAR", game->releaseYear.empty() ? "----" : game->releaseYear);
        badge(324, "REGION", game->region);
        badge(486, "PLAYERS", game->players.empty() ? "--" : game->players);

        DrawText(renderer_, "OVERVIEW", infoX, 374, 2, accent);
        const std::string description = game->description.empty()
                                            ? "No synopsis is available for this title yet. Add one to its metadata file."
                                            : game->description;
        DrawWrappedText(renderer_, description, infoX, 405, 2, {174,186,204,255}, 55, 4);
        std::string footer = (game->genre.empty() ? "GENRE UNKNOWN" : game->genre) + "  /  " + source;
        if (!game->rating.empty()) footer += "  /  RATING " + game->rating;
        DrawText(renderer_, Ellipsize(footer, 72), infoX, 493, 1, {111,130,157,255});
        if (!game->titleId.empty()) DrawText(renderer_, "TITLE ID  " + Ellipsize(game->titleId, 24), infoX, 515, 1, {83,101,128,255});

        FillRoundedRect(renderer_, {x + 38, 548, 1044, 1}, 1, {accent.r,accent.g,accent.b,110});
        FillRoundedRect(renderer_, {x + 52, 568, 176, 42}, 14, accent);
        DrawText(renderer_, "A  LAUNCH", x + 140, 582, 2, {4,12,22,255}, true);
        DrawText(renderer_, game->favorite ? "X  REMOVE FAVORITE" : "X  ADD FAVORITE", x + 270, 582, 2, {225,232,242,255});
        DrawText(renderer_, "Y  CLOSE", x + 720, 582, 2, {170,183,202,255});
        DrawText(renderer_, "B  BACK", x + 930, 582, 2, accent);
    }
    void DrawSettings(Color accent, float progress) {
        static constexpr const char *categories[]={"GENERAL","LIBRARY","MEDIA","DISPLAY","ABOUT"};
        const int x=static_cast<int>(70+(1.0f-progress)*1260.0f);
        DrawPanelBase(accent,x,1140,progress);
        DrawText(renderer_,"SETTINGS",x+48,126,4,{248,250,255,255});
        DrawText(renderer_,"UFLOW CONTROL CENTER",x+830,139,2,{116,133,158,255});
        FillRoundedRect(renderer_,{x+38,182,240,390},18,{5,11,21,220});
        for(int index=0;index<5;++index){
            const int y=208+index*61;
            if(index==settingsCategory_)FillRoundedRect(renderer_,{x+52,y-16,212,46},14,{accent.r,accent.g,accent.b,48});
            DrawText(renderer_,categories[index],x+74,y,index==settingsCategory_?3:2,
                     index==settingsCategory_?Color{255,255,255,255}:Color{128,143,166,255});
        }
        const int contentX=x+320;
        const auto drawRow=[&](int row,const std::string &label,const std::string &value){
            const int y=205+row*76;
            if(row==settingsRow_)FillRoundedRect(renderer_,{contentX-18,y-18,760,57},14,{accent.r,accent.g,accent.b,42});
            DrawText(renderer_,label,contentX,y,2,row==settingsRow_?Color{255,255,255,255}:Color{188,199,216,255});
            DrawText(renderer_,"<  "+value+"  >",contentX+710,y,2,row==settingsRow_?accent:Color{126,142,166,255},true);
        };
        if(settingsCategory_==0){
            drawRow(0,"START VIEW",kTabNames[preferences_.startTab]);
            drawRow(1,"WRAP NAVIGATION",OnOff(preferences_.wrapNavigation));
            DrawText(renderer_,"CHOOSE THE FIRST LIBRARY VIEW AND EDGE BEHAVIOR.",contentX,405,2,{107,125,150,255});
        }else if(settingsCategory_==1){
            drawRow(0,"INSTALLED WII U",OnOff(preferences_.showInstalled));
            drawRow(1,"RAW WII U",OnOff(preferences_.showRawWiiU));
            drawRow(2,"WII GAMES",OnOff(preferences_.showWii));
            drawRow(3,"GAMECUBE GAMES",OnOff(preferences_.showGameCube));
            const char *paths[]={"MLC + WII U USB STORAGE","/WIIU/RAW-GAMES + /WIIU/GAMES","/WBFS","/GAMES"};
            DrawText(renderer_,"PATH",contentX,518,1,{91,109,135,255});
            DrawText(renderer_,paths[settingsRow_],contentX,542,2,accent);
        }else if(settingsCategory_==2){
            drawRow(0,"3D BOX ART",OnOff(preferences_.useBoxArt));
            drawRow(1,"BACKGROUNDS",OnOff(preferences_.useBackgrounds));
            drawRow(2,"TITLE LOGOS",OnOff(preferences_.useLogos));
            drawRow(3,"GAMEPLAY PREVIEWS",OnOff(preferences_.usePreviews));
            const char *paths[]={"/MEDIA/COVERS","/MEDIA/BACKGROUNDS","/MEDIA/LOGOS","/MEDIA/PREVIEWS"};
            DrawText(renderer_,"DIRECTORY",contentX,518,1,{91,109,135,255});
            DrawText(renderer_,paths[settingsRow_],contentX,542,2,accent);
        }else if(settingsCategory_==3){
            static constexpr const char *layouts[]={"CLASSIC FLOW","CAROUSEL","FLAT ROW","STACKED"};
            static constexpr const char *speeds[]={"RELAXED","SMOOTH","FAST"};
            static constexpr const char *spacing[]={"COMPACT","BALANCED","WIDE"};
            static constexpr const char *themes[]={"PLATFORM","CYAN","VIOLET","AMBER"};
            drawRow(0,"COVER LAYOUT",layouts[preferences_.coverLayout]);
            drawRow(1,"ANIMATION",speeds[preferences_.animationSpeed]);
            drawRow(2,"COVER SPACING",spacing[preferences_.coverSpacing]);
            drawRow(3,"BACKGROUND MOTION",OnOff(preferences_.backgroundMotion));
            drawRow(4,"ACCENT THEME",themes[preferences_.theme]);
        }else{
            DrawText(renderer_,"UFLOW",contentX,216,5,accent);
            DrawText(renderer_,"A UNIFIED WII U LIBRARY EXPERIENCE",contentX,284,2,{222,229,239,255});
            DrawText(renderer_,"INSTALLED WII U + RAW WII U + WII + GAMECUBE",contentX,330,2,{145,160,182,255});
            DrawText(renderer_,"SETTINGS ARE SAVED TO /WIIU/APPS/UFLOW/UFLOW.CFG",contentX,388,2,{145,160,182,255});
            DrawText(renderer_,"BUILD 0.1 DEVELOPMENT",contentX,454,2,{102,120,146,255});
        }
        FillRoundedRect(renderer_,{x+38,582,1064,1},1,{accent.r,accent.g,accent.b,80});
        DrawText(renderer_,"L/R  SECTION",x+56,600,2,{211,220,233,255});
        DrawText(renderer_,"UP/DOWN  OPTION",x+295,600,2,{211,220,233,255});
        DrawText(renderer_,"LEFT/RIGHT  CHANGE",x+590,600,2,{211,220,233,255});
        DrawText(renderer_,"B  BACK",x+952,600,2,accent);
    }
    void DrawToast(Color accent) {
        const int width=std::min(820,TextWidth(status_,2)+64);
        FillRoundedRect(renderer_,{(kWidth-width)/2,568,width,42},16,{accent.r,accent.g,accent.b,235});
        DrawText(renderer_,Ellipsize(status_,62),kWidth/2,581,2,{4,12,22,255},true);
    }

    SDL_Renderer *renderer_;
    std::vector<GameEntry> &games_;
    TextureCache textures_;
    UserSettings preferences_;
    std::vector<std::size_t> visible_;
    std::size_t selected_ = 0;
    float visualSelected_ = 0.0f;
    float tabSlide_ = 0.0f;
    float detailsProgress_ = 0.0f;
    float settingsProgress_ = 0.0f;
    int tab_ = 0;
    int settingsCategory_ = 0;
    int settingsRow_ = 0;
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
    int heldHorizontal=0;
    int heldVertical=0;
    Uint32 horizontalStarted=0;
    Uint32 horizontalNext=0;

    while (WHBProcIsRunning()) {
        const Uint32 ticks=SDL_GetTicks();
        const float delta=std::min(.1f,(ticks-previousTicks)/1000.0f); previousTicks=ticks;
        VPADStatus input{}; VPADReadError error;
        if (!launchRequested && VPADRead(VPAD_CHAN_0,&input,1,&error)>0&&error==VPAD_READ_SUCCESS) {
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
            if(verticalEdge)heldVertical=vertical;
            if(!dashboard.OverlayOpen()&&horizontal!=0){
                if(horizontalEdge)dashboard.Move(horizontal);
                else if(SDL_TICKS_PASSED(ticks,horizontalNext)){
                    const Uint32 elapsed=ticks-horizontalStarted;
                    const int stride=elapsed>2600?4:elapsed>1500?2:1;
                    dashboard.Move(horizontal*stride);
                    const Uint32 interval=elapsed>2600?38:elapsed>1500?62:105;
                    horizontalNext=ticks+interval;
                }
            }
            if (input.trigger&VPAD_BUTTON_B) { if(dashboard.OverlayOpen())dashboard.CloseOverlay(); else break; }
            else if(dashboard.SettingsOpen()) {
                if(input.trigger&VPAD_BUTTON_PLUS)dashboard.CloseOverlay();
                else if(input.trigger&VPAD_BUTTON_L)dashboard.SettingsChangeCategory(-1);
                else if(input.trigger&VPAD_BUTTON_R)dashboard.SettingsChangeCategory(1);
                else if(verticalEdge&&vertical)dashboard.SettingsMoveRow(vertical);
                else if(horizontalEdge&&horizontal)dashboard.SettingsAdjust(horizontal);
                else if(input.trigger&VPAD_BUTTON_A)dashboard.SettingsAdjust(1);
            }
            else if(input.trigger&VPAD_BUTTON_PLUS)dashboard.ToggleSettings();
            else if(input.trigger&VPAD_BUTTON_Y)dashboard.ToggleDetails();
            else if(input.trigger&VPAD_BUTTON_X)dashboard.ToggleFavorite();
            else if(input.trigger&VPAD_BUTTON_L)dashboard.ChangeTab(-1);
            else if(input.trigger&VPAD_BUTTON_R)dashboard.ChangeTab(1);
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
                    std::string launchPath=game.absolutePath;
                    if(launchPath.rfind("fs:",0)==0)launchPath.erase(0,3);
                    _SYSLaunchTitleByPathFromLauncher(launchPath.c_str(),static_cast<uint32_t>(launchPath.size()));
                    launchRequested=true;
                } else if(game.platform!=Platform::WiiU) dashboard.SetStatus(std::string(PlatformName(game.platform))+" LAUNCH ADAPTER IS NEXT");
                else if(!launchLoose) dashboard.SetStatus("WII U LOADER MODULE IS NOT AVAILABLE",7000);
                else {
                    dashboard.SetStatus("LAUNCHING "+Ellipsize(game.name,38),10000); dashboard.Render(delta);
                    const RPXLoaderStatus result=launchLoose(game.rpxRelative.c_str(),game.contentRelative.c_str(),game.codeRelative.c_str(),
                            game.saveRelative.c_str(),game.name.c_str(),game.name.c_str(),game.publisher.c_str());
                    if(result==RPX_LOADER_RESULT_SUCCESS)launchRequested=true;
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
