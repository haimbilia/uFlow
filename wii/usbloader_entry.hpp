#pragma once
#include "wii_request.hpp"
#ifdef UFLOW_HOST_TEST
#include "usbloader_mocks.hpp"
#else
#include "usbloader/disc.h"
#endif
extern WiiRequest::Request gUFlowRequest;
extern bool gUFlowActive;
int UFlowUsbLaunch();
inline bool UFlowMatches(const void *id) {
    return gUFlowActive && id && std::memcmp(id,gUFlowRequest.id.data(),6)==0;
}
