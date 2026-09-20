/* Patch for MCP_LoadFile (ioctl 0x53).
 * Also adds a sibling ioctl, 0x64, that allows setting a custom path to a main RPX
 *
 * Reference for most of the types and whatever:
 * https://github.com/exjam/decaf-emu/tree/ios/src/libdecaf/src/ios/mcp
 *
 * This is a proof of concept, and shouldn't be used until it's cleaned up a bit.
 * Co-authored by exjam, Maschell and QuarkTheAwesome
 *
 * Flow of calls:
 * - kernel loads system libraries, app rpx or user calls OSDynLoad
 * - goes to loader.elf, which will call ioctl 0x53
 *   - with fileType = LOAD_FILE_CAFE_OS
 *   - on failure, again with LOAD_FILE_PROCESS_CODE
 *   - on failure, again with LOAD_FILE_SYS_DATA_CODE
 * - each request routes here where we can do whatever
 */

#include "../../common/ipc_defs.h"
#include "../../common/kernel_commands.h"
#include "../../common/raw_title_profile.h"
#include "../../common/raw_rpl_scope.h"
#include "../../common/mcp_path.h"
#include "fsa.h"
#include "imports.h"
#include "ipc_types.h"
#include "logger.h"
#include "svc.h"
#include "syslogserver.h"
#include "raw_usb.h"
#include <inttypes.h>

#include <mocha/commands.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <unistd.h>

typedef enum {
    MOCHA_LOAD_TARGET_DEVICE_NONE = 0,
    MOCHA_LOAD_TARGET_DEVICE_SD   = 1,
    MOCHA_LOAD_TARGET_DEVICE_MLC  = 2,
    MOCHA_LOAD_TARGET_DEVICE_USB  = 3,
} LoadTargetDevice;

static int
MCP_LoadCustomFile(LoadTargetDevice target, char *path, uint32_t filesize, uint32_t fileoffset, void *buffer_out,
                   uint32_t buffer_len,
                   uint32_t pos, bool isRPX);

int (*const real_MCP_LoadFile)(ipcmessage *msg) = (int (*)(ipcmessage *))(0x0501CAA8 + 1); //+1 for thumb
int (*const MCP_DoLoadFile)(const char *path,
                            const char *path2,
                            void *outputBuffer,
                            uint32_t outLength,
                            uint32_t pos,
                            int *bytesRead,
                            uint32_t unk)       = (int (*)(const char *,
                                                     const char *,
                                                     void *,
                                                     uint32_t,
                                                     uint32_t,
                                                     int *,
                                                     uint32_t))(0x05017248 + 1);

typedef enum {
    REPLACEMENT_TYPE_INVALID                              = 0,
    REPLACEMENT_TYPE_HOMEBREW_RPX                         = 1,
    REPLACEMENT_TYPE_HOMEBREW_RPL                         = 2,
    REPLACEMENT_TYPE_BY_PATH                              = 3,
    REPLACEMENT_TYPE_UNTIL_MEM_HOOK_COMPLETED_BUT_BY_PATH = 4,
} ReplacementType;

typedef enum {
    REPLACEMENT_LIFETIME_INVALID                = 0,
    REPLACEMENT_LIFETIME_UNLIMITED              = 1,
    REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH         = 2,
    REPLACEMENT_LIFETIME_DURING_RPX_REPLACEMENT = 3,
} ReplacementLifetime;

typedef enum {
    PATH_RELATIVE_INVALID        = 0,
    PATH_RELATIVE_TO_ENVIRONMENT = 1,
    PATH_RELATIVE_TO_SD_ROOT     = 2,
    PATH_RELATIVE_TO_MLC_ROOT    = 3,
    PATH_RELATIVE_TO_USB_ROOT    = 4,
} PathRelativeTo;


typedef struct RPXFileReplacements {
    ReplacementType type;
    ReplacementLifetime lifetime;
    char replaceName[0x40];
    char replacementPath[256];
    PathRelativeTo relativeTo;
    uint32_t fileSize;
    uint32_t fileOffset;
    uint32_t ageInApplicationStarts;
} RPXFileReplacements;

static bool gMemHookCompleted = false;
static bool sReplacedLastRPX  = false;
static bool gRawTitleProfileActive = false;
/* MCP only needs the executable token, not the 4-KiB PPC launch profile.
   Keep the fixed IOSU BSS allocation within its existing bounds. */
static char gRawTitleExecutable[65];
static RawRplScope gRawRplScope;
static bool gRawUsbScope;
static unsigned gMenuLoadTrace;


// Dynamic replacements have priority over default replacements
static RPXFileReplacements *gDynamicReplacements[5] = {};

