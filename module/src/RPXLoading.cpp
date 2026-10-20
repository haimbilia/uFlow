#include "RPXLoading.h"
#include "RetroArchLaunch.h"
#include "RawSourcePath.h"
#include "RawExitTrace.h"
#include "../../app/source/usb_storage.hpp"
#include "../../payload/source/common/raw_usb_map.h"
#include "RawTitleKernel.h"
#include "RawHai.h"
#include "RawTitleProfile.h"
#include "RawReadRange.h"
#include "RawNativePath.h"
#include "RawNativePathStorage.h"
#include "RawFrameStats.h"
#include "../../app/source/return_to_uflow.hpp"
#include "data/defaultIcon.h"
#include "globals.h"
#include "utils/FileReader.h"
#include "utils/StringTools.h"
#include "utils/ini.h"
#include "utils/logger.h"
#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/exit.h>
#include <coreinit/thread.h>
#include <coreinit/ios.h>
#include <coreinit/messagequeue.h>
#include <coreinit/title.h>
#include <coreinit/time.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mocha/mocha.h>
#include <mutex>
#include <nn/acp/title.h>
#include <romfs_dev.h>
#include <rpxloader/rpxloader.h>
#include <proc_ui/procui.h>
#include <string>
#include <sysapp/title.h>
#include <wuhb_utils/utils.h>

std::mutex fileReaderListMutex;
std::forward_list<std::unique_ptr<FileReader>> openFileReaders;

void RPXLoadingCleanUp() {
    const std::lock_guard<std::mutex> lock(fileReaderListMutex);
    openFileReaders.clear();
}

/*
 * Patch the meta xml for the home menu
 */
DECL_FUNCTION(int32_t, HBM_NN_ACP_ACPGetTitleMetaXmlByDevice, uint32_t titleid_upper, uint32_t titleid_lower, ACPMetaXml *metaxml, uint32_t device) {
    if (gReplacementInfo.rpxReplacementInfo.isRPXReplaced) {
        memset(&metaxml->longname_ja, 0, 0x338C - 0x38C); // clear all names

        snprintf(metaxml->longname_en, sizeof(metaxml->longname_en), "%s", gReplacementInfo.rpxReplacementInfo.metaInformation.longname);
        snprintf(metaxml->shortname_en, sizeof(metaxml->shortname_en), "%s", gReplacementInfo.rpxReplacementInfo.metaInformation.shortname);
        snprintf(metaxml->publisher_en, sizeof(metaxml->publisher_en), "%s", gReplacementInfo.rpxReplacementInfo.metaInformation.author);

        // Disable the emanual
        metaxml->e_manual    = 0;
        metaxml->closing_msg = 0;

        return 0;
    }
    int result = real_HBM_NN_ACP_ACPGetTitleMetaXmlByDevice(titleid_upper, titleid_lower, metaxml, device);
    return result;
}

DECL_FUNCTION(int, RPX_FSOpenFile, FSClient *client, FSCmdBlock *block, char *path, const char *mode, uint32_t *handle, int error) {
    const char *iconTex       = "iconTex.tga";
    std::string_view pathView = path;
    if (gReplacementInfo.rpxReplacementInfo.isRPXReplaced && pathView.ends_with(iconTex)) {
        const std::lock_guard<std::mutex> lock(fileReaderListMutex);
        auto reader = make_unique_nothrow<FileReader>(reinterpret_cast<uint8_t *>(gReplacementInfo.rpxReplacementInfo.iconCache), ICON_SIZE);
        if (!reader) {
            DEBUG_FUNCTION_LINE_ERR("Failed to allocate memory for the FileReader");
            return FS_STATUS_FATAL_ERROR;
        }
        *handle = reader->getHandle();
        openFileReaders.push_front(std::move(reader));
        return FS_STATUS_OK;
    }
    int result = real_RPX_FSOpenFile(client, block, path, mode, handle, error);
    return result;
}

DECL_FUNCTION(FSStatus, RPX_FSReadFile, FSClient *client, FSCmdBlock *block, uint8_t *buffer, uint32_t size, uint32_t count, FSFileHandle handle, uint32_t unk1, uint32_t flags) {
    if (gReplacementInfo.rpxReplacementInfo.isRPXReplaced) {
        const std::lock_guard<std::mutex> lock(fileReaderListMutex);
        for (auto &reader : openFileReaders) {
            if ((uint32_t) reader->getHandle() == (uint32_t) handle) {
                return (FSStatus) (reader->read(buffer, size * count) / size);
            }
        }
    }
    return real_RPX_FSReadFile(client, block, buffer, size, count, handle, unk1, flags);
}

DECL_FUNCTION(FSStatus, RPX_FSCloseFile, FSClient *client, FSCmdBlock *block, FSFileHandle handle, uint32_t flags) {
    if (gReplacementInfo.rpxReplacementInfo.isRPXReplaced) {
        if (remove_locked_first_if(fileReaderListMutex, openFileReaders, [handle](auto &cur) { return cur->getHandle() == (uint32_t) handle; })) {
            return FS_STATUS_OK;
        }
    }

    return real_RPX_FSCloseFile(client, block, handle, flags);
}

RPXLoaderStatus RL_PrepareLaunchFromSD(const char *bundle_path);
RPXLoaderStatus RL_LaunchHomebrew(const char *bundle_path);

static UFlowReturn::MenuGate sCheckUFlowReturn;
void UFlowReturnApplicationStarts() {
    sCheckUFlowReturn.Begin(OSGetTitleID()==_SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU));
}

// Run on Menu's normal foreground message pump, not while the closing game is
// saving/tearing down, and not inside an application-start loader callback.
DECL_FUNCTION(ProcUIStatus, UFlow_ProcUIProcessMessages, BOOL block) {
    const auto status=real_UFlow_ProcUIProcessMessages(block);
    if(sCheckUFlowReturn.Poll(status==PROCUI_STATUS_IN_FOREGROUND)&&UFlowReturn::Consume()) {
        const auto result=RL_LaunchHomebrew(UFlowReturn::RelativeTarget);
        OSReport("uFlow return: launch result=%d\n",result);
        if(FILE *log=std::fopen("fs:/vol/external01/wiiu/apps/uFlow/return-to-uflow.log","a")) {
            std::fprintf(log,"Menu foreground: return launch result=%d\n",result);std::fclose(log);
        }
    }
    return status;
}

// Diagnostic boundaries. Forward every call/argument/status unchanged; never
// perform filesystem I/O, wait, cancel threads or force process exit here.
static RawExitTraceState sRawExitTrace;
void RawExitTraceBegin(bool raw){sRawExitTrace.Begin(raw);}
void RawExitTracePoint(const char *point){
    if(sRawExitTrace.Enabled())OSReport("uFlow exit-trace v1: %s thread=%p\n",point,OSGetCurrentThread());
}
DECL_FUNCTION(ProcUIStatus, Raw_TraceProcUIProcessMessages, BOOL block){
    const auto result=real_Raw_TraceProcUIProcessMessages(block);
    if(sRawExitTrace.Changed(static_cast<int>(result)))
        OSReport("uFlow exit-trace v1: procui-status=%d thread=%p\n",static_cast<int>(result),OSGetCurrentThread());
    return result;
}
DECL_FUNCTION(void, Raw_TraceProcUIShutdown){
    RawExitTracePoint("procui-shutdown-enter");
    real_Raw_TraceProcUIShutdown();
    RawExitTracePoint("procui-shutdown-return");
}
DECL_FUNCTION(void, Raw_TraceExit, int code){
    RawExitTracePoint("coreinit-exit-enter");
    real_Raw_TraceExit(code);
    RawExitTracePoint("coreinit-exit-return");
}

