    struct ProfileChoice {
        uint8_t slot;
        std::string name;
        std::shared_ptr<SDL_Texture> portrait;
    };
    std::vector<ProfileChoice> profileChoices_;
    bool profilePicker_=false;
    int profileChoice_=0;
    std::string profileMessage_;
    uint8_t requestedProfile_=0;
    bool addProfileRequested_=false;
    bool ConsumeAddProfile(){return std::exchange(addProfileRequested_,false);}

    bool ProfilePickerOpen() const {return profilePicker_;}
    uint8_t ConsumeProfileSwitch(){
        const uint8_t slot=requestedProfile_;
        requestedProfile_=0;
        return slot;
    }
#ifndef UFLOW_HOST_PREVIEW
    void SwitchProfile(uint8_t slot){
        profilePicker_=true;
        if(!nn::act::Initialize().IsSuccess()){
            profileMessage_="Wii U account service unavailable.";
            return;
        }
        const bool available=nn::act::IsSlotOccupied(slot);
        const bool needsPassword=available && nn::act::IsNetworkAccountEx(slot) && !nn::act::IsPasswordCacheEnabledEx(slot);
        bool switched=false;
        if(available && !needsPassword){
            const auto expected=nn::act::GetPersistentIdEx(slot);
            const auto result=nn::act::LoadConsoleAccount(slot,0,nullptr,false);
            switched=result.IsSuccess() && expected && nn::act::GetSlotNo()==slot &&
                     nn::act::GetPersistentId()==expected;
            if(!switched)OSReport("uFlow: account switch slot=%u accepted=%u active=%u\n",
                                  unsigned(slot),unsigned(result.IsSuccess()),unsigned(nn::act::GetSlotNo()));
        }
        nn::act::Finalize();
        LoadProfile();
        if(switched){
            profilePicker_=false;
            SetStatus("SWITCHED TO "+profileName_,4000);
        }else{
            profileMessage_=needsPassword?"Sign in to this user on the Wii U and save its password.":
                                          "The Wii U could not switch to this user.";
        }
    }