static const RPXFileReplacements gDefaultReplacements[] = {
        // Redirect men.rpx [ENVIRONMENT]/root.rpx until IPC_CUSTOM_MEN_RPX_HOOK_COMPLETED has been called. (e.g. to init PPC homebrew after iosu-reload)
        {REPLACEMENT_TYPE_UNTIL_MEM_HOOK_COMPLETED_BUT_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "men.rpx", "root.rpx", PATH_RELATIVE_TO_ENVIRONMENT, 0, 0, 0},
        {REPLACEMENT_TYPE_UNTIL_MEM_HOOK_COMPLETED_BUT_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "WAGONU.rpx", "root.rpx", PATH_RELATIVE_TO_ENVIRONMENT, 0, 0, 0},
        // Redirect men.rpx to [ENVIRONMENT]/men.rpx
        {REPLACEMENT_TYPE_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "men.rpx", "men.rpx", PATH_RELATIVE_TO_ENVIRONMENT, 0, 0, 0},
        // Try to load the real H&S safe.rpx from backup
        {REPLACEMENT_TYPE_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "safe.rpx", "sys/title/00050010/1004e000/content/safe.rpx.bak", PATH_RELATIVE_TO_MLC_ROOT, 0, 0, 0},
        {REPLACEMENT_TYPE_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "safe.rpx", "sys/title/00050010/1004e100/content/safe.rpx.bak", PATH_RELATIVE_TO_MLC_ROOT, 0, 0, 0},
        {REPLACEMENT_TYPE_BY_PATH, REPLACEMENT_LIFETIME_UNLIMITED, "safe.rpx", "sys/title/00050010/1004e200/content/safe.rpx.bak", PATH_RELATIVE_TO_MLC_ROOT, 0, 0, 0},
};


// should be at least the size of gDefaultReplacements and gDynamicReplacements
#define TEMP_ARRAY_SIZE 11

bool addDynamicReplacement(RPXFileReplacements *pReplacements) {
    for (uint32_t i = 0; i < sizeof(gDynamicReplacements) / sizeof(gDynamicReplacements[0]); i++) {
        if (gDynamicReplacements[i] == NULL) {
            gDynamicReplacements[i] = pReplacements;
            return true;
        }
    }
    DEBUG_FUNCTION_LINE("Failed to find empty slot!\n");
    return false;
}

void RemoveByLifetime(ReplacementLifetime lifetime) {
    for (uint32_t i = 0; i < sizeof(gDynamicReplacements) / sizeof(gDynamicReplacements[0]); i++) {
        if (gDynamicReplacements[i] != NULL && gDynamicReplacements[i]->lifetime == lifetime) {
            // We DON'T want to remove new entries which had the chance to be used
            // There are intended to be used for the NEXT application start.
            if (lifetime == REPLACEMENT_LIFETIME_DURING_RPX_REPLACEMENT &&
                gDynamicReplacements[i]->ageInApplicationStarts == 0) {
                continue;
            }
            /*DEBUG_FUNCTION_LINE("Remove item %p: lifetime: %d type: %d replace: %s replaceWith: %s\n",
                                gDynamicReplacements[i], gDynamicReplacements[i]->lifetime,
                                gDynamicReplacements[i]->type, gDynamicReplacements[i]->replaceName,
                                gDynamicReplacements[i]->replacementPath);*/
            svcFree(0xCAFF, gDynamicReplacements[i]);
            gDynamicReplacements[i] = NULL;
        }
    }
}

static inline int EndsWith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return 0;
    size_t lenstr    = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

// Set filesize to 0 if unknown.
static int
MCP_LoadCustomFile(LoadTargetDevice target, char *path, uint32_t filesize, uint32_t fileoffset, void *buffer_out,
                   uint32_t buffer_len,
                   uint32_t pos, bool isRPX) {
    if (path == NULL || (filesize > 0 && (pos > filesize))) {
        return 0;
    }
    if(target==MOCHA_LOAD_TARGET_DEVICE_USB) {
        if(fileoffset)return -1; /* USB supports loose executables, not bundles. */
        int result=RawUsbRead(path,buffer_out,buffer_len,pos);
        if(isRPX && (result<0 || result<0x400000))RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
        return result;
    }

    char filepath[256];
    char mountpath[] = "/vol/storage_iosu_homebrew";
    const char *prefix = target == MOCHA_LOAD_TARGET_DEVICE_SD ? mountpath : "";
    if (!MCPJoinPath(filepath, sizeof(filepath), prefix, sizeof(mountpath), path, 256)) return -1;

    if (target == MOCHA_LOAD_TARGET_DEVICE_SD) {
        int fsa_h = svcOpen("/dev/fsa", 0);
        FSA_Mount(fsa_h, "/dev/sdcard01", mountpath, 2, NULL, 0);
        svcClose(fsa_h);

    }

    DEBUG_FUNCTION_LINE("Trying to load .rpx from custom path \"%s\"\n", filepath);

    int bytesRead = 0;
    int result    = MCP_DoLoadFile(filepath, NULL, buffer_out, buffer_len, pos + fileoffset, &bytesRead, 0);
    DEBUG_FUNCTION_LINE("MCP_DoLoadFile returned %d, bytesRead = %d pos %lu\n", result, bytesRead, pos + fileoffset);

    if (result >= 0) {
        if (bytesRead <= 0) {
            result = 0;
        } else {
            if (filesize > 0 && (bytesRead + pos > filesize)) {
                result = filesize - pos;
            } else {
                result = bytesRead;
            }
        }
    }

    // Unmount the sd card once the whole file has been read or there was an error
    if ((result >= 0 && result < 0x400000) || result < 0) {
        if (target == MOCHA_LOAD_TARGET_DEVICE_SD) {
            int fsa_h = svcOpen("/dev/fsa", 0);
            FSA_Unmount(fsa_h, mountpath, 0x80000002);
            svcClose(fsa_h);
        }
        if (isRPX) {
            // if we have read less than 0x400000 bytes we have read the whole file.
            // We can now remove the replacements with "one launch" lifetime
            RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
        }
    }

    return result;
}

