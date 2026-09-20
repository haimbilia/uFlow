// SPDX-License-Identifier: GPL-3.0-or-later
#include "RawTitleKernel.h"
#include <wums.h>

WUMS_MODULE_EXPORT_NAME("uflow_raw_kernel");
WUMS_MODULE_SKIP_INIT_FINI();
// WUMSLoader puts early modules in its persistent 0x00800000..0x01000000 heap.
// No hook is installed at boot; only an explicit raw-game launch arms it.
WUMS_MODULE_INIT_BEFORE_RELOCATION_DONE_HOOK();
WUMS_INITIALIZE() {}
WUMS_EXPORT_FUNCTION(RawTitleKernel_Arm);
WUMS_EXPORT_FUNCTION(RawTitleKernel_Clear);
WUMS_EXPORT_FUNCTION(RawTitleKernel_GetInitializeError);
WUMS_EXPORT_FUNCTION(RawTitleKernel_GetProbe);
