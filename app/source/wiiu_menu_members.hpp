// Dashboard's paged Wii U Menu presentation. No changes to library/launch routing.
    bool WiiUMenuLayout() const {return ViewType()==CoverPresentation::Thumbnails && ViewLayout(CoverPresentation::Thumbnails)==2;}
    int WiiUMenuPages() const {return std::max(1,(int(visible_.size())+14)/15);}
    int WiiUMenuDotCount() const {return std::min(12,WiiUMenuPages());}
    int WiiUMenuActiveDot() const {
        const int pages=WiiUMenuPages(),count=WiiUMenuDotCount();
        return pages<=1?0:int(std::lround(double(selected_/15)*(count-1)/(pages-1)));
    }
    int WiiUMenuDotPage(int dot) const {
        const int count=WiiUMenuDotCount();
        return count<=1?0:int(std::lround(double(std::clamp(dot,0,count-1))*(WiiUMenuPages()-1)/(count-1)));
    }
    static SDL_Rect WiiUMenuCell(int slot) {return {82+(slot%5)*161,13+(slot/5)*151,127,127};}
    SDL_Rect WiiUMenuViewport() const {
        const int top=SwitchSkin()?72:(preferences_.skin==1?76:80);
        const int bottom=SwitchSkin()?HomeDockY()-40:(preferences_.skin==1?666:650);
        const float scale=std::min(1200.f/938.f,float(bottom-top)/464.f);
        const int width=int(938*scale),height=int(464*scale);
        return {(1280-width)/2,top,width,height};
    }
    float WiiUMenuPagePitch() const {return SwitchSkin()?866.f:826.f;}
    float WiiUMenuScale() const {return WiiUMenuViewport().w/938.f;}
    SDL_Rect WiiUMenuToScreen(SDL_Rect r) const {
        const auto v=WiiUMenuViewport();const float scale=WiiUMenuScale();
        return {v.x+int(r.x*scale),v.y+int(r.y*scale),int(r.w*scale),int(r.h*scale)};
    }
    float wiiuMenuPage_=-1,wiiuPageFrom_=0,wiiuPageTarget_=0,wiiuPageTime_=1,wiiuFocus_=1;
    int wiiuFocusIndex_=-1,wiiuPreviousFocus_=-1;
    void UpdateWiiUMenu(float dt) {
        if(!WiiUMenuLayout()){wiiuMenuPage_=-1;wiiuFocusIndex_=-1;return;}
        const float target=float(selected_/15);
        if(wiiuMenuPage_<0 || wiiuMenuPage_>=WiiUMenuPages()){
            wiiuMenuPage_=wiiuPageFrom_=wiiuPageTarget_=target;wiiuPageTime_=1;
        }
        if(target!=wiiuPageTarget_){wiiuPageFrom_=wiiuMenuPage_;wiiuPageTarget_=target;wiiuPageTime_=0;}
        wiiuPageTime_=std::min(1.f,wiiuPageTime_+std::max(0.f,dt)/.24f);
        const float t=wiiuPageTime_,ease=t*t*(3-2*t);
        wiiuMenuPage_=wiiuPageFrom_+(wiiuPageTarget_-wiiuPageFrom_)*ease;
        if(wiiuFocusIndex_!=int(selected_)){
            wiiuPreviousFocus_=wiiuFocusIndex_;wiiuFocus_=wiiuFocusIndex_<0?1.f:0.f;wiiuFocusIndex_=int(selected_);
        }
        wiiuFocus_=std::min(1.f,wiiuFocus_+std::max(0.f,dt)/.12f);
    }
    void WiiUMenuSelect(int index) {
        if(visible_.empty())return;
        index=std::clamp(index,0,int(visible_.size())-1);
        if(index!=int(selected_)){DismissVideo();Move(index-int(selected_));}
    }
    void WiiUMenuPage(int direction) {
        if(visible_.empty())return;
        int page=int(selected_/15)+direction;
        if(preferences_.wrapNavigation)page=(page%WiiUMenuPages()+WiiUMenuPages())%WiiUMenuPages();
        else page=std::clamp(page,0,WiiUMenuPages()-1);
        WiiUMenuSelect(std::min(page*15+int(selected_%15),int(visible_.size())-1));
    }
    void WiiUMenuNavigate(int dx,int dy) {
        if(visible_.empty())return;
        const int slot=int(selected_%15),col=slot%5,row=slot/5;
        if(dy>0&&(row==2||int(selected_)+5>=int(visible_.size()))){if(SwitchSkin())dockFocus_=std::min(col,5);return;}
        if(dy){if(row+dy>=0&&row+dy<3)WiiUMenuSelect(int(selected_)+dy*5);return;}
        if(dx && (col+dx<0||col+dx>4)) {
            int page=int(selected_/15)+(dx>0?1:-1);
            if(preferences_.wrapNavigation)page=(page+WiiUMenuPages())%WiiUMenuPages();
            else if(page<0||page>=WiiUMenuPages())return;
            WiiUMenuSelect(page*15+row*5+(dx>0?0:4));
        }else if(dx)WiiUMenuSelect(int(selected_)+dx);
    }
    bool WiiUMenuTouch(int x,int y) {
        const auto viewport=WiiUMenuViewport();
        const int localX=int((x-viewport.x)/WiiUMenuScale()),localY=int((y-viewport.y)/WiiUMenuScale());
        for(int direction:{-1,1}){
            if((direction<0&&selected_/15==0)||(direction>0&&int(selected_/15)+1>=WiiUMenuPages()))continue;
            const auto arrow=WiiUMenuArrowRect(direction,true);
            if(x>=arrow.x&&x<arrow.x+arrow.w&&y>=arrow.y&&y<arrow.y+arrow.h){WiiUMenuPage(direction);return true;}
        }
        if(x<viewport.x||x>=viewport.x+viewport.w||y<viewport.y||y>=viewport.y+viewport.h)return false;
        x=localX;y=localY;
        if(y<28){
            const int count=WiiUMenuDotCount();
            const int left=468-count*23/2;
            if(x>=left&&x<left+count*23)WiiUMenuSelect(WiiUMenuDotPage((x-left)/23)*15);
            return true;
        }
        if(std::abs(wiiuMenuPage_-float(selected_/15))>.02f)return true;
        for(int slot=0;slot<15;++slot){const auto r=WiiUMenuCell(slot);const int index=int(selected_/15)*15+slot;
            if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h&&index<int(visible_.size())){
                if(index==int(selected_))detailsLaunchRequested_=true;else WiiUMenuSelect(index);
                return true;
            }
        }
        return true;
    }
    void WiiUMenuTriangle(float ax,float ay,float bx,float by,float cx,float cy,Color color) {
        const SDL_Color c{color.r,color.g,color.b,color.a};
        const SDL_Vertex v[]={{{ax,ay},c,{0,0}},{{bx,by},c,{0,0}},{{cx,cy},c,{0,0}}};
        SDL_RenderGeometry(renderer_,nullptr,v,3,nullptr,0);
    }
    void DrawWiiUMenuFrame(SDL_Rect r,bool selected,Uint8 opacity) {
        gWiiUMenuMaterial.DrawShadow(renderer_,r,opacity);
        if(SwitchSkin()){
            auto border=selected?SwitchAccent():SwitchMuted();border.a=opacity;
            auto face=SwitchBackground();face.a=opacity;
            FillRoundedRect(renderer_,r,15,border);
            FillRoundedRect(renderer_,{r.x+4,r.y+4,r.w-8,r.h-8},12,face);
        }else gWiiUMenuMaterial.Draw(renderer_,r,selected,opacity);
    }

    static SDL_Rect WiiUMenuIcon(SDL_Rect rect,float focus) {
        const int edge=rect.w-20+int(22*focus);
        return {rect.x+10-int(11*focus),rect.y+10-int(10*focus),edge,edge};
    }
    void DrawWiiUMenuTile(int index,SDL_Rect rect,bool selected,float focus=0,Uint8 opacity=255,int offsetX=0) {
        SDL_Rect frame=rect;
        if(focus>0)frame={rect.x-int(15*focus),rect.y-int(13*focus),rect.w+int(30*focus),rect.h+int(30*focus)};
        DrawWiiUMenuFrame(frame,focus>0,opacity);
        if(index<0||index>=int(visible_.size()))return;
        const auto &game=games_[visible_[index]];
        const SDL_Rect icon=WiiUMenuIcon(rect,focus);
        auto artwork=textures_.GetCover(game.coverPath,selected);
        float u0=0,u1=1;
        if(!artwork.texture){
            const auto profile=CaseProfileForGame(game,libraries_);const float total=profile.widthMm*2+profile.thicknessMm;
            const auto face=CoverFaces::Resolve(game,CoverFaces::Face::Front,profile.widthMm/total,(profile.widthMm+profile.thicknessMm)/total,[&](const std::string &path){return textures_.GetCover(path,selected).texture!=nullptr;});
            artwork=textures_.GetCover(face.path,selected);u0=face.u0;u1=face.u1;
        }
        auto iconFace=SwitchSkin()?SwitchBackground():Color{239,244,240,255};iconFace.a=opacity;
        FillRoundedRect(renderer_,icon,13,iconFace);
        if(artwork.texture){
            const int faceWidth=std::max(1,int((u1-u0)*artwork.width));
            const int edge=std::min(faceWidth,artwork.height);
            SDL_Rect source{int(u0*artwork.width)+(faceWidth-edge)/2,(artwork.height-edge)/2,edge,edge};
            // Rounded icon mesh avoids square image corners over the glass bevel.
            std::vector<SDL_Vertex> vertices;std::vector<int> indices;
            vertices.push_back({{icon.x+icon.w*.5f,icon.y+icon.h*.5f},{255,255,255,opacity},{(source.x+edge*.5f)/artwork.width,(source.y+edge*.5f)/artwork.height}});
            for(int corner=0;corner<4;++corner)for(int step=0;step<=8;++step){
                const float a=(-3.14159265f+corner*1.57079633f)+step*1.57079633f/8;
                const float cx=corner==0||corner==3?icon.x+12.f:icon.x+icon.w-12.f;
                const float cy=corner<2?icon.y+12.f:icon.y+icon.h-12.f;
                const float x=cx+12*std::cos(a),y=cy+12*std::sin(a);
                vertices.push_back({{x,y},{255,255,255,opacity},{(source.x+(x-icon.x)/icon.w*edge)/artwork.width,(source.y+(y-icon.y)/icon.h*edge)/artwork.height}});
            }
            for(int i=1;i<int(vertices.size());++i){indices.push_back(0);indices.push_back(i);indices.push_back(i+1==int(vertices.size())?1:i+1);}
            SDL_RenderGeometry(renderer_,artwork.texture,vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));
        }else {auto ink=SwitchSkin()?SwitchInk():Color{94,111,107,255};ink.a=opacity;DrawFittedText(renderer_,game.name,icon.x+8,icon.y+44,icon.w-16,2,ink,true);}
        // The label shelf sits over square artwork; it never compresses the image.
        if(focus>0){
            if(SwitchSkin()){
                auto face=SwitchBackground();face.a=opacity;
                const int top=icon.y+icon.h-int(21*focus);
                FillRoundedRect(renderer_,{frame.x+4,top,frame.w-8,frame.y+frame.h-4-top},8,face);
            }else gWiiUMenuMaterial.DrawShelf(renderer_,frame,icon,focus,opacity);
        }
        if(selected){
            if(!launchAnimation_&&!launchCapture_){auto origin=icon;origin.x-=offsetX;launchOrigin_=WiiUMenuToScreen(origin);launchYaw_=launchPitch_=launchRoll_=0;}
            if(focus>.8f){
                if(!SwitchSkin())for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
                    if(dx*dx+dy*dy<=5)DrawText(renderer_,"Start",rect.x+rect.w/2+dx,rect.y+112+dy,2,{36,70,76,opacity},true);
                auto ink=SwitchSkin()?SwitchInk():Color{255,255,255,255};ink.a=opacity;
                DrawText(renderer_,"Start",rect.x+rect.w/2,rect.y+112,2,ink,true);
            }
        }
        if(game.favorite)DrawFavoriteBadge(icon.x+icon.w-12,icon.y+12,28,opacity);
        DrawInstallBadge(game,icon,opacity);
    }
    void DrawWiiUMenu() {
        SDL_Rect oldViewport{},oldClip{};float oldX=1,oldY=1;
        SDL_RenderGetViewport(renderer_,&oldViewport);SDL_RenderGetScale(renderer_,&oldX,&oldY);
        const bool clipped=SDL_RenderIsClipEnabled(renderer_);SDL_RenderGetClipRect(renderer_,&oldClip);
        const auto viewport=WiiUMenuViewport();
        const SDL_Rect fullWidth{0,viewport.y,1280,viewport.h};
        SDL_RenderSetViewport(renderer_,&fullWidth);SDL_RenderSetScale(renderer_,oldX*WiiUMenuScale(),oldY*WiiUMenuScale());
        SDL_RenderSetClipRect(renderer_,nullptr);
        const bool font=gSwitchFont,smooth=gSmoothText,halo=gGlassTextHalo;
        SetSwitchFont(true);gSmoothText=true;gGlassTextHalo=false;
        const auto savedFaces=gSmoothFonts;const bool menuFont=gMenuFont;
        // Read the console's standard font in place; no Nintendo font is shipped.
        // Hosts and unavailable shared-font data retain the bundled fallback.
        for(int i=1;i<6;++i)if(auto *face=FallbackFace(gFallbackFonts[0],i)){
            gSmoothFonts[i]=face;gMenuFont=true;
        }
        DrawWiiUMenuContent(false,int(std::round(viewport.x/WiiUMenuScale())));
        gSmoothFonts=savedFaces;gMenuFont=menuFont;
        SDL_RenderSetScale(renderer_,oldX,oldY);SDL_RenderSetViewport(renderer_,&oldViewport);
        SDL_RenderSetClipRect(renderer_,clipped?&oldClip:nullptr);
        SetSwitchFont(font);gSmoothText=smooth;gGlassTextHalo=halo;
        DrawWiiUMenuArrows(true);
    }
    static Uint8 WiiUMenuPageOpacity(int page,float camera){
        const float distance=std::clamp(std::abs(page-camera),0.f,1.f);
        const float eased=distance*distance*(3-2*distance);
        return Uint8(255.f-155.f*eased);
    }
    void DrawWiiUMenuContent(bool arrows=true,int offsetX=0) {
        DrawWiiUMenuDots(offsetX);
        const int page=int(selected_/15);
        const float camera=wiiuMenuPage_<0?float(page):wiiuMenuPage_;
        // Paint neighboring page edges too, as in the console menu.
        for(int p=std::max(0,int(std::floor(camera))-1);p<=std::min(WiiUMenuPages()-1,int(std::ceil(camera))+1);++p)
            for(int slot=0;slot<15;++slot){const int i=p*15+slot;auto r=WiiUMenuCell(slot);r.x+=int((p-camera)*WiiUMenuPagePitch())+offsetX;
                if(r.x+r.w<0||r.x>938+2*offsetX||i==int(selected_)&&!visible_.empty())continue;
                DrawWiiUMenuTile(i,r,false,i==wiiuPreviousFocus_?1-wiiuFocus_:0,WiiUMenuPageOpacity(p,camera));
            }
        if(!visible_.empty()){
            auto r=WiiUMenuCell(int(selected_%15));r.x+=int((page-camera)*WiiUMenuPagePitch())+offsetX;
            DrawWiiUMenuTile(int(selected_),r,dockFocus_<0,dockFocus_<0?wiiuFocus_:0,WiiUMenuPageOpacity(page,camera),offsetX);
            if(dockFocus_<0&&wiiuFocus_>=1&&std::abs(page-camera)<.02f){
                const std::string title=Selected()->name;
                const int width=std::clamp(TextWidth(title,3)+48,132,650);
                const bool top=int(selected_%15)<5;
                const int height=74;
                const int x=std::clamp(r.x+r.w/2-width/2,12+offsetX,926+offsetX-width),y=top?r.y+158:r.y-102;
                const Color bubble=SwitchSkin()?(preferences_.skin==2?Color{255,255,255,250}:Color{65,65,65,250}):Color{255,255,255,239};
                FillRoundedRect(renderer_,{x,y+4,width,height},22,{64,82,78,28});
                FillRoundedRect(renderer_,{x,y,width,height},22,bubble);
                if(top)WiiUMenuTriangle(float(r.x+r.w/2-12),float(y+2),float(r.x+r.w/2+12),float(y+2),float(r.x+r.w/2),float(y-18),bubble);
                else WiiUMenuTriangle(float(r.x+r.w/2-12),float(y+height-2),float(r.x+r.w/2+12),float(y+height-2),float(r.x+r.w/2),float(y+height+18),bubble);
                DrawFittedText(renderer_,title,x+24,y+22,width-48,3,SwitchSkin()?SwitchInk():Color{62,65,64,255},true);
            }
        }
        if(arrows)DrawWiiUMenuArrows(false);
    }
    SDL_Rect WiiUMenuArrowRect(int direction,bool screenSpace) const {
        SDL_Rect r{direction<0?-54:880,155,112,190};
        if(screenSpace){
            r=WiiUMenuToScreen(r);
            r.x=std::clamp(r.x,4,1280-r.w-4);
        }
        return r;
    }
    void DrawWiiUMenuArrows(bool screenSpace){
        const int page=int(selected_/15);
        for(int direction:{-1,1}){
            if((direction<0&&page==0)||(direction>0&&page+1>=WiiUMenuPages()))continue;
            const auto r=WiiUMenuArrowRect(direction,screenSpace);
            if(SwitchSkin()){
                auto ink=SwitchInk();ink.a=210;SetColor(renderer_,ink);
                const float tail=r.x+r.w*(direction<0?.68f:.32f),tip=r.x+r.w*(direction<0?.32f:.68f);
                for(int n=-2;n<=2;++n){const SDL_FPoint points[]={{tail+n,r.y+r.h*.18f},{tip+n,r.y+r.h*.5f},{tail+n,r.y+r.h*.82f}};SDL_RenderDrawLinesF(renderer_,points,3);}
            }else gWiiUMenuMaterial.DrawArrow(renderer_,r,direction<0);
        }
    }
    void DrawWiiUMenuDots(int offsetX=0) {
        const int count=WiiUMenuDotCount(),activeDot=WiiUMenuActiveDot();
        for(int i=0;i<count;++i){const bool active=i==activeDot;const int size=active?18:9;
            FillRoundedRect(renderer_,{offsetX+468-count*23/2+i*23+(18-size)/2,1+(18-size)/2,size,size},3,active?(UForceSkin()?SwitchAccent():Color{0,190,203,255}):Color{175,183,179,255});
        }
    }