int DoSDRedirectionByPath(ipcmessage *msg, MCPLoadFileRequest *request) {
    if (strlen(request->name) > 1 && request->name[0] == '~' && request->name[1] == '|') {
        // OSDynload_Acquire is cutting of the name right after the last '/'. This means "~/wiiu/libs/test.rpl" would simply become "test.rpl".
        // To still have directories, Mocha expects '|' instead of '/'. (Modules like the AromaBaseModule might handle this transparent for the user.)
        // Example: "~|wiiu|libs|test.rpl" would load "sd://wiiu/libs/test.rpl".
        char *curPtr = &request->name[1];
        while (*curPtr != '\0') {
            if (*curPtr == '|') {
                *curPtr = '/';
            }
            curPtr++;
        }
        // DEBUG_FUNCTION_LINE("Trying to load %s from sd\n", &request->name[2]);
        int result = MCP_LoadCustomFile(MOCHA_LOAD_TARGET_DEVICE_SD, &request->name[2], 0, 0, msg->ioctl.buffer_io,
                                        msg->ioctl.length_io, request->pos, EndsWith(request->name, ".rpx"));

        if (result >= 0) {
            return result;
        } else {
            DEBUG_FUNCTION_LINE("Failed: result was %d\n", result);
        }
        return real_MCP_LoadFile(msg);
    }
    return -1;
}

bool isCurrentHomebrewWrapperReplacement(MCPLoadFileRequest *request) {
    if ((strncmp("ply.rpx", request->name, strlen("ply.rpx")) == 0) ||
        (strncmp("safe.rpx", request->name, strlen("safe.rpx")) == 0)) {
        return true;
    }

    if (gRawTitleProfileActive) {
        char executableName[sizeof(request->name)] = {};
        size_t length = 0;
        while (length < sizeof(executableName) - 1 &&
               gRawTitleExecutable[length] != '\0' &&
               gRawTitleExecutable[length] != ' ') {
            executableName[length] = gRawTitleExecutable[length];
            length++;
        }
        if (length > 0 && strncmp(executableName, request->name, sizeof(request->name)) == 0) {
            return true;
        }
    }
    return false;
}

const RPXFileReplacements *GetCurrentRPXReplacementEx(MCPLoadFileRequest *request, const RPXFileReplacements **list, uint32_t list_size, uint32_t *offset) {
    if (!offset || *offset >= list_size) {
        return NULL;
    }
    for (uint32_t i = *offset; i < list_size; i++) {
        const RPXFileReplacements *cur = list[i];
        if (cur == NULL || cur->lifetime == REPLACEMENT_LIFETIME_INVALID) {
            continue;
        }
        bool valid = false;
        switch (cur->type) {
            case REPLACEMENT_TYPE_HOMEBREW_RPX: {
                // DEBUG_FUNCTION_LINE("Check REPLACEMENT_TYPE_HOMEBREW_RPX for %s... ", cur->replacementPath);
                if (isCurrentHomebrewWrapperReplacement(request)) {
                    // printf("Yay!\n");
                    valid = true;
                } else {
                    // printf("No :(!\n");
                }
                break;
            }
            case REPLACEMENT_TYPE_HOMEBREW_RPL:
                // DEBUG_FUNCTION_LINE("Check REPLACEMENT_TYPE_HOMEBREW_RPL for %s (%s) == %s (sReplacedLastRPX %d)... ", cur->replaceName,
                //                    cur->replacementPath, request->name, sReplacedLastRPX);
                if (sReplacedLastRPX && strncmp(cur->replaceName, request->name, sizeof(request->name) - 1) == 0) {
                    // printf("Yay!\n");
                    valid = true;
                } else {
                    // printf("No :(!\n");
                }
                break;
            case REPLACEMENT_TYPE_BY_PATH:
                // DEBUG_FUNCTION_LINE("Check REPLACEMENT_TYPE_BY_PATH for %s (%s) == %s... ", cur->replaceName,
                //                    cur->replacementPath, request->name);
                if (strncmp(cur->replaceName, request->name, sizeof(request->name) - 1) == 0) {
                    // printf("Yay!\n");
                    valid = true;
                } else {
                    // printf("No :(!\n");
                }
                break;
            case REPLACEMENT_TYPE_UNTIL_MEM_HOOK_COMPLETED_BUT_BY_PATH:
                // DEBUG_FUNCTION_LINE("Check REPLACEMENT_TYPE_UNTIL_MEM_HOOK_COMPLETED_BUT_BY_PATH for %s (%s) == %s... ", cur->replaceName,
                //                    cur->replacementPath, request->name);
                if (strncmp(cur->replaceName, request->name, sizeof(request->name) - 1) == 0 && !gMemHookCompleted) {
                    // printf("Yay!\n");
                    valid = true;
                } else {
                    // printf("No :(!\n");
                }
                break;
            case REPLACEMENT_TYPE_INVALID:
                DEBUG_FUNCTION_LINE("REPLACEMENT_TYPE_INVALID\n");
                break;
        }
        if (valid) {
            *offset = i + 1;
            return cur;
        }
    }
    return NULL;
}

