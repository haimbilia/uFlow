// uForce: native charcoal/green skin. No external runtime assets required.
    bool UForceSkin() const {return preferences_.skin==4;}
    bool UForceHome() const {
        if(!UForceSkin())return false;
        const int type=ViewType(),layout=ViewLayout(type);
        return type==CoverPresentation::Box?(layout==0||layout==2||layout==6):layout==0;
    }
    // Shared UI colors only: artwork, brand icons and warning colors are unchanged.
    Color UIShade(Color legacy) const {
        if(LayoutSkin()){const int level=(int(legacy.r)+legacy.g+legacy.b)/3;auto color=level<65?SwitchBackground():level<170?SwitchMuted():SwitchInk();color.a=legacy.a;return color;}
        if(!UForceSkin())return legacy;
        const Uint8 alpha=legacy.a;
        if(legacy.b<=legacy.g || legacy.g<legacy.r)return legacy;
        const int level=(int(legacy.r)+legacy.g+legacy.b)/3;
        Color themed=level<60?Color{30,33,29,alpha}:level<120?Color{66,74,61,alpha}:
            level<220?Color{181,191,173,alpha}:SwitchInk();
        themed.a=alpha;return themed;
    }
    void UForceLines(const std::string&text,int x,int y,int width,int scale,int lines,int pitch,Color ink){
        std::istringstream words(text);std::string word,line;int row=0;
        while(words>>word){const auto next=line+(line.empty()?"":" ")+word;
            if(!line.empty()&&TextWidth(next,scale)>width){
                if(row==lines-1){DrawFittedText(renderer_,line+"...",x,y+row*pitch,width,scale,ink);return;}
                DrawFittedText(renderer_,line,x,y+row*pitch,width,scale,ink);++row;line=word;
            }else line=next;
        }
        if(!line.empty())DrawFittedText(renderer_,line,x,y+row*pitch,width,scale,ink);
    }
    void DrawUForceHeader(){
        const auto ink=SwitchInk(),accent=SwitchAccent();
        FillRoundedRect(renderer_,{0,0,1280,78},0,{30,33,30,255});
        FillRoundedRect(renderer_,{40,20,6,36},0,accent);
        DrawText(renderer_,"uForce",60,14,3,ink);
        DrawText(renderer_,"Experience",60,47,1,{179,187,176,255});
        DrawText(renderer_,"LIBRARY",232,31,2,accent);
        FillRoundedRect(renderer_,{377,18,432,42},3,{47,51,47,255});
        DrawFittedText(renderer_,searchQuery_.empty()?"Find a game":searchQuery_,396,30,355,2,{179,187,176,255});
        DrawArc(renderer_,780,35,7,0,6.283185f,ink);SetColor(renderer_,ink);SDL_RenderDrawLine(renderer_,785,41,792,48);
        DrawFittedText(renderer_,LibraryLabel(CurrentLibraryId()),839,30,190,2,ink,true);
        DrawProfile(1186,20,36,false);DrawFittedText(renderer_,profileName_,1045,31,125,1,ink,true);
        SetColor(renderer_,{65,72,61,255});SDL_RenderDrawLine(renderer_,0,77,1280,77);
    }
    void DrawUForceArt(const GameEntry&game,SDL_Rect r,bool hero,bool coverOnly=false){
        const bool landscape=!coverOnly&&!game.backgroundPath.empty();
        const auto path=landscape?game.backgroundPath:game.coverPath;
        const auto image=!hero?textures_.GetShelf(path):landscape?textures_.GetBackground(path):textures_.GetCover(path,true);
        FillRoundedRect(renderer_,r,0,{25,31,24,255});
        if(!image.texture||image.width<=0||image.height<=0){
            DrawFittedText(renderer_,game.name,r.x+20,r.y+r.h/2-12,r.w-40,2,{165,178,156,255},true);return;
        }
        if(landscape){
            const float scale=std::max(float(r.w)/image.width,float(r.h)/image.height);
            SDL_Rect src{int((image.width-r.w/scale)/2),int((image.height-r.h/scale)/2),std::max(1,int(r.w/scale)),std::max(1,int(r.h/scale))};
            SDL_RenderCopy(renderer_,image.texture,&src,&r);
        }else{
            const float scale=std::min(float(r.w)/image.width,float(r.h)/image.height);
            SDL_Rect dst{r.x+(r.w-int(image.width*scale))/2,r.y,r.w,r.h};dst.w=int(image.width*scale);dst.h=int(image.height*scale);dst.y+=(r.h-dst.h)/2;
            SDL_RenderCopy(renderer_,image.texture,nullptr,&dst);
        }
    }
    void DrawUForceHome(){
        const auto ink=SwitchInk(),accent=SwitchAccent();const auto *game=Selected();
        FillRoundedRect(renderer_,{0,78,1280,596},0,SwitchBackground());
        if(!game){DrawText(renderer_,"Your library starts here",60,180,4,ink);DrawText(renderer_,"Add a game directory in Settings > Library.",60,244,2,{182,191,177,255});return;}

        DrawUForceArt(*game,{465,100,775,320},true);
        if(videoOptions_.mode==VideoMode::Background&&video_.Ready()){
            SDL_Rect old;SDL_RenderGetViewport(renderer_,&old);SDL_Rect frame{465,100,775,320};
            SDL_RenderSetViewport(renderer_,&frame);video_.Draw(frame.w,frame.h);SDL_RenderSetViewport(renderer_,&old);
        }
        FillRoundedRect(renderer_,{40,100,425,320},0,{21,25,20,255});
        const SDL_Vertex fade[]={{{465,100},{21,25,20,255},{0,0}},{{760,100},{21,25,20,0},{0,0}},{{760,420},{21,25,20,0},{0,0}},{{465,420},{21,25,20,255},{0,0}}};
        const int indices[]={0,1,2,0,2,3};SDL_RenderGeometry(renderer_,nullptr,fade,4,indices,6);
        FillRoundedRect(renderer_,{40,100,4,320},0,accent);
        DrawFittedText(renderer_,game->publisher.empty()?LibraryLabel(game->libraryId):game->publisher,66,124,410,1,{175,191,163,255});
        UForceLines(game->name,66,155,454,4,2,43,ink);
        DrawFittedText(renderer_,PlatformName(game->platform)+(game->releaseYear.empty()?std::string():"  /  "+game->releaseYear)+(game->genre.empty()?std::string():"  /  "+game->genre),66,252,455,1,{185,195,177,255});
        UForceLines(game->description.empty()?"Select a game from your library to play.":game->description,66,281,453,1,2,23,{195,203,189,255});
        FillRoundedRect(renderer_,{66,350,146,44},2,accent);
        DrawText(renderer_,std::string(ActionButton(ControlAction::Launch))+"  PLAY",139,362,2,{14,24,8,255},true);
        DrawText(renderer_,std::string(ActionButton(ControlAction::Details))+"  DETAILS",238,363,2,ink);
        DrawText(renderer_,"MY LIBRARY",40,447,2,ink);
        DrawFittedText(renderer_,LibraryLabel(CurrentLibraryId())+"  /  "+LibraryPosition(),898,450,340,1,{165,178,157,255},true);
        const int first=int(selected_)/5*5;
        for(int col=0;col<5&&first+col<int(visible_.size());++col){
            const int n=first+col;const auto&entry=games_[visible_[n]];SDL_Rect r{40+col*243,488,228,116};
            const bool thumbnail=ViewType()==CoverPresentation::Thumbnails;
            const bool focused=n==int(selected_);
            if(thumbnail){
                DrawUForceArt(entry,r,false);
                FillRoundedRect(renderer_,{r.x,604,228,32},0,{38,42,36,255});
                DrawFittedText(renderer_,entry.name,r.x+10,613,208,1,ink);
                if(focused){SetColor(renderer_,accent);SDL_Rect border{r.x-3,r.y-3,234,154};
                    for(int j=0;j<3;++j){SDL_RenderDrawRect(renderer_,&border);++border.x;++border.y;border.w-=2;border.h-=2;}}
            } else {
                // Bare models/logos; scale distinguishes the focus without a card frame.
                r.h=148;
                const int inset=focused?0:12;
                SDL_Rect art{r.x+inset,r.y+inset,r.w-2*inset,r.h-2*inset};
                if(ViewType()==CoverPresentation::Box && ViewLayout(CoverPresentation::Box)!=2)
                    DrawBox(entry,art,0.f,focused,accent,.22f,-.04f,0.f,true);
                else if(ViewType()==CoverPresentation::Box)DrawCover(entry,art,focused,accent);
                else DrawPresentation(entry,art,focused,accent,1.f,0.f,.18f,-.04f,0.f);
            }
            if(focused&&!launchAnimation_&&!launchCapture_&&thumbnail){launchOrigin_={r.x,r.y,228,148};launchYaw_=launchPitch_=launchRoll_=0;}

        }
        const int pages=(int(visible_.size())+4)/5;const int active=first/5;
        const int dots=std::min(9,pages),start=std::clamp(active-4,0,std::max(0,pages-dots));
        for(int i=0;i<dots;++i)FillRoundedRect(renderer_,{640-dots*11+i*22,651,14,3},1,start+i==active?accent:SwitchMuted());
    }
    void DrawUForceFooter(){
        FillRoundedRect(renderer_,{0,674,1280,46},0,{29,32,28,255});
        SetColor(renderer_,{66,74,59,255});SDL_RenderDrawLine(renderer_,40,674,1240,674);
        for(int i=0;i<7;++i){const auto r=GlassFooterRect(i);if(dockFocus_==i)FillRoundedRect(renderer_,r,2,SwitchAccent());
            const Color ink=dockFocus_==i?Color{15,25,8,255}:SwitchInk();
            if(i==6){DrawArc(renderer_,r.x+r.w/2,r.y+22,9,-1.05f,4.19f,ink);SetColor(renderer_,ink);SDL_RenderDrawLine(renderer_,r.x+r.w/2,r.y+10,r.x+r.w/2,r.y+23);}
            else{const auto&hint=footerHints_[i];DrawFittedText(renderer_,hint.button+" "+hint.action,r.x+8,r.y+12,r.w-16,1,ink);}
        }
    }
    bool UForceTouch(int x,int y){
        if(y>=18&&y<61&&x>=377&&x<809){FindOrClearSearch();return true;}
        if(y>=18&&y<61&&x>=839&&x<1030){ChangeTab(1);return true;}
        if(!UForceHome())return false;
        if(y>=350&&y<394&&x>=66&&x<212){if(Selected())detailsLaunchRequested_=true;return true;}
        if(y>=350&&y<394&&x>=230&&x<410){ToggleDetails();return true;}
        if(y>=488&&y<636&&x>=40&&x<1240){int col=(x-40)/243;if((x-40)%243>=228)return true;
            const int target=int(selected_)/5*5+col;if(target>=int(visible_.size()))return true;
            if(target==int(selected_))detailsLaunchRequested_=true;else Move(target-int(selected_));return true;}
        return false;
    }
