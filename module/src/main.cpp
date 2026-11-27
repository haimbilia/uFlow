#include "FileUtils.h"
#include "RPXLoading.h"
#include "RawSourcePath.h"
#include "RawExitTrace.h"
#include "RawPerformanceTrace.h"
#include "../../app/source/usb_storage.hpp"
#include "RawTitleKernel.h"
#include "RawHai.h"
#include "RetroArchLaunch.h"
#include "globals.h"
#include "utils/StringTools.h"
#include "utils/logger.h"
#include "version.h"
#include <content_redirection/redirection.h>
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/title.h>
#include <mocha/mocha.h>
#include <nn/act.h>
#include <cstdio>
#include <romfs_dev.h>
#include <string>
#include <sys/stat.h>
#include <sysapp/title.h>
#include <wuhb_utils/utils.h>
#include <wums.h>

#define VERSION "v0.3.6-raw-port28-local-dlc"

static void RawLauncherTrace(const char *message) {
    FILE *file = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a");
    if (file != nullptr) {
        fprintf(file, "%s\n", message);
        fclose(file);
    }
}

WUMS_MODULE_EXPORT_NAME("homebrew_rpx_loader");
WUMS_USE_WUT_DEVOPTAB();

WUMS_DEPENDS_ON(homebrew_content_redirection);
WUMS_DEPENDS_ON(homebrew_wuhb_utils);
WUMS_DEPENDS_ON(homebrew_functionpatcher);

static void RegisterFunctionPatches() {
    for (uint32_t i = 0; i < rpx_utils_function_replacements_size; i++) {
        bool wasPatched = false;
        const auto status = FunctionPatcher_AddFunctionPatch(&rpx_utils_function_replacements[i], nullptr, &wasPatched);
        // SUCCESS means registered. Dynamic RPLs (including nn_aoc/nn_acp)
        // need not be loaded at module initialization; FunctionPatcher applies
        // these registered hooks when their library is loaded into the game.
        if (status != FUNCTION_PATCHER_RESULT_SUCCESS) {
            char message[160];
            snprintf(message,sizeof(message),"homebrew_rpx_loader: hook %u registration failed (%d)",i,static_cast<int>(status));
            OSFatal(message);
        }
        if (!wasPatched) OSReport("uFlow: hook %u registered, awaiting target library/process\n",i);
    }
}

WUMS_INITIALIZE() {
    initLogging();

    if (FunctionPatcher_InitLibrary() != FUNCTION_PATCHER_RESULT_SUCCESS) {
        OSFatal("homebrew_rpx_loader: FunctionPatcher_InitLibrary failed");
    }

    DEBUG_FUNCTION_LINE("Patch functions");
    RegisterFunctionPatches();
    DEBUG_FUNCTION_LINE("Patch functions finished");
    gReplacementInfo = {};

    ContentRedirectionStatus error;
    if ((error = ContentRedirection_InitLibrary()) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init ContentRedirection. Error %d", error);
        OSFatal("Failed to init ContentRedirection.");
    }

    WUHBUtilsStatus error2;
    if ((error2 = WUHBUtils_InitLibrary()) != WUHB_UTILS_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init WUHBUtils. Error %d", error2);
        OSFatal("Failed to init WUHBUtils.");
    }


    // Init libmocha
    MochaUtilsStatus error3;
    if ((error3 = Mocha_InitLibrary()) != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Failed to init libmocha. Error %s", Mocha_GetStatusStr(error3));
        OSFatal("Failed to init libmocha. Make sure to use the latest version of MochaPayload");
    }

    // Public builds never start privileged network diagnostics.

    if (!RawTitleKernel_Initialize()) {
        OSFatal(RawTitleKernel_GetInitializeError());
    }

    deinitLogging();
}

WUMS_APPLICATION_ENDS() {
    RawPerformanceWrite();
    RawExitTracePoint("module-end-enter");
    RL_UnmountCurrentRunningBundle();
    RawExitTracePoint("bundle-layers-released");

    gReplacementInfo.rpxReplacementInfo.isRPXReplaced = false;

    gRawAssetCache.Clear();
    gRawUpdateFiles.Clear();
    RPXLoadingCleanUp();
    RawExitTracePoint("file-readers-released");
    // Raw FSA clients belong to the outgoing process; reopen on game start.
    int usbRemoved=-1;
    ContentRedirection_RemoveDevice("ufusb:",&usbRemoved);
    RawExitTracePoint("usb-redirection-device-removed");
    USBStorage::Unmount();
    RawExitTracePoint("usb-unmounted");
    // Wii (HAI) titles only; keeps the raw-title cleanup order above unchanged.
    RawHai::RemoveCompanionPatch();

    deinitLogging();
    RawExitTracePoint("module-end-return");
}

