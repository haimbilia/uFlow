#pragma once
#include <SDL2/SDL.h>
#include "controller_pairing.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#ifndef UFLOW_HOST_PREVIEW
#include <bloopair/bloopair.h>
#include <coreinit/dynload.h>
#endif

namespace Controllers {
struct Device {
    std::string address, name;
    int channel=-1, battery=-1;
    bool paired=false;
    bool reportValid=false;
    uint32_t buttons=0;
    int leftX=0,leftY=0,rightX=0,rightY=0;
};
struct Snapshot {
    bool ready=false, active=false, pairing=false, busy=false;
    std::string version, message="Checking controller support...";
    std::vector<Device> devices;
};
inline bool StoredHeader(const std::string& filename,const unsigned char* bytes,size_t size,Device& out){
    if(filename.size()!=16||filename.substr(12)!=".bin"||size!=6||bytes[0]!=0||bytes[1]<0xb1||bytes[1]>0xb4)return false;
    for(int i=0;i<12;++i)if(!((filename[i]>='0'&&filename[i]<='9')||(filename[i]>='A'&&filename[i]<='F')))return false;
    const unsigned vendor=(unsigned(bytes[2])<<8)|bytes[3];
    out.address=filename.substr(0,12);out.paired=true;
    out.name=vendor==0x2dc8?"8BitDo controller":vendor==0x057e?"Nintendo / Switch controller":vendor==0x054c?"PlayStation controller":vendor==0x045e?"Xbox controller":"Bluetooth controller";
    return true;
}
inline std::vector<Device> StoredDevices(const char* directory){
    std::vector<Device> result;DIR* dir=opendir(directory);if(!dir)return result;
    while(auto* entry=readdir(dir)){
        if(result.size()>=32)break;
        const std::string name=entry->d_name;if(name.size()!=16)continue;
        unsigned char header[6]{};Device device;
        // Pairing records contain link keys. Read only their public six-byte header.
        FILE* file=fopen((std::string(directory)+"/"+name).c_str(),"rb");if(!file)continue;
        const auto read=fread(header,1,sizeof(header),file);fclose(file);
        if(StoredHeader(name,header,read,device))result.push_back(device);
    }
    closedir(dir);return result;
}
// Install only when absent; never replace a user's newer/custom module.
inline std::string InstallSupport(){
    const std::string target="fs:/vol/external01/wiiu/environments/aroma/modules/setup/30_bloopair.rpx";
    struct stat st{};
    if(stat(target.c_str(),&st)==0)return "Bloopair is installed. Restart Wii U to activate it.";
    if(stat("fs:/vol/external01/wiiu/environments/aroma",&st)!=0||!S_ISDIR(st.st_mode))return "Aroma is required. Install the Aroma environment first.";
    FILE* input=fopen("fs:/vol/content/controllers/30_bloopair.rpx","rb");if(!input)return "Bundled controller module is missing.";
    std::vector<unsigned char> data(610211);const auto length=fread(data.data(),1,data.size(),input);const int extra=fgetc(input);fclose(input);
    if(length!=data.size()||extra!=EOF)return "Bundled controller module has an invalid size.";
    mkdir("fs:/vol/external01/wiiu/environments/aroma/modules",0777);mkdir("fs:/vol/external01/wiiu/environments/aroma/modules/setup",0777);
    const auto temp=target+".uflow-part";FILE* output=fopen(temp.c_str(),"wb");if(!output)return "Could not write controller support. Check SD space.";
    bool ok=fwrite(data.data(),1,data.size(),output)==data.size();if(fflush(output)!=0)ok=false;if(fsync(fileno(output))!=0)ok=false;if(fclose(output)!=0)ok=false;
    std::vector<unsigned char> readback(data.size());input=fopen(temp.c_str(),"rb");if(!input)ok=false;else{if(fread(readback.data(),1,readback.size(),input)!=readback.size()||readback!=data)ok=false;fclose(input);}
    if(!ok){remove(temp.c_str());return "Controller support could not be saved. Check SD space.";}
    if(stat(target.c_str(),&st)==0||rename(temp.c_str(),target.c_str())!=0){remove(temp.c_str());return "Controller module was not replaced. Restart Wii U or retry.";}
    return "Controller support installed. Restart Wii U to activate it.";
}
enum class Command { None, Pair, Cancel, Disconnect, ClearAll, Install };
class Manager {
public:
    Manager(){mutex_=SDL_CreateMutex();condition_=SDL_CreateCond();}
    ~Manager(){
        if(thread_){SDL_LockMutex(mutex_);stop_=true;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);SDL_WaitThread(thread_,nullptr);}
        if(condition_)SDL_DestroyCond(condition_);if(mutex_)SDL_DestroyMutex(mutex_);
    }
    Manager(const Manager&)=delete;Manager& operator=(const Manager&)=delete;
    void Open(){
        if(!mutex_||!condition_)return;
        if(!thread_)thread_=SDL_CreateThreadWithStackSize(Run,"uflow-controllers",128*1024,this);
        SDL_LockMutex(mutex_);open_=true;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);
    }
    void Close(){if(!thread_)return;SDL_LockMutex(mutex_);open_=false;command_=Command::Cancel;SDL_CondSignal(condition_);SDL_UnlockMutex(mutex_);}
    bool Submit(Command cmd,const std::string& address=""){
        if(!thread_)return false;
        SDL_LockMutex(mutex_);const bool ok=open_&&!state_.busy&&command_==Command::None;
        if(ok){command_=cmd;address_=address;state_.busy=true;SDL_CondSignal(condition_);}SDL_UnlockMutex(mutex_);return ok;
    }
    Snapshot Poll(){if(!mutex_)return {};SDL_LockMutex(mutex_);auto result=state_;SDL_UnlockMutex(mutex_);return result;}
