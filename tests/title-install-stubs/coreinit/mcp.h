#pragma once
#include <cstdint>
struct MCPTitleListType{uint64_t titleId;int titleVersion;char indexedDevice[10];};
struct MCPInstallInfo{unsigned char data[0x27f];};
struct MCPInstallProgress{uint32_t inProgress;uint64_t tid,sizeTotal,sizeProgress;};
enum MCPInstallTarget{MCP_INSTALL_TARGET_MLC,MCP_INSTALL_TARGET_USB};
int MCP_Open();int MCP_Close(int);int MCP_TitleCount(int);int MCP_TitleList(int,uint32_t*,MCPTitleListType*,uint32_t);
int MCP_InstallSetTargetDevice(int,MCPInstallTarget);int MCP_InstallSetTargetUsb(int,int);int MCP_InstallGetInfo(int,const char*,MCPInstallInfo*);int MCP_InstallGetProgress(int,MCPInstallProgress*);
