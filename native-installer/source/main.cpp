#include "installer.hpp"
#include "generated_files.hpp"
#ifndef UFLOW_SETUP_PREVIEW
#include "vwii.hpp"
#endif
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <png.h>
#include <ctime>
#ifndef UFLOW_SETUP_PREVIEW
#include <whb/sdcard.h>
#include <coreinit/filesystem.h>
#include <vpad/input.h>
#endif

namespace {
constexpr int W=1280,H=720;
SDL_Renderer *renderer=nullptr;TTF_Font *font=nullptr,*large=nullptr;
bool running=true,foreground=true,cancelRequested=false;
uint32_t selected=Setup::All;int cursor=0,page=0;
constexpr int Rows=9,RowStep=46,ListTop=166; // components in two columns
std::string phase,path,message;uint64_t done=0,total=0;bool canCancel=true;

void Text(const std::string &s,int x,int y,SDL_Color color={36,57,69,255},bool big=false,int width=1160){
    auto *surface=TTF_RenderUTF8_Blended_Wrapped(big?large:font,s.c_str(),color,width);if(!surface)return;
    auto *texture=SDL_CreateTextureFromSurface(renderer,surface);SDL_Rect dst{x,y,surface->w,surface->h};
    SDL_RenderCopy(renderer,texture,nullptr,&dst);SDL_DestroyTexture(texture);SDL_FreeSurface(surface);
}
void Box(int x,int y,int w,int h,SDL_Color c){SDL_SetRenderDrawColor(renderer,c.r,c.g,c.b,c.a);SDL_Rect r{x,y,w,h};SDL_RenderFillRect(renderer,&r);}
void Draw(){
    if(!foreground)return;
    SDL_SetRenderDrawColor(renderer,235,244,247,255);SDL_RenderClear(renderer);
    Box(0,0,W,104,{211,232,240,255});Text("uFlow Setup",48,23,{28,79,102,255},true);
    Text("One installer  /  selected content and console setup",520,40);
    if(page==0){
        Text("Choose content types and extras. uFlow itself is always installed.",48,119);
        for(int i=0;i<Setup::ComponentCount;++i){int col=i/Rows,row=i%Rows,x=48+col*610,y=ListTop+row*RowStep;
            if(i==cursor)Box(x-9,y-6,588,40,{174,219,235,255});
            Box(x,y,26,26,Setup::On(selected,i)?SDL_Color{37,147,185,255}:SDL_Color{200,214,220,255});
            if(Setup::On(selected,i))Text("X",x+5,y-1,{255,255,255,255});
            Text(Setup::components[i].name,x+38,y,{36,57,69,255},false,530);
        }
        Text("Shared backends are automatic. Existing unchecked files are kept.",48,590);
        const int previousStyle=TTF_GetFontStyle(font);
        TTF_SetFontStyle(font,previousStyle|TTF_STYLE_BOLD);
        Text("Backup your SD card before installing",48,625);
        TTF_SetFontStyle(font,previousStyle);
        Text("A / touch: toggle     X: all     Y: none     +: continue     B: exit",48,660);
    }else if(page==1){
        Text("Ready to install selected components",48,125,{28,79,102,255},true);
        Text(message,48,201);
        Text("Target: this Wii U and its SD card. No formatting or game/save deletion.",48,306);
        Text("Existing settings and saves are kept. Replaced files are backed up.",48,359);
        Text("After installation, reboot fully before launching games.",48,412);
        if(Setup::On(selected,1)||Setup::On(selected,2)||Setup::On(selected,12))Text("CONSOLE STORAGE: install Homebrew Channel, WiiFlow forwarder,\nvWii cIOS 248/38, 249/56, 250/57 and 251/58. Keep power connected.\nNo region changes, system wipes, or IOS80 patches.",48,468);
        Text("A: confirm and install everything selected     B: change selection",48,660);
    }else if(page==2){
        Text(phase,48,136,{28,79,102,255},true);
        std::string shown=path;if(shown.size()>190)shown="..."+shown.substr(shown.size()-187);
        Text(shown,48,235,{36,57,69,255},false,1160);
        Box(48,409,1184,27,{200,217,226,255});
        if(total)Box(48,409,int(1184.0*std::min(done,total)/total),27,{37,147,185,255});
        Text(total?std::to_string(done/1024)+" / "+std::to_string(total/1024)+" KiB (current file)":"Working...",48,459);
        Text(canCancel?"B: cancel before publishing. Keep the SD inserted.":"Publishing verified files. Do not power off or remove the SD.",48,660);
    }else if(page==3){
        Text("Installation complete",48,137,{28,106,78,255},true);
        Text("All selected files verified. Reboot the console before using uFlow.",48,216);
        Text("Backups and report:\n"+message,48,278,{36,57,69,255},false,1150);
        if(Setup::On(selected,1)||Setup::On(selected,2)||Setup::On(selected,12))Text("vWii prerequisites installed and verified. No separate installers needed.\nAdd your game files to the selected library folders, then Scan Now.",48,453);
        Text("B: return to Wii U Menu     X: adding your games",48,660);
    }else if(page==4){
        Text("Setup stopped",48,137,{160,55,45,255},true);
        Text(message,48,228,{95,46,43,255},false,1130);
        Text("No automatic restore. If publishing started, complete setup before reboot.\nExisting backups are under SD:/.uflow-setup/",48,468);
        Text("A: return to component selection     B: exit",48,660);
    }else{
        Text("Add your games",48,128,{28,79,102,255},true);
        Text("1. Reboot fully into Aroma, then open uFlow.",48,213);
        Text("2. Copy game files to the selected library folders on the SD.",48,269);
        Text("3. In uFlow: + Settings > Library > Scan Now.",48,325);
        Text("Installed Wii U games are discovered automatically.",48,412);
        Text("Virtual Console ROMs ask to inject on first launch.\nROM compatibility varies by emulator / VC base.\nYour selected launchers and vWii prerequisites were handled by Setup.",48,472);
        Text("B: back",48,660);
    }
    SDL_RenderPresent(renderer);
}
struct Input {bool a=false,b=false,x=false,y=false,plus=false;int move=0,touch=-1;};
Input Poll(){
    Input input;SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(event.type==SDL_QUIT)running=false;
        else if(event.type==SDL_APP_WILLENTERBACKGROUND)foreground=false;
        else if(event.type==SDL_APP_DIDENTERFOREGROUND)foreground=true;
        else if(event.type==SDL_KEYDOWN){switch(event.key.keysym.sym){case SDLK_RETURN:input.a=true;break;case SDLK_ESCAPE:input.b=true;break;case SDLK_DOWN:input.move=1;break;case SDLK_UP:input.move=-1;break;case SDLK_RIGHT:input.move=Rows;break;case SDLK_LEFT:input.move=-Rows;break;case SDLK_x:input.x=true;break;case SDLK_y:input.y=true;break;case SDLK_p:input.plus=true;break;}}
    }
#ifndef UFLOW_SETUP_PREVIEW
    if(foreground){VPADStatus v{};VPADReadError error;const auto n=VPADRead(VPAD_CHAN_0,&v,1,&error);
        if(n>0&&error==VPAD_READ_SUCCESS){
            input.a|=(v.trigger&VPAD_BUTTON_A)!=0;input.b|=(v.trigger&VPAD_BUTTON_B)!=0;input.x|=(v.trigger&VPAD_BUTTON_X)!=0;
            input.y|=(v.trigger&VPAD_BUTTON_Y)!=0;input.plus|=(v.trigger&VPAD_BUTTON_PLUS)!=0;
            if(v.trigger&VPAD_BUTTON_DOWN)input.move=1;
            if(v.trigger&VPAD_BUTTON_UP)input.move=-1;
            if(v.trigger&VPAD_BUTTON_RIGHT)input.move=Rows;
            if(v.trigger&VPAD_BUTTON_LEFT)input.move=-Rows;
            static bool held=false;
            if(v.tpNormal.touched&&!held){VPADTouchData point{};VPADGetTPCalibratedPoint(VPAD_CHAN_0,&point,&v.tpNormal);
                int x=point.x*W/854,y=point.y*H/480;if(y>=ListTop-6&&y<ListTop-6+Rows*RowStep){int col=x>=649?1:0,row=(y-(ListTop-6))/RowStep;if(x>=39&&x<1239)input.touch=col*Rows+row;}}
            held=v.tpNormal.touched;
        }
    }
#endif
    return input;
}
bool Progress(const std::string &a,const std::string &b,uint64_t n,uint64_t t,bool cancel){
    phase=a;path=b;done=n;total=t;canCancel=cancel;
    do{auto input=Poll();if(input.b&&cancel)cancelRequested=true;if(cancel&&(!running||cancelRequested))return false;if(!foreground)SDL_Delay(20);}while(!foreground);
    static uint32_t last=0;auto now=SDL_GetTicks();if(now-last>=80){Draw();last=now;}return true;
}
uint64_t FreeSpace(){
#ifndef UFLOW_SETUP_PREVIEW
    static FSClient client;static FSCmdBlock cmd;FSInit();FSInitCmdBlock(&cmd);
    Setup::Need(FSAddClient(&client,FS_ERROR_FLAG_ALL)==FS_STATUS_OK,"Cannot open SD free-space check");uint64_t bytes=0;
    auto status=FSGetFreeSpaceSize(&client,&cmd,"/vol/external01",&bytes,FS_ERROR_FLAG_ALL);FSDelClient(&client,FS_ERROR_FLAG_ALL);
    Setup::Need(status==FS_STATUS_OK,"Cannot read SD free space");return bytes;
#else
    return 10000000000ULL;
#endif
}
#ifdef UFLOW_SETUP_PREVIEW
void SavePNG(const char *filename){
    std::vector<unsigned char> pixels(W*H*4);SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),W*4);
    png_image png{};png.version=PNG_IMAGE_VERSION;png.width=W;png.height=H;png.format=PNG_FORMAT_RGBA;
    Setup::Need(png_image_write_to_file(&png,filename,0,pixels.data(),0,nullptr)!=0,"Cannot save preview");
}
#endif
}
int main(int argc,char **argv){
    std::string sd="fs:/vol/external01",bundle="fs:/vol/content";bool mounted=false;
#ifndef UFLOW_SETUP_PREVIEW
    (void)argc;(void)argv;mounted=WHBMountSdCard()==1;
#else
    if(argc!=3)return 2;bundle=argv[1];SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
#endif
    if(SDL_Init(SDL_INIT_VIDEO)!=0){
#ifndef UFLOW_SETUP_PREVIEW
        if(mounted)WHBUnmountSdCard();
#endif
        return 1;
    }
    auto *window=SDL_CreateWindow("uFlow Setup",0,0,W,H,SDL_WINDOW_SHOWN);
    renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_PRESENTVSYNC):nullptr;
    if(renderer&&TTF_Init()==0){font=TTF_OpenFont((bundle+"/font.ttf").c_str(),24);large=TTF_OpenFont((bundle+"/font.ttf").c_str(),38);}
    if(!renderer||!font||!large){running=false;SDL_Log("uFlow Setup: initialization failed: %s",SDL_GetError());}
    if(renderer)SDL_RenderSetLogicalSize(renderer,W,H);
    Setup::Plan plan;
