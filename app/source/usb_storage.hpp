#pragma once
namespace USBStorage {
bool Mount();
void Unmount();
#ifdef UFLOW_USB_LOADER
bool PrepareCode(const char *directory);
bool MountRuntime();
#endif
}