static uint32_t getReplacementDataInSingleArray(const RPXFileReplacements **tmpArray, uint32_t tmpArraySize) {
    uint32_t offsetInResult = 0;

    for (uint32_t i = 0; i < sizeof(gDynamicReplacements) / sizeof(gDynamicReplacements[0]); i++) {
        if (offsetInResult >= tmpArraySize) {
            DEBUG_FUNCTION_LINE("ELEMENTS DO NOT FIT INTO ARRAY");
            return offsetInResult;
        }
        if (gDynamicReplacements[i] != NULL) {
            tmpArray[offsetInResult] = gDynamicReplacements[i];
            offsetInResult++;
        }
    }

    for (uint32_t i = 0; i < sizeof(gDefaultReplacements) / sizeof(gDefaultReplacements[0]); i++) {
        if (offsetInResult >= tmpArraySize) {
            DEBUG_FUNCTION_LINE("ELEMENTS DO NOT FIT INTO ARRAY");
            return offsetInResult;
        }
        if (gDefaultReplacements[i].lifetime != REPLACEMENT_LIFETIME_INVALID) {
            tmpArray[offsetInResult] = &gDefaultReplacements[i];
            offsetInResult++;
        }
    }
    return offsetInResult;
}

const RPXFileReplacements *GetCurrentRPXReplacement(MCPLoadFileRequest *request, uint32_t *offset) {
    const RPXFileReplacements *tmpArray[TEMP_ARRAY_SIZE] = {};
    uint32_t elementsInArray                             = getReplacementDataInSingleArray(tmpArray, TEMP_ARRAY_SIZE);
    if (elementsInArray == 0) {
        return NULL;
    }

    return GetCurrentRPXReplacementEx(request, tmpArray, elementsInArray, offset);
}

int DoReplacementByStruct(ipcmessage *msg, MCPLoadFileRequest *request, const RPXFileReplacements *curReplacement) {
    char _rpxpath[256] = {};

    LoadTargetDevice target = MOCHA_LOAD_TARGET_DEVICE_NONE;
    if (curReplacement->relativeTo == PATH_RELATIVE_TO_ENVIRONMENT) {
        char *environmentPath = &((char *) 0x0511FF00)[19];
        if (!MCPJoinPath(_rpxpath, sizeof(_rpxpath), environmentPath, 0x100 - 19,
                         curReplacement->replacementPath, sizeof(curReplacement->replacementPath))) return -1;
        target = MOCHA_LOAD_TARGET_DEVICE_SD;
    } else if (curReplacement->relativeTo == PATH_RELATIVE_TO_SD_ROOT) {
        strncpy(_rpxpath, curReplacement->replacementPath, sizeof(_rpxpath) - 1);
        target = MOCHA_LOAD_TARGET_DEVICE_SD;
    } else if (curReplacement->relativeTo == PATH_RELATIVE_TO_USB_ROOT) {
        strncpy(_rpxpath, curReplacement->replacementPath, sizeof(_rpxpath)-1);
        target=MOCHA_LOAD_TARGET_DEVICE_USB;
    } else if (curReplacement->relativeTo == PATH_RELATIVE_TO_MLC_ROOT) {
        if (!MCPJoinPath(_rpxpath, sizeof(_rpxpath), "/vol/storage_mlc01", sizeof("/vol/storage_mlc01"),
                         curReplacement->replacementPath, sizeof(curReplacement->replacementPath))) return -1;
        target = MOCHA_LOAD_TARGET_DEVICE_MLC;
    } else {
        DEBUG_FUNCTION_LINE("Unknown relativeTo: %d\n", curReplacement->relativeTo);
        return -1;
    }

    if (gMenuLoadTrace) DEBUG_FUNCTION_LINE("Menu replacement path ready: %s\n", _rpxpath);
    return MCP_LoadCustomFile(target,
                              _rpxpath,
                              curReplacement->fileSize,
                              curReplacement->fileOffset,
                              msg->ioctl.buffer_io,
                              msg->ioctl.length_io,
                              request->pos, EndsWith(request->name, ".rpx"));
}