static std::atomic<uint32_t> sRawReadSamples{0};
static std::atomic<uint32_t> sRawSlowReadSamples{0};
static std::atomic<uint32_t> sRawNativeOpenSamples{0};
static std::atomic<uint32_t> sRawSaveOpenSamples{0};
static std::atomic<uint32_t> sRawFlushSamples{0};
static std::atomic<uint32_t> sRawSpaceSamples{0};
static std::atomic<uint32_t> sRawWindowReadCalls{0}, sRawWindowReadMs{0};
static RawFrameStats sRawFrames;
static uint32_t sRawFrameReports = 0;
static uint32_t sRawMaxSwapUs = 0;
static std::atomic_flag sRawFrameGuard = ATOMIC_FLAG_INIT;

DECL_FUNCTION(void, Raw_GX2SwapScanBuffers) {
    const bool raw = gLooseTitleInfo.active;
    const OSTime started = raw ? OSGetTime() : 0;
    real_Raw_GX2SwapScanBuffers();
    if (!raw || sRawFrameGuard.test_and_set(std::memory_order_acquire)) return;
    if (sRawFrameReports < 24) {
        const OSTime ended = OSGetTime();
        const auto swapUs = static_cast<uint32_t>(OSTicksToMicroseconds(ended - started));
        if (swapUs > sRawMaxSwapUs) sRawMaxSwapUs = swapUs;
        if (sRawFrames.sample(OSTicksToMicroseconds(ended))) {
            OSReport("uFlow raw-port8: frame-window_ms=%u intervals=%u over25ms=%u over50ms=%u max_gap_us=%u max_swap_us=%u sync_reads=%u sync_read_ms=%u\n",
                     static_cast<uint32_t>((sRawFrames.last - sRawFrames.first) / 1000),
                     sRawFrames.intervals, sRawFrames.over25ms, sRawFrames.over50ms,
                     sRawFrames.maxGapUs, sRawMaxSwapUs,
                     sRawWindowReadCalls.exchange(0, std::memory_order_relaxed),
                     sRawWindowReadMs.exchange(0, std::memory_order_relaxed));
            ++sRawFrameReports;
            sRawFrames.nextWindow();
            sRawMaxSwapUs = 0;
        }
    }
    sRawFrameGuard.clear(std::memory_order_release);
}

static void FinishRawRead(void *buffer, uint32_t size, uint32_t count,
                          FSStatus result, OSTime started) {
    const uint32_t bytes = RawReadBytes(static_cast<int32_t>(result), size, count);
    // libmocha's unaligned head/tail reads use memcpy. Publish CPU-written
    // data before a game can submit this buffer to the GPU/DSP. No invalidation:
    // it could discard precisely the dirty bytes we need to preserve.
    if (buffer && bytes) DCFlushRange(buffer, bytes);
    const auto elapsed = static_cast<uint32_t>(OSTicksToMilliseconds(OSGetTime() - started));
    sRawWindowReadCalls.fetch_add(1, std::memory_order_relaxed);
    sRawWindowReadMs.fetch_add(elapsed, std::memory_order_relaxed);
    const uint32_t sample = sRawReadSamples.fetch_add(1, std::memory_order_relaxed);
    const bool slow = elapsed >= 100;
    const bool reportSlow = slow && sRawSlowReadSamples.fetch_add(1, std::memory_order_relaxed) < 8;
    if (sample < 4 || reportSlow) {
        OSReport("uFlow raw-port5: sync read result=%d bytes=%u duration_ms=%u\n",
                 result, bytes, elapsed);
    }
}

DECL_FUNCTION(FSStatus, Raw_FSReadFile, FSClient *client, FSCmdBlock *block,
              uint8_t *buffer, uint32_t size, uint32_t count, FSFileHandle handle,
              uint32_t flags, FSErrorFlag errorMask) {
    const bool raw = gLooseTitleInfo.active;
    const OSTime started = raw ? OSGetTime() : 0;
    const auto result = real_Raw_FSReadFile(client, block, buffer, size, count, handle, flags, errorMask);
    if (raw) FinishRawRead(buffer, size, count, result, started);
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSReadFileWithPos, FSClient *client, FSCmdBlock *block,
              uint8_t *buffer, uint32_t size, uint32_t count, FSAFilePosition pos,
              FSFileHandle handle, uint32_t flags, FSErrorFlag errorMask) {
    const bool raw = gLooseTitleInfo.active;
    const OSTime started = raw ? OSGetTime() : 0;
    const auto result = real_Raw_FSReadFileWithPos(client, block, buffer, size, count, pos, handle, flags, errorMask);
    if (raw) FinishRawRead(buffer, size, count, result, started);
    return result;
}

// The raw-game trace reached FS_VOLSTATE_FATAL with a ContentRedirection
// virtual handle (0x3.......), submitted to native STAT_FILE. Route the sync
// entry through the exported async entry so the handle stays with its owner.
// Keep native behavior outside a raw game and preserve async completion/errors.
DECL_FUNCTION(FSStatus, Raw_FSGetStatFile, FSClient *client, FSCmdBlock *block,
              FSFileHandle handle, FSStat *stat, FSErrorFlag errorMask) {
    if (!gLooseTitleInfo.active) {
        return real_Raw_FSGetStatFile(client, block, handle, stat, errorMask);
    }
    OSMessageQueue queue;
    OSMessage slot;
    OSInitMessageQueue(&queue, &slot, 1);
    FSAsyncData async {};
    async.ioMsgQueue = &queue;
#ifdef UFLOW_RAW_TRACE
    OSReport("uFlow raw: stat begin handle=%08X\n", (uint32_t) handle);
#endif
    auto result = FSGetStatFileAsync(client, block, handle, stat, errorMask, &async);
    if (result != FS_STATUS_OK) {
        OSReport("uFlow raw: stat submit=%d\n", result);
        return result;
    }
    OSMessage message;
    OSReceiveMessage(&queue, &message, OS_MESSAGE_FLAGS_BLOCKING);
    auto *completed = FSGetAsyncResult(reinterpret_cast<FSMessage *>(&message));
    result = completed->status;
#ifdef UFLOW_RAW_TRACE
    OSReport("uFlow raw: stat complete handle=%08X result=%d size=%u\n",
             (uint32_t) handle, result, result == FS_STATUS_OK ? stat->size : 0);
#else
    if (result < FS_STATUS_OK) {
        OSReport("uFlow raw: stat failed handle=%08X result=%d\n", (uint32_t) handle, result);
    }
#endif
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSOpenFile, FSClient *client, FSCmdBlock *block,
              const char *path, const char *mode, FSFileHandle *handle, FSErrorFlag errorMask) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool native = RawNativeOpenPath(nativePath, RawNativePathStorage::Capacity, gLooseTitleInfo.active,
                                          path, mode, gLooseTitleInfo.contentPath, gLooseTitleInfo.codePath,
                                          gLooseTitleInfo.savePath);
    auto result = real_Raw_FSOpenFile(client, block, native ? nativePath : path, mode, handle, errorMask);
    if (native && std::strncmp(path, "/vol/save/", 10) == 0 &&
        sRawSaveOpenSamples.fetch_add(1, std::memory_order_relaxed) < 16) {
        OSReport("uFlow raw-port10: native save open result=%d handle=%08X mode=%s path=%s\n",
                 result, result == FS_STATUS_OK && handle ? (uint32_t)*handle : 0, mode, nativePath);
    }
    if (native && sRawNativeOpenSamples.fetch_add(1, std::memory_order_relaxed) < 4) {
        OSReport("uFlow raw-port7: native open result=%d handle=%08X path=%s\n",
                 result, result == FS_STATUS_OK && handle ? (uint32_t)*handle : 0, nativePath);
    }
