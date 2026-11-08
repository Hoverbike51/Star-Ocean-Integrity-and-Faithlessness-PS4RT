#include "log_console.h"
// SPDX-License-Identifier: GPL-2.0-or-later
// Native Windows launcher for the game-specific PS4RT runtime.
#include <windows.h>
#include <windowsx.h>
#include <wtypes.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <winhttp.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "resource.h"
#include "native_preparer.h"
#include "game_languages.h"
#include "cheat_catalog.h"
#include "native_tweaks.h"
#include "../tools/hud/hud_preset_library.h"
#include "dlss_probe.h"
#include "localization.h"
#include "save_backup_guard.h"
#include "save_preview.h"
#include "ps4rt_version.h"
#include "updater.h"
#include "shortcuts.h"
#include "pkg/progress.h"
#include "../gpu/shim/ps4rt_upscaler_support.h"
#include "../gpu/shim/ps4rt_fsr3_policy.h"
#include "../gpu/shim/ps4rt_scene_preset.h"
#include "../gpu/shim/ps4rt_graphics.h"
#include "../gpu/shim/ps4rt_output.h"
#include "../gpu/shim/ps4rt_ultrawide.h"
#include "../gpu/shim/ps4rt_display.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <tuple>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;
namespace fs = std::filesystem;

namespace {
constexpr UINT WM_LOG_UPDATED = WM_APP + 1;
constexpr UINT WM_AUTOPLAY = WM_APP + 2;
constexpr UINT WM_GAME_STARTED = WM_APP + 3;
constexpr UINT WM_GAME_ENDED = WM_APP + 4;
constexpr UINT WM_UPDATE_DONE = WM_APP + 5;
constexpr UINT WM_CHEATS_READY = WM_APP + 6;
constexpr UINT WM_QUICK_FAILED = WM_APP + 7;
constexpr wchar_t kClassName[] = L"PS4RTStarOceanLauncher";
constexpr wchar_t kVersion[] = PS4RT_VERSION_WSTRING;
constexpr int kHeaderHeight = 104;
constexpr int kDragHeight = 32;
constexpr int kMinimizeButton = 500;
constexpr int kCloseButton = 501;
constexpr int kBindingHitBase = 3000;

enum class Page { Home, Game, Cheats, Video, Controls, Advanced, Logs, Tweaks };
enum class GameIdentity { None, Tested, Untested, Rejected };
enum class Indicator { Red, Orange, Green };
enum Hit {
    HitNone, HitClose, HitMinimize,
    HitTweaks, HitLeaderEnabled, HitCameraFollow, HitHudPreset, HitHudRefresh, HitHudOpenPresets,
    HitHome, HitGame, HitCheats, HitCheatsToggle, HitFastForward, HitFastForwardPad, HitVideo, HitControls, HitAdvanced, HitLogs,
    HitFolder, HitPkg, HitPrepare, HitPlay, HitStop,
    HitSaveFolder, HitDefaultSaveFolder, HitOpenSaves, HitBackupToggle,
    HitOpenBackups, HitImportSave, HitShortcut, HitGameLanguage,
    HitGpu, HitMonitor, HitOutputAspect, HitDisplayMode, HitAspectRatio, HitVsync, HitFpsLimit, HitFsr, HitResolution, HitNativeRender,
    HitAnisotropy, HitAsyncShaders, HitForceFsr3,
    HitController, HitKeyboardProfile, HitResetControls,
    HitLauncherLanguage, HitUpdateToggle, HitCheckUpdate,
    HitPerformanceOverlay, HitOverlayFps, HitOverlayFrameTime, HitOverlayUpscaler,
    HitOverlayGpu, HitOverlayCpu, HitOverlayRam, HitOverlayVram, HitOverlayVersion,
    HitOverlayGpuModel, HitOverlayCpuModel,
    HitOverlayScale, HitOverlayCorner, HitOverlayOpacity, HitOverlayLayout,
    HitOpenCache, HitClearCache, HitOpenRoot, HitRefresh,
    HitExportLogs, HitClearLogs, HitPerformanceTools, HitPerformanceCapture, HitOpenPerformanceCaptures,
    HitLogOlder, HitLogNewer, HitLogPageOlder, HitLogPageNewer,
    HitModalPrimary, HitModalSecondary, HitModalTertiary, HitModalBackdrop
};

enum class ModalAction { None, ClearCache, CloseIdle, CloseGame, ForceCloseGame, ConfirmExtraction, CancelExtraction,
                         UpdateOffer, UpdateLink, UpdateProgress, ForceFsr3 };

struct UpdateEvent {
    enum Kind { Checked, Prepared, Installer } kind = Checked;
    So5Update::CheckResult checked;
    std::optional<So5Update::Prepared> prepared;
    So5Update::Code code = So5Update::Code::Current;
    std::string detail;
};

struct CheatInspection {
    So5Cheats::Result result; std::string error; unsigned generation=0;
};

struct AppState {
    HWND window{};
    bool quick_launch = false;
    HWND minimize_button{}, close_button{}, tooltip{};
    DWORD game_pid = 0;
    bool game_running = false;
    bool auto_minimized = false;
    bool waiting_for_game_window = false;
    bool close_when_idle = false;
    ULONGLONG close_deadline = 0;
    ULONG_PTR gdiplus_token{};
    std::unique_ptr<Image> background;
    std::unique_ptr<Image> launcher_icon;
    std::unique_ptr<Image> game_logo;
    std::unique_ptr<Image> save_preview;
    fs::path root;
    fs::path config;
    std::wstring game_path;
    std::unique_ptr<SaveBackupGuard> backup_guard;
    std::wstring detected_game_title,detected_game_version;
    std::wstring save_root;
    std::wstring pkg_extract_root;
    std::wstring fsr = L"FSR 3.1";
    std::wstring resolution = L"1920 × 1080";
    Ps4RtDisplay::Target target_display;
    std::vector<Ps4RtDisplay::Target> displays;
    std::wstring selected_display_id;
    int scene_preset=-1;
    int native_render_height=1080;
    int output_aspect=Ps4RtOutput::All;
    bool shader_cache_present=false, runtime_present=false;
    std::wstring display_mode = L"borderless";
    bool window_stretch = false;
    std::wstring game_language = L"Français";
    std::vector<std::wstring> game_languages;
    std::wstring launcher_language = L"Français";
    std::wstring keyboard_profile = L"AZERTY";
    std::wstring controller = L"Automatique · SDL3";
    struct GraphicsAdapter {
        int runtime_index;
        std::wstring label;
        bool fsr4_supported{}, discrete{}, cpu{};
        uint64_t local_memory{};
        bool fsr3_technical{},fsr3_recommended{},dlss_supported{};
        std::string device_key;
    };
    std::vector<GraphicsAdapter> graphics_adapters;
    int gpu_index = -1;
    std::wstring force_fsr3_gpu;
    std::array<std::wstring, 25> key_bindings{};
    std::vector<std::wstring> detected_controllers;
    int capture_binding = -1;
    std::wstring save_title;
    std::wstring save_subtitle;
    std::wstring save_detail;
    std::wstring save_date;
    std::wstring status = L"Sélectionnez le dossier de votre jeu PS4";
    std::wstring detected_title_id;
    std::wstring detected_region;
    std::wstring modal_title;
    std::wstring modal_body;
    std::wstring modal_primary;
    std::wstring modal_secondary;
    std::wstring modal_tertiary;
    ModalAction modal_action = ModalAction::None;
    bool modal_visible = false;
    bool extraction_active = false;
    std::atomic_bool extraction_cancel_requested{false};
    ULONGLONG extraction_started = 0;
    std::wstring extraction_source;
    std::wstring extraction_destination;
    std::uintmax_t extraction_size = 0;
    std::atomic<std::uint64_t> extraction_done{0}, extraction_total{0};
    std::wstring extraction_title; // Protected by mutex; presented on the UI thread.
    fs::path pending_pkg;
    std::vector<std::wstring> logs;
    So5Log::RetainedDiagnostics retained_diagnostics;
    std::wstring log_stream_pending;
    std::wstring last_update_log;
    std::atomic_bool log_update_pending{false};
    std::atomic<uint64_t> logs_revision{0};
    uint64_t native_logs_revision=UINT64_MAX;
    int log_scroll_lines = 0;
    int log_scroll_max = 0;
    std::vector<fs::path> saves;
    int selected_save = -1;
    int fps_limit = 60;
    int anisotropy = 16;
    Page page = Page::Home;
    GameIdentity game_identity = GameIdentity::None;
    bool native_preparer_ok = true;
    bool vulkan_ok = false;
    bool prepared = false;
    bool pkg_selected = false;
    bool busy = false;
    bool vsync = true;
    bool async_shaders = true;
    bool automatic_save_backups = true;
    bool update_on_start = true;
    bool performance_tools = false;
    bool performance_capture = false;
    LogConsole log_console;
    bool performance_overlay = false;
    bool overlay_fps = true;
    bool overlay_frametime = true;
    bool overlay_upscaler = true;
    bool overlay_gpu = false;
    bool overlay_cpu = false;
    bool overlay_gpu_model = true;
    bool overlay_cpu_model = false;
    bool overlay_ram = false;
    bool overlay_vram = false;
    bool overlay_version = false;
    int overlay_scale = 100;
    int overlay_corner = 1;
    int overlay_opacity = 50;
    int overlay_layout = 0;
    bool game_verified = false;
    HANDLE job{};
    std::mutex mutex;
    std::vector<std::pair<int, RECT>> hits;
    int hover = HitNone;
    int popup_hit = HitNone;
    int popup_scroll = 0;
    std::vector<std::wstring> popup_values;
    std::vector<bool> popup_enabled;
    bool cheats_enabled=false,fast_forward=true,fast_forward_pad=false;
    std::vector<So5Cheats::Entry> cheat_entries;
    std::thread cheat_thread; std::mutex cheat_mutex; std::unique_ptr<CheatInspection> cheat_result;
    unsigned cheat_generation=0; bool cheat_loading=false,cheat_refresh_pending=false;
    std::array<int, 8> scroll{};
    So5Tweaks::Settings tweaks;
    std::unique_ptr<So5Hud::PresetLibrary> hud_library;
    So5Hud::LibrarySnapshot hud_snapshot;
    So5Hud::Aspect hud_aspect=So5Hud::Aspect::Standard;
    std::wstring hud_selection;
    bool clipping_content_hits = false;
    int content_bottom = 0;
    std::thread update_thread;
    std::mutex update_mutex;
    std::unique_ptr<UpdateEvent> update_event;
    std::optional<So5Update::CheckResult> update_notice;
    std::optional<So5Update::Release> update_release;
    std::optional<So5Update::Prepared> update_prepared;
    std::atomic_bool update_cancel{false};
    std::atomic<So5Update::Phase> update_phase{So5Update::Phase::Download};
    std::atomic<std::uint64_t> update_done{0}, update_total{0};
    bool update_running = false, update_interactive = false, update_transfer = false, close_after_update = false;
    std::optional<fs::path> update_cleanup;
};

AppState app;
bool selected_gpu_supports_fsr3();
bool selected_gpu_supports_fsr4();
bool selected_gpu_supports_dlss();

// GDI+ objects must be released before GdiplusShutdown, on the UI thread.
struct DrawingCache {
    std::map<std::tuple<std::wstring, int, int>, std::unique_ptr<Font>> fonts;
    struct Artwork { Image* source; int width, height; bool cover; std::unique_ptr<Bitmap> bitmap; };
    std::vector<Artwork> artwork;
    std::unique_ptr<Bitmap> canvas;
    void Clear() { canvas.reset(); artwork.clear(); fonts.clear(); }
} drawing_cache;

constexpr std::array<const wchar_t*, 25> kBindingIniNames = {{
    L"Cross", L"Circle", L"Square", L"Triangle", L"L1", L"R1", L"L2", L"R2", L"L3", L"R3",
    L"Options", L"TouchpadLeft", L"TouchpadRight", L"DpadUp", L"DpadDown", L"DpadLeft", L"DpadRight",
    L"LeftStickLeft", L"LeftStickRight", L"LeftStickUp", L"LeftStickDown",
    L"RightStickLeft", L"RightStickRight", L"RightStickUp", L"RightStickDown"
}};

constexpr std::array<const wchar_t*, 25> kAzertyDefaults = {{
    L"Space", L"Left Shift", L"E", L"A", L"1", L"3", L"R", L"F", L"X", L"C",
    L"Return", L"Tab", L"Backspace", L"I", L"K", L"J", L"L",
    L"Q", L"D", L"Z", L"S", L"Left", L"Right", L"Up", L"Down"
}};

constexpr std::array<const wchar_t*, 25> kQwertyDefaults = {{
    L"Space", L"Left Shift", L"E", L"Q", L"1", L"3", L"R", L"F", L"X", L"C",
    L"Return", L"Tab", L"Backspace", L"I", L"K", L"J", L"L",
    L"A", L"D", L"W", L"S", L"Left", L"Right", L"Up", L"Down"
}};

void apply_keyboard_defaults(bool azerty) {
    const auto& defaults = azerty ? kAzertyDefaults : kQwertyDefaults;
    for (size_t i = 0; i < defaults.size(); ++i) app.key_bindings[i] = defaults[i];
}

struct UiWords {
    const wchar_t* home; const wchar_t* game; const wchar_t* video; const wchar_t* controls;
    const wchar_t* advanced; const wchar_t* logs; const wchar_t* play; const wchar_t* select_game;
};

UiWords words() {
    const std::wstring& l = app.launcher_language;
    if (l == L"English") return {L"Home",L"Game",L"Video",L"Controls",L"Advanced",L"Logs & diagnostics",L"PLAY",L"SELECT GAME"};
    if (l == L"Español") return {L"Inicio",L"Juego",L"Vídeo",L"Controles",L"Avanzado",L"Registro y diagnóstico",L"JUGAR",L"SELECCIONAR JUEGO"};
    if (l == L"Português") return {L"Início",L"Jogo",L"Vídeo",L"Controlos",L"Avançado",L"Registo e diagnóstico",L"JOGAR",L"SELECIONAR JOGO"};
    if (l == L"Português (Brasil)") return {L"Início",L"Jogo",L"Vídeo",L"Controles",L"Avançado",L"Registro e diagnóstico",L"JOGAR",L"SELECIONAR JOGO"};
    if (l == L"Italiano") return {L"Home",L"Gioco",L"Video",L"Comandi",L"Avanzate",L"Registro e diagnostica",L"GIOCA",L"SELEZIONA GIOCO"};
    if (l == L"Deutsch") return {L"Start",L"Spiel",L"Video",L"Steuerung",L"Erweitert",L"Protokoll und Diagnose",L"SPIELEN",L"SPIEL AUSWÄHLEN"};
    if (l == L"Русский") return {L"Главная",L"Игра",L"Видео",L"Управление",L"Дополнительно",L"Журнал и диагностика",L"ИГРАТЬ",L"ВЫБРАТЬ ИГРУ"};
    if (l == L"简体中文") return {L"主页",L"游戏",L"视频",L"控制",L"高级",L"日志与诊断",L"开始游戏",L"选择游戏"};
    if (l == L"繁體中文") return {L"首頁",L"遊戲",L"影像",L"控制",L"進階",L"日誌與診斷",L"開始遊戲",L"選擇遊戲"};
    if (l == L"한국어") return {L"홈",L"게임",L"비디오",L"컨트롤",L"고급",L"로그 및 진단",L"플레이",L"게임 선택"};
    if (l == L"日本語") return {L"ホーム",L"ゲーム",L"ビデオ",L"操作",L"詳細",L"ログと診断",L"プレイ",L"ゲームを選択"};
    return {L"Accueil",L"Jeu",L"Vidéo",L"Commandes",L"Avancé",L"Journal et diagnostic",L"JOUER",L"SÉLECTIONNER LE JEU"};
}

const wchar_t* loc(const wchar_t* french, const wchar_t* english) {
    return app.launcher_language == L"Français" ? french : ps4rt_localize(app.launcher_language, english);
}

void sync_chrome() {
    if (!app.minimize_button) return;
    const wchar_t* labels[] = {loc(L"Réduire", L"Minimize"), loc(L"Fermer le lanceur", L"Close launcher")};
    const HWND controls[] = {app.minimize_button, app.close_button};
    for (size_t i = 0; i < std::size(controls); ++i) {
        SetWindowTextW(controls[i], labels[i]);
        TOOLINFOW tool{sizeof(tool)};
        tool.hwnd = app.window;
        tool.uId = reinterpret_cast<UINT_PTR>(controls[i]);
        tool.lpszText = const_cast<wchar_t*>(labels[i]);
        SendMessageW(app.tooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

std::wstring display_mode_label() {
    if (app.display_mode == L"windowed" || app.display_mode == L"Fenêtré" || app.display_mode == L"Windowed") return loc(L"Fenêtré",L"Windowed");
    if (app.display_mode == L"fullscreen" || app.display_mode == L"Plein écran" || app.display_mode == L"Fullscreen") return loc(L"Plein écran",L"Fullscreen");
    return loc(L"Sans bordure",L"Borderless");
}

std::wstring fsr_label() {
    return app.fsr == L"off" || app.fsr == L"Désactivé" || app.fsr == L"Disabled" ? loc(L"Désactivé",L"Disabled") : app.fsr;
}

std::wstring overlay_corner_label() {
    switch (app.overlay_corner) {
    case 0: return loc(L"En haut à gauche",L"Top left");
    case 2: return loc(L"En bas à gauche",L"Bottom left");
    case 3: return loc(L"En bas à droite",L"Bottom right");
    case 4: return loc(L"En haut au milieu",L"Top center");
    case 5: return loc(L"En bas au milieu",L"Bottom center");
    default: return loc(L"En haut à droite",L"Top right");
    }
}

std::wstring overlay_layout_label() {
    return app.overlay_layout == 1 ? loc(L"Une donnée par ligne",L"One item per line")
                                   : loc(L"Une seule ligne",L"One line");
}

std::wstring controller_label() {
    if (app.controller == L"Automatique · SDL3" || app.controller == L"Automatic · SDL3") return loc(L"Automatique · SDL3",L"Automatic · SDL3");
    if (app.controller == L"Clavier" || app.controller == L"Keyboard") return loc(L"Clavier",L"Keyboard");
    return app.controller;
}

std::wstring keyboard_profile_label() {
    return app.keyboard_profile == L"Custom" || app.keyboard_profile == L"Personnalisé" ? loc(L"Personnalisé",L"Custom") : app.keyboard_profile;
}

const wchar_t* font_for(const std::wstring& value) {
    bool cjk = false;
    for (const wchar_t c : value) {
        if (c >= 0xac00 && c <= 0xd7af) return L"Malgun Gothic";
        if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x31f0 && c <= 0x31ff)) return L"Yu Gothic UI";
        if (c >= 0x3400 && c <= 0x9fff) cjk = true;
    }
    return cjk ? L"Microsoft YaHei UI" : L"Segoe UI";
}

std::wstring quote(const std::wstring& text) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(c);
        }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring from_utf8(const char* data, int length) {
    if (!length) return {};
    UINT cp = CP_UTF8;
    int count = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, data, length, nullptr, 0);
    if (!count) { cp = CP_ACP; count = MultiByteToWideChar(cp, 0, data, length, nullptr, 0); }
    std::wstring out(count, L'\0');
    MultiByteToWideChar(cp, 0, data, length, out.data(), count);
    return out;
}

int append_complete_log_lines_locked(const std::wstring& text, bool include_tail) {
    int added = 0;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find_first_of(L"\r\n", at);
        if (end == std::wstring::npos && !include_tail) break;
        std::wstring line = text.substr(at, end == std::wstring::npos ? text.size() - at : end - at);
        if (!line.empty()) {
            app.logs.push_back(So5Log::Stamp(line));
            ++added;
        }
        if (end == std::wstring::npos) break;
        at = end + 1;
        if (text[end] == L'\r' && at < text.size() && text[at] == L'\n') ++at;
    }
    if (app.log_scroll_lines > 0) app.log_scroll_lines += added;
    if (app.logs.size() > 1200) {
        for(size_t i=0;i<400;++i) app.retained_diagnostics.Keep(app.logs[i]);
        app.logs.erase(app.logs.begin(), app.logs.begin() + 400);
    }
    if(added) ++app.logs_revision;
    return added;
}

void notify_log_updated() {
    // Keep every log line, but queue at most one outstanding UI notification.
    if (!app.window || app.log_update_pending.exchange(true)) return;
    if (!PostMessageW(app.window, WM_LOG_UPDATED, 0, 0)) app.log_update_pending = false;
}

void append_log(const std::wstring& text) {
    std::lock_guard guard(app.mutex);
    append_complete_log_lines_locked(text, true);
}

// stdout/stderr pipe reads can end in the middle of a word. Keep the unfinished tail until the
// child process emits a real line ending instead of presenting arbitrary pipe chunks as lines.
void append_log_stream(const std::wstring& chunk) {
    std::lock_guard guard(app.mutex);
    app.log_stream_pending += chunk;
    const size_t last_lf = app.log_stream_pending.find_last_of(L"\r\n");
    if (last_lf == std::wstring::npos) {
        // Avoid unbounded growth if a third-party tool writes a very long line without a newline.
        if (app.log_stream_pending.size() > 32768) {
            append_complete_log_lines_locked(app.log_stream_pending, true);
            app.log_stream_pending.clear();
        }
        return;
    }
    size_t consumed = last_lf + 1;
    if (app.log_stream_pending[last_lf] == L'\r' && consumed < app.log_stream_pending.size() &&
        app.log_stream_pending[consumed] == L'\n') ++consumed;
    append_complete_log_lines_locked(app.log_stream_pending.substr(0, consumed), true);
    app.log_stream_pending.erase(0, consumed);
}

void flush_log_stream() {
    std::lock_guard guard(app.mutex);
    if (!app.log_stream_pending.empty()) {
        append_complete_log_lines_locked(app.log_stream_pending, true);
        app.log_stream_pending.clear();
    }
}

void show_modal(const std::wstring& title, const std::wstring& body,
                const std::wstring& primary, const std::wstring& secondary = {},
                ModalAction action = ModalAction::None, const std::wstring& tertiary = {}) {
    {
        std::lock_guard guard(app.mutex);
        app.modal_title = title;
        app.modal_body = body;
        app.modal_primary = primary;
        app.modal_secondary = secondary;
        app.modal_tertiary = tertiary;
        app.modal_action = action;
        app.modal_visible = true;
    }
    notify_log_updated();
}

fs::path executable_directory() {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path();
}

fs::path find_root() {
    const fs::path executable = executable_directory();
    // Installed root/bin and local out/gpu builds share the same assets.
    // Resolve from the executable only; never depend on the working directory.
    auto directory=executable;
    for(int level=0;level<3;++level,directory=directory.parent_path())
        if(fs::is_directory(directory/L"launcher/assets"))return directory;
    return executable;
}

fs::path program_path(const fs::path& root, const wchar_t* name) {
    for (const auto& directory : {root / "bin", root, root / "out"}) {
        const auto candidate=directory / name;
        if (fs::is_regular_file(candidate)) return candidate;
    }
    return root / "bin" / name;
}

fs::path runtime_path() {
    return program_path(app.root,L"Star Ocean 5.exe");
}

fs::path pkg_extractor_path() {
    return program_path(app.root,L"PkgExtractor.exe");
}

fs::path pkg_import_root() {
    if (!app.pkg_extract_root.empty()) return fs::path(app.pkg_extract_root);
    PWSTR value = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &value))) {
        result = fs::path(value) / "PS4RT" / "Games" / "Imports";
        CoTaskMemFree(value);
    } else result = app.root / "out" / "imports";
    return result;
}

fs::path performance_capture_directory() {
    return app.root / "out" / "CUSA03219" / "user" / "gpu" / "performance";
}

std::wstring find_program(const wchar_t* name) {
    std::wstring buffer(32768, L'\0');
    DWORD length = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length && length < buffer.size()) { buffer.resize(length); return buffer; }
    return {};
}

void detect_environment() {
    app.native_preparer_ok = true;
    app.vulkan_ok = false;
    app.graphics_adapters.clear();

    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loader_version) != VK_SUCCESS) return;

    const VkApplicationInfo application{
        VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "STAR OCEAN 5 Launcher", VK_MAKE_VERSION(PS4RT_VERSION_MAJOR, PS4RT_VERSION_MINOR, PS4RT_VERSION_PATCH),
        "PS4RT", VK_MAKE_VERSION(PS4RT_VERSION_MAJOR, PS4RT_VERSION_MINOR, PS4RT_VERSION_PATCH), std::min(loader_version, VK_API_VERSION_1_3)};
    const VkInstanceCreateInfo create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0,
                                           &application, 0, nullptr, 0, nullptr};
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) return;

    uint32_t count = 0;
    if (vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count) {
        std::vector<VkPhysicalDevice> devices(count);
        if (vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS) {
            for (uint32_t i = 0; i < count; ++i) {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(devices[i], &properties);
                if (properties.apiVersion < VK_API_VERSION_1_3) continue;
                std::wstring name = from_utf8(properties.deviceName,
                    static_cast<int>(std::strlen(properties.deviceName)));
                VkPhysicalDeviceMemoryProperties memory{};
                vkGetPhysicalDeviceMemoryProperties(devices[i],&memory);
                uint64_t local=0;
                for(uint32_t heap=0;heap<memory.memoryHeapCount;++heap)
                    if(memory.memoryHeaps[heap].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                        local=std::max(local,uint64_t(memory.memoryHeaps[heap].size));
                app.graphics_adapters.push_back({static_cast<int>(i),
                    L"GPU " + std::to_wstring(i) + L" · " + name,
                    Ps4RtUpscaler::ProbeFsr4(devices[i]).Supported(),
                    properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                    properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU,local});
                VkPhysicalDeviceIDProperties identity{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
                VkPhysicalDeviceProperties2 physical{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&identity};
                vkGetPhysicalDeviceProperties2(devices[i],&physical);
                auto& adapter=app.graphics_adapters.back();
                adapter.dlss_supported=So5ProbeDlss(instance,devices[i],program_path(app.root,L"Star Ocean 5.exe").parent_path());
                adapter.fsr3_technical=Ps4RtUpscaler::ProbeFsr3(devices[i]);
                adapter.fsr3_recommended=Ps4RtUpscaler::Fsr3Recommended(properties.vendorID,properties.deviceName);
                adapter.device_key=Ps4RtUpscaler::Fsr3DeviceKey(properties.vendorID,properties.deviceID,identity.deviceUUID);
            }
        }
    }
    vkDestroyInstance(instance, nullptr);
    app.vulkan_ok = !app.graphics_adapters.empty();
    const bool selected_present = std::any_of(app.graphics_adapters.begin(), app.graphics_adapters.end(),
        [](const AppState::GraphicsAdapter& adapter) { return adapter.runtime_index == app.gpu_index; });
    if (!selected_present) app.gpu_index = -1;
    // Never display a remembered provider as selectable when this adapter policy
    // excludes it, including on first startup or after a device rescan.
    if(app.fsr==L"FSR 4" && !selected_gpu_supports_fsr4()) app.fsr=selected_gpu_supports_fsr3()?L"FSR 3.1":L"off";
    if(app.fsr==L"FSR 3.1" && !selected_gpu_supports_fsr3()) app.fsr=L"off";
}

std::wstring gpu_label() {
    if (app.gpu_index < 0) return loc(L"Automatique · recommandé", L"Automatic · recommended");
    const auto selected = std::find_if(app.graphics_adapters.begin(), app.graphics_adapters.end(),
        [](const AppState::GraphicsAdapter& adapter) { return adapter.runtime_index == app.gpu_index; });
    return selected == app.graphics_adapters.end()
        ? loc(L"Automatique · recommandé", L"Automatic · recommended") : selected->label;
}

const AppState::GraphicsAdapter* selected_adapter() {
    if(app.graphics_adapters.empty()) return nullptr;
    if(app.gpu_index>=0) {
        const auto found=std::find_if(app.graphics_adapters.begin(),app.graphics_adapters.end(),
            [](const auto& device){return device.runtime_index==app.gpu_index;});
        return found!=app.graphics_adapters.end()?&*found:nullptr;
    }
    // Same preference order as the runtime: valid API, discrete, non-CPU, local memory.
    const auto selected=std::max_element(app.graphics_adapters.begin(),app.graphics_adapters.end(),
        [](const auto& a,const auto& b){
            if(a.discrete!=b.discrete) return !a.discrete;
            if(a.cpu!=b.cpu) return a.cpu;
            return a.local_memory<b.local_memory;
        });
    return &*selected;
}
bool selected_gpu_supports_dlss() {const auto* gpu=selected_adapter();return gpu && gpu->dlss_supported;}
bool selected_gpu_supports_fsr4() {const auto* gpu=selected_adapter();return gpu && gpu->fsr4_supported;}
bool fsr3_forced() {
    const auto* gpu=selected_adapter();
    return gpu && !gpu->fsr3_recommended && gpu->fsr3_technical &&
        app.force_fsr3_gpu==std::wstring(gpu->device_key.begin(),gpu->device_key.end());
}
bool selected_gpu_supports_fsr3() {
    const auto* gpu=selected_adapter();return gpu && gpu->fsr3_technical && (gpu->fsr3_recommended || fsr3_forced());
}

void reject_incompatible_fsr4() {
    app.fsr=selected_gpu_supports_fsr3()?L"FSR 3.1":L"off";
    append_log(L"Upscaler: incompatible FSR 4 selection rejected. Selected provider: "+app.fsr);
    show_modal(loc(L"Reconstruction incompatible",L"Incompatible upscaler"),
        std::wstring(loc(L"FSR 4 n’est pas compatible avec les fonctions Vulkan du GPU sélectionné.",
            L"FSR 4 is incompatible with the selected GPU's Vulkan capabilities."))+L"\n\n"+
        loc(L"Reconstruction",L"Upscaling")+L": "+(app.fsr==L"off"?loc(L"Désactivé",L"Disabled"):app.fsr),L"OK");
}

std::vector<std::wstring> gpu_selector_values() {
    std::vector<std::wstring> values{loc(L"Automatique · recommandé", L"Automatic · recommended")};
    for (const auto& adapter : app.graphics_adapters) values.push_back(adapter.label);
    return values;
}

void refresh_controllers() {
    app.detected_controllers.clear();
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    SDL_UpdateGamepads();
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        const char* raw = SDL_GetGamepadNameForID(ids[i]);
        std::wstring name = raw && *raw ? from_utf8(raw, static_cast<int>(std::strlen(raw)))
                                       : std::wstring(loc(L"Manette détectée", L"Detected controller"));
        if (std::find(app.detected_controllers.begin(), app.detected_controllers.end(), name) == app.detected_controllers.end())
            app.detected_controllers.push_back(std::move(name));
    }
    SDL_free(ids);
    app.detected_controllers.emplace_back(loc(L"Clavier", L"Keyboard"));
    const bool selected_present = std::find(app.detected_controllers.begin(), app.detected_controllers.end(), app.controller) != app.detected_controllers.end();
    if (!selected_present) app.controller = app.detected_controllers.front();
}