int MCPLoadFileReplacement(ipcmessage *msg, MCPLoadFileRequest *request) {
    int res;

    if ((res = DoSDRedirectionByPath(msg, request)) >= 0) {
        // DEBUG_FUNCTION_LINE("We replaced by path!\n");
        return res;
    }

    // Try any fitting rpx replacement
    uint32_t offset                           = 0;
    const RPXFileReplacements *curReplacement = NULL;
    do {
        // the offset is used as input AND output.
        // The start index for looking for a replacement will be read from "offset"
        // if the function returns != NULL, offset will be set to the next possible index/offset
        curReplacement = GetCurrentRPXReplacement(request, &offset);
        if (gMenuLoadTrace) DEBUG_FUNCTION_LINE("Menu replacement selected offset=%lu found=%d\n",
                                                (unsigned long) offset, curReplacement != NULL);
        if (curReplacement == NULL) {
            // DEBUG_FUNCTION_LINE("Couldn't find replacement for %s!\n", request->name);
            return -1;
        }

        res = DoReplacementByStruct(msg, request, curReplacement);
        if (gMenuLoadTrace) DEBUG_FUNCTION_LINE("Menu replacement read result=%d\n", res);
        if (res >= 0) {
            return res;
        }
    } while (curReplacement);

    return -1;
}

void IncreaseApplicationStartCounter() {
    for (uint32_t i = 0; i < sizeof(gDynamicReplacements) / sizeof(gDynamicReplacements[0]); i++) {
        if (gDynamicReplacements[i] != NULL) {
            gDynamicReplacements[i]->ageInApplicationStarts++;
        }
    }
}

bool hasHomebrewReplacementsEx(const RPXFileReplacements **list, uint32_t list_size) {
    for (uint32_t i = 0; i < list_size; i++) {
        const RPXFileReplacements *cur = list[i];
        if (cur != NULL && cur->type != REPLACEMENT_TYPE_INVALID) {
            if (cur->type == REPLACEMENT_TYPE_HOMEBREW_RPX) {
                return true;
            }
        }
    }
    return false;
}

bool hasHomebrewReplacements() {
    const RPXFileReplacements *tmpArray[TEMP_ARRAY_SIZE] = {};
    uint32_t elementsInArray                             = getReplacementDataInSingleArray(tmpArray, TEMP_ARRAY_SIZE);
    if (elementsInArray == 0) {
        return false;
    }

    if (!hasHomebrewReplacementsEx(tmpArray, elementsInArray)) {
        // DEBUG_FUNCTION_LINE("Has no homebrew replacements\n");
        return false;
    }
    return true;
}

int _MCP_LoadFile_patch(ipcmessage *msg) {
    MCPLoadFileRequest *request = (MCPLoadFileRequest *) msg->ioctl.buffer_in;
    if (strncmp(request->name, "men.rpx", sizeof(request->name)) == 0 && request->pos == 0)
        gMenuLoadTrace = 96;
    const bool traceMenu = gMenuLoadTrace != 0;
    if (traceMenu) {
        --gMenuLoadTrace;
        /* IOSU's printf does not implement string precision (%.64s). Log the
           numeric request fields without shifting its variadic arguments. */
        DEBUG_FUNCTION_LINE("Menu load begin pid=%lu type=%lu pos=%lu len=%lu\n",
                            (unsigned long) request->cafe_pid, (unsigned long) request->type,
                            (unsigned long) request->pos, (unsigned long) msg->ioctl.length_io);
    }

    // we only care about Foreground app/COS-MASTER for now.
    if (request->cafe_pid != 7) {
        // DEBUG_FUNCTION_LINE("Not for pid 7, lets ignore\n");
        int nativeResult = real_MCP_LoadFile(msg);
        if (traceMenu) DEBUG_FUNCTION_LINE("Menu native pid result=%d\n", nativeResult);
        return nativeResult;
    }

    bool requestIsRPX = EndsWith(request->name, ".rpx");

    // Check if a fresh RPX is loaded
    if (request->pos == 0 && requestIsRPX) {
        if (traceMenu) DEBUG_FUNCTION_LINE("Menu load reset scope begin\n");
        RawRplBeginExecutable(&gRawRplScope, request->name);
        if(!gRawRplScope.prepared){RawUsbClear();gRawUsbScope=false;}
        // DEBUG_FUNCTION_LINE("fresh RPX load\n");
        // Remove any old replacements that should only survive one RPX replacement
        RemoveByLifetime(REPLACEMENT_LIFETIME_DURING_RPX_REPLACEMENT);
        // Increase of the all replacements when we load a new RPX!
        IncreaseApplicationStartCounter();
        sReplacedLastRPX = false;
        if (traceMenu) DEBUG_FUNCTION_LINE("Menu load reset scope complete\n");
    }

    int res;
    if (traceMenu) DEBUG_FUNCTION_LINE("Menu replacement lookup begin\n");
    if ((res = MCPLoadFileReplacement(msg, request)) >= 0) {
        if (requestIsRPX) {
            sReplacedLastRPX = true;
            RawRplExecutableLoaded(&gRawRplScope);
        }
        if (traceMenu) DEBUG_FUNCTION_LINE("Menu replacement result=%d\n", res);
        return res;
    }

    /* Static imports are requested before APPLICATION_STARTS installs PPC FS
       redirection. Resolve game-local RPLs here, also for later OSDynLoad calls.
       CafeOS requests and explicit Aroma replacements retain priority. */
    char libraryPath[256];
    if (RawRplPath(&gRawRplScope, request->name, request->cafe_pid,
                   request->type, libraryPath)) {
        res = MCP_LoadCustomFile(gRawUsbScope?MOCHA_LOAD_TARGET_DEVICE_USB:MOCHA_LOAD_TARGET_DEVICE_SD, libraryPath, 0, 0,
                                msg->ioctl.buffer_io, msg->ioctl.length_io,
                                request->pos, false);
        if (res >= 0) return res;
    }
    if (traceMenu) DEBUG_FUNCTION_LINE("Menu native fallback begin\n");
    res = real_MCP_LoadFile(msg);
    if (traceMenu) DEBUG_FUNCTION_LINE("Menu native fallback result=%d\n", res);
    return res;
}

