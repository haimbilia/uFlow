#pragma once
#include <cstdint>
struct __attribute__((packed)) discHdr {
    uint8_t id[6],disc_no,disc_ver,streaming,bufsize,is_ciso;
    uint64_t tid;
    uint8_t type,unused[4];
    uint32_t magic,gc_magic;
    char title[64];
    uint8_t encryption,h3_verify;
    char path[260];
};
constexpr int TYPE_GAME_WII_IMG=0, TYPE_GAME_GC_IMG=2;
struct MockSettings { int SDMode=0,partition=0; };
constexpr int PART_FS_FAT=1;
struct DeviceHandler {
    static DeviceHandler *Instance() { static DeviceHandler instance; return &instance; }
    bool IsMounted(int partition) { return partition==2; }
    static int GetFilesystemType(int) { return PART_FS_FAT; }
};
extern MockSettings Settings;
int MountGamePartition(bool);
void gprintf(const char *,...);
struct GameBooter { static int BootGame(discHdr *); };
