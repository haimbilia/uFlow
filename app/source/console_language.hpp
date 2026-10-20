#pragma once
#include <array>
#include <cstdint>
#include <string>
#ifdef __WIIU__
#include <coreinit/userconfig.h>
#include <cstring>
#endif
namespace ConsoleLanguage {
// Cafe language numbering and ACP metadata order.
inline constexpr std::array<const char *,12> Tags{
    "ja","en","fr","de","it","es","zhs","ko","nl","pt","ru","zht"};
inline uint32_t Normalize(uint32_t value) { return value<Tags.size()?value:1; }
#ifdef UFLOW_TEST_LANGUAGE
inline uint32_t testLanguage=1;
#endif
inline uint32_t Current() {
#ifdef UFLOW_TEST_LANGUAGE
    return Normalize(testLanguage);
#elif defined(__WIIU__)
    static const uint32_t language=[] {
        const auto handle=UCOpen();
        if(handle<0)return uint32_t{1};
        uint32_t value=1;
        UCSysConfig setting{};
        std::strcpy(setting.name,"cafe.language");
        setting.dataType=UC_DATATYPE_UNSIGNED_INT;
        setting.dataSize=sizeof(value);setting.data=&value;
        const auto result=UCReadSysConfig(handle,1,&setting);
        UCClose(handle);
        return result<0||setting.error<0?uint32_t{1}:Normalize(value);
    }();
    return language;
#else
    return 1;
#endif
}
template<class Read> std::string Select(Read read,uint32_t language=Current()) {
    auto value=read(Tags[Normalize(language)]);
    if(!value.empty())return value;
    for(unsigned i:{1u,0u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u}) {
        value=read(Tags[i]);if(!value.empty())return value;
    }
    return {};
}
}
