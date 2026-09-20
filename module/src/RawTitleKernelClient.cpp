// SPDX-License-Identifier: GPL-3.0-or-later
#include "RawTitleKernel.h"
#include <coreinit/dynload.h>
#include <cstdio>

namespace {
OSDynLoad_Module kernel = nullptr;
bool (*arm)(const RawTitleProfile &) = nullptr;
void (*clear)() = nullptr;
const char *(*error)() = nullptr;
const char *(*probe)() = nullptr;
bool Resolve() {
    if (arm && clear && error) return true;
    if (!kernel && OSDynLoad_Acquire("uflow_raw_kernel", &kernel) != OS_DYNLOAD_OK) return false;
    return OSDynLoad_FindExport(kernel, OS_DYNLOAD_EXPORT_FUNC, "RawTitleKernel_Arm", reinterpret_cast<void **>(&arm)) == OS_DYNLOAD_OK &&
           OSDynLoad_FindExport(kernel, OS_DYNLOAD_EXPORT_FUNC, "RawTitleKernel_Clear", reinterpret_cast<void **>(&clear)) == OS_DYNLOAD_OK &&
           OSDynLoad_FindExport(kernel, OS_DYNLOAD_EXPORT_FUNC, "RawTitleKernel_GetInitializeError", reinterpret_cast<void **>(&error)) == OS_DYNLOAD_OK;
}
}
bool RawTitleKernel_Initialize() { return true; }
bool RawTitleKernel_Arm(const RawTitleProfile &profile) {
    if (!Resolve()) return false;
    const bool result = arm(profile);
    // Keep filesystem I/O in the regular module, whose WUT devoptab is ready.
    if (!probe) OSDynLoad_FindExport(kernel, OS_DYNLOAD_EXPORT_FUNC,
                                  "RawTitleKernel_GetProbe", reinterpret_cast<void **>(&probe));
    if (probe) {
        if (FILE *log = fopen("fs:/vol/external01/wiiu/raw-launcher.log", "a")) {
            fprintf(log,"%s arm=%d\n",probe(),result);
            fclose(log);
        }
    }
    return result;
}
const char *RawTitleKernel_GetProbe() { return probe ? probe() : "kernel probe unavailable"; }
void RawTitleKernel_Clear() { if (clear) clear(); }
const char *RawTitleKernel_GetInitializeError() {
    return error ? error() : "uFlowRawKernel.wms is missing";
}