void _MassEffect3LaunchDetected() {
    DEBUG_FUNCTION_LINE("Start of Mass Effects 3 detected. Do NOT keep aroma loaded on iosu reload\n");
    svcCustomKernelCommand(KERNEL_WRITE32, 0x0812A120, 0xebffffca);
}

int _MCP_ReadCOSXml_patch(uint32_t u1, uint32_t u2, MCPPPrepareTitleInfo *xmlData) {
    int (*const real_MCP_ReadCOSXml_patch)(uint32_t u1, uint32_t u2, MCPPPrepareTitleInfo * xmlData) = (void *) 0x050024ec + 1; //+1 for thumb

    int res = real_MCP_ReadCOSXml_patch(u1, u2, xmlData);

    /*
    For some reason ONE PIECE Unlimited World Red softlocks when it has full FSA permissions.
    */
    if (xmlData->titleId != 0x0005000010175C00 && // ONE PIECE Unlimited World Red USA
        xmlData->titleId != 0x0005000010175D00 && // ONE PIECE Unlimited World Red EUR
        xmlData->titleId != 0x0005000010148000) { // ONE PIECE Unlimited World Red JPN

        // Give us sd access!
        xmlData->permissions[4].mask = 0xFFFFFFFFFFFFFFFF;
    }
    /*
    For some reason Mass Effects 3 softlocks when it has full ACP permissions.
    */
    if (xmlData->titleId != 0x000500001010DC00 && // Mass Effect 3 Special Edition USA
        xmlData->titleId != 0x000500001010F500 && // Mass Effect 3 Special Edition EUR
        xmlData->titleId != 0x0005000010113000) { // Mass Effect 3 Special Edition JPN

        // Give all titles permission to use ACP
        xmlData->permissions[10].mask = 0xFFFFFFFFFFFFFFFF;
    }

    // if we replace the RPX we want to increase the max_codesize and give us full permission!
    if (hasHomebrewReplacements()) {
        if (xmlData->titleId == 0x000500101004E000 || // H&S
            xmlData->titleId == 0x000500101004E100 ||
            xmlData->titleId == 0x000500101004E200 ||
            xmlData->titleId == 0x000500101004C000 || // Daily log
            xmlData->titleId == 0x000500101004C100 ||
            xmlData->titleId == 0x000500101004C200) {
            xmlData->codegen_size = 0x02000000;
            xmlData->codegen_core = 0x80000001;
            xmlData->max_size     = 0x40000000;

            // Set maximum codesize to 64 MiB
            xmlData->max_codesize  = 0x04000000;
            xmlData->avail_size    = 0;
            xmlData->overlay_arena = 0;

            bool dlpPatched = false;

            // Give us full permissions everywhere
            for (uint32_t i = 0; i < 19; i++) {
                // Add DLP permission group to all titles
                if (!dlpPatched && xmlData->permissions[i].group == 0) {
                    xmlData->permissions[i].group = 25; // DLP
                    dlpPatched                    = true;
                }

                xmlData->permissions[i].mask = 0xFFFFFFFFFFFFFFFF;
            }

            xmlData->default_stack0_size   = 0;
            xmlData->default_stack1_size   = 0;
            xmlData->default_stack2_size   = 0;
            xmlData->default_redzone0_size = 0;
            xmlData->default_redzone1_size = 0;
            xmlData->default_redzone2_size = 0;
            xmlData->exception_stack0_size = 0x00001000;
            xmlData->exception_stack1_size = 0x00001000;
            xmlData->exception_stack2_size = 0x00001000;

            /* Keep MCP's installed wrapper profile intact. The raw title profile
               is applied later by the PPC PrepareTitle hook, after MCP has
               successfully prepared the real installed wrapper. */
        }
    }

    // When the PPC Kernel reboots we replace the men.rpx to set up our PPC side again
    // for this the Wii U Menu temporarily gets replaced by our root.rpx and needs code gen access
    if (!gMemHookCompleted) {
        if (xmlData->titleId == 0x0005001010040000 ||  // Wii U Menu
            xmlData->titleId == 0x0005001010040100 ||  // Wii U Menu
            xmlData->titleId == 0x0005001010040200 ||  // Wii U Menu
            xmlData->titleId == 0x0005001010062000L || // System Transfer
            xmlData->titleId == 0x0005001010062100L || // System Transfer
            xmlData->titleId == 0x0005001010062200L) { // System Transfer

            xmlData->codegen_size = 0x02000000;
            xmlData->codegen_core = 0x80000001;
            xmlData->max_codesize = 0x02800000;
        }
    }

    return res;
}

