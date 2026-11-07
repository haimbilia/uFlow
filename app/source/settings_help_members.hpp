    bool settingsHelpOpen_=false;
    std::string settingsHelpLabel_="GENERAL",settingsHelpTitle_,settingsHelpBody_;
    void ToggleSettingsHelp(){
        if(settingsHelpOpen_){settingsHelpOpen_=false;return;}
        if(!settings_||bindingCapture_>=0)return;

        static const char* categories[]={"GENERAL","LIBRARY","MEDIA","DISPLAY","CONTROLS","LAUNCHERS"};
        std::string label=settingsHelpLabel_;

        if(supportOpen_)label=supportRow_==2?"CRASH REPORT NOTIFICATIONS":supportRow_==1?"SAVE REPORT":"PROBLEM RECORDING";
        else if(supportFocus_)label="SAVE REPORT";
        else if(typeEditor_)label=SettingsHelp::ContentTypeLabel(typeRow_);
        else if(launcherEditor_)label=LauncherRowText(LauncherLogicalRow()).first;
        else if(settingsSidebar_)label=categories[settingsCategory_];
        else if(settingsCategory_==4){
            label=settingsRow_<int(ControlAction::Count)?ControlActionName(static_cast<ControlAction>(settingsRow_)):"RESET CONTROLS";
        }
        settingsHelpTitle_=label;settingsHelpBody_=typeEditor_?SettingsHelp::ContentTypeText(typeRow_):SettingsHelp::Text(label);
        if(sourceEditor_ && mediaMode_ && label=="LIBRARY")
            settingsHelpBody_="Choose which content type uses artwork from this folder. All libraries makes it shared; a content-type-specific folder takes priority for that type. This does not move or rename images.";
        if(!settingsSidebar_&&settingsCategory_==4&&!supportOpen_){
            settingsHelpBody_=label=="RESET CONTROLS"?"Restores the default buttons for all actions, including Minus for Settings Help.":
                SettingsHelp::Text(label)+" Select this action, hold the desired button or combination, then release to assign it. You can add alternatives or clear its mapping.";
        }
        if(directoryBrowser_||launcherBrowse_)settingsHelpBody_="Open a folder to browse inside it. Choose the folder or supported file requested by this setting. Back returns without changing the selection.";
        else if(sourceManager_&&!sourceEditor_&&!typeManager_&&!launcherEditor_)settingsHelpBody_=mediaMode_?SettingsHelp::Text("MEDIA"):SettingsHelp::Text("LIBRARY");
        if(settingsAction_>=0){
            const auto action=SettingsActions()[settingsAction_];
            if(action==SettingsAction::Save){settingsHelpTitle_="SAVE CHANGES";settingsHelpBody_="Saves the current configuration. Game files and saves are not moved or deleted.";}
            if(action==SettingsAction::Delete){settingsHelpTitle_="DELETE ENTRY";settingsHelpBody_="Removes this configuration entry. It does not delete the files in its folder.";}
            if(action==SettingsAction::Back||action==SettingsAction::Cancel){settingsHelpTitle_="BACK";settingsHelpBody_="Returns to the previous view. If an editor has unsaved changes, uFlow asks before discarding them.";}
        }
        settingsHelpOpen_=true;
    }
    void DrawSettingsHelp(Color accent){
        if(!settings_||!settingsHelpOpen_)return;
        if(preferences_.skin!=1){SetColor(renderer_,{0,0,0,155});SDL_RenderFillRect(renderer_,nullptr);}
        const bool light=preferences_.skin==1||preferences_.skin==2;
        const PopupTextGlowScope glow(preferences_.skin==1);
        const Color ink=light?UIShade(Color{38,53,64,255}):UIShade(Color{238,244,252,255});
        FillRoundedRect(renderer_,{190,190,900,345},22,light?UIShade(Color{238,246,249,255}):UIShade(Color{20,32,46,255}));
        if(preferences_.skin==1)DrawGlassSurface(renderer_,{190,190,900,345},24,accent,100,true);
        DrawFittedText(renderer_,settingsHelpTitle_,230,224,820,3,ink);
        std::istringstream words(settingsHelpBody_);std::string word,line;int y=284;
        while(words>>word){const auto next=line.empty()?word:line+" "+word;
            if(!line.empty()&&TextWidth(next,2)>810){DrawText(renderer_,line,230,y,2,ink);y+=30;line=word;}else line=next;
        }
        if(!line.empty())DrawText(renderer_,line,230,y,2,ink);
        FillRoundedRect(renderer_,{835,474,210,42},10,{accent.r,accent.g,accent.b,75});
        DrawText(renderer_,"B  CLOSE",880,486,2,ink);
    }
