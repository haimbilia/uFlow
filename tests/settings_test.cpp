#include "settings.hpp"
#include "binding_capture.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdio>
int main() {
    const std::string root="fs:/vol/external01/wiiu/apps/uFlow";
    assert(!std::filesystem::exists(root));
    assert(LoadSettings().backgroundTransparency==50);
    assert(LoadSettings().menuSoundsEnabled && LoadSettings().menuSoundsVolume==35);
    std::filesystem::create_directories(root);
    const auto path=root+"/uflow.cfg";
    std::ofstream(path)<<"skin=1\nuse_backgrounds=1\nstartup_video=0\npreload_builtin_covers=0\n";
    auto legacy=LoadSettings();
    legacy.menuSoundsEnabled=false;legacy.menuSoundsVolume=65;SaveSettings(legacy);
    assert(!LoadSettings().menuSoundsEnabled && LoadSettings().menuSoundsVolume==65);
    assert(!legacy.demoMode && legacy.demoIdleSeconds==30);
    legacy.demoMode=true;legacy.demoIdleSeconds=75;SaveSettings(legacy);
    assert(LoadSettings().demoMode && LoadSettings().demoIdleSeconds==75);
    std::ofstream(path)<<"demo_idle_seconds=9999\n";assert(LoadSettings().demoIdleSeconds==600);
    std::ofstream(path)<<"demo_idle_seconds=-1\n";assert(LoadSettings().demoIdleSeconds==5);
    std::ofstream(path)<<"demo_idle_seconds=invalid\n";assert(LoadSettings().demoIdleSeconds==30);
    for(int zoom=2;zoom<=8;++zoom){legacy.inspectionZoom=zoom;SaveSettings(legacy);assert(LoadSettings().inspectionZoom==zoom);}
    legacy.skinId="my-theme";legacy.bindings[8]=StickButton::Up|0x0200;SaveSettings(legacy);
    assert(LoadSettings().skinId=="my-theme" && LoadSettings().bindings[8]==(StickButton::Up|0x0200));
    legacy.bindings[8]=0;SaveSettings(legacy);assert(LoadSettings().bindings[8]==0);
    std::ofstream(path)<<"controls_version=2\nbind_8=512\nbind_9=256\nbind_0=4096\n";
    auto migrated=LoadSettings();assert(migrated.bindings[8]==StickButton::RUp&&migrated.bindings[9]==StickButton::RDown&&migrated.bindings[0]==4096);
    assert(StickButton::RightDirections(1,-1)==(StickButton::RRight|StickButton::RDown));
    {BindingCapture r;r.Begin();r.Update(0);r.Update(StickButton::RUp);assert(r.Update(0)==StickButton::RUp);}
    assert((UserSettings{}.bindings[15]&StickButton::Up)!=0);
    assert(UserSettings{}.bindings[8]==StickButton::RUp);
    legacy.bindings[8]=StickButton::RDown;AddControlBinding(legacy,0,StickButton::RLeft|0x8000);SaveSettings(legacy);
    assert(LoadSettings().bindings[8]==StickButton::RDown&&LoadSettings().combos[0].back()==(StickButton::RLeft|0x8000));
    assert(StickButton::Directions(-1,1)==(StickButton::Left|StickButton::Up));
    assert(std::string(ControlBindingName(StickButton::Up|0x0200)).find(" / ")!=std::string::npos);
    {
        BindingCapture capture;capture.Begin();
        assert(!capture.Update(0x8000));assert(!capture.Update(0)); // activation excluded
        assert(!capture.Update(0x20));assert(!capture.Update(0x8020));assert(!capture.Update(0x20));
        assert(capture.Update(0)==0x8020); // retains all simultaneous buttons until release
        capture.Begin();capture.Update(0);capture.Update(StickButton::Left);assert(capture.Update(0)==StickButton::Left);
        UserSettings combos;const int favorite=int(ControlAction::Favorite);
        ClearControlBindings(combos,favorite);AddControlBinding(combos,favorite,0x8020);AddControlBinding(combos,favorite,0x8020);
        assert(combos.combos[favorite].size()==1);
        assert(!ControlPressed(combos,ControlAction::Favorite,0x20,0x20));
        assert(!ControlPressed(combos,ControlAction::Favorite,0x8000,0x8000));
        assert(ControlPressed(combos,ControlAction::Favorite,0x8020,0x8000));
        assert(!ControlPressed(combos,ControlAction::Launch,0x8020,0x8000)); // chord wins over single
        assert(ControlHeld(combos,ControlAction::Favorite,0x8020)&&!ControlHeld(combos,ControlAction::Favorite,0x20));
        AddControlBinding(combos,favorite,0x1000);assert(ControlPressed(combos,ControlAction::Favorite,0x1000,0x1000));
        ControlChordInput routing;
        routing.Update(combos,0x8000,0x8000);assert(!routing.liveTrigger&&!routing.liveHold&&!routing.releasedSingles);
        routing.Update(combos,0x8020,0x20);assert(ControlPressed(combos,ControlAction::Favorite,0x8020,routing.liveTrigger));
        routing.Update(combos,0x8000,0);assert(!routing.liveTrigger&&!routing.liveHold&&!routing.releasedSingles);
        routing.Update(combos,0,0);assert(!routing.releasedSingles); // chord release cannot launch
        routing.Update(combos,0x8000,0x8000);routing.Update(combos,0,0);assert(routing.releasedSingles==0x8000); // A alone still works
        routing.Update(combos,0,0);assert(!routing.releasedSingles);
        routing.Update(combos,0x20,0x20);routing.Update(combos,0x8020,0x8000);assert(routing.liveTrigger==0x8000); // either press order
        routing.Update(combos,0,0);assert(!routing.releasedSingles);
        SaveSettings(combos);const auto loaded=LoadSettings();assert(loaded.combos[favorite]==combos.combos[favorite]&&loaded.bindings[favorite]==0x1000);
        assert(ControlBindingLabel(loaded,ControlAction::Favorite).find("L + A")!=std::string::npos);
        ClearControlBindings(combos,favorite);assert(combos.combos[favorite].empty()&&!combos.bindings[favorite]);
        AddControlBinding(combos,favorite,0x8020);ResetControlBindings(combos);assert(combos.combos[favorite].empty()&&combos.bindings[favorite]==0x2000);
        std::ofstream(path)<<"controls_version=3\nbind_0=36864\n";auto old=LoadSettings();
        assert(old.bindings[0]==36864&&old.combos[0].empty()); // legacy OR remains alternatives
        std::ofstream(path)<<"combo_2=4294967296,3,32768,32800\n";auto bounded=LoadSettings();assert(bounded.combos[2]==std::vector<uint32_t>{32800});
        puts("PASS: simultaneous chord capture, activation/release isolation, alternatives, specificity, roundtrip, legacy OR and clear/reset");
    }
    legacy.demoMode=false;
    assert(legacy.startLibrary.empty());
    std::ofstream(path)<<"start_tab=7\n";
    assert(LoadSettings().startTab==7 && LoadSettings().startLibrary.empty());
    for(const auto *id:{"__all","__favorites","gba","custom_library"}) {
        legacy.startLibrary=id;legacy.startTab=7;SaveSettings(legacy);
        assert(LoadSettings().startLibrary==id && LoadSettings().startTab==7);
    }
    std::ofstream(path)<<"start_library=ds\r\n";assert(LoadSettings().startLibrary=="ds");
    std::ofstream(path)<<"start_library=bad/path\n";assert(LoadSettings().startLibrary.empty());
    assert(legacy.coverZoom==120);
    for(int zoom:{40,50,75,100,120,155,190,200}) {legacy.coverZoom=zoom;SaveSettings(legacy);assert(LoadSettings().coverZoom==zoom);}
    std::ofstream(path)<<"cover_zoom=999\n";assert(LoadSettings().coverZoom==200);
    std::ofstream(path)<<"cover_zoom=-1\n";assert(LoadSettings().coverZoom==40);
    std::ofstream(path)<<"cover_zoom=invalid\n";assert(LoadSettings().coverZoom==120);
    assert(LoadSettings().coverType==CoverPresentation::Box);
    for(int type=0;type<3;++type) {
        legacy.coverType=type;legacy.coverLayout=8;legacy.logoLayout=6;legacy.mediaLayout=3;
        SaveSettings(legacy);const auto round=LoadSettings();
        assert(round.coverType==type && round.coverLayout==8 && round.logoLayout==6 && round.mediaLayout==3);
    }
    for(int value=50;value<=200;value+=5){legacy.coverSpacing=value;SaveSettings(legacy);assert(LoadSettings().coverSpacing==value);}
    for(int old=0;old<3;++old){std::ofstream(path)<<"cover_spacing="<<old<<'\n';assert(LoadSettings().coverSpacing==75+old*25);}
    std::ofstream(path)<<"cover_spacing_percent=999\ncover_type=999\nlogo_layout=999\nmedia_layout=-1\ncover_layout=999\n";
    auto bounded=LoadSettings();assert(bounded.coverSpacing==200 && bounded.coverType==3 && bounded.logoLayout==6 && bounded.mediaLayout==0 && bounded.coverLayout==9);
    std::ofstream(path)<<"cover_spacing_percent=invalid\n";assert(LoadSettings().coverSpacing==100);
    for(int skin:{2,3})for(int layout:{0,1}) {
        legacy.skin=skin;legacy.coverType=CoverPresentation::Thumbnails;legacy.thumbnailLayout=layout;
        SaveSettings(legacy);const auto restored=LoadSettings();
        assert(restored.skin==skin && restored.coverType==CoverPresentation::Thumbnails && restored.thumbnailLayout==layout);
    }
    legacy.skin=1;
    legacy.coverZoom=100;
    assert(!legacy.alphabetFilterMode);
    const auto az=static_cast<int>(ControlAction::Alphabet);
    assert(legacy.bindings[az]==0x20000);
    legacy.bindings[az]=0x0040;SaveSettings(legacy);assert(LoadSettings().bindings[az]==0x0040);
    ResetControlBindings(legacy);assert(legacy.bindings[az]==0x20000);
    legacy.titleInstallerEnabled=false;SaveSettings(legacy);assert(!LoadSettings().titleInstallerEnabled);
    legacy.titleInstallerEnabled=true;SaveSettings(legacy);assert(LoadSettings().titleInstallerEnabled);
    legacy.alphabetFilterMode=true;SaveSettings(legacy);assert(LoadSettings().alphabetFilterMode);
    legacy.alphabetFilterMode=false;SaveSettings(legacy);assert(!LoadSettings().alphabetFilterMode);
    assert(legacy.backgroundTransparency==50 && !legacy.startupVideo && !legacy.preloadBuiltInCovers);
    legacy.backgroundTransparency=65;SaveSettings(legacy);
    auto saved=LoadSettings();
    assert(saved.backgroundTransparency==65 && saved.skin==1 && saved.useBackgrounds && !saved.startupVideo && !saved.preloadBuiltInCovers);
    std::ofstream(path)<<"background_transparency=150\n";
    assert(LoadSettings().backgroundTransparency==100);
    std::ofstream(path)<<"background_transparency=-10\n";
    assert(LoadSettings().backgroundTransparency==0);
    std::ofstream(path)<<"background_transparency=invalid\n";
    assert(LoadSettings().backgroundTransparency==50);
    for(int value:{0,5,50,95,100}) {saved.backgroundTransparency=value;SaveSettings(saved);assert(LoadSettings().backgroundTransparency==value);}
    for(int theme=0;theme<AccentThemes::Count;++theme){
        std::ofstream(path)<<"theme="<<theme<<'\n';
        saved=LoadSettings();assert(saved.theme==theme);
        SaveSettings(saved);assert(LoadSettings().theme==theme);
    }
    std::ofstream(path)<<"theme=999\n";assert(LoadSettings().theme==AccentThemes::Count-1);
    std::ofstream(path)<<"theme=-1\n";assert(LoadSettings().theme==0);
    std::ofstream(path)<<"theme=invalid\n";assert(LoadSettings().theme==0);
    std::ofstream(path)<<"skin=1\ntheme=3\n";
    assert(LoadSettings().skin==1 && LoadSettings().theme==3); // legacy Amber keeps its ID
    const int nextLayout=int(ControlAction::NextLayout);
    assert(UserSettings{}.bindings[nextLayout]==0);
    AddControlBinding(saved,nextLayout,0x0030);SaveSettings(saved);
    assert(LoadSettings().combos[nextLayout]==std::vector<uint32_t>{0x0030});
    assert(ControlPressed(saved,ControlAction::NextLayout,0x0030,0x0010));
    saved.coverLayout=9;SaveSettings(saved);assert(LoadSettings().coverLayout==9);
    std::puts("PASS: all accent colors persist, legacy IDs survive, invalid values are bounded");
    std::puts("PASS: background transparency defaults, legacy settings, round trip and bounds");
    std::ofstream(path)<<"use_previews=1\n";assert(LoadSettings().previewDelaySeconds==0);
    for(int value:{0,1,10,30}){saved.previewDelaySeconds=value;SaveSettings(saved);assert(LoadSettings().previewDelaySeconds==value);}
    std::ofstream(path)<<"preview_delay_seconds=-1\n";assert(LoadSettings().previewDelaySeconds==0);
    std::ofstream(path)<<"preview_delay_seconds=999\n";assert(LoadSettings().previewDelaySeconds==30);

}
