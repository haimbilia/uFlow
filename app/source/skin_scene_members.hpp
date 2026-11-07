// Downloaded layouts share the app's actions and existing Display selectors.
    int sceneButtonFocus_=-1;
    SkinEffects::Renderer skinEffectsRenderer_, skinFocusRenderer_;
#ifdef UFLOW_HOST_PREVIEW
    float layoutPreviewTime_=-1.f;
#endif
    bool SceneSkin() const {return preferences_.skin==5 && SkinSpec().layout.Enhanced();}
    const std::vector<SkinLayout::Node>& SceneNodes() const {return SkinSpec().layout.Nodes(ViewType(),PresentationLayout());}
    // Flat scene nodes request their own bounded artwork. Do not also run the
    // unrelated cover/model prefetcher or background full-library warmup.
    bool SceneFlatArtwork() const {
        if(!SceneSkin())return false;
        const auto *library=SceneLibrary();
        if(!library || (library->source!="cover"&&library->source!="background"&&library->source!="thumbnail"))return false;
        for(const auto &node:SceneNodes())if(node.kind=="model")return false;
        return true;
    }
    bool SceneVideo() const {if(!SceneSkin())return false;for(const auto&n:SceneNodes())if(n.kind=="video")return true;return false;}
    Color SceneColor(const std::string&name) const {return name=="background"?SwitchBackground():name=="accent"?SwitchAccent():name=="muted"||name=="panel"?SwitchMuted():SwitchInk();}
    const SkinLayout::Node *SceneLibrary() const {for(const auto &n:SceneNodes())if(n.kind=="library")return &n;return nullptr;}
    int sceneQuality_=2,sceneSlowFrames_=0,sceneFastFrames_=0;
    void SceneBudget(float delta){
        if(delta<=0||OverlayOpen()||!preferences_.backgroundMotion)return;
        if(delta>.020f){sceneFastFrames_=0;if(++sceneSlowFrames_>=12){sceneQuality_=std::max(1,sceneQuality_-1);sceneSlowFrames_=0;}}
        else {sceneSlowFrames_=0;if(delta<.018f&&++sceneFastFrames_>=180){sceneQuality_=std::min(3,sceneQuality_+1);sceneFastFrames_=0;}}
    }
    int SceneEffects() const {return preferences_.backgroundMotion?sceneQuality_:0;}
    Uint32 sceneTick_=0; float sceneTime_=0;
    float SceneSeconds() {
#ifdef UFLOW_HOST_PREVIEW
        if(layoutPreviewTime_>=0)return layoutPreviewTime_;
#endif
        const auto now=SDL_GetTicks();if(sceneTick_&&preferences_.backgroundMotion&&!OverlayOpen())sceneTime_+=std::min<Uint32>(now-sceneTick_,50)/1000.f;sceneTick_=now;return sceneTime_;
    }
    std::string SceneText(std::string s) const {
        const auto *g=Selected();const char *levels[]={"OFF","ON","ON","ON"};
        const std::vector<std::pair<std::string,std::string>> values={{"{genre}",g?g->genre:""},{"{players}",g?g->players:""},{"{title}",g?g->name:"No games"},{"{description}",g?g->description:"Add games in Library settings."},
            {"{library}",LibraryLabel(CurrentLibraryId())},{"{content-type}",g?LibraryLabel(GameLibraryId(*g)):LibraryLabel(CurrentLibraryId())},{"{position}",LibraryPosition()},{"{profile}",profileName_},{"{effects}",levels[SceneEffects()]},
            {"{play}",ControlBindingLabel(preferences_,ControlAction::Launch)},{"{details}",ControlBindingLabel(preferences_,ControlAction::Details)},
            {"{system-buttons}",ControlBindingLabel(preferences_,ControlAction::FocusSystemButtons)},
            {"{favorite}",ControlBindingLabel(preferences_,ControlAction::Favorite)},{"{sort}",ControlBindingLabel(preferences_,ControlAction::Sort)}};
        for(const auto &[key,value]:values){size_t at=0;while((at=s.find(key,at))!=s.npos){s.replace(at,key.size(),value);at+=value.size();}}return s;
    }
    void SceneAction(const std::string&a){
        if(a=="launch"){if(Selected())detailsLaunchRequested_=true;}
        else if(a=="details")ToggleDetails();else if(a=="settings")ToggleSettings();else if(a=="search")FindOrClearSearch();
        else if(a=="next-library")ChangeTab(1);else if(a=="previous-library")ChangeTab(-1);else if(a=="next-game")Move(1);else if(a=="previous-game")Move(-1);
        else if(a=="effects"){preferences_.backgroundMotion=!preferences_.backgroundMotion;QueueSettingsSave();}
        else if(a=="favorite")ToggleFavorite();
        else if(a=="sort")OpenSort();
        else if(a=="favorites"||a=="all") {auto found=std::find(libraryTabs_.begin(),libraryTabs_.end(),a=="favorites"?kFavoritesLibraryId:kAllLibraryId);if(found!=libraryTabs_.end())ChangeTab(int(found-libraryTabs_.begin())-tab_);}
    }
    bool SceneImage(const std::string&path,SDL_Rect r,bool crop,int opacity=255,int radius=0,bool small=false){
        // Scene assets are not high-detail cover requests. Reuse the pinned
        // background and bounded shelf cache rather than competing with cover prefetch.
        const auto im=small?textures_.GetBrowseThumbnail(path):textures_.GetBackground(path);if(!im.texture||im.width<=0||im.height<=0)return false;
        SDL_Rect src{0,0,im.width,im.height},dest=r;
        if(crop){const float scale=std::max(float(r.w)/im.width,float(r.h)/im.height);src.w=std::min(im.width,int(r.w/scale));src.h=std::min(im.height,int(r.h/scale));src.x=(im.width-src.w)/2;src.y=(im.height-src.h)/2;}
        else {const float scale=std::min(float(r.w)/im.width,float(r.h)/im.height);dest.w=int(im.width*scale);dest.h=int(im.height*scale);dest.x+=(r.w-dest.w)/2;dest.y+=(r.h-dest.h)/2;}
        Uint8 old=255;SDL_GetTextureAlphaMod(im.texture,&old);SDL_SetTextureAlphaMod(im.texture,Uint8(opacity));
        if(radius>0){SDL_ScaleMode previous;SDL_GetTextureScaleMode(im.texture,&previous);SDL_SetTextureScaleMode(im.texture,SDL_ScaleModeLinear);
            SkinSurface::Fill(renderer_,{float(dest.x),float(dest.y),float(dest.w),float(dest.h)},float(radius),{255,255,255,255},{255,255,255,255},im.texture,{float(src.x)/im.width,float(src.y)/im.height,float(src.w)/im.width,float(src.h)/im.height});SDL_SetTextureScaleMode(im.texture,previous);}
        else SDL_RenderCopy(renderer_,im.texture,&src,&dest);SDL_SetTextureAlphaMod(im.texture,old);return true;
    }
    CoverFaces::Selection SceneCoverFace(const GameEntry &g){
        const auto profile=CaseProfileForGame(g,libraries_);
        const float panel=profile.sidewaysSleeve?profile.heightMm:profile.widthMm,total=panel*2+profile.thicknessMm;
        return CoverFaces::Resolve(g,CoverFaces::Face::Front,panel/total,(panel+profile.thicknessMm)/total,
            [&](const std::string &path){return textures_.GetBrowseThumbnail(path).texture!=nullptr;});
    }
    bool SceneCoverImage(const GameEntry &g,SDL_Rect r,bool crop,int radius){
        const auto face=SceneCoverFace(g);if(face.path.empty())return false;
        const auto im=textures_.GetBrowseThumbnail(face.path);if(!im.texture||im.width<=0||im.height<=0)return false;
        const auto profile=CaseProfileForGame(g,libraries_);
        const bool fallback=face.path==g.defaultSleevePath;
        const bool turn=CoverFaces::TurnSleeve(profile.sidewaysSleeve,profile.sidewaysDefaultSleeve,face.sleeve,fallback,im.width,im.height);
        const int rotation=turn?(fallback?1:-1):0;
        const float width=turn?im.height:(face.u1-face.u0)*im.width,height=turn?(face.u1-face.u0)*im.width:im.height;
        if(width<=0||height<=0)return false;
        SDL_FRect box{float(r.x),float(r.y),float(r.w),float(r.h)},cut{0,0,1,1};
        const float scale=crop?std::max(box.w/width,box.h/height):std::min(box.w/width,box.h/height);
        if(crop){cut.w=box.w/(width*scale);cut.h=box.h/(height*scale);cut.x=(1-cut.w)/2;cut.y=(1-cut.h)/2;}
        else {const float w=width*scale,h=height*scale;box.x+=(box.w-w)/2;box.y+=(box.h-h)/2;box.w=w;box.h=h;}
        const float span=face.u1-face.u0;SDL_FRect uv;
        if(rotation>0)uv={face.u0+cut.y*span,1-cut.x-cut.w,cut.h*span,cut.w};
        else if(rotation<0)uv={face.u0+(1-cut.y-cut.h)*span,cut.x,cut.h*span,cut.w};
        else uv={face.u0+cut.x*span,cut.y,cut.w*span,cut.h};
        SkinSurface::Fill(renderer_,box,radius,{255,255,255,255},{255,255,255,255},im.texture,uv,rotation);return true;
    }
    SDL_Rect SceneCoverRect(const GameEntry &g,SDL_Rect r){
        const auto face=SceneCoverFace(g);if(face.path.empty())return r;
        const auto im=textures_.GetBrowseThumbnail(face.path);if(!im.texture||im.width<=0||im.height<=0)return r;
        const auto profile=CaseProfileForGame(g,libraries_);
        const bool turn=CoverFaces::TurnSleeve(profile.sidewaysSleeve,profile.sidewaysDefaultSleeve,face.sleeve,face.path==g.defaultSleevePath,im.width,im.height);
        const float w=turn?im.height:(face.u1-face.u0)*im.width,h=turn?(face.u1-face.u0)*im.width:im.height;
        if(w<=0||h<=0)return r;
        const float scale=std::min(r.w/w,r.h/h);const int width=std::max(1,int(w*scale)),height=std::max(1,int(h*scale));
        return {r.x+(r.w-width)/2,r.y+(r.h-height)/2,width,height};
    }
    void DrawSceneArt(const GameEntry&g,SDL_Rect r,const std::string &source,bool selected,int radius=0){
        if(source=="cover")r=SceneCoverRect(g,r);
        if(source=="cover"||source=="background"||source=="thumbnail"){
            if(radius>0){const SDL_FRect box{float(r.x),float(r.y),float(r.w),float(r.h)};
                bool drawn=false;
                if(source=="background"&&!g.backgroundPath.empty())drawn=SceneImage(g.backgroundPath,r,true,255,radius,true);
                if(!drawn){SkinSurface::Fill(renderer_,box,radius,{28,30,39,255},{8,10,17,255});drawn=SceneCoverImage(g,r,source=="thumbnail",radius);}
                if(source=="background"||!drawn){SkinSurface::Fill(renderer_,box,radius,{0,0,0,0},{0,0,0,170});
                    DrawFittedText(renderer_,g.name,r.x+12,r.y+r.h-30,r.w-24,1,SwitchInk());}
                SkinSurface::Border(renderer_,box,radius,{210,215,225,55},false);return;}
            FillRoundedRect(renderer_,r,8,SwitchMuted());
            bool drawn=source=="background"&&!g.backgroundPath.empty()&&SceneImage(g.backgroundPath,r,true,255,0,true);
            if(!drawn)drawn=SceneCoverImage(g,r,source=="thumbnail",0);
            if(!drawn)DrawFittedText(renderer_,g.name,r.x+12,r.y+r.h/2-10,r.w-24,2,SwitchInk());return;
        }
        skinPresentation_=source=="media"?CoverPresentation::Media:source=="logo"?CoverPresentation::Logo:CoverPresentation::Box;
        if(skinPresentation_==CoverPresentation::Box)DrawBox(g,r,0.f,selected,SwitchAccent(),.20f,-.04f,0.f,true);
        else DrawPresentation(g,r,selected,SwitchAccent(),1.f,0.f,.18f,-.04f,0.f);skinPresentation_=-1;
    }
    struct SceneCell {int index;SDL_Rect rect;};
    struct SceneCellList {std::array<SceneCell,60> data{};size_t count=0;
        void push_back(SceneCell cell){if(count<data.size())data[count++]=cell;}
        const SceneCell* begin()const{return data.data();}const SceneCell* end()const{return data.data()+count;}};
    #include "skin_shelf_members.hpp"
    SceneCellList SceneCells(const SkinLayout::Node&n) const {
        SceneCellList cells;const int total=int(visible_.size());if(!total)return cells;
        if(n.mode=="shelf"){const auto books=SceneShelfBooks(n);for(int i=0;i<books.count;++i)cells.push_back({books.books[i].index,books.books[i].bounds});return cells;}
        if(n.mode=="carousel"){
            // Use the same animated selection position as the built-in layouts.
            // Move() already adjusts it across the wrapping seam.
            const float position=total==1?0.f:visualSelected_;
            const int first=total==1?0:int(std::floor(position))-1;
            const int count=preferences_.wrapNavigation?std::min(6,total):6;
            const float sx=float(n.w)/1280,sy=float(n.h)/470;
            const float xs[]={-35,236,570,815,1060};
            for(int slot=0;slot<count;++slot){
                const int logical=first+slot;if(!preferences_.wrapNavigation&&(logical<0||logical>=total))continue;
                const int index=(logical%total+total)%total;
                const float relative=logical-position,focus=std::max(0.f,1.f-std::abs(relative));
                const float stop=relative+1.f;const int left=std::clamp(int(std::floor(stop)),0,3);
                const float x=xs[left]+(xs[left+1]-xs[left])*(stop-left);
                const int w=int((218+86*focus)*sx),h=int((318+132*focus)*sy);
                if(x*sx+w>0&&x*sx<n.w)cells.push_back({index,{n.x+int(x*sx),n.y+(n.h-h)/2,w,h}});
            }
        }else {
            const int count=n.columns*n.rows,first=int(selected_)/count*count,cw=n.w/n.columns,ch=n.h/n.rows;
            for(int cell=0;cell<count&&first+cell<total;++cell)cells.push_back({first+cell,{n.x+(cell%n.columns)*cw+n.gap/2,n.y+(cell/n.columns)*ch+n.gap/2,cw-n.gap,ch-n.gap}});
        }return cells;
    }
    SDL_Rect SceneFocusRect() {
        const auto *library=SceneLibrary();if(!library||visible_.empty())return {0,0,0,0};
        // The frame belongs to the selected artwork, never the space between cards.
        // Carousel cards still move through SceneCells; their frame follows that same cell.
        for(const auto &cell:SceneCells(*library))if(cell.index==int(selected_))
            return library->source=="cover"?SceneCoverRect(games_[visible_[cell.index]],cell.rect):cell.rect;
        return {0,0,0,0};
    }
    void FocusSceneButtons(){
        dockFocus_=-1;
        if(sceneButtonFocus_>=0){sceneButtonFocus_=-1;return;}
        int first=-1,index=0;
        for(const auto &node:SceneNodes())if(node.kind=="button"){
            if(first<0)first=index;
            if(node.action=="launch"){sceneButtonFocus_=index;return;}++index;
        }
        sceneButtonFocus_=first;
    }
    void DrawSceneHome(){
        FillRoundedRect(renderer_,{0,0,1280,720},0,SwitchBackground());
        const auto &document=SkinSpec().layout;const float seconds=SceneSeconds();int button=0;
        for(const auto &n:SceneNodes()){
            SDL_Rect r{n.x,n.y,n.w,n.h};const auto *g=Selected();
            if(n.kind=="panel"){auto color=SceneColor(n.color);color.a=Uint8(n.opacity);FillRoundedRect(renderer_,r,n.radius,color);}
            else if(n.kind=="shade"){
                for(int i=0;i<32;++i){const int x=r.x+r.w*i/32,next=r.x+r.w*(i+1)/32;FillRoundedRect(renderer_,{x,r.y,next-x,r.h},0,{0,0,0,Uint8(n.opacity*(31-i)/31)});}
            }else if(n.kind=="effects"){
                const auto focus=SceneFocusRect();SDL_FRect area{float(r.x),float(r.y),float(r.w),float(r.h)};
                if(n.source=="focus"&&focus.w>0)area={float(focus.x-100),float(focus.y-45),float(focus.w+200),float(focus.h+90)};
                const auto color=SceneColor(n.color);skinEffectsRenderer_.SetTint({color.r,color.g,color.b,color.a});
                skinEffectsRenderer_.Ambient(renderer_,document.particles,document.trails,SceneEffects(),seconds,area);
            }else if(n.kind=="text"){
                if(n.style=="profile"){
                    const int size=std::min(r.h,r.w);
                    DrawProfile(r.x+r.w-size,r.y,size,false);
                    if(r.w>size+12)DrawFittedText(renderer_,profileName_,r.x,r.y+(r.h-20)/2,r.w-size-12,1,SceneColor(n.color));
                    continue;
                }
                if(n.style=="logo-title"&&g&&!g->logoPath.empty()){
                    const auto logo=textures_.GetBackground(g->logoPath);
                    if(logo.texture&&logo.width>0&&logo.height>0){const float scale=std::min(float(r.w)/logo.width,float(r.h)/logo.height);
                        SDL_Rect logoBox{r.x,r.y,std::max(1,int(logo.width*scale)),std::max(1,int(logo.height*scale))};
                        if(SceneImage(g->logoPath,logoBox,false,n.opacity,1))continue;}
                }
                SDL_RenderSetClipRect(renderer_,&r);UForceLines(SceneText(n.text),n.x,n.y,n.w,n.scale,std::max(1,n.h/(n.scale*12+6)),n.scale*12+6,SceneColor(n.color));SDL_RenderSetClipRect(renderer_,nullptr);
            }else if(n.kind=="button"){
                const bool focused=button==sceneButtonFocus_;const auto label=SceneText(n.text);
                const bool primary=n.style=="primary",outline=n.style=="outline";
                if(primary||outline){const auto a=SwitchAccent();SDL_Color accent{a.r,a.g,a.b,255};SDL_FRect box{float(r.x),float(r.y),float(r.w),float(r.h)};
                    if(primary){SkinSurface::Fill(renderer_,box,n.radius,SkinSurface::Mix(accent,{255,249,222,255},.55f),accent);SkinSurface::Border(renderer_,box,n.radius,{255,231,166,Uint8(focused?255:215)},true);}
                    else{SkinSurface::Fill(renderer_,box,n.radius,{12,14,20,110},{12,14,20,160});SkinSurface::Border(renderer_,box,n.radius,focused?accent:SDL_Color{230,232,237,125},focused);}}
                else if(n.style!="text")FillRoundedRect(renderer_,r,n.radius,focused?SwitchAccent():SwitchMuted());
                else if(focused)FillRoundedRect(renderer_,{r.x+12,r.y+r.h-3,r.w-24,2},1,SwitchAccent());
                const auto ink=primary||(focused&&n.style!="text"&&!outline)?SwitchBackground():SwitchInk();
                DrawFittedText(renderer_,label,n.x+std::max(8,(n.w-TextWidth(label,2))/2),n.y+(n.h-20)/2,n.w-16,2,ink);++button;
            }else if(n.kind=="image"||n.kind=="video"){
                const bool bound=n.kind=="video"||n.source=="background"||n.source=="cover"||n.source=="logo";
                const auto path=bound?(g?(n.kind=="video"||n.source=="background"?g->backgroundPath:n.source=="cover"?g->coverPath:g->logoPath):std::string{}):SkinSpec().root+"/"+n.source;
                if(!path.empty())SceneImage(path,r,n.fit=="cover",n.opacity);
                if(n.kind=="video"&&videoOptions_.mode==VideoMode::Background&&video_.Ready()){SDL_Rect old;SDL_RenderGetViewport(renderer_,&old);SDL_RenderSetViewport(renderer_,&r);video_.Draw(r.w,r.h);SDL_RenderSetViewport(renderer_,&old);}
            }else if(n.kind=="model"&&g)DrawSceneArt(*g,r,n.source,true);
            else if(n.kind=="library"){
                if(n.mode=="shelf"){DrawSceneShelf(n);continue;}
                SDL_Rect clip=r;if(n.radius>0){clip={std::max(0,r.x-16),std::max(0,r.y-16),std::min(1280,r.x+r.w+16)-std::max(0,r.x-16),std::min(720,r.y+r.h+16)-std::max(0,r.y-16)};}SDL_RenderSetClipRect(renderer_,&clip);
                for(const auto &cell:SceneCells(n)){const bool focus=cell.index==int(selected_);DrawSceneArt(games_[visible_[cell.index]],cell.rect,n.source,focus,n.radius);
                }
                const auto focus=SceneFocusRect();
                if(focus.w>0){const auto tint=SwitchAccent();if(n.radius>0){SkinSurface::Border(renderer_,{float(focus.x),float(focus.y),float(focus.w),float(focus.h)},n.radius,{tint.r,tint.g,tint.b,Uint8(SceneEffects()?220+25*std::sin(seconds*1.4f):240)},true);}
                    else{SetColor(renderer_,tint);SDL_RenderDrawRect(renderer_,&focus);skinFocusRenderer_.SetTint({tint.r,tint.g,tint.b,tint.a});skinFocusRenderer_.Focus(renderer_,focus,document.trails?SceneEffects():0,seconds);}}
                SDL_RenderSetClipRect(renderer_,nullptr);
                if(visible_.empty())DrawText(renderer_,"No games in this library",r.x+r.w/2,r.y+r.h/2,2,SwitchInk(),true);
            }
        }if(sceneButtonFocus_>=button)sceneButtonFocus_=-1;
    }
    void MoveSceneButtonFocus(int dx,int dy){
        std::array<const SkinLayout::Node*,64> buttons{};int count=0;
        for(const auto &node:SceneNodes())if(node.kind=="button")buttons[count++]=&node;
        if(sceneButtonFocus_<0||sceneButtonFocus_>=count)return;
        const auto &from=*buttons[sceneButtonFocus_];dx=(dx>0)-(dx<0);dy=(dy>0)-(dy<0);if(dy)dx=0;
        const auto distance=[&](const SkinLayout::Node &to){
            const int axis=dx>0?to.x-(from.x+from.w):dx<0?from.x-(to.x+to.w):dy>0?to.y-(from.y+from.h):from.y-(to.y+to.h);
            if(axis<0)return 1000000;
            const int cross=dx?std::max({0,to.y-(from.y+from.h),from.y-(to.y+to.h)}):std::max({0,to.x-(from.x+from.w),from.x-(to.x+to.w)});
            return axis+cross*4;
        };
        int best=1000000,next=sceneButtonFocus_;
        for(int i=0;i<count;++i)if(i!=sceneButtonFocus_){const int score=distance(*buttons[i]);if(score<best){best=score;next=i;}}
        if(const auto *library=SceneLibrary();library&&distance(*library)<best)next=-1;
        sceneButtonFocus_=next;
    }
    bool SceneNavigate(int horizontal,int vertical,bool accept,bool back){
        if(!SceneSkin())return false;const auto *n=SceneLibrary();int buttons=0;for(const auto &node:SceneNodes())buttons+=node.kind=="button";
        if(sceneButtonFocus_>=buttons)sceneButtonFocus_=-1;
        if(sceneButtonFocus_>=0){
            if(back){sceneButtonFocus_=-1;return true;}
            if(horizontal||vertical){MoveSceneButtonFocus(horizontal,vertical);return true;}
            if(accept){int i=0;for(const auto&node:SceneNodes())if(node.kind=="button"&&i++==sceneButtonFocus_){SceneAction(node.action);return node.action!="launch";}}
            return horizontal!=0; // Unhandled hotkeys must reach the shared action dispatcher.
        }
        if(n&&vertical){
            if(n->mode=="grid"||n->mode=="shelf")Move(vertical*n->columns);
            else if(vertical>0)FocusSceneButtons();
            return n->mode=="grid"||n->mode=="shelf"||vertical>0;
        }
        // Match the normal row/carousel path: Move consumes movement, but the
        // caller must still dispatch simultaneous hotkeys and library switches.
        if(horizontal)Move(horizontal);
        return false;
    }

    bool SceneTouch(int x,int y){
        const auto &nodes=SceneNodes();
        for(auto it=nodes.rbegin();it!=nodes.rend();++it){const auto &n=*it;
            if(x<n.x||y<n.y||x>=n.x+n.w||y>=n.y+n.h)continue;
            if(n.kind=="button"){sceneButtonFocus_=-1;SceneAction(n.action);return true;}
            if(n.kind=="library"&&n.mode=="shelf"){
                const auto focus=SceneFocusRect();
                if(x>=focus.x&&x<focus.x+focus.w&&y>=focus.y&&y<focus.y+focus.h){sceneButtonFocus_=-1;detailsLaunchRequested_=!visible_.empty();return true;}
            }
            if(n.kind=="library")for(const auto&cell:SceneCells(n)){const auto&r=cell.rect;if(x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h){sceneButtonFocus_=-1;if(cell.index==int(selected_))detailsLaunchRequested_=true;else Move(cell.index-int(selected_));return true;}}
        }return false;
    }

