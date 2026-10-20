// Render native system button glyphs where available, with vector fallbacks.
    void DrawControlButton(uint32_t bit,SDL_Rect r,Color ink) {
        const bool smooth=gSmoothText;gSmoothText=true;
        const bool stick=bit>=StickButton::Left;
        const bool direction=stick||(bit>=0x100&&bit<=0x800);
        const char *label=bit==0x8000?"A":bit==0x4000?"B":bit==0x2000?"X":bit==0x1000?"Y":bit==0x20?"L":bit==0x10?"R":bit==0x80?"ZL":bit==0x40?"ZR":bit==8?"+":bit==4?"-":bit==0x40000?"L3":bit==0x20000?"R3":"L";
        SetColor(renderer_,ink);
        if(direction){
            const int cx=r.x+12,cy=r.y+r.h/2;
            if(stick){
                FillRoundedRect(renderer_,{cx-11,cy-10,22,20},10,ink);
                const Color cut=preferences_.skin==1|| (SwitchSkin()&&SwitchBackground().r>128)?Color{240,246,248,255}:Color{12,23,36,255};
                DrawText(renderer_,bit>=StickButton::RLeft?"R":"L",cx,cy-TTF_FontHeight(gSmoothFonts[1])/2,1,cut,true);
                SDL_Rect base{cx-7,cy+11,14,2};SDL_RenderFillRect(renderer_,&base);
            }else {
                SDL_Rect h{cx-10,cy-4,20,8},v{cx-4,cy-10,8,20};
                SDL_RenderFillRect(renderer_,&h);SDL_RenderFillRect(renderer_,&v);
            }
            const int dx=(bit==0x400||bit==StickButton::Right||bit==StickButton::RRight)?1:(bit==0x800||bit==StickButton::Left||bit==StickButton::RLeft)?-1:0;
            const int dy=(bit==0x100||bit==StickButton::Down||bit==StickButton::RDown)?1:(bit==0x200||bit==StickButton::Up||bit==StickButton::RUp)?-1:0;
            const float ax=r.x+33.f,ay=float(cy);
            const SDL_Color color{ink.r,ink.g,ink.b,ink.a};
            const SDL_Vertex arrow[]={{{ax+dx*7,ay+dy*7},color,{0,0}},{{ax-dx*5-dy*6,ay-dy*5+dx*6},color,{0,0}},{{ax-dx*5+dy*6,ay-dy*5-dx*6},color,{0,0}}};
            SDL_RenderGeometry(renderer_,nullptr,arrow,3,nullptr,0);
        }else {
            const bool round=bit>=0x1000&&bit<=0x8000;
            if(round){for(int i=0;i<32;++i){float a=i*6.2831853f/32,b=(i+1)*6.2831853f/32;SDL_RenderDrawLine(renderer_,r.x+r.w/2+int(10*std::cos(a)),r.y+r.h/2+int(10*std::sin(a)),r.x+r.w/2+int(10*std::cos(b)),r.y+r.h/2+int(10*std::sin(b)));}}
            else SDL_RenderDrawRect(renderer_,&r);
            DrawText(renderer_,label,r.x+r.w/2,r.y+(r.h-TTF_FontHeight(gSmoothFonts[1]))/2,1,ink,true);
        }
        gSmoothText=smooth;
    }
    void DrawControlBindingIcons(int action,SDL_Rect r,Color ink) {
        std::vector<uint32_t> groups;
        if(bindingCapture_==action){if(bindingCaptureState_.candidate)groups.push_back(bindingCaptureState_.candidate);
            else {DrawFittedText(renderer_,bindingCaptureState_.waitingRelease?"RELEASE TO START":"LISTENING...",r.x,r.y,r.w,1,ink,true);return;}}
        else {for(unsigned i=0;i<28;++i)if(preferences_.bindings[action]&(1u<<i))groups.push_back(1u<<i);
            groups.insert(groups.end(),preferences_.combos[action].begin(),preferences_.combos[action].end());}
        if(groups.empty()){DrawFittedText(renderer_,"NONE",r.x,r.y,r.w,1,ink,true);return;}
        int x=r.x;bool firstGroup=true;
        for(auto group:groups){if(!firstGroup){DrawText(renderer_,"/",x,r.y+2,1,ink);x+=18;}firstGroup=false;bool first=true;
            for(unsigned i=0;i<28;++i)if(group&(1u<<i)){
                if(x+54>r.x+r.w){DrawText(renderer_,"...",x,r.y+2,1,ink);return;}
                if(!first){DrawText(renderer_,"+",x,r.y+2,1,ink);x+=16;}first=false;
                DrawControlButton(1u<<i,{x,r.y,42,r.h},ink);x+=48;
            }}
    }
