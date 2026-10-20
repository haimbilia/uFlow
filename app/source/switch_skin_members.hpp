// Dashboard rendering helpers; included inside Dashboard to share its UI state.
    std::vector<Skin::Definition> skins_;
    void LoadSkins() {
#ifdef UFLOW_HOST_PREVIEW
        const std::string root="/project/app/content/skins";
#else
        const std::string root="fs:/vol/content/skins";
#endif
        skins_=Skin::Discover(root,"fs:/vol/external01/wiiu/apps/uFlow/skins");
        for(const auto &skin:skins_)if(skin.id==preferences_.skinId){preferences_.skin=skin.engine;break;}
    }
    const Skin::Definition &SkinSpec() const {
        for(const auto &skin:skins_)if(skin.id==preferences_.skinId&&skin.engine==preferences_.skin)return skin;
        for(const auto &skin:skins_)if(skin.engine==preferences_.skin)return skin;
        return skins_.front();
    }
    void ChangeSkin(int direction) {
        const auto current=SkinSpec().id;
        int index=0;for(size_t i=0;i<skins_.size();++i)if(skins_[i].id==current)index=int(i);
        const auto &skin=skins_[(index+direction%int(skins_.size())+int(skins_.size()))%skins_.size()];
        preferences_.skinId=skin.id;preferences_.skin=skin.engine;
        if(SwitchSkin())preferences_.coverType=CoverPresentation::Thumbnails;
    }
    bool SwitchSkin() const {return preferences_.skin>=2;}
    static Color SkinColor(const std::array<unsigned char,4> &c){return {c[0],c[1],c[2],c[3]};}
    Color SwitchBackground() const {return SkinColor(SkinSpec().background);}
    Color SwitchInk() const {return SkinColor(SkinSpec().ink);}
    Color SwitchMuted() const {return SkinColor(SkinSpec().muted);}
    Color SwitchAccent() const {return SkinColor(SkinSpec().accent);}
    void LoadProfile() {
#ifndef UFLOW_HOST_PREVIEW
        nn::act::Initialize();
        profileId_=nn::act::GetPersistentId();profileSlot_=nn::act::GetSlotNo();
        int16_t name[nn::act::MiiNameSize]{};
        if(nn::act::GetMiiName(name).IsSuccess()) {
            profileName_.clear();
            for(unsigned i=0;i<nn::act::MiiNameSize && name[i];++i) {
                unsigned c=static_cast<uint16_t>(name[i]);
                if(c<128)profileName_+=char(c);
                else if(c<2048){profileName_+=char(0xc0|(c>>6));profileName_+=char(0x80|(c&63));}
                else {profileName_+=char(0xe0|(c>>12));profileName_+=char(0x80|((c>>6)&63));profileName_+=char(0x80|(c&63));}
            }
        }
        std::vector<unsigned char> data(65554);size_t size=0;
        if(nn::act::GetMiiImageEx(&size,data.data(),data.size(),0,nn::act::GetSlotNo()).IsSuccess() && size>=128*128*4) {
            auto *surface=SDL_CreateRGBSurfaceWithFormat(0,128,128,32,SDL_PIXELFORMAT_RGBA32);
            if(surface) {
                for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                    const auto *src=&data[((127-y)*128+x)*4];
                    const float radius=std::sqrt((x-63.5f)*(x-63.5f)+(y-63.5f)*(y-63.5f));
                    const auto alpha=Uint8(src[0]*std::clamp(64.f-radius,0.f,1.f));
                    auto *pixel=reinterpret_cast<Uint32*>(static_cast<Uint8*>(surface->pixels)+y*surface->pitch)+x;
                    *pixel=SDL_MapRGBA(surface->format,src[1],src[2],src[3],alpha);
                }
                profileTexture_=SDL_CreateTextureFromSurface(renderer_,surface);SDL_FreeSurface(surface);
                if(profileTexture_){SDL_SetTextureBlendMode(profileTexture_,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(profileTexture_,SDL_ScaleModeLinear);}
            }
        }
        nn::act::Finalize();
        if(nn::pdm::Initialize()==0){
            uint32_t count=0;
            if(nn::pdm::GetPlayStatsLength(&count,profileSlot_)==0&&count<=8192) {
                std::vector<nn::pdm::PlayStats> records(count);uint32_t received=0;
                if(count&&nn::pdm::GetPlayStats(&received,records.data(),profileSlot_,count)==0)
                    for(size_t i=0;i<std::min<size_t>(received,records.size());++i)nativeStats_[uint64_t(records[i].title_id)]={uint32_t(records[i].playtime),uint32_t(records[i].times_played)};
            }
            nn::pdm::Finalize();
        }
#endif
    }
    UserStatistics::Store userStats_;
    uint32_t profileId_=0,profileSlot_=1;
    std::map<uint64_t,std::pair<uint32_t,uint32_t>> nativeStats_;
    std::string StatisticsText(const GameEntry &game) const {
        const auto own=userStats_.Get(game);
        std::string time="Not reported for this launcher",plays=std::to_string(own.launches)+" through uFlow";
        if(game.platform==Platform::WiiU && game.titleId.size()==16) {
            try {auto it=nativeStats_.find(std::stoull(game.titleId,nullptr,16));if(it!=nativeStats_.end()){
                time=std::to_string(it->second.first/60)+"h "+std::to_string(it->second.first%60)+"m";
                plays=std::to_string(it->second.second)+" (Wii U activity log)";
            }}catch(...){}
        }
        return "Time played: "+time+"\nTimes played: "+plays+"\nYour rating: "+(own.rating?std::to_string(own.rating)+" / 5":"Not rated")+"\nA: change your rating (0-5)\n";
    }
    void DrawProfile(int x,int y,int size,bool label) {
        const Color ink=SwitchSkin()?SwitchInk():preferences_.skin==1?Color{48,61,71,255}:Color{237,244,255,255};
        FillSmoothDot(renderer_,x+size*.5f,y+size*.5f,size*.5f+3,{255,255,255,230});
        FillSmoothDot(renderer_,x+size*.5f,y+size*.5f,size*.5f,{66,103,132,255});
        if(profileTexture_){SDL_Rect rect{x,y,size,size};SDL_RenderCopy(renderer_,profileTexture_,nullptr,&rect);}
        else {
            FillSmoothDot(renderer_,x+size*.5f,y+size*.37f,size*.17f,{239,244,249,255});
            FillEllipse(renderer_,{x+size*.23f,y+size*.57f,size*.54f,size*.30f},{239,244,249,255});
        }
        if(label)DrawFittedText(renderer_,profileName_,x+size+20,y+size/2-10,210,2,ink);
    }
    void DrawWallChromeShade(bool top) {
        if(!WallLayout()||!SkinSpec().gridFullscreen)return;
        const auto c=SwitchBackground();const float y0=top?0.f:520.f,y1=top?200.f:720.f;
        const SDL_Color a{c.r,c.g,c.b,Uint8(top?225:0)},b{c.r,c.g,c.b,Uint8(top?0:225)};
        const SDL_Vertex vertices[]={{{0,y0},a,{0,0}},{{1280,y0},a,{0,0}},{{1280,y1},b,{0,0}},{{0,y1},b,{0,0}}};
        const int indices[]={0,1,2,0,2,3};SDL_RenderGeometry(renderer_,nullptr,vertices,4,indices,6);
    }
    void DrawSwitchHeader() {
        DrawWallChromeShade(true);
        const auto &skin=SkinSpec();DrawProfile(skin.avatarX,skin.avatarY,skin.avatarSize,true);
        char clock[16]="12:00";
#ifndef UFLOW_HOST_PREVIEW
        const auto now=std::time(nullptr);if(const auto *t=std::localtime(&now))std::strftime(clock,sizeof(clock),"%H:%M",t);
#endif
        DrawText(renderer_,clock,1030,50,3,SwitchInk(),true);
        DrawFittedText(renderer_,LibraryLabel(CurrentLibraryId())+"   "+LibraryPosition(),610,58,300,2,SwitchInk());
        DrawSystemIndicators();
        if(Selected())DrawFittedText(renderer_,Selected()->name,100,145,1080,3,SwitchAccent());
        if(!searchQuery_.empty() || alphabetFilter_)DrawFittedText(renderer_,"FILTER: "+searchQuery_+(alphabetFilter_?"  "+AlphabetLabel(alphabetFilter_):""),400,105,720,1,SwitchAccent());
    }
    static void DrawArc(SDL_Renderer *renderer,float x,float y,float radius,float start,float end,Color color) {
        SetColor(renderer,color);
        for(int band=-1;band<=1;++band){SDL_FPoint points[65];for(int i=0;i<=64;++i){float a=start+(end-start)*i/64.f;points[i]={x+std::cos(a)*(radius+band),y+std::sin(a)*(radius+band)};}SDL_RenderDrawLinesF(renderer,points,65);}
    }
    void UpdateSystemIndicators(bool available,int battery) {
        if(available){batteryLevel_=std::clamp(battery,0,6);lastPadSeen_=SDL_GetTicks();}
        else if(SDL_GetTicks()-lastPadSeen_>2000)batteryLevel_=-1;
#ifndef UFLOW_HOST_PREVIEW
        if(SDL_TICKS_PASSED(SDL_GetTicks(),networkPoll_)) {
            networkPoll_=SDL_GetTicks()+5000;
            if(!networkInitialized_)networkInitialized_=NNResult_IsSuccess(ACInitialize());
            BOOL connected=false;networkConnected_=networkInitialized_&&NNResult_IsSuccess(ACIsApplicationConnected(&connected))&&connected;
        }
#endif
    }
    void DrawSystemIndicators() {
        const auto ink=SwitchInk();
        for(int band=0;band<3;++band)DrawArc(renderer_,1120,76,float(9+band*8),-2.35f,-.79f,networkConnected_?ink:SwitchMuted());
        FillSmoothDot(renderer_,1120,76,3,networkConnected_?ink:SwitchMuted());
        if(!networkConnected_){SetColor(renderer_,ink);SDL_RenderDrawLine(renderer_,1100,55,1136,81);}
        SetColor(renderer_,ink);SDL_Rect outline{1171,53,48,25};for(int i=0;i<3;++i){SDL_RenderDrawRect(renderer_,&outline);++outline.x;++outline.y;outline.w-=2;outline.h-=2;}
        SDL_Rect nub{1219,60,5,11};SDL_RenderFillRect(renderer_,&nub);
        if(batteryLevel_>0){SDL_Rect level{1177,59,36*batteryLevel_/6,13};SetColor(renderer_,batteryLevel_<=1?Color{237,75,75,255}:ink);SDL_RenderFillRect(renderer_,&level);}
        else DrawText(renderer_,batteryLevel_==0?"+":"?",1195,56,2,ink,true);
    }
    int batteryLevel_=-1;
    bool networkInitialized_=false,networkConnected_=false;
    Uint32 networkPoll_=0,lastPadSeen_=0;
    void DrawDockIcon(int index,int cx,int cy,Color ink) {
        const auto &custom=SkinSpec().icons[index];
        if(!custom.empty()){const auto image=textures_.Get(custom);if(image.texture){SDL_Rect r{cx-24,cy-24,48,48};SDL_RenderCopy(renderer_,image.texture,nullptr,&r);return;}}
        SetColor(renderer_,ink);
        const auto line=[&](int x1,int y1,int x2,int y2){for(int n=-1;n<=1;++n)SDL_RenderDrawLine(renderer_,cx+x1+n,cy+y1,cx+x2+n,cy+y2);};
        const auto rect=[&](int x,int y,int w,int h){for(int n=0;n<3;++n){SDL_Rect r{cx+x+n,cy+y+n,w-2*n,h-2*n};SDL_RenderDrawRect(renderer_,&r);}};
        if(index==0){rect(-19,-18,38,30);line(-9,12,0,20);line(0,20,9,12);line(-12,-7,12,-7);line(-12,1,5,1);}
        if(index==1){DrawArc(renderer_,float(cx),float(cy),21,0,6.283185f,ink);line(-21,0,21,0);line(0,-21,0,21);rect(-9,-17,18,34);}
        if(index==2){rect(-22,-16,39,32);rect(-15,-21,39,32);line(-10,6,0,-7);line(0,-7,10,6);FillSmoothDot(renderer_,cx+13,cy-12,3,ink);}
        if(index==3){rect(-15,-23,30,46);line(-9,-10,-1,-10);line(-5,-14,-5,-6);FillSmoothDot(renderer_,cx+7,cy+8,3,ink);FillSmoothDot(renderer_,cx+1,cy+15,3,ink);}
        if(index==4){DrawArc(renderer_,float(cx),float(cy),14,0,6.283185f,ink);for(int i=0;i<8;++i){float a=i*.785398f;line(int(std::cos(a)*15),int(std::sin(a)*15),int(std::cos(a)*24),int(std::sin(a)*24));}}
        if(index==5){DrawArc(renderer_,float(cx),float(cy+2),20,-1.05f,4.19f,ink);line(0,-24,0,2);}
    }
    void DrawSwitchFooter() {
        DrawWallChromeShade(false);
        const auto &skin=SkinSpec();
        static constexpr const char *labels[]={"NOTIFICATIONS","WEB BROWSER","SCREENSHOTS","CONTROLLERS","SETTINGS","POWER"};
        const Color colors[]={{238,74,85,255},{234,160,20,255},{47,134,229,255},SwitchInk(),SwitchInk(),SwitchInk()};
        for(int i=0;i<6;++i){const int cx=skin.dockX+i*skin.dockGap,cy=skin.dockY;
            if(dockFocus_==i)FillSmoothDot(renderer_,float(cx),float(cy),42,SwitchAccent());
            FillSmoothDot(renderer_,float(cx),float(cy),36,preferences_.skin==2?Color{255,255,255,255}:Color{73,73,73,255});
            DrawDockIcon(i,cx,cy,colors[i]);
            if(dockFocus_==i)DrawText(renderer_,labels[i],cx,cy+49,1,SwitchInk(),true);
        }
        SetColor(renderer_,SwitchMuted());SDL_RenderDrawLine(renderer_,30,646,1250,646);
        DrawFittedText(renderer_,std::string(ActionButton(ControlAction::FocusSystemButtons))+"  SYSTEM BUTTONS",54,675,550,2,SwitchInk());
        DrawFittedText(renderer_,std::string(ActionButton(ControlAction::Settings))+"  OPTIONS     "+ActionButton(ControlAction::Launch)+(dockFocus_>=0?"  SELECT":"  START"),790,675,430,2,SwitchInk(),true);
    }
    void DrawThumbnail(const GameEntry &game,SDL_Rect rect,bool selected,Color accent,float opacity) {
        const int edge=std::min(rect.w,rect.h);rect.x+=(rect.w-edge)/2;rect.y+=(rect.h-edge)/2;rect.w=rect.h=edge;
        if(selected && !launchAnimation_){if(launchCapture_)return;launchOrigin_=rect;launchYaw_=launchPitch_=launchRoll_=0;}
        const auto profile=CaseProfileForGame(game,libraries_);
        const float total=profile.widthMm*2+profile.thicknessMm;
        const auto face=CoverFaces::Resolve(game,CoverFaces::Face::Front,profile.widthMm/total,(profile.widthMm+profile.thicknessMm)/total,[&](const std::string &path){return textures_.GetCover(path,selected).texture!=nullptr;});
        const auto image=textures_.GetCover(face.path,selected);
        SetColor(renderer_,SwitchSkin()?SwitchMuted():Color{39,58,79,255});SDL_RenderFillRect(renderer_,&rect);
        if(image.texture) {
            SDL_Rect src{int(face.u0*image.width),0,std::max(1,int((face.u1-face.u0)*image.width)),image.height};
            const int edge=std::min(src.w,src.h);src.x+=(src.w-edge)/2;src.y+=(src.h-edge)/2;src.w=src.h=edge;
            SDL_SetTextureAlphaMod(image.texture,Uint8(255*opacity));SDL_RenderCopy(renderer_,image.texture,&src,&rect);SDL_SetTextureAlphaMod(image.texture,255);
        } else DrawFittedText(renderer_,game.name,rect.x+12,rect.y+rect.h/2-12,rect.w-24,2,SwitchSkin()?SwitchInk():Color{240,245,255,255},true);
        if(selected) {
            SetColor(renderer_,accent);SDL_Rect outline{rect.x-7,rect.y-7,rect.w+14,rect.h+14};
            for(int i=0;i<4;++i){SDL_RenderDrawRect(renderer_,&outline);++outline.x;++outline.y;outline.w-=2;outline.h-=2;}
        }
        if(game.favorite)DrawFavoriteBadge(rect.x+rect.w-19,rect.y+19);
        DrawInstallBadge(game,rect);
    }