#ifdef UFLOW_HOST_PREVIEW
    bool CheckSkinScenes(const std::string &output){
        const auto fail=[](int line){SDL_Log("Skin scene check failed: %d",line);return false;};
        VisualPreview("library");Skin::Definition skin;
        const char *fixture=std::getenv("UFLOW_SCENE_FIXTURE");
        if(!Skin::Read(fixture?fixture:"/project/tests/fixtures/animated-skin","scene-test",skin))return fail(__LINE__);
        skins_.push_back(skin);preferences_.skin=5;preferences_.skinId="scene-test";preferences_.usePreviews=false;
        preferences_.backgroundMotion=true;layoutPreviewTime_=3;selected_=0;visualSelected_=0;
        for(int type:{0,3})for(int layout:{0,1}){
            preferences_.coverType=type;PresentationLayout()=layout;sceneButtonFocus_=-1;
            for(int i=0;i<12;++i){Render(0);SDL_Delay(10);}
            if(!Render(0,output+"-"+std::to_string(type)+"-"+std::to_string(layout)+".png"))return fail(__LINE__);
            auto before=selected_;SceneNavigate(1,0,false,false);if(selected_==before)return fail(__LINE__);visualSelected_=float(selected_);
            const auto rect=SceneFocusRect();if(rect.w<=0||rect.h<=0)return fail(__LINE__);
            SceneTouch(rect.x+rect.w/2,rect.y+rect.h/2);if(!ConsumeDetailsLaunch())return fail(__LINE__);
        }
        // Categorized installed VC titles retain Wii U storage identity.
        auto &typed=games_[visible_[selected_]];const auto saved=typed;
        typed.libraryId="wiiu";typed.platform=Platform::WiiU;
        for(const auto &id:{"nes","snes","n64","gba","ds","wii"}){
            typed.navigationLibraryId=id;
            if(SceneText("{content-type}")!=LibraryLabel(id))return fail(__LINE__);
        }
        typed.navigationLibraryId.clear();
        if(SceneText("{content-type}")!=LibraryLabel("wiiu"))return fail(__LINE__);
        typed=saved;
        preferences_.coverType=3;PresentationLayout()=1;
        if(!SceneFlatArtwork())return fail(__LINE__);
        textures_.BeginFrame();PrefetchArtwork();
        if(textures_.Pending())return fail(__LINE__); // No invisible full-cover requests.

        selected_=visible_.size()-1;FocusSceneButtons();if(sceneButtonFocus_<0)return fail(__LINE__);
        dockFocus_=0;if(HomeNavigate(0,0,false,false))return fail(__LINE__);dockFocus_=-1; // No invisible dock may swallow hotkeys.
        int launchButton=0;for(const auto&node:SceneNodes())if(node.kind=="button"){
            if(node.action=="launch"){sceneButtonFocus_=launchButton;if(SceneNavigate(0,0,true,false)||!ConsumeDetailsLaunch())return fail(__LINE__);break;}++launchButton;}
        SceneNavigate(0,0,false,true);if(sceneButtonFocus_!=-1)return fail(__LINE__);
        // Match built-in Move semantics, including accelerated holds and wraps.
        sceneButtonFocus_=-1;preferences_.wrapNavigation=true;
        for(int stride:{1,1,2,4,-4,-1}){
            const auto start=selected_;const auto visual=visualSelected_;
            Move(stride);const auto expected=selected_;selected_=start;visualSelected_=visual;
            if(HomeNavigate(stride,0,false,false)||selected_!=expected)return fail(__LINE__);
            visualSelected_=float(selected_);
        }
        // Grid down continues browsing, instead of jumping to a header button.
        selected_=0;visualSelected_=0;sceneButtonFocus_=-1;
        const auto *grid=SceneLibrary();if(!grid||grid->mode!="grid")return fail(__LINE__);
        const int columns=grid->columns;
        SceneNavigate(0,1,false,false);
        if(selected_!=size_t(columns%visible_.size())||sceneButtonFocus_>=0)return fail(__LINE__);
        // Touch uses the authored grid dimensions and the existing TouchSwipe recognizer.
        selected_=0;visualSelected_=0;homeSwipe_={};
        const int tx=grid->x+grid->w/2,ty=grid->y+grid->h-10,pitch=grid->h/grid->rows;
        HomeSwipeInput(true,true,true,tx,ty);
        HomeSwipeInput(true,true,true,tx,ty-pitch-1);
        HomeSwipeInput(true,true,false,tx,ty-pitch-1);
        if(selected_!=size_t(columns%visible_.size()))return fail(__LINE__);
        FocusSystemButtons();if(!SystemButtonsFocused())return fail(__LINE__);
        sceneButtonFocus_=0;SceneNavigate(0,1,false,false);
        if(sceneButtonFocus_!=1)return fail(__LINE__);
        SceneNavigate(0,-1,false,false);if(sceneButtonFocus_!=0)return fail(__LINE__);
        SceneNavigate(1,0,false,false);if(sceneButtonFocus_!=-1)return fail(__LINE__);
        SceneNavigate(0,0,false,true);if(SystemButtonsFocused())return fail(__LINE__);
        // Carousel positions interpolate using the shared animated selection.
        preferences_.coverType=0;PresentationLayout()=0;selected_=2;visualSelected_=1;
        const auto first=SceneFocusRect();visualSelected_=1.5f;const auto middle=SceneFocusRect();
        visualSelected_=2;const auto last=SceneFocusRect();
        if(!(first.w>0&&first.x>middle.x&&middle.x>last.x&&first.w<middle.w&&middle.w<last.w))return fail(__LINE__);
        // Controls painted over cards must win hit testing during movement.
        preferences_.coverType=0;PresentationLayout()=1;selected_=2;visualSelected_=1;
        for(const auto &node:SceneNodes())if(node.kind=="button"&&node.action=="launch"){
            if(!SceneTouch(node.x+node.w/2,node.y+node.h/2)||selected_!=2||!ConsumeDetailsLaunch())return fail(__LINE__);break;}
        // Non-wrapping carousels must not display an unreachable opposite end.
        const auto keptVisible=visible_;
        for(int count:{0,1,2,8,9}){
            visible_=keptVisible;while(visible_.size()<size_t(count))visible_.push_back(keptVisible.front());visible_.resize(count);
            for(bool wrap:{false,true})for(int end:{0,std::max(0,count-1)}){
                preferences_.wrapNavigation=wrap;selected_=end;visualSelected_=float(end);const auto cells=SceneCells(*SceneLibrary());
                if(!count){if(cells.count)return fail(__LINE__);continue;}
                bool opposite=false;std::set<int> unique;
                for(const auto &cell:cells){if(cell.index<0||cell.index>=count||!unique.insert(cell.index).second)return fail(__LINE__);opposite|=cell.index==count-1;}
                if(!wrap&&end==0&&count>=8&&opposite)return fail(__LINE__);
                if(wrap&&end==0&&count>1&&!opposite)return fail(__LINE__);
                if(count<=2&&cells.count!=size_t(count))return fail(__LINE__);
            }
        }
        visible_=keptVisible;selected_=0;visualSelected_=0;preferences_.wrapNavigation=true;
        if(fixture){ // This package intentionally covers every artwork type.
            preferences_.coverType=1;if(SceneLibrary()->source!="logo")return fail(__LINE__);
            preferences_.coverType=2;if(SceneLibrary()->source!="media")return fail(__LINE__);
            preferences_.coverType=0;PresentationLayout()=2;if(SceneLibrary()->source!="cover"||SceneLibrary()->mode!="row")return fail(__LINE__);
            selected_=2;visualSelected_=1.25f;
            SDL_Rect rowCell{};for(const auto &cell:SceneCells(*SceneLibrary()))if(cell.index==2)rowCell=cell.rect;
            const auto rowFrame=SceneFocusRect(),artFrame=SceneCoverRect(games_[visible_[2]],rowCell);
            if(rowFrame.x!=artFrame.x||rowFrame.y!=artFrame.y||rowFrame.w!=artFrame.w||rowFrame.h!=artFrame.h)return fail(__LINE__);
            visualSelected_=1.75f;const auto sameFrame=SceneFocusRect();
            if(sameFrame.x!=rowFrame.x||sameFrame.y!=rowFrame.y||sameFrame.w!=rowFrame.w||sameFrame.h!=rowFrame.h)return fail(__LINE__);
            for(const auto &variant:SkinSpec().layout.variants)for(const auto &node:variant.nodes)if(node.action=="effects")return fail(__LINE__);
            PresentationLayout()=7;if(SceneLibrary()->source!="box"||SceneLibrary()->mode!="grid")return fail(__LINE__);
            const int oldTab=tab_;LibraryDefinition definition;definition.id="scene-override";definition.viewType=1;
            libraries_.push_back(definition);libraryTabs_.push_back(definition.id);tab_=int(libraryTabs_.size()-1);
            if(SceneLibrary()->source!="logo")return fail(__LINE__);
            libraries_.back().viewType=2;if(SceneLibrary()->source!="media")return fail(__LINE__);
            libraries_.pop_back();libraryTabs_.pop_back();tab_=oldTab;
        }
        // A real two-colour sleeve: the front is blue, back is red. Verify
        // resolver precedence and sampled rendered colour, not just a path name.
        SDL_SetRenderTarget(renderer_,nullptr);SDL_RenderSetClipRect(renderer_,nullptr);
        SetColor(renderer_,{255,0,0,255});SDL_RenderClear(renderer_);
        SDL_Rect front{650,0,630,720};SetColor(renderer_,{0,0,255,255});SDL_RenderFillRect(renderer_,&front);
        const auto sleeve=output+"-sleeve-fixture.png";if(!SaveVisualFrame(renderer_,sleeve))return fail(__LINE__);
        GameEntry sleeveGame;sleeveGame.fullCoverPath=sleeve;sleeveGame.name="Sleeve only";
        const auto resolved=SceneCoverFace(sleeveGame);if(resolved.path!=sleeve||!resolved.sleeve)return fail(__LINE__);
        SetColor(renderer_,{0,0,0,255});SDL_RenderClear(renderer_);
        if(!SceneCoverImage(sleeveGame,{100,100,200,300},false,12))return fail(__LINE__);
        unsigned char pixel[4]{};SDL_Rect center{200,250,1,1};
        if(SDL_RenderReadPixels(renderer_,&center,SDL_PIXELFORMAT_RGBA32,pixel,4)!=0||pixel[2]<200||pixel[0]>40)return fail(__LINE__);
        sleeveGame.fullCoverPath.clear();sleeveGame.defaultSleevePath=sleeve;
        if(SceneCoverFace(sleeveGame).path!=sleeve)return fail(__LINE__);
        sleeveGame.defaultSleevePath.clear();sleeveGame.coverPath=sleeve;
        if(SceneCoverFace(sleeveGame).sleeve)return fail(__LINE__);
        sleeveGame.coverPath.clear();if(SceneCoverImage(sleeveGame,{100,100,200,300},false,12))return fail(__LINE__);
        preferences_.coverType=0;PresentationLayout()=1;
        preferences_.backgroundMotion=false;auto builds=skinEffectsRenderer_.Rebuilds();Render(0);
        if(skinEffectsRenderer_.Rebuilds()!=builds)return fail(__LINE__);
        preferences_.backgroundMotion=true;layoutPreviewTime_=-1;SceneSeconds();settings_=true;auto time=SceneSeconds();SDL_Delay(10);
        if(SceneSeconds()!=time)return fail(__LINE__);settings_=false;
        for(int i=0;i<36;++i)SceneBudget(.04f);if(sceneQuality_!=1||SceneEffects()!=1)return fail(__LINE__);
        Render(0);if(!skinEffectsRenderer_.SpriteDraws())return fail(__LINE__);
        visible_.clear();Render(0);SceneNavigate(1,1,false,false);
        SDL_Log("PASS: skin variants, held navigation/hotkey pass-through, grid controller/swipe parity, button focus, carousel interpolation, motion-off, low-budget effects, overlay pause, empty library");return true;
    }
#endif