WUMS_APPLICATION_REQUESTS_EXIT() {
    RawExitTracePoint("exit-request-callback");
}

WUMS_ALL_APPLICATION_REQUESTS_EXIT_DONE() {
    RawExitTracePoint("all-exit-request-callbacks-done");
}

WUMS_ALL_APPLICATION_ENDS_DONE() {
    // Only OSReport here: WUT filesystem teardown may already have started.
    RawExitTracePoint("all-module-end-callbacks-done");
}

WUMS_APPLICATION_STARTS() {
    RawPerformanceBegin(gLooseTitleInfo.pending);
    RawExitTraceBegin(gLooseTitleInfo.pending);
    OSReport("uFlow loader: application-start entry pid=%u\n", OSGetUPID());
    UFlowReturnApplicationStarts();
    OSReport("uFlow loader: return gate initialized\n");
    uint32_t upid = OSGetUPID();
    char traceLine[192];
    snprintf(traceLine, sizeof(traceLine),
             "application starts: upid=%u title=%08X%08X pending=%d active=%d replace=%d",
             upid, (uint32_t) (OSGetTitleID() >> 32), (uint32_t) OSGetTitleID(),
             gLooseTitleInfo.pending, gLooseTitleInfo.active,
             gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced);
    RawLauncherTrace(traceLine);
    if (upid != 2 && upid != 15) {
        return;
    }
    OSReport("Running RPXLoadingModule " VERSION VERSION_EXTRA "\n");
    initLogging();
    if (gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced) {
        gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = false;
        gReplacementInfo.rpxReplacementInfo.isRPXReplaced     = true;
    }

    if (!gLooseTitleInfo.pending && !gLooseTitleInfo.active &&
        _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY) != OSGetTitleID()) {
        gReplacementInfo.lastFileLoaded[0] = '\0';
    }

    gActiveSaveRedirectionPath.clear();

    if (gLooseTitleInfo.pending) {
        // Final PrepareTitle is complete. Stop patching before game execution.
        OSReport("uFlow raw launch kernel: %s\n", RawTitleKernel_GetProbe());
        RawLauncherTrace(RawTitleKernel_GetProbe());
        RawTitleKernel_Clear();
        RawLauncherTrace("raw redirect setup begin");
        nn::act::Initialize();
        nn::act::PersistentId persistentId = nn::act::GetPersistentId();
        nn::act::Finalize();

        if(RawSourceUSB(gLooseTitleInfo.contentPath)&&!USBStorage::MountRuntime()) {
            OSFatal("uFlow: USB source unavailable or changed during launch. Reconnect the original drive and restart.");
        }
        std::string contentPath = RawSourcePath(gLooseTitleInfo.contentPath);
        std::string codePath    = RawSourcePath(gLooseTitleInfo.codePath);
        std::string metaPath    = contentPath.substr(0, contentPath.size() - strlen("content")) + "meta";
        std::string savePath    = std::string("fs:/vol/external01/") + gLooseTitleInfo.savePath;
        std::string commonPath  = savePath + "/common";
        std::string userPath    = savePath + string_format("/%08X", 0x80000000 | persistentId);

        CreateSubfolder(commonPath.c_str());
        CreateSubfolder(userPath.c_str());

        auto contentResult = ContentRedirection_AddFSLayer(&contentLayerHandle,
                                                           "Raw title content",
                                                           contentPath.c_str(),
                                                           FS_LAYER_TYPE_CONTENT_REPLACE);
        auto codeResult = ContentRedirection_AddFSLayerEx(&codeLayerHandle,
                                                         "Raw title code",
                                                         "/vol/code",
                                                         codePath.c_str(),
                                                         FS_LAYER_TYPE_EX_REPLACE_DIRECTORY);
        ContentRedirectionStatus updateResult=CONTENT_REDIRECTION_RESULT_SUCCESS;
        ContentRedirectionStatus dlcResult=CONTENT_REDIRECTION_RESULT_SUCCESS;
        if(gLooseTitleInfo.updateContentPath[0]) {
            const auto update=RawSourcePath(gLooseTitleInfo.updateContentPath);
            // Newer layers run first; missing update files continue to base.
            updateResult=ContentRedirection_AddFSLayer(&updateLayerHandle,"Raw title update",
                update.c_str(),FS_LAYER_TYPE_CONTENT_MERGE);
        }
        if(gLooseTitleInfo.dlcPath[0]) {
            const auto dlc=RawSourcePath(gLooseTitleInfo.dlcPath);
            dlcResult=ContentRedirection_AddFSLayerEx(&dlcLayerHandle,"Raw title DLC",
                gLooseTitleInfo.dlcVolume,dlc.c_str(),FS_LAYER_TYPE_EX_REPLACE_DIRECTORY);
        }
        ContentRedirectionStatus metaResult = CONTENT_REDIRECTION_RESULT_SUCCESS;
        struct stat metaInfo {};
        if (stat(metaPath.c_str(), &metaInfo) == 0 && S_ISDIR(metaInfo.st_mode)) {
            metaResult = ContentRedirection_AddFSLayerEx(&metaLayerHandle,
                                                        "Raw title meta",
                                                        "/vol/meta",
                                                        metaPath.c_str(),
                                                        FS_LAYER_TYPE_EX_REPLACE_DIRECTORY);
        }
        auto saveResult = ContentRedirection_AddFSLayer(&saveLayerHandle,
                                                       "Raw title save",
                                                       savePath.c_str(),
                                                       FS_LAYER_TYPE_SAVE_REPLACE);
        snprintf(traceLine, sizeof(traceLine),
                 "raw redirect results: content=%d code=%d meta=%d save=%d",
                 contentResult, codeResult, metaResult, saveResult);
        RawLauncherTrace(traceLine);
        if (contentResult != CONTENT_REDIRECTION_RESULT_SUCCESS ||
            updateResult != CONTENT_REDIRECTION_RESULT_SUCCESS ||
            dlcResult != CONTENT_REDIRECTION_RESULT_SUCCESS ||
            codeResult != CONTENT_REDIRECTION_RESULT_SUCCESS ||
            metaResult != CONTENT_REDIRECTION_RESULT_SUCCESS ||
            saveResult != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE_ERR("Raw title redirection failed: content=%d code=%d meta=%d save=%d",
                                    contentResult, codeResult, metaResult, saveResult);
            OSFatal("Wii U Raw Launcher: failed to install filesystem redirection");
        }

        RawLauncherTrace("update index load begin");
        if(gLooseTitleInfo.updateContentPath[0] &&
           !gRawUpdateFiles.Load(codePath+"/uflow-update-index.txt",gLooseTitleInfo.updateContentPath)) {
            RawLauncherTrace("update index load FAILED");
            OSFatal("uFlow: unable to read the prepared update index. Relaunch from uFlow.");
        }
        snprintf(traceLine,sizeof(traceLine),"update index ready: files=%u",static_cast<unsigned>(gRawUpdateFiles.Size()));
        RawLauncherTrace(traceLine);
        gRawAssetCache.Clear();
        if(!RawSourceUSB(gLooseTitleInfo.contentPath)) {
            const auto identity=RawAssetHex(gLooseTitleInfo.assetFingerprint);
            const auto cache=std::string("wiiu/cache/raw-assets/")+RawAssetHex(gLooseTitleInfo.baseTitleId)+"/"+identity;
            const bool loaded=gRawAssetCache.Load(RawSourcePath(cache.c_str()),cache,identity);
            snprintf(traceLine,sizeof(traceLine),"asset cache: loaded=%d files=%u",loaded,static_cast<unsigned>(gRawAssetCache.Size()));
            RawLauncherTrace(traceLine);
        }
        RawHai::InstallCompanionPatch();
        gLooseTitleInfo.pending = false;
        gLooseTitleInfo.active  = true;
        gActiveSaveRedirectionPath = gLooseTitleInfo.savePath;
        OSMemoryBarrier();
        RawLauncherTrace("raw redirect setup complete");
        DEBUG_FUNCTION_LINE("Raw title ready: content=%s code=%s save=%s",
                            contentPath.c_str(), codePath.c_str(), savePath.c_str());
        return;
    }

    if (_SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY) == OSGetTitleID() &&
        strlen(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) > 0) {
        uint32_t currentHash           = StringTools::hash(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath);
        std::string shortNameSanitized = sanitizeName(gReplacementInfo.rpxReplacementInfo.metaInformation.shortname);

        nn::act::Initialize();
        nn::act::PersistentId persistentId = nn::act::GetPersistentId();
        nn::act::Finalize();

        std::string relativePath = string_format("wiiu/apps/save/%08X", currentHash);
        if (!shortNameSanitized.empty()) {
            relativePath += string_format(" (%s)", shortNameSanitized.c_str());
        }
        std::string basePath = "fs:/vol/external01/" + relativePath;

        std::string common = basePath + "/common";
        std::string user   = basePath + string_format("/%08X", 0x80000000 | persistentId);

        CreateSubfolder(common.c_str());
        CreateSubfolder(user.c_str());
        DEBUG_FUNCTION_LINE("Created %s and %s", common.c_str(), user.c_str());

        if (romfsMount(WUHB_ROMFS_NAME, gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath, RomfsSource_FileDescriptor_CafeOS) == 0) {
            auto device = GetDeviceOpTab(WUHB_ROMFS_PATH);
            if (device == nullptr || strcmp(device->name, WUHB_ROMFS_NAME) != 0) {
                romfsUnmount(WUHB_ROMFS_NAME);
                gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted = false;
                DEBUG_FUNCTION_LINE_ERR("DeviceOpTab for %s not found.", WUHB_ROMFS_PATH);
                return;
            }
            int outRes = -1;
            if (ContentRedirection_AddDevice(device, &outRes) != CONTENT_REDIRECTION_RESULT_SUCCESS || outRes < 0) {
                DEBUG_FUNCTION_LINE_ERR("Failed to AddDevice to ContentRedirection");
                romfsUnmount(WUHB_ROMFS_NAME);
                gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted = false;
                return;
            }
            auto res = ContentRedirection_AddFSLayer(&contentLayerHandle,
                                                     "WUHB Content",
                                                     WUHB_ROMFS_CONTENT_PATH,
                                                     FS_LAYER_TYPE_CONTENT_REPLACE);
            if (res == CONTENT_REDIRECTION_RESULT_SUCCESS) {
                res = ContentRedirection_AddFSLayer(&saveLayerHandle,
                                                    "WUHB Save",
                                                    basePath.c_str(),
                                                    FS_LAYER_TYPE_SAVE_REPLACE);
                if (res == CONTENT_REDIRECTION_RESULT_SUCCESS) {
                    gReplacementInfo.contentReplacementInfo.bundleMountInformation.mountedPath[0] = '\0';
                    strncpy(gReplacementInfo.contentReplacementInfo.bundleMountInformation.mountedPath,
                            gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath,
                            sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.mountedPath));
                    DEBUG_FUNCTION_LINE_VERBOSE("Mounted %s to /vol/content", gReplacementInfo.contentReplacementInfo.bundleMountInformation.mountedPath);
                    gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted      = true;
                    gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath[0] = '\0';

                    gActiveSaveRedirectionPath = relativePath;

                    OSMemoryBarrier();
                    return;
                } else {
                    if (contentLayerHandle != 0) {
                        ContentRedirection_RemoveFSLayer(contentLayerHandle);
                        contentLayerHandle = 0;
                    }
                    DEBUG_FUNCTION_LINE_ERR("ContentRedirection_AddFSLayer had failed for save");
                }
            } else {
                int outRes = -1;
                if (ContentRedirection_RemoveDevice(WUHB_ROMFS_PATH, &outRes) != CONTENT_REDIRECTION_RESULT_SUCCESS || res < 0) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to remove device");
                }
                romfsUnmount(WUHB_ROMFS_PATH);
                DEBUG_FUNCTION_LINE_ERR("ContentRedirection_AddFSLayer had failed for content");
            }
        }
        DEBUG_FUNCTION_LINE_ERR("Failed to mount %s", gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath);
        gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted = false;
        OSMemoryBarrier();
    }
}
