    Controllers::Manager controllers_;
    bool controllersOpen_=false,controllersFocus_=false,controllersConfirm_=false,controllersYes_=false;
    int controllerRow_=0,controllerSelected_=0;
    bool controllerTest_=false;
#ifdef UFLOW_HOST_PREVIEW
    const Controllers::Snapshot* controllersPreview_=nullptr;
#endif
    Controllers::Snapshot ControllerState(){
#ifdef UFLOW_HOST_PREVIEW
        if(controllersPreview_)return *controllersPreview_;
#endif
        return controllers_.Poll();
    }
    void CloseControllers(){controllers_.Close();controllersOpen_=controllersConfirm_=controllerTest_=false;controllersFocus_=true;settingsAction_=-1;}
    void OpenControllers(){
        if(ConfirmDraftExit(DraftExit::Controllers))return;
        supportOpen_=supportFocus_=false;
        controllersOpen_=true;controllersFocus_=controllersConfirm_=controllersYes_=false;
        settingsAction_=-1;controllerRow_=0;controllerSelected_=0;controllerTest_=false;
#ifdef UFLOW_HOST_PREVIEW
        if(controllersPreview_)return;
#endif
        controllers_.Open();
    }
    void ControllerInput(int horizontal,int vertical,bool accept,bool back){
        const auto state=ControllerState();
        if(controllerTest_){if(back)controllerTest_=false;return;}
        if(controllersConfirm_){
            if(back){controllersConfirm_=false;return;}
            if(horizontal)controllersYes_=!controllersYes_;
            if(accept){if(controllersYes_)controllers_.Submit(Controllers::Command::ClearAll);controllersConfirm_=false;}return;
        }
        if(back){CloseControllers();return;}
        if(vertical)controllerRow_=std::clamp(controllerRow_+vertical,0,3);
        if(horizontal&&controllerRow_==0&&!state.devices.empty())controllerSelected_=(controllerSelected_+horizontal+int(state.devices.size()))%int(state.devices.size());
        if(!accept||state.busy||!state.ready)return;
        if(controllerRow_==0&&!state.devices.empty())controllerTest_=true;
        else if(controllerRow_==1)controllers_.Submit(!state.active?Controllers::Command::Install:state.pairing?Controllers::Command::Cancel:Controllers::Command::Pair);
        else if(controllerRow_==2&&controllerSelected_<int(state.devices.size())&&state.devices[controllerSelected_].channel>=0)controllers_.Submit(Controllers::Command::Disconnect,state.devices[controllerSelected_].address);
        else if(controllerRow_==3){controllersConfirm_=true;controllersYes_=false;}

    }
    void ControllerTouch(int x,int y,int drag){
        if(std::abs(drag)>=30)return;
        if(controllerTest_){controllerTest_=false;return;}
        if(controllersConfirm_){if(y>=445&&y<493&&x>=390&&x<1125){controllersYes_=x>=760;ControllerInput(0,0,true,false);}return;}
        const auto state=ControllerState();
        if(y>=258&&y<362&&x>=390&&x<1122){const int index=(controllerSelected_/4)*4+(x-390)/183;if(index<int(state.devices.size())){if(index==controllerSelected_&&controllerRow_==0)controllerTest_=true;controllerSelected_=index;controllerRow_=0;}return;}
        if(y>=366&&y<395){controllerRow_=0;if(x>=390&&x<500)ControllerInput(-1,0,false,false);else if(x>=1000&&x<1125)ControllerInput(1,0,false,false);return;}
        if(x>=390&&x<1125&&y>=400&&y<520){controllerRow_=1+(y-400)/40;ControllerInput(0,0,true,false);}
    }
    void DrawControllerGlyph(int x,int y,int size,Color color){
#ifdef UFLOW_HOST_PREVIEW
        const auto icon=textures_.Get("/project/app/content/controllers/gamepad.png");
#else
        const auto icon=textures_.Get("fs:/vol/content/controllers/gamepad.png");
#endif
        if(!icon.texture)return;
        SDL_SetTextureColorMod(icon.texture,color.r,color.g,color.b);SDL_SetTextureAlphaMod(icon.texture,color.a);
        const int height=size*3/4;SDL_Rect rect{x-size/2,y-height/2,size,height};
        SDL_RenderCopy(renderer_,icon.texture,nullptr,&rect);
        SDL_SetTextureColorMod(icon.texture,255,255,255);SDL_SetTextureAlphaMod(icon.texture,255);
    }
    void DrawControllers(int x,Color accent,bool light){
        const auto state=ControllerState();
        const Color ink=light?UIShade(Color{38,53,64,255}):UIShade(Color{238,244,252,255});
        const Color muted=light?UIShade(Color{64,83,96,255}):UIShade(Color{160,180,198,255});
        DrawText(renderer_,"CONTROLLERS",x,195,3,ink);
        DrawFittedText(renderer_,state.active?"Bloopair "+state.version:"Bloopair support is not active",x,233,730,1,muted);
        if(controllerTest_){
            DrawText(renderer_,"INPUT TEST",x,285,2,ink);
            if(controllerSelected_>=int(state.devices.size())||!state.devices[controllerSelected_].reportValid)DrawText(renderer_,"Connect a Bloopair controller to test its input.",x,335,1,muted);
            else {const auto& d=state.devices[controllerSelected_];char buttons[32];snprintf(buttons,sizeof(buttons),"BUTTONS  %08lX",static_cast<unsigned long>(d.buttons));
                DrawFittedText(renderer_,d.name,x,325,730,2,ink);DrawText(renderer_,buttons,x,369,2,accent);
                DrawText(renderer_,"LEFT STICK   "+std::to_string(d.leftX)+" / "+std::to_string(d.leftY),x,412,2,ink);
                DrawText(renderer_,"RIGHT STICK  "+std::to_string(d.rightX)+" / "+std::to_string(d.rightY),x,451,2,ink);
            }
            DrawText(renderer_,"Hold buttons to see their raw input. B / tap to return.",x,515,1,muted);
        }else if(controllersConfirm_){
            DrawText(renderer_,"UNPAIR ALL CONTROLLERS?",x,285,2,ink);
            DrawText(renderer_,"This removes saved Wii Remote, Pro and Bloopair pairings.",x,333,1,muted);
            DrawText(renderer_,"You will need to pair those controllers again.",x,360,1,muted);
            DrawText(renderer_,"The Wii U GamePad is not affected.",x,387,1,muted);
            for(int i=0;i<2;++i){FillRoundedRect(renderer_,{x+i*370,445,360,48},8,controllersYes_==(i==1)?Color{accent.r,accent.g,accent.b,100}:Color{80,100,120,35});DrawText(renderer_,i?"UNPAIR ALL":"CANCEL",x+18+i*370,460,2,ink);}
        }else{
            controllerSelected_=std::clamp(controllerSelected_,0,std::max(0,int(state.devices.size())-1));
            const int page=controllerSelected_/4;
            for(int slot=0;slot<4;++slot){const int index=page*4+slot,left=x+slot*183;const bool selected=index==controllerSelected_&&index<int(state.devices.size());
                FillRoundedRect(renderer_,{left,258,173,104},8,selected?Color{accent.r,accent.g,accent.b,85}:Color{110,130,145,25});
                if(index>=int(state.devices.size())){DrawText(renderer_,"--",left+76,297,2,muted);continue;}
                const auto& device=state.devices[index];DrawControllerGlyph(left+86,283,32,device.channel>=0?accent:muted);
                DrawFittedText(renderer_,device.name,left+8,307,157,1,ink);
                DrawText(renderer_,device.channel>=0?"Player "+std::to_string(device.channel+1):"Saved pairing",left+8,330,1,muted);
                if(device.channel>=0)for(int bar=0;bar<4;++bar)FillRoundedRect(renderer_,{left+110+bar*12,335,9,12},1,bar<device.battery?accent:Color{110,130,145,80});

            }
            DrawFittedText(renderer_,state.devices.empty()?"No controllers found. Pair a controller below.":"<  Select controller  >   "+std::to_string(controllerSelected_+1)+" / "+std::to_string(state.devices.size())+"    A: input test",x,373,730,1,muted);
            const std::string labels[]={!state.active?"Install controller support":state.pairing?"Cancel pairing":"Pair new controller","Disconnect selected","Unpair all controllers"};
            for(int i=0;i<3;++i){const bool enabled=!state.busy&&(i!=1||(!state.devices.empty()&&state.devices[controllerSelected_].channel>=0));
                if(controllerRow_==i+1&&settingsAction_<0)FillRoundedRect(renderer_,{x-8,400+i*40,746,37},8,{accent.r,accent.g,accent.b,65});
                DrawText(renderer_,labels[i],x+8,409+i*40,2,enabled?ink:muted);
            }
        }
        DrawSettingsActions(x,accent);
        // The pairing guide and status share the existing footer, never another window.
        DrawFittedText(renderer_,state.pairing?"Put your controller in pairing mode.":state.message,x,539,730,1,muted);
    }
