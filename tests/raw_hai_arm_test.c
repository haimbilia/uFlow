#include "../payload/source/common/raw_hai.h"
#include <assert.h>
#include <stdio.h>

static RawHaiArm Valid(void) {
    RawHaiArm a;
    memset(&a, 0, sizeof(a));
    a.magic   = UFLOW_HAI_MAGIC;
    a.device  = 1;
    a.titleId = 0x000500001019CF00ull;
    strcpy(a.path, UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai");
    return a;
}

int main(void) {
    /* MCP copies path into MCPTitleListType::path[56]. */
    assert(sizeof(((RawHaiArm *) 0)->path) == 56);
    RawHaiArm a = Valid();
    assert(RawHaiArmValid(&a));
    a.device = 2;
    assert(RawHaiArmValid(&a));
    a.device = 3;
    assert(!RawHaiArmValid(&a));
    a = Valid(); a.magic ^= 1;
    assert(!RawHaiArmValid(&a));
    a = Valid(); a.titleId = 0x0005000E1019CF00ull; /* update, not a base title */
    assert(!RawHaiArmValid(&a));
    a = Valid(); strcpy(a.path, "/vol/storage_mlc01/usr/title/00050000/1019cf00");
    assert(!RawHaiArmValid(&a));
    a = Valid(); strcpy(a.path, UFLOW_HAI_SD_PREFIX "wiiu/../sys");
    assert(!RawHaiArmValid(&a));
    a = Valid(); strcpy(a.path, UFLOW_HAI_SD_PREFIX "wiiu//hai");
    assert(!RawHaiArmValid(&a));
    a = Valid(); strcpy(a.path, UFLOW_HAI_SD_PREFIX "a:b");
    assert(!RawHaiArmValid(&a));
    a = Valid(); memset(a.path, 'a', sizeof(a.path)); /* unterminated */
    memcpy(a.path, UFLOW_HAI_SD_PREFIX, sizeof(UFLOW_HAI_SD_PREFIX) - 1);
    assert(!RawHaiArmValid(&a));
    a = Valid(); memset(a.path + sizeof(UFLOW_HAI_SD_PREFIX) - 1, 'b', 55 - (sizeof(UFLOW_HAI_SD_PREFIX) - 1));
    a.path[55] = 0; /* longest terminated path */
    assert(RawHaiArmValid(&a));
    puts("PASS: HAI MCP arm request validation");
    return 0;
}
