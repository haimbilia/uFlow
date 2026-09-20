#pragma once
#include <string>
#include <cstring>
inline bool RawSourceUSB(const char *path){return path&&strncmp(path,"usb1:/",6)==0;}
inline std::string RawSourcePath(const char *path){return RawSourceUSB(path)?std::string("ufusb:/")+(path+6):std::string("fs:/vol/external01/")+path;}
