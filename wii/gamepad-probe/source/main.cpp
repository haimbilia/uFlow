#include <gccore.h>
#include <fat.h>
#include <ogc/wiilaunch.h>
#include <ogc/lwp_watchdog.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include "../packet.hpp"
#include "../transport.hpp"

static FILE *logFile;
static volatile bool finish;
static void Log(const char *format, ...) {
    va_list args;
    va_start(args, format); vprintf(format, args); va_end(args);
    if (logFile) { va_start(args, format); vfprintf(logFile, format, args); va_end(args); fflush(logFile); }
}
static void ResetPressed(u32, void *) { finish = true; }
static uint32_t Word(uintptr_t address) {
    DCInvalidateRange(reinterpret_cast<void *>(address & ~uintptr_t(31)), 32);
    return *reinterpret_cast<volatile uint32_t *>(address);
}
struct Bus {
    uint32_t lastStatus = 0, lastFifo = 0;
    uint32_t Read(uint32_t offset) {
        asm volatile("eieio" ::: "memory");
        const auto value = *reinterpret_cast<volatile uint32_t *>(0xcd800570 + offset);
        if (offset == GamePadProbe::Status) lastStatus = value;
        if (offset == GamePadProbe::ReadData) lastFifo = value;
        return value;
    }
    void Write(uint32_t offset, uint32_t value) {
        *reinterpret_cast<volatile uint32_t *>(0xcd800570 + offset) = value;
        asm volatile("eieio" ::: "memory");
    }
    void Delay() { usleep(500); }
};
int main(int argc, char **argv) {
    const bool modeRequested = GamePadProbe::HasOption(argc, argv, "--query-hai-mode");
    const bool queryRequested = GamePadProbe::QueryRequested(argc, argv) || modeRequested;
    VIDEO_Init();
    auto *mode = VIDEO_GetPreferredMode(nullptr);
    void *frame = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    if (!frame) return 1;
    console_init(frame, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode); VIDEO_SetNextFramebuffer(frame); VIDEO_SetBlack(false); VIDEO_Flush(); VIDEO_WaitVSync();
    SYS_SetResetCallback(ResetPressed);
    const bool sd = fatInitDefault();
    if (sd) logFile = fopen("sd:/wiiu/apps/uFlow/gamepad-probe.log", "a");
    Log("\nUFLOW GAMEPAD PROBE v3 (%s, 30 seconds)\n", modeRequested ? "input-query-hai-mode" : queryRequested ? "input-query" : "passive");
    Log("IOS=%u revision=%u log=%s\n", IOS_GetVersion(), IOS_GetRevision(), logFile ? "SD" : "screen only");
    const bool wiiu = *reinterpret_cast<volatile uint16_t *>(0xcd8005a0) == 0xcafe;
    const bool ahb = *reinterpret_cast<volatile uint32_t *>(0xcd800064) == 0xffffffff;
    Log("WiiU=%u AHBPROT-access=%u\n", unsigned(wiiu), unsigned(ahb));
    uintptr_t buffer = 0;
    if (wiiu && ahb) {
        // Existing libwiidrc profile literals. Never infer a buffer from IOS
        // version alone, and never follow an arbitrary pointer read from RAM.
        const auto r569 = Word(0x938b2964), later = Word(0x938b2564);
        Log("HAI profile words=%08x,%08x\n", unsigned(r569), unsigned(later));
        if (r569 == 0x138bb004 || later == 0x138bb004) buffer = 0x938bb004;
        else if (later == 0x138ba004) buffer = 0x938ba004;
        // Observe control and status only. Reading FIFO data would consume it;
        // this first probe never reads FIFOs or writes hardware registers.
        Log("I2C3 control=%08x interrupt-enable=%08x status=%08x\n",
            unsigned(*reinterpret_cast<volatile uint32_t *>(0xcd800570)),
            unsigned(*reinterpret_cast<volatile uint32_t *>(0xcd800580)),
            unsigned(*reinterpret_cast<volatile uint32_t *>(0xcd800584)));
    }
    Log("input=%s\n", buffer ? "known HAI shared buffer" : "UNAVAILABLE: no validated exposed buffer");
    Bus bus;
    GamePadProbe::Transport<Bus> transport(bus, modeRequested ? 0x611e0001 : 0x611e0003);
    GamePadProbe::ModeExperiment<Bus> modeExperiment(bus);
    // Deliberately restricted to the IOS58 foreground environment examined in
    // the first hardware test. HAI already owns the bus and is never queried.
    bool query = queryRequested && wiiu && ahb && !buffer &&
        IOS_GetVersion() == 58 && IOS_GetRevision() == 6432;
    if (query && modeRequested) {
        query = modeExperiment.Begin();
        Log("HAI mode experiment start=%u control=%08x\n", unsigned(query), unsigned(bus.Read(GamePadProbe::Control)));
    }
    query = query && transport.Compatible();
    if (!query) modeExperiment.End();
    Log("direct-input-query=%s\n", query ? "enabled; device 70 register F0 only" : "disabled");
    Log("Press/release GamePad HOME several times. It will NOT exit.\n");
    Log("Automatic Wii U return after 30 seconds; RESET ends early.\n");
    GamePadProbe::HomeEdges edges;
    unsigned home = 0, changes = 0;
    uint8_t previous[9]{};
    bool havePrevious = false;
    const auto start = gettime();
    while (!finish && ticks_to_millisecs(gettime() - start) < 30000) {
        if (query) {
            uint8_t packet[11]{};
            GamePadProbe::Sample sample;
            const auto result = transport.Read(sample, packet);
            if (result != GamePadProbe::ReadResult::Ok) {
                Log("QUERY STOP %s status=%08x fifo=%08x\n", GamePadProbe::Name(result), unsigned(bus.lastStatus), unsigned(bus.lastFifo));
                Log("packet="); for (auto byte : packet) Log("%02x", unsigned(byte)); Log("\n");
                query = false; edges.Reset(); // Never retry errors or reset the bus.
                modeExperiment.End(); // Restore the original mode immediately on any error.
            } else {
                if (edges.Update(sample)) { ++home; Log("HOME edge at %llu ms\n", ticks_to_millisecs(gettime() - start)); }
                if (!havePrevious || memcmp(packet, previous, 9)) {
                    ++changes;
                    if (!havePrevious || previous[2] != packet[2] || previous[3] != packet[3])
                        Log("packet buttons=%04x sequence=%02x/%02x shutdown=%u\n", unsigned(sample.buttons), unsigned(sample.sequence), unsigned(sample.sequenceExtra), unsigned(sample.shutdown));
                    memcpy(previous, packet, 9); havePrevious = true;
                }
            }
        }
        if (buffer) {
            uint8_t data[9], second[9];
            DCInvalidateRange(reinterpret_cast<void *>(buffer & ~uintptr_t(31)), 32);
            for (unsigned i = 0; i < 9; ++i) data[i] = reinterpret_cast<volatile uint8_t *>(buffer)[i];
            DCInvalidateRange(reinterpret_cast<void *>(buffer & ~uintptr_t(31)), 32);
            for (unsigned i = 0; i < 9; ++i) second[i] = reinterpret_cast<volatile uint8_t *>(buffer)[i];
            if (memcmp(data, second, 9) != 0) { usleep(5000); continue; }
            if (!havePrevious || memcmp(data, previous, 9)) {
                GamePadProbe::Sample sample;
                GamePadProbe::Payload(data, 9, sample);
                if (edges.Update(sample)) { ++home; Log("HOME edge at %llu ms\n", ticks_to_millisecs(gettime() - start)); }
                ++changes; memcpy(previous, data, 9); havePrevious = true;
            }
        }
        usleep(query ? 20000 : 5000);
    }
    modeExperiment.End();
    if (modeRequested && wiiu && ahb) Log("restored-control=%08x\n", unsigned(bus.Read(GamePadProbe::Control)));
    Log("RESULT buffer=%s query_requested=%u changes=%u home_edges=%u\n", buffer ? "known" : "unavailable", unsigned(queryRequested), changes, home);
    Log("This test is foreground only; it does not prove in-game persistence.\nReturning through HCVA.\n");
    if (logFile) { fclose(logFile); logFile = nullptr; }
    if (sd) fatUnmount("sd:");
    if (wiiu) { WII_Initialize(); WII_LaunchTitle(0x0001000248435641ULL); }
    return 0;
}
