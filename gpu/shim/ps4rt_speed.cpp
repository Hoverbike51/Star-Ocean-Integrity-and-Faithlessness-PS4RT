// SPDX-License-Identifier: GPL-2.0-or-later
// Optional hold-to-accelerate, adapted from the audited donor's speed control.
#include "ps4rt_speed.h"
#include <atomic>
#include <map>
#include <cstdlib>
#include <cstring>
#include <cstdio>
namespace {
std::atomic<unsigned> factor{1};std::atomic<bool> combo{false};
bool key=false,focused=true;std::map<SDL_JoystickID,unsigned> pads;
unsigned Setting(){static unsigned value=[] {const char* s=std::getenv("PS4RT_FAST_FORWARD");return s&&s[0]=='1'?1u:3u;}();return value;}
bool KeyAllowed(){static bool allowed=[] {const char* s=std::getenv("PS4RT_KEY_MAP"),*axes=std::getenv("PS4RT_KEY_AXES");return (!s||!std::strstr(s,"F2"))&&(!axes||!std::strstr(axes,"F2"));}();return allowed;}
bool PadAllowed(){static bool allowed=[] {const char* enabled=std::getenv("SO5_FAST_FORWARD_PAD"),*map=std::getenv("PS4RT_PAD_MAP");const char* mode=std::getenv("PS4RT_INPUT_MODE");return enabled&&enabled[0]=='1'&&(!map||!map[0])&&(!mode||std::strcmp(mode,"keyboard"));}();return allowed;}
void Publish(){bool held=false;for(auto [id,state]:pads)held|=state==3;combo=held&&PadAllowed();factor=(key||combo.load())?Setting():1;}
}
extern "C" unsigned ps4rtgpu_speed(void){return factor.load(std::memory_order_relaxed);}
extern "C" int ps4rtgpu_fast_forward_combo(void){return factor>1&&combo.load(std::memory_order_relaxed);}
namespace Ps4RtSpeed {
void Reset(){key=false;pads.clear();combo=false;factor=1;}
void Handle(const SDL_Event& event,bool captured) {
    if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST){focused=false;Reset();return;}
    if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED){focused=true;Reset();return;}
    if(captured||!focused){Reset();return;}
    if(Setting()==1)return;
    if((event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&event.key.key==SDLK_F2&&KeyAllowed())key=event.type==SDL_EVENT_KEY_DOWN;
    if(event.type==SDL_EVENT_GAMEPAD_REMOVED)pads.erase(event.gdevice.which);
    if(PadAllowed()) {
        if((event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN||event.type==SDL_EVENT_GAMEPAD_BUTTON_UP)&&event.gbutton.button==SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            auto& s=pads[event.gbutton.which];s=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?s|4u:s&~4u;
        }
        if(event.type==SDL_EVENT_GAMEPAD_AXIS_MOTION&&
           (event.gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER||event.gaxis.axis==SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) {
            auto& s=pads[event.gaxis.which];
            const unsigned bit=event.gaxis.axis==SDL_GAMEPAD_AXIS_RIGHT_TRIGGER?1u:2u;
            const bool down=event.gaxis.value>((s&bit)?12000:16384);
            s=down?s|bit:s&~bit;
        }
        // State bit 4 suppresses acceleration while R3 is held: L2+R3 belongs
        // to the menu, including noisy trigger events before it captures input.
    }
    Publish();
}
}
