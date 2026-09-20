#pragma once
#include <SDL2/SDL.h>
#include <png.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

// Called before Present: no background writer, hooks, or gameplay polling.
inline bool SaveVisualFrame(SDL_Renderer *renderer, const std::string &path) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) || width <= 0 || height <= 0 ||
        width > 4096 || height > 2160) return false;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4)) return false;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width; image.height = height; image.format = PNG_FORMAT_RGBA;
    const bool success = png_image_write_to_file(&image, path.c_str(), 0, pixels.data(), width * 4, nullptr) != 0;
    png_image_free(&image);
    return success;
}

#ifndef UFLOW_HOST_PREVIEW
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class VisualRemote {
    int socket_ = -1;
    std::string token_;
    sockaddr_in peer_{};
public:
    VisualRemote() {
        char token[34]{};
        FILE *file = std::fopen("fs:/vol/external01/wiiu/apps/uFlow/visual-check.key", "rb");
        if (!file) return;
        size_t length = std::fread(token, 1, 33, file);
        std::fclose(file);
        if (length == 33 && token[32] == '\n') { token[32] = 0; length = 32; }
        if (length != 32 || std::strspn(token, "0123456789abcdef") != 32) return;
        token_ = token;
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ < 0) return;
        int nonblocking = 1;
        sockaddr_in address{};
        address.sin_family = AF_INET; address.sin_port = htons(32180); address.sin_addr.s_addr = INADDR_ANY;
        if (setsockopt(socket_, SOL_SOCKET, SO_NONBLOCK, &nonblocking, sizeof(nonblocking)) ||
            bind(socket_, reinterpret_cast<sockaddr *>(&address), sizeof(address))) Stop();
    }
    ~VisualRemote() { Stop(); }
    void Stop() { if (socket_ >= 0) close(socket_); socket_ = -1; }
    std::string Poll() {
        if (socket_ < 0) return {};
        char packet[128]{};
        socklen_t length = sizeof(peer_);
        const int count = recvfrom(socket_, packet, sizeof(packet)-1, MSG_DONTWAIT,
                                   reinterpret_cast<sockaddr *>(&peer_), &length);
        if (count <= 33 || std::string(packet, 32) != token_ || packet[32] != ' ') return {};
        return std::string(packet + 33, count - 33);
    }
    void Reply(bool success) {
        const char *text = success ? "OK visual.png" : "ERROR capture";
        if (socket_ >= 0) sendto(socket_, text, std::strlen(text), MSG_DONTWAIT,
                                reinterpret_cast<sockaddr *>(&peer_), sizeof(peer_));
    }
    void ReplyStats(const std::string &text) {
        if (socket_ >= 0) sendto(socket_, text.data(), text.size(), MSG_DONTWAIT,
                                reinterpret_cast<sockaddr *>(&peer_), sizeof(peer_));
    }
};
#endif