#ifdef UFLOW_RAW_TRACE
    if (gLooseTitleInfo.active) {
        OSReport("uFlow raw: open path=%s result=%d handle=%08X\n",
                 path ? path : "<null>", result,
                 result == FS_STATUS_OK && handle ? (uint32_t) *handle : 0);
    }
#endif
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSOpenFileAsync, FSClient *client, FSCmdBlock *block,
              const char *path, const char *mode, FSFileHandle *handle,
              FSErrorFlag errorMask, FSAsyncData *asyncData) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool native = RawNativeOpenPath(nativePath, RawNativePathStorage::Capacity, gLooseTitleInfo.active,
                                          path, mode, gLooseTitleInfo.contentPath, gLooseTitleInfo.codePath,
                                          gLooseTitleInfo.savePath);
    // Native FS copies the path into the command block during submission;
    // the caller's async queue/callback and completion status stay unchanged.
    return real_Raw_FSOpenFileAsync(client, block, native ? nativePath : path,
                                    mode, handle, errorMask, asyncData);
}

// Games may use either standard or extended opens, synchronously or
// asynchronously. Cover every entry so no save open returns a virtual handle.
DECL_FUNCTION(FSStatus, Raw_FSOpenFileEx, FSClient *client, FSCmdBlock *block,
              const char *path, const char *mode, FSMode createMode,
              FSOpenFileFlags openFlag, uint32_t preallocSize,
              FSFileHandle *handle, FSErrorFlag errorMask) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool native = RawNativeOpenPath(nativePath, RawNativePathStorage::Capacity, gLooseTitleInfo.active,
                                          path, mode, gLooseTitleInfo.contentPath, gLooseTitleInfo.codePath,
                                          gLooseTitleInfo.savePath);
    const auto result = real_Raw_FSOpenFileEx(client, block, native ? nativePath : path, mode,
                                              createMode, openFlag, preallocSize, handle, errorMask);
    if (native && std::strncmp(path, "/vol/save/", 10) == 0 &&
        sRawSaveOpenSamples.fetch_add(1, std::memory_order_relaxed) < 16) {
        OSReport("uFlow raw-port10: native save open-ex result=%d handle=%08X mode=%s path=%s\n",
                 result, result == FS_STATUS_OK && handle ? (uint32_t)*handle : 0, mode, nativePath);
    }
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSOpenFileExAsync, FSClient *client, FSCmdBlock *block,
              const char *path, const char *mode, FSMode createMode,
              FSOpenFileFlags openFlag, uint32_t preallocSize,
              FSFileHandle *handle, FSErrorFlag errorMask, FSAsyncData *asyncData) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool native = RawNativeOpenPath(nativePath, RawNativePathStorage::Capacity, gLooseTitleInfo.active,
                                          path, mode, gLooseTitleInfo.contentPath, gLooseTitleInfo.codePath,
                                          gLooseTitleInfo.savePath);
    // Do not replace or wait on the caller's callback/queue. In particular,
    // never inspect *handle until the caller receives the async completion.
    const auto result = real_Raw_FSOpenFileExAsync(client, block, native ? nativePath : path, mode,
                                                   createMode, openFlag, preallocSize, handle,
                                                   errorMask, asyncData);
    if (native && std::strncmp(path, "/vol/save/", 10) == 0 &&
        sRawSaveOpenSamples.fetch_add(1, std::memory_order_relaxed) < 16) {
        OSReport("uFlow raw-port10: native save open-ex async submit=%d mode=%s path=%s\n",
                 result, mode, nativePath);
    }
    return result;
}

static void PrepareCurrentTitleRelaunch() {
    if (gLooseTitleInfo.active) {
        gLooseTitleInfo.active  = false;
        gLooseTitleInfo.pending = true;
    }
    RL_PrepareLaunchFromSD(gReplacementInfo.lastFileLoaded);
    OSMemoryBarrier();
}

// Save capacity must describe the same SD destination as file opens. The
// wrapper title's native save mount has a different quota; ContentRedirection
// does not redirect free-space queries. Preserve real results, including zero
// free bytes and all errors, rather than manufacturing available space.
DECL_FUNCTION(FSStatus, Raw_FSGetFreeSpaceSize, FSClient *client, FSCmdBlock *block,
              const char *path, uint64_t *outSize, FSErrorFlag errorMask) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool redirected = RawNativeSavePath(nativePath, RawNativePathStorage::Capacity,
                                              gLooseTitleInfo.active, path, gLooseTitleInfo.savePath);
    const auto result = real_Raw_FSGetFreeSpaceSize(client, block, redirected ? nativePath : path,
                                                    outSize, errorMask);
    if (redirected && sRawSpaceSamples.fetch_add(1, std::memory_order_relaxed) < 16) {
        const uint64_t bytes = result == FS_STATUS_OK && outSize ? *outSize : 0;
        OSReport("uFlow raw-port15: save space result=%d bytes=%08X%08X path=%s\n",
                 result, static_cast<uint32_t>(bytes >> 32), static_cast<uint32_t>(bytes), nativePath);
    }
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSGetFreeSpaceSizeAsync, FSClient *client, FSCmdBlock *block,
              const char *path, uint64_t *outSize, FSErrorFlag errorMask, FSAsyncData *asyncData) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool redirected = RawNativeSavePath(nativePath, RawNativePathStorage::Capacity,
                                              gLooseTitleInfo.active, path, gLooseTitleInfo.savePath);
    // Submission copies the path. Keep the caller's output, callback and queue;
    // only its eventual completion may read outSize.
    const auto result = real_Raw_FSGetFreeSpaceSizeAsync(client, block, redirected ? nativePath : path,
                                                         outSize, errorMask, asyncData);
    if (redirected && sRawSpaceSamples.fetch_add(1, std::memory_order_relaxed) < 16)
        OSReport("uFlow raw-port15: async save space submit=%d path=%s\n", result, nativePath);
    return result;
}

