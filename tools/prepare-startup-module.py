"""Add opt-in uFlow startup to the pinned Aroma AutobootModule (SD only)."""
import argparse
import shutil
import subprocess
from pathlib import Path

PIN = 'db1a0dd32a77fbe66b0287ab853a3af0d2946ed8'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    source=parser.parse_args().source.resolve(strict=True)
    if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=PIN:
        raise RuntimeError('Unexpected AutobootModule revision')
    original=subprocess.check_output(['git','-C',str(source),'show','HEAD:source/main.cpp']).decode().replace('\r\n','\n')
    anchor='            case BOOT_OPTION_WII_U_MENU:\n                bootWiiUMenu();'
    replacement='''            case BOOT_OPTION_WII_U_MENU:
                // Only this boot invocation; exiting uFlow later still reaches Menu.
                // An explicit boot-menu choice (+), missing app, or launch error
                // must always keep the normal Menu route available.
                if (!hadMenu && !(buttons.hold & VPAD_BUTTON_PLUS) && UFlowStartup::Enabled()) {
                    struct stat appInfo{};
                    if (stat(UFlowStartup::Target, &appInfo) == 0 && S_ISREG(appInfo.st_mode) &&
                        RPXLoader_LaunchHomebrew("wiiu/apps/uFlow/uFlow.wuhb") == RPX_LOADER_RESULT_SUCCESS) break;
                }
                bootWiiUMenu();'''
    assert original.count(anchor)==1
    patched=original.replace('#include "BootUtils.h"','#include "BootUtils.h"\n#include "startup_config.hpp"').replace(anchor,replacement)
    path=source/'source/main.cpp'
    if path.read_text() not in (original,patched): raise RuntimeError('Unrelated startup source edits')
    path.write_text(patched,newline='\n')
    shutil.copyfile(Path(__file__).resolve().parents[1]/'app/source/startup_config.hpp',source/'source/startup_config.hpp')
    print('Prepared opt-in uFlow startup; + bypass, Quick Start and Menu fallback preserved.')

if __name__=='__main__': main()
