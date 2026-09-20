#pragma once
#include "launcher_modules.hpp"
#include "vc_inject.hpp"

inline std::string VCInjectionProfile(const LauncherModule *module,const GameEntry &rom,
                                      const std::string &profile,VCInject::Settings &settings) {
    if(!module||!module->error.empty())return "VC INJECTION MODULE MISSING / INVALID";
    const auto system=VCSystems::Protocol(module->protocol);
    if(system.empty())return "UNSUPPORTED VC INJECTION PROTOCOL";
    if(rom.platform!=Platform::Files||VCSystems::File(rom.absolutePath)!=system)
        return "ROM DOES NOT MATCH VC INJECTION MODULE";
    settings=VCInject::Load(profile);
    settings.system=system; // System comes from the validated module, not a DS default.
    return {};
}

inline std::string ConfigureVCLaunch(GameEntry &rom,const LauncherModule *module,
                                     const std::string &profile,VCInject::Progress progress={}) {
    VCInject::Settings settings;
    const auto error=VCInjectionProfile(module,rom,profile,settings);
    return error.empty()?VCInject::LaunchOrPrepare(rom,settings,progress):error;
}
