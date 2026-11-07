#pragma once
#include <cstdint>
namespace NumberRepeat {
// One step per pulse: retain fine control, then repeat at 10 Hz.
struct Hold {
    int direction=0,context=0;uint32_t next=0;
    void Reset(){direction=0;}
    int Update(int value,int field,uint32_t now){
        if(!value){Reset();return 0;}
        if(value!=direction||field!=context){direction=value;context=field;next=now+360;return value;}
        if(static_cast<int32_t>(now-next)<0)return 0;
        next=now+100;return value;
    }
};
}
