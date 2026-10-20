#pragma once
#include "wii_request.hpp"
#include <cerrno>

// Narrow read-only probe for the exact requested path. No directory scan,
// fallback to SD, filesystem writes or persistent loader settings.
namespace WiiUsbProbe {
constexpr const char *Marker="UFLOW_USB_PROBE_V2";
enum class Result { Ready, NoFat, Missing, InvalidHeader, WrongId, ReadError, Ambiguous, InvalidRequest };
struct Partition { bool mounted; bool fat; const char *name; };
inline const char *Name(Result value) {
    switch(value) {
        case Result::Ready:return "ready";
        case Result::NoFat:return "no-fat-partition";
        case Result::Missing:return "file-missing";
        case Result::InvalidHeader:return "invalid-wbfs-header";
        case Result::WrongId:return "disc-id-mismatch";
        case Result::ReadError:return "file-read-error";
        case Result::Ambiguous:return "ambiguous-copies";
        default:return "invalid-request";
    }
}
template<class Inspect> Result Resolve(WiiRequest::Request &request, Inspect inspect, FILE *log) {
    if(!WiiRequest::IsUsbPath(request.path))return Result::Ready;
    if(!WiiRequest::ValidPath(request.path,request.kind)||!WiiRequest::ValidId(request.id))return Result::InvalidRequest;
    unsigned fats=0,matches=0;
    std::string selected;
    Result failure=Result::Missing;
    for(int n=1;n<=8;++n) {
        const auto part=inspect(n);
        if(log)fprintf(log,"partition=usb%d mounted=%d fs=%s\n",n,part.mounted,part.name?part.name:"");
        if(!part.mounted||!part.fat)continue;
        ++fats;
        const auto path="usb"+std::to_string(n)+":/"+request.path.substr(6);
        struct stat info{};errno=0;
        const int status=stat(path.c_str(),&info),statError=errno;
        if(log)fprintf(log,"probe_path=%s stat=%d errno=%d size=%llu mode=%o\n",path.c_str(),status,statError,
            static_cast<unsigned long long>(info.st_size),static_cast<unsigned>(info.st_mode));
        if(status!=0 || !S_ISREG(info.st_mode)) {
            if(status!=0 && statError!=ENOENT && statError!=ENOTDIR)failure=Result::ReadError;
            continue;
        }
        std::string actual;
        errno=0;
        const auto error=WiiRequest::CheckGame(path,{},nullptr,&actual,false);
        const int readError=errno;
        if(log)fprintf(log,"header=%s errno=%d actual_id=%s expected_id=%s\n",
            error.empty()?"passed":error.c_str(),readError,actual.c_str(),request.id.c_str());
        if(!error.empty()) {
            const auto reason=readError ? Result::ReadError : Result::InvalidHeader;
            if(static_cast<int>(reason)>static_cast<int>(failure))failure=reason;
        } else if(actual!=request.id) {
            if(static_cast<int>(Result::WrongId)>static_cast<int>(failure))failure=Result::WrongId;
        } else {++matches;selected=path;}
    }
    const auto result=matches>1?Result::Ambiguous:matches==1?Result::Ready:!fats?Result::NoFat:failure;
    if(result==Result::Ready)request.path=selected;
    if(log){fprintf(log,"usb_resolution=%s matches=%u fat_partitions=%u\n",Name(result),matches,fats);fflush(log);}
    return result;
}
template<class Inspect,class Remount> Result ResolveWithRetry(WiiRequest::Request &request,Inspect inspect,Remount remount,FILE *log) {
    auto result=Resolve(request,inspect,log);
    // Only mount failure merits a retry. Missing/corrupt/wrong games must not
    // trigger device resets or silently boot another copy of the same title.
    if(result==Result::NoFat) {
        if(log){fprintf(log,"usb_reinitialize=begin\n");fflush(log);}
        const bool mounted=remount();
        if(log)fprintf(log,"usb_reinitialize_mounted=%d\n",mounted);
        result=Resolve(request,inspect,log);
    }
    return result;
}
inline const wchar_t *Message(Result result) {
    switch(result) {
        case Result::NoFat:return L"uFlow: USB drive did not mount after retry. Check wii-launch.log.";
        case Result::Missing:return L"uFlow: USB mounted, but the selected file is missing. Check wii-launch.log.";
        case Result::WrongId:return L"uFlow: USB file has a different game ID. Check wii-launch.log.";
        case Result::InvalidHeader:return L"uFlow: USB WBFS header is invalid. Check wii-launch.log.";
        case Result::ReadError:return L"uFlow: USB file could not be read. Check wii-launch.log.";
        case Result::Ambiguous:return L"uFlow: Multiple USB copies match this path. Check wii-launch.log.";
        default:return L"uFlow: Invalid USB launch request. Check wii-launch.log.";
    }
}
}
