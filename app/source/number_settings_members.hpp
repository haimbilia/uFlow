// Shared numeric-field classification for controller and touchscreen repetition.
    NumberRepeat::Hold numberHold_,numberTouchHold_;
    int numberTouchField_=0,numberTouchTop_=0,numberTouchHeight_=0,numberTouchLeft_=0,numberTouchSplit_=0;
    int NumericSettingsField(bool requireEditing=true) const {
        if(!settings_||settingsSidebar_||settingsAction_>=0||settingsHelpOpen_||draftExit_!=DraftExit::None||typeText_||directoryBrowser_||launcherBrowse_||vcMapMode_||supportOpen_)return 0;
        if(boxColorOpen_)return boxColorRow_>=1&&boxColorRow_<=3?800+boxColorRow_:0;
        if(requireEditing&&!settingsEditing_)return 0;
        if(typeEditor_)return (typeRow_==5||(typeRow_>=11&&typeRow_<=13))?100+typeRow_:0;
        if(launcherEditor_){const int row=LauncherLogicalRow();return launcherDraft_.kind==LauncherKind::DSInjection&&(row==InjectionCacheRow()||(injectionDraft_.system=="ds"&&row==12))?200+row:0;}
        if(sourceEditor_)return sourceEditorRow_==(mediaMode_?3:2)||(VideoSourceEditing()&&(sourceEditorRow_==5||sourceEditorRow_==7))||(BackgroundVideoEditing()&&sourceEditorRow_==8)?300+sourceEditorRow_:0;
        if(sourceManager_||typeManager_||mediaHelp_)return 0;
        if(audioSettings_)return settingsRow_==2||settingsRow_==5?400+settingsRow_:0;
        if(settingsCategory_==0)return preferences_.demoMode&&settingsRow_==10?510:0;
        if(settingsCategory_==3)return settingsRow_==3||settingsRow_==4||settingsRow_==5||settingsRow_==12||settingsRow_==16?600+settingsRow_:0;
        return 0;
    }
    int SettingsHeldHorizontal(int direction,bool edge,Uint32 now){
        const int field=NumericSettingsField();if(!field){numberHold_.Reset();return edge?direction:0;}
        return numberHold_.Update(direction,field,now);
    }
    bool SettingsNumericTouch(int x,int y,Uint32 now,bool begin){
        if(boxColorOpen_)return false;
        if(begin){
            numberTouchHold_.Reset();numberTouchField_=0;
            if(!settings_||settingsProgress_<.95f)return false;
            int *selection=&settingsRow_,first=SettingsFirstRow(),count=SettingsRowCount(),visible=6;
            int top=187,pitch=audioSettings_?48:62,height=audioSettings_?46:57,split=990,left=860,bottom=574;
            if(typeEditor_){selection=&typeRow_;first=std::max(0,typeRow_-5);count=TypeRowCount()-2;visible=8;top=260;pitch=height=40;split=760;left=390;bottom=548;}
            else if(launcherEditor_){selection=&launcherRow_;first=std::max(0,launcherRow_-5);count=SettingsContentCount();visible=7;top=260;pitch=height=40;split=760;left=390;bottom=540;}
            else if(sourceEditor_){selection=&sourceEditorRow_;first=SourceEditorFirst();count=SourceEditorCount()-2;visible=7;top=272;pitch=38;height=37;}
            if(x<left||x>=1132||y<top||y>=bottom)return false;
            const int offset=(y-top)/pitch,row=first+offset;
            if(offset>=visible||row>=count||y>=top+offset*pitch+height)return false;
            const int oldRow=*selection,oldAction=settingsAction_;const bool oldSidebar=settingsSidebar_;
            *selection=row;settingsSidebar_=false;settingsAction_=-1;
            const int field=NumericSettingsField(false);
            if(!field){*selection=oldRow;settingsSidebar_=oldSidebar;settingsAction_=oldAction;return false;}
            settingsEditing_=true;
            // Keep the original hit rectangle when selecting a row scrolls its viewport.
            numberTouchField_=field;numberTouchTop_=top+offset*pitch;numberTouchHeight_=height;
            numberTouchLeft_=left;numberTouchSplit_=split;
        }
        if(!numberTouchField_||NumericSettingsField(false)!=numberTouchField_){numberTouchHold_.Reset();return false;}
        if(x<numberTouchLeft_||x>=1132||y<numberTouchTop_||y>=numberTouchTop_+numberTouchHeight_){numberTouchHold_.Reset();return true;}
        const int amount=numberTouchHold_.Update(x<numberTouchSplit_?-1:1,numberTouchField_,now);
        if(amount){if(typeEditor_)TypeAdjust(amount);else if(launcherEditor_)LauncherInput(amount,0,false,false);else if(sourceEditor_)SourceEditorAdjust(amount);else SettingsAdjust(amount);}
        return true;
    }