// ContentRedirection handles save file I/O but does not redirect quota flushes.
// Like Loadiine, submit a REAL flush against the SD save directory. Never turn
// an error into success: preserve the native status and async completion path.
DECL_FUNCTION(FSStatus, Raw_FSFlushQuota, FSClient *client, FSCmdBlock *block,
              const char *path, FSErrorFlag errorMask) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool redirected = RawNativeSavePath(nativePath, RawNativePathStorage::Capacity,
                                              gLooseTitleInfo.active, path, gLooseTitleInfo.savePath);
    const auto result = real_Raw_FSFlushQuota(client, block, redirected ? nativePath : path, errorMask);
    if (redirected && sRawFlushSamples.fetch_add(1, std::memory_order_relaxed) < 16)
        OSReport("uFlow raw-port9: save flush result=%d path=%s\n", result, nativePath);
    return result;
}

DECL_FUNCTION(FSStatus, Raw_FSFlushQuotaAsync, FSClient *client, FSCmdBlock *block,
              const char *path, FSErrorFlag errorMask, FSAsyncData *asyncData) {
    RawNativePathStorage storage(gLooseTitleInfo.active);
    char *nativePath = storage.get();
    // Allocation failure must not fall back to the wrapper title's save.
    if (gLooseTitleInfo.active && !nativePath) return FS_STATUS_FATAL_ERROR;
    const bool redirected = RawNativeSavePath(nativePath, RawNativePathStorage::Capacity,
                                              gLooseTitleInfo.active, path, gLooseTitleInfo.savePath);
    const auto result = real_Raw_FSFlushQuotaAsync(client, block, redirected ? nativePath : path,
                                                  errorMask, asyncData);
    if (redirected && sRawFlushSamples.fetch_add(1, std::memory_order_relaxed) < 16)
        OSReport("uFlow raw-port9: async save flush submit=%d path=%s\n", result, nativePath);
    return result;
}

DECL_FUNCTION(void, OSRestartGame, int argc, char *argv[]) {
    if (gLooseTitleInfo.active ||
        (OSGetTitleID() == _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY) &&
         strlen(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) == 0)) {
        PrepareCurrentTitleRelaunch();
    }
    real_OSRestartGame(argc, argv);
}

DECL_FUNCTION(void, _SYSLaunchTitleWithStdArgsInNoSplash, uint64_t titleId, void *u1) {
    if (gLooseTitleInfo.active ||
        (titleId == _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY) &&
         titleId == OSGetTitleID() &&
         strlen(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) == 0)) {
        // RetroArch prepares its own core before relaunching the wrapper with
        // native arguments; re-preparing the previous RPX would undo that.
        if (!RetroArchLaunch::AdoptCoreSwitch(u1)) {
            PrepareCurrentTitleRelaunch();
        }
    }
    real__SYSLaunchTitleWithStdArgsInNoSplash(titleId, u1);
}

function_replacement_data_t rpx_utils_function_replacements[] = {
        REPLACE_FUNCTION_FOR_PROCESS(UFlow_ProcUIProcessMessages, LIBRARY_PROC_UI, ProcUIProcessMessages, FP_TARGET_PROCESS_WII_U_MENU),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_TraceProcUIProcessMessages, LIBRARY_PROC_UI, ProcUIProcessMessages, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_TraceProcUIShutdown, LIBRARY_PROC_UI, ProcUIShutdown, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_TraceExit, LIBRARY_COREINIT, _Exit, FP_TARGET_PROCESS_GAME),
        // Raw-title handles, saves and frame instrumentation belong only to
        // the game process. Do not interpose on Root/Menu loader/bootstrap I/O.
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSGetFreeSpaceSize, LIBRARY_COREINIT, FSGetFreeSpaceSize, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSGetFreeSpaceSizeAsync, LIBRARY_COREINIT, FSGetFreeSpaceSizeAsync, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSFlushQuota, LIBRARY_COREINIT, FSFlushQuota, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSFlushQuotaAsync, LIBRARY_COREINIT, FSFlushQuotaAsync, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_GX2SwapScanBuffers, LIBRARY_GX2, GX2SwapScanBuffers, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSReadFile, LIBRARY_COREINIT, FSReadFile, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSReadFileWithPos, LIBRARY_COREINIT, FSReadFileWithPos, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSGetStatFile, LIBRARY_COREINIT, FSGetStatFile, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSOpenFile, LIBRARY_COREINIT, FSOpenFile, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSOpenFileAsync, LIBRARY_COREINIT, FSOpenFileAsync, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSOpenFileEx, LIBRARY_COREINIT, FSOpenFileEx, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_FOR_PROCESS(Raw_FSOpenFileExAsync, LIBRARY_COREINIT, FSOpenFileExAsync, FP_TARGET_PROCESS_GAME),
        REPLACE_FUNCTION_VIA_ADDRESS_FOR_PROCESS(HBM_NN_ACP_ACPGetTitleMetaXmlByDevice, 0x2E36CE44, 0x0E36CE44, FP_TARGET_PROCESS_HOME_MENU),
        REPLACE_FUNCTION_FOR_PROCESS(RPX_FSOpenFile, LIBRARY_COREINIT, FSOpenFile, FP_TARGET_PROCESS_HOME_MENU),
        REPLACE_FUNCTION_FOR_PROCESS(RPX_FSReadFile, LIBRARY_COREINIT, FSReadFile, FP_TARGET_PROCESS_HOME_MENU),
        REPLACE_FUNCTION_FOR_PROCESS(RPX_FSCloseFile, LIBRARY_COREINIT, FSCloseFile, FP_TARGET_PROCESS_HOME_MENU),
        REPLACE_FUNCTION(OSRestartGame, LIBRARY_COREINIT, OSRestartGame),
        REPLACE_FUNCTION(_SYSLaunchTitleWithStdArgsInNoSplash, LIBRARY_SYSAPP, _SYSLaunchTitleWithStdArgsInNoSplash),
};

uint32_t rpx_utils_function_replacements_size = sizeof(rpx_utils_function_replacements) / sizeof(function_replacement_data_t);

static int parseINIhandler(void *user, const char *section, const char *name,
                           const char *value) {
    auto *fInfo = (MetaInformation *) user;
#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("menu", "longname")) {
        fInfo->longname[0] = '\0';
        strncat(fInfo->longname, value, sizeof(fInfo->longname) - 1);
    } else if (MATCH("menu", "shortname")) {
        fInfo->shortname[0] = '\0';
        strncat(fInfo->shortname, value, sizeof(fInfo->shortname) - 1);
    } else if (MATCH("menu", "author")) {
        fInfo->author[0] = '\0';
        strncat(fInfo->author, value, sizeof(fInfo->author) - 1);
    } else {
        return 0; /* unknown section/name, error */
    }

    return 1;
}

