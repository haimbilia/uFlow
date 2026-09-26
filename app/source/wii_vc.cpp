#include "wii_vc.hpp"
#include "wii_vc_image.hpp"
#include "wii_request.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace WiiVC {
namespace {
constexpr const char *sd = "fs:/vol/external01/";
// v2 uses the original encrypted Wii partition payload.  Keep any earlier
// experimental cache isolated so it cannot be mistaken for a valid image.
constexpr const char *cacheRoot = "wiiu/apps/uFlow/wii-vc-cache-v2/";

bool Directory(const std::string &path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool EnsureDirectory(const std::string &path) {
    if (path.empty() || Directory(path)) return true;
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0 && !EnsureDirectory(path.substr(0, slash))) return false;
    return mkdir(path.c_str(), 0777) == 0 || Directory(path);
}

std::string Base(const GameEntry &game) {
    return std::string(sd) + cacheRoot + game.titleId;
}

std::string Marker(const GameEntry &game) { return Base(game) + ".ready"; }

bool ReadMarker(const GameEntry &game, std::string &id) {
    std::ifstream input(Marker(game), std::ios::binary);
    if (!input) return false;
    std::getline(input, id);
    if (!input || !WiiRequest::ValidId(id)) return false;
    const auto path = Base(game) + ".wbfs";
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= (1 << 20);
}

void Publish(GameEntry &game, const std::string &id, const std::string &staging) {
    struct stat info{};
    if (stat((Base(game) + ".wbfs").c_str(), &info) == 0)
        throw std::runtime_error("WII VC CACHE ALREADY EXISTS WITHOUT A VALID MARKER");
    unsigned published = 0;
    for (;; ++published) {
        const auto suffix = published ? ".wbf" + std::to_string(published) : ".wbfs";
        const auto from = staging + suffix, to = Base(game) + suffix;
        if (stat(from.c_str(), &info) != 0) break;
        if (std::rename(from.c_str(), to.c_str()) != 0) {
            for (unsigned i = 0; i < published; ++i) {
                const auto rollback = i ? ".wbf" + std::to_string(i) : ".wbfs";
                std::remove((Base(game) + rollback).c_str());
            }
            throw std::runtime_error("CANNOT PUBLISH WII VC CACHE - CHECK SD SPACE");
        }
    }
    if (published == 0) throw std::runtime_error("WII VC CONVERSION PRODUCED NO IMAGE");
    std::ofstream marker(Marker(game), std::ios::binary | std::ios::trunc);
    if (!marker || !(marker << id << '\n')) {
        for (unsigned i = 0; i < published; ++i) {
            const auto suffix = i ? ".wbf" + std::to_string(i) : ".wbfs";
            std::remove((Base(game) + suffix).c_str());
        }
        throw std::runtime_error("CANNOT COMMIT WII VC CACHE MARKER");
    }
    marker.close();
    if (!marker) {
        for (unsigned i = 0; i < published; ++i) {
            const auto suffix = i ? ".wbf" + std::to_string(i) : ".wbfs";
            std::remove((Base(game) + suffix).c_str());
        }
        std::remove(Marker(game).c_str());
        throw std::runtime_error("CANNOT FLUSH WII VC CACHE MARKER");
    }
}

}

bool HasPrepared(const GameEntry &game) {
    if (!InjectionAvailable) return false;
    if (!IsRaw(game)) return false;
    std::string id;
    return ReadMarker(game, id);
}

std::string Prepare(GameEntry &game, VCInject::Progress progress) {
    if (!InjectionAvailable) return InjectionUnavailable;
    try {
        if (!IsRaw(game)) return "SELECT A RAW WII VC GAME";
        if (!EnsureDirectory(std::string(sd) + cacheRoot)) return "CANNOT CREATE WII VC CACHE DIRECTORY";

        std::string id;
        if (ReadMarker(game, id)) {
            game.platform = Platform::Wii;
            game.absolutePath = Base(game) + ".wbfs";
            game.folder = game.titleId + ".wbfs";
            game.titleId = id;
            return {};
        }

        Image::Source source(game.absolutePath,progress);
        const auto staging = Base(game) + ".staging";
        std::remove((staging + ".wbfs").c_str());
        std::remove((staging + ".wbf1").c_str());
        const auto result = Image::Convert(source, staging,
            [&](const std::string &phase, unsigned long long done, unsigned long long total) {
                return !progress || progress(phase, done, total);
            });
        Publish(game, result.id, staging);
        game.platform = Platform::Wii;
        game.absolutePath = Base(game) + ".wbfs";
        game.folder = game.titleId + ".wbfs";
        game.titleId = result.id;
        return {};
    } catch (const std::exception &error) {
        return error.what();
    } catch (...) {
        return "WII VC PREPARATION FAILED";
    }
}
}
