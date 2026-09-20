#pragma once

// Shared with the small WiiFlow entry-point patch. No Nintendo SDK dependencies.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <sys/stat.h>

namespace WiiRequest {
constexpr const char *Magic = "UFLOW-WII-1";
constexpr const char *CafeRequest = "fs:/vol/external01/wiiu/apps/uFlow/wii-launch.req";
constexpr const char *WiiRequestPath = "sd:/wiiu/apps/uFlow/wii-launch.req";
constexpr const char *BackendMarker = "UFLOW_WII_PATH_BACKEND_V1";
constexpr const char *GcMagic = "UFLOW-GC-1";
constexpr const char *GcBackendMarker = "UFLOW_GC_PATH_BACKEND_V1";
enum class Kind { Wii, GameCube };
enum class Launcher { WiiFlow, USBLoaderGX };
constexpr const char *SelectorMarker = "UFLOW_LAUNCHER_SELECT_V1";
constexpr const char *UsbMarker = "UFLOW_USBLOADER_PATH_V1";
constexpr const char *UsbRequestPath = "sd:/wiiu/apps/uFlow/usb-launch.req";
struct Request { std::string id, path; Kind kind = Kind::Wii; Launcher launcher = Launcher::WiiFlow; };

inline bool ValidId(const std::string &id) {
    if (id.size() != 6) return false;
    for (unsigned char c : id) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

inline bool ValidPath(const std::string &path, Kind kind = Kind::Wii) {
    if (path.rfind("sd:/", 0) != 0 || path.size() >= 256 || path.size() < 10) return false;
    if (kind != Kind::Wii && kind != Kind::GameCube) return false;
    const size_t suffixSize = kind == Kind::GameCube ? 4 : 5;
    std::string suffix = path.substr(path.size() - suffixSize);
    for (char &c : suffix) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (suffix != (kind == Kind::GameCube ? ".iso" : ".wbfs")) return false;
    for (unsigned char c : path) if (c < 32 || c == 127 || c == '\\') return false;
    if (path.find(':', 3) != std::string::npos) return false;
    for (size_t start = 4; start < path.size();) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const auto part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

inline std::string Encode(const Request &request) {
    if (!ValidId(request.id) || !ValidPath(request.path, request.kind)) return {};
    if(request.launcher != Launcher::WiiFlow && request.launcher != Launcher::USBLoaderGX) return {};
    const char *magic = request.launcher == Launcher::USBLoaderGX ?
        (request.kind == Kind::GameCube ? "UFLOW-USB-GC-1" : "UFLOW-USB-WII-1") :
        (request.kind == Kind::GameCube ? GcMagic : Magic);
    return std::string(magic) + "\n" + request.id + "\n" + request.path + "\n";
}

inline bool Decode(const std::string &text, Request &out) {
    out = {};
    if (text.size() > 280 || text.find('\0') != std::string::npos) return false;
    const bool usb = text.rfind("UFLOW-USB-",0)==0;
    const Kind kind = text.rfind(usb ? "UFLOW-USB-GC-1\n" : "UFLOW-GC-1\n", 0) == 0 ? Kind::GameCube : Kind::Wii;
    const std::string prefix = std::string(usb ? (kind==Kind::GameCube ? "UFLOW-USB-GC-1" : "UFLOW-USB-WII-1") : (kind == Kind::GameCube ? GcMagic : Magic)) + "\n";
    if (text.rfind(prefix, 0) != 0) return false;
    const size_t split = text.find('\n', prefix.size());
    if (split == std::string::npos || text.back() != '\n') return false;
    Request request{text.substr(prefix.size(), split - prefix.size()),
                    text.substr(split + 1, text.size() - split - 2), kind, usb ? Launcher::USBLoaderGX : Launcher::WiiFlow};
    if (!ValidId(request.id) || !ValidPath(request.path, kind)) return false;
    out = request;
    return true;
}

// Verify metadata only, never read/write the entire game. Opening rb+ mirrors
// WiiFlow's O_RDWR requirement but this function never writes game bytes.
inline std::string CheckGame(const std::string &path, const std::string &id,
                             std::string *title = nullptr) {
    if (!ValidId(id)) return "INVALID WII GAME ID";
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return "WII GAME FILE IS MISSING";
    if (!(info.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) return "WII GAME IS READ-ONLY - CLEAR ITS READ-ONLY FLAG";
    FILE *file = std::fopen(path.c_str(), "rb+");
    if (!file) return "CANNOT OPEN WII GAME READ/WRITE - CHECK READ-ONLY FLAG";
    unsigned char header[32]{};
    bool valid = std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
        std::memcmp(header, "WBFS", 4) == 0 && header[8] >= 9 && header[8] <= 12 &&
        header[9] >= header[8] && header[9] <= 31 && header[12] == 1;
    unsigned char disc[96]{};
    if (valid) valid = std::fseek(file, 1L << header[8], SEEK_SET) == 0 &&
        std::fread(disc, 1, sizeof(disc), file) == sizeof(disc) &&
        std::memcmp(disc, id.data(), 6) == 0 &&
        disc[0x18] == 0x5d && disc[0x19] == 0x1c && disc[0x1a] == 0x9e && disc[0x1b] == 0xa3;
    std::fclose(file);
    if (!valid) return "WBFS HEADER OR GAME ID DOES NOT MATCH - RESCAN LIBRARY";
    if (title) {
        size_t length = 0;
        while (length < 64 && disc[32 + length]) ++length;
        title->assign(reinterpret_cast<char *>(disc + 32), length);
    }
    return {};
}

// Plain GameCube ISO only. Read-only access is sufficient (unlike Wii WBFS).
// These are header/range checks, not a full game-integrity verification.
inline std::string CheckGameCube(const std::string &path, const std::string &expectedId,
                                 std::string *title = nullptr, std::string *actualId = nullptr) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return "GAMECUBE ISO IS MISSING";
    unsigned char header[0x440]{};
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) return "CANNOT READ GAMECUBE ISO";
    const bool read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
    std::fclose(file);
    if (!read || std::memcmp(header + 0x1c, "\xc2\x33\x9f\x3d", 4) != 0)
        return "NOT A PLAIN GAMECUBE ISO - CONVERT RVZ/CISO TO ISO";
    const std::string id(reinterpret_cast<char *>(header), 6);
    if (!ValidId(id) || (!expectedId.empty() && expectedId != id))
        return "GAMECUBE HEADER OR GAME ID DOES NOT MATCH - RESCAN LIBRARY";
    if (header[6] != 0) return "SELECT DISC 1 (game.iso), NOT disc2.iso";
    const auto word = [&](size_t p) -> uint32_t {
        return uint32_t(header[p])<<24 | uint32_t(header[p+1])<<16 |
               uint32_t(header[p+2])<<8 | uint32_t(header[p+3]);
    };
    const uint64_t size = static_cast<uint64_t>(info.st_size);
    const uint32_t dol = word(0x420), fst = word(0x424), fstSize = word(0x428);
    if (dol < 0x2440 || uint64_t(dol)+0x100 > size || fst < 0x2440 ||
        fstSize < 12 || uint64_t(fst)+fstSize > size)
        return "GAMECUBE ISO IS TRUNCATED OR HAS INVALID DOL/FST OFFSETS";
    if (actualId) *actualId = id;
    if (title) {
        size_t length = 0;
        while (length < 64 && header[32+length]) ++length;
        title->assign(reinterpret_cast<char *>(header+32), length);
    }
    return {};
}

// Consume before handing off: returning to the forwarder must not relaunch the
// previous game. Malformed requests are consumed too; failed removal blocks boot.
inline bool Consume(const char *path, Request &out) {
    out = {};
    FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    char data[281];
    size_t size = std::fread(data, 1, sizeof(data), file);
    bool readOK = !std::ferror(file);
    std::fclose(file);
    if (std::remove(path) != 0 || !readOK) return false;
    return Decode(std::string(data, size), out);
}
} // namespace WiiRequest
