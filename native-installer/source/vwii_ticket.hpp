#pragma once
#include "vwii_plan.hpp"

namespace Vwii {
// Only used after the bundled FIX94 vWii DWFA WAD passes its SHA-256 pin.
// Its legacy ticket uses 0x49; Compat Installer decrypts unknown indices with
// standard Wii key 0. Keep the compatibility exception limited to this ticket.
inline uint8_t ForwarderCommonKeyIndex(const Bytes &ticket){
    Setup::Need(ticket.size()>=0x2a4&&U32(ticket,0)==0x10001&&
                U64(ticket,0x1dc)==WiiFlow,"Invalid forwarder ticket");
    const auto index=ticket[0x1f1];
    if(index<=2)return index;
    Setup::Need(index==0x49,"Unsupported forwarder common-key index: "+std::to_string(index));
    return 0;
}
}
