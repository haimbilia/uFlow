"""Pinned, exact SD-path uFlow entry for USB Loader GX; no saved path changes."""
import argparse
import shutil
import subprocess
from pathlib import Path
PIN='e25c4f3501ed957b7db73f79c51fdf00715ab2e2'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    source=parser.parse_args().source.resolve(strict=True)
    if subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()!=PIN:
        raise RuntimeError('Unexpected USB Loader GX revision')
    patches={
        'source/StartUpProcess.cpp':[
            ('\t\tif ((strlen(argv[i]) == 6', '\t\tif (strcmp(argv[i], "--uflow") == 0) quickBoot = i;\n\n\t\tif ((strlen(argv[i]) == 6'),
            ('int StartUpProcess::QuickGameBoot(const char *gameID)\n{', 'int StartUpProcess::QuickGameBoot(const char *gameID)\n{\n\tif(strcmp(gameID,"--uflow")==0) return UFlowUsbLaunch();'),
        ],
        'source/usbloader/MountGamePartition.cpp':[
            ('int MountGamePartition(bool ShowGUI)\n{','int MountGamePartition(bool ShowGUI)\n{\n\tif(gUFlowActive) return gUFlowRequest.kind==WiiRequest::Kind::GameCube ? 0 : WBFS_OpenPartSD();'),
        ],
        'source/usbloader/GameList.cpp':[
            ('int GameList::GetPartitionNumber(const u8 *gameID) const\n{','int GameList::GetPartitionNumber(const u8 *gameID) const\n{\n\tif(UFlowMatches(gameID)) return DeviceHandler::GetSDPartition();'),
        ],
        'source/usbloader/wbfs/wbfs_fat.cpp':[
            ('std::string Wbfs_Fat::FindFilename(u8 *id)\n{','std::string Wbfs_Fat::FindFilename(u8 *id)\n{\n\tif(UFlowMatches(id)) return gUFlowRequest.path;'),
        ],
        'source/GameCube/GCGames.cpp':[
            ('const char *GCGames::GetPath(const char *gameID) const\n{','const char *GCGames::GetPath(const char *gameID) const\n{\n\tif(UFlowMatches(gameID)) return gUFlowRequest.path.c_str();'),
        ],
        'source/usbloader/GameBooter.cpp':[
            ('int GameBooter::BootGCMode(struct discHdr *gameHdr)\n{','int GameBooter::BootGCMode(struct discHdr *gameHdr)\n{\n\tif(gUFlowActive) return BootNintendont(gameHdr);'),
            ('u8 multiDiscChoice = Settings.MultiDiscPrompt;','u8 multiDiscChoice = gUFlowActive ? 0 : Settings.MultiDiscPrompt;'),
            ('u8 ninAutobootChoice = Settings.NINAutoboot;','u8 ninAutobootChoice = gUFlowActive ? 1 : Settings.NINAutoboot;'),
            ('u64 returnToChoice = strlen(Settings.returnTo) > 0 ? (game_cfg->returnTo ? NandTitles.FindU32(Settings.returnTo) : 0) : 0;',
             'u64 returnToChoice = gUFlowActive ? 0x0001000248435641ULL : (strlen(Settings.returnTo) > 0 ? (game_cfg->returnTo ? NandTitles.FindU32(Settings.returnTo) : 0) : 0);'),
        ],
        'source/homebrewboot/BootHomebrew.cpp':[
            ('Set_Stub(returnTo(false));','Set_Stub(gUFlowActive ? 0x0001000248435641ULL : returnTo(false));'),
        ],
    }
    staged={}
    for relative,replacements in patches.items():
        original=subprocess.check_output(['git','-C',str(source),'show','HEAD:'+relative]).decode().replace('\r\n','\n')
        patched=original
        for before,after in replacements:
            count=2 if before.startswith(('u64 returnToChoice','u8 multiDiscChoice')) else 1
            if patched.count(before)!=count: raise RuntimeError('Upstream anchor mismatch: '+before)
            patched=patched.replace(before,after)
        patched='#include "usbloader_entry.hpp"\n'+patched
        if (source/relative).read_text() not in (original,patched): raise RuntimeError('Unrelated edits: '+relative)
        staged[relative]=patched
    for relative,text in staged.items(): (source/relative).write_text(text,newline='\n')
    project=Path(__file__).resolve().parents[1]
    for name in ('usbloader_entry.cpp','usbloader_entry.hpp'):
        shutil.copyfile(project/'wii'/name,source/'source'/name)
    shutil.copyfile(project/'app/source/wii_request.hpp',source/'source/wii_request.hpp')
    print('Prepared USB Loader GX exact-path adapter (SD Wii/GC).')
if __name__=='__main__': main()
