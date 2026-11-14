// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shim/ps4rt_speed.h"
#include <windows.h>
#include <cassert>
#include <string>
extern "C" unsigned ps4rtgpu_speed(void);
extern "C" int ps4rtgpu_fast_forward_combo(void);
int main(int argc,char** argv) {
    _putenv_s("PS4RT_FAST_FORWARD","5");_putenv_s("SO5_FAST_FORWARD_PAD","1");
    const bool conflict=argc>1&&std::string(argv[1])=="conflict";
    _putenv_s("PS4RT_KEY_MAP",conflict?"cross=F2":"cross=Space");
    _putenv_s("PS4RT_PAD_MAP",conflict?"south=east":"");
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_F2;Ps4RtSpeed::Handle(event,false);assert(ps4rtgpu_speed()==(conflict?1u:5u));
    event.type=SDL_EVENT_WINDOW_FOCUS_LOST;Ps4RtSpeed::Handle(event,false);assert(ps4rtgpu_speed()==1);
    event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_F2;Ps4RtSpeed::Handle(event,false);assert(ps4rtgpu_speed()==1);
    event.type=SDL_EVENT_WINDOW_FOCUS_GAINED;Ps4RtSpeed::Handle(event,false);
    const auto trigger=[&](SDL_JoystickID id,Uint8 axis,Sint16 value) {
        SDL_Event e{};e.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;e.gaxis.which=id;e.gaxis.axis=axis;e.gaxis.value=value;Ps4RtSpeed::Handle(e,false);
    };
    const auto stick=[&](bool down) {
        SDL_Event e{};e.type=down?SDL_EVENT_GAMEPAD_BUTTON_DOWN:SDL_EVENT_GAMEPAD_BUTTON_UP;
        e.gbutton.which=42;e.gbutton.button=SDL_GAMEPAD_BUTTON_RIGHT_STICK;Ps4RtSpeed::Handle(e,false);
    };
    trigger(42,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,25000);assert(ps4rtgpu_speed()==1);
    stick(true);assert(ps4rtgpu_speed()==1); // Old R2+R3 shortcut is retired.
    stick(false);
    trigger(43,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,25000);assert(ps4rtgpu_speed()==1); // Same physical pad required.
    trigger(42,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,25000);
    assert(ps4rtgpu_speed()==(conflict?1u:5u));assert(bool(ps4rtgpu_fast_forward_combo())==!conflict);
    trigger(42,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,13000);assert(ps4rtgpu_speed()==(conflict?1u:5u)); // Hysteresis.
    stick(true);assert(ps4rtgpu_speed()==1); // In-game menu chord takes precedence.
    trigger(42,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,26000);assert(ps4rtgpu_speed()==1);
    stick(false);assert(ps4rtgpu_speed()==(conflict?1u:5u));
    trigger(42,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,0);assert(ps4rtgpu_speed()==1);
    trigger(42,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,25000);assert(ps4rtgpu_speed()==(conflict?1u:5u));
    event.type=SDL_EVENT_GAMEPAD_REMOVED;event.gdevice.which=42;Ps4RtSpeed::Handle(event,false);assert(ps4rtgpu_speed()==1);
    event.type=SDL_EVENT_KEY_DOWN;event.key.key=SDLK_F2;Ps4RtSpeed::Handle(event,true);assert(ps4rtgpu_speed()==1);
}
