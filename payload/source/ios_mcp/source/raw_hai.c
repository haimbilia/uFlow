#include "raw_hai.h"
#include "fsa.h"
#include "logger.h"
#include "svc.h"
#include <string.h>

/* MCP's HAI prepare handler (0x050081BE) asks this for the caller's
   MCPTitleListType, then derives everything storage-related from it:
   "%s/code/fw.tmd" and the fw.img/htk.bin/rvlt.* copies use +0x0C, the USB
   devId comes from LBA 0 of "/dev/usb%02x" (+0x60) when +0x56 is "usb", and
   the companion URI becomes "usb://shared2/sys/hai/imginf.bin". */
#define MCP_GET_PID_TITLE_INFO ((int (*)(int, uint8_t *)) 0x0501C5C9)
#define TITLE_INFO_SIZE 0x61

static RawHaiArm gHaiArm;
static int gHaiArmed;

int RawHaiArmInstall(const void *data, unsigned size) {
    gHaiArmed = 0;
    if (size != sizeof(RawHaiArm)) return -1;
    memcpy(&gHaiArm, data, sizeof(gHaiArm));
    if (!RawHaiArmValid(&gHaiArm)) return -1;
    gHaiArmed = 1;
    return 0;
}

void RawHaiClear(void) {
    gHaiArmed = 0;
}

int _HAI_GetTitleInfo_patch(int pid, uint8_t *info) {
    int result = MCP_GET_PID_TITLE_INFO(pid, info);
    if (!gHaiArmed) return result;
    gHaiArmed = 0; /* never leak into a later, unrelated compat launch */

    int fsa = svcOpen("/dev/fsa", 0);
    if (fsa >= 0) {
        FSA_Mount(fsa, "/dev/sdcard01", "/vol/storage_iosu_homebrew", 2, NULL, 0);
        svcClose(fsa);
    }
    if (result != 0) memset(info, 0, TITLE_INFO_SIZE);
    for (int i = 0; i < 8; ++i) info[i] = (uint8_t) (gHaiArm.titleId >> (56 - 8 * i));
    memset(info + 0x0C, 0, UFLOW_HAI_PATH_CAPACITY);
    memcpy(info + 0x0C, gHaiArm.path, strnlen(gHaiArm.path, UFLOW_HAI_PATH_CAPACITY));
    memset(info + 0x56, 0, 10);
    memcpy(info + 0x56, "usb", 3);
    info[0x60] = (uint8_t) gHaiArm.device;
    DEBUG_FUNCTION_LINE("uFlow HAI title info: pid=%d original=%d path=%s device=usb%02x\n",
                        pid, result, gHaiArm.path, (unsigned) gHaiArm.device);
    return 0;
}
