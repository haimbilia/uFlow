#pragma once
#include <array>
#include <padscore/kpad.h>
#include <vpad/input.h>

// Upright Wii Remote. HOME stays with the system; minus never starts a scan.
constexpr uint32_t WiiRemoteButtons(uint32_t buttons) {
    uint32_t mapped = 0;
    constexpr uint32_t pairs[][2] = {
        {WPAD_BUTTON_LEFT, VPAD_BUTTON_LEFT}, {WPAD_BUTTON_RIGHT, VPAD_BUTTON_RIGHT},
        {WPAD_BUTTON_UP, VPAD_BUTTON_UP}, {WPAD_BUTTON_DOWN, VPAD_BUTTON_DOWN},
        {WPAD_BUTTON_A, VPAD_BUTTON_A}, {WPAD_BUTTON_B, VPAD_BUTTON_B},
        {WPAD_BUTTON_PLUS, VPAD_BUTTON_PLUS},
        {WPAD_BUTTON_1, VPAD_BUTTON_L}, {WPAD_BUTTON_2, VPAD_BUTTON_R},
        {WPAD_BUTTON_MINUS, VPAD_BUTTON_Y}, // title details
    };
    for (const auto &pair : pairs) if (buttons & pair[0]) mapped |= pair[1];
    return mapped;
}

constexpr uint32_t NunchukButtons(uint32_t buttons) {
    return ((buttons & WPAD_NUNCHUK_BUTTON_C) ? VPAD_BUTTON_Y : 0) |
           ((buttons & WPAD_NUNCHUK_BUTTON_Z) ? VPAD_BUTTON_X : 0);
}

// The Pro Controller is reported by KPAD as its own extension, with a separate
// bit layout. Normalize it into the VPAD layout used by the rest of uFlow.
constexpr uint32_t ProControllerButtons(uint32_t buttons) {
    uint32_t mapped = 0;
    constexpr uint32_t pairs[][2] = {
        {WPAD_PRO_BUTTON_LEFT, VPAD_BUTTON_LEFT}, {WPAD_PRO_BUTTON_RIGHT, VPAD_BUTTON_RIGHT},
        {WPAD_PRO_BUTTON_UP, VPAD_BUTTON_UP}, {WPAD_PRO_BUTTON_DOWN, VPAD_BUTTON_DOWN},
        {WPAD_PRO_BUTTON_A, VPAD_BUTTON_A}, {WPAD_PRO_BUTTON_B, VPAD_BUTTON_B},
        {WPAD_PRO_BUTTON_X, VPAD_BUTTON_X}, {WPAD_PRO_BUTTON_Y, VPAD_BUTTON_Y},
        {WPAD_PRO_BUTTON_PLUS, VPAD_BUTTON_PLUS},
        {WPAD_PRO_TRIGGER_L, VPAD_BUTTON_L}, {WPAD_PRO_TRIGGER_R, VPAD_BUTTON_R},
        {WPAD_PRO_TRIGGER_ZL, VPAD_BUTTON_ZL}, {WPAD_PRO_TRIGGER_ZR, VPAD_BUTTON_ZR},
    };
    for (const auto &pair : pairs) if (buttons & pair[0]) mapped |= pair[1];
    return mapped;
}

// Separate engage/release thresholds prevent drift and repeat restarts near an edge.
constexpr uint32_t NunchukStick(float x, float y, uint32_t previous) {
    uint32_t directions = 0;
    if (x < -((previous & VPAD_BUTTON_LEFT) ? .28f : .42f)) directions |= VPAD_BUTTON_LEFT;
    if (x > ((previous & VPAD_BUTTON_RIGHT) ? .28f : .42f)) directions |= VPAD_BUTTON_RIGHT;
    if (y > ((previous & VPAD_BUTTON_UP) ? .32f : .48f)) directions |= VPAD_BUTTON_UP;
    if (y < -((previous & VPAD_BUTTON_DOWN) ? .32f : .48f)) directions |= VPAD_BUTTON_DOWN;
    return directions;
}

// On returning from HOME, require released buttons / centered sticks before
// accepting input again. The neutral interval also lets old wireless samples drain.
class InputResumeGate {
public:
    void Arm() { active_ = true; neutralSeen_ = false; }
    bool Allows(uint32_t ticks, bool neutral) {
        if (!active_) return true;
        if (!neutral) { neutralSeen_ = false; return false; }
        if (!neutralSeen_) { neutralSeen_ = true; neutralSince_ = ticks; }
        if (uint32_t(ticks-neutralSince_) < 150) return false;
        active_ = false;
        return true;
    }
private:
    bool active_ = false, neutralSeen_ = false;
    uint32_t neutralSince_ = 0;
};

class WiiRemoteInput {
public:
    void Reset() { channels_ = {}; }
    void Merge(VPADStatus &input, uint32_t ticks) {
        for (unsigned i = 0; i < channels_.size(); ++i) {
            auto &cached = channels_[i];
            KPADStatus remote{};
            KPADError error = KPAD_ERROR_NO_SAMPLES;
            const auto count = KPADReadEx(static_cast<KPADChan>(i), &remote, 1, &error);
            if (count && error == KPAD_ERROR_OK && remote.error == 0) {
                if (remote.extensionType == WPAD_EXT_PRO_CONTROLLER) {
                    cached.hold = ProControllerButtons(remote.pro.hold) |
                        NunchukStick(remote.pro.leftStick.x, remote.pro.leftStick.y, cached.stick);
                    cached.stick = cached.hold & (VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN);
                    input.trigger |= ProControllerButtons(remote.pro.trigger);
                    cached.updated = ticks;
                    input.hold |= cached.hold;
                    continue;
                }
                const bool supported = remote.extensionType == WPAD_EXT_CORE ||
                    remote.extensionType == WPAD_EXT_NUNCHUK || remote.extensionType == WPAD_EXT_CLASSIC ||
                    remote.extensionType == WPAD_EXT_MPLUS || remote.extensionType == WPAD_EXT_MPLUS_NUNCHUK ||
                    remote.extensionType == WPAD_EXT_MPLUS_CLASSIC;
                cached.hold = supported ? WiiRemoteButtons(remote.hold) : 0;
                const bool nunchuk = remote.extensionType == WPAD_EXT_NUNCHUK ||
                    remote.extensionType == WPAD_EXT_MPLUS_NUNCHUK;
                if (nunchuk) {
                    cached.stick = NunchukStick(remote.nunchuk.stick.x, remote.nunchuk.stick.y, cached.stick);
                    cached.hold |= cached.stick | NunchukButtons(remote.nunchuk.hold);
                    input.trigger |= NunchukButtons(remote.nunchuk.trigger);
                } else cached.stick = 0;
                cached.updated = ticks;
                if (supported) input.trigger |= WiiRemoteButtons(remote.trigger);
            } else if ((error != KPAD_ERROR_NO_SAMPLES && error != KPAD_ERROR_BUSY) ||
                       count || uint32_t(ticks - cached.updated) >= 100) {
                cached.hold = 0;
                cached.stick = 0;
            }
            // Preserve held navigation between wireless samples, never replay triggers.
            input.hold |= cached.hold;
        }
    }
private:
    struct Channel { uint32_t hold = 0, updated = 0, stick = 0; };
    std::array<Channel, 4> channels_{};
};

static_assert(WiiRemoteButtons(WPAD_BUTTON_1) == VPAD_BUTTON_L);
static_assert(WiiRemoteButtons(WPAD_BUTTON_2) == VPAD_BUTTON_R);
static_assert(WiiRemoteButtons(WPAD_BUTTON_HOME) == 0);