private:
#ifndef UFLOW_HOST_PREVIEW
    inline static std::atomic<int> clearResult_{-1};
    using ClearCallback=void (*)(uint32_t);
    inline static ClearCallback previousClear_=nullptr;
    BOOL (*cancelSync_)()=nullptr;
    using SyncCallback=void (*)(int,uint32_t);
    inline static SyncCallback previousSync_=nullptr;
    inline static PairingSession pairingSession_;
    SyncCallback (*setSync_)(SyncCallback)=nullptr;
    static void SyncEvent(int event,uint32_t value){pairingSession_.Event(unsigned(event));if(previousSync_)previousSync_(event,value);}
    void (*startClear_)()=nullptr;
    ClearCallback (*setClear_)(ClearCallback)=nullptr;
    OSDynLoad_Module module_=0;
    void LoadPairingAPI(){
        if(OSDynLoad_Acquire("padscore.rpl",&module_)!=OS_DYNLOAD_OK)return;
        OSDynLoad_FindExport(module_,OS_DYNLOAD_EXPORT_FUNC,"WPADCancelSyncDevice",reinterpret_cast<void**>(&cancelSync_));
        OSDynLoad_FindExport(module_,OS_DYNLOAD_EXPORT_FUNC,"WPADSetSyncDeviceCallback",reinterpret_cast<void**>(&setSync_));
        OSDynLoad_FindExport(module_,OS_DYNLOAD_EXPORT_FUNC,"WPADStartClearDevice",reinterpret_cast<void**>(&startClear_));
        OSDynLoad_FindExport(module_,OS_DYNLOAD_EXPORT_FUNC,"WPADSetClearDeviceCallback",reinterpret_cast<void**>(&setClear_));
    }
    static void Cleared(uint32_t status){clearResult_.store(int(status));if(previousClear_)previousClear_(status);}
    static std::string Address(WPADChan channel){WPADAddress a{};WPADGetAddress(channel,&a);char text[13];snprintf(text,sizeof(text),"%02X%02X%02X%02X%02X%02X",a.btDeviceAddress[0],a.btDeviceAddress[1],a.btDeviceAddress[2],a.btDeviceAddress[3],a.btDeviceAddress[4],a.btDeviceAddress[5]);return text;}
    static const char* Name(unsigned type){switch(type){
        case BLOOPAIR_CONTROLLER_SWITCH_N64:return "Switch N64 controller";
        case BLOOPAIR_CONTROLLER_SWITCH_PRO:return "Switch Pro controller";
        case BLOOPAIR_CONTROLLER_DUALSENSE:return "DualSense";
        case BLOOPAIR_CONTROLLER_DUALSHOCK4:return "DualShock 4";
        case BLOOPAIR_CONTROLLER_DUALSHOCK3:return "DualShock 3";
        case BLOOPAIR_CONTROLLER_XBOX_ONE:return "Xbox One controller";
        case BLOOPAIR_CONTROLLER_XBOX_SERIES:return "Xbox Series controller";
        default:return "Bluetooth controller";
    }}
#endif
    static int Run(void* pointer){return static_cast<Manager*>(pointer)->Loop();}
    int Loop(){
        SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);Snapshot next;
#ifndef UFLOW_HOST_PREVIEW
        LoadPairingAPI();IOSHandle handle=Bloopair_Open();next.active=handle>=0&&Bloopair_IsActive(handle);
        if(next.active){const int version=Bloopair_GetVersion(handle);if(version>=0)next.version=std::to_string(BLOOPAIR_VERSION_MAJOR(version))+"."+std::to_string(BLOOPAIR_VERSION_MINOR(version))+"."+std::to_string(BLOOPAIR_VERSION_PATCH(version));}
        bool clearing=false;
        if(setSync_)previousSync_=setSync_(SyncEvent);
