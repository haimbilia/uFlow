"""Apply the small uFlow SD-path entry patch to an exact WiiFlow v5.6.2 checkout."""
import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path

UPSTREAM = '66d12730f6d77fda4f659f2da2828303e074944d'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    project = Path(__file__).resolve().parents[1]
    revision = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != UPSTREAM:
        raise RuntimeError('Requires official WiiFlow v5.6.2 at ' + UPSTREAM)
    if hashlib.sha256((source / 'data/wfstub.bin').read_bytes()).hexdigest() != 'ba06ecf145fd26f9f13307b3e5803e56f7b0a37c74e2f74213a95b57fc1c8ea9':
        raise RuntimeError('Unexpected WiiFlow return stub; refusing fixed-offset patch build')
    changes = {
        'scripts/buildtype.sh': [],  # Windows checkout CRLF breaks shell heredoc
        'source/main.cpp': [
            ('#include "defines.h"', '#include "defines.h"\n#include "wii_request.hpp"'),
            ('char gameid[6];', 'char gameid[7];'),  # upstream strcpy of ID6 needs terminator
            ('DeviceHandle.MountSD();// mount SD before calling isUsingUSB() duh!',
             'DeviceHandle.MountSD();// mount SD before calling isUsingUSB() duh!\n'
             '\tWiiRequest::Request uflowRequest;\n'
             '\tconst bool uflowLaunch = !isWiiVC && WiiRequest::Consume(WiiRequest::WiiRequestPath, uflowRequest);'),
            ('if(strlen(gameid) == 0)// dont show if autobooting a wii game.',
             'if(!uflowLaunch && strlen(gameid) == 0)// dont show if autobooting a wii game.'),
            ('if(!isWiiVC && strlen(gameid) != 0)// if argv game ID then launch it',
             'if(uflowLaunch)\n'
             '\t\t\tmainMenu.launchUFlowGame(uflowRequest.id.c_str(), uflowRequest.path.c_str());\n'
             '\t\telse if(!isWiiVC && strlen(gameid) != 0)// if argv game ID then launch it'),
            ('mainMenu.launchUFlowGame(uflowRequest.id.c_str(), uflowRequest.path.c_str());',
             'mainMenu.launchUFlowGame(uflowRequest.id.c_str(), uflowRequest.path.c_str(), uflowRequest.kind == WiiRequest::Kind::GameCube);'),
            ('mainMenu.launchUFlowGame(uflowRequest.id.c_str(), uflowRequest.path.c_str(), uflowRequest.kind == WiiRequest::Kind::GameCube);',
             'mainMenu.launchUFlowGame(uflowRequest.id.c_str(), uflowRequest.path.c_str(), uflowRequest.kind == WiiRequest::Kind::GameCube, uflowRequest.launcher == WiiRequest::Launcher::USBLoaderGX);'),
        ],
        'source/menu/menu.hpp': [
            ('void directlaunch(const char *GameID);',
             'void directlaunch(const char *GameID);\n\tvoid launchUFlowGame(const char *id, const char *path);'),
            ('void launchUFlowGame(const char *id, const char *path);',
             'void launchUFlowGame(const char *id, const char *path);\n'
             '\tbool m_uflowReturnToWiiU = false;\n\tbool patchUFlowReturn();'),
            ('void launchUFlowGame(const char *id, const char *path);',
             'void launchUFlowGame(const char *id, const char *path, bool gameCube);\n\tbool m_uflowGameCube = false;'),
            ('void launchUFlowGame(const char *id, const char *path, bool gameCube);',
             'void launchUFlowGame(const char *id, const char *path, bool gameCube, bool usbLoader);'),
        ],
        'source/menu/menu_game_boot.cpp': [
            ('\t/* if d2x cios patch returnto and enable emu nand */',
             '\t// uFlow direct launches return through the built-in HCVA system channel.\n'
             '\tif(m_uflowReturnToWiiU)\n\t{\n'
             '\t\tif(!patchUFlowReturn()) return;\n'
             '\t\treturnTo = 0; // do not let the legacy 32-bit booter override HCVA\n'
             '\t}\n\n\t/* if d2x cios patch returnto and enable emu nand */'),
            ('if(disc)\n\t\tloader = NINTENDONT;',
             'if(disc || m_uflowGameCube)\n\t\tloader = NINTENDONT;'),
            ('if(loader == NINTENDONT && !disc)// Check if game has multi Discs',
             'if(loader == NINTENDONT && !disc && !m_uflowGameCube)// uFlow already selected disc 1; Nintendont handles swapping'),
            ('bool use_slippi = (m_cfg.getBool(GC_DOMAIN, "use_slippi", false)',
             'bool use_slippi = (!m_uflowGameCube && m_cfg.getBool(GC_DOMAIN, "use_slippi", false)'),
        ],
    }
    # Idempotent, but never overwrite unrelated source edits.
    staged = {}
    for relative, replacements in changes.items():
        pristine = subprocess.check_output(['git', '-C', str(source), 'show', 'HEAD:' + relative]).decode('utf-8')
        patched = pristine
        accepted = [pristine.replace('\r\n', '\n')]
        for before, after in replacements:
            # Match upstream LF and CRLF checkouts without changing unrelated text.
            if before not in patched and '\n' in before:
                before, after = before.replace('\n', '\r\n'), after.replace('\n', '\r\n')
            if patched.count(before) != 1:
                raise RuntimeError('Upstream anchor differs: ' + before)
            patched = patched.replace(before, after)
            accepted.append(patched.replace('\r\n', '\n'))
        current = (source / relative).read_text(encoding='utf-8')
        if current not in accepted:
            raise RuntimeError('Unrelated changes in ' + relative)
        staged[relative] = patched
    for relative, text in staged.items():
        (source / relative).write_text(text, encoding='utf-8', newline='\n')
    shutil.copyfile(project / 'app/source/wii_request.hpp', source / 'source/wii_request.hpp')
    shutil.copyfile(project / 'wii/wiiflow_entry.cpp', source / 'source/uflow_entry.cpp')
    shutil.copyfile(project / 'wii/wii_return.hpp', source / 'source/wii_return.hpp')
    shutil.copyfile(project / 'wii/wii_return.cpp', source / 'source/wii_return.cpp')
    shutil.copyfile(project / 'wii/gc_return_stub.hpp', source / 'source/gc_return_stub.hpp')
    print('Prepared WiiFlow v5.6.2 exact-path entry point (SD only).')

if __name__ == '__main__':
    main()