std::wstring ini_value(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t value[32768]{};
    GetPrivateProfileStringW(section, key, fallback, value, 32768, app.config.c_str());
    return value;
}

bool ini_bool(const wchar_t* key, bool fallback) {
    return GetPrivateProfileIntW(L"Launcher", key, fallback ? 1 : 0, app.config.c_str()) != 0;
}

void write_bool(const wchar_t* key, bool value) {
    WritePrivateProfileStringW(L"Launcher", key, value ? L"1" : L"0", app.config.c_str());
}

void save_settings() {
    fs::create_directories(app.config.parent_path());
    write_bool(L"CheatsEnabled",app.cheats_enabled);write_bool(L"FastForward",app.fast_forward);write_bool(L"FastForwardPad",app.fast_forward_pad);
    WritePrivateProfileStringW(L"Launcher",L"ScenePreset",std::to_wstring(app.scene_preset).c_str(),app.config.c_str());
    WritePrivateProfileStringW(L"Launcher",L"NativeRenderHeight",std::to_wstring(app.native_render_height).c_str(),app.config.c_str());
    WritePrivateProfileStringW(L"Launcher",L"ForceFsr3Adapter",app.force_fsr3_gpu.c_str(),app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"GamePath", app.game_path.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"SaveRoot", app.save_root.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"PkgExtractRoot", app.pkg_extract_root.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"FSR", app.fsr.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Resolution", app.resolution.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"TargetDisplay", app.selected_display_id.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"OutputAspect", std::to_wstring(app.output_aspect).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"DisplayMode", app.display_mode.c_str(), app.config.c_str());
    write_bool(L"WindowStretch", app.window_stretch);
    WritePrivateProfileStringW(L"Launcher", L"GameLanguage", app.game_language.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"LauncherLanguage", app.launcher_language.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"KeyboardProfile", app.keyboard_profile.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Controller", app.controller.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"GpuId", std::to_wstring(app.gpu_index).c_str(), app.config.c_str());
    for (size_t i = 0; i < app.key_bindings.size(); ++i)
        WritePrivateProfileStringW(L"Keyboard", kBindingIniNames[i], app.key_bindings[i].c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"FpsLimit", std::to_wstring(app.fps_limit).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Anisotropy", std::to_wstring(app.anisotropy).c_str(), app.config.c_str());
    write_bool(L"Vsync", app.vsync);
    write_bool(L"AsyncShaders", app.async_shaders);
    write_bool(L"AutomaticSaveBackups", app.automatic_save_backups);
    WritePrivateProfileStringW(L"Launcher", L"BackupBeforeLaunch", nullptr, app.config.c_str());
    write_bool(L"UpdateOnStart", app.update_on_start);
    write_bool(L"PerformanceTools", app.performance_tools);
    write_bool(L"PerformanceCapture", app.performance_capture);
    write_bool(L"PerformanceOverlay", app.performance_overlay);
    write_bool(L"OverlayFps", app.overlay_fps);
    write_bool(L"OverlayFrameTime", app.overlay_frametime);
    write_bool(L"OverlayUpscaler", app.overlay_upscaler);
    write_bool(L"OverlayGpu", app.overlay_gpu);
    write_bool(L"OverlayCpu", app.overlay_cpu);
    write_bool(L"OverlayGpuModel", app.overlay_gpu_model);
    write_bool(L"OverlayCpuModel", app.overlay_cpu_model);
    write_bool(L"OverlayRam", app.overlay_ram);
    write_bool(L"OverlayVram", app.overlay_vram);
    write_bool(L"OverlayVersion", app.overlay_version);
    WritePrivateProfileStringW(L"Launcher", L"OverlayScale", std::to_wstring(app.overlay_scale).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"OverlayCorner", std::to_wstring(app.overlay_corner).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"OverlayOpacity", std::to_wstring(app.overlay_opacity).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"OverlayLayout", std::to_wstring(app.overlay_layout).c_str(), app.config.c_str());
}

void load_settings() {
    app.scene_preset=std::clamp(_wtoi(ini_value(L"Launcher",L"ScenePreset",L"-1").c_str()),-1,4);
    app.native_render_height=Ps4RtScene::NativeHeight(_wtoi(ini_value(L"Launcher",L"NativeRenderHeight",L"1080").c_str()));
    app.cheats_enabled=ini_bool(L"CheatsEnabled",false);app.fast_forward=ini_bool(L"FastForward",true);app.fast_forward_pad=ini_bool(L"FastForwardPad",false);
    app.force_fsr3_gpu=ini_value(L"Launcher",L"ForceFsr3Adapter",L"");
    app.game_path = ini_value(L"Launcher", L"GamePath", L"");
    app.save_root = ini_value(L"Launcher", L"SaveRoot", L"");
    app.pkg_extract_root = ini_value(L"Launcher", L"PkgExtractRoot", L"");
    app.fsr = ini_value(L"Launcher", L"FSR", L"FSR 3.1");
    if(app.fsr!=L"FSR 3.1" && app.fsr!=L"FSR 4" && app.fsr!=L"DLSS") app.fsr=L"off";
    app.resolution = ini_value(L"Launcher", L"Resolution", L"1920 × 1080");
    app.resolution = Ps4RtOutput::Labels[Ps4RtOutput::Parse(app.resolution)];
    app.selected_display_id=ini_value(L"Launcher",L"TargetDisplay",L"");
    app.output_aspect=std::clamp(_wtoi(ini_value(L"Launcher",L"OutputAspect",L"0").c_str()),0,int(Ps4RtOutput::AspectCount)-1);
    app.display_mode = ini_value(L"Launcher", L"DisplayMode", L"borderless");
    app.window_stretch = ini_bool(L"WindowStretch", false);
    if (app.display_mode == L"Fenêtré" || app.display_mode == L"Windowed") app.display_mode = L"windowed";
    else if (app.display_mode == L"Plein écran" || app.display_mode == L"Fullscreen") app.display_mode = L"fullscreen";
    else if (app.display_mode != L"windowed" && app.display_mode != L"fullscreen") app.display_mode = L"borderless";
    app.game_language = ini_value(L"Launcher", L"GameLanguage", L"Français");
    wchar_t language[LOCALE_NAME_MAX_LENGTH]{};
    GetUserDefaultLocaleName(language, LOCALE_NAME_MAX_LENGTH);
    const wchar_t* launcher_default = L"English";
    if (!wcsncmp(language,L"fr",2)) launcher_default=L"Français";
    else if (!wcsncmp(language,L"es",2)) launcher_default=L"Español";
    else if (!wcsncmp(language,L"pt-BR",5)) launcher_default=L"Português (Brasil)";
    else if (!wcsncmp(language,L"pt",2)) launcher_default=L"Português";
    else if (!wcsncmp(language,L"it",2)) launcher_default=L"Italiano";
    else if (!wcsncmp(language,L"de",2)) launcher_default=L"Deutsch";
    else if (!wcsncmp(language,L"ru",2)) launcher_default=L"Русский";
    else if (!wcsncmp(language,L"zh-Hant",7) || !wcsncmp(language,L"zh-TW",5)) launcher_default=L"繁體中文";
    else if (!wcsncmp(language,L"zh",2)) launcher_default=L"简体中文";
    else if (!wcsncmp(language,L"ko",2)) launcher_default=L"한국어";
    else if (!wcsncmp(language,L"ja",2)) launcher_default=L"日本語";
    app.launcher_language = ini_value(L"Launcher", L"LauncherLanguage", launcher_default);
    app.keyboard_profile = ini_value(L"Launcher", L"KeyboardProfile", L"AZERTY");
    app.controller = ini_value(L"Launcher", L"Controller", L"Automatique · SDL3");
    app.gpu_index = GetPrivateProfileIntW(L"Launcher", L"GpuId", -1, app.config.c_str());
    apply_keyboard_defaults(app.keyboard_profile != L"QWERTY");
    for (size_t i = 0; i < app.key_bindings.size(); ++i)
        app.key_bindings[i] = ini_value(L"Keyboard", kBindingIniNames[i], app.key_bindings[i].c_str());
    app.fps_limit = GetPrivateProfileIntW(L"Launcher", L"FpsLimit", 60, app.config.c_str());
    if (app.fps_limit != 0 && app.fps_limit != 30 && app.fps_limit != 60) app.fps_limit = 60;
    app.anisotropy = GetPrivateProfileIntW(L"Launcher", L"Anisotropy", 16, app.config.c_str());
    app.vsync = ini_bool(L"Vsync", true);
    app.async_shaders = ini_bool(L"AsyncShaders", true);
    app.automatic_save_backups = ini_bool(L"AutomaticSaveBackups", ini_bool(L"BackupBeforeLaunch", true));
    app.update_on_start = ini_bool(L"UpdateOnStart", true);
    app.performance_tools=ini_bool(L"PerformanceTools",false);
    app.performance_capture = app.performance_tools && ini_bool(L"PerformanceCapture", false);
    app.performance_overlay = ini_bool(L"PerformanceOverlay", false);
    app.overlay_fps = ini_bool(L"OverlayFps", true);
    app.overlay_frametime = ini_bool(L"OverlayFrameTime", true);
    app.overlay_upscaler = ini_bool(L"OverlayUpscaler", true);
    app.overlay_gpu = ini_bool(L"OverlayGpu", false);
    app.overlay_cpu = ini_bool(L"OverlayCpu", false);
    app.overlay_gpu_model = ini_bool(L"OverlayGpuModel", true);
    app.overlay_cpu_model = ini_bool(L"OverlayCpuModel", false);
    app.overlay_ram = ini_bool(L"OverlayRam", false);
    app.overlay_vram = ini_bool(L"OverlayVram", false);
    app.overlay_version = ini_bool(L"OverlayVersion", false);
    app.overlay_scale = std::clamp(int(GetPrivateProfileIntW(L"Launcher", L"OverlayScale", 100, app.config.c_str())), 50, 300);
    app.overlay_corner = std::clamp(int(GetPrivateProfileIntW(L"Launcher", L"OverlayCorner", 1, app.config.c_str())), 0, 5);
    app.overlay_opacity = std::clamp(int(GetPrivateProfileIntW(L"Launcher", L"OverlayOpacity", 50, app.config.c_str())), 0, 100);
    app.overlay_layout = GetPrivateProfileIntW(L"Launcher", L"OverlayLayout", 0, app.config.c_str()) == 1 ? 1 : 0;
}

bool game_layout_complete(const fs::path& path) {
    const fs::path sfo = path / "sce_sys" / "param.sfo";
    return fs::is_directory(path) && !fs::exists(path / ".hmv-pkg-incomplete") && fs::is_regular_file(path / "eboot.bin") &&
           fs::is_regular_file(sfo) && fs::is_directory(path / "sce_module");
}

GameIdentity classify_title_id(const std::wstring& id) {
    if (id == L"CUSA03219") return GameIdentity::Tested;
    if (id == L"CUSA03246" || id == L"CUSA01722" || id == L"CUSA03567") return GameIdentity::Untested;
    return GameIdentity::Rejected;
}

std::wstring region_for_title_id(const std::wstring& id) {
    if (id == L"CUSA03219") return loc(L"Europe",L"Europe");
    if (id == L"CUSA03246") return loc(L"Amérique du Nord",L"North America");
    if (id == L"CUSA01722") return loc(L"Japon",L"Japan");
    if (id == L"CUSA03567") return loc(L"Asie",L"Asia");
    return loc(L"Inconnue",L"Unknown");
}

GameIdentity inspect_game_folder(const fs::path& path, std::wstring* title_id = nullptr) {
    if (!game_layout_complete(path)) return GameIdentity::None;
    try {
        const std::string id = ps4rt::title_id_from_sfo(path / "sce_sys" / "param.sfo");
        const std::wstring wide = from_utf8(id.data(), static_cast<int>(id.size()));
        if (title_id) *title_id = wide;
        return classify_title_id(wide);
    } catch (...) {
        return GameIdentity::None;
    }
}

bool supported_game_folder(const fs::path& path) {
    const GameIdentity identity = inspect_game_folder(path);
    return identity == GameIdentity::Tested || identity == GameIdentity::Untested;
}

void update_game_status() {
    app.game_languages.clear();
    ++app.cheat_generation;app.cheat_entries.clear();
    app.detected_game_title.clear();app.detected_game_version.clear();
    app.game_verified = false;
    app.game_identity = GameIdentity::None;
    app.detected_title_id.clear();
    app.detected_region.clear();
    if (app.game_path.empty()) {
        app.status = loc(L"Sélectionnez le dossier de votre jeu PS4",L"Select your PS4 game directory");
        app.prepared = false;
        return;
    }
    fs::path selected(app.game_path);
    app.pkg_selected = selected.extension() == L".pkg" || selected.extension() == L".PKG";
    if (app.pkg_selected) {
        app.status = loc(L"PKG détecté · extraction en clair requise",L"PKG detected · decrypted extraction required");
        app.prepared = false;
    } else if (fs::exists(selected / ".hmv-pkg-incomplete")) {
        app.status = loc(L"Extraction incomplète · sélectionnez un dossier de jeu complet",L"Incomplete extraction · select a complete game folder");
        app.prepared = false;
    } else if (!game_layout_complete(selected)) {
        app.status = loc(L"Dossier invalide · eboot.bin, sce_sys et sce_module requis",L"Invalid directory · eboot.bin, sce_sys and sce_module required");
        app.prepared = false;
    } else {
        app.game_identity = inspect_game_folder(selected, &app.detected_title_id);
        for(const auto* field:{"TITLE","APP_VER"}) try {
            const auto value=ps4rt::string_from_sfo(selected/"sce_sys"/"param.sfo",field);
            auto& target=std::strcmp(field,"TITLE")==0?app.detected_game_title:app.detected_game_version;
            target=from_utf8(value.data(),static_cast<int>(value.size()));
        } catch(...) {}
        app.detected_region = region_for_title_id(app.detected_title_id);
        app.game_verified = app.game_identity == GameIdentity::Tested || app.game_identity == GameIdentity::Untested;
        if (app.game_identity == GameIdentity::None) {
            app.status = loc(L"Dossier refusé · TITLE_ID illisible",L"Directory rejected · unreadable TITLE_ID");
            app.prepared = false;
            return;
        }
        if (app.game_identity == GameIdentity::Rejected) {
            app.status = loc(L"Jeu refusé · ce TITLE_ID n’est pas Star Ocean 5",L"Game rejected · this TITLE_ID is not Star Ocean 5");
            app.prepared = false;
            return;
        }
        app.game_languages = So5Languages::Detect(selected);
        if (!app.game_languages.empty() &&
            std::find(app.game_languages.begin(), app.game_languages.end(), app.game_language) == app.game_languages.end())
            app.game_language = app.game_languages.front();
        const fs::path output = app.root / "out" / "CUSA03219";
        std::string prepared_id;
        std::ifstream marker(output / "prepared-title-id.txt");
        std::getline(marker, prepared_id);
        const std::wstring prepared_title_id = from_utf8(prepared_id.data(), static_cast<int>(prepared_id.size()));
        const bool legacy_tested_build = prepared_id.empty() && app.game_identity == GameIdentity::Tested;
        app.prepared = fs::exists(output / "boot-linked.bin") &&
                       (legacy_tested_build || prepared_title_id == app.detected_title_id);
        if (app.game_identity == GameIdentity::Untested) {
            app.status = loc(L"Star Ocean 5 détecté · version non testée",L"Star Ocean 5 detected · untested version");
        } else {
            app.status = app.prepared ? loc(L"Jeu détecté · prêt à jouer",L"Game detected · ready to play") :
                                        loc(L"Jeu détecté · préparation requise",L"Game detected · preparation required");
        }
    }
}

void show_game_identity_notice() {
    if (app.game_identity == GameIdentity::Untested) {
        show_modal(loc(L"Version de jeu non testée",L"Untested game version"),
                   loc(L"Cette édition PS4 de Star Ocean 5 est reconnue, mais elle n’a pas encore été validée avec ce runtime : ",
                       L"This PS4 edition of Star Ocean 5 is recognized, but it has not yet been validated with this runtime: ") + app.detected_title_id,
                   loc(L"Continuer",L"Continue"));
    } else if (app.game_identity == GameIdentity::Rejected) {
        show_modal(loc(L"Jeu non pris en charge",L"Unsupported game"),
                   loc(L"Le TITLE_ID détecté ne correspond pas à Star Ocean 5. Le lancement est bloqué : ",
                       L"The detected TITLE_ID does not belong to Star Ocean 5. Launch is blocked: ") + app.detected_title_id,
                   L"OK");
    }
}

std::wstring pick_path(bool folder, const wchar_t* custom_title = nullptr) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return {};
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dialog->SetTitle(custom_title ? custom_title :
        (folder ? loc(L"Choisir le dossier du jeu PS4",L"Choose the PS4 game directory") : loc(L"Choisir un fichier PKG",L"Choose a PKG file")));
    COMDLG_FILTERSPEC filter[] = {{L"Paquets PlayStation 4 (*.pkg)", L"*.pkg"}, {L"Tous les fichiers", L"*.*"}};
    if (!folder) dialog->SetFileTypes(2,filter);
    std::wstring result;
    if (SUCCEEDED(dialog->Show(app.window))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

fs::path save_directory() {
    // A custom selection is the exact title directory, not a parent with a
    // second implicit /1/CUSAxxxxx suffix. This keeps launcher and runtime in sync.
    if (!app.save_root.empty()) return fs::path(app.save_root);
    const std::wstring title_id = app.detected_title_id.empty() ? L"CUSA03219" : app.detected_title_id;
    return app.root / "out" / "CUSA03219" / "user" / "savedata" / "1" / title_id;
}

fs::path backup_directory() {
    // Match sceSaveDataUmountWithBackup: keep editions separate even with custom save paths.
    return app.root / "out" / "CUSA03219" / "user" / "savedata-backups" / "1" / app.detected_title_id;
}

std::wstring read_utf8_field(const std::array<char, 1328>& data, size_t offset, size_t capacity) {
    size_t length = 0;
    while (length < capacity && data[offset + length]) ++length;
    return from_utf8(data.data() + offset, static_cast<int>(length));
}

void load_save_details(const fs::path& save) {
    app.save_title = save.filename().wstring();
    app.save_subtitle.clear();
    app.save_detail = loc(L"Données de sauvegarde locales",L"Local save data");
    app.save_date.clear();
    fs::path metadata = save.parent_path() / (save.filename().wstring() + L".sce_sys");
    fs::path param = metadata / "param.bin";
    std::array<char, 1328> bytes{};
    std::ifstream input(param, std::ios::binary);
    if (input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        const std::wstring title = read_utf8_field(bytes, 0, 128);
        const std::wstring subtitle = read_utf8_field(bytes, 128, 128);
        const std::wstring detail = read_utf8_field(bytes, 256, 1024);
        if (!title.empty()) app.save_title = title;
        app.save_subtitle = subtitle;
        if (!detail.empty()) app.save_detail = detail;
    }
    std::error_code ec;
    const auto stamp = fs::last_write_time(param, ec);
    if (!ec) {
        const auto system_stamp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            stamp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t when = std::chrono::system_clock::to_time_t(system_stamp);
        std::tm local{};
        localtime_s(&local, &when);
        wchar_t formatted[64]{};
        wcsftime(formatted, 64, L"%d/%m/%Y · %H:%M", &local);
        app.save_date = formatted;
    }
    fs::path icon = metadata / "icon0.png";
    if (!fs::is_regular_file(icon)) {
        size_t index = 0;
        for (size_t i = 0; i < app.saves.size(); ++i) if (app.saves[i] == save) index = i;
        wchar_t fallback[32]{};
        swprintf_s(fallback, L"save_data_%02zu.png", std::min<size_t>(index + 1, 13));
        icon = app.root / "launcher" / "assets" / "saves" / fallback;
        if (!fs::is_regular_file(icon)) icon = app.root / "launcher" / "assets" / "saves" / "ICON0.png";
    }
    drawing_cache.artwork.clear();
    app.save_preview=fs::is_regular_file(icon)?So5SavePreview::Load(icon):nullptr;
}

void refresh_saves() {
    app.saves.clear();
    app.selected_save = -1;
    fs::path directory = save_directory();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (entry.is_directory(ec) && entry.path().extension() != L".sce_sys") app.saves.push_back(entry.path());
    }
    std::sort(app.saves.begin(), app.saves.end(), [](const fs::path& left, const fs::path& right) {
        std::error_code left_ec, right_ec;
        return fs::last_write_time(left, left_ec) > fs::last_write_time(right, right_ec);
    });
    if (!app.saves.empty()) {
        app.selected_save = 0;
        load_save_details(app.saves[0]);
    } else {
        app.save_title = loc(L"Aucune sauvegarde",L"No save data");
        app.save_subtitle.clear();
        app.save_detail = loc(L"Une miniature apparaîtra après la première sauvegarde.",L"A preview will appear after the first save.");
        app.save_date.clear();
        fs::path fallback = app.root / "launcher" / "assets" / "saves" / "ICON0.png";
        drawing_cache.artwork.clear();
        app.save_preview=fs::is_regular_file(fallback)?So5SavePreview::Load(fallback):nullptr;
    }
}

void open_path(const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    ShellExecuteW(app.window, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

fs::path cache_directory() {
    return app.root / "out" / "CUSA03219" / "user" / "gpu";
}

std::wstring timestamp_name() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t when = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &when);
    wchar_t buffer[40]{};
    wcsftime(buffer, 40, L"%Y%m%d-%H%M%S", &local);
    return buffer;
}

std::wstring format_size(std::uintmax_t bytes) {
    static constexpr const wchar_t* units[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) { value /= 1024.0; ++unit; }
    wchar_t formatted[64]{};
    swprintf_s(formatted, unit ? L"%.1f %ls" : L"%.0f %ls", value, units[unit]);
    return formatted;
}

void create_shortcut() {
    PWSTR desktop = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop);
    if (SUCCEEDED(result)) result = So5Shortcuts::Create(desktop, app.root,
        executable_directory() / L"Star Ocean 5 Launcher.exe", runtime_path());
    CoTaskMemFree(desktop);
    app.status = SUCCEEDED(result) ? loc(L"Deux raccourcis créés sur le Bureau",L"Two desktop shortcuts created") : loc(L"Impossible de créer les deux raccourcis",L"Could not create both shortcuts");
    append_log(SUCCEEDED(result) ? L"Desktop shortcuts created: launcher and quick play" :
        L"ERROR · could not create both desktop shortcuts (HRESULT " + std::to_wstring(static_cast<unsigned long>(result)) + L")");
}

void import_save() {
    std::wstring selected = pick_path(true);
    if (selected.empty()) return;
    fs::path source(selected);
    fs::path root = save_directory();
    // Accept current runtime snapshots and legacy launcher snapshots without rewriting them.
    fs::path data_source = fs::is_directory(source / "data") ? source / "data" : source;
    fs::path meta_source = fs::is_directory(source / "metadata") ? source / "metadata" :
                           fs::is_directory(source / "sce_sys") ? source / "sce_sys" : fs::path{};
    std::wstring slot_name = source.filename().wstring();
    if (slot_name.starts_with(L"backup-") && source.parent_path().parent_path().parent_path().parent_path().filename() == L"savedata-backups") {
        slot_name = source.parent_path().filename().wstring();
    } else if (fs::is_directory(source / "data")) {
        const size_t suffix = slot_name.find_last_of(L'-');
        if (suffix != std::wstring::npos && suffix + 1 < slot_name.size() &&
            std::all_of(slot_name.begin() + suffix + 1, slot_name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) slot_name.resize(suffix);
    }
    fs::path destination = root / slot_name;
    if (destination.filename().empty()) destination = root / source.filename();
    std::error_code ec;
    fs::create_directories(root, ec);
    if (fs::exists(destination)) {
        app.status = loc(L"Import refusé · une sauvegarde porte déjà ce nom",L"Import refused · a save already uses this name");
        return;
    }
    fs::copy(data_source, destination, fs::copy_options::recursive, ec);
    if (!ec && !meta_source.empty())
        fs::copy(meta_source, root / (destination.filename().wstring() + L".sce_sys"), fs::copy_options::recursive, ec);
    app.status = ec ? loc(L"Échec de l’importation",L"Import failed") : loc(L"Sauvegarde importée",L"Save imported");
    if (ec) append_log(L"ERROR · save import failed");
    else append_log(L"Save imported from " + source.wstring());
    refresh_saves();
}

std::wstring fsr_argument() {
    if (app.fsr == L"FSR 3.1") return L"fsr3";
    if (app.fsr == L"FSR 4") return L"fsr4";
    if (app.fsr == L"DLSS") return L"dlss";
    return L"off";
}

std::wstring resolution_argument() {
    const char* value=Ps4RtOutput::Arguments[Ps4RtOutput::Parse(app.resolution)];
    return std::wstring(value,value+std::strlen(value));
}

std::uint64_t available_outputs() {
    return app.display_mode==L"windowed"?app.target_display.window_modes:app.target_display.fullscreen_modes;
}

bool requested_ultrawide() {
    return app.game_verified && app.detected_title_id==L"CUSA03219" &&
        app.detected_game_version==L"01.00" && Ps4RtUltrawide::Wide(Ps4RtOutput::Parse(app.resolution));
}
Ps4RtOutput::Size requested_scene_output() {
    const int output=Ps4RtOutput::Parse(app.resolution);
    return requested_ultrawide()?Ps4RtOutput::Size{Ps4RtOutput::Widths[output],Ps4RtOutput::Heights[output]}:Ps4RtOutput::Content(output);
}

std::wstring output_aspect_label(int aspect) {
    if(aspect==Ps4RtOutput::All) return loc(L"Tous",L"All");
    const char* label=Ps4RtOutput::AspectNames[std::clamp(aspect,0,int(Ps4RtOutput::AspectCount)-1)];
    return std::wstring(label,label+std::strlen(label));
}

std::wstring display_label(size_t index) {
    const auto& display=app.displays[index];
    return std::to_wstring(index+1)+L" · "+display.name+L" · "+std::to_wstring(display.width)+L" × "+std::to_wstring(display.height);
}

std::wstring selected_display_label() {
    for(size_t i=0;i<app.displays.size();++i) if(app.displays[i].monitor==app.target_display.monitor) return display_label(i);
    return app.target_display.name;
}

void refresh_target_display(bool rescan=false) {
    if(!SDL_WasInit(SDL_INIT_VIDEO)) SDL_InitSubSystem(SDL_INIT_VIDEO);
    if(rescan || app.displays.empty()) app.displays=Ps4RtDisplay::Enumerate();
    const HMONITOR automatic=MonitorFromWindow(app.window,MONITOR_DEFAULTTOPRIMARY);
    auto selected=std::find_if(app.displays.begin(),app.displays.end(),[](const auto& display){return display.id==app.selected_display_id;});
    if(selected==app.displays.end()) {
        if(!app.selected_display_id.empty()) { append_log(L"Display: selected monitor is disconnected; using the launcher's monitor"); app.selected_display_id.clear(); }
        selected=std::find_if(app.displays.begin(),app.displays.end(),[&](const auto& display){return display.monitor==automatic;});
    }
    if(selected==app.displays.end() && !app.displays.empty()) selected=app.displays.begin();
    if(selected!=app.displays.end()) app.target_display=*selected;
    else app.target_display=Ps4RtDisplay::ForWindow(app.window);
    const auto& display = app.target_display;
    if(app.display_mode==L"fullscreen" && !display.fullscreen_modes) app.display_mode=L"borderless";
    if(!Ps4RtOutput::Filter(available_outputs(),app.output_aspect)) app.output_aspect=Ps4RtOutput::All;
    const int output = Ps4RtOutput::Select(Ps4RtOutput::Parse(app.resolution),available_outputs(),app.output_aspect);
    app.resolution = Ps4RtOutput::Labels[output>=0?output:1];
    if (!Ps4RtOutput::TemporalAllowed(output)) app.fsr=L"off";
}

std::string narrow_ascii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (wchar_t c : value) result.push_back(c < 128 ? static_cast<char>(c) : '?');
    return result;
}

std::wstring keyboard_button_map() {
    constexpr std::array<const wchar_t*, 17> names = {{
        L"cross", L"circle", L"square", L"triangle", L"l1", L"r1", L"l2", L"r2", L"l3", L"r3",
        L"options", L"touchpad_left", L"touchpad_right", L"up", L"down", L"left", L"right"
    }};
    std::wstring result;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) result += L",";
        result += names[i]; result += L"="; result += app.key_bindings[i];
    }
    return result;
}