#ifndef UFLOW_SETUP_PREVIEW
    Vwii::Session consoleSession;
#endif
#ifdef UFLOW_SETUP_PREVIEW
    if(running){
        if(const char *screen=std::getenv("UFLOW_SETUP_PREVIEW_PAGE")){page=std::clamp(std::atoi(screen),0,5);message="Selected SD files and console prerequisites checked.\nSD:/.uflow-setup/test/report.txt";}
        Draw();SavePNG(argv[2]);
    }running=false;
#else
    if(!mounted){page=4;message="SD could not be mounted. Unlock and reinsert the SD after powering off.";}
#endif
    while(running){
        auto input=Poll();if(!foreground){SDL_Delay(20);continue;}
        if(page==0){
            if(input.move)cursor=(cursor+input.move+Setup::ComponentCount)%Setup::ComponentCount;
            if(input.touch>=0&&input.touch<Setup::ComponentCount){cursor=input.touch;selected^=1u<<cursor;}
            else if(input.a)selected^=1u<<cursor;
            if(input.x)selected=Setup::All;
            if(input.y)selected=0;
            if(input.b)running=false;
            if(input.plus){page=2;cancelRequested=false;try{
                plan=Setup::Prepare(sd,bundle,BundledFiles(),selected,Progress);
                Setup::Need(FreeSpace()>=plan.bytes+64*1024*1024ULL,"Not enough SD space for selected files and staging reserve.");
#ifndef UFLOW_SETUP_PREVIEW
                consoleSession=Vwii::Session{};
                consoleSession=Vwii::PrepareConsole(bundle,selected,BundledFiles(),Progress);
#endif
                message=std::to_string(plan.bytes/1024/1024)+" MiB to install. "+std::to_string(plan.preserved)+" existing settings/resource files will be kept.";
#ifndef UFLOW_SETUP_PREVIEW
                if(consoleSession.plan.resumeLegacy)message+="\nPrevious staged console files verified; continue installation.";
#endif
                page=1;
            }catch(const std::exception &e){message=e.what();page=4;}}
        }else if(page==1){
            if(input.b)page=0;
            if(input.a){page=2;cancelRequested=false;try{
                Setup::Need(FreeSpace()>=plan.bytes+64*1024*1024ULL,"SD free space changed; cannot install.");
                message=Setup::Install(sd,bundle,plan,std::to_string(std::time(nullptr))+"-"+std::to_string(SDL_GetPerformanceCounter()),Progress);
#ifndef UFLOW_SETUP_PREVIEW
                Vwii::InstallConsole(consoleSession,message,Progress);
#endif
                page=3;
            }catch(const std::exception &e){message=e.what();page=4;}}
        }else if(page==3){if(input.b)running=false;if(input.x)page=5;}
        else if(page==4){if(input.a)page=0;if(input.b)running=false;}
        else if(page==5&&input.b)page=3;
        if(running)Draw();
    }
    if(font)TTF_CloseFont(font);
    if(large)TTF_CloseFont(large);
    TTF_Quit();
    if(renderer)SDL_DestroyRenderer(renderer);
    if(window)SDL_DestroyWindow(window);
#ifndef UFLOW_SETUP_PREVIEW
    consoleSession=Vwii::Session{};
#endif
    // Match uFlow's proven exit lifecycle: SDL exclusively owns ProcUI.
    SDL_Quit();
#ifndef UFLOW_SETUP_PREVIEW
    if(mounted)WHBUnmountSdCard();
#endif
    return 0;
}
