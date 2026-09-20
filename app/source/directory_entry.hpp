#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include <string>

// devkitPPC readdir already supplies stat data (including the USB FatFs entry).
// Looking up every name again can turn a large FAT directory into quadratic I/O.
inline mode_t DirectoryEntryMode(const dirent &entry, const std::string &path) {
#ifdef _DIRENT_HAVE_D_STAT
    if (entry.d_stat.st_mode & S_IFMT) return entry.d_stat.st_mode;
#endif
#ifdef _DIRENT_HAVE_D_TYPE
    if (entry.d_type == DT_DIR) return S_IFDIR;
    if (entry.d_type == DT_REG) return S_IFREG;
    if (entry.d_type != DT_UNKNOWN && entry.d_type != DT_LNK) return 0;
#endif
    struct stat info{};
    return stat(path.c_str(), &info) == 0 ? info.st_mode : 0;
}
