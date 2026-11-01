/* Exercise the production keyboard parser/sampler with deterministic OS layouts. */
#include <SDL3/SDL.h>
#include <assert.h>
static bool french_layout;
static SDL_Scancode test_scancode_from_key(SDL_Keycode key, SDL_Keymod *mods) {
    (void)mods;
    if (french_layout) {
        switch (key) {
        case SDLK_A: return SDL_SCANCODE_Q;
        case SDLK_Q: return SDL_SCANCODE_A;
        case SDLK_W: return SDL_SCANCODE_Z;
        case SDLK_Z: return SDL_SCANCODE_W;
        case SDLK_M: return SDL_SCANCODE_SEMICOLON;
        default: break;
        }
    }
    return SDL_GetScancodeFromName(SDL_GetKeyName(key));
}
#define SDL_GetScancodeFromKey test_scancode_from_key
#include "../src/runtime_pad.c"
#undef SDL_GetScancodeFromKey

int ps4rtgpu_overlay_captures_input(void) { return 0; }
static int vibration_strength=100;
int ps4rtgpu_vibration_percent(void) { return vibration_strength; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
static void environment(const char *name, const char *value) {
#ifdef _WIN32
    assert(_putenv_s(name,value)==0);
#else
    assert(setenv(name,value,1)==0);
#endif
}
static PadData keyboard(const bool *keys) {
    PadData d={.left_x=128,.left_y=128,.right_x=128,.right_y=128};
    sample_keyboard(&d,keys);
    return d;
}
/* Exercise the production PS4 export through SDL without vibrating a physical device. */
static Uint16 motor_low, motor_high;
static bool rumble_ok=true;
static bool SDLCALL virtual_rumble(void *userdata, Uint16 low, Uint16 high) {
    (void)userdata;
    motor_low=low; motor_high=high;
    if (!rumble_ok) return SDL_SetError("Virtual rumble failure");
    return true;
}
static void vibration_test(void) {
    assert(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; desc.naxes=6; desc.nbuttons=15;
    desc.name="SO5 vibration test"; desc.Rumble=virtual_rumble;
    const SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    assert(id);
    gamepad=SDL_OpenGamepad(id); assert(gamepad);
    assert(SDL_GetBooleanProperty(SDL_GetGamepadProperties(gamepad),SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN,false));
    keyboard_only=false; sdl_ready=1; next_gamepad_scan=UINT64_MAX;
    assert(pad_init()==0 && pad_open(1,0,0,NULL)==PAD_HANDLE);
    const uint8_t motors[2]={255,64}, stopped[2]={0,0};
    assert(pad_vibration(42,motors)==ERR_INVALID_HANDLE);
    assert(pad_vibration(PAD_HANDLE,NULL)==ERR_INVALID_ARG);
    assert(vibration_requests==0);
    assert(pad_vibration(PAD_HANDLE,motors)==0);
    assert(motor_low==65535 && motor_high==64*257);
    vibration_strength=50;
    assert(pad_vibration(PAD_HANDLE,motors)==0);
    assert(motor_low==32767 && motor_high==64*257/2);
    vibration_strength=0;
    assert(pad_vibration(PAD_HANDLE,motors)==0 && motor_low==0 && motor_high==0);
    vibration_strength=100;
    assert(pad_vibration(PAD_HANDLE,motors)==0);
    assert(pad_vibration(PAD_HANDLE,stopped)==0 && motor_low==0 && motor_high==0);
    rumble_ok=false;
    assert(pad_vibration(PAD_HANDLE,motors)==0 && vibration_failed==1);
    rumble_ok=true;
    assert(pad_vibration(PAD_HANDLE,stopped)==0);
    SDL_CloseGamepad(gamepad); gamepad=NULL;
    assert(SDL_DetachVirtualJoystick(id));
    keyboard_only=true;
    assert(pad_vibration(PAD_HANDLE,motors)==0 && vibration_without_pad==1);
    assert(pad_close(PAD_HANDLE)==0);
    puts("Controller vibration: PS4 export, SDL motors, intensity, stop and failure diagnostics PASS");
}
int main(void) {
    assert(SDL_Init(0));
    environment("PS4RT_KEY_PROFILE","azerty");
    environment("PS4RT_KEY_MAP","");
    environment("PS4RT_KEY_AXES","");
    environment("PS4RT_PAD_MAP","");
    environment("PS4RT_INPUT_MODE","keyboard");
    french_layout=true;
    runtime_pad_refresh_keyboard();
    assert(keyboard_only && current_gamepad()==NULL);
    assert(key_axes[0]==SDL_SCANCODE_A && key_axes[2]==SDL_SCANCODE_W);
    bool keys[SDL_SCANCODE_COUNT]={0};
    keys[SDL_SCANCODE_A]=keys[SDL_SCANCODE_W]=true; /* French Q and Z */
    PadData d=keyboard(keys);
    assert(d.left_x==0 && d.left_y==0 && d.buttons==0);
    memset(keys,0,sizeof(keys));
    keys[SDL_SCANCODE_Q]=true; /* French A: Triangle, not movement */
    d=keyboard(keys);
    assert(d.buttons==BTN_TRIANGLE && d.left_x==128);

    environment("PS4RT_KEY_PROFILE","qwerty");
    runtime_pad_refresh_keyboard();
    assert(key_axes[0]==SDL_SCANCODE_Q && key_axes[2]==SDL_SCANCODE_Z);
    french_layout=false;
    runtime_pad_refresh_keyboard();
    assert(key_axes[0]==SDL_SCANCODE_A && key_axes[2]==SDL_SCANCODE_W);

    /* Custom mappings replace defaults and survive keyboard-layout refresh. */
    environment("PS4RT_KEY_PROFILE","custom");
    environment("PS4RT_KEY_MAP","cross=M,triangle=Left Bracket,l3=X");
    environment("PS4RT_KEY_AXES","left_up=Up,right_up=Z");
    runtime_pad_refresh_keyboard();
    memset(keys,0,sizeof(keys)); keys[SDL_SCANCODE_SPACE]=true;
    assert(!(keyboard(keys).buttons & BTN_CROSS));
    keys[SDL_SCANCODE_M]=true;
    assert(keyboard(keys).buttons & BTN_CROSS);
    french_layout=true;
    runtime_pad_refresh_keyboard();
    memset(keys,0,sizeof(keys)); keys[SDL_SCANCODE_SEMICOLON]=true;
    keys[SDL_SCANCODE_UP]=keys[SDL_SCANCODE_W]=true;
    d=keyboard(keys);
    assert((d.buttons & BTN_CROSS) && d.left_y==0 && d.right_y==0);
    memset(keys,0,sizeof(keys)); keys[SDL_SCANCODE_LEFTBRACKET]=true;
    assert(keyboard(keys).buttons==BTN_TRIANGLE);

    /* Quiet sticks are exactly neutral; full travel and monotonicity are preserved. */
    for (int v=-2621;v<=2621;++v) assert(axis((int16_t)v)==128);
    assert(axis(-32768)==0 && axis(32767)==255);
    uint8_t previous=0;
    for (int v=-32768;v<=32767;++v) {
        const uint8_t current=axis((int16_t)v);
        assert(current>=previous); previous=current;
    }
    uint8_t merged=200;
    key_axis(&merged,false,false); assert(merged==200); /* keep real pad input */
    key_axis(&merged,true,true); assert(merged==128);
    key_axis(&merged,true,false); assert(merged==0);
    key_axis(&merged,false,true); assert(merged==255);
    vibration_test();
    SDL_Quit();
    puts("Pad input: AZERTY/QWERTY, custom overrides, layout refresh, keyboard-only and stick dead zone PASS");
}