RPXLoaderStatus RL_PrepareLaunchFromSD(const char *bundle_path) {
    MochaRPXLoadInfo request;
    memset(&request, 0, sizeof(request));

    const bool usb=RawSourceUSB(bundle_path);
    request.target     = usb?static_cast<LoadRPXTargetEnum>(UFLOW_USB_TARGET):LOAD_RPX_TARGET_SD_CARD;
    request.filesize   = 0;                       // unknown filesize
    request.fileoffset = 0;                       //

    WUHBRPXInfo fileInfo;

    bool metaLoaded = false;

    std::string completePath       = std::string("/vol/external01/") + bundle_path;
    std::string completeNewLibPath = RawSourcePath(bundle_path);

    struct stat st {};
    if (stat(completeNewLibPath.c_str(), &st) < 0) {
        DEBUG_FUNCTION_LINE_ERR("Failed to prepare launch from SD, file (%s) not found.", bundle_path);
        return RPX_LOADER_RESULT_NOT_FOUND;
    }

    bool isBundle = false;
    auto rpxInfo  = usb?WUHB_UTILS_RESULT_UNKNOWN_ERROR:WUHBUtils_GetRPXInfo(completePath.c_str(), BundleSource_FileDescriptor_CafeOS, &fileInfo);
    if (rpxInfo == WUHB_UTILS_RESULT_SUCCESS) {
        isBundle           = true;
        request.filesize   = ((uint32_t *) &fileInfo.length)[1];
        request.fileoffset = ((uint32_t *) &fileInfo.offset)[1];
        auto res           = -1;

        if (WUHBUtils_MountBundle("rcc", completePath.c_str(), BundleSource_FileDescriptor_CafeOS, &res) == WUHB_UTILS_RESULT_SUCCESS && res == 0) {
            uint8_t *buffer;
            uint32_t bufferSize;
            if (WUHBUtils_ReadWholeFile("rcc:/meta/meta.ini", &buffer, &bufferSize) == WUHB_UTILS_RESULT_SUCCESS) {
                buffer[bufferSize - 1] = '\0';
                if (ini_parse_string((const char *) buffer, parseINIhandler, &gReplacementInfo.rpxReplacementInfo.metaInformation) < 0) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to load and parse meta.ini");
                } else {
                    metaLoaded = true;
                }
                free(buffer);
            } else {
                DEBUG_FUNCTION_LINE_ERR("Failed to read whole file meta.ini");
            }
            buffer     = nullptr;
            bufferSize = 0;

            if (WUHBUtils_ReadWholeFile("rcc:/meta/iconTex.tga", &buffer, &bufferSize) == WUHB_UTILS_RESULT_SUCCESS) {
                uint32_t cpySize = ICON_SIZE;
                if (bufferSize < cpySize) {
                    cpySize = bufferSize;
                    memset(gReplacementInfo.rpxReplacementInfo.iconCache, 0, ICON_SIZE);
                }
                memcpy(gReplacementInfo.rpxReplacementInfo.iconCache, buffer, cpySize);
                free(buffer);
            } else {
                memcpy(gReplacementInfo.rpxReplacementInfo.iconCache, defaultIconTexTGA, ICON_SIZE);
                DEBUG_FUNCTION_LINE_ERR("Failed to read iconTex.tga");
            }

            auto outRes = 0;
            if (WUHBUtils_UnmountBundle("rcc", &outRes) != WUHB_UTILS_RESULT_SUCCESS || outRes != WUHB_UTILS_RESULT_SUCCESS) {
                DEBUG_FUNCTION_LINE_ERR("Failed to unmount bundle");
            }
        }
    } else {
        if (!gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted) {
            memcpy(gReplacementInfo.rpxReplacementInfo.iconCache, defaultIconTexTGA, ICON_SIZE);
        }
    }

    if (!metaLoaded) {
        gReplacementInfo.rpxReplacementInfo.metaInformation.author[0]    = '\0';
        gReplacementInfo.rpxReplacementInfo.metaInformation.shortname[0] = '\0';
        gReplacementInfo.rpxReplacementInfo.metaInformation.longname[0]  = '\0';

        strncat(gReplacementInfo.rpxReplacementInfo.metaInformation.author, bundle_path, sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.author) - 1);
        strncat(gReplacementInfo.rpxReplacementInfo.metaInformation.shortname, bundle_path, sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.shortname) - 1);
        strncat(gReplacementInfo.rpxReplacementInfo.metaInformation.longname, bundle_path, sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.longname) - 1);
    }

    request.path[0] = '\0';
    strncat(request.path, usb?bundle_path+6:bundle_path, sizeof(request.path) - 1);

    OSMemoryBarrier();
    bool success = false;
    MochaUtilsStatus res;
    if ((res = Mocha_PrepareRPXLaunch(&request)) == MOCHA_RESULT_SUCCESS) {
        success = true;
    } else {
        DEBUG_FUNCTION_LINE_ERR("Failed to prepare rpx launch: %s", Mocha_GetStatusStr(res));
    }

    if (!success) {
        request.target = LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE;
        Mocha_PrepareRPXLaunch(&request);

        gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = false;
        DEBUG_FUNCTION_LINE_ERR("Failed to load %s on next restart", request.path);
        return RPX_LOADER_RESULT_UNKNOWN_ERROR;
    } else {
        gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = true;
    }

    DEBUG_FUNCTION_LINE("Launch %s on next restart [size: %08X offset: %08X]", request.path, request.filesize, request.fileoffset);

    gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath[0] = '\0';
    if (isBundle) {
        DEBUG_FUNCTION_LINE_VERBOSE("Loaded file is a .wuhb bundle");
        strncat(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath,
                completePath.c_str(),
                sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) - 1);
    } else {
        DEBUG_FUNCTION_LINE_VERBOSE("Loaded file is no bundle");
        gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = true;

        if (gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted) {
            // keep the old /vol/content mounted, this way you can reload just the rpx via wiiload
            strncat(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath,
                    gReplacementInfo.contentReplacementInfo.bundleMountInformation.mountedPath,
                    sizeof(gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath) - 2);
        }
    }

    strncpy(gReplacementInfo.lastFileLoaded, bundle_path, sizeof(gReplacementInfo.lastFileLoaded) - 2);

    OSMemoryBarrier();

    return RPX_LOADER_RESULT_SUCCESS;
}

static bool IsSafeRelativePath(const char *path) {
    if(RawSourceUSB(path))return RawUsbSafePath(path+6);
    if (path == nullptr || path[0] == '\0' || path[0] == '/' || path[0] == '\\' || strchr(path, ':') != nullptr) {
        return false;
    }
    std::string_view value(path);
    return value != ".." && !value.starts_with("../") && !value.ends_with("/..") && value.find("/../") == std::string_view::npos &&
           !value.starts_with("..\\") && !value.ends_with("\\..") && value.find("\\..\\") == std::string_view::npos;
}

static bool PathHasExpectedType(const char *relativePath, bool directory) {
    std::string fullPath = RawSourcePath(relativePath);
    struct stat info {};
    if (stat(fullPath.c_str(), &info) != 0) {
        return false;
    }
    return directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
}

static std::string ReadTextFile(const std::string &path, size_t maximum = 256 * 1024) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    std::string result(maximum, '\0');
    input.read(result.data(), static_cast<std::streamsize>(result.size()));
    result.resize(static_cast<size_t>(input.gcount()));
    return result;
}

static std::string XmlValue(const std::string &xml, const std::string &tag) {
    const std::string opening = "<" + tag;
    const std::string closing = "</" + tag + ">";
    const size_t begin = xml.find(opening);
    if (begin == std::string::npos) {
        return {};
    }
    const size_t valueBegin = xml.find('>', begin + opening.size());
    const size_t end = valueBegin == std::string::npos ? std::string::npos : xml.find(closing, valueBegin + 1);
    if (end == std::string::npos) {
        return {};
    }
    return xml.substr(valueBegin + 1, end - valueBegin - 1);
}

