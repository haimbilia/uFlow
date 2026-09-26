#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "raw_rpl_scope.h"
#include "raw_title_profile.h"
#include "raw_usb_map.h"
#include "raw_hai.h"
// PRODUCTION_REQUEST_TYPE
#include "/project/payload/source/ios_mcp/source/ipc_types.h"
#define DEBUG_FUNCTION_LINE(...) ((void)0)
#define IPC_CUSTOM_MEN_RPX_HOOK_COMPLETED 0xFE
#define IPC_CUSTOM_LOAD_CUSTOM_RPX 0xFC
#define LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE 0xFFFF
#define LOAD_RPX_TARGET_SD_CARD 0
typedef struct { uint32_t target, size, offset; char path[256]; } MochaRPXLoadInfo;
// PRODUCTION_TYPES
static int stockCalls, localCalls, replacementResult, localResult, oneLaunchRemovals;
static uint32_t seenPos;
static char seenPath[256];
static bool allocationFails, registrationFails;
static bool usbMapAvailable;
static LoadTargetDevice seenTarget;
static void RawUsbClear(void){usbMapAvailable=false;}
static bool haiArmed;
static void RawHaiClear(void){haiArmed=false;}
static int RawHaiArmInstall(const void *data,unsigned size){
    haiArmed=size==sizeof(RawHaiArm)&&RawHaiArmValid((const RawHaiArm*)data);return haiArmed?0:-1;
}
static int RawUsbHasFile(const char *path){(void)path;return usbMapAvailable;}
static int RawUsbInstall(const void *data,unsigned size){(void)data;usbMapAvailable=size==sizeof(RawUsbMap);return usbMapAvailable?0:-1;}
static void *svcAlloc(uint32_t heap, size_t size) { (void)heap; return allocationFails ? NULL : malloc(size); }
static void svcFree(uint32_t heap, void *p) { (void)heap; free(p); }
static bool addDynamicReplacement(RPXFileReplacements *r) {
    if (registrationFails) return false;
    assert(!gDynamicReplacements[0]); gDynamicReplacements[0] = r; return true;
}
static void RemoveByLifetime(ReplacementLifetime life) {
    if (life == REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH) {
        ++oneLaunchRemovals; free(gDynamicReplacements[0]); gDynamicReplacements[0] = NULL;
    }
}
static void IncreaseApplicationStartCounter(void) {}
static bool EndsWith(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix); return n >= m && !strcmp(s + n - m, suffix);
}
static int real_MCP_LoadFile(ipcmessage *msg) { (void)msg; ++stockCalls; return 99; }
static int MCPLoadFileReplacement(ipcmessage *msg, MCPLoadFileRequest *r) {
    (void)msg; (void)r; return replacementResult;
}
static int MCP_LoadCustomFile(LoadTargetDevice target, char *path, uint32_t size,
                              uint32_t offset, void *out, uint32_t len, uint32_t pos, bool rpx) {
    (void)out; seenTarget=target;assert(size == 0 && offset == 0 && !rpx);
    assert(len == 4096); ++localCalls; seenPos = pos; strcpy(seenPath, path); return localResult;
}
// PRODUCTION_HOOK
static int ioctlTest(ipcmessage *msg) {
    switch (msg->ioctl.buffer_in[0]) {
// PRODUCTION_CASES
    default: break;
    }
    return 0;
}
static void profile(bool enabled) {
    uint32_t packet[(sizeof(RawTitleProfile) + 4) / 4] = {IPC_CUSTOM_SET_RAW_TITLE_PROFILE};
    RawTitleProfile *p = (RawTitleProfile *)(packet + 1);
    p->magic = RAW_TITLE_PROFILE_MAGIC; p->profileVersion = RAW_TITLE_PROFILE_VERSION;
    p->enabled = enabled; strcpy(p->argstr, "Turbo.rpx");
    ipcmessage msg = {0}; msg.ioctl.buffer_in = packet; msg.ioctl.length_in = sizeof(packet);
    assert(ioctlTest(&msg) == 0);
}
static int prepare(const char *path, uint32_t offset, uint32_t target) {
    uint32_t packet[1 + sizeof(MochaRPXLoadInfo) / 4] = {IPC_CUSTOM_LOAD_CUSTOM_RPX};
    packet[1] = target; packet[3] = offset; strcpy((char *)(packet + 4), path);
    ipcmessage msg = {0}; msg.ioctl.buffer_in = packet; msg.ioctl.length_in = sizeof(packet);
    return ioctlTest(&msg);
}
static void installUsb(void){
    uint32_t packet[1+sizeof(RawUsbMap)/4]={UFLOW_USB_MAP_COMMAND}, response=0;
    ipcmessage msg={0};msg.ioctl.buffer_in=packet;msg.ioctl.length_in=sizeof(packet);
    msg.ioctl.buffer_io=&response;msg.ioctl.length_io=4;
    assert(ioctlTest(&msg)==0&&response==UFLOW_USB_MAP_MAGIC);
}
static int armHai(const char *path){
    struct { uint32_t command; RawHaiArm arm; } __attribute__((packed)) packet={UFLOW_HAI_ARM_COMMAND,{0}};
    packet.arm.magic=UFLOW_HAI_MAGIC;packet.arm.device=1;packet.arm.titleId=0x000500001019CF00ull;
    strcpy(packet.arm.path,path);
    uint32_t response=0;ipcmessage msg={0};msg.ioctl.buffer_in=(uint32_t*)&packet;msg.ioctl.length_in=sizeof(packet);
    msg.ioctl.buffer_io=&response;msg.ioctl.length_io=4;
    const int result=ioctlTest(&msg);
    assert(result!=0||response==UFLOW_HAI_MAGIC);
    return result;
}
static void haiArmLifecycle(void){
    const char *stage=UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai";
    profile(false);assert(armHai(stage)==29&&!haiArmed); /* only for an armed raw title */
    profile(true);assert(armHai("/vol/storage_mlc01/usr/title")==29&&!haiArmed);
    assert(armHai(stage)==0&&haiArmed);
    profile(false);assert(!haiArmed); /* clearing the raw title always disarms HAI */
    profile(true);assert(!haiArmed);
    profile(false);
}
static int load(const char *name, uint32_t pos, uint32_t pid, uint32_t type) {
    MCPLoadFileRequest r = {0}; strcpy(r.name, name); r.pos = pos; r.cafe_pid = pid; r.type = type;
    ipcmessage msg = {0}; msg.ioctl.buffer_in = (uint32_t *)&r; msg.ioctl.length_io = 4096;
    return _MCP_LoadFile_patch(&msg);
}
static const char *game = "wiiu/raw-games/MARIO KART 8 [Game] [000500001010ec00]/code/Turbo.rpx";
static void start(const char *mainName) {
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    profile(true); assert(prepare(game, 0, LOAD_RPX_TARGET_SD_CARD) == 0);
    assert(gRawRplScope.prepared && !gRawRplScope.active);
    replacementResult = 1024; assert(load(mainName, 0, 7, 0) == 1024);
    assert(gRawRplScope.active && !gRawRplScope.prepared);
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH); // final RPX read
    replacementResult = -1; localResult = 512;
}
int main(void) {
    haiArmLifecycle();
    replacementResult = -1; assert(load("mvplayer", 0, 7, 0) == 99);
    start("Turbo.rpx");
    assert(load("mvplayer", 0, 7, 0) == 512);
    assert(!strcmp(seenPath, "wiiu/raw-games/MARIO KART 8 [Game] [000500001010ec00]/code/mvplayer.rpl"));
    assert(load("mw_igr.rpl", 4096, 7, 0) == 512 && seenPos == 4096);
    assert(load("mw_igr_sbc", 8388608, 7, 0) == 512 && seenPos == 8388608);
    localResult = 0; int stock = stockCalls;
    assert(load("mvplayer", 99999, 7, 0) == 0 && stockCalls == stock); // EOF is success
    localResult = -6; assert(load("missing", 0, 7, 0) == 99); // stock fallback
    localResult = 512; int calls = localCalls;
    for (int type = 1; type <= 3; ++type) assert(load("coreinit", 0, 7, type) == 99);
    assert(load("mvplayer", 0, 5, 0) == 99 && localCalls == calls);
    replacementResult = 17; assert(load("mvplayer", 0, 7, 0) == 17 && localCalls == calls);
    replacementResult = -1;
    const char *bad[] = {"../bad", "a/b", "a\\b", "a:b", "~|x", ".", "..", "a..b", "x.rpx", "a b", "x.txt", ""};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) assert(load(bad[i], 20, 7, 0) == 99);
    assert(localCalls == calls);
    assert(load("men.rpx", 0, 7, 0) == 99 && !gRawRplScope.active);
    assert(load("mvplayer", 0, 7, 0) == 99);
    start("safe.rpx"); assert(load("mvplayer.RPL", 0, 7, 0) == 512);
    profile(false); assert(!gRawRplScope.active && load("mvplayer", 0, 7, 0) == 99);
    start("ply.rpx"); assert(prepare("", 0, LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE) == 0);
    assert(!gRawRplScope.active && !gDynamicReplacements[0]);
    start("Turbo.rpx"); assert(prepare("wiiu/apps/other.rpx", 0, LOAD_RPX_TARGET_SD_CARD) == 0);
    assert(!gRawRplScope.active && !gRawRplScope.prepared);
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    profile(true); assert(prepare(game, 0, LOAD_RPX_TARGET_SD_CARD) == 0);
    assert(load("unrelated.rpx", 0, 7, 0) == 99 && !gRawRplScope.prepared);
    start("Turbo.rpx"); assert(load("Turbo.rpx", 0, 7, 0) == 99 && !gRawRplScope.active);
    profile(true); allocationFails = true;
    assert(prepare(game, 0, LOAD_RPX_TARGET_SD_CARD) == 22 && !gRawRplScope.prepared); allocationFails = false;
    registrationFails = true;
    assert(prepare(game, 0, LOAD_RPX_TARGET_SD_CARD) == 22 && !gRawRplScope.prepared); registrationFails = false;
    assert(prepare(game, 32, LOAD_RPX_TARGET_SD_CARD) == 0 && !gRawRplScope.prepared);
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    RawRplScope s; char output[256];
    assert(!RawRplPrepare(&s, "a/../Turbo.rpx", "Turbo.rpx", true, 0));
    assert(!RawRplPrepare(&s, "/a/Turbo.rpx", "Turbo.rpx", true, 0));
    assert(!RawRplPrepare(&s, game, "Other.rpx", true, 0));
    char longPath[256]; memset(longPath, 'a', 210); strcpy(longPath + 210, "/Turbo.rpx");
    assert(RawRplPrepare(&s, longPath, "Turbo.rpx args", true, 0)); RawRplExecutableLoaded(&s);
    assert(!RawRplPath(&s, "very_long_library_name.rpl", 7, 0, output));
    char noTerminator[64]; memset(noTerminator, 'a', sizeof(noTerminator));
    assert(!RawRplPath(&s, noTerminator, 7, 0, output));
    profile(true);
    assert(prepare(game,0,UFLOW_USB_TARGET)==29);
    installUsb();assert(prepare(game,32,UFLOW_USB_TARGET)==29);
    assert(prepare("../bad.rpx",0,UFLOW_USB_TARGET)==29);
    assert(prepare(game,0,UFLOW_USB_TARGET)==0&&gRawUsbScope);
    assert(gDynamicReplacements[0]->relativeTo==PATH_RELATIVE_TO_USB_ROOT);
    assert(armHai(UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai")==0&&haiArmed);
    replacementResult=1024;assert(load("Turbo.rpx",0,7,0)==1024&&usbMapAvailable);
    assert(haiArmed); /* survives this title's executable startup */
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    replacementResult=-1;localResult=512;
    assert(load("mvplayer",4096,7,0)==512&&seenTarget==MOCHA_LOAD_TARGET_DEVICE_USB);
    assert(load("men.rpx",0,7,0)==99&&!usbMapAvailable&&!gRawUsbScope);
    assert(!haiArmed); /* never hijack the next title's compatibility launch */
    profile(true);installUsb();assert(prepare(game,0,UFLOW_USB_TARGET)==0);
    assert(armHai(UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai")==0&&haiArmed);
    assert(prepare("",0,LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE)==0&&!usbMapAvailable&&!gRawUsbScope);
    assert(!haiArmed);
    profile(true);assert(armHai(UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai")==0&&haiArmed);
    assert(prepare("wiiu/apps/other.rpx",0,LOAD_RPX_TARGET_SD_CARD)==0&&!haiArmed);
    RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    start("Turbo.rpx");assert(load("mvplayer",0,7,0)==512&&seenTarget==MOCHA_LOAD_TARGET_DEVICE_SD);
    profile(false);RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
    puts("Raw RPL SD/USB production dispatch/lifecycle regressions PASS (ASan/UBSan)");
    return 0;
}