#endif
        next.ready=true;next.message=next.active?"Select a controller or pair a new one.":"Bloopair is not running. Install support, then restart Wii U.";
        Uint32 lastStored=0;std::vector<Device> stored;
        for(;;){
            SDL_LockMutex(mutex_);
            while(!stop_&&!open_&&!next.busy&&command_==Command::None)SDL_CondWait(condition_,mutex_);
            const bool stop=stop_,open=open_;const auto command=command_;const auto address=address_;command_=Command::None;
            SDL_UnlockMutex(mutex_);
#ifndef UFLOW_HOST_PREVIEW
            if(stop){pairingSession_.Cancel();if(next.pairing)cancelSync_();if(clearing)setClear_(previousClear_);break;}
            if(command==Command::Install&&!next.active&&!clearing)next.message=InstallSupport();
            else if(command==Command::Pair&&(!cancelSync_||!setSync_))next.message="Pairing controls unavailable on this system.";
            else if(command==Command::Pair&&!clearing){
                pairingSession_.Begin(SDL_GetTicks());next.pairing=WPADStartSyncDevice();
                if(!next.pairing)pairingSession_.Cancel();
                next.message=next.pairing?"Searching... put your controller in pairing mode.":"Could not start pairing. Try again.";
            }else if(command==Command::Cancel&&next.pairing){pairingSession_.Cancel();cancelSync_();next.pairing=false;next.message="Pairing cancelled.";}
            else if(command==Command::Disconnect&&!clearing){
                // Recheck identity: channel numbers can be reassigned while the UI is open.
                for(int i=0;i<7;++i){WPADExtensionType type;const auto channel=static_cast<WPADChan>(i);if(WPADProbe(channel,&type)==WPAD_ERROR_NONE&&Address(channel)==address)WPADDisconnect(channel);}
                next.message="Disconnect requested. Saved pairing is retained.";
            }else if(command==Command::ClearAll&&(!startClear_||!setClear_))next.message="Unpair controls unavailable on this system.";
            else if(command==Command::ClearAll&&!clearing){
                pairingSession_.Cancel();if(next.pairing)cancelSync_();next.pairing=false;
                clearResult_.store(-1);previousClear_=setClear_(Cleared);clearing=true;startClear_();next.message="Removing controller pairings...";
            }
            if(clearing&&clearResult_.load()!=-1){
                setClear_(previousClear_);clearing=false;
                next.message=clearResult_.load()==0?"Controller pairings removed. Pair controllers again to use them.":"Could not remove pairings. Try again.";lastStored=0;
            }
            const auto pairingResult=pairingSession_.Poll(SDL_GetTicks());
            if(next.pairing&&pairingResult!=PairingSession::Result::Waiting){
                pairingSession_.Cancel();cancelSync_();next.pairing=false;
                next.message=pairingResult==PairingSession::Result::TimedOut?"Pairing timed out. Check controller mode and try again.":"System search ended. Check the connected controllers below.";lastStored=0;
            }
            next.busy=clearing;
#else
            if(stop)break;next.message="Controller support is unavailable in the desktop preview.";
#endif
            if(open){
                if(!lastStored||SDL_GetTicks()-lastStored>=2000){stored=StoredDevices("fs:/vol/external01/wiiu/bloopair/devices");lastStored=SDL_GetTicks();}
                next.devices=stored;
#ifndef UFLOW_HOST_PREVIEW
                for(int i=0;i<7;++i){
                    const auto channel=static_cast<WPADChan>(i);WPADExtensionType type;
                    if(WPADProbe(channel,&type)!=WPAD_ERROR_NONE)continue;
                    const auto key=Address(channel);auto found=std::find_if(next.devices.begin(),next.devices.end(),[&](const Device& d){return d.address==key;});
                    if(found==next.devices.end()){next.devices.push_back({key,"Wii controller"});found=next.devices.end()-1;}
                    found->channel=i;found->battery=WPADGetBatteryLevel(channel);
                    BloopairControllerInformationData info{};
                    if(next.active&&Bloopair_GetControllerInformation(handle,channel,&info)>=0&&info.controllerType!=BLOOPAIR_CONTROLLER_OFFICIAL)found->name=Name(info.controllerType);
                    if(next.active&&info.controllerType!=BLOOPAIR_CONTROLLER_OFFICIAL){
                        BloopairReportBuffer report{};
                        if(Bloopair_ReadRawReport(handle,channel,&report)>=0){found->reportValid=true;found->buttons=report.buttons;found->leftX=report.left_stick_x;found->leftY=report.left_stick_y;found->rightX=report.right_stick_x;found->rightY=report.right_stick_y;}
                    }
                }
#endif
                std::stable_sort(next.devices.begin(),next.devices.end(),[](const Device&a,const Device&b){if((a.channel>=0)!=(b.channel>=0))return a.channel>=0;return a.address<b.address;});
            }
            SDL_LockMutex(mutex_);state_=next;if((open_||next.busy)&&!stop_&&command_==Command::None)SDL_CondWaitTimeout(condition_,mutex_,250);SDL_UnlockMutex(mutex_);
        }
#ifndef UFLOW_HOST_PREVIEW
        if(setSync_)setSync_(previousSync_);
        if(handle>=0)Bloopair_Close(handle);
        if(module_)OSDynLoad_Release(module_);
#endif
        return 0;
    }
    SDL_mutex* mutex_=nullptr;SDL_cond* condition_=nullptr;SDL_Thread* thread_=nullptr;
    bool open_=false,stop_=false;Command command_=Command::None;std::string address_;Snapshot state_;
};
}
