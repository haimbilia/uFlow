#include <cassert>
#include <cerrno>
#include <cstdio>
#include <string>
#include <filesystem>
#include <fstream>
#define DEBUG_FUNCTION_LINE_VERBOSE(...) ((void)0)
#define DEBUG_FUNCTION_LINE_ERR(...) ((void)0)
enum FSError { FS_ERROR_OK, FS_ERROR_FORCE_PARENT_LAYER, FS_ERROR_PERMISSION_ERROR,
    FS_ERROR_NOT_FOUND, FS_ERROR_NOT_DIR, FS_ERROR_ACCESS_ERROR, FS_ERROR_NOT_FILE,
    FS_ERROR_MEDIA_ERROR };
static int injectedError = 0;
static int Remove(const char *path) {
    if (injectedError) { errno = injectedError; return -1; }
    return std::remove(path);
}
struct FSWrapper {
    bool pIsWriteable = true;
    bool matches = true;
    bool IsPathToReplace(const char *) { return matches; }
    std::string GetNewPath(const char *path) { return path; }
    FSError FSRemoveWrapper(const char *path);
};
#define remove Remove
// PRODUCTION_REMOVE
#undef remove
int main() {
    const auto root = std::filesystem::temp_directory_path() / "uflow-remove-test";
    std::filesystem::create_directory(root);
    const auto path = (root / "fs0.fs").string();
    FSWrapper wrapper;
    assert(!std::filesystem::exists(path));
    assert(wrapper.FSRemoveWrapper(path.c_str()) == FS_ERROR_NOT_FOUND);
    std::ofstream(path) << "temporary save";
    assert(wrapper.FSRemoveWrapper(path.c_str()) == FS_ERROR_OK);
    assert(!std::filesystem::exists(path));
    assert(wrapper.FSRemoveWrapper(path.c_str()) == FS_ERROR_NOT_FOUND);
    for (auto [error, expected] : {std::pair{EIO, FS_ERROR_MEDIA_ERROR},
         {EACCES, FS_ERROR_ACCESS_ERROR}, {ENOTDIR, FS_ERROR_NOT_DIR},
         {EISDIR, FS_ERROR_NOT_FILE}, {EPERM, FS_ERROR_PERMISSION_ERROR}}) {
        injectedError = error;
        assert(wrapper.FSRemoveWrapper(path.c_str()) == expected);
    }
    wrapper.pIsWriteable = false;
    assert(wrapper.FSRemoveWrapper(path.c_str()) == FS_ERROR_PERMISSION_ERROR);
    wrapper.matches = false;
    assert(wrapper.FSRemoveWrapper(path.c_str()) == FS_ERROR_FORCE_PARENT_LAYER);
    std::filesystem::remove(root);
    puts("PASS: actual removal wrapper: missing file, existing file, repeated cleanup, I/O errors, permissions and layer routing");
}
