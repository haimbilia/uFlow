// Downloaded layouts use the same artwork renderers and game actions as built-in skins.
    int skinPresentation_=-1, skinButtonFocus_=-1;
    bool LayoutSkin() const {return preferences_.skin==5;}
    bool LayoutVideo() const {if(SceneSkin())return SceneVideo();if(!LayoutSkin())return false;for(const auto&n:SkinSpec().layout.nodes)if(n.kind=="video")return true;return false;}
    bool BannerVideo() const {return UForceHome()||LayoutVideo();}
    Color LayoutColor(const std::string&name) const {
        return name=="background"?SwitchBackground():name=="accent"?SwitchAccent():name=="muted"||name=="panel"?SwitchMuted():SwitchInk();
    }
    const SkinLayout::Node *LayoutLibrary() const {
        for(const auto &n:SkinSpec().layout.nodes)if(n.kind=="library")return &n;
        return nullptr;
    }
    std::string LayoutText(std::string s) const {
        const auto *g=Selected();
        for(const auto &[key,value]:std::vector<std::pair<std::string,std::string>>{{"{title}",g?g->name:"No games"},{"{description}",g?g->description:""},{"{library}",LibraryLabel(CurrentLibraryId())},{"{position}",LibraryPosition()},{"{profile}",profileName_}}){
            size_t at=0;while((at=s.find(key,at))!=s.npos){s.replace(at,key.size(),value);at+=value.size();}
        }
        return s;
    }
    void LayoutAction(const std::string&a){
        if(a=="launch"){if(Selected())detailsLaunchRequested_=true;}
        else if(a=="details")ToggleDetails();else if(a=="settings")ToggleSettings();
        else if(a=="search")FindOrClearSearch();else if(a=="next-library")ChangeTab(1);
        else if(a=="previous-library")ChangeTab(-1);else if(a=="next-game")Move(1);else if(a=="previous-game")Move(-1);
    }
    void DrawLayoutArt(const GameEntry&g,SDL_Rect r,const std::string &source,bool selected){
        skinPresentation_=source=="media"?CoverPresentation::Media:source=="logo"?CoverPresentation::Logo:source=="thumbnail"?CoverPresentation::Thumbnails:CoverPresentation::Box;
        if(skinPresentation_==CoverPresentation::Box)DrawBox(g,r,0.f,selected,SwitchAccent(),.20f,-.04f,0.f,true);
        else DrawPresentation(g,r,selected,SwitchAccent(),1.f,0.f,.18f,-.04f,0.f);
        skinPresentation_=-1;
    }
    void DrawLayoutHome(){
        if(SceneSkin()){DrawSceneHome();return;}
        FillRoundedRect(renderer_,{0,0,1280,720},0,SwitchBackground());
        int button=0;
        for(const auto &n:SkinSpec().layout.nodes){
            SDL_Rect r{n.x,n.y,n.w,n.h};const auto *g=Selected();
            if(n.kind=="panel")FillRoundedRect(renderer_,r,n.radius,LayoutColor(n.color));
            else if(n.kind=="text"){
                SDL_RenderSetClipRect(renderer_,&r);UForceLines(LayoutText(n.text),n.x,n.y,n.w,n.scale,std::max(1,n.h/(n.scale*12+6)),n.scale*12+6,LayoutColor(n.color));SDL_RenderSetClipRect(renderer_,nullptr);
            }else if(n.kind=="button"){
                FillRoundedRect(renderer_,r,n.radius,button==skinButtonFocus_?SwitchAccent():SwitchMuted());
                const auto label=LayoutText(n.text);DrawFittedText(renderer_,label,n.x+std::max(8,(n.w-TextWidth(label,2))/2),n.y+(n.h-20)/2,n.w-16,2,SwitchInk());++button;
            }else if((n.kind=="image"||n.kind=="video")&&g){
                const auto path=n.kind=="video"||n.source=="background"?g->backgroundPath:n.source=="cover"?g->coverPath:n.source=="logo"?g->logoPath:SkinSpec().root+"/"+n.source;
                const auto im=textures_.GetCover(path,true);
                if(im.texture&&im.width>0&&im.height>0){const float scale=std::min(float(r.w)/im.width,float(r.h)/im.height);SDL_Rect dest{r.x+(r.w-int(im.width*scale))/2,r.y+(r.h-int(im.height*scale))/2,int(im.width*scale),int(im.height*scale)};SDL_RenderCopy(renderer_,im.texture,nullptr,&dest);}
                if(n.kind=="video"&&videoOptions_.mode==VideoMode::Background&&video_.Ready()){SDL_Rect old;SDL_RenderGetViewport(renderer_,&old);SDL_RenderSetViewport(renderer_,&r);video_.Draw(r.w,r.h);SDL_RenderSetViewport(renderer_,&old);}
            }else if(n.kind=="model"&&g)DrawLayoutArt(*g,r,n.source,true);
            else if(n.kind=="library"){
                const int count=n.columns*n.rows,first=int(selected_)/count*count,cw=n.w/n.columns,ch=n.h/n.rows;
                for(int cell=0;cell<count&&first+cell<int(visible_.size());++cell){const int index=first+cell;SDL_Rect art{r.x+(cell%n.columns)*cw+n.gap/2,r.y+(cell/n.columns)*ch+n.gap/2,cw-n.gap,ch-n.gap};
                    const bool focus=index==int(selected_);DrawLayoutArt(games_[visible_[index]],art,n.source,focus);
                    if(focus){SetColor(renderer_,SwitchAccent());SDL_RenderDrawRect(renderer_,&art);}
                }
            }
        }
    }
    bool LayoutNavigate(int horizontal,int vertical,bool accept,bool back){
        if(SceneSkin())return SceneNavigate(horizontal,vertical,accept,back);
        if(!LayoutSkin())return false;
        const auto *n=LayoutLibrary();int buttons=0;for(const auto &node:SkinSpec().layout.nodes)buttons+=node.kind=="button";
        if(skinButtonFocus_>=0){
            if(back||vertical<0){skinButtonFocus_=-1;return true;}
            if(horizontal)skinButtonFocus_=std::clamp(skinButtonFocus_+horizontal,0,std::max(0,buttons-1));
            if(accept){int i=0;for(const auto&node:SkinSpec().layout.nodes)if(node.kind=="button"&&i++==skinButtonFocus_){LayoutAction(node.action);return node.action!="launch";}}
            return horizontal!=0;
        }
        if(n&&vertical){if(vertical>0&&buttons&&(int(selected_)% (n->columns*n->rows))/n->columns==n->rows-1)skinButtonFocus_=0;else Move(vertical*n->columns);return true;}
        if(horizontal){Move(horizontal);return true;}return false;
    }
    bool LayoutTouch(int x,int y){
        if(SceneSkin())return SceneTouch(x,y);
        for(const auto&n:SkinSpec().layout.nodes){if(x<n.x||y<n.y||x>=n.x+n.w||y>=n.y+n.h)continue;
            if(n.kind=="button"){LayoutAction(n.action);return true;}
            if(n.kind=="library"){int count=n.columns*n.rows,index=int(selected_)/count*count+(y-n.y)/(n.h/n.rows)*n.columns+(x-n.x)/(n.w/n.columns);
                if(index<int(visible_.size())){if(index==int(selected_))detailsLaunchRequested_=true;else Move(index-int(selected_));}return true;}
        }return false;
    }
