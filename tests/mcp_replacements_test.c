#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "raw_rpl_scope.h"
#include "mcp_path.h"
#include "/project/payload/source/ios_mcp/source/ipc_types.h"
// PRODUCTION_REQUEST_TYPE
#define DEBUG_FUNCTION_LINE(...) ((void)0)

static char environment[256];
static char paths[32][256];
static int pathCount, nativeCalls, mounts, unmounts;
static const char *available;
static void svcFree(unsigned heap, void *p) { (void)heap; free(p); }
static int svcOpen(const char *p, unsigned flags) { (void)p; (void)flags; return 1; }
static int svcClose(int fd) { (void)fd; return 0; }
static int FSA_Mount(int fd, const char *p, const char *to, unsigned flags, void *arg, unsigned size) {
    (void)fd; (void)p; (void)to; (void)flags; (void)arg; (void)size; ++mounts; return 0;
}
static int FSA_Unmount(int fd, const char *p, unsigned flags) {
    (void)fd; (void)p; (void)flags; ++unmounts; return 0;
}
static void RawUsbClear(void) {}
static int RawUsbRead(const char *p, void *out, unsigned n, unsigned pos) {
    (void)p; (void)out; (void)n; (void)pos; return -6;
}
static int real_MCP_LoadFile(ipcmessage *msg) { (void)msg; ++nativeCalls; return 77; }
static int MCP_DoLoadFile(const char *p, const char *p2, void *out, uint32_t n,
                          uint32_t pos, int *read, uint32_t unk) {
    (void)p2; (void)out; (void)n; (void)pos; (void)unk;
    assert(pathCount < 32 && strlen(p) < sizeof(paths[0]));
    strcpy(paths[pathCount++], p);
    if (available && strcmp(p, available) == 0) { *read = 1024; return 0; }
    *read = 0;
    return -6;
}
// PRODUCTION_REPLACEMENTS
// PRODUCTION_HOOK

static int load(const char *name) {
    MCPLoadFileRequest request = {0};
    strcpy(request.name, name); request.cafe_pid = 7;
    ipcmessage msg = {0}; msg.ioctl.buffer_in = (uint32_t *)&request;
    msg.ioctl.length_io = 4096;
    return _MCP_LoadFile_patch(&msg);
}
static void reset(void) {
    for (size_t i = 0; i < 5; ++i) { free(gDynamicReplacements[i]); gDynamicReplacements[i] = NULL; }
    memset(environment, 0, sizeof(environment));
    strcpy(environment + 19, "wiiu/environments/aroma");
    RawRplReset(&gRawRplScope);
    gRawTitleProfileActive = false; gMemHookCompleted = true;
    gRawUsbScope = false; sReplacedLastRPX = false; gMenuLoadTrace = 0;
    pathCount = nativeCalls = mounts = unmounts = 0; available = NULL;
}
int main(void) {
    char path[8];
    assert(MCPJoinPath(path, sizeof(path), "ab", 3, "cdef", 5));
    assert(strcmp(path, "ab/cdef") == 0); // exact fit with terminator
    assert(!MCPJoinPath(path, 7, "ab", 3, "cdef", 5) && !path[0]);
    assert(MCPJoinPath(path, 8, "ab/", 4, "cdef", 5) && !strcmp(path, "ab/cdef"));
    assert(MCPJoinPath(path, 8, "", 1, "cdef", 5) && !strcmp(path, "cdef"));
    assert(!MCPJoinPath(path, 8, "ab", 2, "cdef", 5));
    assert(!MCPJoinPath(path, 8, "ab", 3, "cdef", 4));
    assert(!MCPJoinPath(path, 8, "ab", 3, "", 1));

    // Normal Menu: the optional SD override does not exist. The real production
    // replacement loop must finish and return to the installed Menu loader.
    reset();
    assert(load("men.rpx") == 77 && nativeCalls == 1 && pathCount == 1);
    assert(!strcmp(paths[0], "/vol/storage_iosu_homebrew/wiiu/environments/aroma/men.rpx"));
    assert(mounts == 1 && unmounts == 1);

    // Aroma bootstrap retains root.rpx precedence; explicit Menu overrides work.
    reset(); gMemHookCompleted = false;
    available = "/vol/storage_iosu_homebrew/wiiu/environments/aroma/root.rpx";
    assert(load("men.rpx") == 1024 && pathCount == 1 && nativeCalls == 0);
    reset();
    available = "/vol/storage_iosu_homebrew/wiiu/environments/aroma/men.rpx";
    assert(load("men.rpx") == 1024 && pathCount == 1 && nativeCalls == 0);
    reset(); gMemHookCompleted = false;
    assert(load("men.rpx") == 77 && pathCount == 2 && nativeCalls == 1);

    // All five dynamic slots plus six built-in records, including removal of
    // an outgoing homebrew replacement before Menu's first executable read.
    reset();
    for (int i = 0; i < 5; ++i) {
        RPXFileReplacements *r = calloc(1, sizeof(*r));
        assert(r); r->type = REPLACEMENT_TYPE_HOMEBREW_RPX;
        r->lifetime = REPLACEMENT_LIFETIME_DURING_RPX_REPLACEMENT;
        r->ageInApplicationStarts = 1; assert(addDynamicReplacement(r));
    }
    assert(load("men.rpx") == 77 && pathCount == 1);
    for (int i = 0; i < 5; ++i) assert(gDynamicReplacements[i] == NULL);
    reset();
    for (int i = 0; i < 5; ++i) {
        RPXFileReplacements *r = calloc(1, sizeof(*r));
        assert(r); r->type = REPLACEMENT_TYPE_BY_PATH;
        r->lifetime = REPLACEMENT_LIFETIME_UNLIMITED;
        strcpy(r->replaceName, "unrelated.rpl"); assert(addDynamicReplacement(r));
    }
    assert(load("men.rpx") == 77 && pathCount == 1);
    for (int i = 0; i < 5; ++i) assert(gDynamicReplacements[i] != NULL);

    // Invalid environment fields fail closed without an over-read or a truncated
    // path, and still allow the installed Menu to load.
    reset(); memset(environment + 19, 'x', sizeof(environment) - 19);
    assert(load("men.rpx") == 77 && pathCount == 0 && mounts == 0);
    reset(); memset(environment + 19, 'x', 232); environment[251] = 0;
    assert(load("men.rpx") == 77 && pathCount == 0 && mounts == 0);
    // Original H&S backups retain regional MLC fallback without an SD mount.
    reset(); available = "/vol/storage_mlc01/sys/title/00050010/1004e100/content/safe.rpx.bak";
    assert(load("safe.rpx") == 1024 && pathCount == 2 && mounts == 0);
    assert(!strcmp(paths[1], available));
    reset();
    puts("PASS: actual MCP Menu replacement loop, bootstrap, missing override/native fallback, MLC backups, lifecycle and bounded paths (ASan/UBSan)");
}
