// Local/USB syslog hooks only. No sockets, discovery or remote commands.
#include "syslogserver.h"

void (*const Syslog_FlushHistoryToOutput)(const SyslogCircularBuffer *, uint32_t) = (void *) (0x05110744 + 1);
bool gSkipSyslogCatchUpForUSBOutput = false;
static bool sHasSkippedFlushOutputForUSB = false;

void _Syslog_FlushHistoryToOutputForUSB(SyslogCircularBuffer *buffer, const uint32_t flags) {
    if (gSkipSyslogCatchUpForUSBOutput && !sHasSkippedFlushOutputForUSB) {
        gActiveSyslogBuffer->readOffset = gActiveSyslogBuffer->writeOffset;
        sHasSkippedFlushOutputForUSB = true;
        return;
    }
    Syslog_FlushHistoryToOutput(buffer, flags);
}

void _Syslog_RouteOutputPatch(char *buffer, uint32_t len, uint32_t flags) {
    void (*const original)(char *, uint32_t, uint32_t) = (void *) (0x051107a0 + 1);
    original(buffer, len, flags);
}
