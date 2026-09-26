#include "raw_launch.hpp"
#include "wii_vc.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <sys/stat.h>

namespace {
bool NonemptyFile(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}
}

std::string ValidateRawGame(const GameEntry &game) {
    if (game.installed || game.platform != Platform::WiiU) return "SELECT A RAW WII U GAME";
    const bool usb=game.absolutePath.rfind("usb1:/",0)==0;
    if (!usb&&game.absolutePath.rfind("fs:/vol/external01/", 0) != 0)
        return "RAW LOADER REQUIRES AN SD OR USB FAT32 SOURCE";
    if (WiiVC::IsRaw(game)) {
        // Authored Wii VC titles launch unchanged through HAI, which reads the
        // disc images by physical USB sector. The loader also needs these files.
        if (!usb) return "WII VC DIRECT LAUNCH REQUIRES THE GAME ON USB";
        for (const char *file : {"/code/nn_hai_user.rpl", "/code/fw.img", "/code/fw.tmd", "/code/htk.bin",
                                 "/code/rvlt.tik", "/code/rvlt.tmd", "/content/hif_000000.nfs"})
            if (!NonemptyFile(game.absolutePath + file)) return "WII VC TITLE IS INCOMPLETE";
    }
    if (game.titleId.size() != 16 || game.titleId.substr(0, 8) != "00050000")
        return "RAW LAUNCH REQUIRES A BASE GAME, NOT UPDATE OR DLC";
    if (!NonemptyFile(game.absolutePath + "/code/app.xml")) return "MISSING CODE/APP.XML";
    if (!NonemptyFile(game.absolutePath + "/code/cos.xml")) return "MISSING CODE/COS.XML";
    const std::string path = usb?game.rpxRelative:"fs:/vol/external01/" + game.rpxRelative;
    if (!NonemptyFile(path)) return "GAME EXECUTABLE IS MISSING OR EMPTY";
    std::ifstream executable(path, std::ios::binary);
    std::array<unsigned char, 20> header {};
    executable.read(reinterpret_cast<char *>(header.data()), header.size());
    if (executable.gcount() != static_cast<std::streamsize>(header.size()) ||
        header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' ||
        header[4] != 1 || header[5] != 2 || header[18] != 0 || header[19] != 20)
        return "GAME EXECUTABLE IS NOT A WII U POWERPC ELF";
    struct stat info {};
    if (stat((game.absolutePath + "/content").c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
        return "GAME CONTENT DIRECTORY IS MISSING";
    return {};
}
