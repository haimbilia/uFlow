#pragma once
#include "library.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <utility>
#ifndef UFLOW_HOST_PREVIEW
#include <coreinit/mcp.h>
#include <malloc.h>
#endif

// Launch-only, bounded diagnostics. No polling, metadata mutation or routing changes.
namespace LaunchDiagnostics {
inline constexpr const char *Path = "fs:/vol/external01/wiiu/apps/uFlow/launch-diagnostics.log";
inline std::string XmlValue(const std::string &xml, const char *tag) {
    const std::string prefix=std::string("<")+tag;
    auto start=xml.find(prefix);
    while(start!=std::string::npos){
        const auto next=start+prefix.size();
        if(next<xml.size() && (xml[next]=='>' || xml[next]==' ' || xml[next]=='\t' || xml[next]=='\n' || xml[next]=='\r'))break;
        start=xml.find(prefix,next);
    }
    if(start==std::string::npos) return "<absent>";
    start=xml.find('>',start);
    if(start==std::string::npos)return "<invalid>";
    ++start;
    const auto end=xml.find(std::string("</")+tag+">",start);
    if(end==std::string::npos || end-start>256) return "<invalid>";
    return xml.substr(start,end-start);
}
inline void Xml(FILE *log,const std::string &path) {
    errno=0;FILE *file=std::fopen(path.c_str(),"rb");
    if(!file){
        // Reuse the scanner's existing privileged mounts; never create a mount here.
        for(const auto &mapping:{std::pair{"fs:/vol/storage_mlc01/","ufmlc:/"},std::pair{"fs:/vol/storage_usb01/","ufwfs:/"}}){
            if(path.rfind(mapping.first,0)==0){
                const std::string fallback=std::string(mapping.second)+path.substr(std::strlen(mapping.first));
                file=std::fopen(fallback.c_str(),"rb");
                std::fprintf(log,"xml-read-alias=%s opened=%d\n",fallback.c_str(),file!=nullptr);
                break;
            }
        }
    }
    if(!file){std::fprintf(log,"xml path=%s open_errno=%d\n",path.c_str(),errno);return;}
    std::string xml(65536,'\0');const auto bytes=std::fread(xml.data(),1,xml.size(),file);
    const int error=std::ferror(file);std::fclose(file);xml.resize(bytes);
    std::fprintf(log,"xml path=%s bytes=%u read_error=%d",path.c_str(),static_cast<unsigned>(bytes),error);
    for(const char *tag:{"title_id","title_version","os_version","sdk_version","app_type","group_id"})
        std::fprintf(log," %s=%s",tag,XmlValue(xml,tag).c_str());
    std::fprintf(log,"\n");
}
inline std::string FsPath(const std::string &path) {
    if(path.rfind("/vol/",0)==0)return "fs:"+path;
    if(path.find(":/")!=std::string::npos)return path;
    return "fs:/vol/external01/"+path;
}
inline void Snapshot(const GameEntry &game) {
#ifndef UFLOW_HOST_PREVIEW
    if(game.platform!=Platform::WiiU)return;
    FILE *log=std::fopen(Path,"ab");if(!log)return;
    std::fprintf(log,"\nBEGIN launch-diag-v1 build=%s %s installed=%d title=%s installed_id=%016llx name=%s\nroot=%s storage=%s\nrpx=%s\ncode=%s\ncontent=%s\nsave=%s\n",__DATE__,__TIME__,game.installed,game.titleId.c_str(),static_cast<unsigned long long>(game.installedTitleId),game.name.c_str(),game.absolutePath.c_str(),game.storage.c_str(),game.rpxRelative.c_str(),game.codeRelative.c_str(),game.contentRelative.c_str(),game.saveRelative.c_str());
    Xml(log,game.absolutePath+"/code/app.xml");
    Xml(log,game.absolutePath+"/meta/meta.xml");
    if(!game.codeRelative.empty())Xml(log,FsPath(game.codeRelative)+"/app.xml");
    const int32_t handle=static_cast<int32_t>(MCP_Open());std::fprintf(log,"mcp-open=%d\n",handle);
    if(handle>=0){
        const int32_t count=static_cast<int32_t>(MCP_TitleCount(handle));std::fprintf(log,"mcp-count=%d\n",count);
        if(count>0 && count<=4096){
            const auto bytes=static_cast<size_t>(count)*sizeof(MCPTitleListType);
            auto *titles=static_cast<MCPTitleListType *>(memalign(0x40,bytes));
            if(titles){
                std::memset(titles,0,bytes);uint32_t received=count;
                const int32_t result=static_cast<int32_t>(MCP_TitleList(handle,&received,titles,bytes));
                std::fprintf(log,"mcp-list=%d received=%u\n",result,received);
                unsigned matches=0;
                const uint64_t id=game.installedTitleId?game.installedTitleId:std::strtoull(game.titleId.c_str(),nullptr,16);
                if(result>=0 && id)for(uint32_t i=0;i<std::min(received,static_cast<uint32_t>(count));++i){
                    const auto &t=titles[i];if(static_cast<uint32_t>(t.titleId)!=static_cast<uint32_t>(id))continue;
                    ++matches;
                    const std::string path(t.path,strnlen(t.path,sizeof(t.path)));
                    const std::string device(t.indexedDevice,strnlen(t.indexedDevice,sizeof(t.indexedDevice)));
                    std::fprintf(log,"matching-installed id=%016llx app_type=%u device=%s path=%s\n",static_cast<unsigned long long>(t.titleId),static_cast<unsigned>(t.appType),device.c_str(),path.c_str());
                    Xml(log,FsPath(path)+"/code/app.xml");Xml(log,FsPath(path)+"/meta/meta.xml");
                }
                std::fprintf(log,"matching-installed-count=%u (enumeration only; not proof of mounted content)\n",matches);std::free(titles);
            }else std::fprintf(log,"mcp-allocation-failed\n");
        }
        MCP_Close(handle);
    }
    std::fprintf(log,"END snapshot\n");std::fclose(log);
#else
    (void)game;
#endif
}
inline void Event(const std::string &event){
#ifndef UFLOW_HOST_PREVIEW
    if(FILE *log=std::fopen(Path,"ab")){std::fprintf(log,"event=%s\n",event.c_str());std::fclose(log);}
#else
    (void)event;
#endif
}
}
