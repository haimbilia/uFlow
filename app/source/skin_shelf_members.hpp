// Generic API 2 shelf geometry; no skin IDs or skin-specific art here.
    bool FixedShelfScene() const {
        if(preferences_.skin!=5)return false;
        const auto &d=SkinSpec().layout;if(d.schema<2||!d.variants.empty())return false;
        for(const auto &n:d.nodes)if(n.kind=="library")return n.mode=="shelf";
        return false;
    }
#ifdef UFLOW_HOST_PREVIEW
    bool CheckRestingShelfCache(){
        const auto before=caseDraws_;for(int i=0;i<5;++i)Render(0);
        SDL_Log("Resting shelf model rebuilds: %u, cached cases: %zu, bytes: %zu",caseDraws_-before,sideCases_.size(),sideCaseBytes_);
        for(const auto *path:{&Selected()->fullCoverPath,&Selected()->coverPath,&Selected()->backCoverPath,&Selected()->spinePath,&Selected()->defaultSleevePath})SDL_Log("Selected face %s: known=%d available=%d",path->c_str(),textures_.RenderCoverKnown(*path,true),textures_.HasRenderCover(*path,true));
        return caseDraws_==before&&ViewType()==CoverPresentation::Box&&ViewLayout(CoverPresentation::Box)==8;
    }
#endif
    SkinShelf::Motion sceneShelfMotion_;
    Uint32 sceneShelfTick_=0;
    std::string sceneShelfKey_;
    struct ShelfBook {int index;SDL_Rect draw,bounds;float yaw,focus,slotWidth;};
    struct ShelfBooks {std::array<ShelfBook,60> books{};int count=0;};
    void AdvanceSceneShelf(const SkinLayout::Node &n){
        const int count=n.columns*n.rows,page=int(selected_)/count;
        const auto key=preferences_.skinId+":"+CurrentLibraryId()+":"+std::to_string(page)+":"+std::to_string(visible_.size());
        const auto now=SDL_GetTicks();
        if(key!=sceneShelfKey_){sceneShelfMotion_.Reset();sceneShelfKey_=key;sceneShelfTick_=now;}
        sceneShelfMotion_.Advance(int(selected_)%count,std::min(count,int(visible_.size())-page*count),
            sceneShelfTick_&&!OverlayOpen()?float(now-sceneShelfTick_)/1000.f:0.f,n.animationMs);
        sceneShelfTick_=now;
    }
    ShelfBooks SceneShelfBooks(const SkinLayout::Node &n) const {
        ShelfBooks out;const int capacity=n.columns*n.rows,first=int(selected_)/capacity*capacity;
        constexpr float radians=3.14159265f/180.f;
        for(int row=0;row<n.rows;++row){
            const int start=out.count;float total=0;
            for(int col=0;col<n.columns;++col){
                const int slot=row*n.columns+col,index=first+slot;if(index>=int(visible_.size()))break;
                const float focus=sceneShelfMotion_.Focus(slot);
                const float angle=n.shelfAngles[slot%n.shelfAngles.size()];
                const float yaw=(angle+(n.selectedAngle-angle)*focus)*radians;
                const int height=int(n.shelfHeight*(1+(n.selectedScale/100.f-1)*focus));
                const auto profile=CaseProfileForGame(games_[visible_[index]],libraries_);
                SDL_Rect draw{0,0,height,height};const auto b=CaseView(draw,profile,yaw).Bounds();
                const float slotWidth=std::ceil(CaseView({0,0,n.shelfHeight,n.shelfHeight},profile,angle*radians).Bounds().w);
                out.books[out.count++]={index,draw,{int(std::floor(b.x)),int(std::floor(b.y)),int(std::ceil(b.w)),int(std::ceil(b.h))},yaw,focus,slotWidth};
                total+=slotWidth+n.gap;
            }
            if(out.count==start)continue;
            total-=n.gap;const float scale=std::min(1.f,n.w/std::max(1.f,total));float x=float(n.x);
            const float floor=float(n.y+(n.shelfBaselines.empty()?(row+1)*n.h/n.rows-n.pullout:n.shelfBaselines[row]));
            for(int i=start;i<out.count;++i){auto &book=out.books[i];
                book.draw.w=book.draw.h=int(book.draw.h*scale);
                const auto profile=CaseProfileForGame(games_[visible_[book.index]],libraries_);
                const auto b=CaseView(book.draw,profile,book.yaw).Bounds();
                const float slot=book.slotWidth*scale;
                const float left=std::clamp(x+slot*.5f-b.w*.5f,float(n.x),std::max(float(n.x),float(n.x+n.w)-b.w));
                const float contact=floor+(n.shelfSlopes.empty()?0:n.shelfSlopes[row])*(x+slot*.5f-n.x)/n.w;
                book.draw.x=int(std::round(left-b.x));book.draw.y=int(std::round(contact+n.pullout*book.focus-b.y-b.h));
                book.bounds={int(std::round(left)),int(std::round(contact+n.pullout*book.focus-b.h)),int(std::ceil(b.w)),int(std::ceil(b.h))};
                x+=slot+n.gap*scale;
            }
        }return out;
    }
    void DrawSceneShelf(const SkinLayout::Node &n){
        AdvanceSceneShelf(n);const auto books=SceneShelfBooks(n);
        const int previous=skinPresentation_;skinPresentation_=CoverPresentation::Box;
        const auto draw=[&](const ShelfBook &book){
            const bool selected=book.index==int(selected_);
            DrawBox(games_[visible_[book.index]],book.draw,book.focus==0?1.f:0.f,selected,SceneColor(n.color),book.yaw,0,0,true,CaseView::kCamera,0,0,book.focus==1);
            if(selected){const auto &r=book.bounds;const int len=16;
                SetColor(renderer_,SwitchAccent());
                for(int side:{0,1})for(int bottom:{0,1}){
                    int x=side?r.x+r.w+4:r.x-4,y=bottom?r.y+r.h+4:r.y-4;
                    for(int k=0;k<3;++k){SDL_RenderDrawLine(renderer_,x,y+k,x+(side?-len:len),y+k);SDL_RenderDrawLine(renderer_,x+k,y,x+k,y+(bottom?-len:len));}
                }
            }
        };
        // Raised cases overlap their neighbors; paint them last.
        for(int i=0;i<books.count;++i)if(books.books[i].focus==0&&books.books[i].index!=int(selected_))draw(books.books[i]);
        for(int i=0;i<books.count;++i)if(books.books[i].focus>0&&books.books[i].index!=int(selected_))draw(books.books[i]);
        for(int i=0;i<books.count;++i)if(books.books[i].index==int(selected_))draw(books.books[i]);
        skinPresentation_=previous;
        if(!books.count)DrawText(renderer_,"No games in this library",n.x+n.w/2,n.y+n.h/2,2,SwitchInk(),true);
    }
