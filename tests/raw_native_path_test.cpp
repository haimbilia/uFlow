#include "RawNativePath.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    char out[1024];
    auto map = [&](const char *path, const char *mode = "r", bool active = true, size_t capacity = sizeof(out)) {
        return RawNativeReadPath(out, capacity, active, path, mode,
                                 "wiiu/raw-games/FAST [WFSE]/content", "wiiu/raw-games/FAST [WFSE]/code");
    };
    assert(map("/vol/content/tracks/test.bin"));
    assert(std::strcmp(out, "/vol/external01/wiiu/raw-games/FAST [WFSE]/content/tracks/test.bin") == 0);
    assert(map("/vol/code/project.rpx", "rb"));
    assert(std::strcmp(out, "/vol/external01/wiiu/raw-games/FAST [WFSE]/code/project.rpx") == 0);
    assert(!map("/vol/content-other/test"));
    assert(!map("/vol/save/user/save.bin"));
    assert(!map("/vol/external01/already-redirected"));
    assert(!map("/vol/content/test", "w"));
    assert(!map("/vol/content/test", "r+"));
    assert(!map("/vol/content/test", "r", false));
    assert(!map("/vol/content/test", "r", true, 16));
    assert(!map(nullptr));
    auto save = [&](const char *path, bool active = true, size_t capacity = sizeof(out)) {
        return RawNativeSavePath(out, capacity, active, path, "wiiu/raw-saves/0005000010128c00");
    };
    assert(save("/vol/save/common/"));
    assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010128c00/common/") == 0);
    assert(save("/vol/save/8000000b"));
    assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010128c00/8000000b") == 0);
    assert(save("/vol/save"));
    assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010128c00") == 0);
    assert(!save("/vol/save-other/common/"));
    assert(!save("/vol/content/test"));
    assert(!save(out)); // Already redirected, must not map twice.
    assert(!save("/vol/save/common/", false));
    assert(!save("/vol/save/common/", true, 16));
    assert(!save(nullptr));
    auto open = [&](const char *path, const char *mode, bool active = true, size_t capacity = sizeof(out)) {
        return RawNativeOpenPath(out, capacity, active, path, mode,
                                  "wiiu/raw-games/Yoshi/content", "wiiu/raw-games/Yoshi/code",
                                  "wiiu/raw-saves/0005000010184d00");
    };
    for (const char *mode : {"r", "rb", "r+", "w", "wb", "w+", "a", "a+"}) {
        assert(open("/vol/save/80000007/save.bin", mode));
        assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010184d00/80000007/save.bin") == 0);
        assert(open("/vol/save/common/save.bin", mode));
        assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010184d00/common/save.bin") == 0);
    }
    assert(open("/vol/content/movie.bin", "rb"));
    assert(!open("/vol/content/movie.bin", "w"));
    assert(!open("/vol/code/game.rpx", "r+"));
    assert(!open("/vol/save-other/file", "w"));
    assert(!open("/vol/save/80000007/save.bin", "w", false));
    assert(!open("/vol/save/80000007/save.bin", "w", true, 16));
    assert(!open("/vol/save/80000007/save.bin", nullptr));
    assert(!open(nullptr, "w"));
    assert(!open("/vol/external01/wiiu/raw-saves/already-native", "w"));
    assert(open("/vol/save/common/save.bin", "r"));
    const auto exactCapacity = std::strlen(out) + 1;
    assert(open("/vol/save/common/save.bin", "w", true, exactCapacity));
    assert(!open("/vol/save/common/save.bin", "w", true, exactCapacity - 1));
    assert(!RawNativeOpenPath(out, sizeof(out), true, "/vol/save/common/file", "w", "", "", ""));
    assert(!RawNativeOpenPath(nullptr, sizeof(out), true, "/vol/save/common/file", "w", "", "", "save"));
    constexpr char usbContent[] = "usb1:/Games/Mario Luigi Superstar Saga [Game] [0005000010157400]/content";
    constexpr char usbCode[] = "usb1:/Games/Mario Luigi Superstar Saga [Game] [0005000010157400]/code";
    for (const char *path : {"/vol/content/alldata.psb.m", "/vol/content/alldata.psb.m.m", "/vol/code/m2engage.rpx"}) {
        for (const char *mode : {"r", "rb", "r+"}) {
            std::strcpy(out, "unchanged");
            assert(!RawNativeOpenPath(out, sizeof(out), true, path, mode, usbContent, usbCode,
                                      "wiiu/raw-saves/0005000010157400"));
            assert(std::strcmp(out, "unchanged") == 0);
        }
    }
    for (const char *root : {"ufusb:/Games/Test/content", "fs:/vol/external01/Games/Test/content", "/Games/Test/content"}) {
        std::strcpy(out, "unchanged");
        assert(!RawNativeReadPath(out, sizeof(out), true, "/vol/content/file", "r", root, root));
        assert(!RawNativeReadPath(out, sizeof(out), true, "/vol/code/file", "rb", root, root));
        assert(std::strcmp(out, "unchanged") == 0);
    }
    for (const char *mode : {"r", "rb", "r+", "w", "wb", "w+", "a", "a+"}) {
        assert(RawNativeOpenPath(out, sizeof(out), true, "/vol/save/common/save.bin", mode,
                                 usbContent, usbCode, "wiiu/raw-saves/0005000010157400"));
        assert(std::strcmp(out, "/vol/external01/wiiu/raw-saves/0005000010157400/common/save.bin") == 0);
    }
    std::cout << "PASS: native SD reads/saves and USB content/code bypass exclusion\n";
}
