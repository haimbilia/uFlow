#include "RawHai.h"
#include "FileUtils.h"
#include "RawSourcePath.h"
#include "../../app/source/hai_gamepad.hpp"
#include "../../app/source/usb_storage.hpp"
#include "../../payload/source/common/raw_hai.h"
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memorymap.h>
#include <coreinit/title.h>
#include <function_patcher/function_patching.h>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" int32_t MCP_CompatHaiCreateCompanionFile(int32_t handle, const void *buffer, uint32_t size);

namespace {
constexpr const char *kStageRoot    = "fs:/vol/external01/wiiu/apps/uFlow/hai";
constexpr const char *kStageIosPath = UFLOW_HAI_SD_PREFIX "wiiu/apps/uFlow/hai";
// MCP's HAI prepare handler reads fw.tmd and copies the rest from <title>/code.
constexpr const char *kCodeFiles[] = {"fw.tmd", "fw.img", "htk.bin", "rvlt.tik", "rvlt.tmd"};
// Optional per-title override: "000500001019CF00=sideways" (classic|wiimote|sideways).
constexpr const char *kGamePadConfig = "fs:/vol/external01/wiiu/apps/uFlow/vwii_gamepad.cfg";

std::vector<unsigned char> sCompanion;
uint64_t sTitleId = 0;
bool sReady = false;
PatchedFunctionHandle sPatch = 0;
OSDynLoad_Module sHaiUser = nullptr;
// Allocated only while a Wii title is prepared: module memory is shared by every
// Aroma module, and a permanent static buffer broke raw-title exits (Freedom Planet).
constexpr size_t kCopySize = 0x8000;
unsigned char *sCopyBuffer = nullptr;

void Trace(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (FILE *file = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a")) {
        fprintf(file, "%s\n", line);
        fclose(file);
    }
}

bool IsFile(const std::string &path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

uint64_t Fnv(uint64_t hash, const unsigned char *data, size_t size) {
    for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 1099511628211ULL;
    return hash;
}

bool HashFile(const std::string &path, uint64_t &hash, uint64_t &size) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    hash = 14695981039346656037ULL;
    size = 0;
    for (size_t got; (got = fread(sCopyBuffer, 1, kCopySize, file)) != 0; size += got)
        hash = Fnv(hash, sCopyBuffer, got);
    const bool ok = !ferror(file);
    fclose(file);
    return ok;
}

// Byte-identical copy; the SD file is replaced only after the copy verifies.
bool CopyVerified(const std::string &from, const std::string &to) {
    FILE *in = fopen(from.c_str(), "rb");
    if (!in) return false;
    const std::string temp = to + ".tmp";
    remove(temp.c_str());
    FILE *out = fopen(temp.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    uint64_t hash = 14695981039346656037ULL, size = 0;
    bool ok = true;
    for (size_t got; ok && (got = fread(sCopyBuffer, 1, kCopySize, in)) != 0; size += got) {
        hash = Fnv(hash, sCopyBuffer, got);
        ok = fwrite(sCopyBuffer, 1, got, out) == got;
    }
    ok = ok && !ferror(in);
    fclose(in);
    ok = fclose(out) == 0 && ok;
    uint64_t writtenHash = 0, writtenSize = 0;
    ok = ok && size && HashFile(temp, writtenHash, writtenSize) && writtenHash == hash && writtenSize == size;
    if (ok) {
        remove(to.c_str());
        ok = rename(temp.c_str(), to.c_str()) == 0;
    }
    if (!ok) remove(temp.c_str());
    return ok;
}

bool ArmMcp(unsigned device, uint64_t titleId) {
    RawHaiArm arm{};
    arm.magic   = UFLOW_HAI_MAGIC;
    arm.device  = device;
    arm.titleId = titleId;
    strncpy(arm.path, kStageIosPath, sizeof(arm.path) - 1);
    if (!RawHaiArmValid(&arm)) return false;
    alignas(64) unsigned char request[(sizeof(RawHaiArm) + 4 + 63) & ~63u] {};
    alignas(64) uint32_t response[16] {};
    *reinterpret_cast<uint32_t *>(request) = UFLOW_HAI_ARM_COMMAND;
    memcpy(request + 4, &arm, sizeof(arm));
    const int mcp = IOS_Open("/dev/mcp", static_cast<IOSOpenMode>(0));
    if (mcp < 0) return false;
    const auto result = IOS_Ioctl(mcp, 100, request, sizeof(arm) + 4, response, 4);
    IOS_Close(mcp);
    return result == 0 && response[0] == UFLOW_HAI_MAGIC;
}

HaiGamePad::Mode GamePadMode(uint64_t titleId, const std::string &rvltTmd, bool &configured) {
    configured = false;
    if (FILE *file = fopen(kGamePadConfig, "r")) {
        char line[96];
        char wanted[17];
        snprintf(wanted, sizeof(wanted), "%016llX", (unsigned long long) titleId);
        while (fgets(line, sizeof(line), file)) {
            std::string text(line);
            while (!text.empty() && isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
            const auto equals = text.find('=');
            if (equals != 16) continue;
            std::string id = text.substr(0, 16);
            for (auto &c : id) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            HaiGamePad::Mode mode;
            if (id == wanted && HaiGamePad::Parse(text.substr(17), mode)) {
                fclose(file);
                configured = true;
                return mode;
            }
        }
        fclose(file);
    }
    char gameCode[4] {};
    if (FILE *file = fopen(rvltTmd.c_str(), "rb")) {
        if (fseek(file, 0x190, SEEK_SET) != 0 || fread(gameCode, 1, 4, file) != 4) memset(gameCode, 0, 4);
        fclose(file);
    }
    return HaiGamePad::Default(gameCode);
}

bool ScanFirmware(const std::string &path, HaiGamePad::Scanner &scanner) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return false;
    for (size_t got; (got = fread(sCopyBuffer, 1, kCopySize, file)) != 0;) scanner.Feed(sCopyBuffer, got);
    const bool ok = !ferror(file);
    fclose(file);
    scanner.Finish();
    return ok;
}

// Edits only the staged SD copy; every site is verified before any write.
bool ApplyEdits(const std::string &path, const std::vector<HaiGamePad::Edit> &edits, std::string &detail) {
    FILE *file = fopen(path.c_str(), "r+b");
    if (!file) {
        detail = "open-write";
        return false;
    }
    auto readHalf = [&](uint32_t offset, uint16_t &value) {
        unsigned char b[2];
        if (fseek(file, offset, SEEK_SET) != 0 || fread(b, 1, 2, file) != 2) return false;
        value = uint16_t(b[0] << 8 | b[1]);
        return true;
    };
    bool ok = true;
    for (const auto &e : edits) {
        uint16_t value = 0;
        if (!readHalf(e.offset, value) || (value & e.mask) != e.expect) {
            detail = "unexpected-instruction";
            ok = false;
            break;
        }
    }
    for (size_t i = 0; ok && i < edits.size(); ++i) {
        const auto &e = edits[i];
        uint16_t value = 0;
        ok = readHalf(e.offset, value);
        const uint16_t result = uint16_t((value & ~e.writeMask) | (e.value & e.writeMask));
        const unsigned char b[2] = {uint8_t(result >> 8), uint8_t(result)};
        ok = ok && fseek(file, e.offset, SEEK_SET) == 0 && fwrite(b, 1, 2, file) == 2;
        if (!ok) detail = "write";
    }
    ok = fclose(file) == 0 && ok;
    return ok;
}

// meta.xml <drc_use>: 65537 is what the Wii U Menu treats as "GamePad can be the controller".
unsigned MetaDrcUse(const std::string &codeDirectory) {
    const std::string meta = codeDirectory.substr(0, codeDirectory.size() - 4) + "meta/meta.xml";
    FILE *file = fopen(meta.c_str(), "rb");
    if (!file) return 0;
    std::string xml(64 * 1024, '\0');
    xml.resize(fread(xml.data(), 1, xml.size(), file));
    fclose(file);
    const auto tag = xml.find("<drc_use");
    const auto start = tag == std::string::npos ? tag : xml.find('>', tag);
    return start == std::string::npos ? 0 : static_cast<unsigned>(strtoul(xml.c_str() + start + 1, nullptr, 10));
}

// The Wii U Menu asks "Select a display option" before a Wii title starts and
// stores the answer with these nn_cmpt account settings; nn_cmpt then writes it
// into the vWii's conf.bin/settings.sav. uFlow bypasses the Menu, so it sets
// them itself (same calls as the Menu's "Use GamePad as controller").
void SetHaiDisplay(bool gamePadController) {
    OSDynLoad_Module cmpt = nullptr;
    int32_t (*setScreen)(int32_t) = nullptr, (*setDrcCtrl)(int32_t) = nullptr, (*checkScreen)() = nullptr;
    if (OSDynLoad_Acquire("nn_cmpt.rpl", &cmpt) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(cmpt, OS_DYNLOAD_EXPORT_FUNC, "CMPTAcctSetScreenType", reinterpret_cast<void **>(&setScreen)) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(cmpt, OS_DYNLOAD_EXPORT_FUNC, "CMPTAcctSetDrcCtrlEnabled", reinterpret_cast<void **>(&setDrcCtrl)) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(cmpt, OS_DYNLOAD_EXPORT_FUNC, "CMPTCheckScreenState", reinterpret_cast<void **>(&checkScreen)) != OS_DYNLOAD_OK) {
        Trace("hai-display unavailable");
        if (cmpt) OSDynLoad_Release(cmpt);
        return;
    }
    constexpr int32_t kScreenDrc = 2, kScreenBoth = 3; // CmptScreenType
    int32_t drcResult, screenResult;
    if (gamePadController) {
        screenResult = setScreen(kScreenBoth);
        drcResult    = setDrcCtrl(1);
    } else {
        drcResult    = setDrcCtrl(0);
        screenResult = setScreen(kScreenBoth); // TV and GamePad; Wii Remote controls
    }
    const int32_t state = checkScreen();
    if (state < 0) screenResult = setScreen(kScreenDrc); // no TV: GamePad screen only
    Trace("hai-display gamepad-controller=%d screen=%d drc=%d tv-state=%d", gamePadController, screenResult, drcResult, state);
    OSDynLoad_Release(cmpt);
}

// Nintendo's own starting value for nn::hai::error::Error (returned via r3).
void InitHaiError(uint32_t *error) {
    memset(error, 0, 5 * sizeof(uint32_t));
    OSDynLoad_Module module = nullptr;
    void (*init)(uint32_t *) = nullptr;
    if (OSDynLoad_Acquire("nn_hai", &module) == OS_DYNLOAD_OK &&
        OSDynLoad_FindExport(module, OS_DYNLOAD_EXPORT_FUNC, "Init__Q3_2nn3hai5errorFv",
                             reinterpret_cast<void **>(&init)) == OS_DYNLOAD_OK &&
        init) {
        init(error);
    }
    if (module) OSDynLoad_Release(module);
}
} // namespace

// nn::hai::user::CreateCompanionFile(void *, unsigned) -> nn::hai::error::Error.
// Nintendo asks FSGetFileBlockAddress for the WFS blocks of each hif_*.nfs;
// uFlow supplies the same physical-range table from the files' FAT32 chains.
DECL_FUNCTION(void, HaiCreateCompanionFile, uint32_t *error, void *buffer, uint32_t size) {
    if (!sReady) {
        real_HaiCreateCompanionFile(error, buffer, size);
        return;
    }
    InitHaiError(error);
    if (!buffer || size < sCompanion.size()) {
        Trace("hai-companion-buffer-too-small have=%u need=%u", size, (unsigned) sCompanion.size());
        OSFatal("uFlow: Wii VC companion buffer is too small. Power off the console.");
    }
    memcpy(buffer, sCompanion.data(), sCompanion.size());
    const int32_t mcp = MCP_Open();
    const int32_t result = mcp < 0 ? mcp : MCP_CompatHaiCreateCompanionFile(mcp, buffer, sCompanion.size());
    if (mcp >= 0) MCP_Close(mcp);
    Trace("hai-companion-created result=%d bytes=%u", result, (unsigned) sCompanion.size());
    if (result < 0) OSFatal("uFlow: the system rejected the Wii VC USB companion. Power off the console.");
}

namespace RawHai {
Result PrepareStaged(const std::string &code, const char *codePath, const char *contentPath, uint64_t titleId);

Result Prepare(const char *codePath, const char *contentPath, uint64_t titleId) {
    Clear();
    const std::string code = RawSourcePath(codePath);
    if (!IsFile(code + "/nn_hai_user.rpl") || !IsFile(code + "/fw.img")) return Result::NotHai;
    sCopyBuffer = static_cast<unsigned char *>(malloc(kCopySize));
    if (!sCopyBuffer) {
        Trace("hai-prepare-failed copy-buffer");
        return Result::Failed;
    }
    const Result result = PrepareStaged(code, codePath, contentPath, titleId);
    free(sCopyBuffer);
    sCopyBuffer = nullptr;
    return result;
}

Result PrepareStaged(const std::string &code, const char *codePath, const char *contentPath, uint64_t titleId) {
    if (!RawSourceUSB(codePath) || !RawSourceUSB(contentPath)) {
        Trace("hai-prepare-failed source-not-usb");
        return Result::Failed;
    }
    if ((titleId >> 32) != 0x00050000) {
        Trace("hai-prepare-failed title=%016llX", (unsigned long long) titleId);
        return Result::Failed;
    }
    std::vector<unsigned char> companion;
    if (!USBStorage::BuildHaiCompanion(contentPath, companion)) {
        Trace("hai-prepare-failed companion");
        return Result::Failed;
    }
    const unsigned device = USBStorage::RawDevice();
    if (!device) {
        Trace("hai-prepare-failed usb-device");
        return Result::Failed;
    }
    const std::string stage = std::string(kStageRoot) + "/code";
    CreateSubfolder(stage.c_str());
    for (const char *name : kCodeFiles) {
        if (!CopyVerified(code + "/" + name, stage + "/" + name)) {
            Trace("hai-prepare-failed stage %s", name);
            return Result::Failed;
        }
    }
    bool configured = false;
    const auto mode = GamePadMode(titleId, stage + "/rvlt.tmd", configured);
    HaiGamePad::Scanner scanner;
    if (!ScanFirmware(stage + "/fw.img", scanner)) {
        Trace("hai-prepare-failed scan fw.img");
        return Result::Failed;
    }
    // Only the title's own firmware is ever used. Substituting Rhythm Heaven
    // Fever's r590 fw.img for DKCR's r569 one hard-froze the console (2026-09-22).
    const std::string firmware = code;
    std::vector<HaiGamePad::Edit> edits, part;
    std::string why;
    bool gamePadPlanned = false;
    if (mode != HaiGamePad::Mode::Classic) {
        if ((gamePadPlanned = HaiGamePad::Plan(mode, scanner, part, why)))
            edits.insert(edits.end(), part.begin(), part.end());
        else
            Trace("hai-gamepad mode=%s unavailable %s; using stock mapping", HaiGamePad::Name(mode), why.c_str());
    }
    // HOME Menu "Wii Menu" -> Return to Wii U Menu channel (HCVA) -> uFlow.
    const bool returnPlanned = HaiGamePad::PlanReturnToWiiU(scanner, part, why);
    if (returnPlanned) edits.insert(edits.end(), part.begin(), part.end());
    else Trace("hai-return unavailable %s; Wii Menu stays in vWii", why.c_str());
    if (!edits.empty()) {
        std::string detail;
        if (ApplyEdits(stage + "/fw.img", edits, detail)) {
            if (gamePadPlanned)
                Trace("hai-gamepad mode=%s source=%s r%u", HaiGamePad::Name(mode), configured ? "config" : "default", scanner.revision);
            if (returnPlanned) Trace("hai-return patched wii-menu->HCVA");
        } else {
            // Never launch a partially edited firmware: restore the stock copy.
            Trace("hai-firmware-edit failed %s; using stock firmware", detail.c_str());
            if (!CopyVerified(firmware + "/fw.img", stage + "/fw.img")) {
                Trace("hai-prepare-failed restage fw.img");
                return Result::Failed;
            }
        }
    }
    const unsigned drcUse = MetaDrcUse(code);
    Trace("hai-meta drc_use=%u", drcUse);
    SetHaiDisplay(drcUse == 65537 || mode != HaiGamePad::Mode::Classic);
    if (!ArmMcp(device, titleId)) {
        Trace("hai-prepare-failed mcp-arm");
        return Result::Failed;
    }
    sCompanion = std::move(companion);
    sTitleId   = titleId;
    sReady     = true;
    Trace("hai-prepare-ready title=%016llX device=usb%02u companion=%u",
          (unsigned long long) titleId, device, (unsigned) sCompanion.size());
    return Result::Ready;
}

void Clear() {
    sReady = false;
    sCompanion.clear();
    sTitleId = 0;
}

void InstallCompanionPatch() {
    if (!sReady || sPatch) return;
    if (OSGetTitleID() != sTitleId) {
        Trace("hai-companion-patch skipped title=%016llX", (unsigned long long) OSGetTitleID());
        return;
    }
    // WUMS runs this from the RPX entry point, after the loader linked every
    // import of frisbiiU.rpx. FunctionPatcher's by-name executable patches
    // cannot be used: they require MCP title info, which loose titles lack.
    void *function = nullptr;
    if (OSDynLoad_Acquire("nn_hai_user", &sHaiUser) != OS_DYNLOAD_OK ||
        OSDynLoad_FindExport(sHaiUser, OS_DYNLOAD_EXPORT_FUNC, "CreateCompanionFile__Q3_2nn3hai4userFPvUi",
                             &function) != OS_DYNLOAD_OK ||
        !function) {
        Trace("hai-companion-patch export-missing");
        OSFatal("uFlow: Wii VC wrapper library nn_hai_user was not found. Power off the console.");
    }
    const uint32_t effective = reinterpret_cast<uint32_t>(function);
    const uint32_t physical  = OSEffectiveToPhysical(effective);
    function_replacement_data_t data =
            REPLACE_FUNCTION_VIA_ADDRESS_FOR_PROCESS(HaiCreateCompanionFile, physical, effective, FP_TARGET_PROCESS_GAME);
    bool patched      = false;
    const auto status = FunctionPatcher_AddFunctionPatch(&data, &sPatch, &patched);
    Trace("hai-companion-patch status=%d patched=%d address=%08X physical=%08X", status, patched, effective, physical);
    if (status != FUNCTION_PATCHER_RESULT_SUCCESS || !patched || !physical) {
        if (status == FUNCTION_PATCHER_RESULT_SUCCESS) FunctionPatcher_RemoveFunctionPatch(sPatch);
        sPatch = 0;
        OSFatal("uFlow: the Wii VC companion redirect could not be installed. Power off the console.");
    }
}

void RemoveCompanionPatch() {
    if (!sPatch) return; // only the prepared game process installs it
    FunctionPatcher_RemoveFunctionPatch(sPatch);
    sPatch = 0;
    if (sHaiUser) OSDynLoad_Release(sHaiUser);
    sHaiUser = nullptr;
    Clear();
}
} // namespace RawHai
