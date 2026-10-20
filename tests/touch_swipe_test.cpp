#include "touch_swipe.hpp"
#include <cassert>
#include <cstdio>
int main(){
 TouchSwipe s;auto frame=[&](bool down,int x,int y){return s.Update(true,true,down,x,y,true,true,100,80);};
 frame(true,500,300);auto e=frame(true,390,306);assert(e.dx==1&&!e.dy&&!e.tap);
 e=frame(true,280,310);assert(e.dx==1);e=frame(false,0,0);assert(!e.dx&&!e.tap);
 frame(true,500,300);e=frame(true,505,210);assert(e.dy==1&&!e.dx);e=frame(true,300,130);assert(e.dy==1&&!e.dx);frame(false,0,0);
 frame(true,500,300);frame(true,455,301);e=frame(false,0,0);assert(e.dx==1&&!e.tap);
 frame(true,500,300);frame(true,510,310);e=frame(false,0,0);assert(e.tap&&e.x==500);
 frame(true,500,300);s.Update(false,false,false,0,0,true,true,100,80);e=frame(true,200,300);assert(!e.dx&&!e.tap);frame(false,0,0);
 frame(true,500,300);s.Cancel();e=frame(false,0,0);assert(!e.tap&&!e.dx);
 s.Update(true,true,true,500,550,false,false,100,80);e=s.Update(true,true,true,390,550,false,false,100,80);assert(!e.dx);e=frame(false,0,0);assert(!e.tap);
 puts("PASS swipe axes, continuous steps, short flick, tap isolation, invalid samples, cancellation and dock drag");
}
