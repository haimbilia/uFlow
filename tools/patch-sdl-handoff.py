"""uFlow diagnostic checkpoints in the Wii U SDL foreground handoff.

The optional app hook only stores atomics; callbacks must not do filesystem I/O.
Does not alter ProcUI ordering or callback ownership.
"""
from pathlib import Path
import sys

p = Path(sys.argv[1]) / 'src/video/wiiu/SDL_wiiuvideo.c'
s = p.read_text()
if 'UFLOW_HANDOFF_CHECKPOINTS' in s:
    raise SystemExit('handoff patch already applied')
marker = 'static int WIIU_ForegroundAcquired(_THIS)'
assert s.count(marker) == 1
s = s.replace(marker, '''/* UFLOW_HANDOFF_CHECKPOINTS: altered uFlow diagnostic build. */
extern void UFlow_HandoffStage(unsigned stage) __attribute__((weak));
#define UFLOW_STAGE(id) do { if (UFlow_HandoffStage) UFlow_HandoffStage(id); } while (0)
''' + marker)
start = s.index('static int WIIU_ForegroundReleased(_THIS)')
end = s.index('static uint32_t WiiU_SaveCallback', start)
release = s[start:end]
release = release.replace('// make sure the GPU is done drawing\n\tGX2DrawDone();',
    '// make sure the GPU is done drawing\n\tUFLOW_STAGE(5);\n\tUFLOW_STAGE(6);\n\tif (GX2DrawDone()) UFLOW_STAGE(7); else UFLOW_STAGE(8);')
release = release.replace('\tWIIU_GfxHeap_MEM1Destroy();', '\tUFLOW_STAGE(9);\n\tUFLOW_STAGE(10);\n\tWIIU_GfxHeap_MEM1Destroy();\n\tUFLOW_STAGE(11);')
release = release.replace('\tWIIU_GfxHeap_ForegroundDestroy();', '\tWIIU_GfxHeap_ForegroundDestroy();\n\tUFLOW_STAGE(12);')
release = release.replace('\tGX2SetDRCEnable(TRUE);', '\tGX2SetDRCEnable(TRUE);\n\tUFLOW_STAGE(13);')
release = release.replace('\tvideodata->hasForeground = SDL_FALSE;', '\tvideodata->hasForeground = SDL_FALSE;\n\tUFLOW_STAGE(14);')
s = s[:start] + release + s[end:]
start = s.index(marker)
end = s.index('static int WIIU_ForegroundReleased', start)
acquire = s[start:end].replace('\tvideodata =', '\tUFLOW_STAGE(15);\n\tvideodata =', 1)
acquire = acquire.replace('\treturn 0;\n}', '\tUFLOW_STAGE(16);\n\treturn 0;\n}')
s = s[:start] + acquire + s[end:]
s = s.replace('\tOSSavesDone_ReadyToRelease();', '\tUFLOW_STAGE(17);\n\tOSSavesDone_ReadyToRelease();\n\tUFLOW_STAGE(18);')
start = s.index('static void WIIU_PumpEvents(_THIS)')
end = s.index('static void WIIU_DeleteDevice', start)
pump = s[start:end]
pump = pump.replace('\tif (videodata->handleProcUI) {', '\tSDL_bool traceHandoff = videodata->enteringBackground;\n\tif (videodata->handleProcUI) {', 1)
pump = pump.replace('\t\t\tProcUIDrawDoneRelease();', '\t\t\tUFLOW_STAGE(1);\n\t\t\tProcUIDrawDoneRelease();\n\t\t\tUFLOW_STAGE(2);')
pump = pump.replace('\t\t\tProcUIStatus status = ProcUIProcessMessages(TRUE);', '\t\t\tif (traceHandoff) UFLOW_STAGE(3);\n\t\t\tProcUIStatus status = ProcUIProcessMessages(TRUE);\n\t\t\tif (traceHandoff) UFLOW_STAGE(4);')
s = s[:start] + pump + s[end:]
p.write_text(s)
print('Patched SDL foreground handoff checkpoints')
