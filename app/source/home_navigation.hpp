// Dashboard home navigation and stable wall grid.
    bool WallLayout() const {
        const int t=ViewType(),l=PresentationLayout();
        return (t==CoverPresentation::Box&&(l==7||l==9))||(t==CoverPresentation::Logo&&l==6)||
            (t==CoverPresentation::Media&&l==3)||(t==CoverPresentation::Thumbnails&&l==1);
    }
    bool CurvedWall() const {return ViewType()==CoverPresentation::Box && PresentationLayout()==9;}
    SDL_Rect GridViewport() const {return SkinSpec().gridFullscreen?SDL_Rect{0,0,1280,720}:SDL_Rect{80,SkinSpec().gridTop,1120,SkinSpec().gridHeight};}
    int GridColumns() const {return SkinSpec().gridColumns;}
    float GridGap() const {return std::clamp(preferences_.coverSpacing/10.f,5.f,20.f);}
    float GridPitchX() const {return float(GridViewport().w)/(SkinSpec().gridFullscreen?std::max(2,GridColumns()-1):GridColumns());}
    float GridPitchY() const {
        if(!SkinSpec().gridFullscreen)return float(SkinSpec().gridHeight)/SkinSpec().gridRows;
        return ViewType()==CoverPresentation::Box?(GridPitchX()-GridGap())/.71f*(CurvedWall()?1.f:std::cos(.72f))+GridGap():GridPitchX();
    }
    int GridRows() const {return int(std::ceil(GridViewport().h/GridPitchY()));}
    SDL_Rect GridCellAt(float column,float row) const {
        const auto viewport=GridViewport();const float scale=std::min(1.f,preferences_.coverZoom/120.f);
        const bool box=ViewType()==CoverPresentation::Box;
        const int w=int((box?GridPitchX()-GridGap():std::min(GridPitchX(),GridPitchY())-GridGap())*scale);
        const int h=box?int(w/.71f):w;
        return {int(640+(column-gridCameraX_)*GridPitchX()-w*.5f),
            int(viewport.y+viewport.h*.5f+(row-gridCameraY_)*GridPitchY()-h*.5f),w,h};
    }
    SDL_Rect GridCell(int index) const {return index==int(selected_)?GridCellAt(gridTargetX_,gridTargetY_):GridCellAt(index%GridColumns(),index/GridColumns());}
    SDL_Rect GridFocusRect() const {
        const auto cell=GridCell(int(selected_)),viewport=GridViewport();
        const int h=ViewType()==CoverPresentation::Box?std::min(340,int(viewport.h*.76f)):std::min(300,int(cell.h*1.8f));
        const int w=ViewType()==CoverPresentation::Box?int(h*.71f):h;
        return {640-w/2,viewport.y+(viewport.h-h)/2,w,h};
    }
    void EnsureGridSelection() {
        if(!gridCameraReady_||gridSelection_!=selected_){
            gridTargetX_=int(selected_)%GridColumns();gridTargetY_=int(selected_)/GridColumns();
            if(!gridCameraReady_){gridCameraX_=gridTargetX_;gridCameraY_=gridTargetY_;}
            gridSelection_=selected_;gridCameraReady_=true;
        }
    }
    void MoveWall(int horizontal,int vertical){
        if(visible_.empty())return;EnsureGridSelection();
        const int count=int(visible_.size()),old=int(selected_),delta=horizontal+vertical*GridColumns();
        const int next=((old+delta)%count+count)%count;
        Move(next-old);visualSelected_=float(selected_);
        gridTargetX_+=horizontal;gridTargetY_+=vertical;gridSelection_=selected_;
    }
    void UpdateGridCamera(float seconds) {
        if(SceneSkin()||!WallLayout()){gridCameraReady_=false;return;}
        EnsureGridSelection();
        const float blend=1.f-std::exp(-std::max(0.f,seconds)*24.f);
        const float x=gridTargetX_,y=gridTargetY_;
        gridCameraX_+=(x-gridCameraX_)*blend;gridCameraY_+=(y-gridCameraY_)*blend;
        if(std::abs(x-gridCameraX_)<.001f)gridCameraX_=x;
        if(std::abs(y-gridCameraY_)<.001f)gridCameraY_=y;
    }
    // All table cells share a camera and a plane. The curved wall instead wraps
    // that plane around a cylinder; its case normals follow the column angle.
    CaseScene WallScene(int column,int row,float &yaw,float &pitch) const {
        const float dx=column-gridCameraX_,dy=row-gridCameraY_;
        const float px=GridPitchX(),py=GridPitchY();
        const float gap=std::clamp(preferences_.coverSpacing/10.f,5.f,20.f);
        pitch=CurvedWall()?0.f:-.72f;yaw=0.f;
        const float pixels=std::min((px-gap)/.71f,(py-gap)/std::cos(pitch))*std::min(1.f,preferences_.coverZoom/120.f);
        float x=dx*px/pixels,y=-dy*py/(pixels*std::cos(pitch)),z=0;
        if(CurvedWall()){
            const float angle=dx*.12f,radius=px/(pixels*.12f);
            x=std::sin(angle)*radius;z=(1.f-std::cos(angle))*radius;yaw=-angle;
        }else {z=y*std::sin(pitch);y*=std::cos(pitch);}
        const auto viewport=GridViewport();
        return {x,y,z,pixels,640.f,viewport.y+viewport.h*.5f};
    }
    CaseProfile WallProfile(const GameEntry &game) const {
        auto profile=CaseProfileForGame(game,libraries_);
        const float fit=std::min({1.f,135.f/profile.widthMm,190.f/profile.heightMm});
        profile.widthMm*=fit;profile.heightMm*=fit;profile.thicknessMm*=fit;
        return profile;
    }
    void DrawWallCase(const GameEntry &game,const CaseView &view) {
#ifdef UFLOW_HOST_PREVIEW
        ++caseDraws_;
#endif
        const auto p=CaseProfileForGame(game,libraries_);
        const float panel=p.sidewaysSleeve?p.heightMm:p.widthMm,total=panel*2+p.thicknessMm;
        struct Face {float xyz[4][3];CoverFaces::Face side;int shade;bool art;};
        const Face faces[]={
            {{{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}},CoverFaces::Face::Spine,195,false},
            {{{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,-.5f,-.5f},{-.5f,-.5f,-.5f}},CoverFaces::Face::Spine,170,false},
            {{{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f}},CoverFaces::Face::Back,225,true},
            {{{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f}},CoverFaces::Face::Spine,235,true},
            {{{.5f,.5f,.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f}},CoverFaces::Face::Spine,165,false},
            {{{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f},{-.5f,-.5f,.5f}},CoverFaces::Face::Front,255,true}
        };
        for(const auto &face:faces){
            std::array<CoverProjection::Point,4> projected;
            for(int j=0;j<4;++j){const auto q=view(face.xyz[j][0],face.xyz[j][1],face.xyz[j][2]);projected[j]={q.point.x,q.point.y,view.camera-q.depth};}
            const auto &a=projected[0],&b=projected[1],&c=projected[2];
            if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<=0)continue;
            const auto art=face.art?CoverFaces::Resolve(game,face.side,panel/total,(panel+p.thicknessMm)/total,
                [&](const auto &path){return textures_.GetCover(path,false).texture!=nullptr;}):CoverFaces::Selection{};
            const auto texture=face.art?textures_.GetCover(art.path,false):TextureInfo{};
            std::array<SDL_Vertex,81> vertices;std::array<int,384> indices;int n=0;
            const int ni=CoverProjection::Build(projected,0.f,1.f,vertices.data(),indices.data(),n,face.art?3:1);
            const bool turn=texture.texture && face.side!=CoverFaces::Face::Spine && CoverFaces::TurnSleeve(p.sidewaysSleeve,p.sidewaysDefaultSleeve,art.sleeve,art.path==game.defaultSleevePath,texture.width,texture.height);
            const auto material=BoxModelSDLColor(game,{45,65,73,255});
            for(int j=0;j<n;++j){const auto uv=vertices[j].tex_coord;
                vertices[j].color=texture.texture?SDL_Color{Uint8(face.shade),Uint8(face.shade),Uint8(face.shade),255}:material;
                if(turn){const auto t=CoverFaces::SidewaysUV(art.u0,art.u1,uv.x,uv.y,face.side,art.path==game.defaultSleevePath&&(p.sidewaysDefaultSleeve||face.side==CoverFaces::Face::Back));vertices[j].tex_coord={t.u,t.v};}
                else vertices[j].tex_coord={art.u0+uv.x*(art.u1-art.u0),uv.y};
            }
            SDL_RenderGeometry(renderer_,texture.texture,vertices.data(),n,indices.data(),ni);
        }
    }
    void DrawWall(Color accent) {
        EnsureGridSelection();
        if(visible_.empty())return;
        const bool resting=std::abs(gridCameraX_-gridTargetX_)<.001f&&std::abs(gridCameraY_-gridTargetY_)<.001f;
        const bool hidden=detailsProgress_>.015f||coverInspectProgress_>.015f||showcaseProgress_>0;
        const std::string cacheKey="wall:"+std::to_string(bookshelfEpoch_)+":"+std::to_string(selected_)+":"+
            std::to_string(gridCameraX_)+":"+std::to_string(gridCameraY_)+":"+std::to_string(ViewType())+":"+
            std::to_string(PresentationLayout())+":"+std::to_string(preferences_.coverZoom)+":"+std::to_string(preferences_.coverSpacing)+":"+
            SkinSpec().id+":"+std::to_string(preferences_.theme)+":"+std::to_string(hidden)+":"+std::to_string(Selected()->favorite);
        if(resting&&bookshelfTexture_&&bookshelfCached_&&bookshelfKey_==cacheKey&&BookshelfAssetsCurrent()){
            SDL_RenderCopy(renderer_,bookshelfTexture_,nullptr,nullptr);++bookshelfCacheHits_;return;
        }
        if(!bookshelfTexture_){
            bookshelfTexture_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,kWidth,kHeight);
            if(bookshelfTexture_){const auto blend=SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ONE,SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,SDL_BLENDOPERATION_ADD);
                if(SDL_SetTextureBlendMode(bookshelfTexture_,blend)!=0){SDL_DestroyTexture(bookshelfTexture_);bookshelfTexture_=nullptr;}}
        }
        SDL_Texture *oldTarget=SDL_GetRenderTarget(renderer_);SDL_Rect oldViewport,oldClip;
        SDL_RenderGetViewport(renderer_,&oldViewport);SDL_RenderGetClipRect(renderer_,&oldClip);const bool oldClipped=SDL_RenderIsClipEnabled(renderer_);
        const bool capture=resting&&bookshelfTexture_&&SDL_SetRenderTarget(renderer_,bookshelfTexture_)==0;
        if(capture){SDL_RenderSetViewport(renderer_,nullptr);SDL_RenderSetClipRect(renderer_,nullptr);SetColor(renderer_,{0,0,0,0});SDL_RenderClear(renderer_);}
        SDL_Rect previousClip{};const bool clipped=SDL_RenderIsClipEnabled(renderer_);SDL_RenderGetClipRect(renderer_,&previousClip);
        SDL_Rect clip=GridViewport();
        if(clipped)SDL_IntersectRect(&clip,&previousClip,&clip);
        SDL_RenderSetClipRect(renderer_,&clip);
        struct Cell {int index;CaseScene scene;float yaw,pitch;};std::vector<Cell> cells;
        const int rowReach=GridRows()/2+3,columnReach=GridColumns()/2+2,count=int(visible_.size());
        // Overscan in grid coordinates, including columns across a row seam.
        // The screen edge is the only crop in the fullscreen wall.
        for(int row=int(std::floor(gridCameraY_))-rowReach;row<=int(std::ceil(gridCameraY_))+rowReach;++row)
        for(int column=int(std::floor(gridCameraX_))-columnReach;column<=int(std::ceil(gridCameraX_))+columnReach;++column){
            const int logical=row*GridColumns()+column;
            const int i=(logical%count+count)%count;
            if(row==int(gridTargetY_) && column==int(gridTargetX_))continue;
            if(ViewType()==CoverPresentation::Box){float yaw,pitch;auto scene=WallScene(column,row,yaw,pitch);cells.push_back({i,scene,yaw,pitch});}
            else {const auto rect=GridCellAt(float(column),float(row));if(SDL_HasIntersection(&rect,&clip))DrawPresentation(games_[visible_[i]],rect,false,accent);}
        }
        std::sort(cells.begin(),cells.end(),[](const Cell&a,const Cell&b){return a.scene.z<b.scene.z;});
        for(const auto &cell:cells){const auto &game=games_[visible_[cell.index]];
            const CaseView view({0,0,220,314},WallProfile(game),cell.yaw,cell.pitch,0.f,12.f,&cell.scene);
            const auto bounds=view.Bounds();SDL_Rect rect{int(bounds.x),int(bounds.y),int(std::ceil(bounds.w)),int(std::ceil(bounds.h))};
            if(SDL_HasIntersection(&rect,&clip))DrawWallCase(game,view);
        }
        if(!visible_.empty()&&detailsProgress_<=.015f&&coverInspectProgress_<=.015f&&showcaseProgress_<=0){
            const auto &game=games_[visible_[selected_]];const auto rect=GridFocusRect();
            if(ViewType()==CoverPresentation::Box)DrawBox(game,rect,0.f,true,accent,.18f,-.04f,0.f,true);
            else DrawPresentation(game,rect,true,accent);
        }
        SDL_RenderSetClipRect(renderer_,clipped?&previousClip:nullptr);
        if(capture){SDL_SetRenderTarget(renderer_,oldTarget);SDL_RenderSetViewport(renderer_,&oldViewport);SDL_RenderSetClipRect(renderer_,oldClipped?&oldClip:nullptr);
            SDL_RenderCopy(renderer_,bookshelfTexture_,nullptr,nullptr);
            std::vector<int> indices;indices.reserve(cells.size()+1);for(const auto &cell:cells)indices.push_back(cell.index);indices.push_back(selected_);
            CaptureBookshelfAssets(indices);bookshelfKey_=cacheKey;bookshelfCached_=true;}
    }
    void NextLayout(){
        if(FixedShelfScene()){SetStatus("THIS SKIN USES BOX / BOOKSHELF");return;}
        if(OverlayOpen())return;
        DismissVideo();CancelHomeSwipe();dockFocus_=-1;
        auto &layout=PresentationLayout();const int before=layout;layout=(layout+1)%CoverPresentation::LayoutCount(ViewType());
        if(PresentationOverride()&&!SaveLibraries(libraries_,false)){layout=before;SetStatus("COULD NOT SAVE CONTENT TYPE LAYOUT");return;}
        visualSelected_=float(selected_);gridCameraReady_=false;ClearSideCases();
        detailsBackdropReady_=false;PrefetchArtwork();if(!PresentationOverride())QueueSettingsSave();
        SetStatus(CoverPresentation::LayoutName(ViewType(),layout));
    }
    bool GlassFooterAvailable()const{return preferences_.skin==1||UForceSkin();}
    bool SystemButtonsFocused()const{return SceneSkin()?sceneButtonFocus_>=0:LayoutSkin()?skinButtonFocus_>=0:dockFocus_>=0;}
    SDL_Rect GlassFooterRect(int index)const{return index==6?SDL_Rect{1134,677,104,41}:SDL_Rect{42+index*180,677,174,41};}
    void FocusSystemButtons(){if(SceneSkin()){FocusSceneButtons();return;}if(GlassFooterAvailable()){DismissVideo();dockFocus_=dockFocus_<0?0:-1;}else if(SwitchSkin())dockFocus_=0;}
    void DrawGlassFooter(Color accent){
        DrawGlassSurface(renderer_,{32,674,1216,78},23,accent,88,true);
        for(int i=0;i<7;++i){const auto r=GlassFooterRect(i);
            if(dockFocus_==i)FillRoundedRect(renderer_,r,12,{accent.r,accent.g,accent.b,95});
            if(i==6){
                const int cx=r.x+r.w/2,cy=r.y+r.h/2;
                const Color ink{68,78,87,255};
                DrawArc(renderer_,float(cx),float(cy+1),10,-1.05f,4.19f,ink);
                SetColor(renderer_,ink);
                SDL_Rect stem{cx-1,cy-12,2,13};SDL_RenderFillRect(renderer_,&stem);
                continue;
            }
            const auto &hint=footerHints_[i];
            const std::string label=hint.button.find("NONE")!=std::string::npos?hint.action:hint.button+" "+hint.action;
            DrawFittedText(renderer_,label,r.x+8,689,r.w-16,2,{68,78,87,255});
        }
        if(dockFocus_>=0)DrawText(renderer_,"L3 / B BACK    A SELECT",640,650,1,{68,78,87,255},true);
    }
    bool HomeNavigate(int horizontal,int vertical,bool accept,bool back) {
        if(powerPrompt_) {
            if(back){powerPrompt_=false;return true;}
            if(horizontal)powerConfirm_=horizontal>0;
            if(accept){if(powerConfirm_)shutdownRequested_=true;powerPrompt_=false;}
            return true;
        }
        if(LayoutSkin())return LayoutNavigate(horizontal,vertical,accept,back);
        if(dockFocus_>=0) {
            horizontal=(horizontal>0)-(horizontal<0);
            if(back||vertical<0){dockFocus_=-1;return true;}
            if(horizontal)dockFocus_=std::clamp(dockFocus_+horizontal,0,GlassFooterAvailable()?6:5);
            if(accept)ActivateDock(dockFocus_);
            return true;
        }
        if(WiiUMenuLayout()){if(horizontal||vertical)WiiUMenuNavigate(horizontal,vertical);return horizontal||vertical;}
        if(WallLayout()) {
            if(horizontal||vertical)MoveWall(horizontal,vertical);
            return horizontal||vertical;
        }
        if(vertical>0&&SwitchSkin()){dockFocus_=0;return true;}
        if(horizontal)Move(horizontal);
        return false;
    }
    void ActivateDock(int index) {
        DismissVideo();dockFocus_=-1;
        if(GlassFooterAvailable()){
            if(index==0)detailsLaunchRequested_=true;
            else if(index==1)ToggleDetails();
            else if(index==2)ToggleFavorite();
            else if(index==3)ChangeTab(1);
            else if(index==4)ToggleSettings();
            else if(index==5)FindOrClearSearch();
            else if(index==6){powerPrompt_=true;powerConfirm_=false;}
            return;
        }
        if(index==2){OpenGallery();return;}
        if(index==4){ToggleSettings();return;}
        if(index==5){powerPrompt_=true;powerConfirm_=false;return;}
#ifndef UFLOW_HOST_PREVIEW
        FlushSettingsSave(true);PrepareSystemBackground();
        if(index==3){if(SYSSwitchToSyncControllerOnHBM()<0){ResumeSystemForeground();SetStatus("CONTROLLER SETTINGS COULD NOT OPEN",5000);}}
        else if(index==0){SysAppNotificationArgs args{};_SYSLaunchNotifications(&args);}
        else if(index==1){SysAppBrowserArgs args{};if(SYSSwitchToBrowser(&args)<0){ResumeSystemForeground();SetStatus("WEB BROWSER COULD NOT OPEN",5000);}}
#else
        SetStatus(index==3?"WII U CONTROLLER PAIRING":index==0?"WII U NOTIFICATIONS":"WII U WEB BROWSER");
#endif
    }
    bool HomeTouch(int x,int y) {
        if(LayoutSkin())return LayoutTouch(x,y);
        if(UForceSkin()&&UForceTouch(x,y))return true;
        if(GlassFooterAvailable())for(int i=0;i<7;++i){const auto r=GlassFooterRect(i);if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h){ActivateDock(i);return true;}}

        if(UForceSkin()&&UForceHome())return false;
        if(WiiUMenuLayout() && WiiUMenuTouch(x,y))return true;
        const auto &skin=SkinSpec();if(UForceSkin()||!SwitchSkin()||std::abs(y-HomeDockY())>40)return false;
        const int index=(x-skin.dockX+skin.dockGap/2)/skin.dockGap;
        if(x<skin.dockX-skin.dockGap/2||index<0||index>=6)return false;
        ActivateDock(index);return true;
    }
    void DrawPowerPrompt() {
        if(!powerPrompt_)return;
        const bool glass=preferences_.skin==1;
        const PopupTextGlowScope glow(glass);
        const Color ink=glass?Color{42,53,62,255}:SwitchInk();
        FillRoundedRect(renderer_,{330,238,620,230},24,glass?Color{239,246,248,245}:SwitchBackground());
        if(glass)DrawGlassSurface(renderer_,{330,238,620,230},24,SwitchAccent(),100,true);
        DrawText(renderer_,"SHUT DOWN THE WII U?",640,275,3,ink,true);
        DrawText(renderer_,"Unsaved changes will be saved before shutdown.",640,325,1,ink,true);
        for(int i=0;i<2;++i){SDL_Rect r{410+i*260,383,200,52};FillRoundedRect(renderer_,r,10,(powerConfirm_==bool(i))?SwitchAccent():SwitchMuted());DrawText(renderer_,i?"SHUT DOWN":"CANCEL",r.x+100,r.y+16,2,ink,true);}
    }
    bool ConsumeShutdown(){const bool result=shutdownRequested_;shutdownRequested_=false;if(result)FlushSettingsSave(true);return result;}
    float gridCameraX_=0,gridCameraY_=0,gridTargetX_=0,gridTargetY_=0;
    size_t gridSelection_=0;
    bool gridCameraReady_=false;
    int dockFocus_=-1;
    bool powerPrompt_=false,powerConfirm_=false,shutdownRequested_=false;

    bool PowerPromptOpen()const{return powerPrompt_;}
    bool GalleryOpen()const{return galleryOpen_;}
    void OpenGallery() {
        galleryFiles_.clear();galleryIndex_=0;galleryFull_=false;galleryOpen_=true;
        const auto scan=[&](auto &&self,const std::string &path,int depth)->void {
            if(depth<0||galleryFiles_.size()>=4096)return;
            DIR *directory=opendir(path.c_str());if(!directory)return;
            while(const auto *entry=readdir(directory)) {
                if(entry->d_name[0]=='.')continue;const auto child=path+"/"+entry->d_name;struct stat info{};
                if(stat(child.c_str(),&info))continue;
                if(S_ISDIR(info.st_mode))self(self,child,depth-1);
                else {auto ext=child.substr(child.find_last_of('.')+1);for(auto &c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));if(ext=="png"||ext=="jpg"||ext=="jpeg")galleryFiles_.push_back(child);}
                if(galleryFiles_.size()>=4096)break;
            }
            closedir(directory);
        };
        scan(scan,"fs:/vol/external01/wiiu/screenshots",3);
        std::sort(galleryFiles_.rbegin(),galleryFiles_.rend());
    }
    void GalleryInput(int dx,int dy,bool accept,bool back) {
        if(back){if(galleryFull_)galleryFull_=false;else galleryOpen_=false;return;}
        if(galleryFiles_.empty())return;
        galleryIndex_=std::clamp(galleryIndex_+dx+dy*5,0,int(galleryFiles_.size())-1);
        if(accept)galleryFull_=!galleryFull_;
    }
    void DrawGallery() {
        if(!galleryOpen_)return;
        SetColor(renderer_,SwitchBackground());SDL_RenderFillRect(renderer_,nullptr);
        DrawText(renderer_,"SCREENSHOT GALLERY",60,45,3,SwitchInk());
        if(galleryFiles_.empty())DrawText(renderer_,"No screenshots in SD:/wiiu/screenshots",640,335,2,SwitchInk(),true);
        const int first=galleryFull_?galleryIndex_:(galleryIndex_/15)*15;
        const int end=std::min(int(galleryFiles_.size()),first+(galleryFull_?1:15));
        for(int i=first;i<end;++i){const int slot=i-first;SDL_Rect rect=galleryFull_?SDL_Rect{60,110,1160,510}:SDL_Rect{50+(slot%5)*240,120+(slot/5)*160,220,140};
            const auto image=textures_.GetBackground(galleryFiles_[i]);
            if(image.texture){float scale=std::min(float(rect.w)/image.width,float(rect.h)/image.height);SDL_Rect fit{rect.x+(rect.w-int(image.width*scale))/2,rect.y+(rect.h-int(image.height*scale))/2,int(image.width*scale),int(image.height*scale)};SDL_RenderCopy(renderer_,image.texture,nullptr,&fit);}
            if(i==galleryIndex_){SetColor(renderer_,SwitchAccent());for(int j=0;j<3;++j){SDL_RenderDrawRect(renderer_,&rect);--rect.x;--rect.y;rect.w+=2;rect.h+=2;}}
        }
        DrawText(renderer_,"D-PAD / STICK  MOVE     A  VIEW     B  BACK",640,664,2,SwitchInk(),true);
    }
    bool galleryOpen_=false,galleryFull_=false;
    int galleryIndex_=0;
    std::vector<std::string> galleryFiles_;

    void SystemOverlayTouch(int x,int y) {
        if(powerPrompt_&&y>=383&&y<435){
            if(x>=410&&x<610){powerPrompt_=false;return;}
            if(x>=670&&x<870){powerConfirm_=true;HomeNavigate(0,0,true,false);return;}
        }
        if(!galleryOpen_||galleryFiles_.empty())return;
        if(galleryFull_){galleryFull_=false;return;}
        const int col=(x-50)/240,row=(y-120)/160;
        if(x<50||y<120||col>=5||row>=3)return;
        const int index=(galleryIndex_/15)*15+row*5+col;
        if(index<int(galleryFiles_.size())){galleryIndex_=index;galleryFull_=true;}
    }

    TouchSwipe homeSwipe_;
    void CancelHomeSwipe(bool force=false){homeSwipe_.Cancel(force);}
    bool HomeSwipeInput(bool sample,bool valid,bool down,int x,int y){
        if(OverlayOpen()){homeSwipe_.Cancel();return false;}
        if(SceneSkin()){
            const auto *library=SceneLibrary();
            const bool inside=library&&x>=library->x&&x<library->x+library->w&&y>=library->y&&y<library->y+library->h;
            const bool grid=library&&(library->mode=="grid"||library->mode=="shelf");
            const auto event=homeSwipe_.Update(sample,valid,down,x,y,inside,inside&&grid,
                library?float(library->w)/library->columns:120.f,library?float(library->h)/library->rows:100.f);
            if(event.dx||event.dy){sceneButtonFocus_=-1;dockFocus_=-1;DismissVideo();Move(event.dx+(grid?event.dy*library->columns:0));}
            if(event.tap)HomeTouch(event.x,event.y);
            return event.consumed;
        }
        if(WiiUMenuLayout()){
            const auto event=homeSwipe_.Update(sample,valid,down,x,y,y>=WiiUMenuViewport().y&&y<WiiUMenuViewport().y+WiiUMenuViewport().h,false,400.f*WiiUMenuScale(),100.f);
            if(event.dx){dockFocus_=-1;WiiUMenuPage(event.dx>0?1:-1);}
            if(event.tap)HomeTouch(event.x,event.y);
            return event.consumed;
        }
        const bool wheel=ViewType()==CoverPresentation::Logo&&(PresentationLayout()==1||PresentationLayout()==2);
        const bool browse=UForceHome()?(y>=488&&y<636):(y>=100&&y<(SwitchSkin()?HomeDockY()-42:620));
        const auto event=homeSwipe_.Update(sample,valid,down,x,y,browse&&(!wheel||WallLayout()),browse&&(wheel||WallLayout()),
            WallLayout()?GridPitchX():120.f,WallLayout()?GridPitchY():100.f);
        if(event.dx||event.dy){
            dockFocus_=-1;DismissVideo();
            if(WallLayout()&&!visible_.empty()){
                MoveWall(event.dx,event.dy);
            }else Move(wheel?event.dy:event.dx);
        }
        if(event.tap)HomeTouch(event.x,event.y);
        return event.consumed;
    }
