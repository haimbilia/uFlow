#pragma once

#include "RawTitleProfile.h"

bool RawTitleKernel_Initialize();
const char *RawTitleKernel_GetInitializeError();
const char *RawTitleKernel_GetProbe();
bool RawTitleKernel_Arm(const RawTitleProfile &profile);
void RawTitleKernel_Clear();