static uint32_t XmlU32(const std::string &xml, const char *tag, int base, uint32_t fallback) {
    const std::string value = XmlValue(xml, tag);
    if (value.empty()) {
        return fallback;
    }
    char *end = nullptr;
    const unsigned long parsed = strtoul(value.c_str(), &end, base);
    return end == value.c_str() ? fallback : static_cast<uint32_t>(parsed);
}

static uint64_t XmlU64(const std::string &xml, const char *tag, int base, uint64_t fallback) {
    const std::string value = XmlValue(xml, tag);
    if (value.empty()) {
        return fallback;
    }
    char *end = nullptr;
    const unsigned long long parsed = strtoull(value.c_str(), &end, base);
    return end == value.c_str() ? fallback : static_cast<uint64_t>(parsed);
}

static bool SendRawTitleProfile(const char *codePath, const char *rpxPath, bool enabled) {
    RawTitleProfile profile {};
    profile.magic          = RAW_TITLE_PROFILE_MAGIC;
    profile.profileVersion = RAW_TITLE_PROFILE_VERSION;
    profile.enabled        = enabled ? 1 : 0;

    if (enabled) {
        const std::string absoluteCode = RawSourcePath(codePath);
        const std::string appXml       = ReadTextFile(absoluteCode + "/app.xml");
        const std::string cosXml       = ReadTextFile(absoluteCode + "/cos.xml");
        if (appXml.empty() || cosXml.empty()) {
            DEBUG_FUNCTION_LINE_ERR("Raw title is missing code/app.xml or code/cos.xml");
            return false;
        }

        profile.cosVersion   = XmlU32(cosXml, "version", 10, 18);
        profile.osVersion    = XmlU64(appXml, "os_version", 16, 0x000500101000400AULL);
        profile.titleId      = XmlU64(appXml, "title_id", 16, 0);
        profile.titleVersion = XmlU32(appXml, "title_version", 16, 0);
        profile.sdkVersion   = XmlU32(appXml, "sdk_version", 10, 20909);
        profile.appType      = XmlU32(appXml, "app_type", 16, 0x80000000);
        profile.groupId      = XmlU32(appXml, "group_id", 16, 0);
        profile.cmdFlags     = XmlU32(cosXml, "cmdFlags", 10, 0);
        profile.maxSize      = XmlU32(cosXml, "max_size", 16, 0x40000000);
        profile.availSize    = XmlU32(cosXml, "avail_size", 16, 0);
        profile.codegenSize  = XmlU32(cosXml, "codegen_size", 16, 0);
        profile.codegenCore  = XmlU32(cosXml, "codegen_core", 16, 1);
        profile.maxCodeSize  = XmlU32(cosXml, "max_codesize", 16, 0x03000000);
        profile.overlayArena = XmlU32(cosXml, "overlay_arena", 16, 0);
        profile.numWorkareaHeapBlocks = XmlU32(cosXml, "num_workarea_heap_blocks", 16, 0);
        profile.numCodeareaHeapBlocks = XmlU32(cosXml, "num_codearea_heap_blocks", 16, 0);
        profile.defaultStack0Size = XmlU32(cosXml, "default_stack0_size", 16, 0);
        profile.defaultStack1Size = XmlU32(cosXml, "default_stack1_size", 16, 0);
        profile.defaultStack2Size = XmlU32(cosXml, "default_stack2_size", 16, 0);
        profile.defaultRedzone0Size = XmlU32(cosXml, "default_redzone0_size", 16, 0);
        profile.defaultRedzone1Size = XmlU32(cosXml, "default_redzone1_size", 16, 0);
        profile.defaultRedzone2Size = XmlU32(cosXml, "default_redzone2_size", 16, 0);
        profile.exceptionStack0Size = XmlU32(cosXml, "exception_stack0_size", 16, 0x1000);
        profile.exceptionStack1Size = XmlU32(cosXml, "exception_stack1_size", 16, 0x1000);
        profile.exceptionStack2Size = XmlU32(cosXml, "exception_stack2_size", 16, 0x1000);

        std::string arguments = XmlValue(cosXml, "argstr");
        if (arguments.empty()) {
            arguments = rpxPath;
            const size_t slash = arguments.find_last_of("/\\");
            if (slash != std::string::npos) {
                arguments.erase(0, slash + 1);
            }
        }
        snprintf(profile.argstr, sizeof(profile.argstr), "%s", arguments.c_str());

        for (uint32_t i = 0; i < 19; ++i) {
            const std::string permissionXml = XmlValue(cosXml, "p" + std::to_string(i));
            profile.permissions[i].group = XmlU32(permissionXml, "group", 10, 0);
            profile.permissions[i].mask  = XmlU64(permissionXml, "mask", 16, 0);
        }

        if (profile.titleId == 0 || profile.argstr[0] == '\0') {
            DEBUG_FUNCTION_LINE_ERR("Raw title launch profile is incomplete");
            return false;
        }
    }

    struct alignas(0x40) RawProfileRequest {
        uint32_t command;
        RawTitleProfile profile;
    } request {};
    alignas(0x40) uint32_t response[0x10] {};
    request.command = IPC_CUSTOM_SET_RAW_TITLE_PROFILE;
    request.profile = profile;

    const int mcp = IOS_Open("/dev/mcp", static_cast<IOSOpenMode>(0));
    if (mcp < 0) {
        return false;
    }
    const int result = IOS_Ioctl(mcp, 100, &request, sizeof(request), response, sizeof(uint32_t));
    IOS_Close(mcp);
    OSReport("uFlow raw profile: enabled=%d title=%08X%08X ipc=%d response=%08X\n", enabled,
             static_cast<uint32_t>(profile.titleId >> 32), static_cast<uint32_t>(profile.titleId), result, response[0]);
    // Clearing the profile also runs during late process teardown. Keep SD
    // diagnostics only for preparation, never open a new log while shutting down.
    if (enabled) if (FILE *log = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a")) {
        fprintf(log,"profile enabled=%d title=%08X%08X ipc=%d response=%08X\n", enabled,
                static_cast<uint32_t>(profile.titleId >> 32), static_cast<uint32_t>(profile.titleId), result, response[0]);
        fclose(log);
    }
    bool success = result == IOS_ERROR_OK && response[0] == RAW_TITLE_PROFILE_MAGIC;
    if (success) {
        if (enabled) {
            success = RawTitleKernel_Arm(profile);
            if (!success) {
                DEBUG_FUNCTION_LINE_ERR("%s", RawTitleKernel_GetInitializeError());
                if (FILE *log = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a")) {
                    fprintf(log,"kernel arm failed: %s\n",RawTitleKernel_GetInitializeError());
                    fclose(log);
                }
                SendRawTitleProfile(nullptr, nullptr, false);
            }
        } else {
            RawTitleKernel_Clear();
        }
    }
    return success;
}

bool RL_SupportsWiiVCHai() {
    return true;
}

void RL_ClearRawTitleProfile() {
    SendRawTitleProfile(nullptr, nullptr, false);
    RawTitleKernel_Clear();
    RawHai::Clear();
}