std::wstring keyboard_axis_map() {
    constexpr std::array<const wchar_t*, 8> names = {{
        L"left_left", L"left_right", L"left_up", L"left_down",
        L"right_left", L"right_right", L"right_up", L"right_down"
    }};
    std::wstring result;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) result += L",";
        result += names[i]; result += L"="; result += app.key_bindings[17 + i];
    }
    return result;
}

std::string display_mode_key() {
    if (app.display_mode == L"windowed") return "windowed";
    if (app.display_mode == L"fullscreen") return "fullscreen";
    return "borderless";
}

int language_code() {
    return So5Languages::SystemCode(app.game_languages.empty() ? L"English" : app.game_language);
}

void write_runtime_settings() {
    const fs::path path = app.root / "out" / "CUSA03219" / "ps4rt.ini";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::vector<std::string> lines;
    {
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) lines.push_back(line);
    }
    const auto scene_output=requested_scene_output();
    if(app.fsr!=L"off") app.scene_preset=Ps4RtScene::ForOutput(app.scene_preset,scene_output.width,scene_output.height);
    const std::vector<std::pair<std::string, std::string>> settings = {
        {"scene_preset",std::to_string(app.scene_preset)},
        {"native_render_height",std::to_string(app.native_render_height)},
        {"upscaler", narrow_ascii(fsr_argument())},
        {"output_res", Ps4RtOutput::Arguments[Ps4RtOutput::Parse(app.resolution)]},
        {"output_aspect", std::to_string(app.output_aspect)},
        {"display_mode", display_mode_key()}, {"vsync", app.vsync ? "1" : "0"},
        {"window_stretch", app.window_stretch ? "1" : "0"},
        {"fps_limit", std::to_string(app.fps_limit)}, {"async_shaders", app.async_shaders ? "1" : "0"},
        {"frames_queued", "2"}, {"render_resolution", "native"},
        {"anisotropy", std::to_string(app.anisotropy)},
        {"show_fps", app.performance_overlay ? "1" : "0"},
        {"overlay_fps", app.overlay_fps ? "1" : "0"},
        {"overlay_frametime", app.overlay_frametime ? "1" : "0"},
        {"overlay_upscaler", app.overlay_upscaler ? "1" : "0"},
        {"overlay_gpu", app.overlay_gpu ? "1" : "0"},
        {"overlay_cpu", app.overlay_cpu ? "1" : "0"},
        {"overlay_gpu_model", app.overlay_gpu_model ? "1" : "0"},
        {"overlay_cpu_model", app.overlay_cpu_model ? "1" : "0"},
        {"overlay_ram", app.overlay_ram ? "1" : "0"},
        {"overlay_vram", app.overlay_vram ? "1" : "0"},
        {"overlay_version", app.overlay_version ? "1" : "0"},
        {"overlay_scale", std::to_string(app.overlay_scale)},
        {"overlay_corner", std::to_string(app.overlay_corner)},
        {"overlay_opacity", std::to_string(app.overlay_opacity)},
        {"overlay_layout", std::to_string(app.overlay_layout)}
    };
    for (const auto& [key, value] : settings) {
        const std::string prefix = key + "=";
        bool found = false;
        for (std::string& line : lines) {
            if (line.rfind(prefix, 0) == 0) { line = prefix + value; found = true; break; }
        }
        if (!found) lines.push_back(prefix + value);
    }
    const fs::path temporary = path.wstring() + L".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        for (const std::string& line : lines) output << line << '\n';
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec) append_log(L"ERROR · could not save runtime settings");
}

void read_live_runtime_settings() {
    std::ifstream file(app.root/"out/CUSA03219/ps4rt.ini");
    if(!file) return;
    std::map<std::string,std::string> values;
    std::string line;
    while(std::getline(file,line)) {
        if(line.empty() || line[0]=='#') continue;
        const auto equal=line.find('=');
        if(equal!=std::string::npos) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            values[line.substr(0,equal)]=line.substr(equal+1);
        }
    }
    auto integer=[&](const char* key,int current,int low,int high) {
        auto found=values.find(key);if(found==values.end()) return current;
        char* end=nullptr;const long value=std::strtol(found->second.c_str(),&end,10);
        return end && end!=found->second.c_str() && !*end && value>=low && value<=high?int(value):current;
    };
    if(auto found=values.find("upscaler");found!=values.end()) {
        if(found->second=="off") app.fsr=L"off";
        else if(found->second=="fsr3") app.fsr=L"FSR 3.1";
        else if(found->second=="fsr4") app.fsr=L"FSR 4";
        else if(found->second=="dlss") app.fsr=L"DLSS";
    }
    if(auto found=values.find("display_mode");found!=values.end() &&
        (found->second=="windowed" || found->second=="borderless" || found->second=="fullscreen"))
        app.display_mode=std::wstring(found->second.begin(),found->second.end());
    if(auto found=values.find("output_res");found!=values.end())
        for(int i=0;i<Ps4RtOutput::Count;++i) if(found->second==Ps4RtOutput::Arguments[i]) app.resolution=Ps4RtOutput::Labels[i];
    app.scene_preset=integer("scene_preset",app.scene_preset,-1,4);
    app.native_render_height=Ps4RtScene::NativeHeight(integer("native_render_height",app.native_render_height,0,2160));
    app.output_aspect=integer("output_aspect",app.output_aspect,0,Ps4RtOutput::AspectCount-1);
    app.vsync=integer("vsync",app.vsync,0,1)!=0;
    app.window_stretch=integer("window_stretch",app.window_stretch,0,1)!=0;
    app.async_shaders=integer("async_shaders",app.async_shaders,0,1)!=0;
    app.anisotropy=Ps4RtGraphics::ParseAnisotropy(integer("anisotropy",app.anisotropy,0,16));
    const int fps=integer("fps_limit",app.fps_limit,0,60);
    if(fps==0 || fps==30 || fps==60) app.fps_limit=fps;
    app.performance_overlay=integer("show_fps",app.performance_overlay,0,1)!=0;
    const std::pair<const char*,bool*> items[]={
        {"overlay_fps",&app.overlay_fps},{"overlay_frametime",&app.overlay_frametime},{"overlay_upscaler",&app.overlay_upscaler},
        {"overlay_gpu",&app.overlay_gpu},{"overlay_cpu",&app.overlay_cpu},{"overlay_ram",&app.overlay_ram},
        {"overlay_vram",&app.overlay_vram},{"overlay_version",&app.overlay_version},
        {"overlay_gpu_model",&app.overlay_gpu_model},{"overlay_cpu_model",&app.overlay_cpu_model}};
    for(const auto& [key,value]:items) *value=integer(key,*value,0,1)!=0;
    app.overlay_scale=integer("overlay_scale",app.overlay_scale,50,300);
    app.overlay_corner=integer("overlay_corner",app.overlay_corner,0,5);
    app.overlay_opacity=integer("overlay_opacity",app.overlay_opacity,0,100);
    app.overlay_layout=integer("overlay_layout",app.overlay_layout,0,1);
    refresh_target_display(true);save_settings();
}

fs::path find_extracted_game(const fs::path& root) {
    if (game_layout_complete(root)) return root;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it.depth() > 5) { it.disable_recursion_pending(); continue; }
        if (it->is_directory(ec) && game_layout_complete(it->path())) return it->path();
    }
    return {};
}

void confirm_pkg_extraction(const fs::path& pkg) {
    if (app.busy || !fs::is_regular_file(pkg)) return;
    std::error_code ec;
    const auto bytes = fs::file_size(pkg, ec);
    app.pending_pkg = pkg;
    const std::wstring details = pkg.filename().wstring() + L"\n" +
        loc(L"Taille : ",L"Size: ") + (ec ? L"—" : format_size(bytes)) + L"\n" +
        loc(L"Destination : ",L"Destination: ") + pkg_import_root().wstring();
    show_modal(loc(L"Extraire ce PKG ?",L"Extract this PKG?"), details,
               loc(L"Extraire",L"Extract"), loc(L"Annuler",L"Cancel"), ModalAction::ConfirmExtraction,
               loc(L"Changer le dossier",L"Change folder"));
}

void extract_pkg(const fs::path& pkg) {
    if (app.busy || !fs::is_regular_file(pkg)) return;
    const fs::path extractor = pkg_extractor_path();
    if (!fs::is_regular_file(extractor)) {
        app.status = loc(L"Extracteur PKG absent", L"PKG extractor is missing");
        show_modal(loc(L"Extracteur indisponible",L"Extractor unavailable"),
                   loc(L"PkgExtractor.exe doit être présent dans le paquet.",
                       L"PkgExtractor.exe must be present in the package."),
                   L"OK");
        return;
    }
    const fs::path destination = fs::absolute(pkg_import_root()).lexically_normal();
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        app.status = loc(L"Impossible de créer le dossier d’extraction",L"Could not create the extraction folder");
        return;
    }
    app.busy = true;
    app.pkg_selected = true;
    app.extraction_active = true;
    app.extraction_cancel_requested = false;
    app.extraction_started = GetTickCount64();
    app.extraction_source = pkg.filename().wstring();
    app.extraction_destination = destination.wstring();
    app.extraction_size = fs::file_size(pkg, ec);
    app.extraction_done=0; app.extraction_total=0;
    app.extraction_title.clear();
    app.status = loc(L"Extraction du PKG en cours…",L"Extracting PKG…");
    append_log(L"PKG extraction started: " + pkg.wstring());
    append_log(L"PKG extraction directory: " + destination.wstring());
    show_modal(loc(L"Extraction du PKG",L"PKG extraction"),
               app.extraction_source + L"\n" + loc(L"Taille : ",L"Size: ") + format_size(app.extraction_size) +
               L"\n" + loc(L"Destination : ",L"Destination: ") + app.extraction_destination,
               loc(L"Annuler l’extraction",L"Cancel extraction"), {}, ModalAction::CancelExtraction);
    SetTimer(app.window, 1, 1000, nullptr);

    std::thread([pkg, destination, extractor] {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read_pipe = nullptr, write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            append_log(L"ERROR · could not create PKG extractor diagnostics pipe");
            {
                std::lock_guard guard(app.mutex);
                app.busy = false; app.pkg_selected = false; app.extraction_active = false;
                app.status = loc(L"Échec de l’extraction du PKG · consultez le journal",L"PKG extraction failed · see diagnostics");
            }
            show_modal(loc(L"Échec de l’extraction",L"Extraction failed"),
                       loc(L"Impossible de démarrer la capture des diagnostics.",L"Could not start the diagnostics capture."), L"OK");
            return;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
        std::wstring command = quote(extractor.wstring()) + L" --title-folder " + quote(pkg.wstring()) + L" " + quote(destination.wstring());
        std::vector<wchar_t> mutable_command(command.begin(), command.end()); mutable_command.push_back(L'\0');
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write_pipe; startup.hStdError = write_pipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
        const BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, extractor.parent_path().c_str(), &startup, &process);
        CloseHandle(write_pipe);
        if (!created) {
            CloseHandle(read_pipe); if (job) CloseHandle(job);
            append_log(L"ERROR · could not start PkgExtractor.exe (code " + std::to_wstring(GetLastError()) + L")");
            {
                std::lock_guard guard(app.mutex);
                app.busy = false; app.pkg_selected = false; app.extraction_active = false;
                app.status = loc(L"Échec de l’extraction du PKG · consultez le journal",L"PKG extraction failed · see diagnostics");
            }
            show_modal(loc(L"Échec de l’extraction",L"Extraction failed"),
                       loc(L"PkgExtractor.exe n’a pas pu démarrer. Consultez le journal pour le code Windows.",
                           L"PkgExtractor.exe could not start. See the log for the Windows error code."), L"OK");
            return;
        }
        if (job) { AssignProcessToJobObject(job, process.hProcess); std::lock_guard guard(app.mutex); app.job = job; }
        if (app.extraction_cancel_requested && job) TerminateJobObject(job, 0);
        CloseHandle(process.hThread);
        HmvPkg::Progress progress;
        HmvPkg::Lines lines;
        auto receive_line=[&](const std::string& line) {
            if(progress.Parse(line)) {
                std::lock_guard guard(app.mutex);
                app.extraction_done=progress.done; app.extraction_total=progress.total;
                app.extraction_title=std::wstring(progress.title.begin(),progress.title.end());
            }
            append_log(from_utf8(line.c_str(),int(line.size())));
            notify_log_updated();
        };
        char buffer[4096]; DWORD got = 0;
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got) {
            lines.Feed(std::string_view(buffer,got),receive_line);
        }
        if(!lines.pending.empty()) receive_line(lines.pending);
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1; GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess); CloseHandle(read_pipe);
        const fs::path extracted = progress.title.empty() ? fs::path{} : destination/fs::path(progress.title);
        // A completed publish wins a simultaneous Cancel click; never delete a completed game.
        const bool canceled = app.extraction_cancel_requested && code!=0;
        const fs::path game = !canceled && code == 0 && !extracted.empty() ? find_extracted_game(extracted) : fs::path{};
        std::wstring extracted_title_id;
        const GameIdentity extracted_identity = game.empty() ? GameIdentity::None :
            inspect_game_folder(game, &extracted_title_id);
        const std::wstring extracted_region = region_for_title_id(extracted_title_id);
        {
            std::lock_guard guard(app.mutex);
            if (app.job == job) app.job = nullptr;
            app.busy = false; app.pkg_selected = false; app.extraction_active = false;
            if (!game.empty()) {
                app.game_path = game.wstring();
                app.prepared = false;
                app.game_identity = extracted_identity;
                app.detected_title_id = extracted_title_id;
                app.detected_region = extracted_region;
                app.game_verified = extracted_identity == GameIdentity::Tested || extracted_identity == GameIdentity::Untested;
                app.game_languages = app.game_verified ? So5Languages::Detect(game) : std::vector<std::wstring>{};
                if (!app.game_languages.empty() &&
                    std::find(app.game_languages.begin(), app.game_languages.end(), app.game_language) == app.game_languages.end())
                    app.game_language = app.game_languages.front();
                if (extracted_identity == GameIdentity::Tested)
                    app.status = loc(L"PKG extrait · jeu détecté · préparation requise",L"PKG extracted · game detected · preparation required");
                else if (extracted_identity == GameIdentity::Untested)
                    app.status = loc(L"PKG extrait · Star Ocean 5 non testé détecté",L"PKG extracted · untested Star Ocean 5 detected");
                else if (extracted_identity == GameIdentity::Rejected)
                    app.status = loc(L"PKG extrait · jeu refusé",L"PKG extracted · game rejected");
                else
                    app.status = loc(L"PKG extrait · TITLE_ID illisible",L"PKG extracted · unreadable TITLE_ID");
            } else if (canceled) app.status = loc(L"Extraction annulée",L"Extraction canceled");
            else app.status = code == 0 ? loc(L"Extraction terminée, mais aucun jeu PS4 valide n’a été trouvé",L"Extraction completed, but no valid PS4 game was found")
                                        : loc(L"Échec de l’extraction du PKG · consultez le journal",L"PKG extraction failed · see diagnostics");
        }
        if (canceled) {
            std::error_code cleanup_error;
            // Only the random, protocol-validated child belongs to this operation.
            // Never delete the extraction parent or an existing TITLE_ID directory.
            if(!progress.staging.empty()) {
                const auto partial=destination/fs::path(progress.staging);
                const DWORD attributes=GetFileAttributesW(partial.c_str());
                if(attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
                    fs::remove_all(partial,cleanup_error);
            }
            append_log(cleanup_error ? L"ERROR · canceled PKG extraction left partial files in " + destination.wstring()
                                     : L"PKG extraction canceled · partial files removed");
            show_modal(loc(L"Extraction annulée",L"Extraction canceled"),
                       cleanup_error ? loc(L"L’extraction a été arrêtée, mais certains fichiers partiels n’ont pas pu être supprimés.",
                                           L"Extraction stopped, but some partial files could not be removed.")
                                     : loc(L"L’extraction a été arrêtée et les fichiers partiels ont été supprimés.",
                                           L"Extraction stopped and partial files were removed."), L"OK");
        } else if (!game.empty() && (extracted_identity == GameIdentity::Tested || extracted_identity == GameIdentity::Untested)) {
            save_settings(); append_log(L"PKG extraction complete: " + game.wstring());
            if (extracted_identity == GameIdentity::Untested) {
                append_log(L"WARNING · recognized but untested Star Ocean 5 PS4 TITLE_ID: " + extracted_title_id);
                show_modal(loc(L"Version de jeu non testée",L"Untested game version"),
                           loc(L"Le PKG a été extrait et cette édition PS4 de Star Ocean 5 a été sélectionnée. Elle n’a pas encore été validée avec ce runtime : ",
                               L"The PKG was extracted and this PS4 edition of Star Ocean 5 was selected. It has not yet been validated with this runtime: ") + extracted_title_id,
                           loc(L"Continuer",L"Continue"));
            } else {
                show_modal(loc(L"Extraction terminée",L"Extraction complete"),
                           loc(L"Le jeu PS4 a été extrait et sélectionné automatiquement. Vous pouvez maintenant le préparer ou le lancer.",
                               L"The PS4 game was extracted and selected automatically. You can now prepare or launch it."),
                           loc(L"Continuer",L"Continue"));
            }
        } else if (!game.empty()) {
            save_settings();
            append_log(L"ERROR · extracted PS4 TITLE_ID is not supported: " +
                       (extracted_title_id.empty() ? std::wstring(L"unreadable") : extracted_title_id));
            show_modal(loc(L"Jeu non pris en charge",L"Unsupported game"),
                       extracted_identity == GameIdentity::Rejected
                           ? loc(L"Le PKG a été extrait, mais le TITLE_ID ne correspond pas à Star Ocean 5. Le lancement est bloqué : ",
                                 L"The PKG was extracted, but its TITLE_ID does not belong to Star Ocean 5. Launch is blocked: ") + extracted_title_id
                           : loc(L"Le PKG a été extrait, mais son TITLE_ID PS4 est illisible. Le lancement est bloqué.",
                                 L"The PKG was extracted, but its PS4 TITLE_ID is unreadable. Launch is blocked."),
                       L"OK");
        } else {
            append_log(L"ERROR · PKG extraction did not produce a valid PS4 game directory (exit code " + std::to_wstring(code) + L")");
            show_modal(loc(L"Échec de l’extraction",L"Extraction failed"),
                       code == 0 ? loc(L"L’extraction est terminée, mais aucun dossier de jeu PS4 valide n’a été trouvé.",
                                           L"Extraction completed, but no valid PS4 game directory was found.")
                                 : loc(L"L’extracteur a signalé une erreur. Consultez Journal et diagnostic.",
                                           L"The extractor reported an error. See Logs and diagnostics."), L"OK");
        }
        if (job) CloseHandle(job);
        notify_log_updated();
    }).detach();
}

void run_game(bool prepare_only) {
    if(app.hud_library&&app.hud_library->Snapshot().busy)return;
    refresh_target_display(true);
    if (app.busy || app.update_transfer) return;
    update_game_status();
    if (app.pkg_selected) {
        app.status = loc(L"Ce PKG doit être extrait et déchiffré avant utilisation",L"This PKG must be extracted and decrypted before use");
        append_log(L"PKG · select the package from the Game page to use the integrated extractor.");
        app.page = Page::Logs;
        InvalidateRect(app.window, nullptr, FALSE);
        return;
    }
    if (!app.game_verified) {
        app.status = loc(L"Jeu refusé ou dossier PS4 invalide",L"Game rejected or invalid PS4 directory");
        return;
    }
    if(!prepare_only && app.fsr==L"FSR 4" && !selected_gpu_supports_fsr4()) {
        reject_incompatible_fsr4(); save_settings(); write_runtime_settings(); return;
    }
    if(!prepare_only && app.fsr==L"DLSS" && !selected_gpu_supports_dlss()) {app.fsr=L"off";append_log(L"DLSS: selected GPU or optional components unavailable; original rendering selected.");}
    if(!prepare_only && app.fsr==L"FSR 3.1" && !selected_gpu_supports_fsr3()) {
        app.fsr=L"off";save_settings();
        append_log(L"Upscaler: FSR 3.1 is unavailable under the selected GPU policy; original rendering selected.");
    }
    if(!prepare_only && !available_outputs()) {
        app.status=loc(L"Aucun mode d’affichage vérifié n’est disponible.",L"No verified display mode is available.");
        append_log(L"Display: no exact Windows/SDL output mode could be verified on the target monitor. Launch cancelled.");
        app.page=Page::Logs;return;
    }
    write_runtime_settings();
    app.busy = true;
    app.status = prepare_only ? loc(L"Préparation en cours…",L"Preparing…") : loc(L"Démarrage du jeu…",L"Starting game…");
    append_log(prepare_only ? L"Preparing " + app.detected_title_id + L"…" : L"Starting STAR OCEAN 5 (" + app.detected_title_id + L")…");
    if (!prepare_only) {
        if (app.game_languages.empty()) append_log(L"Game language: no verified text-language metadata; using system language code 1 (English).");
        else append_log(L"Game language: system code " + std::to_wstring(language_code()) + L" (verified installed text language).");
        save_settings();
    }
    InvalidateRect(app.window, nullptr, FALSE);

    std::wstring game = app.game_path;
    std::wstring fsr = fsr_argument();
    std::wstring resolution = resolution_argument();
    std::wstring save_root = app.save_root;
    const int language = language_code();
    const std::wstring ui_language=app.launcher_language;
    fs::path root = app.root;
    std::wstring keyboard_profile = app.keyboard_profile;
    std::wstring keyboard_buttons = keyboard_button_map();
    std::wstring keyboard_axes = keyboard_axis_map();
    std::wstring controller_profile = app.controller;
    // The Keyboard entry selects the keyboard mapping view. Keep SDL hot-plug active:
    // a pad connected after launch must work alongside the keyboard without a restart.
    const bool keyboard_only = false;
    std::wstring title_id = app.detected_title_id;
    const int gpu_index = app.gpu_index;
    const auto* adapter=selected_adapter();
    const std::wstring fsr3_force=fsr3_forced() && adapter?std::wstring(adapter->device_key.begin(),adapter->device_key.end()):L"";
    const bool performance_capture = app.performance_tools && app.performance_capture;
    const bool automatic_save_backups = app.automatic_save_backups;
    const bool cheats_enabled=app.cheats_enabled,fast_forward=app.fast_forward,fast_forward_pad=app.fast_forward_pad;
    const auto display_device=std::wstring(app.target_display.info.szDevice);
    const auto aspect=std::to_wstring(app.output_aspect);
    std::thread([prepare_only, game, fsr, resolution, save_root, language, ui_language, keyboard_profile, keyboard_buttons, keyboard_axes, controller_profile, keyboard_only, title_id, root, gpu_index, fsr3_force, performance_capture, automatic_save_backups,display_device,aspect,cheats_enabled,fast_forward,fast_forward_pad] {
        DWORD code = 1;
        const fs::path output = root / "out" / "CUSA03219";
        try {
            ps4rt::prepare_title(fs::path(game), output, [](const std::wstring& line) {
                append_log(line); notify_log_updated();
            });
            std::ofstream marker(output / "prepared-title-id.txt", std::ios::trunc);
            marker << narrow_ascii(title_id) << '\n';
            if (prepare_only) code = 0;
        } catch (const std::exception& error) {
            code = 3;
            append_log(std::wstring(L"ERROR · native preparation: ") + from_utf8(error.what(), static_cast<int>(std::strlen(error.what()))));
            notify_log_updated();
        }
        if (prepare_only || code != 1) {
            std::lock_guard guard(app.mutex);
            app.busy = false; app.prepared = code == 0;
            app.status = code == 0 ? loc(L"Préparation native terminée",L"Native preparation complete") : loc(L"Échec de la préparation native",L"Native preparation failed");
            notify_log_updated();
            if (!prepare_only) PostMessageW(app.window, WM_QUICK_FAILED, 0, 0);
            return;
        }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read_pipe = nullptr, write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            append_log(L"ERROR · could not create diagnostics pipe");
            std::lock_guard guard(app.mutex); app.busy = false;
            notify_log_updated(); PostMessageW(app.window, WM_QUICK_FAILED, 0, 0); return;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
        const fs::path runtime = program_path(root,L"Star Ocean 5.exe");
        const fs::path user = output / "user";
        std::error_code directory_error;
        fs::create_directories(user / "gpu", directory_error);
        const fs::path capture_directory = user / "gpu" / "performance";
        if (performance_capture) {
            fs::create_directories(capture_directory, directory_error);
            append_log(L"Performance capture enabled for this session.");
        }
        if (!fs::is_regular_file(runtime)) {
            CloseHandle(read_pipe); CloseHandle(write_pipe);
            append_log(L"ERROR · Star Ocean 5.exe is missing from the application bin directory or source build output");
            std::lock_guard guard(app.mutex); app.busy = false; app.status = loc(L"Runtime Windows absent",L"Windows runtime missing");
            notify_log_updated(); PostMessageW(app.window, WM_QUICK_FAILED, 0, 0); return;
        }
        std::wstring command = quote(runtime.wstring()) + L" " + quote((output / "boot-linked.bin").wstring()) +
            L" --app0 " + quote(game) + L" --user " + quote(user.wstring()) + L" --content-profile " +
            quote((output / "content.bin").wstring()) + L" --timeout 0";
        if(cheats_enabled && fs::is_regular_file(output/L"cheats.txt")) command+=L" --cheats "+quote((output/L"cheats.txt").wstring());
        struct SavedEnv { std::wstring key, old; bool existed{}; };
        std::vector<SavedEnv> saved;
        auto set_env = [&](const wchar_t* key, const std::wstring& value) {
            DWORD needed = GetEnvironmentVariableW(key, nullptr, 0); SavedEnv item{key, {}, needed != 0};
            if (needed) { item.old.resize(needed - 1); GetEnvironmentVariableW(key, item.old.data(), needed); }
            saved.push_back(std::move(item)); SetEnvironmentVariableW(key, value.c_str());
        };
        set_env(L"PS4RT_BC6_FLOAT", L"unsigned"); set_env(L"PS4RT_TRANSITION_SNAPSHOT", L"1");
        set_env(L"SO5_NATIVE_4K_TEST",L"0");
        set_env(L"PS4RT_OBJECT_MOTION", L"0"); set_env(L"PS4RT_FSR1", L"0");
        // Keep cached pipelines on demand. The experimental bulk warm-up can crash inside
        // AMD's Vulkan driver while several pipelines are created concurrently, preventing
        // the game from starting at all. Normal cache lookup and asynchronous compilation
        // remain enabled, so already discovered shaders are still reused between sessions.
        set_env(L"PS4RT_BACKGROUND_WARMUP", L"0");
        set_env(L"PS4RT_PARALLEL_WARMUP", L"0");
        set_env(L"PS4RT_WARMUP_ONCE", L"0");
        set_env(L"PS4RT_PARTIAL_HEIGHT_VIEWS", L"0"); set_env(L"PS4RT_UPSCALER", fsr);
        set_env(L"PS4RT_OUTPUT_RES", resolution);
        set_env(L"PS4RT_DISPLAY_DEVICE", display_device);
        set_env(L"PS4RT_OUTPUT_ASPECT", aspect);
        set_env(L"PS4RT_GPU_USER_DIR", (user / "gpu").wstring());
        set_env(L"PS4RT_GPU_ID", std::to_wstring(gpu_index));
        set_env(L"SO5_FSR3_FORCE_GPU",fsr3_force);
        set_env(L"PS4RT_FAST_FORWARD",fast_forward?L"3":L"1");
        set_env(L"SO5_FAST_FORWARD_PAD",fast_forward_pad?L"1":L"0");
        set_env(L"PS4RT_BUILD_VERSION", kVersion);
        set_env(L"PS4RT_PERF_CAPTURE_DIR", performance_capture ? capture_directory.wstring() : L"");
        set_env(L"PS4RT_CONFIG", (output / "ps4rt.ini").wstring());
        set_env(L"PS4RT_LANGUAGE", std::to_wstring(language));
        set_env(L"SO5_UI_LANGUAGE",ui_language);
        set_env(L"SO5_MENU_LOGO",(root/L"launcher/assets/star-ocean-logo.png").wstring());
        set_env(L"PS4RT_KEY_PROFILE", keyboard_profile == L"AZERTY" ? L"azerty" : keyboard_profile == L"QWERTY" ? L"qwerty" : L"custom");
        set_env(L"PS4RT_KEY_MAP", keyboard_buttons);
        set_env(L"PS4RT_KEY_AXES", keyboard_axes);
        set_env(L"PS4RT_PAD_PROFILE", controller_profile);
        set_env(L"PS4RT_INPUT_MODE", keyboard_only ? L"keyboard" : L"auto");
        set_env(L"PS4RT_SAVEDATA_BACKUP", automatic_save_backups ? L"1" : L"0");
        set_env(L"SO5_BACKUP_AT_LAUNCH",automatic_save_backups?L"1":L"0");
        set_env(L"PS4RT_SAVEDATA_TITLE_DIR", save_root);
        set_env(L"PS4RT_SAVEDATA_ROOT", L"");
        const fs::path overlay = root / "gpu" / "assets" / "star_ocean_overlay.png";
        if (fs::is_regular_file(overlay)) set_env(L"PS4RT_OVERLAY_BACKGROUND", overlay.wstring());
        // Use the integrated synthesized cues validated in the native 4K test.
        set_env(L"PS4RT_MENU_SOUND_DIR",L"");
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write_pipe;
        startup.hStdError = write_pipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        BOOL created = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW, nullptr, root.c_str(), &startup, &process);
        for (auto it = saved.rbegin(); it != saved.rend(); ++it)
            SetEnvironmentVariableW(it->key.c_str(), it->existed ? it->old.c_str() : nullptr);
        CloseHandle(write_pipe);
        if (!created) {
            CloseHandle(read_pipe);
            append_log(std::wstring(L"ERROR · could not start runtime (code ") + std::to_wstring(GetLastError()) + L")");
            std::lock_guard guard(app.mutex); app.busy = false;
            notify_log_updated(); PostMessageW(app.window, WM_QUICK_FAILED, 0, 0); return;
        }
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
            AssignProcessToJobObject(job, process.hProcess);
        }
        {
            std::lock_guard guard(app.mutex);
            app.job = job;
        }
        PostMessageW(app.window, WM_GAME_STARTED, process.dwProcessId, 0);
        char buffer[4096]; DWORD got{};
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got) {
            append_log_stream(from_utf8(buffer, static_cast<int>(got)));
            notify_log_updated();
        }
        flush_log_stream();
        WaitForSingleObject(process.hProcess, INFINITE);
        code = 0; GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(read_pipe); CloseHandle(process.hThread); CloseHandle(process.hProcess);
        {
            std::lock_guard guard(app.mutex);
            if (app.job == job) app.job = nullptr;
            app.busy = false;
            if (prepare_only && code == 0) app.prepared = true;
            app.status = code == 0 ? (prepare_only ? loc(L"Préparation terminée",L"Preparation complete") : loc(L"Session terminée",L"Session ended"))
                                   : loc(L"Arrêt avec une erreur · consultez le journal",L"Stopped with an error · see diagnostics");
        }
        if (job) CloseHandle(job);
        append_log(L"Process ended · code " + std::to_wstring(code));
        PostMessageW(app.window, WM_GAME_ENDED, code, 0);
    }).detach();
}

