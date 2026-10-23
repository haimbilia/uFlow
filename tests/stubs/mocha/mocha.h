#pragma once
#include <string>
#include <vector>
constexpr int MOCHA_RESULT_SUCCESS=0;
inline std::vector<std::string> testStorageMounts;
inline int Mocha_InitLibrary(){return 0;}
inline int Mocha_MountFS(const char *name,const char *device,const char *volume){
    if(device)return -1;
    testStorageMounts.push_back(std::string(name)+"="+volume);
    return 0;
}
