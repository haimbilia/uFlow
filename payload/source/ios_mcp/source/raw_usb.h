#pragma once
#include "../../common/raw_usb_map.h"
int RawUsbInstall(const void *data, unsigned size);
void RawUsbClear(void);
int RawUsbHasFile(const char *path);
int RawUsbRead(const char *path, void *output, unsigned length, unsigned position);
