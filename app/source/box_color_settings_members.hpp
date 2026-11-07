    bool boxColorOpen_=false;
    int boxColorRow_=0,boxColorPreset_=0,boxColorBefore_=-1;
    const LibraryDefinition *boxPreviewDefinition_=nullptr;
    static constexpr std::array<int,12> BoxColors{-1,0x181818,0xf4f4ee,0x808890,0x008fba,0x184fba,0x6d3ba1,0xb43264,0xb52a27,0xe38125,0xe8cd51,0x348b55};
    static std::string BoxColorName(int color){if(color<0)return "Auto (model / skin)";char text[8];std::snprintf(text,sizeof(text),"#%06X",unsigned(color));return text;}
    int BoxModelTint(const GameEntry &game)const{
        if(boxPreviewDefinition_)return boxPreviewDefinition_->boxColor;
        const auto id=GameModelLibraryId(game);
        for(const auto &library:libraries_)if(library.id==id)return library.boxColor;
        return -1;
    }
    bool BoxModelColorSet(const GameEntry &game)const{return BoxModelTint(game)>=0;}
    Color BoxModelColor(const GameEntry &game,Color fallback)const{
        const int rgb=BoxModelTint(game);return rgb<0?fallback:Color{Uint8(rgb>>16),Uint8(rgb>>8),Uint8(rgb),fallback.a};
    }
    SDL_Color BoxModelSDLColor(const GameEntry &game,Color fallback)const{const auto c=BoxModelColor(game,fallback);return {c.r,c.g,c.b,c.a};}
    void OpenBoxColor(){boxColorOpen_=true;boxColorBefore_=typeDraft_.boxColor;boxColorRow_=0;settingsEditing_=false;settingsAction_=-1;
        auto found=std::find(BoxColors.begin(),BoxColors.end(),typeDraft_.boxColor);boxColorPreset_=found==BoxColors.end()?-1:int(found-BoxColors.begin());}
    void BoxColorChannel(int channel,int value){
        boxColorPreset_=-1;
        unsigned rgb=typeDraft_.boxColor<0?0x808080:unsigned(typeDraft_.boxColor),shift=(2-channel)*8;
        typeDraft_.boxColor=int((rgb&~(255u<<shift))|(unsigned(std::clamp(value,0,255))<<shift));
    }
    void BoxColorInput(int horizontal,int vertical,bool accept,bool back,bool done=false){
        if(back){typeDraft_.boxColor=boxColorBefore_;boxColorOpen_=false;return;}
        if(done||(accept&&boxColorRow_==4)){boxColorOpen_=false;return;}
        if(vertical)boxColorRow_=std::clamp(boxColorRow_+vertical,0,4);
        if(boxColorRow_==0&&(horizontal||accept)){
            boxColorPreset_=boxColorPreset_<0?(horizontal<0?int(BoxColors.size())-1:0):(boxColorPreset_+horizontal+int(BoxColors.size()))%int(BoxColors.size());typeDraft_.boxColor=BoxColors[boxColorPreset_];
        }else if(horizontal&&boxColorRow_>=1&&boxColorRow_<=3){
            const int rgb=typeDraft_.boxColor<0?0x808080:typeDraft_.boxColor;
            BoxColorChannel(boxColorRow_-1,((rgb>>((3-boxColorRow_)*8))&255)+horizontal);
        }
    }
    void BoxColorTouch(int x,int y){
        if(y>=285&&y<327&&x>=390&&x<894){const int i=(x-390)/42;if(i<int(BoxColors.size())){boxColorPreset_=i;boxColorRow_=0;typeDraft_.boxColor=BoxColors[i];}return;}
        if(y>=350&&y<485&&x>=500&&x<=860){const int row=(y-350)/45;boxColorRow_=row+1;BoxColorChannel(row,(x-500)*255/360);return;}
        if(y>=530&&y<574){if(x>=390&&x<610)BoxColorInput(0,0,false,true);else if(x>=630&&x<850)BoxColorInput(0,0,false,false,true);}
        if(x>=900&&x<1110&&y>=530&&y<574)ToggleSettingsHelp();
    }
    void DrawBoxColor(int x,Color accent,bool light){
        const Color ink=light?Color{43,54,63,255}:Color{238,244,252,255};
        DrawText(renderer_,"BOX COLOR",x,205,3,ink);
        DrawText(renderer_,"Case material only - artwork keeps its colors",x,249,1,ink);
        for(int i=0;i<int(BoxColors.size());++i){const int rgb=BoxColors[i];const Color c=rgb<0?Color{130,140,150,255}:Color{Uint8(rgb>>16),Uint8(rgb>>8),Uint8(rgb),255};
            if(boxColorRow_==0&&i==boxColorPreset_)FillRoundedRect(renderer_,{x+i*42-3,282,40,48},7,accent);
            FillRoundedRect(renderer_,{x+i*42,285,34,42},5,c);
            if(i==0)DrawText(renderer_,"A",x+9,295,1,{255,255,255,255});
        }
        const int rgb=typeDraft_.boxColor<0?0x808080:typeDraft_.boxColor;
        for(int i=0;i<3;++i){const int y=350+i*45,value=(rgb>>((2-i)*8))&255;
            if(boxColorRow_==i+1)FillRoundedRect(renderer_,{x-8,y-3,505,40},7,{accent.r,accent.g,accent.b,65});
            DrawText(renderer_,std::string(i==0?"RED":i==1?"GREEN":"BLUE"),x,y+5,1,ink);
            FillRoundedRect(renderer_,{500,y+12,360,10},5,{105,112,120,255});
            FillRoundedRect(renderer_,{500,y+12,std::max(1,value*360/255),10},5,accent);
            FillRoundedRect(renderer_,{496+value*360/255,y+6,8,22},4,ink);
            DrawText(renderer_,std::to_string(value),870,y+5,1,ink);
        }
        DrawPreparedModelPreview(typeDraft_.boxModel,true,{930,305,160,190},accent);
        DrawFittedText(renderer_,BoxColorName(typeDraft_.boxColor),950,507,155,1,ink);
        FillRoundedRect(renderer_,{390,530,220,42},8,{90,100,110,80});DrawText(renderer_,"B Cancel",410,541,1,ink);
        FillRoundedRect(renderer_,{630,530,220,42},8,boxColorRow_==4?accent:Color{90,100,110,80});DrawText(renderer_,"A Done / + Done",644,541,1,ink);
        DrawText(renderer_,"Help",960,541,1,ink);
    }
