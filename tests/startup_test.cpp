#include "settings.hpp"
#include "startup_config.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main() {
    std::filesystem::create_directories("fs:/vol/external01/wiiu/apps/uFlow");
    UserSettings controls;
    assert(controls.bindings[static_cast<int>(ControlAction::ZoomFront)]==0x0200);
    assert(controls.bindings[static_cast<int>(ControlAction::ZoomBack)]==0x0100);
    controls.bindings[static_cast<int>(ControlAction::ZoomFront)]=0x2000;
    SaveSettings(controls);
    assert(LoadSettings().bindings[static_cast<int>(ControlAction::ZoomFront)]==0x2000);
    ResetControlBindings(controls);
    assert(controls.bindings[static_cast<int>(ControlAction::ZoomFront)]==0x0200);
    assert(std::string(ControlActionName(ControlAction::ZoomBack))=="ZOOM BACK COVER");
    assert(controls.bindings[static_cast<int>(ControlAction::LaunchOptions)]==0x0004);
    assert(std::string(ControlActionName(ControlAction::LaunchOptions))=="LAUNCH OPTIONS");
    assert(std::string(ControlBindingName(0x0100))=="DPAD DOWN");
    std::ofstream("fs:/vol/external01/wiiu/apps/uFlow/uflow.cfg")<<"bind_0=4096\nbind_6=4\nbind_7=2\n";
    const auto migrated=LoadSettings();
    assert(migrated.bindings[0]==4096 && migrated.bindings[6]==0x0080 && migrated.bindings[7]==0x0040);
    assert(!LoadSettings().startOnBoot);
    assert(SetStartOnBoot(true));
    assert(LoadSettings().startOnBoot);
    assert(SetStartOnBoot(false));
    assert(!LoadSettings().startOnBoot);
    assert(SetStartOnBoot(false));
    for(const char *bad:{"", "enabled=0\n", "enabled=1", "enabled=1\nextra"}) {
        std::ofstream(UFlowStartup::Flag)<<bad;
        assert(!UFlowStartup::Enabled());
    }
    assert(SetStartOnBoot(false));
    puts("PASS: startup off by default, persistence, disable, malformed flags");
}
