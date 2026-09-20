#pragma once
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace SavedFile {
// Some console drivers cannot rename over an existing file. Preserve the old
// version until the replacement is ready, and roll back if the commit fails.
inline bool Commit(const std::string &temp, const std::string &target,
                   int (*renameFile)(const char *, const char *) = std::rename) {
    if (renameFile(temp.c_str(), target.c_str()) == 0) return true;
    struct stat info{};
    if (stat(target.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    const auto backup = target + ".bak";
    if (std::remove(backup.c_str()) != 0 && errno != ENOENT) return false;
    if (renameFile(target.c_str(), backup.c_str()) != 0) return false;
    if (renameFile(temp.c_str(), target.c_str()) != 0) {
        renameFile(backup.c_str(), target.c_str());
        return false;
    }
    std::remove(backup.c_str());
    return true;
}
inline std::ifstream Open(const std::string &path,
                          std::ios::openmode mode = std::ios::in) {
    std::ifstream input(path, mode);
    if (!input.is_open()) { input.clear(); input.open(path + ".bak", mode); }
    return input;
}
} // namespace SavedFile
