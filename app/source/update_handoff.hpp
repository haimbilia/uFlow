#pragma once
#include <stdexcept>
namespace UFlowUpdate {
// A bare RPX inherits the old WUHB content mount on Aroma. Detach it before
// recovery as well as commit: either operation may need to rename the bundle.
template<class Detach,class Install>
void InstallDetached(Detach detach,Install install){
    if(!detach())throw std::runtime_error("Cannot release previous uFlow content mount; installation not started");
    install();
}
}
