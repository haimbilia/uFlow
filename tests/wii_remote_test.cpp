#include "wii_remote.hpp"
#include <cassert>
#include <cstdio>

static std::array<KPADStatus,4> samples{};
static std::array<KPADError,4> errors{};
uint32_t KPADReadEx(KPADChan channel, KPADStatus *out, uint32_t count, KPADError *error) {
    assert(count==1);
    *out=samples[channel]; *error=errors[channel];
    return *error==KPAD_ERROR_OK ? 1 : 0;
}
int main() {
    WiiRemoteInput remote;
    VPADStatus input{};
    errors.fill(KPAD_ERROR_NO_SAMPLES);
    errors[2]=KPAD_ERROR_OK;
    samples[2].hold=WPAD_BUTTON_RIGHT;
    samples[2].trigger=WPAD_BUTTON_1|WPAD_BUTTON_A;
    remote.Merge(input,1000);
    assert(input.hold==VPAD_BUTTON_RIGHT);
    assert(input.trigger==(VPAD_BUTTON_L|VPAD_BUTTON_A));
    errors[2]=KPAD_ERROR_NO_SAMPLES;
    input={}; remote.Merge(input,1016);
    assert(input.hold==VPAD_BUTTON_RIGHT && input.trigger==0);
    input={}; remote.Merge(input,1100);
    assert(input.hold==0 && input.trigger==0); // stale wireless data expires
    errors[2]=KPAD_ERROR_OK;
    input={VPAD_BUTTON_LEFT,VPAD_BUTTON_B}; remote.Merge(input,1200);
    assert(input.hold==(VPAD_BUTTON_LEFT|VPAD_BUTTON_RIGHT));
    assert(input.trigger==(VPAD_BUTTON_B|VPAD_BUTTON_L|VPAD_BUTTON_A));
    errors[2]=KPAD_ERROR_INVALID_CONTROLLER;
    input={}; remote.Merge(input,1201); assert(input.hold==0);
    errors[2]=KPAD_ERROR_OK;
    samples[2].extensionType=WPAD_EXT_PRO_CONTROLLER;
    samples[2].pro.hold=WPAD_PRO_BUTTON_RIGHT|WPAD_PRO_TRIGGER_L;
    samples[2].pro.trigger=WPAD_PRO_BUTTON_A|WPAD_PRO_TRIGGER_R;
    input={}; remote.Merge(input,1300); assert(input.hold==(VPAD_BUTTON_RIGHT|VPAD_BUTTON_L));
    assert(input.trigger==(VPAD_BUTTON_A|VPAD_BUTTON_R));
    samples[2].extensionType=WPAD_EXT_MPLUS;
    input={}; remote.Merge(input,1400); assert(input.hold==VPAD_BUTTON_RIGHT);
    remote.Reset(); errors.fill(KPAD_ERROR_NO_SAMPLES);
    input={}; remote.Merge(input,1401); assert(input.hold==0);
    assert(WiiRemoteButtons(WPAD_BUTTON_MINUS)==VPAD_BUTTON_Y);
    assert(WiiRemoteButtons(WPAD_BUTTON_PLUS)==VPAD_BUTTON_PLUS);
    assert(WiiRemoteButtons(WPAD_BUTTON_UP|WPAD_BUTTON_DOWN)==(VPAD_BUTTON_UP|VPAD_BUTTON_DOWN));
    assert(NunchukStick(.2f,-.2f,0)==0);
    assert(NunchukStick(-.7f,.7f,0)==(VPAD_BUTTON_LEFT|VPAD_BUTTON_UP));
    assert(NunchukStick(.7f,-.7f,0)==(VPAD_BUTTON_RIGHT|VPAD_BUTTON_DOWN));
    assert(NunchukStick(.35f,0,0)==0);
    assert(NunchukStick(.35f,0,VPAD_BUTTON_RIGHT)==VPAD_BUTTON_RIGHT);
    assert(NunchukStick(.25f,0,VPAD_BUTTON_RIGHT)==0);
    errors[0]=KPAD_ERROR_OK;
    samples[0]={}; samples[0].extensionType=WPAD_EXT_NUNCHUK;
    samples[0].nunchuk.stick={.8f,0};
    samples[0].nunchuk.hold=WPAD_NUNCHUK_BUTTON_C;
    samples[0].nunchuk.trigger=WPAD_NUNCHUK_BUTTON_Z;
    samples[0].trigger=WPAD_BUTTON_2;
    input={}; remote.Merge(input,1500);
    assert(input.hold==(VPAD_BUTTON_RIGHT|VPAD_BUTTON_Y));
    assert(input.trigger==(VPAD_BUTTON_X|VPAD_BUTTON_R));
    errors[0]=KPAD_ERROR_NO_SAMPLES;
    input={}; remote.Merge(input,1516);
    assert(input.hold==(VPAD_BUTTON_RIGHT|VPAD_BUTTON_Y) && input.trigger==0);
    input={}; remote.Merge(input,1600); assert(input.hold==0);
    errors[0]=KPAD_ERROR_OK;
    samples[0].extensionType=WPAD_EXT_MPLUS_NUNCHUK;
    input={}; remote.Merge(input,1700); assert(input.hold==(VPAD_BUTTON_RIGHT|VPAD_BUTTON_Y));
    samples[0].extensionType=WPAD_EXT_CORE;
    input={}; remote.Merge(input,1716); assert(input.hold==0); // extension detached
    samples[0].extensionType=WPAD_EXT_NUNCHUK;
    samples[0].nunchuk.stick={.35f,0}; samples[0].nunchuk.hold=0;
    input={}; remote.Merge(input,1732); assert(input.hold==0); // no stale hysteresis
    samples[0].nunchuk.stick={-.9f,0}; samples[0].error=-1;
    input={}; remote.Merge(input,1748); assert(input.hold==0 && input.trigger==0);
    puts("PASS: Wii Remote mappings, no GamePad, repeat samples, expiry, disconnect, merge, reset");
    puts("PASS: Nunchuk stick/deadzone/hysteresis, C/Z, MotionPlus, detachment and errors");
    InputResumeGate gate;
    assert(gate.Allows(0,false)); // normal use has no gate
    gate.Arm();
    assert(!gate.Allows(1000,false));
    assert(!gate.Allows(1001,true));
    assert(!gate.Allows(1100,false)); // queued held sample restarts neutral window
    assert(!gate.Allows(1101,true));
    assert(!gate.Allows(1250,true));
    assert(gate.Allows(1251,true));
    assert(gate.Allows(1252,false));
    gate.Arm();
    assert(!gate.Allows(0xfffffff0u,true));
    assert(gate.Allows(134,true)); // tick wrap
    puts("PASS: HOME resume neutral gate and queued input suppression");
}