void stop_game() {
    std::lock_guard guard(app.mutex);
    if (app.job) {
        TerminateJobObject(app.job, 0);
        app.status = loc(L"Arrêt demandé…",L"Stop requested…");
    }
}

void rounded(Graphics& g, const RectF& r, REAL radius, const Brush& brush) {
    if(!g.IsVisible(r)) return;
    GraphicsPath path;
    REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

Font* cached_font(const std::wstring& value, REAL size, INT style) {
    const auto key = std::tuple{std::wstring(font_for(value)), int(std::max<REAL>(size,14)*118), style};
    auto& font = drawing_cache.fonts[key];
    if (!font) font = std::make_unique<Font>(std::get<0>(key).c_str(), std::get<1>(key)/100.0f, style, UnitPixel);
    return font.get();
}

void text(Graphics& g, const std::wstring& value, REAL x, REAL y, REAL width, REAL height,
          REAL size, Color color, FontStyle style = FontStyleRegular,
          StringAlignment alignment = StringAlignmentNear) {
    if(!g.IsVisible(RectF(x,y,width,height))) return;
    auto* font = cached_font(value, size, style);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(value.c_str(), -1, font, RectF(x, y, width, height), &format, &brush);
}

void paragraph(Graphics& g, const std::wstring& value, REAL x, REAL y, REAL width, REAL height,
               REAL size, Color color) {
    if(!g.IsVisible(RectF(x,y,width,height))) return;
    auto* font = cached_font(value, size, FontStyleRegular);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord);
    g.DrawString(value.c_str(), -1, font, RectF(x, y, width, height), &format, &brush);
}

void hit(int id, REAL x, REAL y, REAL width, REAL height) {
    if (id == HitNone) return;
    if (app.clipping_content_hits && (y + height <= kHeaderHeight + 4 || y >= app.content_bottom)) return;
    const REAL clipped_y = app.clipping_content_hits ? std::max<REAL>(y, kHeaderHeight + 4) : y;
    const REAL clipped_bottom = app.clipping_content_hits ? std::min<REAL>(y + height, app.content_bottom) : y + height;
    app.hits.push_back({id, RECT{static_cast<LONG>(x), static_cast<LONG>(clipped_y),
                                 static_cast<LONG>(x + width), static_cast<LONG>(clipped_bottom)}});
}

void button(Graphics& g, int id, const std::wstring& label, REAL x, REAL y, REAL width, REAL height,
            bool primary = false, bool enabled = true) {
    const bool hot = enabled && id != HitNone && app.hover == id;
    Color color = !enabled ? Color(115, 45, 52, 61)
                           : primary ? (hot ? Color(255, 126, 231, 255) : Color(255, 82, 207, 244))
                                     : (hot ? Color(220, 49, 75, 94) : Color(185, 30, 48, 61));
    SolidBrush brush(color);
    rounded(g, RectF(x, y, width, height), 12, brush);
    const Color label_color = primary && enabled ? Color(255, 2, 21, 31) : Color(255, 235, 242, 247);
    text(g, label, x, y, width, height, 15, label_color,
         FontStyleBold, StringAlignmentCenter);
    if (enabled) hit(id, x, y, width, height);
}

REAL label_width(Graphics& g, const std::wstring& label, REAL size, INT style) {
    auto* font = cached_font(label, size, style);
    StringFormat format;
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF measured;
    g.MeasureString(label.c_str(), -1, font, PointF(0, 0), &format, &measured);
    return std::ceil(measured.Width);
}

REAL action_width(Graphics& g, const std::wstring& label) {
    // Match button() typography and leave room around even the longest translation.
    return std::max<REAL>(180, label_width(g, label, 15, FontStyleBold) + 44);
}

void chip(Graphics& g, const std::wstring& label, Indicator indicator, REAL x, REAL y, REAL width) {
    const bool good = indicator == Indicator::Green;
    const bool warning = indicator == Indicator::Orange;
    SolidBrush brush(good ? Color(180, 21, 78, 70) : warning ? Color(190, 94, 66, 22) : Color(180, 81, 45, 48));
    rounded(g, RectF(x, y, width, 38), 10, brush);
    text(g, L"●", x + 12, y, 24, 38, 14,
         good ? Color(255, 63, 221, 168) : warning ? Color(255, 250, 183, 74) : Color(255, 238, 113, 113));
    text(g, label, x + 37, y, width - 45, 38, 13, Color(255, 224, 232, 237));
}

void chip(Graphics& g, const std::wstring& label, bool ok, REAL x, REAL y, REAL width) {
    chip(g, label, ok ? Indicator::Green : Indicator::Red, x, y, width);
}

void card(Graphics& g, REAL x, REAL y, REAL width, REAL height, BYTE alpha = 220) {
    SolidBrush brush(Color(alpha, 5, 21, 36));
    rounded(g, RectF(x, y, width, height), 15, brush);
}

void cached_artwork(Graphics& g, Image* image, REAL x, REAL y, REAL width, REAL height, bool cover) {
    if(!g.IsVisible(RectF(x,y,width,height))) return;
    if (!image || image->GetLastStatus() != Ok) return;
    const int w = std::max(1,int(std::ceil(width))), h = std::max(1,int(std::ceil(height)));
    auto& entries = drawing_cache.artwork;
    auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& e) {
        return e.source == image && e.width == w && e.height == h && e.cover == cover;
    });
    if (found == entries.end()) {
        // Reuse only the latest rasterization of each source, bounding memory during resizing.
        std::erase_if(entries, [&](const auto& e) { return e.source == image; });
        DrawingCache::Artwork entry{image,w,h,cover,std::make_unique<Bitmap>(w,h,PixelFormat32bppPARGB)};
        Graphics scaled(entry.bitmap.get()); scaled.Clear(Color(0,0,0,0));
        scaled.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        scaled.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        const REAL scale = cover ? std::max(width/image->GetWidth(),height/image->GetHeight())
                                 : std::min(width/image->GetWidth(),height/image->GetHeight());
        const REAL dw = image->GetWidth()*scale, dh = image->GetHeight()*scale;
        scaled.DrawImage(image,(width-dw)/2,(height-dh)/2,dw,dh);
        entries.push_back(std::move(entry)); found = std::prev(entries.end());
    }
    g.DrawImage(found->bitmap.get(),Rect(int(x),int(y),w,h),0,0,w,h,UnitPixel);
}

void image_fit(Graphics& g, Image* image, REAL x, REAL y, REAL width, REAL height) {
    cached_artwork(g,image,x,y,width,height,false);
}

void image_cover(Graphics& g, Image* image, REAL x, REAL y, REAL width, REAL height) {
    cached_artwork(g,image,x,y,width,height,true);
}

void page_title(Graphics& g, const wchar_t* overline, const wchar_t* title, REAL x, REAL y, REAL width) {
    text(g, overline, x, y, width, 20, 10, Color(255, 82, 207, 244), FontStyleBold);
    text(g, title, x, y + 19, width, 42, 28, Color(255, 244, 249, 252), FontStyleRegular);
}

void setting_row(Graphics& g, int id, const std::wstring& label, const std::wstring& value,
                 REAL x, REAL y, REAL width, bool enabled = true) {
    // HitNone means no control under the pointer, never a hovered read-only row.
    const bool interactive = enabled && id != HitNone;
    SolidBrush hover(interactive && app.hover == id ? Color(150, 30, 67, 91) : Color(80, 18, 45, 64));
    rounded(g, RectF(x, y, width, 54), 10, hover);
    text(g, label, x + 15, y + 2, width * .48f, 50, 14, enabled ? Color(255, 179, 203, 216) : Color(255, 92, 108, 118));
    text(g, value, x + width * .47f, y + 2, width * .47f - (enabled ? 29 : 4), 50, 14,
         enabled ? Color(255, 233, 242, 247) : Color(255, 105, 121, 131), FontStyleBold, StringAlignmentFar);
    if (interactive) text(g, L"⌄", x + width - 25, y, 18, 54, 17, Color(255, 82, 207, 244), FontStyleBold, StringAlignmentCenter);
    if (interactive) hit(id, x, y, width, 54);
}

void open_selector(int setting, std::initializer_list<const wchar_t*> values) {
    app.popup_enabled.clear();
    app.popup_hit = setting;
    app.popup_scroll=0;
    app.popup_values.clear();app.popup_enabled.clear();
    for (const auto* value : values) app.popup_values.emplace_back(value);
}

void open_selector(int setting, const std::vector<std::wstring>& values) {
    app.popup_enabled.clear();
    app.popup_hit = setting;
    app.popup_scroll=0;
    app.popup_values = values;
}

void draw_selector(Graphics& g, REAL width, REAL height) {
    if (app.popup_hit == HitNone || app.popup_values.empty()) return;
    hit(1999, 0, kHeaderHeight, width, height - kHeaderHeight);
    SolidBrush veil(Color(155, 0, 5, 12));
    g.FillRectangle(&veil, 0.0f, REAL(kHeaderHeight), width, height - kHeaderHeight);
    const REAL panel_width = std::min<REAL>(590, width - 100);
    const REAL row_height = 48;
    const int visible=std::min<int>(app.popup_values.size(),std::max(1,int((height-kHeaderHeight-90)/row_height)));
    app.popup_scroll=std::clamp(app.popup_scroll,0,int(app.popup_values.size())-visible);
    const REAL panel_height = 74 + row_height * visible;
    const REAL x = width - panel_width - 42;
    const REAL y = std::max<REAL>(kHeaderHeight+6, (height - panel_height) / 2);
    SolidBrush panel(Color(250, 5, 22, 36));
    rounded(g, RectF(x, y, panel_width, panel_height), 18, panel);
    text(g, loc(L"CHOISIR UNE OPTION",L"CHOOSE AN OPTION"), x + 22, y + 12, panel_width - 170, 42, 13,
         Color(255, 82, 207, 244), FontStyleBold);
    SolidBrush divider(Color(65, 82, 207, 244));
    if(visible<int(app.popup_values.size())) {
        button(g,1997,L"▲",x+panel_width-120,y+15,42,34,false,app.popup_scroll>0);
        button(g,1998,L"▼",x+panel_width-68,y+15,42,34,false,app.popup_scroll+visible<int(app.popup_values.size()));
    }
    for (int row = 0; row < visible; ++row) {
        const size_t i=app.popup_scroll+row;
        const REAL row_y = y + 62 + row * row_height;
        const bool enabled=app.popup_enabled.empty() || (i<app.popup_enabled.size() && app.popup_enabled[i]);
        const bool hot = enabled && app.hover == 2000 + static_cast<int>(i);
        if (hot) { SolidBrush selected(Color(160, 24, 78, 105)); rounded(g, RectF(x + 12, row_y, panel_width - 24, row_height - 3), 9, selected); }
        text(g, app.popup_values[i]+(enabled?L"":L" · "+std::wstring(loc(L"Indisponible",L"Unavailable"))), x + 25, row_y, panel_width - 70, row_height - 3, 15,
             enabled?Color(255, 232, 242, 247):Color(255,100,119,130), hot ? FontStyleBold : FontStyleRegular);
        text(g, L"›", x + panel_width - 48, row_y, 22, row_height - 3, 18, Color(255, 82, 207, 244), FontStyleBold, StringAlignmentCenter);
        g.FillRectangle(&divider, RectF(x + 22, row_y + row_height - 2, panel_width - 44, 1));
        if(enabled) hit(2000 + static_cast<int>(i), x + 12, row_y, panel_width - 24, row_height - 3);
    }
}

void draw_modal(Graphics& g, REAL width, REAL height) {
    if (!app.modal_visible) return;
    hit(HitModalBackdrop, 0, 0, width, height);
    SolidBrush veil(Color(205, 0, 5, 12));
    g.FillRectangle(&veil, 0.0f, 0.0f, width, height);
    const bool extracting = app.extraction_active && app.modal_action == ModalAction::CancelExtraction;
    const bool updating = app.modal_action == ModalAction::UpdateProgress;
    const REAL panel_width = std::min<REAL>(760, width - 100);
    auto* body_font = cached_font(app.modal_body,15,FontStyleRegular);
    RectF measured;
    g.MeasureString(app.modal_body.c_str(), -1, body_font, RectF(0, 0, panel_width - 60, 2000), &measured);
    const REAL body_height = std::clamp<REAL>(measured.Height + 14, 92, std::max<REAL>(92, height - 288));
    const REAL panel_height = extracting || updating ? 370.0f : body_height + 208;
    const REAL x = (width - panel_width) / 2, y = (height - panel_height) / 2;
    SolidBrush glow(Color(95, 82, 207, 244));
    rounded(g, RectF(x - 2, y - 2, panel_width + 4, panel_height + 4), 20, glow);
    SolidBrush panel(Color(255, 5, 22, 36));
    rounded(g, RectF(x, y, panel_width, panel_height), 18, panel);
    SolidBrush accent(Color(255, 82, 207, 244));
    g.FillEllipse(&accent, RectF(x + 28, y + 29, 46, 46));
    text(g, L"i", x + 28, y + 29, 46, 46, 24, Color(255, 2, 21, 31), FontStyleBold, StringAlignmentCenter);
    text(g, app.modal_title, x + 94, y + 25, panel_width - 125, 56, 22, Color(255, 244, 249, 252), FontStyleBold);
    paragraph(g, app.modal_body, x + 30, y + 102, panel_width - 60, extracting || updating ? 112.0f : body_height, 15, Color(255, 184, 207, 219));
    SolidBrush divider(Color(70, 82, 207, 244));
    if (updating) {
        const auto phase = app.update_phase.load();
        const wchar_t* label = phase == So5Update::Phase::Download ? loc(L"Téléchargement…",L"Downloading…") :
            phase == So5Update::Phase::Verify ? loc(L"Vérification du paquet…",L"Verifying package…") :
            phase == So5Update::Phase::Extract ? loc(L"Extraction…",L"Extracting…") :
            phase == So5Update::Phase::Install ? loc(L"Préparation de l’installation…",L"Preparing installation…") :
                                               loc(L"Vérification de l’application…",L"Checking application…");
        std::wstring detail = app.update_cancel ? loc(L"Annulation…",L"Canceling…") : label;
        const auto total = app.update_total.load(), done = std::min(app.update_done.load(), total);
        if (total && (phase == So5Update::Phase::Download || phase == So5Update::Phase::Extract))
            detail += L"  " + std::to_wstring(done * 100 / total) + L" % · " + format_size(done) + L" / " + format_size(total);
        text(g, detail, x + 30, y + 218, panel_width - 60, 27, 13, Color(255, 118, 199, 224), FontStyleBold);
        SolidBrush track(Color(255, 17, 49, 67)), fill(Color(255, 82, 207, 244));
        rounded(g, RectF(x + 30, y + 254, panel_width - 60, 12), 6, track);
        if (total && done) rounded(g, RectF(x + 30, y + 254, std::max<REAL>(12, (panel_width - 60) * double(done) / total), 12), 6, fill);
        if (!total) {
            const REAL travel = panel_width - 220;
            const REAL offset = REAL((GetTickCount64() / 7) % ULONGLONG(std::max<REAL>(1, travel)));
            rounded(g, RectF(x + 30 + offset, y + 254, 160, 12), 6, fill);
        }
        g.FillRectangle(&divider, RectF(x + 30, y + 282, panel_width - 60, 1));
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 244, y + 302, 214, 48, false,
               !app.update_cancel && phase != So5Update::Phase::Install);
        return;
    }
    if (extracting) {
        const ULONGLONG elapsed_seconds = (GetTickCount64() - app.extraction_started) / 1000;
        wchar_t elapsed[32]{};
        swprintf_s(elapsed, L"%02llu:%02llu", elapsed_seconds / 60, elapsed_seconds % 60);
        const auto total=app.extraction_total.load(),done=app.extraction_done.load();
        const auto percent=total ? std::min<std::uint64_t>(99,done*100/total) : 0;
        const std::wstring detail = !total ? loc(L"Analyse du PKG…",L"Analyzing PKG…") : done==total
            ? loc(L"Finalisation…",L"Finalizing…")
            : std::to_wstring(percent)+L" % · "+format_size(done)+L" / "+format_size(total);
        text(g, detail + L" · " + elapsed,
             x + 30, y + 218, panel_width - 60, 27, 12, Color(255, 118, 199, 224), FontStyleBold);
        SolidBrush track(Color(255, 17, 49, 67));
        rounded(g, RectF(x + 30, y + 250, panel_width - 60, 12), 6, track);
        SolidBrush pulse(Color(255, 82, 207, 244));
        if(total && done) {
            const REAL filled=(panel_width-60)*REAL(std::min(.99,double(done)/total));
            rounded(g,RectF(x+30,y+250,filled,12),std::min<REAL>(6,filled/2),pulse);
        }
        g.FillRectangle(&divider, RectF(x + 30, y + 282, panel_width - 60, 1));
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 244, y + 302, 214, 48);
        return;
    }
    g.FillRectangle(&divider, RectF(x + 30, y + panel_height - 89, panel_width - 60, 1));
    if (!app.modal_tertiary.empty()) {
        const REAL button_width = 194, gap = 12;
        const REAL start = x + panel_width - (button_width * 3 + gap * 2) - 30;
        button(g, HitModalSecondary, app.modal_secondary, start, y + panel_height - 70, button_width, 48);
        button(g, HitModalTertiary, app.modal_tertiary, start + button_width + gap, y + panel_height - 70, button_width, 48);
        button(g, HitModalPrimary, app.modal_primary, start + (button_width + gap) * 2, y + panel_height - 70, button_width, 48, true);
    } else if (app.modal_secondary.empty()) {
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 224, y + panel_height - 70, 194, 48, true);
    } else {
        button(g, HitModalSecondary, app.modal_secondary, x + panel_width - 434, y + panel_height - 70, 194, 48);
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 224, y + panel_height - 70, 194, 48, true);
    }
}

void toggle_row(Graphics& g, int id, const std::wstring& label, bool value, REAL x, REAL y, REAL width,bool enabled=true) {
    SolidBrush hover(id != HitNone && enabled && app.hover == id ? Color(150, 30, 67, 91) : Color(80, 18, 45, 64));
    rounded(g, RectF(x, y, width, 50), 9, hover);
    paragraph(g, label, x + 13, y + 5, width - 80, 40, 12, enabled?Color(255, 174, 196, 208):Color(255,102,122,134));
    SolidBrush track(value ? Color(220, 38, 126, 154) : Color(220, 44, 62, 74));
    rounded(g, RectF(x + width - 57, y + 15, 42, 21), 10, track);
    SolidBrush knob(value ? Color(255, 94, 221, 250) : Color(255, 137, 153, 164));
    g.FillEllipse(&knob, RectF(x + width - (value ? 35.0f : 54.0f), y + 18, 15, 15));
    if(enabled) hit(id,x,y,width,50);
}

void draw_header(Graphics& g, REAL width) {
    SolidBrush header(Color(242, 2, 12, 22));
    g.FillRectangle(&header, 0.0f, 0.0f, width, REAL(kHeaderHeight));
    SolidBrush line(Color(80, 82, 207, 244));
    g.FillRectangle(&line, 0.0f, REAL(kHeaderHeight - 1), width, 1.0f);
    image_cover(g, app.launcher_icon.get(), 20, 39, 48, 48);
    text(g, L"STAR OCEAN 5", 80, 38, 155, 28, 15, Color(255, 244, 249, 252), FontStyleBold);
    text(g, L"WINDOWS", 80, 66, 150, 20, 11, Color(255, 82, 207, 244), FontStyleBold);

    const UiWords w = words();
    struct Nav { Page page; int id; const wchar_t* label; } nav[] = {
        {Page::Home, HitHome, w.home}, {Page::Game, HitGame, w.game},
        {Page::Cheats,HitCheats,loc(L"Triches",L"Cheats")},
        {Page::Tweaks,HitTweaks,ps4rt_localize(app.launcher_language,L"Tweaks")},
        {Page::Video, HitVideo, w.video}, {Page::Controls, HitControls, w.controls},
        {Page::Advanced, HitAdvanced, w.advanced}, {Page::Logs, HitLogs, w.logs}
    };
    const REAL nav_x = 245, nav_width = width - nav_x - 18;
    const REAL tab_width = nav_width / REAL(std::size(nav));
    for (int i = 0; i < int(std::size(nav)); ++i) {
        const REAL x = nav_x + i * tab_width;
        const bool selected = app.page == nav[i].page;
        if (selected || app.hover == nav[i].id) {
            SolidBrush selected_brush(selected ? Color(150, 25, 69, 91) : Color(75, 25, 69, 91));
            rounded(g, RectF(x + 3, 42, tab_width - 6, 46), 9, selected_brush);
        }
        // Long translated tab names can use two lines on laptop-sized windows.
        SolidBrush nav_text(selected ? Color(255,244,249,252) : Color(255,142,165,179));
        StringFormat nav_format; nav_format.SetAlignment(StringAlignmentCenter);
        nav_format.SetLineAlignment(StringAlignmentCenter);
        nav_format.SetTrimming(StringTrimmingEllipsisWord);
        g.DrawString(nav[i].label,-1,cached_font(nav[i].label,13,selected?FontStyleBold:FontStyleRegular),
                     RectF(x+5,40,tab_width-10,52),&nav_format,&nav_text);
        if (selected) g.FillRectangle(&line, RectF(x + 16, 100, tab_width - 32, 3));
        hit(nav[i].id, x + 2, 39, tab_width - 4, 52);
    }
}

void draw_home(Graphics& g, REAL width, REAL height) {
    const REAL logo_width = std::min<REAL>(600, width * .50f);
    const REAL logo_height = std::min<REAL>(310,(height-250)*.67f);
    image_fit(g, app.game_logo.get(), (width - logo_width) / 2, 108, logo_width, logo_height);
    const bool valid = !app.pkg_selected && app.game_verified;
    const UiWords w = words();
    const std::wstring play_label = app.busy ? L"■   STOP" : valid ? std::wstring(L"▶   ") + w.play : w.select_game;
    const REAL button_width = std::min<REAL>(390, width * .34f);
    button(g, app.busy ? HitStop : HitPlay, play_label, (width - button_width) / 2,
           std::max<REAL>(108+logo_height+20, height-255), button_width, 78, true);

    const REAL dock_x = 28, dock_y = height - 105, dock_width = width - 56, item_width = dock_width / 5.0f;
    card(g, dock_x, dock_y, dock_width, 78, 238);
    const std::array<std::pair<std::wstring, std::wstring>, 5> items = {{
        {loc(L"ÉTAT",L"STATUS"), app.prepared ? loc(L"Jeu prêt",L"Game ready") : loc(L"Préparation requise",L"Preparation required")},
        {loc(L"AFFICHAGE",L"DISPLAY"), app.resolution + L" · " + display_mode_label()},
        {loc(L"RECONSTRUCTION",L"UPSCALING"), fsr_label()},
        {loc(L"SAUVEGARDE",L"SAVES"), app.automatic_save_backups ? loc(L"Backup automatique",L"Automatic backup") : loc(L"Backup désactivé",L"Backup disabled")},
        {loc(L"SYSTÈME",L"SYSTEM"), app.vulkan_ok ? L"Vulkan + Native C++" : loc(L"Pilote Vulkan requis",L"Vulkan driver required")}
    }};
    const int ids[5] = {HitGame, HitVideo, HitVideo, HitGame, HitLogs};
    SolidBrush dock_divider(Color(55, 82, 207, 244));
    for (int i = 0; i < 5; ++i) {
        if (i) g.FillRectangle(&dock_divider, RectF(dock_x + i * item_width, dock_y + 13, 1, 52));
        text(g, items[i].first, dock_x + i * item_width + 14, dock_y + 10, item_width - 28, 20, 9, Color(255, 94, 135, 158), FontStyleBold);
        text(g, items[i].second, dock_x + i * item_width + 14, dock_y + 30, item_width - 28, 34, 11, Color(255, 224, 237, 244), FontStyleBold);
        hit(ids[i], dock_x + i * item_width, dock_y, item_width, 78);
    }
}