#ifdef UFLOW_HOST_PREVIEW
    bool CheckUForceComplete(const std::string &output){
        VisualPreview("library");preferences_.skin=4;preferences_.skinId="uforce";preferences_.usePreviews=false;
        const char *names[]={"Mario Kart 8","Super Mario 3D World","Splatoon","Bayonetta 2","Captain Toad Treasure Tracker"};
        for(size_t i=0;i<games_.size();++i){games_[i].name=names[i%5];games_[i].coverPath=std::string("/media/Front Cover/")+names[i%5]+".png";
            if(std::ifstream(std::string("/media/Front Cover/")+names[i%5]+".jpg"))games_[i].coverPath=std::string("/media/Front Cover/")+names[i%5]+".jpg";
            games_[i].backgroundPath=std::string("/media/Fanart/")+names[i%5]+".jpg";games_[i].fullCoverPath.clear();}
        for(int type=0;type<CoverPresentation::TypeCount;++type){
            preferences_.coverType=type;PresentationLayout()=0;
            if(!UForceHome())return false;
            for(int n=0;n<60;++n){Render(0);SDL_Delay(10);}
            if(!Render(0,output+"-row-"+std::to_string(type)+".png"))return false;
            selected_=0;visualSelected_=0;HomeNavigate(1,0,false,false);if(selected_!=1)return false;
            HomeTouch(60,520);if(selected_!=0)return false;
            HomeTouch(60,520);if(!ConsumeDetailsLaunch())return false;
            FocusSystemButtons();HomeNavigate(0,-1,false,false);if(dockFocus_!=-1)return false;
        }
        preferences_.coverType=CoverPresentation::Box;
        for(int layout:{0,2,6}){preferences_.coverLayout=layout;if(!UForceHome())return false;}
        for(int layout:{1,3,4,5,7,8,9}){preferences_.coverLayout=layout;if(UForceHome())return false;}
        for(int skin=0;skin<4;++skin){preferences_.skin=skin;
            const auto c=UIShade({20,31,49,250});if(c.r!=20||c.g!=31||c.b!=49||c.a!=250)return false;}
        preferences_.skin=4;
        for(Color original:{Color{20,31,49,250},Color{10,19,32,250},Color{32,52,73,255},Color{130,147,171,255}}){
            const auto c=UIShade(original);if(c.b>c.g||c.a!=original.a)return false;}
        SDL_Log("PASS: uForce box/logo/media/thumbnail rows, controller and touch, layout boundaries, popup palette and other-skin isolation");
        return true;
    }
    bool CheckUForce(const std::string&output){
        if(!VisualPreview("library"))return false;preferences_.skin=4;preferences_.skinId="uforce";preferences_.coverType=CoverPresentation::Thumbnails;preferences_.thumbnailLayout=0;
        if(SkinSpec().engine!=4||SkinSpec().id!="uforce"||!UForceHome())return false;
        if(!TextureCache::CheckShelfCache("/media/Fanart/Mario Kart 8.jpg"))return false;
        for(int skin=0;skin<5;++skin)for(int type=0;type<CoverPresentation::TypeCount;++type)
            for(int layout=0;layout<CoverPresentation::LayoutCount(type);++layout) {
                preferences_.skin=skin;preferences_.coverType=type;PresentationLayout()=layout;
                if(!Render(0))return false;
            }
        preferences_.skin=4;preferences_.skinId="uforce";preferences_.coverType=CoverPresentation::Thumbnails;preferences_.thumbnailLayout=0;
        SDL_Log("PASS shared artwork: all five skins/all layouts render; 256/512px disk caches reuse source decode in native and SDL modes");
        selected_=0;visualSelected_=0;
        for(size_t i=0;i<games_.size();++i){
            const std::string files[]={"Mario Kart 8","Super Mario 3D World","Splatoon","Bayonetta 2","Captain Toad Treasure Tracker"};
            games_[i].name=files[i%5];games_[i].backgroundPath="/media/Fanart/"+files[i%5]+".jpg";games_[i].coverPath="/media/Front Cover/"+files[i%5]+".png";
            if(std::ifstream("/media/Front Cover/"+files[i%5]+".jpg"))games_[i].coverPath="/media/Front Cover/"+files[i%5]+".jpg";
            games_[i].fullCoverPath.clear();
            games_[i].publisher="Nintendo";
            const std::string descriptions[]={"Race through gravity-defying courses with Mario and friends. Choose your kart and head for the finish line.","Explore colourful worlds with Mario, Luigi, Peach and Toad. Team up with friends and discover the Super Bell.","Make a splash in colourful turf battles. Cover the arena in your team's ink and dive into action.","Master spectacular combos and Witch Time in this stylish action adventure from PlatinumGames.","Explore miniature worlds full of secrets. Guide Captain Toad past tricky traps and uncover hidden treasure."};
            games_[i].description=descriptions[i%5];
        }
        for(int n=0;n<30;++n){Render(0);SDL_Delay(15);}
        if(!Render(0,output+"-home.png"))return false;
        HomeNavigate(1,0,false,false);if(selected_!=1)return false;
        HomeTouch(60,520);if(selected_!=0)return false;
        HomeTouch(60,520);if(!ConsumeDetailsLaunch())return false;
        FocusSystemButtons();HomeNavigate(1,0,false,false);if(dockFocus_!=1)return false;
        HomeNavigate(0,-1,false,false);if(dockFocus_!=-1)return false;
        ToggleSettings();settingsProgress_=1;if(!Render(0,output+"-settings.png"))return false;
        settings_=false;settingsProgress_=0;ToggleDetails();detailsProgress_=1;for(int n=0;n<30;++n){Render(0);SDL_Delay(15);}if(!Render(0,output+"-details.png"))return false;
        details_=false;detailsProgress_=0;preferences_.thumbnailLayout=1;if(!Render(0,output+"-wall.png"))return false;
        preferences_.thumbnailLayout=0;preferences_.usePreviews=true;preferences_.previewDelaySeconds=0;
        videoTest_=true;videoSelection_=nullptr;
        games_[visible_[selected_]].previewPath="/project/artifacts/video-preview-20260913/Disney-Bolt.mp4";
        mediaSources_.push_back({MediaType::Preview,-1,"/project/artifacts/video-preview-20260913",true,0,"",{0,VideoMode::Foreground,0}});
        const auto start=SDL_GetTicks();
        do {Render(0);SDL_Delay(5);}while(!video_.Ready()&&SDL_GetTicks()-start<10000);
        if(!video_.Ready()||videoOptions_.mode!=VideoMode::Background||showcaseProgress_!=0.f)return false;
        const auto playbackAt=SDL_GetTicks();
        while(SDL_GetTicks()-playbackAt<2500){Render(0);SDL_Delay(5);}
        if(video_.Statistics().presented<10)return false;
        if(!Render(0,output+"-banner-video.png"))return false;
        selected_=1;visualSelected_=1;games_[visible_[selected_]].previewPath.clear();Render(0);
        if(video_.Active()||videoPending_)return false;
        if(!Render(0,output+"-banner-fallback.png"))return false;
        for(int type:{CoverPresentation::Box,CoverPresentation::Logo,CoverPresentation::Media}){
            preferences_.coverType=type;PresentationLayout()=0;selected_=0;visualSelected_=0;
            const auto until=SDL_GetTicks()+10000;
            do{Render(0);SDL_Delay(5);}while(!video_.Ready()&&!SDL_TICKS_PASSED(SDL_GetTicks(),until));
            if(!video_.Ready()||videoOptions_.mode!=VideoMode::Background)return false;
            selected_=1;visualSelected_=1;Render(0);if(video_.Active()||videoPending_)return false;
        }
        SDL_Log("PASS uForce: banner video, no-video fallback,  skin discovery, shelf navigation, touch selection/launch, footer focus, settings/details/wall");return true;
    }
#endif