RPXLoaderStatus RL_PrepareLaunchFromLooseDirectory(const char *rpxPath,
                                                   const char *contentPath,
                                                   const char *codePath,
                                                   const char *savePath,
                                                   const char *shortName,
                                                   const char *longName,
                                                   const char *publisher) {
    sRawReadSamples.store(0, std::memory_order_relaxed);
    sRawNativeOpenSamples.store(0, std::memory_order_relaxed);
    sRawFlushSamples.store(0, std::memory_order_relaxed);
    sRawSlowReadSamples.store(0, std::memory_order_relaxed);
    sRawWindowReadCalls.store(0, std::memory_order_relaxed);
    sRawWindowReadMs.store(0, std::memory_order_relaxed);
    sRawFrames = {};
    sRawFrameReports = sRawMaxSwapUs = 0;
    if (!IsSafeRelativePath(rpxPath) || !IsSafeRelativePath(contentPath) ||
        !IsSafeRelativePath(codePath) || !IsSafeRelativePath(savePath) ||
        strlen(rpxPath)>=256 ||
        RawSourceUSB(rpxPath)!=RawSourceUSB(contentPath) || RawSourceUSB(rpxPath)!=RawSourceUSB(codePath) || RawSourceUSB(savePath) ||
        strlen(contentPath) >= sizeof(gLooseTitleInfo.contentPath) ||
        strlen(codePath) >= sizeof(gLooseTitleInfo.codePath) ||
        strlen(savePath) >= sizeof(gLooseTitleInfo.savePath)) {
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    if(RawSourceUSB(rpxPath)&&!USBStorage::Mount())return RPX_LOADER_RESULT_NOT_FOUND;
    if (!PathHasExpectedType(rpxPath, false) || !PathHasExpectedType(contentPath, true) ||
        !PathHasExpectedType(codePath, true)) {
        return RPX_LOADER_RESULT_NOT_FOUND;
    }

    if (!SendRawTitleProfile(codePath, rpxPath, true)) {
        DEBUG_FUNCTION_LINE_ERR("Custom Mocha raw-title support is unavailable or the XML profile is invalid");
        return RPX_LOADER_RESULT_UNSUPPORTED_COMMAND;
    }

    if(RawSourceUSB(rpxPath)&&!USBStorage::PrepareCode(codePath)) {
        RL_ClearRawTitleProfile();USBStorage::Unmount();
        return RPX_LOADER_RESULT_UNSUPPORTED_COMMAND;
    }
    {
        const uint64_t titleId = XmlU64(ReadTextFile(RawSourcePath(codePath) + "/app.xml"), "title_id", 16, 0);
        if (RawHai::Prepare(codePath, contentPath, titleId) == RawHai::Result::Failed) {
            RL_ClearRawTitleProfile();
            if (RawSourceUSB(rpxPath)) USBStorage::Unmount();
            return RPX_LOADER_RESULT_UNSUPPORTED_COMMAND;
        }
    }

    auto result = RL_PrepareLaunchFromSD(rpxPath);
    if (result != RPX_LOADER_RESULT_SUCCESS) {
        SendRawTitleProfile(codePath, rpxPath, false);
        RawTitleKernel_Clear();
        RawHai::Clear();
        return result;
    }

    gLooseTitleInfo = {};
    strncpy(gLooseTitleInfo.contentPath, contentPath, sizeof(gLooseTitleInfo.contentPath) - 1);
    strncpy(gLooseTitleInfo.codePath, codePath, sizeof(gLooseTitleInfo.codePath) - 1);
    strncpy(gLooseTitleInfo.savePath, savePath, sizeof(gLooseTitleInfo.savePath) - 1);
    gLooseTitleInfo.pending = true;

    snprintf(gReplacementInfo.rpxReplacementInfo.metaInformation.shortname,
             sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.shortname), "%s",
             shortName != nullptr && shortName[0] != '\0' ? shortName : rpxPath);
    snprintf(gReplacementInfo.rpxReplacementInfo.metaInformation.longname,
             sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.longname), "%s",
             longName != nullptr && longName[0] != '\0' ? longName : gReplacementInfo.rpxReplacementInfo.metaInformation.shortname);
    snprintf(gReplacementInfo.rpxReplacementInfo.metaInformation.author,
             sizeof(gReplacementInfo.rpxReplacementInfo.metaInformation.author), "%s",
             publisher != nullptr ? publisher : "");

    gReplacementInfo.contentReplacementInfo.bundleMountInformation.toMountPath[0] = '\0';
    OSMemoryBarrier();
    return RPX_LOADER_RESULT_SUCCESS;
}

RPXLoaderStatus RL_LaunchPreparedHomebrew() {
    // Request to launch the homebrew wrapper (H&S or Daily Log)
    auto res = Mocha_LaunchHomebrewWrapper();
    if (res != MOCHA_RESULT_SUCCESS) {
        DEBUG_FUNCTION_LINE_ERR("Launch failed: %s", Mocha_GetStatusStr(res));
    }
    if (res == MOCHA_RESULT_NOT_FOUND) {
        return RPX_LOADER_RESULT_NOT_FOUND;
    } else if (res != MOCHA_RESULT_SUCCESS) {
        return RPX_LOADER_RESULT_UNKNOWN_ERROR;
    }
    return RPX_LOADER_RESULT_SUCCESS;
}

RPXLoaderStatus RL_LaunchHomebrew(const char *bundle_path) {
    RPXLoaderStatus res = RPX_LOADER_RESULT_SUCCESS;
    // Tell iosu which .rpx to launch and prepare the content redirection.
    if ((res = RL_PrepareLaunchFromSD(bundle_path)) != RPX_LOADER_RESULT_SUCCESS) {
        return res;
    }
    // Request to launch the homebrew wrapper (H&S or Daily Log)
    if ((res = RL_LaunchPreparedHomebrew()) != RPX_LOADER_RESULT_SUCCESS) {
        // If we fail to launch the correct application, we revert the .rpx redirection.
        MochaRPXLoadInfo loadInfoRevert;
        loadInfoRevert.target = LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE;

        if (MochaUtilsStatus mochaRes = Mocha_PrepareRPXLaunch(&loadInfoRevert); mochaRes != MOCHA_RESULT_SUCCESS) {
            DEBUG_FUNCTION_LINE_WARN("Revert Mocha_PrepareRPXLaunch failed: %s [%d]", Mocha_GetStatusStr(mochaRes), mochaRes);
        }
        return res;
    }

    return res;
}

std::mutex mutex;

