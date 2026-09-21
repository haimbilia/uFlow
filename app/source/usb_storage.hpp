#pragma once
namespace USBStorage {
bool Mount();
void Unmount();
#ifndef UFLOW_USB_LOADER
void LogReadStats(const char *phase);
#endif
#ifdef UFLOW_USB_LOADER
bool PrepareCode(const char *directory);
bool MountRuntime();
#endif
}