extern int _startMainThread(void);

bool sInitIOPShellDone = false;
void InitIOPShell() {
    if (sInitIOPShellDone) return;
    svcCustomKernelCommand(KERNEL_WRITE32, 0xe4007828, 0xe3a00000);

    usleep(1000 * 10);

    int handle = svcOpen("/dev/testproc1", 0);
    if (handle > 0) {
        svcResume(handle);
        svcClose(handle);
    }
    sInitIOPShellDone = true;
}

/*  RPX replacement! Call this ioctl to replace the next loaded RPX with an arbitrary path.
    DO NOT RETURN 0, this affects the codepaths back in the IOSU code */
int _MCP_ioctl100_patch(ipcmessage *msg) {
    if (msg->ioctl.buffer_in && msg->ioctl.length_in >= 4) {
        int command = msg->ioctl.buffer_in[0];

        switch (command) {
            case UFLOW_USB_MAP_COMMAND: {
                if(!gRawTitleProfileActive || msg->ioctl.length_in!=sizeof(RawUsbMap)+4 ||
                   !msg->ioctl.buffer_io || msg->ioctl.length_io<4)return 29;
                int result=RawUsbInstall((uint8_t*)msg->ioctl.buffer_in+4,sizeof(RawUsbMap));
                if(result<0)return 29;
                msg->ioctl.buffer_io[0]=UFLOW_USB_MAP_MAGIC;
                return 0;
            }
            case IPC_CUSTOM_SET_RAW_TITLE_PROFILE: {
                if (msg->ioctl.length_in < sizeof(RawTitleProfile) + sizeof(uint32_t)) {
                    return 29;
                }
                RawTitleProfile *profile = (RawTitleProfile *) ((uint8_t *) msg->ioctl.buffer_in + sizeof(uint32_t));
                if (profile->magic != RAW_TITLE_PROFILE_MAGIC ||
                    profile->profileVersion != RAW_TITLE_PROFILE_VERSION) {
                    return 29;
                }
                memcpy(gRawTitleExecutable, profile->argstr, sizeof(gRawTitleExecutable) - 1);
                gRawTitleExecutable[sizeof(gRawTitleExecutable) - 1] = '\0';
                RawRplReset(&gRawRplScope);
                RawUsbClear();gRawUsbScope=false;
                gRawTitleProfileActive = profile->enabled != 0;
                if (msg->ioctl.buffer_io && msg->ioctl.length_io >= sizeof(uint32_t)) {
                    msg->ioctl.buffer_io[0] = RAW_TITLE_PROFILE_MAGIC;
                }
                DEBUG_FUNCTION_LINE("Raw title profile %s\n", gRawTitleProfileActive ? "armed" : "cleared");
                return 0;
            }
            case IPC_CUSTOM_MEN_RPX_HOOK_COMPLETED: {
                gMemHookCompleted = true;
                break;
            }
            case IPC_CUSTOM_LOAD_CUSTOM_RPX: {
                RawRplReset(&gRawRplScope);
                if (msg->ioctl.length_in >= sizeof(MochaRPXLoadInfo) + 4) {
                    int target = msg->ioctl.buffer_in[0x04 / 0x04];
                    if (target == LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE) {
                        RawUsbClear();gRawUsbScope=false;
                        RemoveByLifetime(REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH);
                        return 0;
                    }
                    if (target != LOAD_RPX_TARGET_SD_CARD && target != UFLOW_USB_TARGET) {
                        return 29;
                    }
                    uint32_t filesize   = msg->ioctl.buffer_in[0x08 / 0x04];
                    uint32_t fileoffset = msg->ioctl.buffer_in[0x0C / 0x04];
                    char *str_ptr       = (char *) &msg->ioctl.buffer_in[0x10 / 0x04];
                    const bool usb=target==UFLOW_USB_TARGET;
                    if(usb && (!gRawTitleProfileActive || fileoffset || filesize ||
                       !RawUsbSafePath(str_ptr)||!RawUsbHasFile(str_ptr)))return 29;
                    if(!usb)RawUsbClear();
                    gRawUsbScope=usb;

                    RPXFileReplacements *newReplacement = svcAlloc(0xCAFF, sizeof(RPXFileReplacements));
                    if (newReplacement == NULL) {
                        DEBUG_FUNCTION_LINE("Failed to allocate memory on heap\n");
                        return 22;
                    }

                    memset(newReplacement, 0, sizeof(*newReplacement));

                    strncpy(newReplacement->replacementPath, str_ptr, sizeof(newReplacement->replacementPath) - 1);
                    newReplacement->fileOffset = fileoffset;
                    newReplacement->fileSize   = filesize;
                    newReplacement->lifetime   = REPLACEMENT_LIFETIME_ONE_RPX_LAUNCH;
                    newReplacement->relativeTo = usb?PATH_RELATIVE_TO_USB_ROOT:PATH_RELATIVE_TO_SD_ROOT;
                    newReplacement->type       = REPLACEMENT_TYPE_HOMEBREW_RPX;

                    if (!addDynamicReplacement(newReplacement)) {
                        DEBUG_FUNCTION_LINE("addDynamicReplacement failed, abort redirecting %s\n", newReplacement->replacementPath);
                        svcFree(0xCAFF, newReplacement);
                        return 22;
                    }

                    RawRplPrepare(&gRawRplScope, newReplacement->replacementPath,
                                  gRawTitleExecutable, gRawTitleProfileActive,
                                  fileoffset);

                    DEBUG_FUNCTION_LINE("Will load %s for next title from target: %d (offset %lu, filesize %lu)\n",
                                        newReplacement->replacementPath, target, fileoffset, filesize);
                    return 0;
                } else {
                    return 29;
                }
                break;
            }
            case IPC_CUSTOM_START_MCP_THREAD: {
                svcCustomKernelCommand(KERNEL_WRITE32, 0xe4007828, 0xe3a00000);

                usleep(1000 * 10);

                int handle = svcOpen("/dev/testproc1", 0);
                if (handle > 0) {
                    svcResume(handle);
                    svcClose(handle);
                }

                _startMainThread();
                return 0;
                break;
            }
            case IPC_CUSTOM_COPY_ENVIRONMENT_PATH: {
                if (msg->ioctl.buffer_io && msg->ioctl.length_io >= 0x100) {
                    strncpy((char *) msg->ioctl.buffer_io, (void *) 0x0511FF00, 0xFF);
                    return 0;
                } else {
                    return 29;
                }
            }
            case IPC_CUSTOM_START_USB_LOGGING: {
                if (*((uint32_t *) 0x050290dc) == 0x42424242) {
                    // Skip syslog after a reload
                    break;
                }

                InitIOPShell();

                // set the flag to not run this twice.
                svcCustomKernelCommand(KERNEL_WRITE32, 0x050290dc, 0x42424242);

                // Patch MCP debugmode check for usb syslog
                svcCustomKernelCommand(KERNEL_WRITE32, 0x050290d8, 0x20004770);

                usleep(1000 * 10);

                int handle = svcOpen("/dev/usb_syslog", 0);
                if (handle > 0) {
                    svcResume(handle);
                    svcClose(handle);
                }

                bool showCompleteLog = msg->ioctl.buffer_in && msg->ioctl.length_in >= 0x08 && msg->ioctl.buffer_in[1] == 1;
                if (!showCompleteLog) {
                    gSkipSyslogCatchUpForUSBOutput = true;
                }

                break;
            }
            case IPC_CUSTOM_START_IOPSHELL_SERVER: {
                InitIOPShell();
                break;
            }
            case IPC_CUSTOM_START_TCP_LOGGING: {
                bool useIPFilter  = false;
                uint32_t ipFilter = 0;
                if (msg->ioctl.buffer_in && msg->ioctl.length_in >= 0x0C) {
                    useIPFilter = msg->ioctl.buffer_in[1] == 1;
                    ipFilter    = msg->ioctl.buffer_in[2];
                }

                TCPSyslogStartThread(useIPFilter, ipFilter);
                break;
            }
            case IPC_CUSTOM_STOP_TCP_LOGGING: {
                TCPSyslogEndThread();
                break;
            }
            case IPC_CUSTOM_GET_MOCHA_API_VERSION: {
                if (msg->ioctl.buffer_io && msg->ioctl.length_io >= 8) {
                    msg->ioctl.buffer_io[0] = 0xCAFEBABE;
                    msg->ioctl.buffer_io[1] = MOCHA_API_VERSION;
                    return 0;
                } else {
                    return 29;
                }
                break;
            }
            default: {
            }
        }
    } else {
        return 29;
    }
    return 0;
}