RPXLoaderStatus RL_DisableContentRedirection() {
    std::lock_guard<std::mutex> lock(mutex);
    if (contentLayerHandle != 0) {
        if (ContentRedirection_SetActive(contentLayerHandle, false) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (codeLayerHandle != 0) {
        if (ContentRedirection_SetActive(codeLayerHandle, false) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, true);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (metaLayerHandle != 0) {
        if (ContentRedirection_SetActive(metaLayerHandle, false) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, true);
            ContentRedirection_SetActive(codeLayerHandle, true);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (saveLayerHandle != 0) {
        if (ContentRedirection_SetActive(saveLayerHandle, false) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, true);
            ContentRedirection_SetActive(codeLayerHandle, true);
            ContentRedirection_SetActive(metaLayerHandle, true);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    return RPX_LOADER_RESULT_SUCCESS;
}

RPXLoaderStatus RL_LaunchLooseDirectory(const char *rpxPath,
                                        const char *contentPath,
                                        const char *codePath,
                                        const char *savePath,
                                        const char *shortName,
                                        const char *longName,
                                        const char *publisher) {
    auto result = RL_PrepareLaunchFromLooseDirectory(rpxPath, contentPath, codePath, savePath,
                                                     shortName, longName, publisher);
    if (result != RPX_LOADER_RESULT_SUCCESS) {
        return result;
    }

    result = RL_LaunchPreparedHomebrew();
    if (result != RPX_LOADER_RESULT_SUCCESS) {
        MochaRPXLoadInfo revert {};
        revert.target = LOAD_RPX_TARGET_EXTRA_REVERT_PREPARE;
        Mocha_PrepareRPXLaunch(&revert);
        SendRawTitleProfile(codePath, rpxPath, false);
        RawTitleKernel_Clear();
        RawHai::Clear();
        gLooseTitleInfo = {};
        gReplacementInfo.rpxReplacementInfo.willRPXBeReplaced = false;
        gReplacementInfo.lastFileLoaded[0] = '\0';
        OSMemoryBarrier();
    }
    return result;
}

RPXLoaderStatus RL_EnableContentRedirection() {
    std::lock_guard<std::mutex> lock(mutex);
    if (contentLayerHandle != 0) {
        if (ContentRedirection_SetActive(contentLayerHandle, true) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (codeLayerHandle != 0) {
        if (ContentRedirection_SetActive(codeLayerHandle, true) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, false);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (metaLayerHandle != 0) {
        if (ContentRedirection_SetActive(metaLayerHandle, true) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, false);
            ContentRedirection_SetActive(codeLayerHandle, false);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    if (saveLayerHandle != 0) {
        if (ContentRedirection_SetActive(saveLayerHandle, true) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            ContentRedirection_SetActive(contentLayerHandle, false);
            ContentRedirection_SetActive(codeLayerHandle, false);
            ContentRedirection_SetActive(metaLayerHandle, false);
            return RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
    }
    return RPX_LOADER_RESULT_SUCCESS;
}

RPXLoaderStatus RL_UnmountCurrentRunningBundle() {
    RawExitTracePoint("bundle-lock-enter");
    std::lock_guard<std::mutex> lock(mutex);
    RawExitTracePoint("bundle-lock-acquired");
    const bool bundleMounted = gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted;
    const bool looseActive   = gLooseTitleInfo.active;
    const bool hasLayers     = contentLayerHandle != 0 || codeLayerHandle != 0 || metaLayerHandle != 0 || saveLayerHandle != 0;
    if (!bundleMounted && !looseActive && !hasLayers) {
        return RPX_LOADER_RESULT_SUCCESS;
    }

    if (bundleMounted) {
        int outRes = -1;
        if (ContentRedirection_RemoveDevice(WUHB_ROMFS_PATH, &outRes) == CONTENT_REDIRECTION_RESULT_SUCCESS) {
            if (outRes < 0) {
                DEBUG_FUNCTION_LINE_ERR("RemoveDevice \"%s\" failed for ContentRedirection Module", WUHB_ROMFS_NAME);
            }
        } else {
            DEBUG_FUNCTION_LINE_ERR("ContentRedirection_RemoveDevice failed");
        }
    }

    RPXLoaderStatus res = RPX_LOADER_RESULT_SUCCESS;

    if (contentLayerHandle != 0) {
        RawExitTracePoint("content-layer-remove-enter");
        if (ContentRedirection_RemoveFSLayer(contentLayerHandle) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            res = RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
        contentLayerHandle = 0;
    }

    if (codeLayerHandle != 0) {
        if (ContentRedirection_RemoveFSLayer(codeLayerHandle) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            res = RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
        codeLayerHandle = 0;
    }

    if (metaLayerHandle != 0) {
        if (ContentRedirection_RemoveFSLayer(metaLayerHandle) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            res = RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
        metaLayerHandle = 0;
    }

    if (saveLayerHandle != 0) {
        RawExitTracePoint("save-layer-remove-enter");
        if (ContentRedirection_RemoveFSLayer(saveLayerHandle) != CONTENT_REDIRECTION_RESULT_SUCCESS) {
            res = RPX_LOADER_RESULT_UNKNOWN_ERROR;
        }
        saveLayerHandle = 0;
    }
    RawExitTracePoint("fs-layers-removed");

    if (bundleMounted && romfsUnmount(WUHB_ROMFS_NAME) < 0) {
        DEBUG_FUNCTION_LINE_WARN("Failed to unmount romfs \"%s\"", WUHB_ROMFS_NAME);
    }
    gReplacementInfo.contentReplacementInfo.bundleMountInformation.isMounted = false;
    if (looseActive) {
        RawExitTracePoint("raw-profile-clear-enter");
        RL_ClearRawTitleProfile();
        RawExitTracePoint("raw-profile-clear-return");
        gLooseTitleInfo = {};
    }
    gActiveSaveRedirectionPath.clear();
    OSMemoryBarrier();
    return res;
}

RPXLoaderStatus RL_GetVersion(RPXLoaderVersion *outVersion) {
    if (!outVersion) {
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    *outVersion = 3;
    return RPX_LOADER_RESULT_SUCCESS;
}

RPXLoaderStatus RL_GetPathOfRunningExecutable(char *outBuffer, uint32_t outSize) {
    if (outBuffer == nullptr || outSize == 0) {
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    if (strlen(gReplacementInfo.lastFileLoaded) > 0) {
        strncpy(outBuffer, gReplacementInfo.lastFileLoaded, outSize - 1);
        return RPX_LOADER_RESULT_SUCCESS;
    }

    return RPX_LOADER_RESULT_NOT_AVAILABLE;
}

RPXLoaderStatus RL_GetPathOfSaveRedirection(char *outBuffer, uint32_t outSize) {
    if (outBuffer == nullptr || outSize == 0) {
        return RPX_LOADER_RESULT_INVALID_ARGUMENT;
    }
    if (saveLayerHandle != 0 && !gActiveSaveRedirectionPath.empty()) {
        strncpy(outBuffer, gActiveSaveRedirectionPath.c_str(), outSize - 1);
        return RPX_LOADER_RESULT_SUCCESS;
    }

    return RPX_LOADER_RESULT_NOT_AVAILABLE;
}

WUMS_EXPORT_FUNCTION(RL_PrepareLaunchFromSD);
WUMS_EXPORT_FUNCTION(RL_PrepareLaunchFromLooseDirectory);
WUMS_EXPORT_FUNCTION(RL_LaunchLooseDirectory);
WUMS_EXPORT_FUNCTION(RL_SupportsWiiVCHai);
WUMS_EXPORT_FUNCTION(RL_LaunchPreparedHomebrew);
WUMS_EXPORT_FUNCTION(RL_LaunchHomebrew);

WUMS_EXPORT_FUNCTION(RL_GetVersion);
WUMS_EXPORT_FUNCTION(RL_EnableContentRedirection);
WUMS_EXPORT_FUNCTION(RL_DisableContentRedirection);
WUMS_EXPORT_FUNCTION(RL_UnmountCurrentRunningBundle);
WUMS_EXPORT_FUNCTION(RL_GetPathOfRunningExecutable);
WUMS_EXPORT_FUNCTION(RL_GetPathOfSaveRedirection);
