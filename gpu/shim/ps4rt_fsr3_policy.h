// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <cstdint>

namespace Ps4RtUpscaler {
// Project-specific, conservative default availability, not a Vulkan feature test
// or a promise of equal performance across drivers, power limits and games.
inline bool Fsr3Recommended(uint32_t vendor,std::string_view device) {
    std::string name(device);
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(name.find("LAPTOP")!=std::string::npos || name.find("MOBILE")!=std::string::npos || name.find("MAX-Q")!=std::string::npos) return false;
    const auto model=[&](const char* prefix) {
        const auto at=name.find(prefix);if(at==std::string::npos) return 0;
        size_t i=at+std::char_traits<char>::length(prefix);while(i<name.size() && name[i]==' ') ++i;
        int value=0;while(i<name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) value=value*10+name[i++]-'0';
        if(i<name.size() && name[i]=='M') return 0;
        return value;
    };
    if(vendor==0x10de) {
        const int gtx=model("GTX"),rtx=model("RTX");
        if(gtx==1080 || (name.find("TITAN XP")!=std::string::npos || name.find("TITAN X (PASCAL)")!=std::string::npos) || name.find("TITAN RTX")!=std::string::npos) return true;
        if(rtx==2060 && name.find("SUPER")!=std::string::npos) return true;
        const int generation=rtx/100,tier=rtx%100;
        return (generation==20 && tier>=70) || ((generation==30 || generation==40 || generation==50) && tier>=60);
    }
    if(vendor==0x1002) {
        const int rx=model("RX"),generation=rx/1000,tier=rx%1000;
        return (generation==5 && tier>=700) || (generation==6 && tier>=600) ||
               (generation==7 && tier>=600) || (generation==9 && tier>=60);
    }
    if(vendor==0x8086 && name.find("ARC")!=std::string::npos) {
        const int a=model("A"),b=model("B");
        // Match the Arc model, not the A in GRAPHICS. Real names contain A750,
        // A770, B570 or B580, with or without a separating space after Arc.
        return a==750 || a==770 || b==570 || b==580 ||
            name.find("A750")!=std::string::npos || name.find("A770")!=std::string::npos ||
            name.find("B570")!=std::string::npos || name.find("B580")!=std::string::npos;
    }
    return false;
}
inline std::string Fsr3DeviceKey(uint32_t vendor,uint32_t device,const uint8_t* uuid) {
    char prefix[24]{};std::snprintf(prefix,sizeof(prefix),"%04x-%04x-",vendor,device);
    std::string result=prefix;constexpr char hex[]="0123456789abcdef";
    for(int i=0;i<16;++i) {result+=hex[uuid[i]>>4];result+=hex[uuid[i]&15];}
    return result;
}
inline bool Fsr3Allowed(bool technical,bool recommended,std::string_view actual,std::string_view forced) {
    return technical && (recommended || (!actual.empty() && actual==forced));
}
}
