// Simulate Wii U replacement behavior for catalog and injection records.
#include <cerrno>
#include <string>
#include <sys/stat.h>
extern "C" int __real_rename(const char *, const char *);
extern "C" int __wrap_rename(const char *from, const char *to) {
    const std::string target=to;
    struct stat info{};
    if ((target.ends_with("content_sources.cfg") || target.ends_with("libraries.cfg") ||
         target.ends_with("library-v1.bin") || target.ends_with("/game.cfg") ||
         target.ends_with("launchers.cfg") || target.ends_with(".uflow-title") ||
         target.ends_with("start-on-boot.cfg")) && stat(to, &info)==0) {
        errno=EEXIST; return -1;
    }
    return __real_rename(from, to);
}