#endif
    void OpenProfilePicker(){
        profileMessage_.clear();
        profileChoices_.clear();
        profileChoice_=0;
#ifndef UFLOW_HOST_PREVIEW
        nn::act::Initialize();
        for(uint8_t slot=1;slot<=12;++slot){
            if(!nn::act::IsSlotOccupied(slot))continue;
            std::string label="User "+std::to_string(slot);
            int16_t name[nn::act::MiiNameSize]{};
            if(nn::act::GetMiiNameEx(name,slot).IsSuccess()){
                char *utf8=SDL_iconv_string("UTF-8","UTF-16BE",reinterpret_cast<const char*>(name),sizeof(name));
                if(utf8 && *utf8)label=utf8;
                SDL_free(utf8);
            }
            if(slot==profileSlot_)profileChoice_=int(profileChoices_.size());
            profileChoices_.push_back({slot,label,{LoadProfilePortrait(slot),SDL_DestroyTexture}});
        }
        nn::act::Finalize();
#else
        profileChoices_={{1,"Player 1"},{2,"Player 2"}};
#endif

        if(!settings_)ToggleSettings();
        settingsCategory_=0;settingsRow_=0;settingsSidebar_=false;settingsEditing_=false;
        sourceManager_=mediaMode_=audioSettings_=typeManager_=false;
        settingsAction_=-1;profilePicker_=true;
    }
    int ProfileTileCount() const {
        const int count=int(profileChoices_.size());
        return count+(count<12?1:0);
    }
    int FirstProfileTile() const {return std::max(0,profileChoice_/4-1)*4;}
    SDL_Rect ProfileTileRect(int i) const {
        const int count=ProfileTileCount();
        const int cols=std::min(count,4);
        return {640-(cols*214-12)/2+(i%4)*214,207+((i-FirstProfileTile())/4)*150,202,138};
    }
    void ProfileInput(int horizontal,int vertical,bool accept,bool back){
        if(back){profilePicker_=false;return;}
        const int count=ProfileTileCount();
        if(horizontal)profileChoice_=(profileChoice_+(horizontal>0?1:-1)+count)%count;
        else if(vertical) {
            const int rows=(count+3)/4;
            const int row=(profileChoice_/4+(vertical>0?1:-1)+rows)%rows;
            profileChoice_=std::min(row*4+profileChoice_%4,count-1);
        }
        if(!accept)return;
        profilePicker_=false;
        if(profileChoice_==int(profileChoices_.size()))addProfileRequested_=true;
        else if(profileChoices_[profileChoice_].slot!=profileSlot_)requestedProfile_=profileChoices_[profileChoice_].slot;
    }
    void ProfileTouch(int x,int y,int drag){
        if(std::abs(drag)>=30){ProfileInput(0,drag<0?1:-1,false,false);return;}
        if(y>=568 && y<620 && x>=945 && x<1170){ProfileInput(0,0,false,true);return;}
        const int first=FirstProfileTile();
        for(int i=first;i<std::min(first+8,ProfileTileCount());++i){
            const SDL_Rect tile=ProfileTileRect(i);
            const SDL_Point point{x,y};
            if(SDL_PointInRect(&point,&tile)){
                profileChoice_=i;ProfileInput(0,0,true,false);return;
            }
        }
    }
    void DrawProfilePicker(Color accent){
        DrawPanelBase(accent,70,1140,1.f);
        const auto ink=SwitchInk();
        DrawText(renderer_,"CHOOSE A USER",640,124,3,ink,true);
        if(!profileMessage_.empty())DrawText(renderer_,profileMessage_,640,170,1,ink,true);
        const int first=FirstProfileTile();
        for(int i=first;i<std::min(first+8,ProfileTileCount());++i){
            const SDL_Rect tile=ProfileTileRect(i);
            const int cx=tile.x+tile.w/2;
            FillControlRoundedRect(renderer_,tile,14,{ink.r,ink.g,ink.b,22});
            if(i==profileChoice_)DrawFocusFrame(tile,14,accent);
            const bool add=i==int(profileChoices_.size());
            if(add){
                FillSmoothDot(renderer_,cx,tile.y+54,35,{accent.r,accent.g,accent.b,85});
                FillControlRoundedRect(renderer_,{cx-16,tile.y+51,32,6},3,ink);
                FillControlRoundedRect(renderer_,{cx-3,tile.y+38,6,32},3,ink);
            }else{
                DrawProfilePortrait(profileChoices_[i].portrait.get(),cx-35,tile.y+19,70);
                if(profileChoices_[i].slot==profileSlot_)DrawText(renderer_,"CURRENT",cx,tile.y+117,1,ink,true);
            }
            const std::string name=add?"Add New User":profileChoices_[i].name;
            const int width=std::min(tile.w-16,TextWidth(name,2));
            DrawFittedText(renderer_,name,cx-width/2,tile.y+93,tile.w-16,2,ink);
        }
        DrawText(renderer_,"A  SELECT",722,583,2,ink);
        DrawText(renderer_,"B  BACK",993,583,2,ink);
    }

#ifdef UFLOW_HOST_PREVIEW
    bool CheckProfilePicker(){
        VisualPreview("settings");
        settingsCategory_=0;settingsRow_=0;settingsSidebar_=false;settingsProgress_=1;
        profileSlot_=1;
        SettingsInput(0,0,true);
        if(!profilePicker_)return false;
        SettingsInput(1,0);
        SettingsInput(0,0,true);
        if(profilePicker_ || ConsumeProfileSwitch()!=2 || ConsumeProfileSwitch())return false;
        ToggleSettings();
        const auto &skin=SkinSpec();
        if(!HomeTouch(skin.avatarX+5,skin.avatarY+(preferences_.skin==1?-20:-26)+5) || !profilePicker_)return false;
        const auto current=ProfileTileRect(0);
        SettingsTouch(current.x+current.w/2,current.y+35);
        if(profilePicker_ || ConsumeProfileSwitch())return false;
        OpenProfilePicker();
        const auto add=ProfileTileRect(2);
        SettingsTouch(add.x+add.w/2,add.y+35);
        if(!ConsumeAddProfile() || ConsumeAddProfile() || ConsumeProfileSwitch())return false;
        settingsCategory_=3;settingsRow_=0;settingsSidebar_=false;
        const auto old=preferences_.skinId;
        SettingsInput(0,0,true);SettingsInput(1,0);
        return preferences_.skinId!=old;
    }
#endif