void draw_game(Graphics& g, REAL x, REAL y, REAL width, REAL height) {
    page_title(g, loc(L"CONFIGURATION",L"CONFIGURATION"), loc(L"Jeu",L"Game"), x, y, width);
    const REAL top = y + 70, gap = 18, left_width = width * .55f, right_x = x + left_width + gap, right_width = width - left_width - gap;
    card(g, x, top, left_width, 150);
    text(g, loc(L"DOSSIER DU JEU",L"GAME DIRECTORY"), x + 18, top + 12, left_width - 36, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    text(g, app.game_path.empty() ? loc(L"Aucun dossier sélectionné",L"No directory selected") : app.game_path, x + 18, top + 40, left_width - 36, 34, 12, Color(255, 226, 238, 244));
    button(g, HitFolder, loc(L"Sélectionner un dossier",L"Select Folder"), x + 18, top + 89, left_width * .48f, 43);
    button(g, HitPkg, loc(L"Sélectionner un .pkg",L"Select a .pkg"), x + left_width * .52f, top + 89, left_width * .43f, 43);

    card(g, x, top + 164, left_width, 210);
    text(g, loc(L"DOSSIER DES SAUVEGARDES",L"SAVE DIRECTORY"), x + 18, top + 176, left_width - 36, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    const std::wstring save_path = save_directory().lexically_normal().wstring();
    paragraph(g, save_path, x + 18, top + 204, left_width - 36, 40, 12, Color(255, 226, 238, 244));
    button(g, HitSaveFolder, loc(L"Modifier",L"Change"), x + 18, top + 248, 122, 42);
    button(g, HitDefaultSaveFolder, loc(L"Par défaut",L"Default"), x + 151, top + 248, 122, 42, false, !app.save_root.empty());
    button(g, HitOpenSaves, loc(L"Ouvrir",L"Open"), x + 284, top + 248, 112, 42);
    toggle_row(g, HitBackupToggle, loc(L"Copie de sécurité automatique des sauvegardes de jeu au lancement",L"Automatic game-save backups at launch"), app.automatic_save_backups,
               x + 18, top + 300, left_width - 36);

    card(g, x, top + 388, left_width, std::max<REAL>(260, height - 458));
    text(g, loc(L"INFORMATIONS DÉTECTÉES",L"DETECTED INFORMATION"), x + 18, top + 400, left_width - 36, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    const REAL info_y = top + 433;
    const std::wstring empty_value;
    const bool identified = app.game_identity != GameIdentity::None;
    const std::array<std::pair<std::wstring, std::wstring>, 5> info = {{
        {loc(L"Titre",L"Title"), identified ? app.detected_game_title : empty_value},
        {L"ID", identified ? app.detected_title_id : empty_value},
        {loc(L"Version",L"Version"), identified ? app.detected_game_version : empty_value},
        {loc(L"Région",L"Region"), identified ? app.detected_region : empty_value},
        {loc(L"État",L"Status"), identified ? app.status : empty_value}
    }};
    for (size_t i = 0; i < info.size(); ++i) {
        const REAL row_y = info_y + i * 25;
        text(g, info[i].first, x + 18, row_y, 82, 23, 10, Color(255, 105, 133, 150));
        const Color value_color = i == 4 && app.game_identity == GameIdentity::Rejected ? Color(255, 247, 119, 119) :
                                  i == 4 && app.game_identity == GameIdentity::Untested ? Color(255, 250, 183, 74) :
                                  i == 4 && app.prepared ? Color(255, 65, 226, 170) : Color(255, 219, 232, 239);
        text(g, info[i].second, x + 102, row_y, left_width - 120, 23, 10, value_color);
    }
    button(g, HitPrepare, app.prepared ? loc(L"Préparer à nouveau",L"Prepare again") : loc(L"Préparer le jeu",L"Prepare game"),
           x + 18, top + 575, 190, 38, false,
           !app.busy && !app.pkg_selected && app.game_verified);

    card(g, right_x, top, right_width, 500);
    text(g, loc(L"DERNIÈRE SAUVEGARDE",L"LATEST SAVE"), right_x + 16, top + 11, right_width - 32, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    image_cover(g, app.save_preview.get(), right_x + 16, top + 42, right_width - 32, 305);
    text(g, app.save_title, right_x + 16, top + 354, right_width - 32, 30, 15, Color(255, 239, 246, 249), FontStyleBold);
    text(g, app.save_subtitle.empty() ? app.save_date : app.save_subtitle, right_x + 16, top + 384, right_width - 32, 25, 12, Color(255, 118, 151, 169));
    paragraph(g, app.save_detail, right_x + 16, top + 412, right_width - 32, 40, 12, Color(255, 169, 190, 201));
    button(g, HitOpenBackups, loc(L"Ouvrir les sauvegardes de sécurité",L"Open save backups"), right_x + 16, top + 458,
           right_width - 32, 34, false, app.game_verified && !app.pkg_selected);

    card(g, right_x, top + 514, right_width, std::max<REAL>(176, height - 584));
    setting_row(g, HitGameLanguage, loc(L"Langue du jeu",L"Game language"), app.game_languages.empty() ? L"—" : app.game_language,
                right_x + 12, top + 527, right_width - 24, !app.game_languages.empty() && !app.busy && !app.pkg_selected);
    button(g, HitShortcut, loc(L"Créer les raccourcis Bureau",L"Create desktop shortcuts"), right_x + 12, top + 587, right_width - 24, 42);
}


void refresh_native_cheats() {
    if(app.cheat_loading){app.cheat_refresh_pending=true;return;}
    if(app.cheat_thread.joinable())app.cheat_thread.join();
    if(!app.game_verified||app.pkg_selected){app.cheat_entries.clear();return;}
    app.cheat_loading=true;
    const auto game=fs::path(app.game_path),output=app.root/L"out/CUSA03219";
    const auto expected_title=app.detected_title_id,expected_version=app.detected_game_version;
    const unsigned generation=++app.cheat_generation;const HWND window=app.window;
    app.cheat_thread=std::thread([game,output,expected_title,expected_version,generation,window] {
        auto event=std::make_unique<CheatInspection>();event->generation=generation;
        try {
            ps4rt::prepare_title(game,output,{},&event->result);
            const auto title=ps4rt::title_id_from_sfo(game/L"sce_sys/param.sfo"),version=ps4rt::string_from_sfo(game/L"sce_sys/param.sfo","APP_VER");
            if(from_utf8(title.data(),int(title.size()))!=expected_title||from_utf8(version.data(),int(version.size()))!=expected_version)throw std::runtime_error("game identity changed during inspection");
        }catch(const std::exception& e){event->error=e.what();}
        {std::lock_guard lock(app.cheat_mutex);app.cheat_result=std::move(event);}
        PostMessageW(window,WM_CHEATS_READY,0,0);
    });
}
void receive_native_cheats() {
    if(app.cheat_thread.joinable())app.cheat_thread.join();
    std::unique_ptr<CheatInspection> event;{std::lock_guard lock(app.cheat_mutex);event=std::move(app.cheat_result);}
    app.cheat_loading=false;
    if(event&&event->generation==app.cheat_generation) {
        if(event->error.empty()) {
            app.cheat_entries=std::move(event->result.entries);
        } else {
            append_log(L"Cheats: "+from_utf8(event->error.data(),int(event->error.size())));
        }
        for(const auto& problem:event->result.problems)append_log(L"Cheats: "+from_utf8(problem.data(),int(problem.size())));
    }
    if(app.cheat_refresh_pending){app.cheat_refresh_pending=false;if(app.page==Page::Cheats)refresh_native_cheats();}
    InvalidateRect(app.window,nullptr,FALSE);
}
int cheats_content_height(){
    int height=286;bool any=false;
    for(const auto& entry:app.cheat_entries)if(entry.builtin){any=true;height+=entry.id==8?140:108;}
    return height+(any?0:54);
}
void draw_cheats(Graphics& g,REAL x,REAL y,REAL width,REAL) {
    auto tr=[](const wchar_t* value){return ps4rt_localize(app.launcher_language,value);};
    page_title(g,tr(L"Built-in cheats"),tr(L"Cheats"),x,y,width);
    if(app.game_verified)text(g,app.detected_title_id+L" / APP_VER "+app.detected_game_version,x+width*.5f,y+34,width*.5f,30,14,Color(255,168,206,224),FontStyleRegular,StringAlignmentFar);
    card(g,x,y+76,width,122);
    toggle_row(g,HitCheatsToggle,tr(L"Allow cheats"),app.cheats_enabled,x+12,y+84,width-24,!app.game_running);
    paragraph(g,tr(L"All cheats start off. Activate them in the in-game menu."),x+26,y+146,width-52,46,15,Color(255,174,200,214));
    REAL row=y+216;
    {
        text(g,tr(L"Built-in cheats"),x+4,row,width-8,32,17,Color(255,82,207,244),FontStyleBold);row+=42;
        bool any=false;
        for(const auto& entry:app.cheat_entries)if(entry.builtin) {
            any=true;
            const REAL row_height=entry.builtin&&entry.id==8?132:100;
            if(g.IsVisible(RectF(x,row,width,row_height))) {
                card(g,x,row,width,row_height);
                const auto* definition=entry.builtin?So5Cheats::Catalog(entry.id):nullptr;
                const auto title=definition?std::wstring(tr(definition->title)):from_utf8(entry.name.data(),int(entry.name.size()));
                std::wstring detail=definition?std::wstring(tr(definition->detail)):entry.description.empty()?std::wstring(tr(L"No description supplied by the profile.")):from_utf8(entry.description.data(),int(entry.description.size()));
                if(definition&&definition->warning)detail+=L"\n"+std::wstring(tr(definition->warning));
                text(g,title,x+20,row+10,width-230,30,17,Color(255,235,244,248),FontStyleBold);
                paragraph(g,detail,x+20,row+43,width-230,row_height-47,15,Color(255,175,201,215));
                const auto state=tr(entry.available?L"Ready":L"Unavailable for this executable");
                paragraph(g,state,x+width-196,row+14,176,44,14,entry.available?Color(255,88,220,178):Color(255,244,184,101));
            }
            row+=row_height+8;
        }
        if(!any){paragraph(g,tr(app.cheat_loading?L"Checking cheat compatibility...":L"No compatible built-in cheats."),x+20,row,width-40,42,15,Color(255,174,200,214));row+=54;}
        row+=12;
    }
}

bool tweaks_supported() {
    return app.game_verified&&!app.pkg_selected&&app.detected_title_id==L"CUSA03219"&&app.detected_game_version==L"01.00";
}
fs::path tweaks_config(){return app.root/L"out/CUSA03219/ps4rt.ini";}
void refresh_tweaks() {
    app.tweaks=So5Tweaks::Read(tweaks_config());
    if(!tweaks_supported()||app.game_running||app.busy)return;
    const auto aspect=requested_ultrawide()?(Ps4RtOutput::Group(Ps4RtOutput::Parse(app.resolution))==Ps4RtOutput::Ratio32_9?So5Hud::Aspect::Superwide:So5Hud::Aspect::Wide):So5Hud::Aspect::Standard;
    if(app.hud_library&&app.hud_library->Snapshot().busy)return;
    if(!app.hud_library||app.hud_aspect!=aspect) {
        app.hud_aspect=aspect;app.hud_selection.clear();
        app.hud_library=std::make_unique<So5Hud::PresetLibrary>(tweaks_config().parent_path(),aspect,true);
    }
    app.hud_library->Refresh();app.hud_snapshot=app.hud_library->Snapshot();
    SetTimer(app.window,5,100,nullptr);
}
void save_tweak(const char* key,int value) {
    if(app.game_running||app.busy||!tweaks_supported())return;
    try {So5Tweaks::Write(tweaks_config(),key,value);}
    catch(const std::exception& error) {append_log(L"Tweaks: "+from_utf8(error.what(),int(std::strlen(error.what()))));}
    app.tweaks=So5Tweaks::Read(tweaks_config());
}
constexpr int tweaks_content_height=920;
void draw_tweaks(Graphics& g,REAL x,REAL y,REAL width,REAL) {
    auto tr=[](const wchar_t* value){return ps4rt_localize(app.launcher_language,value);};
    page_title(g,tr(L"Tweaks"),tr(L"Tweaks"),x,y,width);
    paragraph(g,tr(L"Configure before launch. During play, use Tweaks in the in-game menu."),x+8,y+76,width-16,62,15,Color(255,174,200,214));
    const bool enabled=tweaks_supported()&&!app.game_running&&!app.busy;
    REAL row=y+150;
    card(g,x,row,width,190);
    text(g,std::wstring(loc(L"Changer de leader",L"Change leader"))+L" (Let Me Lead! Fidel!)",x+20,row+12,width-40,32,17,Color(255,235,244,248),FontStyleBold);
    paragraph(g,tr(L"During exploration, use D-pad left/right or O/P to change leader. Your choice is saved."),x+20,row+50,width-40,60,15,Color(255,175,201,215));
    toggle_row(g,HitLeaderEnabled,tr(L"Change leader during exploration"),app.tweaks.leader,x+12,row+124,width-24,enabled);
    row+=206;
    card(g,x,row,width,190);
    text(g,tr(L"Camera fixes"),x+20,row+12,width-40,32,17,Color(255,235,244,248),FontStyleBold);
    paragraph(g,tr(L"Softens positional camera follow. Original restores the game setting."),x+20,row+50,width-40,60,15,Color(255,175,201,215));
    const wchar_t* modes[]={L"Original",L"Light damping",L"Damped follow"};
    setting_row(g,HitCameraFollow,tr(L"Camera follow"),tr(modes[std::clamp(app.tweaks.camera,0,2)]),x+12,row+124,width-24,enabled);
    row+=206;
    card(g,x,row,width,250);
    text(g,tr(L"HUD adjust"),x+20,row+12,width-40,32,17,Color(255,235,244,248),FontStyleBold);
    paragraph(g,tr(L"Place exported JSON presets in the game’s hud-presets folder. Incompatible presets are disabled."),x+20,row+50,width-40,62,15,Color(255,175,201,215));
    const bool hud_enabled=enabled&&app.hud_library&&!app.hud_snapshot.busy;
    setting_row(g,HitHudPreset,tr(L"HUD layout"),app.hud_selection.empty()?tr(L"Select a HUD preset"):app.hud_selection,x+12,row+120,width-24,hud_enabled&&!app.hud_snapshot.entries.empty());
    button(g,HitHudRefresh,tr(L"Refresh presets"),x+12,row+184,(width-36)/2,48,false,enabled&&!app.hud_snapshot.busy);
    button(g,HitHudOpenPresets,tr(L"Open HUD presets folder"),x+24+(width-36)/2,row+184,(width-36)/2,48,false,tweaks_supported());
    row+=260;
    const wchar_t* status=nullptr;
    switch(app.hud_snapshot.status) {
    case So5Hud::LibraryStatus::Scanning:status=L"Scanning HUD presets…";break;
    case So5Hud::LibraryStatus::Applying:status=L"Applying HUD layout…";break;
    case So5Hud::LibraryStatus::Saved:status=L"HUD layout saved for the next launch.";break;
    case So5Hud::LibraryStatus::Failed:status=L"HUD layout could not be changed. See Logs and diagnostics.";break;
    default:break;
    }
    if(!tweaks_supported())status=L"This patch is unavailable for the prepared game version.";
    if(status)paragraph(g,tr(status),x+20,row,width-40,76,15,Color(255,174,200,214));
}

void draw_video(Graphics& g, REAL x, REAL y, REAL width, REAL) {
    page_title(g, loc(L"AFFICHAGE ET QUALITÉ",L"DISPLAY AND QUALITY"), loc(L"Vidéo",L"Video"), x, y, width);
    const REAL selector_top = y + 70;
    card(g, x, selector_top, width, 144);
    setting_row(g, HitGpu, loc(L"Carte graphique",L"Graphics card"), gpu_label(),
                x + 12, selector_top + 10, width - 24);
    setting_row(g,HitMonitor,loc(L"Écran cible",L"Target monitor"),selected_display_label(),
                x+12,selector_top+76,width-24,app.displays.size()>1);

    const REAL top = selector_top + 162, gap = 18, column = (width - gap) / 2.0f;
    card(g, x, top, column, 470);
    card(g, x + column + gap, top, column, 470);
    text(g, loc(L"AFFICHAGE",L"DISPLAY"), x + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitDisplayMode, loc(L"Mode",L"Display mode"), display_mode_label(), x + 12, top + 47, column - 24);
    toggle_row(g, HitVsync, loc(L"Synchronisation verticale",L"Vertical synchronization"), app.vsync, x + 12, top + 105, column - 24);
    setting_row(g, HitFpsLimit, loc(L"Limite d’IPS",L"Frame-rate limit"), app.fps_limit ? std::to_wstring(app.fps_limit) + L" FPS" : loc(L"Illimitée",L"Unlimited"), x + 12, top + 163, column - 24);
    setting_row(g,HitOutputAspect,loc(L"Format d’image",L"Aspect ratio"),output_aspect_label(app.output_aspect),x+12,top+221,column-24);
    setting_row(g, HitResolution, loc(L"Résolution de sortie",L"Output resolution"),available_outputs()?app.resolution:loc(L"Indisponible",L"Unavailable"),x+12,top+279,column-24,available_outputs()!=0);
    setting_row(g, HitAspectRatio, loc(L"Ajustement de l’image",L"Image fitting"),
                app.window_stretch ? loc(L"Étirer à la fenêtre",L"Stretch to window") : loc(L"Original",L"Original"),
                x + 12, top + 337, column - 24, app.display_mode == L"windowed");
    paragraph(g, requested_ultrawide()?
        loc(L"Ultralarge expérimental : redémarrez le jeu pour changer de format. Le HUD et les vidéos conservent leurs proportions d’origine.",
            L"Experimental ultrawide: restart the game to change aspect ratio. HUD and movies keep their original proportions."):
        loc(L"Le jeu s’ouvre sur l’écran sélectionné. La scène et le HUD restent en 16:9.",
            L"The game opens on the selected monitor. The scene and HUD remain 16:9."), x + 18, top + 402, column - 36, 58, 12, Color(255, 128, 154, 168));

    const REAL middle = x + column + gap;
    text(g, loc(L"RENDU",L"RENDERING"), middle + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitFsr, loc(L"Reconstruction",L"Upscaling"), fsr_label(), middle + 12, top + 47, column - 24,
                Ps4RtOutput::TemporalAllowed(Ps4RtOutput::Parse(app.resolution)));
    setting_row(g, HitNativeRender, loc(L"Résolution de rendu",L"Render resolution"),
                ([&] { const auto output=requested_scene_output(); const auto input=Ps4RtScene::RequestedInput(app.fsr!=L"off",app.scene_preset,app.native_render_height,output.width,output.height,requested_ultrawide()); return std::to_wstring(input.width)+L" × "+std::to_wstring(input.height); })(), middle + 12, top + 105, column - 24, app.fsr==L"off");
    setting_row(g, HitAnisotropy, loc(L"Filtrage anisotrope",L"Anisotropic filtering"), app.anisotropy ? L"×" + std::to_wstring(app.anisotropy) : loc(L"Réglage du jeu",L"Game setting"), middle + 12, top + 163, column - 24);
    toggle_row(g, HitAsyncShaders, loc(L"Shaders asynchrones",L"Asynchronous shaders"), app.async_shaders, middle + 12, top + 221, column - 24);
    paragraph(g,loc(L"Sans reconstruction, choisissez manuellement la résolution interne. L’UI conserve la résolution de sortie. Avec FSR ou DLSS, les presets gèrent la résolution interne.",
        L"Without reconstruction, choose the internal resolution manually. The UI keeps the output resolution. With FSR or DLSS, quality presets control the internal resolution."),
        middle+16,top+280,column-32,82,12,Color(255,147,174,188));
    paragraph(g, app.async_shaders ? loc(L"Gameplay plus fluide et moins de saccades de compilation. De brefs scintillements visuels peuvent apparaître.",
                                         L"Smoother gameplay with fewer shader compilation stutters. Brief visual flicker may occasionally appear.")
                                   : loc(L"Compilation synchrone : moins d’artefacts temporaires, mais des saccades peuvent survenir lors du premier passage.",
                                         L"Synchronous compilation: fewer temporary artifacts, but first-time shader compilation may stutter."),
              middle + 16, top + 370, column - 32, 82, 10, Color(255, 128, 154, 168));

    const REAL lower = top + 488;
    card(g, x, lower, width, 225);
    text(g, loc(L"COMPATIBILITÉ",L"COMPATIBILITY"), x + 18, lower + 12, width - 36, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    const REAL chip_width = (width - 86) / 4.0f;
    chip(g, L"Vulkan 1.3", app.vulkan_ok, x + 16, lower + 50, chip_width);
    chip(g, loc(L"FSR 3.1 intégré",L"Integrated FSR 3.1"), true, x + 34 + chip_width, lower + 50, chip_width);
    chip(g, loc(L"FSR 4 avec repli sûr",L"FSR 4 with safe fallback"), true, x + 52 + chip_width * 2, lower + 50, chip_width);
    chip(g, loc(L"Cache de shaders",L"Shader cache"), app.shader_cache_present, x + 70 + chip_width * 3, lower + 50, chip_width);
    paragraph(g, loc(L"FSR 4 est vérifié par le runtime. Si le GPU ou les shaders fournis ne sont pas compatibles, il revient automatiquement à FSR 3.1 avant la première image.",
                     L"FSR 4 is checked by the runtime. If the GPU or supplied shaders are incompatible, it automatically falls back to FSR 3.1 before the first frame."),
              x + 18, lower + 108, width - 36, 88, 12, Color(255, 147, 174, 188));
}

void draw_controls(Graphics& g, REAL x, REAL y, REAL width, REAL) {
    page_title(g, loc(L"PÉRIPHÉRIQUES ET MAPPAGE",L"DEVICES AND MAPPING"), loc(L"Commandes",L"Controls"), x, y, width);
    const REAL top = y + 70;
    card(g, x, top, width, 116);
    setting_row(g, HitController, loc(L"Périphérique actif",L"Active device"), controller_label(), x + 14, top + 14, width * .50f);
    setting_row(g, HitKeyboardProfile, loc(L"Profil clavier",L"Keyboard profile"), keyboard_profile_label(), x + width * .52f, top + 14, width * .29f);
    button(g, HitResetControls, loc(L"Réinitialiser",L"Reset"), x + width * .83f, top + 14, width * .155f, 54);
    text(g, loc(L"Seules les manettes actuellement détectées par SDL3 sont proposées. Le clavier reste toujours disponible.",
                L"Only controllers currently detected by SDL3 are listed. Keyboard is always available."),
         x + 18, top + 72, width - 36, 28, 12, Color(255, 132, 166, 184));

    card(g, x, top + 132, width, 760);
    text(g, loc(L"MAPPAGE CLAVIER COMPLET",L"FULL KEYBOARD MAPPING"), x + 20, top + 145, width - 40, 28, 14, Color(255, 82, 207, 244), FontStyleBold);
    text(g, app.capture_binding >= 0 ? loc(L"Appuyez sur une touche · Échap pour annuler",L"Press a key · Escape to cancel")
                                      : loc(L"Cliquez sur une touche pour la remplacer. Chaque changement est enregistré immédiatement.",
                                            L"Click a key to replace it. Every change is saved immediately."),
         x + 20, top + 177, width - 40, 29, 12,
         app.capture_binding >= 0 ? Color(255, 108, 226, 249) : Color(255, 135, 165, 181),
         app.capture_binding >= 0 ? FontStyleBold : FontStyleRegular);

    const std::array<std::wstring, 25> labels = {{
        loc(L"Croix · Confirmer",L"Cross · Confirm"), loc(L"Rond · Annuler",L"Circle · Cancel"),
        loc(L"Carré · Action",L"Square · Action"), loc(L"Triangle · Menu",L"Triangle · Menu"),
        L"L1", L"R1", L"L2", L"R2", L"L3", L"R3", L"Options",
        loc(L"Pavé tactile gauche",L"Touchpad left"), loc(L"Pavé tactile droit",L"Touchpad right"),
        loc(L"Croix directionnelle · Haut",L"D-pad · Up"), loc(L"Croix directionnelle · Bas",L"D-pad · Down"),
        loc(L"Croix directionnelle · Gauche",L"D-pad · Left"), loc(L"Croix directionnelle · Droite",L"D-pad · Right"),
        loc(L"Stick gauche · Gauche",L"Left stick · Left"), loc(L"Stick gauche · Droite",L"Left stick · Right"),
        loc(L"Stick gauche · Haut",L"Left stick · Up"), loc(L"Stick gauche · Bas",L"Left stick · Down"),
        loc(L"Stick droit · Gauche",L"Right stick · Left"), loc(L"Stick droit · Droite",L"Right stick · Right"),
        loc(L"Stick droit · Haut",L"Right stick · Up"), loc(L"Stick droit · Bas",L"Right stick · Down")
    }};
    const REAL gap = 26, column_width = (width - 40 - gap) / 2.0f, row_height = 48;
    const REAL start_y = top + 218;
    for (size_t i = 0; i < labels.size(); ++i) {
        const int column_index = i < 13 ? 0 : 1;
        const int row_index = column_index == 0 ? static_cast<int>(i) : static_cast<int>(i - 13);
        const REAL row_x = x + 20 + column_index * (column_width + gap);
        const REAL row_y = start_y + row_index * row_height;
        SolidBrush row(app.hover == kBindingHitBase + static_cast<int>(i) ? Color(130, 27, 66, 87) : Color(60, 18, 45, 64));
        rounded(g, RectF(row_x, row_y, column_width, 42), 9, row);
        text(g, labels[i], row_x + 13, row_y, column_width * .61f, 42, 12, Color(255, 207, 224, 233));
        const bool capturing = app.capture_binding == static_cast<int>(i);
        button(g, kBindingHitBase + static_cast<int>(i), capturing ? L"…" : app.key_bindings[i],
               row_x + column_width * .62f, row_y + 4, column_width * .36f - 5, 34, capturing);
    }
    paragraph(g, loc(L"Les profils AZERTY et QWERTY rétablissent une configuration complète. Dès qu’une touche est modifiée, le profil devient Personnalisé et le runtime reçoit ce mappage au prochain lancement.",
                     L"AZERTY and QWERTY restore a complete preset. Editing any key switches to Custom and the runtime receives the mapping on the next launch."),
              x + 22, top + 853, width - 44, 55, 12, Color(255, 137, 169, 186));
}

void draw_advanced(Graphics& g, REAL x, REAL y, REAL width, REAL) {
    page_title(g, loc(L"SYSTÈME ET MAINTENANCE",L"SYSTEM AND MAINTENANCE"), loc(L"Avancé",L"Advanced"), x, y, width);
    const REAL top = y + 70, gap = 18, column = (width - gap) / 2.0f;
    card(g, x, top, column, 225);
    text(g, loc(L"LANGUE DU LAUNCHER",L"LAUNCHER LANGUAGE"), x + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitLauncherLanguage, loc(L"Interface",L"Interface"), app.launcher_language, x + 12, top + 48, column - 24);
    paragraph(g, loc(L"La langue de Windows est choisie au premier démarrage. La langue du launcher reste indépendante de celle du jeu.",
                     L"The Windows language is selected on first launch. The launcher language remains independent from the game language."), x + 17, top + 112, column - 34, 65, 10, Color(255, 135, 161, 175));
    button(g, HitOpenRoot, loc(L"Ouvrir le dossier de l’application",L"Open application folder"), x + 14, top + 174, column - 28, 37);

    const REAL right = x + column + gap;
    card(g, right, top, column, 225);
    text(g, loc(L"MISES À JOUR",L"UPDATES"), right + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    toggle_row(g, HitUpdateToggle, loc(L"Rechercher au démarrage",L"Check on startup"), app.update_on_start, right + 12, top + 48, column - 24);
    paragraph(g, loc(L"Les mises à jour proviennent du GitHub Hoverbike51. Le téléchargement et l’installation nécessitent votre accord.",
                     L"Updates come from Hoverbike51 on GitHub. Download and installation require your approval."), right + 17, top + 112, column - 34, 55, 10, Color(255, 135, 161, 175));
    button(g, HitCheckUpdate, app.update_running ? loc(L"Vérification en cours…",L"Checking for updates…") : loc(L"Vérifier maintenant",L"Check now"),
           right + 14, top + 174, column - 28, 37, false, !app.update_transfer);

    card(g, x, top + 242, column, 245);
    text(g, loc(L"CACHE DES SHADERS",L"SHADER CACHE"), x + 16, top + 254, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    text(g, cache_directory().wstring(), x + 16, top + 286, column - 32, 36, 10, Color(255, 163, 185, 197));
    button(g, HitOpenCache, loc(L"Ouvrir le cache",L"Open cache"), x + 14, top + 337, column * .47f, 41);
    button(g, HitClearCache, loc(L"Vider le cache",L"Clear cache"), x + column * .51f, top + 337, column * .45f, 41);
    paragraph(g, loc(L"Le nettoyage ne cible que le cache GPU de CUSA03219. Les sauvegardes et les réglages ne sont jamais supprimés.",
                     L"Cleanup only targets the CUSA03219 GPU cache. Saves and settings are never deleted."), x + 17, top + 394, column - 34, 65, 10, Color(255, 135, 161, 175));

    card(g, right, top + 242, column, 245);
    text(g, loc(L"DÉTECTION AUTOMATIQUE",L"AUTOMATIC DETECTION"), right + 16, top + 254, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    chip(g, loc(L"Préparateur C++ intégré",L"Integrated C++ preparer"), app.native_preparer_ok, right + 15, top + 292, column - 30);
    chip(g, loc(L"Pilote Vulkan 1.3",L"Vulkan 1.3 driver"), app.vulkan_ok, right + 15, top + 341, column - 30);
    chip(g, L"Runtime Windows x64", app.runtime_present, right + 15, top + 390, column - 30);
    button(g, HitRefresh, loc(L"Relancer les vérifications",L"Run checks again"), right + 14, top + 439, column - 28, 37);

    const auto* gpu=selected_adapter();
    const bool can_force=gpu && gpu->fsr3_technical && !gpu->fsr3_recommended && !app.game_running && !app.busy;
    card(g,x,top+504,width,144);
    toggle_row(g,HitForceFsr3,loc(L"Forcer la disponibilité de FSR 3.1",L"Force FSR 3.1 availability"),fsr3_forced(),x+12,top+518,width-24,can_force);
    paragraph(g,!gpu || !gpu->fsr3_technical?loc(L"Les fonctions Vulkan requises sont indisponibles. Le forçage est désactivé.",L"Required Vulkan capabilities are unavailable. The override is disabled."):gpu->fsr3_recommended?loc(L"FSR 3.1 est disponible automatiquement sur ce GPU.",L"FSR 3.1 is automatically available on this GPU."):
        loc(L"Déverrouillage pour une carte sous le seuil recommandé ou non reconnue. Appliqué au prochain lancement du jeu.",L"Unlock for a below-threshold or unrecognized GPU. Applies on the next game launch."),x+18,top+582,width-36,56,12,Color(255,147,174,188));
    const REAL overlay_top = top + 666;
    card(g, x, overlay_top, width, 408);
    text(g, loc(L"AFFICHAGE DES PERFORMANCES",L"PERFORMANCE OVERLAY"), x + 18, overlay_top + 12,
         width - 36, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    toggle_row(g, HitPerformanceOverlay, loc(L"Afficher en jeu",L"Show in game"),
               app.performance_overlay, x + 12, overlay_top + 46, width - 24);
    const REAL item_top = overlay_top + 108;
    const REAL item_width = (width - gap - 24) / 2.0f;
    toggle_row(g, HitOverlayFps, loc(L"Images par seconde (IPS)",L"Frames per second (FPS)"),
               app.overlay_fps, x + 12, item_top, item_width);
    toggle_row(g, HitOverlayFrameTime, loc(L"Temps par image",L"Frame time"),
               app.overlay_frametime, x + 12, item_top + 58, item_width);
    toggle_row(g, HitOverlayUpscaler, loc(L"Reconstruction active",L"Active upscaler"),
               app.overlay_upscaler, x + 12, item_top + 116, item_width);
    toggle_row(g, HitOverlayGpu, loc(L"Carte graphique et charge",L"GPU name and load"),
               app.overlay_gpu, x + 12, item_top + 174, item_width);
    const REAL item_right = x + item_width + gap + 12;
    toggle_row(g, HitOverlayCpu, loc(L"Utilisation processeur",L"CPU usage"),
               app.overlay_cpu, item_right, item_top, item_width);
    toggle_row(g, HitOverlayRam, loc(L"Mémoire vive",L"System memory"),
               app.overlay_ram, item_right, item_top + 58, item_width);
    toggle_row(g, HitOverlayVram, loc(L"Mémoire vidéo",L"Video memory"),
               app.overlay_vram, item_right, item_top + 116, item_width);
    toggle_row(g, HitOverlayVersion, loc(L"Version de l’application",L"Application version"),
               app.overlay_version, item_right, item_top + 174, item_width);
    toggle_row(g, HitOverlayGpuModel, loc(L"Afficher le modèle du GPU",L"Show GPU model"),
               app.overlay_gpu_model, x + 12, item_top + 232, item_width, app.overlay_gpu);
    toggle_row(g, HitOverlayCpuModel, loc(L"Afficher le modèle du CPU",L"Show CPU model"),
               app.overlay_cpu_model, item_right, item_top + 232, item_width, app.overlay_cpu);

    const REAL appearance_top = overlay_top + 426;
    card(g, x, appearance_top, width, 180);
    text(g, loc(L"APPARENCE DE L’AFFICHAGE",L"OVERLAY APPEARANCE"), x + 18, appearance_top + 12,
         width - 36, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitOverlayScale, loc(L"Taille du texte",L"Text size"),
                std::to_wstring(app.overlay_scale) + L" %", x + 12, appearance_top + 46, item_width);
    setting_row(g, HitOverlayOpacity, loc(L"Opacité du fond",L"Background opacity"),
                std::to_wstring(app.overlay_opacity) + L" %", item_right, appearance_top + 46, item_width);
    setting_row(g, HitOverlayCorner, loc(L"Position",L"Position"), overlay_corner_label(),
                x + 12, appearance_top + 108, item_width);
    setting_row(g, HitOverlayLayout, loc(L"Disposition",L"Layout"), overlay_layout_label(),
                item_right, appearance_top + 108, item_width);
    const REAL tools_top=appearance_top+198;
    card(g,x,tools_top,width,136);
    toggle_row(g,HitPerformanceTools,loc(L"Outils de capture des performances",L"Performance capture tools"),
               app.performance_tools,x+12,tools_top+12,width-24);
    paragraph(g,loc(L"Affiche les boutons de capture dans Journal et diagnostic. Ne démarre aucun enregistrement.",
                   L"Shows capture buttons in Logs and diagnostics. Does not start recording."),
              x+18,tools_top+72,width-36,48,12,Color(255,147,174,188));

    const REAL shortcuts_y=y+1718;
    card(g,x,shortcuts_y,width,184);
    toggle_row(g,HitFastForward,loc(L"Mode accéléré (maintenir F2)",L"Fast-forward (hold F2)"),app.fast_forward,x+16,shortcuts_y+16,width-32);
    toggle_row(g,HitFastForwardPad,loc(L"Raccourci manette R2 + L2",L"Controller shortcut R2 + L2"),app.fast_forward_pad,x+16,shortcuts_y+76,width-32,app.fast_forward);
    paragraph(g,loc(L"Le raccourci est refusé si une touche a été réaffectée. L’audio est coupé pendant l’accélération.",L"The shortcut is refused when its inputs are remapped. Audio is muted during fast-forward."),x+22,shortcuts_y+134,width-44,46,12,Color(255,174,200,214));

}

void draw_logs(Graphics& g, REAL x, REAL y, REAL width, REAL height) {
    page_title(g, loc(L"SUPPORT ET ANALYSE",L"SUPPORT AND ANALYSIS"), loc(L"Journal et diagnostic",L"Logs and diagnostics"), x, y, width);
    const REAL top = y + 70;
    const std::wstring preparer_label = loc(L"Préparateur natif",L"Native preparer");
    const REAL preparer_width = std::max<REAL>(190, label_width(g, preparer_label, 14, FontStyleRegular) + 58);
    chip(g, preparer_label, app.native_preparer_ok, x, top, preparer_width);
    chip(g, L"Vulkan 1.3", app.vulkan_ok, x + preparer_width + 12, top, 160);
    REAL badge_width = preparer_width + 172;
    if (app.game_identity != GameIdentity::None) {
        const Indicator indicator = app.game_identity == GameIdentity::Tested ? Indicator::Green :
                                    app.game_identity == GameIdentity::Untested ? Indicator::Orange : Indicator::Red;
        chip(g, app.detected_title_id, indicator, x + badge_width + 12, top, 170);
        badge_width += 182;
    }
    const std::wstring export_label = loc(L"Exporter le rapport",L"Export report");
    const std::wstring clear_label = loc(L"Effacer l’affichage",L"Clear display");
    const REAL export_width = action_width(g, export_label);
    const REAL clear_width = action_width(g, clear_label);
    const REAL actions_y = top + (badge_width + 24 + export_width + 12 + clear_width > width ? 50 : 0);
    button(g, HitExportLogs, export_label, x + width - clear_width - 12 - export_width, actions_y, export_width, 40);
    button(g, HitClearLogs, clear_label, x + width - clear_width, actions_y, clear_width, 40);
    const REAL console_top = actions_y + 56;
    const REAL console_bottom = y + height;
    SolidBrush terminal(Color(242, 2, 10, 16));
    rounded(g, RectF(x, console_top, width, console_bottom - console_top), 15, terminal);
    const std::wstring status_label = app.busy ? loc(L"●  PROCESSUS ACTIF",L"●  PROCESS ACTIVE") : loc(L"●  PRÊT",L"●  READY");
    const std::wstring capture_label = app.performance_capture ? loc(L"Capture activée", L"Capture enabled")
                                                             : loc(L"Capturer la prochaine session", L"Capture next session");
    const std::wstring open_label = loc(L"Ouvrir les captures", L"Open captures");
    const REAL capture_width = action_width(g, capture_label);
    const REAL open_width = action_width(g, open_label);
    const bool wrap_capture = app.performance_tools && label_width(g, status_label, 14, FontStyleBold) + 24 + capture_width + 12 + open_width > width - 36;
    const REAL capture_y = console_top + (wrap_capture ? 48 : 8);
    text(g, status_label, x + 18, console_top + 9,
         (!app.performance_tools || wrap_capture) ? width - 36 : width - capture_width - open_width - 72, 28, 10,
         app.busy ? Color(255, 65, 226, 170) : Color(255, 112, 145, 162), FontStyleBold);
    if(app.performance_tools) {
    button(g, HitPerformanceCapture, capture_label,
           x + width - 18 - open_width - 12 - capture_width, capture_y, capture_width, 36,
           app.performance_capture, !app.busy);
    button(g, HitOpenPerformanceCaptures, open_label,
           x + width - 18 - open_width, capture_y, open_width, 36);
    }
    const REAL body_top = capture_y + 48;
    const REAL body_height = console_bottom - 16 - body_top;
    if(app.log_console.Handle()) {
        app.log_console.Place(int(x+18),int(body_top),int(width-36),int(body_height));
        if(app.native_logs_revision!=app.logs_revision.load()) {
            std::vector<std::wstring> current;uint64_t revision;
            {std::lock_guard guard(app.mutex);current=app.logs;revision=app.logs_revision;}
            app.log_console.Update(current);app.native_logs_revision=revision;
        }
        return;
    }
    std::vector<std::wstring> lines;
    constexpr REAL line_height = 24;
    const int max_lines = std::max(1, static_cast<int>(body_height / line_height));
    int scroll_lines = 0;
    int scroll_max = 0;
    {
        std::lock_guard guard(app.mutex);
        lines = app.logs;
        app.log_scroll_max = std::max(0, static_cast<int>(lines.size()) - max_lines);
        app.log_scroll_lines = std::clamp(app.log_scroll_lines, 0, app.log_scroll_max);
        scroll_lines = app.log_scroll_lines;
        scroll_max = app.log_scroll_max;
    }
    const size_t first = lines.size() > static_cast<size_t>(max_lines + scroll_lines)
                             ? lines.size() - max_lines - scroll_lines
                             : 0;
    const size_t last = std::min(lines.size(), first + static_cast<size_t>(max_lines));
    REAL line_y = body_top;
    for (size_t i = first; i < last; ++i) {
        const bool error = lines[i].find(L"ERROR") != std::wstring::npos || lines[i].find(L"STOP:") != std::wstring::npos;
        text(g, lines[i], x + 18, line_y, width - 64, line_height, 11,
             error ? Color(255, 247, 119, 119) : Color(255, 158, 204, 193));
        line_y += line_height;
    }

    // Dedicated console scrollbar. The arrows and track page through history; the mouse wheel
    // scrolls by individual lines. Offset zero intentionally follows the newest output.
    const REAL bar_x = x + width - 34;
    const REAL bar_y = body_top;
    const REAL bar_height = body_height;
    constexpr REAL arrow_height = 24;
    SolidBrush arrow_bg(Color(150, 20, 43, 56));
    SolidBrush arrow_hot(Color(220, 37, 78, 99));
    rounded(g, RectF(bar_x, bar_y, 20, arrow_height), 6,
            app.hover == HitLogOlder ? arrow_hot : arrow_bg);
    rounded(g, RectF(bar_x, bar_y + bar_height - arrow_height, 20, arrow_height), 6,
            app.hover == HitLogNewer ? arrow_hot : arrow_bg);
    text(g, L"▲", bar_x, bar_y, 20, arrow_height, 10, Color(255, 156, 201, 222),
         FontStyleRegular, StringAlignmentCenter);
    text(g, L"▼", bar_x, bar_y + bar_height - arrow_height, 20, arrow_height, 10,
         Color(255, 156, 201, 222), FontStyleRegular, StringAlignmentCenter);
    hit(HitLogOlder, bar_x, bar_y, 20, arrow_height);
    hit(HitLogNewer, bar_x, bar_y + bar_height - arrow_height, 20, arrow_height);

    const REAL track_y = bar_y + arrow_height + 4;
    const REAL track_height = std::max<REAL>(24, bar_height - arrow_height * 2 - 8);
    SolidBrush track(Color(150, 12, 31, 42));
    rounded(g, RectF(bar_x + 5, track_y, 10, track_height), 5, track);
    const REAL visible_ratio = lines.empty() ? 1.0f :
        std::min<REAL>(1.0f, static_cast<REAL>(max_lines) / static_cast<REAL>(lines.size()));
    const REAL thumb_height = std::max<REAL>(34, track_height * visible_ratio);
    const REAL travel = std::max<REAL>(0, track_height - thumb_height);
    const REAL position = scroll_max == 0 ? travel :
        travel * static_cast<REAL>(scroll_max - scroll_lines) / static_cast<REAL>(scroll_max);
    const REAL thumb_y = track_y + position;
    SolidBrush thumb(Color(230, 65, 144, 177));
    rounded(g, RectF(bar_x + 4, thumb_y, 12, thumb_height), 6, thumb);
    if (scroll_max > 0) {
        if (thumb_y > track_y) hit(HitLogPageOlder, bar_x, track_y, 20, thumb_y - track_y);
        const REAL thumb_bottom = thumb_y + thumb_height;
        if (thumb_bottom < track_y + track_height)
            hit(HitLogPageNewer, bar_x, thumb_bottom, 20, track_y + track_height - thumb_bottom);
    }
}

void render_launcher(Graphics& g, int width, int height) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    SolidBrush base(Color(255,1,8,17)); g.FillRectangle(&base,0,0,width,height);
    image_cover(g, app.background.get(), 0, 0, static_cast<REAL>(width), static_cast<REAL>(height));
    LinearGradientBrush shade(RectF(0, 0, static_cast<REAL>(width), static_cast<REAL>(height)),
                              Color(80, 1, 8, 17), Color(178, 2, 10, 20), LinearGradientModeVertical);
    g.FillRectangle(&shade, 0, 0, width, height);
    if (app.page != Page::Home) {
        SolidBrush content_shade(Color(150, 1, 10, 20));
        g.FillRectangle(&content_shade, 0, kHeaderHeight, width, height - kHeaderHeight);
    }
    app.hits.clear();
    draw_header(g, static_cast<REAL>(width));
    const REAL x = 42, y = kHeaderHeight + 26, content_width = width - 84, content_height = height - y - 34;
    const int page_index = static_cast<int>(app.page);
    const int scroll = app.page == Page::Home ? 0 : app.scroll[page_index];
    const REAL page_y = y - static_cast<REAL>(scroll);
    const GraphicsState content_state = g.Save();
    app.content_bottom = height - 32;
    g.SetClip(RectF(0, kHeaderHeight + 1, static_cast<REAL>(width), static_cast<REAL>(app.content_bottom - kHeaderHeight - 1)),CombineModeIntersect);
    app.clipping_content_hits = true;
    switch (app.page) {
    case Page::Home: draw_home(g, static_cast<REAL>(width), static_cast<REAL>(height)); break;
    case Page::Game: draw_game(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Cheats: draw_cheats(g,x,page_y,content_width,content_height+scroll);break;
    case Page::Tweaks: draw_tweaks(g,x,page_y,content_width,content_height+scroll);break;
    case Page::Video: draw_video(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Controls: draw_controls(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Advanced: draw_advanced(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Logs: draw_logs(g, x, page_y, content_width, content_height + scroll); break;
    }
    app.log_console.Show(app.page==Page::Logs && !app.modal_visible);
    app.clipping_content_hits = false;
    g.Restore(content_state);
    const int required = app.page == Page::Controls ? 1200 : app.page == Page::Game ? 900 :
                         app.page == Page::Advanced ? 1998 : app.page == Page::Video ? 1040 :
                         app.page == Page::Tweaks ? tweaks_content_height : app.page == Page::Cheats ? cheats_content_height() : 0;
    const int viewport = height - kHeaderHeight - 28;
    if (required > viewport && app.page != Page::Home) {
        const REAL track_x = width - 15.0f, track_y = kHeaderHeight + 16.0f, track_height = height - kHeaderHeight - 54.0f;
        SolidBrush track(Color(100, 20, 46, 62)); rounded(g, RectF(track_x, track_y, 6, track_height), 3, track);
        const REAL thumb_height = std::max<REAL>(48, track_height * viewport / required);
        const REAL thumb_y = track_y + (track_height - thumb_height) * scroll / std::max(1, required - viewport);
        SolidBrush thumb(Color(230, 82, 207, 244)); rounded(g, RectF(track_x, thumb_y, 6, thumb_height), 3, thumb);
    }
    draw_selector(g, static_cast<REAL>(width), static_cast<REAL>(height));
    draw_modal(g, static_cast<REAL>(width), static_cast<REAL>(height));
    text(g, std::wstring(loc(L"Version",L"Version")) + L" " + kVersion, width - 170, height - 25, 150, 18, 9,
         Color(255, 101, 127, 142), FontStyleRegular, StringAlignmentFar);
}

void paint(HWND window) {
    if (IsWindowEnabled(app.minimize_button) == app.modal_visible) EnableWindow(app.minimize_button, !app.modal_visible);
    if (IsWindowEnabled(app.close_button) == app.modal_visible) EnableWindow(app.close_button, !app.modal_visible);
    // Preserve the actual disjoint invalid rectangles. rcPaint is only their
    // bounding box and can span an entire page between two distant controls.
    HRGN dirty=CreateRectRgn(0,0,0,0);
    const int region_type=GetUpdateRgn(window,dirty,FALSE);
    PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
    RECT client{}; GetClientRect(window, &client);
    if (client.right > 0 && client.bottom > 0) {
        auto& canvas = drawing_cache.canvas;
        const bool resized=!canvas || canvas->GetWidth()!=UINT(client.right) || canvas->GetHeight()!=UINT(client.bottom);
        if (resized)
            canvas=std::make_unique<Bitmap>(client.right,client.bottom,PixelFormat32bppPARGB);
        { Graphics g(canvas.get());
          // A new backing bitmap has no retained pixels. Fully initialize it
          // before presenting, even when Windows invalidated only a thin edge.
          if(resized)g.SetClip(Rect(0,0,client.right,client.bottom));
          else if(region_type!=ERROR) { Region region(dirty); g.SetClip(&region); }
          else g.SetClip(Rect(ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top));
          render_launcher(g,client.right,client.bottom); }
        Graphics target(dc); target.DrawImage(canvas.get(),Rect(0,0,client.right,client.bottom),
            0,0,client.right,client.bottom,UnitPixel);
    }
    EndPaint(window,&ps);
    DeleteObject(dirty);
}

int render_benchmark(const fs::path& output) {
    GdiplusStartupInput input;
    if(GdiplusStartup(&app.gdiplus_token,&input,nullptr)!=Ok) return 2;
    app.background.reset(Image::FromFile((app.root/L"launcher/assets/launcher-background.png").c_str()));
    app.launcher_icon.reset(Image::FromFile((app.root/L"launcher/assets/launcher-icon.png").c_str()));
    app.game_logo.reset(Image::FromFile((app.root/L"launcher/assets/star-ocean-logo.png").c_str()));
    std::ofstream report(output); report<<"{\n  \"samples\": [\n";
    bool first=true, all_matches=true;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    for(const auto& d:So5Cheats::Definitions)app.cheat_entries.push_back({d.id,true,true,d.name,{},{},{},{}});
    // Synthetic, read-only UI fixture. No game preparation or owner settings.
    app.game_verified=true;app.detected_title_id=L"CUSA03219";app.detected_game_version=L"01.00";
    app.hud_library=std::make_unique<So5Hud::PresetLibrary>(output.parent_path()/L"unused-benchmark-fixture",So5Hud::Aspect::Standard,true);
    app.hud_snapshot.entries={{"Example 16:9",true},{"Example 32:9",false}};
    struct BenchmarkLanguage { const wchar_t* name; const char* code; };
    for(const auto language: {BenchmarkLanguage{L"English","en"}, {L"Français","fr"}, {L"Español","es"},
            {L"Português","pt"}, {L"Português (Brasil)","pt-BR"}, {L"Italiano","it"}, {L"Deutsch","de"},
            {L"Русский","ru"}, {L"简体中文","zh-CN"}, {L"繁體中文","zh-TW"}, {L"한국어","ko"}, {L"日本語","ja"}}) {
    app.launcher_language=language.name;
    for(auto size: {std::pair{1360,760},std::pair{1720,1040}}) {
        for(auto page: {Page::Home,Page::Game,Page::Cheats,Page::Tweaks,Page::Video,Page::Controls,Page::Advanced,Page::Logs}) {
            app.page=page;
            if(page==Page::Logs && app.logs.empty()) for(int i=0;i<1200;++i)
                app.logs.push_back(L"Benchmark log · Runtime: shader and texture diagnostic line with a deliberately long payload; no game or personal data. Line "+std::to_wstring(i));
            std::vector<double> times;
            for(int frame=0;frame<12;++frame) {
                app.hover=frame%2?HitFsr:HitResolution;
                LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
                {
                    Bitmap canvas(size.first,size.second,PixelFormat32bppPARGB);
                    Graphics g(&canvas); render_launcher(g,size.first,size.second);
                    g.Flush(FlushIntentionSync);
                }
                QueryPerformanceCounter(&end);
                if(frame>=2) times.push_back((end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart);
            }
            std::sort(times.begin(),times.end());
            // A hover-only repaint must produce exactly the same pixels as a full
            // repaint, including transparent artwork, rounded edges and text.
            const int before=page==Page::Home?HitPlay:page==Page::Game?HitFolder:page==Page::Tweaks?HitTweaks:page==Page::Cheats?HitCheatsToggle:page==Page::Video?HitOutputAspect:page==Page::Controls?HitResetControls:page==Page::Advanced?HitUpdateToggle:HitExportLogs;
            const int after=page==Page::Home?HitVideo:page==Page::Game?HitPkg:page==Page::Tweaks?HitHome:page==Page::Cheats?HitHome:page==Page::Video?HitResolution:page==Page::Controls?HitController:page==Page::Advanced?HitOverlayGpu:HitClearLogs;
            Bitmap retained(size.first,size.second,PixelFormat32bppPARGB), expected(size.first,size.second,PixelFormat32bppPARGB);
            app.hover=before;
            { Graphics g(&retained); render_launcher(g,size.first,size.second); }
            RECT dirty{};
            Region dirty_region; dirty_region.MakeEmpty();
            for(const auto& [id,area]:app.hits) if(id==before || id==after) {
                RECT padded=area; InflateRect(&padded,3,3); UnionRect(&dirty,&dirty,&padded);
                dirty_region.Union(Rect(padded.left,padded.top,padded.right-padded.left,padded.bottom-padded.top));
            }
            std::vector<double> hover_times;
            for(int frame=0;frame<42;++frame) {
                app.hover=frame%2?before:after;
                LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
                { Graphics g(&retained); g.SetClip(&dirty_region);
                  render_launcher(g,size.first,size.second); g.Flush(FlushIntentionSync); }
                QueryPerformanceCounter(&end);
                if(frame>=2) hover_times.push_back((end.QuadPart-begin.QuadPart)*1000.0/frequency.QuadPart);
            }
            std::sort(hover_times.begin(),hover_times.end());
            app.hover=after;
            { Graphics g(&retained); g.SetClip(&dirty_region);
              render_launcher(g,size.first,size.second); }
            { Graphics g(&expected); render_launcher(g,size.first,size.second); }
            Rect area(0,0,size.first,size.second); BitmapData a{},b{};
            bool matches=false;
            if(retained.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&a)==Ok) {
                if(expected.LockBits(&area,ImageLockModeRead,PixelFormat32bppPARGB,&b)==Ok) {
                    matches=true;
                    for(int row=0;row<size.second;++row)
                        if(std::memcmp(static_cast<const char*>(a.Scan0)+row*a.Stride,
                                       static_cast<const char*>(b.Scan0)+row*b.Stride,size.first*4)) { matches=false; break; }
                    expected.UnlockBits(&b);
                }
                retained.UnlockBits(&a);
            }
            all_matches &= matches;
            if(page==Page::Cheats && std::string(language.code)=="fr" && size.first==1360) {
                const CLSID bmp={0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                expected.Save((output.parent_path()/L"cheats-launcher-fr-top.bmp").c_str(),&bmp,nullptr);
                app.scroll[int(Page::Cheats)]=std::max(0,cheats_content_height()-(size.second-kHeaderHeight-28));
                {Graphics g(&expected);render_launcher(g,size.first,size.second);}
                expected.Save((output.parent_path()/L"cheats-launcher-fr-bottom.bmp").c_str(),&bmp,nullptr);
                app.scroll[int(Page::Cheats)]=0;
            }
            if(page==Page::Tweaks && size.first==1360) {
                const CLSID bmp={0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                expected.Save((output.parent_path()/(L"tweaks-launcher-"+std::wstring(language.code,language.code+std::strlen(language.code))+L".bmp")).c_str(),&bmp,nullptr);
            }
            if(page==Page::Video && size.first==1360) {
                const CLSID bmp={0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
                expected.Save((output.parent_path()/(L"video-launcher-"+std::wstring(language.code,language.code+std::strlen(language.code))+L".bmp")).c_str(),&bmp,nullptr);
            }
            if(!first) report<<",\n"; first=false;
            report<<"    {\"language\":\""<<language.code<<"\",\"width\":"<<size.first<<",\"height\":"<<size.second
                  <<",\"page\":"<<int(page)<<",\"median_ms\":"<<times[times.size()/2]
                  <<",\"max_ms\":"<<times.back()<<",\"hover_median_ms\":"<<hover_times[hover_times.size()/2]
                  <<",\"hover_p95_ms\":"<<hover_times[hover_times.size()*95/100]
                  <<",\"hover_max_ms\":"<<hover_times.back()<<",\"partial_redraw_matches_full\":"<<(matches?"true":"false")<<"}";
        }
    }
    }
    report<<"\n  ]\n}\n"; report.flush();
    drawing_cache.Clear();
    app.game_logo.reset(); app.launcher_icon.reset(); app.background.reset();
    GdiplusShutdown(app.gdiplus_token);
    return !report.good()?3:all_matches?0:4;
}

int hit_at(POINT point) {
    for (auto it = app.hits.rbegin(); it != app.hits.rend(); ++it)
        if (PtInRect(&it->second, point)) return it->first;
    return HitNone;
}

void hover_at(HWND window,int next) {
    if(next==app.hover) return;
    const int previous=app.hover; app.hover=next;
    // Hover changes only these controls; keep the rest of the retained canvas.
    for(const auto& [id,area]:app.hits) if(id==previous || id==next) {
        RECT dirty=area; InflateRect(&dirty,3,3); InvalidateRect(window,&dirty,FALSE);
    }
}

void export_logs() {
    const fs::path directory = app.root / "out" / "logs";
    std::error_code ec;
    fs::create_directories(directory, ec);
    const fs::path report = directory / (L"diagnostic-" + timestamp_name() + L".txt");
    FILE* file = _wfopen(report.c_str(), L"w, ccs=UTF-8");
    if (!file) { app.status = loc(L"Impossible d’exporter le rapport",L"Could not export report"); return; }
    fwprintf(file, L"PS4RT Windows %ls\nGame: %ls\nPreparer: %ls\nVulkan 1.3: %ls\n\n",
             kVersion, app.game_path.c_str(), app.native_preparer_ok ? L"Native C++" : L"Missing",
             app.vulkan_ok ? L"OK" : L"Missing");
    { std::lock_guard guard(app.mutex);
      if(!app.retained_diagnostics.lines.empty()) {
          fwprintf(file,L"Earlier reconstruction diagnostics (retained before console rollover):\n");
          for(const auto& line:app.retained_diagnostics.lines) fwprintf(file,L"%ls\n",line.c_str());
          fwprintf(file,L"\nRecent console output:\n");
      }
      for (const auto& line : app.logs) fwprintf(file, L"%ls\n", line.c_str()); }
    fclose(file);
    app.status = std::wstring(loc(L"Rapport exporté : ",L"Report exported: ")) + report.filename().wstring();
    open_path(directory);
}

void clear_cache_confirmed() {
    const fs::path cache = cache_directory();
    std::error_code ec;
    fs::remove_all(cache, ec);
    fs::create_directories(cache, ec);
    app.status = ec ? loc(L"Impossible de vider le cache",L"Could not clear cache") : loc(L"Cache des shaders vidé",L"Shader cache cleared");
    append_log(ec ? L"ERROR · could not clear shader cache" : L"Shader cache cleared");
}

void clear_cache() {
    if (app.busy) { app.status = loc(L"Arrêtez le jeu avant de vider le cache",L"Stop the game before clearing the cache"); return; }
    show_modal(loc(L"Vider le cache des shaders ?",L"Clear shader cache?"),
               loc(L"Seul le cache GPU de CUSA03219 sera supprimé. Les sauvegardes et les réglages seront conservés.",
                   L"Only the CUSA03219 GPU cache will be removed. Saves and settings will be preserved."),
               loc(L"Vider le cache",L"Clear cache"), loc(L"Annuler",L"Cancel"), ModalAction::ClearCache);
}

const wchar_t* update_error(So5Update::Code code) {
    using C = So5Update::Code;
    switch (code) {
    case C::Network: return loc(L"Impossible de contacter GitHub. Vérifiez votre connexion et réessayez.",L"Could not contact GitHub. Check your connection and try again.");
    case C::RateLimit: return loc(L"GitHub limite temporairement les demandes. Réessayez plus tard.",L"GitHub is temporarily limiting update requests. Try again later.");
    case C::InvalidResponse: return loc(L"GitHub n’a pas renvoyé une liste de versions valide.",L"GitHub did not return a valid release list.");
    case C::Http: return loc(L"GitHub a renvoyé une erreur. Consultez Journal et diagnostic.",L"GitHub returned an error. See Logs and diagnostics.");
    case C::Integrity: return loc(L"Le paquet téléchargé n’a pas pu être vérifié. Rien n’a été installé.",L"The downloaded package could not be verified. Nothing was installed.");
    case C::InvalidPackage: return loc(L"Le paquet est incomplet ou incompatible. Rien n’a été installé.",L"The package is incomplete or incompatible. Nothing was installed.");
    case C::Disk: return loc(L"Impossible d’écrire la mise à jour. Vérifiez l’espace libre et les droits du dossier.",L"The update could not be written. Check free space and folder permissions.");
    case C::InUse: return loc(L"Fermez toutes les instances de l’application avant d’installer une mise à jour.",L"Close all running copies of the application before installing an update.");
    case C::Development: return loc(L"Les mises à jour automatiques sont réservées au dossier joueur autonome. Ce dépôt source ne sera pas modifié.",L"Automatic updates are available only in a standalone player folder. This source checkout will not be modified.");
    case C::Canceled: return loc(L"Mise à jour annulée. Rien n’a été installé.",L"Update canceled. Nothing was installed.");
    default: return loc(L"L’installation n’a pas pu démarrer. Rien n’a été installé. Consultez Journal et diagnostic.",L"Installation could not be started. Nothing was installed. See Logs and diagnostics.");
    }
}

void publish_update_event(std::unique_ptr<UpdateEvent> event) {
    { std::lock_guard guard(app.update_mutex); app.update_event = std::move(event); }
    PostMessageW(app.window, WM_UPDATE_DONE, 0, 0);
}

void check_for_updates(bool interactive) {
    app.update_interactive = app.update_interactive || interactive;
    if (app.update_running || app.update_transfer) return;
    app.update_running = true; app.update_cancel = false;
    SetTimer(app.window, 3, 100, nullptr);
    app.update_thread = std::thread([] {
        auto event = std::make_unique<UpdateEvent>();
        event->checked = So5Update::Check(*So5Update::Version::Parse(PS4RT_VERSION_STRING), true, app.update_cancel);
        publish_update_event(std::move(event));
    });
    InvalidateRect(app.window, nullptr, FALSE);
}

void show_update_notice() {
    if (!app.update_notice || app.modal_visible || app.close_when_idle || (app.busy && !app.update_interactive) || IsIconic(app.window)) return;
    const auto result = std::move(*app.update_notice); app.update_notice.reset();
    const bool interactive = app.update_interactive; app.update_interactive = false;
    const auto title = loc(L"Mises à jour Windows",L"Windows updates");
    using C = So5Update::Code;
    std::wstring body = std::wstring(loc(L"Version installée : ",L"Installed version: ")) + kVersion;
    if (result.release) {
        app.update_release = result.release;
        body += L"\n" + std::wstring(loc(L"Nouvelle version : ",L"New version: ")) +
                from_utf8(result.release->version.String().c_str(), int(result.release->version.String().size()));
        if (result.release->experimental) body += L"\n" + std::wstring(loc(L"Cette version est expérimentale.",L"This release is experimental."));
        if (result.code == C::Available && So5Update::PlayerInstall(app.root)) {
            body += L"\n\n" + std::wstring(loc(L"Vous allez télécharger et installer cette mise à jour. Le lanceur redémarrera. Vos sauvegardes et paramètres seront conservés.",
                L"You will download and install this update. The launcher will restart. Saves and settings will be kept."));
            show_modal(title, body, loc(L"Télécharger",L"Download"), loc(L"Non",L"No"), ModalAction::UpdateOffer,
                       loc(L"Ouvrir GitHub",L"Open GitHub"));
        } else {
            body += L"\n\n" + std::wstring(result.code == C::NoDigest
                ? loc(L"Le paquet n’a pas d’empreinte SHA-256. L’installation automatique est indisponible.",L"The package has no SHA-256 digest. Automatic installation is unavailable.")
                : result.code == C::NoPackage
                ? loc(L"Aucun paquet Windows x64 n’est joint à cette version.",L"No Windows x64 package is attached to this release.")
                : update_error(C::Development));
            show_modal(title, body, loc(L"Ouvrir GitHub",L"Open GitHub"), loc(L"Non",L"No"), ModalAction::UpdateLink);
        }
    } else if (interactive) {
        body += L"\n\n" + std::wstring(result.code == C::Current ? loc(L"Aucune version Windows plus récente n’est disponible.",L"No newer Windows release is available.") :
            result.code == C::NoPackage ? loc(L"Aucun paquet Windows destiné aux joueurs n’est publié actuellement.",L"No Windows player package is currently published.") : update_error(result.code));
        show_modal(title, body, L"OK");
    }
}

void begin_update_download() {
    if (!app.update_release || app.update_running) return;
    if (app.busy) {
        show_modal(loc(L"Mises à jour Windows",L"Windows updates"),
            loc(L"Fermez toutes les instances de l’application avant d’installer une mise à jour.",L"Close all running copies of the application before installing an update."), L"OK");
        return;
    }
    if (!So5Update::PlayerInstall(app.root)) {
        show_modal(loc(L"Mises à jour Windows",L"Windows updates"), update_error(So5Update::Code::Development), L"OK"); return;
    }
    PWSTR known = nullptr; fs::path parent;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &known))) {
        parent = fs::path(known) / "StarOcean5/Updates"; CoTaskMemFree(known);
    } else parent = fs::temp_directory_path() / "StarOcean5-Updates";
    const auto release = *app.update_release;
    app.update_running = app.update_transfer = true; app.update_cancel = false;
    app.update_phase = So5Update::Phase::Download; app.update_done = 0; app.update_total = release.size;
    save_settings();
    show_modal(loc(L"Mise à jour de STAR OCEAN 5",L"Updating STAR OCEAN 5"),
        loc(L"Téléchargement et vérification du paquet Windows. L’installation actuelle restera intacte jusqu’à la fin des vérifications.",
            L"Downloading and checking the Windows package. Your installation will remain unchanged until verification succeeds."),
        loc(L"Annuler",L"Cancel"), {}, ModalAction::UpdateProgress);
    SetTimer(app.window, 3, 100, nullptr);
    app.update_thread = std::thread([release, parent] {
        auto event = std::make_unique<UpdateEvent>(); event->kind = UpdateEvent::Prepared;
        try {
            event->prepared = So5Update::Prepare(release, parent, app.update_cancel,
                [](So5Update::Phase phase, std::uint64_t done, std::uint64_t total) {
                    app.update_done = done; app.update_total = total; app.update_phase = phase;
                });
        } catch (const So5Update::Error& error) { event->code = error.code; event->detail = error.what(); }
          catch (const std::exception& error) { event->code = So5Update::Code::Disk; event->detail = error.what(); }
        publish_update_event(std::move(event));
    });
}

void receive_update_event() {
    if (app.update_thread.joinable()) app.update_thread.join();
    app.update_running = false;
    std::unique_ptr<UpdateEvent> event;
    { std::lock_guard guard(app.update_mutex); event = std::move(app.update_event); }
    if (!event) return;
    if (app.close_after_update) {
        if (event->prepared) try { So5Update::RemoveWorkspace(event->prepared->workspace); } catch (...) {}
        DestroyWindow(app.window); return;
    }
    if (event->kind == UpdateEvent::Checked) {
        std::wstring message = L"Updates · installed Windows " + std::wstring(kVersion) + L" · ";
        if (event->checked.release) {
            const auto version = event->checked.release->version.String();
            message += L"available Windows " + from_utf8(version.c_str(), int(version.size())) + L" · ";
        }
        message += from_utf8(event->checked.detail.c_str(), int(event->checked.detail.size()));
        const auto code = event->checked.code;
        const bool successful_check = code == So5Update::Code::Current || code == So5Update::Code::Available ||
                                      code == So5Update::Code::NoPackage || code == So5Update::Code::NoDigest;
        // Startup and manual checks may return the same state. Coalesce only these
        // notices; preserve every failure and all repeated runtime diagnostics.
        if (!successful_check || message != app.last_update_log) append_log(message);
        app.last_update_log = successful_check ? std::move(message) : L"";
        app.update_notice = std::move(event->checked); show_update_notice();
    } else if (event->kind == UpdateEvent::Prepared && event->prepared) {
        app.update_prepared = std::move(event->prepared);
        app.update_phase = So5Update::Phase::Install; app.update_done = 0; app.update_total = 0;
        app.modal_body = loc(L"Préparation de la mise à jour. Le lanceur va se fermer et redémarrer automatiquement.",
                            L"Preparing the update. The launcher will close and restart automatically.");
        app.update_running = true;
        const auto prepared = *app.update_prepared;
        const auto target = app.root;
        const auto launcher = executable_directory() / L"Star Ocean 5 Launcher.exe";
        app.update_thread = std::thread([prepared, target, launcher] {
            auto result = std::make_unique<UpdateEvent>(); result->kind = UpdateEvent::Installer;
            try { So5Update::StartInstaller(prepared, target, launcher, GetCurrentProcessId()); }
            catch (const So5Update::Error& error) { result->code = error.code; result->detail = error.what(); }
            catch (const std::exception& error) { result->code = So5Update::Code::Install; result->detail = error.what(); }
            publish_update_event(std::move(result));
        });
    } else if (event->kind == UpdateEvent::Installer && event->detail.empty()) {
        // The user already accepted Download and install; the native helper is now
        // waiting on this exact process handle before it touches application files.
        DestroyWindow(app.window); return;
    } else {
        app.update_transfer = false;
        if (app.modal_action == ModalAction::UpdateProgress) { app.modal_visible = false; app.modal_action = ModalAction::None; }
        append_log(L"Update · " + from_utf8(event->detail.c_str(), int(event->detail.size())));
        if (app.update_prepared) {
            try { So5Update::RemoveWorkspace(app.update_prepared->workspace); } catch (...) {}
            app.update_prepared.reset();
        }
        show_modal(loc(L"Mises à jour Windows",L"Windows updates"), update_error(event->code), L"OK");
    }
    InvalidateRect(app.window, nullptr, FALSE);
}

void apply_selector(int index) {
    if (index < 0 || index >= static_cast<int>(app.popup_values.size())) return;
    if(!app.popup_enabled.empty() && (size_t(index)>=app.popup_enabled.size() || !app.popup_enabled[index])) return;
    const std::wstring value = app.popup_values[index];
    switch (app.popup_hit) {
    case HitCameraFollow: save_tweak("camera_follow",index);break;
    case HitHudPreset:
        if(app.hud_library&&!app.game_running&&!app.busy&&tweaks_supported()&&size_t(index)<app.hud_snapshot.entries.size()&&app.hud_snapshot.entries[index].compatible) {
            app.hud_selection=value;app.hud_library->Apply(size_t(index));
            app.hud_snapshot=app.hud_library->Snapshot();SetTimer(app.window,5,100,nullptr);
        }
        break;
    case HitGpu:
        app.gpu_index = -1;
        for (const auto& adapter : app.graphics_adapters)
            if (adapter.label == value) { app.gpu_index = adapter.runtime_index; break; }
        if(app.fsr==L"FSR 4" && !selected_gpu_supports_fsr4()) reject_incompatible_fsr4();
        if(app.fsr==L"FSR 3.1" && !selected_gpu_supports_fsr3()) app.fsr=L"off";
        write_runtime_settings(); break;
    case HitDisplayMode:
        if (value == loc(L"Fenêtré",L"Windowed")) app.display_mode = L"windowed";
        else if (value == loc(L"Plein écran",L"Fullscreen")) app.display_mode = L"fullscreen";
        else app.display_mode = L"borderless";
        refresh_target_display();
        write_runtime_settings(); break;
    case HitMonitor:
        app.selected_display_id=index==0?L"":app.displays.at(size_t(index-1)).id;
        refresh_target_display(); write_runtime_settings(); break;
    case HitOutputAspect:
        for(int aspect=0;aspect<Ps4RtOutput::AspectCount;++aspect)
            if(value==output_aspect_label(aspect)) app.output_aspect=aspect;
        refresh_target_display(); write_runtime_settings(); break;
    case HitFpsLimit: app.fps_limit = (value == L"Illimitée" || value == L"Unlimited") ? 0 : _wtoi(value.c_str()); write_runtime_settings(); break;
    case HitFsr:
        if(index==0) app.fsr=L"off";
        else if(value==L"FSR 4" && !selected_gpu_supports_fsr4()) reject_incompatible_fsr4();
        else if(value==L"DLSS") app.fsr=selected_gpu_supports_dlss()?L"DLSS":L"off";
        else app.fsr=value==L"FSR 3.1" && !selected_gpu_supports_fsr3()?L"off":value;
        write_runtime_settings(); break;
    case HitNativeRender:
        if(app.fsr==L"off" && index<int(Ps4RtScene::NativeHeights.size())) {
            app.native_render_height=Ps4RtScene::NativeHeights[size_t(index)];write_runtime_settings();
        }
        break;
    case HitResolution:
        app.resolution = value;
        if (!Ps4RtOutput::TemporalAllowed(Ps4RtOutput::Parse(value))) app.fsr=L"off";
        write_runtime_settings(); break;
    case HitAspectRatio: app.window_stretch = index == 1; write_runtime_settings(); break;
    case HitAnisotropy: app.anisotropy = (value == L"Réglage du jeu" || value == L"Game setting") ? 0 : _wtoi(value.c_str() + 1); write_runtime_settings(); break;
    case HitOverlayScale: app.overlay_scale = std::clamp(_wtoi(value.c_str()), 50, 300); write_runtime_settings(); break;
    case HitOverlayOpacity: app.overlay_opacity = std::clamp(_wtoi(value.c_str()), 0, 100); write_runtime_settings(); break;
    case HitOverlayCorner:
        app.overlay_corner = std::clamp(index, 0, 5);
        write_runtime_settings(); break;
    case HitOverlayLayout:
        app.overlay_layout = value == loc(L"Une donnée par ligne",L"One item per line") ? 1 : 0;
        write_runtime_settings(); break;
    case HitGameLanguage:
        if (std::find(app.game_languages.begin(), app.game_languages.end(), value) != app.game_languages.end()) app.game_language = value;
        break;
    case HitController: app.controller = value; break;
    case HitKeyboardProfile:
        app.keyboard_profile = (value == L"Personnalisé" || value == L"Custom") ? L"Custom" : value;
        if (value == L"AZERTY" || value == L"QWERTY") apply_keyboard_defaults(value == L"AZERTY");
        break;
    case HitLauncherLanguage: app.launcher_language = value; update_game_status(); refresh_saves(); refresh_controllers(); break;
    default: break;
    }
    app.popup_hit = HitNone; app.popup_values.clear();app.popup_enabled.clear(); save_settings(); sync_chrome();
}

void activate(int id) {
    if (app.modal_visible) {
        if (id == HitModalBackdrop) return;
        if (id == HitModalTertiary && app.modal_action == ModalAction::UpdateOffer) {
            if (app.update_release) {
                const auto& page = app.update_release->page;
                ShellExecuteW(app.window, L"open", from_utf8(page.c_str(), int(page.size())).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            app.modal_visible = false; app.modal_action = ModalAction::None; app.update_release.reset(); return;
        }
        if (id == HitModalPrimary && app.modal_action == ModalAction::UpdateProgress) {
            if (app.update_phase != So5Update::Phase::Install && !app.update_cancel) app.update_cancel = true;
            return;
        }
        if (id == HitModalTertiary && app.modal_action == ModalAction::CloseGame) {
            app.modal_visible = false; app.modal_action = ModalAction::None;
            ShowWindow(app.window, SW_MINIMIZE);
            return;
        }
        if (id == HitModalTertiary && app.modal_action == ModalAction::ConfirmExtraction) {
            const fs::path pkg = app.pending_pkg;
            app.modal_visible = false; app.modal_action = ModalAction::None;
            const std::wstring selected = pick_path(true, loc(L"Choisir le dossier d’extraction",L"Choose the extraction folder"));
            if (!selected.empty()) { app.pkg_extract_root = selected; save_settings(); }
            confirm_pkg_extraction(pkg);
            return;
        }
        if (id == HitModalSecondary) {
            if (app.modal_action == ModalAction::ConfirmExtraction) app.pending_pkg.clear();
            app.modal_visible = false; app.modal_action = ModalAction::None; return;
        }
        if (id != HitModalPrimary) return;
        const ModalAction action = app.modal_action;
        app.modal_visible = false; app.modal_action = ModalAction::None;
        if (action == ModalAction::ClearCache) clear_cache_confirmed();
        else if(action==ModalAction::ForceFsr3) {
            const auto* gpu=selected_adapter();
            if(gpu && gpu->fsr3_technical && !gpu->fsr3_recommended && !app.game_running) {
                app.force_fsr3_gpu=std::wstring(gpu->device_key.begin(),gpu->device_key.end());
                app.fsr=L"FSR 3.1";save_settings();write_runtime_settings();
            }
        }
        else if (action == ModalAction::ConfirmExtraction) {
            const fs::path pkg = app.pending_pkg;
            app.pending_pkg.clear();
            extract_pkg(pkg);
        }
        else if (action == ModalAction::CancelExtraction) {
            app.extraction_cancel_requested = true;
            app.status = loc(L"Annulation de l’extraction…",L"Canceling extraction…");
            if (app.job) TerminateJobObject(app.job, 0);
        }
        else if (action == ModalAction::CloseIdle) {
            if (!app.busy) {
                if (app.update_running) { app.update_cancel = true; app.close_after_update = true; }
                else DestroyWindow(app.window);
            }
        }
        else if (action == ModalAction::UpdateOffer) begin_update_download();
        else if (action == ModalAction::UpdateLink) {
            if (app.update_release) {
                const auto& page = app.update_release->page;
                ShellExecuteW(app.window, L"open", from_utf8(page.c_str(), int(page.size())).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                app.update_release.reset();
            }
        }
        else if (action == ModalAction::CloseGame || action == ModalAction::ForceCloseGame) {
            if (!app.game_running) { DestroyWindow(app.window); return; }
            app.close_when_idle = true;
            app.close_deadline = GetTickCount64() + 10000;
            app.status = loc(L"Arrêt du jeu…",L"Stopping game…");
            if (action == ModalAction::ForceCloseGame) {
                std::lock_guard guard(app.mutex);
                if (app.job) TerminateJobObject(app.job, 0);
            } else {
                EnumWindows([](HWND target, LPARAM pid) -> BOOL {
                    DWORD owner{}; GetWindowThreadProcessId(target, &owner);
                    if (owner == static_cast<DWORD>(pid) && IsWindowVisible(target) && !GetWindow(target, GW_OWNER))
                        PostMessageW(target, WM_CLOSE, 0, 0);
                    return TRUE;
                }, app.game_pid);
            }
        }
        return;
    }
    if (id >= kBindingHitBase && id < kBindingHitBase + static_cast<int>(app.key_bindings.size())) {
        app.capture_binding = id - kBindingHitBase;
        app.popup_hit = HitNone;
        app.status = loc(L"Appuyez sur la nouvelle touche · Échap pour annuler",L"Press the new key · Escape to cancel");
        SetFocus(app.window);
        return;
    }
    if(id==1997 || id==1998) {
        RECT area{}; GetClientRect(app.window,&area);
        const int visible=std::min<int>(app.popup_values.size(),std::max(1,int((area.bottom-kHeaderHeight-90)/48)));
        app.popup_scroll=std::clamp(app.popup_scroll+(id==1997?-visible:visible),0,int(app.popup_values.size())-visible);
        app.hover=HitNone; return;
    }
    if (id >= 2000) { apply_selector(id - 2000); return; }
    if (id == 1999) { app.popup_hit = HitNone; app.popup_values.clear();app.popup_enabled.clear(); return; }
    if (id >= 1000) {
        int selected = id - 1000;
        if (selected < static_cast<int>(app.saves.size())) {
            app.selected_save = selected;
            load_save_details(app.saves[selected]);
        }
        return;
    }
    switch (id) {
    case HitClose: SendMessageW(app.window, WM_CLOSE, 0, 0); break;
    case HitMinimize: ShowWindow(app.window, SW_MINIMIZE); break;
    case HitHome: app.page = Page::Home; break;
    case HitTweaks: app.page=Page::Tweaks;refresh_tweaks();break;
    case HitLeaderEnabled: save_tweak("exploration_leader_enabled",!app.tweaks.leader);break;
    case HitCameraFollow:
        if(!app.game_running&&!app.busy&&tweaks_supported())open_selector(id,{ps4rt_localize(app.launcher_language,L"Original"),ps4rt_localize(app.launcher_language,L"Light damping"),ps4rt_localize(app.launcher_language,L"Damped follow")});
        break;
    case HitHudRefresh: refresh_tweaks();break;
    case HitHudOpenPresets:
        if(tweaks_supported()) {
            const auto directory=tweaks_config().parent_path()/L"hud-presets";
            std::error_code error;fs::create_directories(directory,error);
            if(error||reinterpret_cast<INT_PTR>(ShellExecuteW(app.window,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32) {
                append_log(L"Tweaks: could not open the HUD presets folder.");
                show_modal(ps4rt_localize(app.launcher_language,L"HUD layout"),
                    ps4rt_localize(app.launcher_language,L"Could not open the HUD presets folder."),L"OK");
            }
        }
        break;
    case HitHudPreset:
        if(app.hud_library&&!app.game_running&&!app.busy&&tweaks_supported()&&!app.hud_snapshot.busy) {
            std::vector<std::wstring> names;
            for(const auto& entry:app.hud_snapshot.entries)names.push_back(from_utf8(entry.name.data(),int(entry.name.size())));
            open_selector(id,names);app.popup_enabled.clear();
            for(const auto& entry:app.hud_snapshot.entries)app.popup_enabled.push_back(entry.compatible);
        }
        break;
    case HitCheats: app.page=Page::Cheats;refresh_native_cheats();break;
    case HitCheatsToggle: app.cheats_enabled=!app.cheats_enabled;save_settings();break;
    case HitFastForward: app.fast_forward=!app.fast_forward;save_settings();break;
    case HitFastForwardPad: app.fast_forward_pad=!app.fast_forward_pad;save_settings();break;
    case HitGame: app.page = Page::Game; update_game_status(); refresh_saves(); break;
    case HitVideo:
        refresh_target_display(); app.shader_cache_present=fs::is_directory(cache_directory());
        app.page = Page::Video; break;
    case HitControls: app.page = Page::Controls; refresh_controllers(); break;
    case HitAdvanced:
        app.runtime_present=fs::is_regular_file(runtime_path()); app.page = Page::Advanced; break;
    case HitLogs: app.page = Page::Logs; break;
    case HitFolder: {
        std::wstring path = pick_path(true);
        if (!path.empty()) {
            app.game_path = path;
            update_game_status();
            save_settings();
            refresh_saves();
            show_game_identity_notice();
        }
        break;
    }
    case HitPkg: {
        std::wstring path = pick_path(false);
        if (!path.empty()) confirm_pkg_extraction(fs::path(path));
        break;
    }
    case HitSaveFolder: {
        std::wstring path = pick_path(true);
        if (!path.empty()) { app.save_root = path; save_settings(); refresh_saves(); }
        break;
    }
    case HitDefaultSaveFolder: app.save_root.clear(); save_settings(); refresh_saves(); break;
    case HitPrepare: run_game(true); break;
    case HitPlay:
        if (app.busy) stop_game();
        else {
            update_game_status();
            if (!app.game_verified) app.page = Page::Game;
            else run_game(false);
        }
        break;
    case HitStop: stop_game(); break;
    case HitGpu:
        open_selector(HitGpu, gpu_selector_values()); break;
    case HitMonitor: {
        if(app.displays.size()<2) break;
        std::vector<std::wstring> choices{loc(L"Automatique · écran du lanceur",L"Automatic · launcher monitor")};
        for(size_t i=0;i<app.displays.size();++i) choices.push_back(display_label(i));
        open_selector(HitMonitor,choices); break;
    }
    case HitOutputAspect: {
        std::vector<std::wstring> choices;
        for(int aspect=0;aspect<Ps4RtOutput::AspectCount;++aspect)
            if(Ps4RtOutput::Filter(available_outputs(),aspect)) choices.push_back(output_aspect_label(aspect));
        open_selector(HitOutputAspect,choices); break;
    }
    case HitDisplayMode:
        if(app.target_display.fullscreen_modes)
            open_selector(HitDisplayMode, {loc(L"Fenêtré",L"Windowed"),loc(L"Plein écran",L"Fullscreen"),loc(L"Sans bordure",L"Borderless")});
        else open_selector(HitDisplayMode, {loc(L"Fenêtré",L"Windowed"),loc(L"Sans bordure",L"Borderless")});
        break;
    case HitAspectRatio:
        if (app.display_mode == L"windowed")
            open_selector(HitAspectRatio, {loc(L"Original",L"Original"),loc(L"Étirer à la fenêtre",L"Stretch to window")});
        break;
    case HitVsync: app.vsync = !app.vsync; save_settings(); write_runtime_settings(); break;
    case HitForceFsr3: {
        const auto* gpu=selected_adapter();
        if(!gpu || gpu->fsr3_recommended || !gpu->fsr3_technical || app.game_running || app.busy) break;
        if(fsr3_forced()) {
            app.force_fsr3_gpu.clear();if(app.fsr==L"FSR 3.1") app.fsr=L"off";save_settings();write_runtime_settings();
        } else show_modal(loc(L"Forcer FSR 3.1 ?",L"Force FSR 3.1?"),
            loc(L"Ce GPU est sous le seuil recommandé ou n’a pas été reconnu. Forcer FSR 3.1 peut provoquer des chutes de performances et de l’instabilité. Voulez-vous continuer ?",L"This GPU is below the recommended threshold or unrecognized. Forcing FSR 3.1 may cause performance drops and instability. Do you want to continue?"),
            loc(L"Activer quand même",L"Enable anyway"),loc(L"Annuler",L"Cancel"),ModalAction::ForceFsr3);
        break;
    }
    case HitFpsLimit:
        // The native Windows launcher does not apply an engine frame-rate patch. Keep only
        // limits that the original 60 FPS game loop can honour without changing game speed.
        open_selector(HitFpsLimit, {L"30 FPS",L"60 FPS",loc(L"Illimitée",L"Unlimited")}); break;
    case HitFsr: {
        open_selector(HitFsr,{loc(L"Désactivé",L"Disabled"),L"FSR 3.1",L"FSR 4",L"DLSS"});
        const bool temporal=Ps4RtOutput::TemporalAllowed(Ps4RtOutput::Parse(app.resolution));
        app.popup_enabled={true,temporal&&selected_gpu_supports_fsr3(),temporal&&selected_gpu_supports_fsr4(),temporal&&selected_gpu_supports_dlss()};break;
    }
    case HitNativeRender: {
        if(app.fsr!=L"off") break;
        std::vector<std::wstring> choices;
        const auto output=requested_scene_output();
        for(int height:Ps4RtScene::NativeHeights) {
            const auto size=Ps4RtScene::NativeInput(height,output.width,output.height,requested_ultrawide());
            choices.push_back(std::to_wstring(size.width)+L" × "+std::to_wstring(size.height));
        }
        open_selector(HitNativeRender,choices);break;
    }
    case HitResolution:
        refresh_target_display();
        app.popup_hit=HitResolution; app.popup_values.clear();app.popup_enabled.clear(); app.popup_scroll=0;
        for(int output:Ps4RtOutput::Order)
            if(Ps4RtOutput::Filter(available_outputs(),app.output_aspect)&(1ull<<output))
                app.popup_values.emplace_back(Ps4RtOutput::Labels[output]);
        break;
    case HitAnisotropy:
        open_selector(HitAnisotropy, {loc(L"Réglage du jeu",L"Game setting"),L"×2",L"×4",L"×8",L"×16"}); break;
    case HitAsyncShaders: app.async_shaders = !app.async_shaders; save_settings(); write_runtime_settings(); break;
    case HitPerformanceOverlay: app.performance_overlay = !app.performance_overlay; save_settings(); write_runtime_settings(); break;
    case HitOverlayFps: app.overlay_fps = !app.overlay_fps; save_settings(); write_runtime_settings(); break;
    case HitOverlayFrameTime: app.overlay_frametime = !app.overlay_frametime; save_settings(); write_runtime_settings(); break;
    case HitOverlayUpscaler: app.overlay_upscaler = !app.overlay_upscaler; save_settings(); write_runtime_settings(); break;
    case HitOverlayGpu: app.overlay_gpu = !app.overlay_gpu; save_settings(); write_runtime_settings(); break;
    case HitOverlayCpu: app.overlay_cpu = !app.overlay_cpu; save_settings(); write_runtime_settings(); break;
    case HitOverlayGpuModel: app.overlay_gpu_model = !app.overlay_gpu_model; save_settings(); write_runtime_settings(); break;
    case HitOverlayCpuModel: app.overlay_cpu_model = !app.overlay_cpu_model; save_settings(); write_runtime_settings(); break;
    case HitOverlayRam: app.overlay_ram = !app.overlay_ram; save_settings(); write_runtime_settings(); break;
    case HitOverlayVram: app.overlay_vram = !app.overlay_vram; save_settings(); write_runtime_settings(); break;
    case HitOverlayVersion: app.overlay_version = !app.overlay_version; save_settings(); write_runtime_settings(); break;
    case HitOverlayScale:
        open_selector(HitOverlayScale, {L"75 %",L"100 %",L"125 %",L"150 %",L"200 %"}); break;
    case HitOverlayOpacity:
        open_selector(HitOverlayOpacity, {L"0 %",L"25 %",L"50 %",L"75 %",L"100 %"}); break;
    case HitOverlayCorner:
        open_selector(HitOverlayCorner, {loc(L"En haut à gauche",L"Top left"),loc(L"En haut à droite",L"Top right"),
                                           loc(L"En bas à gauche",L"Bottom left"),loc(L"En bas à droite",L"Bottom right"),
                                           loc(L"En haut au milieu",L"Top center"),loc(L"En bas au milieu",L"Bottom center")}); break;
    case HitOverlayLayout:
        open_selector(HitOverlayLayout, {loc(L"Une seule ligne",L"One line"),
                                         loc(L"Une donnée par ligne",L"One item per line")}); break;
    case HitBackupToggle: app.automatic_save_backups = !app.automatic_save_backups; save_settings(); break;
    case HitOpenSaves: open_path(save_directory()); break;
    case HitOpenBackups: if (app.game_verified && !app.pkg_selected) open_path(backup_directory()); break;
    case HitImportSave: import_save(); break;
    case HitShortcut: create_shortcut(); break;
    case HitGameLanguage:
        if (!app.game_languages.empty() && !app.busy && !app.pkg_selected) {
            open_selector(HitGameLanguage, {L"English",L"Français",L"Deutsch",L"Italiano",L"Español"});
            // Keep unavailable translations visible; only installed, routed text languages are selectable.
            for (const auto& value : app.popup_values)
                app.popup_enabled.push_back(std::find(app.game_languages.begin(), app.game_languages.end(), value) != app.game_languages.end());
        }
        break;
    case HitController:
        refresh_controllers(); open_selector(HitController, app.detected_controllers); break;
    case HitKeyboardProfile:
        open_selector(HitKeyboardProfile, {L"AZERTY",L"QWERTY",loc(L"Personnalisé",L"Custom")}); break;
    case HitResetControls:
        app.keyboard_profile = L"AZERTY"; apply_keyboard_defaults(true); refresh_controllers();
        app.capture_binding = -1; save_settings(); break;
    case HitLauncherLanguage:
        open_selector(HitLauncherLanguage, {L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",
            L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"}); break;
    case HitUpdateToggle: app.update_on_start = !app.update_on_start; save_settings(); break;
    case HitCheckUpdate:
        check_for_updates(true);
        break;
    case HitOpenCache: open_path(cache_directory()); break;
    case HitClearCache: clear_cache(); break;
    case HitOpenRoot: open_path(app.root); break;
    case HitRefresh:
        detect_environment(); update_game_status(); refresh_target_display(true);
        app.runtime_present=fs::is_regular_file(runtime_path()); app.shader_cache_present=fs::is_directory(cache_directory()); break;
    case HitExportLogs: export_logs(); break;
    case HitPerformanceTools:
        app.performance_tools=!app.performance_tools;
        if(!app.performance_tools) app.performance_capture=false;
        save_settings();break;
    case HitPerformanceCapture:
        if(!app.performance_tools) break;
        if (app.busy) {
            app.status = loc(L"Arrêtez le jeu avant de modifier la capture des performances",
                             L"Stop the game before changing performance capture");
        } else {
            app.performance_capture = !app.performance_capture;
            save_settings();
            append_log(app.performance_capture
                           ? L"Performance capture enabled for the next game session."
                           : L"Performance capture disabled.");
        }
        break;
    case HitOpenPerformanceCaptures: if(app.performance_tools) open_path(performance_capture_directory()); break;
    case HitClearLogs: {
        std::lock_guard guard(app.mutex);
        app.logs.clear(); app.retained_diagnostics.Clear(); ++app.logs_revision; app.log_stream_pending.clear(); app.log_scroll_lines = 0; app.log_scroll_max = 0;
        app.last_update_log.clear();
        break;
    }
    case HitLogOlder: {
        std::lock_guard guard(app.mutex);
        app.log_scroll_lines = std::min(app.log_scroll_max, app.log_scroll_lines + 1); break;
    }
    case HitLogNewer: {
        std::lock_guard guard(app.mutex);
        app.log_scroll_lines = std::max(0, app.log_scroll_lines - 1); break;
    }
    case HitLogPageOlder: {
        std::lock_guard guard(app.mutex);
        app.log_scroll_lines = std::min(app.log_scroll_max, app.log_scroll_lines + 12); break;
    }
    case HitLogPageNewer: {
        std::lock_guard guard(app.mutex);
        app.log_scroll_lines = std::max(0, app.log_scroll_lines - 12); break;
    }
    default: break;
    }
}

std::wstring sdl_key_name(WPARAM key, LPARAM details) {
    UINT vk = static_cast<UINT>(key);
    const UINT scan = static_cast<UINT>((details >> 16) & 0xff);
    if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
        const UINT extended = MapVirtualKeyW(scan | ((details & 0x01000000) ? 0xe000 : 0), MAPVK_VSC_TO_VK_EX);
        if (extended) vk = extended;
    }
    if ((vk >= L'A' && vk <= L'Z') || (vk >= L'0' && vk <= L'9')) return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Keypad " + std::to_wstring(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_SPACE: return L"Space"; case VK_RETURN: return (details & 0x01000000) ? L"Keypad Enter" : L"Return";
    case VK_TAB: return L"Tab"; case VK_BACK: return L"Backspace";
    case VK_LEFT: return L"Left"; case VK_RIGHT: return L"Right"; case VK_UP: return L"Up"; case VK_DOWN: return L"Down";
    case VK_LSHIFT: return L"Left Shift"; case VK_RSHIFT: return L"Right Shift";
    case VK_LCONTROL: return L"Left Ctrl"; case VK_RCONTROL: return L"Right Ctrl";
    case VK_LMENU: return L"Left Alt"; case VK_RMENU: return L"Right Alt";
    case VK_CAPITAL: return L"CapsLock"; case VK_INSERT: return L"Insert"; case VK_DELETE: return L"Delete";
    case VK_HOME: return L"Home"; case VK_END: return L"End"; case VK_PRIOR: return L"PageUp"; case VK_NEXT: return L"PageDown";
    case VK_OEM_MINUS: return L"Minus"; case VK_OEM_PLUS: return L"Equals";
    case VK_OEM_4: return L"Left Bracket"; case VK_OEM_6: return L"Right Bracket"; case VK_OEM_5: return L"Backslash";
    case VK_OEM_1: return L"Semicolon"; case VK_OEM_7: return L"Apostrophe"; case VK_OEM_3: return L"Grave";
    case VK_OEM_COMMA: return L"Comma"; case VK_OEM_PERIOD: return L"Period"; case VK_OEM_2: return L"Slash";
    case VK_MULTIPLY: return L"Keypad Multiply"; case VK_ADD: return L"Keypad Plus"; case VK_SUBTRACT: return L"Keypad Minus";
    case VK_DECIMAL: return L"Keypad Period"; case VK_DIVIDE: return L"Keypad Divide";
    default: return {};
    }
}

HBRUSH chrome_background() {
    static const HBRUSH brush=CreateSolidBrush(RGB(8,27,42));
    return brush;
}
LRESULT CALLBACK chrome_button_proc(HWND button, UINT message, WPARAM wparam, LPARAM lparam,
                                    UINT_PTR subclass, DWORD_PTR) {
    if(message==WM_ERASEBKGND) {
        RECT area{};GetClientRect(button,&area);
        FillRect(reinterpret_cast<HDC>(wparam),&area,chrome_background());
        return 1;
    }
    if (message == WM_GETDLGCODE && wparam == VK_RETURN) return DLGC_WANTMESSAGE;
    if (message == WM_KEYDOWN && wparam == VK_RETURN) {
        SendMessageW(GetParent(button), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(button), BN_CLICKED),
                     reinterpret_cast<LPARAM>(button));
        return 0;
    }
    if (message == WM_MOUSEMOVE) {
        if (!GetPropW(button, L"CockpitHover")) {
            SetPropW(button, L"CockpitHover", reinterpret_cast<HANDLE>(1));
            InvalidateRect(button, nullptr, FALSE);
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, button, 0}; TrackMouseEvent(&track);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(button, L"CockpitHover"); InvalidateRect(button, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(button, L"CockpitHover"); RemoveWindowSubclass(button, chrome_button_proc, subclass);
    }
    return DefSubclassProc(button, message, wparam, lparam);
}

void draw_chrome_button(const DRAWITEMSTRUCT& item) {
    const bool close = item.CtlID == kCloseButton;
    const bool hot = GetPropW(item.hwndItem, L"CockpitHover") != nullptr;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    RECT area{}; GetClientRect(item.hwndItem, &area);
    const COLORREF color = disabled ? RGB(2, 12, 22) : pressed ? RGB(31, 94, 117) :
        hot ? (close ? RGB(155, 51, 64) : RGB(22, 57, 77)) : RGB(8, 27, 42);
    HBRUSH bg = CreateSolidBrush(color); FillRect(item.hDC, &area, bg); DeleteObject(bg);
    const int cx = area.right / 2, cy = area.bottom / 2;
    HPEN stroke = CreatePen(PS_SOLID, 2, disabled ? RGB(62, 84, 98) : RGB(224, 242, 250));
    const HGDIOBJ old = SelectObject(item.hDC, stroke);
    if (close) {
        MoveToEx(item.hDC, cx - 5, cy - 5, nullptr); LineTo(item.hDC, cx + 6, cy + 6);
        MoveToEx(item.hDC, cx + 5, cy - 5, nullptr); LineTo(item.hDC, cx - 6, cy + 6);
    } else { MoveToEx(item.hDC, cx - 6, cy + 2, nullptr); LineTo(item.hDC, cx + 7, cy + 2); }
    SelectObject(item.hDC, old); DeleteObject(stroke);
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&area,-3,-3);
        HPEN focus=CreatePen(PS_SOLID,1,RGB(82,207,244));
        const auto previous_pen=SelectObject(item.hDC,focus);
        const auto previous_brush=SelectObject(item.hDC,GetStockObject(NULL_BRUSH));
        Rectangle(item.hDC,area.left,area.top,area.right,area.bottom);
        SelectObject(item.hDC,previous_brush);SelectObject(item.hDC,previous_pen);DeleteObject(focus);
    }
}

void position_chrome(HWND window,int width) {
    if(!app.minimize_button||!app.close_button)return;
    static int previous_width=-1;
    if(width==previous_width)return; // Height-only resize needs no child move.
    previous_width=width;
    constexpr UINT flags=SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOREDRAW|SWP_NOCOPYBITS;
    HDWP batch=BeginDeferWindowPos(2);
    if(batch)batch=DeferWindowPos(batch,app.minimize_button,nullptr,width-104,0,52,kDragHeight,flags);
    if(batch)batch=DeferWindowPos(batch,app.close_button,nullptr,width-52,0,52,kDragHeight,flags);
    if(!batch||!EndDeferWindowPos(batch)) {
        SetWindowPos(app.minimize_button,nullptr,width-104,0,52,kDragHeight,flags);
        SetWindowPos(app.close_button,nullptr,width-52,0,52,kDragHeight,flags);
    }
    // One coalesced repaint, without synchronous intermediate child erasure.
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_NOERASE);
}

void confirm_close() {
    if (app.modal_visible || app.close_when_idle) return;
    if (IsIconic(app.window)) ShowWindow(app.window, SW_RESTORE);
    if (app.game_running) {
        show_modal(loc(L"Le jeu est en cours",L"Game is running"),
                   loc(L"Arrêter le jeu et fermer le lanceur ? La progression non sauvegardée peut être perdue. Vous pouvez plutôt réduire le lanceur.",
                       L"Stop the game and close the launcher? Unsaved progress may be lost. You can minimize the launcher instead."),
                   loc(L"Arrêter et fermer",L"Stop game and close"), loc(L"Annuler",L"Cancel"),
                   ModalAction::CloseGame, loc(L"Réduire",L"Minimize"));
    } else if (app.busy) {
        show_modal(loc(L"Opération en cours",L"Operation in progress"),
                   loc(L"Veuillez attendre la fin de l’opération avant de fermer le lanceur.",
                       L"Please wait for the current operation to finish before closing the launcher."), L"OK");
    } else {
        show_modal(loc(L"Fermer le lanceur ?",L"Close launcher?"),
                   loc(L"Vos paramètres seront conservés.",L"Your settings will be kept."),
                   loc(L"Fermer le lanceur",L"Close launcher"), loc(L"Annuler",L"Cancel"), ModalAction::CloseIdle);
    }
    SetFocus(app.window);
}

void reveal_quick_launch_failure() {
    if (!app.quick_launch) return;
    {
        std::lock_guard guard(app.mutex);
        if (!app.modal_visible && (app.busy || app.game_running)) return;
    }
    app.quick_launch = false;
    app.page = app.game_verified ? Page::Logs : Page::Game;
    append_log(L"Quick play could not continue; launcher opened for setup or diagnostics.");
    ShowWindow(app.window, SW_SHOWNORMAL);
    SetForegroundWindow(app.window);
    InvalidateRect(app.window, nullptr, FALSE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        app.window = window;
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
        const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        app.log_console.Create(window);
        app.minimize_button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 52, kDragHeight, window, reinterpret_cast<HMENU>(kMinimizeButton), instance, nullptr);
        app.close_button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 52, kDragHeight, window, reinterpret_cast<HMENU>(kCloseButton), instance, nullptr);
        app.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
        for (HWND button : {app.minimize_button, app.close_button}) {
            SetWindowSubclass(button, chrome_button_proc, 1, 0);
            SetWindowTheme(button,L"",L"");
            TOOLINFOW tool{sizeof(tool)};
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS; tool.hwnd = window;
            tool.uId = reinterpret_cast<UINT_PTR>(button); tool.lpszText = const_cast<wchar_t*>(L"");
            SendMessageW(app.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        sync_chrome();
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            if (LOWORD(wparam) == kMinimizeButton) { ShowWindow(window, SW_MINIMIZE); return 0; }
            if (LOWORD(wparam) == kCloseButton) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        }
        break;
    case WM_DRAWITEM:
        if (wparam == kMinimizeButton || wparam == kCloseButton) {
            draw_chrome_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam)); return TRUE;
        }
        break;
    case WM_CTLCOLORBTN:
        if(reinterpret_cast<HWND>(lparam)==app.minimize_button||reinterpret_cast<HWND>(lparam)==app.close_button) {
            SetBkColor(reinterpret_cast<HDC>(wparam),RGB(8,27,42));
            return reinterpret_cast<LRESULT>(chrome_background());
        }
        break;
    case WM_NCCALCSIZE:
        if (wparam) return 0;
        break;
    case WM_PAINT: paint(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (app.modal_visible && (wparam == VK_ESCAPE || wparam == VK_RETURN)) {
            activate(app.modal_secondary.empty() ? HitModalPrimary : HitModalSecondary);
            InvalidateRect(window, nullptr, FALSE); return 0;
        }
        if (!app.modal_visible && wparam == VK_TAB && app.capture_binding < 0) {
            SetFocus(app.minimize_button); return 0;
        }
        if (app.capture_binding >= 0) {
            if (wparam == VK_ESCAPE) {
                app.capture_binding = -1;
                app.status = loc(L"Modification annulée",L"Change cancelled");
            } else {
                const std::wstring name = sdl_key_name(wparam, lparam);
                if (!name.empty()) {
                    const int target = app.capture_binding;
                    for (size_t i = 0; i < app.key_bindings.size(); ++i) {
                        if (static_cast<int>(i) != target && app.key_bindings[i] == name)
                            std::swap(app.key_bindings[i], app.key_bindings[target]);
                    }
                    app.key_bindings[target] = name;
                    app.keyboard_profile = L"Custom";
                    app.capture_binding = -1;
                    app.status = loc(L"Mappage enregistré",L"Mapping saved");
                    save_settings();
                } else app.status = loc(L"Cette touche n’est pas prise en charge",L"This key is not supported");
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_SIZE:
        if(wparam==SIZE_MINIMIZED)return 0;
        position_chrome(window,LOWORD(lparam));
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: {
        if (app.modal_visible) return 0;
        if(app.popup_hit!=HitNone) {
            RECT area{}; GetClientRect(window,&area);
            const int visible=std::min<int>(app.popup_values.size(),std::max(1,int((area.bottom-kHeaderHeight-90)/48)));
            app.popup_scroll=std::clamp(app.popup_scroll-GET_WHEEL_DELTA_WPARAM(wparam)/WHEEL_DELTA*3,
                                      0,int(app.popup_values.size())-visible);
            app.hover=HitNone; InvalidateRect(window,nullptr,FALSE); return 0;
        }
        if (app.page == Page::Home) return 0;
        if (app.page == Page::Logs) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            {
                std::lock_guard guard(app.mutex);
                app.log_scroll_lines = std::clamp(app.log_scroll_lines + steps * 3, 0, app.log_scroll_max);
            }
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        RECT client{}; GetClientRect(window, &client);
        const int required = app.page == Page::Controls ? 1200 : app.page == Page::Game ? 900 :
                             app.page == Page::Advanced ? 1998 : app.page == Page::Video ? 1040 :
                             app.page == Page::Tweaks ? tweaks_content_height : app.page == Page::Cheats ? cheats_content_height() : 0;
        const int maximum = std::max(0, required - (static_cast<int>(client.bottom) - kHeaderHeight - 28));
        int& offset = app.scroll[static_cast<int>(app.page)];
        offset = std::clamp(offset - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 72, 0, maximum);
        InvalidateRect(window, nullptr, FALSE); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTOPRIMARY),&monitor);
        const RECT& work = monitor.rcWork;
        info->ptMinTrackSize = POINT{std::min<LONG>(1080,work.right-work.left-24),
                                    std::min<LONG>(640,work.bottom-work.top-24)};
        return 0;
    }
    case WM_EXITSIZEMOVE:
        refresh_target_display();
        InvalidateRect(window,nullptr,FALSE); return 0;
    case WM_DISPLAYCHANGE:
        app.popup_hit=HitNone; app.popup_values.clear();app.popup_enabled.clear();
        refresh_target_display(true);
        InvalidateRect(window,nullptr,FALSE); return 0;
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        int now = hit_at(point);
        hover_at(window,now);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking);
        return 0;
    }
    case WM_MOUSELEAVE: hover_at(window,HitNone); return 0;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        activate(hit_at(point));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT normal = DefWindowProcW(window, message, wparam, lparam);
        if (normal != HTCLIENT) return normal;
        POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}, point = screen;
        ScreenToClient(window, &point);
        RECT r{}; GetClientRect(window, &r);
        const int border = 7;
        if (point.y < border) return point.x < border ? HTTOPLEFT : point.x > r.right - border ? HTTOPRIGHT : HTTOP;
        if (point.y > r.bottom - border) return point.x < border ? HTBOTTOMLEFT : point.x > r.right - border ? HTBOTTOMRIGHT : HTBOTTOM;
        if (point.x < border) return HTLEFT;
        if (point.x > r.right - border) return HTRIGHT;
        // The uninterrupted top strip is a real Windows caption hit area. Tabs and
        // native caption buttons stay in the client area; resizing keeps its edges.
        if (point.y < kDragHeight && point.x < r.right - 104) return HTCAPTION;
        if (point.y < kHeaderHeight && point.x < 238) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        if (app.modal_visible || app.busy || app.update_transfer) { DragFinish(drop); return 0; }
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, 32768)) {
            const fs::path dropped(path);
            if (dropped.extension() == L".pkg" || dropped.extension() == L".PKG") confirm_pkg_extraction(dropped);
            else {
                app.game_path = path;
                update_game_status();
                save_settings();
                refresh_saves();
                show_game_identity_notice();
            }
        }
        DragFinish(drop); InvalidateRect(window, nullptr, FALSE); return 0;
    }
    case WM_LOG_UPDATED:
        app.log_update_pending = false;
        if (app.modal_visible) reveal_quick_launch_failure();
        if(app.extraction_active && app.modal_action==ModalAction::CancelExtraction) {
            std::lock_guard guard(app.mutex);
            if(!app.extraction_title.empty()) {
                app.extraction_destination=(pkg_import_root()/app.extraction_title).wstring();
                app.modal_body=app.extraction_source+L"\n"+loc(L"Destination : ",L"Destination: ")+app.extraction_destination;
            }
        }
        EnableWindow(app.minimize_button, !app.modal_visible);
        EnableWindow(app.close_button, !app.modal_visible);
        if (app.modal_visible && GetActiveWindow() == window && GetFocus() != window) SetFocus(window);
        if (!IsIconic(window)) InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_UPDATE_DONE: receive_update_event(); return 0;
    case WM_CHEATS_READY: receive_native_cheats(); return 0;
    case WM_QUICK_FAILED: reveal_quick_launch_failure(); return 0;
    case WM_GAME_STARTED:
        app.game_pid = static_cast<DWORD>(wparam); app.game_running = true;
        if (app.quick_launch) return 0;
        app.waiting_for_game_window = true; app.auto_minimized = false;
        SetTimer(window, 2, 150, nullptr);
        return 0;
    case WM_GAME_ENDED:
        KillTimer(window, 2); app.game_running = false; app.waiting_for_game_window = false;
        read_live_runtime_settings();app.tweaks=So5Tweaks::Read(tweaks_config());refresh_saves();
        app.shader_cache_present=fs::is_directory(cache_directory());
        app.game_pid = 0;
        if (app.quick_launch) {
            if (wparam == 0) { DestroyWindow(window); return 0; }
            reveal_quick_launch_failure();
        }
        if (app.close_when_idle) {
            if (app.update_running) { app.update_cancel = true; app.close_after_update = true; }
            else DestroyWindow(window);
            return 0;
        }
        if (app.auto_minimized && IsIconic(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
        app.auto_minimized = false;
        if (wparam != 0) {
            app.page = Page::Logs;
            FLASHWINFO flash{sizeof(flash), window, FLASHW_TRAY, 3, 0}; FlashWindowEx(&flash);
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_TIMER:
        if(wparam==5) {
            if(app.hud_library) {
                app.hud_snapshot=app.hud_library->Snapshot();
                if(!app.hud_snapshot.busy) {
                    KillTimer(window,5);
                    app.hud_selection=from_utf8(app.hud_snapshot.active_name.data(),int(app.hud_snapshot.active_name.size()));
                    if(app.hud_snapshot.status==So5Hud::LibraryStatus::Failed) {
                        app.hud_selection.clear();append_log(L"Tweaks: startup HUD layout could not be changed.");
                    }
                }
                if(!IsIconic(window)&&app.page==Page::Tweaks)InvalidateRect(window,nullptr,FALSE);
            }else KillTimer(window,5);
            return 0;
        }
        if (wparam == 4) {
            KillTimer(window, 4);
            if (app.update_cleanup) {
                try { So5Update::RemoveWorkspace(*app.update_cleanup); }
                catch (const std::exception& error) { append_log(L"Update staging cleanup deferred · " + from_utf8(error.what(), int(std::strlen(error.what())))); }
                app.update_cleanup.reset();
            }
            return 0;
        }
        if (wparam == 3) {
            show_update_notice();
            if (app.modal_action == ModalAction::UpdateProgress) InvalidateRect(window, nullptr, FALSE);
            if (!app.update_running && !app.update_notice) KillTimer(window, 3);
            return 0;
        }
        if (wparam == 2) {
            if (app.waiting_for_game_window && !app.modal_visible && !app.close_when_idle) {
                HWND game_window = nullptr;
                EnumWindows([](HWND target, LPARAM context) -> BOOL {
                    DWORD pid{}; GetWindowThreadProcessId(target, &pid);
                    if (pid != app.game_pid || !IsWindowVisible(target) || GetWindow(target, GW_OWNER)) return TRUE;
                    *reinterpret_cast<HWND*>(context) = target; return FALSE;
                }, reinterpret_cast<LPARAM>(&game_window));
                if (game_window) {
                    app.waiting_for_game_window = false; app.auto_minimized = true;
                    ShowWindow(window, SW_MINIMIZE);
                }
            }
            if (app.close_when_idle && app.close_deadline && GetTickCount64() >= app.close_deadline) {
                app.close_when_idle = false; app.close_deadline = 0;
                if (IsIconic(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
                show_modal(loc(L"Le jeu est en cours",L"Game is running"),
                    loc(L"Le jeu ne s’est pas encore arrêté. Un arrêt forcé peut perdre la progression non sauvegardée.",
                        L"The game has not stopped yet. Force closing may lose unsaved progress."),
                    loc(L"Forcer l’arrêt et fermer",L"Force stop and close"), loc(L"Annuler",L"Cancel"), ModalAction::ForceCloseGame);
            }
            return 0;
        }
        if (wparam == 1) {
            if (app.extraction_active) InvalidateRect(window, nullptr, FALSE);
            else KillTimer(window, 1);
            return 0;
        }
    case WM_AUTOPLAY:
        run_game(false);
        reveal_quick_launch_failure();
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_CLOSE:
        confirm_close(); return 0;
    case WM_DESTROY:
        if(app.cheat_thread.joinable())app.cheat_thread.join();
        app.update_cancel = true;
        if (app.update_thread.joinable()) app.update_thread.join();
        if (app.job) { TerminateJobObject(app.job, 0); CloseHandle(app.job); app.job = nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    app.root = find_root();
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    const auto launch_mode = So5Shortcuts::Mode(argument_count, arguments);
    if(arguments && argument_count==3 && std::wstring(arguments[1])==L"--native-resolution-self-test") {
        const fs::path fixture=fs::absolute(arguments[2]);LocalFree(arguments);
        // Never reuse an existing folder or touch owner settings during this test.
        if(fs::exists(fixture) || !fs::create_directory(fixture)) return 3;
        app.root=fixture;app.config=fixture/L"out/launcher.ini";
        const wchar_t* languages[]={L"English",L"Français",L"Español",L"Português",L"Português (Brasil)",L"Italiano",L"Deutsch",L"Русский",L"简体中文",L"繁體中文",L"한국어",L"日本語"};
        for(const auto language:languages) {
            app.launcher_language=language;app.fsr=L"FSR 3.1";
            app.popup_hit=HitFsr;app.popup_values={loc(L"Désactivé",L"Disabled"),L"FSR 3.1",L"FSR 4",L"DLSS"};
            apply_selector(0);if(app.fsr!=L"off") return 4;
            for(size_t i=0;i<Ps4RtScene::NativeHeights.size();++i) {
                app.popup_hit=HitNativeRender;app.popup_values={L"720p",L"1080p",L"1440p",L"2160p"};
                apply_selector(int(i));
                const int height=Ps4RtScene::NativeHeights[i];
                if(app.native_render_height!=height || GetPrivateProfileIntW(L"Launcher",L"NativeRenderHeight",0,app.config.c_str())!=UINT(height)) return 5;
                std::ifstream file(fixture/L"out/CUSA03219/ps4rt.ini");
                const std::string text((std::istreambuf_iterator<char>(file)),{});
                if(text.find("native_render_height="+std::to_string(height)+"\n")==std::string::npos || text.find("upscaler=off\n")==std::string::npos) return 6;
                app.fsr=L"FSR 3.1";app.popup_hit=HitNativeRender;app.popup_values={L"720p"};apply_selector(0);
                if(app.native_render_height!=height) return 7;
                app.fsr=L"off";
            }
        }
        std::ofstream(fixture/L"result.json")<<"{\"passed\":true,\"languages\":12,\"manual_choices\":48,\"temporal_override_blocked\":48}\n";
        return 0;
    }
    if(arguments && argument_count==3 && std::wstring(arguments[1])==L"--ui-benchmark") {
        const fs::path report=arguments[2]; LocalFree(arguments);
        return render_benchmark(report);
    }
    if (arguments && argument_count == 3 && std::wstring(arguments[1]) == L"--native-prepare-test") {
        int result = 0;
        try { ps4rt::prepare_title(arguments[2], app.root / "out" / "native-preparer-test", {}); }
        catch (...) { result = 3; }
        LocalFree(arguments);
        return result;
    }
    if (arguments) LocalFree(arguments);
    if (std::wstring(GetCommandLineW()).find(L"--self-test") != std::wstring::npos) {
        const bool title_ids_ok = classify_title_id(L"CUSA03219") == GameIdentity::Tested &&
                                  classify_title_id(L"CUSA03246") == GameIdentity::Untested &&
                                  classify_title_id(L"CUSA01722") == GameIdentity::Untested &&
                                  classify_title_id(L"CUSA03567") == GameIdentity::Untested &&
                                  classify_title_id(L"CUSA00000") == GameIdentity::Rejected &&
                                  classify_title_id(L"BLJM61220") == GameIdentity::Rejected;
        const bool complete = title_ids_ok && fs::is_regular_file(runtime_path()) &&
                              fs::is_regular_file(pkg_extractor_path()) &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "launcher-background.png") &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "launcher-icon.png") &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "star-ocean-logo.png") &&
                              (fs::is_regular_file(app.root / "bin/SO5-Updater.exe") ||
                               fs::is_regular_file(app.root / "tools/updater/SO5-Updater.exe") ||
                               fs::is_regular_file(app.root / "out/SO5-Updater.exe"));
        return complete ? 0 : 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input;
    GdiplusStartup(&app.gdiplus_token, &input, nullptr);
    app.config = app.root / "out" / "launcher.ini";
    load_settings();
    app.quick_launch = launch_mode == So5Shortcuts::LaunchMode::QuickPlay;
    detect_environment();
    app.runtime_present=fs::is_regular_file(runtime_path());
    app.shader_cache_present=fs::is_directory(cache_directory());
    update_game_status();
    refresh_saves();
    app.background.reset(Image::FromFile((app.root / "launcher" / "assets" / "launcher-background.png").c_str()));
    app.launcher_icon.reset(Image::FromFile((app.root / "launcher" / "assets" / "launcher-icon.png").c_str()));
    app.game_logo.reset(Image::FromFile((app.root / "launcher" / "assets" / "star-ocean-logo.png").c_str()));

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    refresh_target_display();
    RECT work = app.target_display.info.rcWork;
    int width = std::min(1580,int((work.right-work.left)*.84));
    int height = std::min(940,int((work.bottom-work.top)*.84));
    const std::wstring caption = std::wstring(L"STAR OCEAN 5 · Launcher Windows ") + kVersion;
    app.window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, caption.c_str(),
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        work.left + (work.right - work.left - width) / 2,
        work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, instance, nullptr);
    const BOOL dark_frame = TRUE;
    const COLORREF no_border = 0xFFFFFFFE;
    const DWORD dark_attribute = 20;
    const DWORD border_attribute = 34;
    DwmSetWindowAttribute(app.window, dark_attribute, &dark_frame, sizeof(dark_frame));
    DwmSetWindowAttribute(app.window, border_attribute, &no_border, sizeof(no_border));
    sync_chrome();
    refresh_target_display();
    SDL_SetAppMetadata("STAR OCEAN 5 Windows Launcher", PS4RT_VERSION_STRING, "org.ps4rt.star-ocean-5.launcher");
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    DragAcceptFiles(app.window, TRUE);
    app.backup_guard=std::make_unique<SaveBackupGuard>(app.root/L"out/CUSA03219/user/savedata-backups");
    if (!app.quick_launch) {
        ShowWindow(app.window, show);
        UpdateWindow(app.window);
    }
    try {
        if (!app.quick_launch) if (const auto result = So5Update::ReadInstallResult(app.root)) {
            append_log(L"Update result · " + from_utf8(result->detail.c_str(), int(result->detail.size())));
            std::wstring body = result->success
                ? loc(L"Mise à jour installée avec succès.",L"Update installed successfully.")
                : result->recovered ? loc(L"La mise à jour a échoué. La version précédente a été restaurée.",L"Update failed. The previous version was restored.")
                : loc(L"La mise à jour a échoué et la récupération nécessite une intervention. Consultez le dossier de secours et Journal et diagnostic.",
                      L"Update failed and recovery needs attention. See the backup folder and Logs and diagnostics.");
            if (!result->backup.empty()) body += L"\n\n" + std::wstring(loc(L"Dossier de secours : ",L"Backup folder: ")) + result->backup.wstring();
            show_modal(loc(L"Mises à jour Windows",L"Windows updates"), body, L"OK");
        }
        app.update_cleanup = So5Update::PendingCleanup(app.root);
        if (app.update_cleanup) SetTimer(app.window, 4, 1500, nullptr);
    } catch (const std::exception& error) { append_log(L"Update result could not be read · " + from_utf8(error.what(), int(std::strlen(error.what())))); }
    if (app.update_on_start && !app.quick_launch) check_for_updates(false);
    if (launch_mode != So5Shortcuts::LaunchMode::Interactive)
        PostMessageW(app.window, WM_AUTOPLAY, 0, 0);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!app.modal_visible && app.capture_binding < 0 && IsDialogMessageW(app.window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    drawing_cache.Clear();
    app.save_preview.reset();
    app.game_logo.reset();
    app.launcher_icon.reset();
    app.background.reset();
    app.backup_guard.reset();
    SDL_Quit();
    GdiplusShutdown(app.gdiplus_token);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
