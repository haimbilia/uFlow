#pragma once
#ifdef UFLOW_USB_LOADER
#include <vector>
#endif
namespace USBStorage {
bool Mount();
void Unmount();
#ifndef UFLOW_USB_LOADER
void LogReadStats(const char *phase);
#endif
#ifdef UFLOW_USB_LOADER
bool PrepareCode(const char *directory);
/* Physical FAT32 extents of content/hif_*.nfs as Nintendo's HAI companion. */
bool BuildHaiCompanion(const char *contentDirectory,std::vector<unsigned char> &companion);
unsigned RawDevice();
bool MountRuntime();
#endif
}
