#pragma once
#include <algorithm>
#include <cmath>
struct TouchSwipe {
    struct Event {int dx=0,dy=0,x=0,y=0;bool tap=false,consumed=false;};
    bool active=false,blocked=false,moved=false;int axis=0,startX=0,startY=0,lastX=0,lastY=0,emitted=0;
    bool horizontal=true,vertical=false;float pitchX=100,pitchY=100;
    void Cancel(bool force=false){blocked=blocked||active||force;active=false;}
    Event Update(bool sample,bool valid,bool down,int x,int y,bool allowX,bool allowY,float stepX,float stepY){
        Event out;out.consumed=active;
        if(!sample||(down&&!valid)){Cancel();return out;}
        if(blocked){if(!down)blocked=false;out.consumed=down;return out;}
        if(down&&!active){active=true;moved=false;axis=0;emitted=0;startX=lastX=x;startY=lastY=y;
            horizontal=allowX;vertical=allowY;pitchX=std::max(40.f,stepX);pitchY=std::max(40.f,stepY);out.consumed=true;return out;}
        if(!active)return out;
        out.consumed=true;
        if(down){lastX=x;lastY=y;}
        const int dx=lastX-startX,dy=lastY-startY;
        if(std::max(std::abs(dx),std::abs(dy))>=22)moved=true;
        if(!axis&&moved){if(std::abs(dx)>=std::abs(dy)){if(horizontal)axis=1;}else if(vertical)axis=2;}
        if(axis){const int distance=axis==1?dx:dy;const float pitch=axis==1?pitchX:pitchY;
            int target=int(-distance/pitch);
            if(!down&&emitted==0&&std::abs(distance)>=35)target=distance<0?1:-1;
            const int delta=std::clamp(target-emitted,-8,8);emitted+=delta;
            if(axis==1)out.dx=delta;else out.dy=delta;
        }
        if(!down){out.tap=!moved;out.x=startX;out.y=startY;active=false;}
        return out;
    }
};
