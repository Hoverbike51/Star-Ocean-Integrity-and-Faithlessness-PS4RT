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
#include <SDL3/SDL.h>

#include "resource.h"
#include "native_preparer.h"
#include "localization.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
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
constexpr wchar_t kClassName[] = L"PS4RTStarOceanLauncher";
constexpr wchar_t kVersion[] = L"0.3.5";
constexpr int kBindingHitBase = 3000;

enum class Page { Home, Game, Video, Controls, Advanced, Logs };
enum class GameIdentity { None, Tested, Untested, Rejected };
enum class Indicator { Red, Orange, Green };
enum Hit {
    HitNone, HitClose, HitMinimize,
    HitHome, HitGame, HitVideo, HitControls, HitAdvanced, HitLogs,
    HitFolder, HitPkg, HitPrepare, HitPlay, HitStop,
    HitSaveFolder, HitDefaultSaveFolder, HitOpenSaves, HitBackupToggle,
    HitBackupSave, HitImportSave, HitShortcut, HitGameLanguage,
    HitDisplayMode, HitVsync, HitFpsLimit, HitFsr, HitResolution,
    HitRenderResolution, HitAnisotropy, HitAsyncShaders,
    HitController, HitKeyboardProfile, HitResetControls,
    HitLauncherLanguage, HitUpdateToggle, HitCheckUpdate,
    HitOpenCache, HitClearCache, HitOpenRoot, HitRefresh,
    HitExportLogs, HitClearLogs,
    HitModalPrimary, HitModalSecondary, HitModalTertiary, HitModalBackdrop
};

enum class ModalAction { None, ClearCache, LaunchWithoutBackup, CloseBusy, ConfirmExtraction, CancelExtraction };

struct AppState {
    HWND window{};
    ULONG_PTR gdiplus_token{};
    std::unique_ptr<Image> background;
    std::unique_ptr<Image> launcher_icon;
    std::unique_ptr<Image> game_logo;
    std::unique_ptr<Image> save_preview;
    fs::path root;
    fs::path config;
    std::wstring game_path;
    std::wstring save_root;
    std::wstring pkg_extract_root;
    std::wstring fsr = L"FSR 3.1";
    std::wstring resolution = L"1920 × 1080";
    std::wstring render_resolution = L"Native 1080p";
    std::wstring display_mode = L"Sans bordure";
    std::wstring game_language = L"Français";
    std::wstring launcher_language = L"Français";
    std::wstring keyboard_profile = L"AZERTY";
    std::wstring controller = L"Automatique · SDL3";
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
    bool skip_backup_once = false;
    bool extraction_active = false;
    std::atomic_bool extraction_cancel_requested{false};
    ULONGLONG extraction_started = 0;
    std::wstring extraction_source;
    std::wstring extraction_destination;
    std::uintmax_t extraction_size = 0;
    fs::path pending_pkg;
    std::vector<std::wstring> logs;
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
    bool backup_before_launch = true;
    bool update_on_start = true;
    bool game_verified = false;
    HANDLE job{};
    std::mutex mutex;
    std::vector<std::pair<int, RECT>> hits;
    int hover = HitNone;
    int popup_hit = HitNone;
    std::vector<std::wstring> popup_values;
    std::array<int, 6> scroll{};
    bool clipping_content_hits = false;
};

AppState app;

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

std::wstring display_mode_label() {
    if (app.display_mode == L"Fenêtré" || app.display_mode == L"Windowed") return loc(L"Fenêtré",L"Windowed");
    if (app.display_mode == L"Plein écran" || app.display_mode == L"Fullscreen") return loc(L"Plein écran",L"Fullscreen");
    return loc(L"Sans bordure",L"Borderless");
}

std::wstring fsr_label() {
    return app.fsr == L"Désactivé" || app.fsr == L"Disabled" ? loc(L"Désactivé",L"Disabled") : app.fsr;
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

void append_log(const std::wstring& text) {
    std::lock_guard guard(app.mutex);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find_first_of(L"\r\n", at);
        std::wstring line = text.substr(at, end == std::wstring::npos ? text.size() - at : end - at);
        if (!line.empty()) app.logs.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        at = end + 1;
        if (text[end] == L'\r' && at < text.size() && text[at] == L'\n') ++at;
    }
    if (app.logs.size() > 1200) app.logs.erase(app.logs.begin(), app.logs.begin() + 400);
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
    PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
}

fs::path executable_directory() {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return fs::path(path).parent_path();
}

fs::path find_root() {
    fs::path directory = executable_directory();
    if (fs::exists(directory / "launcher" / "assets")) return directory;
    if (fs::exists(directory.parent_path() / "launcher" / "assets")) return directory.parent_path();
    return directory;
}

fs::path runtime_path() {
    const fs::path packaged = app.root / "ps4rt.exe";
    return fs::is_regular_file(packaged) ? packaged : app.root / "out" / "ps4rt.exe";
}

fs::path pkg_extractor_path() {
    const fs::path packaged = app.root / "PS4RT-PkgExtractor.exe";
    return fs::is_regular_file(packaged) ? packaged : app.root / "out" / "PS4RT-PkgExtractor.exe";
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

std::wstring find_program(const wchar_t* name) {
    std::wstring buffer(32768, L'\0');
    DWORD length = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length && length < buffer.size()) { buffer.resize(length); return buffer; }
    return {};
}

void detect_environment() {
    app.native_preparer_ok = true;
    HMODULE vulkan = LoadLibraryW(L"vulkan-1.dll");
    app.vulkan_ok = vulkan && GetProcAddress(vulkan, "vkEnumerateInstanceVersion");
    if (vulkan) FreeLibrary(vulkan);
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
    WritePrivateProfileStringW(L"Launcher", L"GamePath", app.game_path.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"SaveRoot", app.save_root.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"PkgExtractRoot", app.pkg_extract_root.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"FSR", app.fsr.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Resolution", app.resolution.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"RenderResolution", app.render_resolution.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"DisplayMode", app.display_mode.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"GameLanguage", app.game_language.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"LauncherLanguage", app.launcher_language.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"KeyboardProfile", app.keyboard_profile.c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Controller", app.controller.c_str(), app.config.c_str());
    for (size_t i = 0; i < app.key_bindings.size(); ++i)
        WritePrivateProfileStringW(L"Keyboard", kBindingIniNames[i], app.key_bindings[i].c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"FpsLimit", std::to_wstring(app.fps_limit).c_str(), app.config.c_str());
    WritePrivateProfileStringW(L"Launcher", L"Anisotropy", std::to_wstring(app.anisotropy).c_str(), app.config.c_str());
    write_bool(L"Vsync", app.vsync);
    write_bool(L"AsyncShaders", app.async_shaders);
    write_bool(L"BackupBeforeLaunch", app.backup_before_launch);
    write_bool(L"UpdateOnStart", app.update_on_start);
}

void load_settings() {
    app.game_path = ini_value(L"Launcher", L"GamePath", L"");
    app.save_root = ini_value(L"Launcher", L"SaveRoot", L"");
    app.pkg_extract_root = ini_value(L"Launcher", L"PkgExtractRoot", L"");
    app.fsr = ini_value(L"Launcher", L"FSR", L"FSR 3.1");
    app.resolution = ini_value(L"Launcher", L"Resolution", L"1920 × 1080");
    app.render_resolution = ini_value(L"Launcher", L"RenderResolution", L"Native 1080p");
    app.display_mode = ini_value(L"Launcher", L"DisplayMode", L"Sans bordure");
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
    apply_keyboard_defaults(app.keyboard_profile != L"QWERTY");
    for (size_t i = 0; i < app.key_bindings.size(); ++i)
        app.key_bindings[i] = ini_value(L"Keyboard", kBindingIniNames[i], app.key_bindings[i].c_str());
    app.fps_limit = GetPrivateProfileIntW(L"Launcher", L"FpsLimit", 60, app.config.c_str());
    app.anisotropy = GetPrivateProfileIntW(L"Launcher", L"Anisotropy", 16, app.config.c_str());
    app.vsync = ini_bool(L"Vsync", true);
    app.async_shaders = ini_bool(L"AsyncShaders", true);
    app.backup_before_launch = ini_bool(L"BackupBeforeLaunch", true);
    app.update_on_start = ini_bool(L"UpdateOnStart", true);
}

bool game_layout_complete(const fs::path& path) {
    const fs::path sfo = path / "sce_sys" / "param.sfo";
    return fs::is_directory(path) && fs::is_regular_file(path / "eboot.bin") &&
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
    } else if (!game_layout_complete(selected)) {
        app.status = loc(L"Dossier invalide · eboot.bin, sce_sys et sce_module requis",L"Invalid directory · eboot.bin, sce_sys and sce_module required");
        app.prepared = false;
    } else {
        app.game_identity = inspect_game_folder(selected, &app.detected_title_id);
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
    if (!folder) dialog->SetFileTypes(2, filter);
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
    app.save_preview.reset(fs::is_regular_file(icon) ? Image::FromFile(icon.c_str()) : nullptr);
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
        app.save_preview.reset(fs::is_regular_file(fallback) ? Image::FromFile(fallback.c_str()) : nullptr);
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

bool backup_before_start() {
    const fs::path source = save_directory();
    if (!fs::is_directory(source)) return true;
    const fs::path backup_root = app.root / "out" / "CUSA03219" / "user" / "save-backups";
    const fs::path destination = backup_root / timestamp_name();
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (!ec) fs::copy(source, destination, fs::copy_options::recursive, ec);
    if (ec) {
        const std::string message = ec.message();
        append_log(L"ERROR · automatic backup failed: " +
                   from_utf8(message.c_str(), static_cast<int>(message.size())));
        return false;
    }
    std::vector<fs::path> backups;
    for (const auto& entry : fs::directory_iterator(backup_root, ec)) if (entry.is_directory(ec)) backups.push_back(entry.path());
    std::sort(backups.begin(), backups.end(), std::greater<fs::path>());
    for (size_t i = 10; i < backups.size(); ++i) fs::remove_all(backups[i], ec);
    append_log(L"Automatic backup created: " + destination.filename().wstring());
    return true;
}

void create_shortcut() {
    PWSTR desktop = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) return;
    fs::path shortcut = fs::path(desktop) / L"STAR OCEAN 5 - PS4RT.lnk";
    CoTaskMemFree(desktop);
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(result)) {
        const fs::path launcher = executable_directory() / L"PS4RT-Launcher.exe";
        link->SetPath(launcher.c_str());
        link->SetArguments(L"--play");
        link->SetWorkingDirectory(app.root.c_str());
        link->SetDescription(L"Lancer STAR OCEAN 5 avec PS4RT pour Windows");
        link->SetIconLocation(launcher.c_str(), 0);
        IPersistFile* persist = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist)))) {
            result = persist->Save(shortcut.c_str(), TRUE);
            persist->Release();
        }
        link->Release();
    }
    app.status = SUCCEEDED(result) ? loc(L"Raccourci créé sur le Bureau",L"Desktop shortcut created") : loc(L"Impossible de créer le raccourci",L"Could not create shortcut");
    append_log(SUCCEEDED(result) ? L"Desktop shortcut created" : L"ERROR · could not create desktop shortcut");
}

void backup_selected_save() {
    if (app.selected_save < 0 || app.selected_save >= static_cast<int>(app.saves.size())) return;
    const fs::path source = app.saves[app.selected_save];
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path destination = app.root / "out" / "CUSA03219" / "user" / "save-backups" /
                           (source.filename().wstring() + L"-" + std::to_wstring(stamp));
    fs::path metadata = source.parent_path() / (source.filename().wstring() + L".sce_sys");
    std::error_code ec;
    fs::create_directories(destination / "data", ec);
    if (!ec) fs::copy(source, destination / "data", fs::copy_options::recursive, ec);
    if (!ec && fs::is_directory(metadata)) {
        fs::create_directories(destination / "metadata", ec);
        if (!ec) fs::copy(metadata, destination / "metadata", fs::copy_options::recursive, ec);
    }
    if (ec) {
        const std::string message = ec.message();
        append_log(std::wstring(L"ERROR · save backup failed: ") +
                   from_utf8(message.data(), static_cast<int>(message.size())));
        app.status = loc(L"Échec de la copie de sauvegarde",L"Save backup failed");
    } else {
        append_log(L"Save copied to " + destination.wstring());
        app.status = loc(L"Copie de sauvegarde créée",L"Save backup created");
    }
}

void import_save() {
    std::wstring selected = pick_path(true);
    if (selected.empty()) return;
    fs::path source(selected);
    fs::path root = save_directory();
    // Accept either a raw slot folder or one of our data+metadata backup folders.
    fs::path data_source = fs::is_directory(source / "data") ? source / "data" : source;
    fs::path meta_source = fs::is_directory(source / "metadata") ? source / "metadata" :
                           fs::is_directory(source / "sce_sys") ? source / "sce_sys" : fs::path{};
    std::wstring slot_name = source.filename().wstring();
    if (fs::is_directory(source / "data")) {
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
    return L"off";
}

std::wstring resolution_argument() {
    if (app.resolution.find(L"2560") != std::wstring::npos) return L"1440";
    if (app.resolution.find(L"3840") != std::wstring::npos) return L"4k";
    return L"1080";
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
    if (app.display_mode == L"Fenêtré") return "windowed";
    if (app.display_mode == L"Plein écran") return "fullscreen";
    return "borderless";
}

std::string render_resolution_key() {
    if (app.render_resolution == L"720p") return "720p";
    if (app.render_resolution == L"1440p") return "1440p";
    if (app.render_resolution == L"1800p") return "1800p";
    if (app.render_resolution == L"2160p") return "4K";
    return "native";
}

int language_code() {
    if (app.game_language == L"Français") return 2;
    if (app.game_language == L"Español") return 3;
    if (app.game_language == L"Deutsch") return 4;
    if (app.game_language == L"Italiano") return 5;
    return 1;
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
    const std::vector<std::pair<std::string, std::string>> settings = {
        {"upscaler", narrow_ascii(fsr_argument())},
        {"output_res", app.resolution.find(L"3840") != std::wstring::npos ? "3840x2160" :
                       app.resolution.find(L"2560") != std::wstring::npos ? "2560x1440" : "1920x1080"},
        {"display_mode", display_mode_key()}, {"vsync", app.vsync ? "1" : "0"},
        {"fps_limit", std::to_string(app.fps_limit)}, {"async_shaders", app.async_shaders ? "1" : "0"},
        {"render_resolution", render_resolution_key()}, {"anisotropy", std::to_string(app.anisotropy)}
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
                   loc(L"PS4RT-PkgExtractor.exe et ses outils doivent être présents dans le paquet.",
                       L"PS4RT-PkgExtractor.exe and its tools must be present in the package."),
                   L"OK");
        return;
    }
    std::wstring stem = pkg.stem().wstring();
    for (wchar_t& c : stem) if (wcschr(L"<>:\"/\\|?*", c)) c = L'_';
    const fs::path destination = pkg_import_root() / (stem + L"-" + timestamp_name());
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
    app.status = loc(L"Extraction du PKG en cours…",L"Extracting PKG…");
    append_log(L"PKG extraction started: " + pkg.wstring());
    append_log(L"PKG extraction directory: " + destination.wstring());
    show_modal(loc(L"Extraction du PKG",L"PKG extraction"),
               app.extraction_source + L"\n" + loc(L"Taille : ",L"Size: ") + format_size(app.extraction_size) +
               L"\n" + loc(L"Destination : ",L"Destination: ") + app.extraction_destination,
               loc(L"Annuler l’extraction",L"Cancel extraction"), {}, ModalAction::CancelExtraction);
    SetTimer(app.window, 1, 80, nullptr);

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
        std::wstring command = quote(extractor.wstring()) + L" " + quote(pkg.wstring()) + L" " + quote(destination.wstring());
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
            append_log(L"ERROR · could not start PS4RT-PkgExtractor.exe (code " + std::to_wstring(GetLastError()) + L")");
            {
                std::lock_guard guard(app.mutex);
                app.busy = false; app.pkg_selected = false; app.extraction_active = false;
                app.status = loc(L"Échec de l’extraction du PKG · consultez le journal",L"PKG extraction failed · see diagnostics");
            }
            show_modal(loc(L"Échec de l’extraction",L"Extraction failed"),
                       loc(L"PS4RT-PkgExtractor.exe n’a pas pu démarrer. Consultez le journal pour le code Windows.",
                           L"PS4RT-PkgExtractor.exe could not start. See the log for the Windows error code."), L"OK");
            return;
        }
        if (job) { AssignProcessToJobObject(job, process.hProcess); std::lock_guard guard(app.mutex); app.job = job; }
        if (app.extraction_cancel_requested && job) TerminateJobObject(job, 0);
        CloseHandle(process.hThread);
        char buffer[4096]; DWORD got = 0;
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got) {
            append_log(from_utf8(buffer, static_cast<int>(got)));
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1; GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hProcess); CloseHandle(read_pipe);
        const bool canceled = app.extraction_cancel_requested;
        const fs::path game = !canceled && code == 0 ? find_extracted_game(destination) : fs::path{};
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
            fs::remove_all(destination, cleanup_error);
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
        PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
    }).detach();
}

void run_game(bool prepare_only) {
    if (app.busy) return;
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
    write_runtime_settings();
    const bool skip_backup = app.skip_backup_once;
    app.skip_backup_once = false;
    if (!prepare_only && !skip_backup && app.backup_before_launch && !backup_before_start()) {
        app.status = loc(L"Backup impossible · confirmation requise",L"Backup failed · confirmation required");
        show_modal(loc(L"Échec du backup",L"Backup failed"),
                   loc(L"La sauvegarde de sécurité a échoué. Voulez-vous lancer le jeu malgré tout ?",L"The safety backup failed. Start the game anyway?"),
                   loc(L"Lancer quand même",L"Launch anyway"), loc(L"Annuler",L"Cancel"), ModalAction::LaunchWithoutBackup);
        return;
    }
    app.busy = true;
    app.status = prepare_only ? loc(L"Préparation en cours…",L"Preparing…") : loc(L"Démarrage du jeu…",L"Starting game…");
    append_log(prepare_only ? L"Preparing " + app.detected_title_id + L"…" : L"Starting STAR OCEAN 5 (" + app.detected_title_id + L")…");
    InvalidateRect(app.window, nullptr, FALSE);

    std::wstring game = app.game_path;
    std::wstring fsr = fsr_argument();
    std::wstring resolution = resolution_argument();
    std::wstring save_root = app.save_root;
    const int language = language_code();
    fs::path root = app.root;
    std::wstring keyboard_profile = app.keyboard_profile;
    std::wstring keyboard_buttons = keyboard_button_map();
    std::wstring keyboard_axes = keyboard_axis_map();
    std::wstring controller_profile = app.controller;
    std::wstring title_id = app.detected_title_id;
    std::thread([prepare_only, game, fsr, resolution, save_root, language, keyboard_profile, keyboard_buttons, keyboard_axes, controller_profile, title_id, root] {
        DWORD code = 1;
        const fs::path output = root / "out" / "CUSA03219";
        try {
            ps4rt::prepare_title(fs::path(game), output, [](const std::wstring& line) {
                append_log(line); PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
            });
            std::ofstream marker(output / "prepared-title-id.txt", std::ios::trunc);
            marker << narrow_ascii(title_id) << '\n';
            if (prepare_only) code = 0;
        } catch (const std::exception& error) {
            code = 3;
            append_log(std::wstring(L"ERROR · native preparation: ") + from_utf8(error.what(), static_cast<int>(std::strlen(error.what()))));
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
        }
        if (prepare_only || code != 1) {
            std::lock_guard guard(app.mutex);
            app.busy = false; app.prepared = code == 0;
            app.status = code == 0 ? loc(L"Préparation native terminée",L"Native preparation complete") : loc(L"Échec de la préparation native",L"Native preparation failed");
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
            return;
        }
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read_pipe = nullptr, write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            append_log(L"ERROR · could not create diagnostics pipe");
            std::lock_guard guard(app.mutex); app.busy = false;
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0); return;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
        const fs::path runtime = fs::is_regular_file(root / "ps4rt.exe") ? root / "ps4rt.exe" : root / "out" / "ps4rt.exe";
        const fs::path user = output / "user";
        std::error_code directory_error;
        fs::create_directories(user / "gpu", directory_error);
        if (!fs::is_regular_file(runtime)) {
            CloseHandle(read_pipe); CloseHandle(write_pipe);
            append_log(L"ERROR · ps4rt.exe must be next to PS4RT-Launcher.exe");
            std::lock_guard guard(app.mutex); app.busy = false; app.status = loc(L"Runtime Windows absent",L"Windows runtime missing");
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0); return;
        }
        std::wstring command = quote(runtime.wstring()) + L" " + quote((output / "boot-linked.bin").wstring()) +
            L" --app0 " + quote(game) + L" --user " + quote(user.wstring()) + L" --content-profile " +
            quote((output / "content.bin").wstring()) + L" --timeout 0";
        struct SavedEnv { std::wstring key, old; bool existed{}; };
        std::vector<SavedEnv> saved;
        auto set_env = [&](const wchar_t* key, const std::wstring& value) {
            DWORD needed = GetEnvironmentVariableW(key, nullptr, 0); SavedEnv item{key, {}, needed != 0};
            if (needed) { item.old.resize(needed - 1); GetEnvironmentVariableW(key, item.old.data(), needed); }
            saved.push_back(std::move(item)); SetEnvironmentVariableW(key, value.c_str());
        };
        set_env(L"PS4RT_BC6_FLOAT", L"unsigned"); set_env(L"PS4RT_TRANSITION_SNAPSHOT", L"1");
        set_env(L"PS4RT_OBJECT_MOTION", L"0"); set_env(L"PS4RT_FSR1", L"0");
        set_env(L"PS4RT_PARTIAL_HEIGHT_VIEWS", L"0"); set_env(L"PS4RT_UPSCALER", fsr);
        set_env(L"PS4RT_OUTPUT_RES", resolution == L"4k" ? L"3840x2160" : resolution == L"1440" ? L"2560x1440" : L"1920x1080");
        set_env(L"PS4RT_GPU_USER_DIR", (user / "gpu").wstring());
        set_env(L"PS4RT_CONFIG", (output / "ps4rt.ini").wstring());
        set_env(L"PS4RT_LANGUAGE", std::to_wstring(language));
        set_env(L"PS4RT_KEY_PROFILE", keyboard_profile == L"AZERTY" ? L"azerty" : L"qwerty");
        set_env(L"PS4RT_KEY_MAP", keyboard_buttons);
        set_env(L"PS4RT_KEY_AXES", keyboard_axes);
        set_env(L"PS4RT_PAD_PROFILE", controller_profile);
        if (!save_root.empty()) set_env(L"PS4RT_SAVEDATA_TITLE_DIR", save_root);
        const fs::path overlay = root / "gpu" / "assets" / "star_ocean_overlay.png";
        if (fs::is_regular_file(overlay)) set_env(L"PS4RT_OVERLAY_BACKGROUND", overlay.wstring());
        const fs::path sounds = root / "gpu" / "assets" / "sounds";
        if (fs::is_directory(sounds)) set_env(L"PS4RT_MENU_SOUND_DIR", sounds.wstring());
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
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0); return;
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
        char buffer[4096]; DWORD got{};
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &got, nullptr) && got) {
            append_log(from_utf8(buffer, static_cast<int>(got)));
            PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
        }
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
        PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
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
    GraphicsPath path;
    REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    g.FillPath(&brush, &path);
}

void text(Graphics& g, const std::wstring& value, REAL x, REAL y, REAL width, REAL height,
          REAL size, Color color, FontStyle style = FontStyleRegular,
          StringAlignment alignment = StringAlignmentNear) {
    FontFamily family(font_for(value));
    const REAL readable_size = std::max<REAL>(size, 14.0f);
    Font font(&family, readable_size * 1.18f, style, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentCenter);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(value.c_str(), -1, &font, RectF(x, y, width, height), &format, &brush);
}

void paragraph(Graphics& g, const std::wstring& value, REAL x, REAL y, REAL width, REAL height,
               REAL size, Color color) {
    FontFamily family(font_for(value));
    const REAL readable_size = std::max<REAL>(size, 14.0f);
    Font font(&family, readable_size * 1.18f, FontStyleRegular, UnitPixel);
    SolidBrush brush(color);
    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord);
    g.DrawString(value.c_str(), -1, &font, RectF(x, y, width, height), &format, &brush);
}

void hit(int id, REAL x, REAL y, REAL width, REAL height) {
    if (app.clipping_content_hits && y + height <= 82) return;
    const REAL clipped_y = app.clipping_content_hits ? std::max<REAL>(y, 82) : y;
    app.hits.push_back({id, RECT{static_cast<LONG>(x), static_cast<LONG>(clipped_y),
                                 static_cast<LONG>(x + width), static_cast<LONG>(y + height)}});
}

void button(Graphics& g, int id, const std::wstring& label, REAL x, REAL y, REAL width, REAL height,
            bool primary = false, bool enabled = true) {
    const bool hot = app.hover == id;
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

void image_fit(Graphics& g, Image* image, REAL x, REAL y, REAL width, REAL height) {
    if (!image || image->GetLastStatus() != Ok) return;
    const REAL scale = std::min(width / image->GetWidth(), height / image->GetHeight());
    const REAL draw_width = image->GetWidth() * scale, draw_height = image->GetHeight() * scale;
    g.DrawImage(image, x + (width - draw_width) / 2, y + (height - draw_height) / 2, draw_width, draw_height);
}

void image_cover(Graphics& g, Image* image, REAL x, REAL y, REAL width, REAL height) {
    if (!image || image->GetLastStatus() != Ok) return;
    GraphicsState state = g.Save();
    g.SetClip(RectF(x, y, width, height));
    const REAL scale = std::max(width / image->GetWidth(), height / image->GetHeight());
    const REAL draw_width = image->GetWidth() * scale, draw_height = image->GetHeight() * scale;
    g.DrawImage(image, x + (width - draw_width) / 2, y + (height - draw_height) / 2, draw_width, draw_height);
    g.Restore(state);
}

void page_title(Graphics& g, const wchar_t* overline, const wchar_t* title, REAL x, REAL y, REAL width) {
    text(g, overline, x, y, width, 20, 10, Color(255, 82, 207, 244), FontStyleBold);
    text(g, title, x, y + 19, width, 42, 28, Color(255, 244, 249, 252), FontStyleRegular);
}

void setting_row(Graphics& g, int id, const std::wstring& label, const std::wstring& value,
                 REAL x, REAL y, REAL width, bool enabled = true) {
    SolidBrush hover(app.hover == id ? Color(150, 30, 67, 91) : Color(80, 18, 45, 64));
    rounded(g, RectF(x, y, width, 54), 10, hover);
    text(g, label, x + 15, y + 2, width * .48f, 50, 14, enabled ? Color(255, 179, 203, 216) : Color(255, 92, 108, 118));
    text(g, value, x + width * .47f, y + 2, width * .47f - 29, 50, 14,
         enabled ? Color(255, 233, 242, 247) : Color(255, 105, 121, 131), FontStyleBold, StringAlignmentFar);
    text(g, L"⌄", x + width - 25, y, 18, 54, 17, enabled ? Color(255, 82, 207, 244) : Color(255, 80, 92, 100), FontStyleBold, StringAlignmentCenter);
    if (enabled) hit(id, x, y, width, 54);
}

void open_selector(int setting, std::initializer_list<const wchar_t*> values) {
    app.popup_hit = setting;
    app.popup_values.clear();
    for (const auto* value : values) app.popup_values.emplace_back(value);
}

void open_selector(int setting, const std::vector<std::wstring>& values) {
    app.popup_hit = setting;
    app.popup_values = values;
}

void draw_selector(Graphics& g, REAL width, REAL height) {
    if (app.popup_hit == HitNone || app.popup_values.empty()) return;
    hit(1999, 0, 78, width, height - 78);
    SolidBrush veil(Color(155, 0, 5, 12));
    g.FillRectangle(&veil, 0.0f, 78.0f, width, height - 78.0f);
    const REAL panel_width = std::min<REAL>(590, width - 100);
    const REAL row_height = 48;
    const REAL panel_height = 74 + row_height * app.popup_values.size();
    const REAL x = width - panel_width - 42;
    const REAL y = std::max<REAL>(96, (height - panel_height) / 2);
    SolidBrush panel(Color(250, 5, 22, 36));
    rounded(g, RectF(x, y, panel_width, panel_height), 18, panel);
    text(g, loc(L"CHOISIR UNE OPTION",L"CHOOSE AN OPTION"), x + 22, y + 12, panel_width - 44, 42, 13,
         Color(255, 82, 207, 244), FontStyleBold);
    SolidBrush divider(Color(65, 82, 207, 244));
    for (size_t i = 0; i < app.popup_values.size(); ++i) {
        const REAL row_y = y + 62 + i * row_height;
        const bool hot = app.hover == 2000 + static_cast<int>(i);
        if (hot) { SolidBrush selected(Color(160, 24, 78, 105)); rounded(g, RectF(x + 12, row_y, panel_width - 24, row_height - 3), 9, selected); }
        text(g, app.popup_values[i], x + 25, row_y, panel_width - 70, row_height - 3, 15,
             Color(255, 232, 242, 247), hot ? FontStyleBold : FontStyleRegular);
        text(g, L"›", x + panel_width - 48, row_y, 22, row_height - 3, 18, Color(255, 82, 207, 244), FontStyleBold, StringAlignmentCenter);
        g.FillRectangle(&divider, RectF(x + 22, row_y + row_height - 2, panel_width - 44, 1));
        hit(2000 + static_cast<int>(i), x + 12, row_y, panel_width - 24, row_height - 3);
    }
}

void draw_modal(Graphics& g, REAL width, REAL height) {
    if (!app.modal_visible) return;
    hit(HitModalBackdrop, 0, 0, width, height);
    SolidBrush veil(Color(205, 0, 5, 12));
    g.FillRectangle(&veil, 0.0f, 0.0f, width, height);
    const bool extracting = app.extraction_active && app.modal_action == ModalAction::CancelExtraction;
    const REAL panel_width = std::min<REAL>(760, width - 100);
    const REAL panel_height = extracting ? 370.0f : 300.0f;
    const REAL x = (width - panel_width) / 2, y = (height - panel_height) / 2;
    SolidBrush glow(Color(95, 82, 207, 244));
    rounded(g, RectF(x - 2, y - 2, panel_width + 4, panel_height + 4), 20, glow);
    SolidBrush panel(Color(255, 5, 22, 36));
    rounded(g, RectF(x, y, panel_width, panel_height), 18, panel);
    SolidBrush accent(Color(255, 82, 207, 244));
    g.FillEllipse(&accent, RectF(x + 28, y + 29, 46, 46));
    text(g, L"i", x + 28, y + 29, 46, 46, 24, Color(255, 2, 21, 31), FontStyleBold, StringAlignmentCenter);
    text(g, app.modal_title, x + 94, y + 25, panel_width - 125, 56, 22, Color(255, 244, 249, 252), FontStyleBold);
    paragraph(g, app.modal_body, x + 30, y + 102, panel_width - 60, extracting ? 112.0f : 92.0f, 15, Color(255, 184, 207, 219));
    SolidBrush divider(Color(70, 82, 207, 244));
    if (extracting) {
        const ULONGLONG elapsed_seconds = (GetTickCount64() - app.extraction_started) / 1000;
        wchar_t elapsed[32]{};
        swprintf_s(elapsed, L"%02llu:%02llu", elapsed_seconds / 60, elapsed_seconds % 60);
        text(g, std::wstring(loc(L"Extraction en cours · ",L"Extraction in progress · ")) + elapsed,
             x + 30, y + 218, panel_width - 60, 27, 12, Color(255, 118, 199, 224), FontStyleBold);
        SolidBrush track(Color(255, 17, 49, 67));
        rounded(g, RectF(x + 30, y + 250, panel_width - 60, 12), 6, track);
        const REAL travel = panel_width - 220;
        const REAL position = static_cast<REAL>((GetTickCount64() / 7) % static_cast<ULONGLONG>(std::max<REAL>(1, travel)));
        SolidBrush pulse(Color(255, 82, 207, 244));
        rounded(g, RectF(x + 30 + position, y + 250, 160, 12), 6, pulse);
        g.FillRectangle(&divider, RectF(x + 30, y + 282, panel_width - 60, 1));
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 244, y + 302, 214, 48);
        return;
    }
    g.FillRectangle(&divider, RectF(x + 30, y + 211, panel_width - 60, 1));
    if (!app.modal_tertiary.empty()) {
        const REAL button_width = 194, gap = 12;
        const REAL start = x + panel_width - (button_width * 3 + gap * 2) - 30;
        button(g, HitModalSecondary, app.modal_secondary, start, y + 230, button_width, 48);
        button(g, HitModalTertiary, app.modal_tertiary, start + button_width + gap, y + 230, button_width, 48);
        button(g, HitModalPrimary, app.modal_primary, start + (button_width + gap) * 2, y + 230, button_width, 48, true);
    } else if (app.modal_secondary.empty()) {
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 224, y + 230, 194, 48, true);
    } else {
        button(g, HitModalSecondary, app.modal_secondary, x + panel_width - 434, y + 230, 194, 48);
        button(g, HitModalPrimary, app.modal_primary, x + panel_width - 224, y + 230, 194, 48, true);
    }
}

void toggle_row(Graphics& g, int id, const std::wstring& label, bool value, REAL x, REAL y, REAL width) {
    SolidBrush hover(app.hover == id ? Color(150, 30, 67, 91) : Color(80, 18, 45, 64));
    rounded(g, RectF(x, y, width, 50), 9, hover);
    text(g, label, x + 13, y, width - 80, 50, 12, Color(255, 174, 196, 208));
    SolidBrush track(value ? Color(220, 38, 126, 154) : Color(220, 44, 62, 74));
    rounded(g, RectF(x + width - 57, y + 15, 42, 21), 10, track);
    SolidBrush knob(value ? Color(255, 94, 221, 250) : Color(255, 137, 153, 164));
    g.FillEllipse(&knob, RectF(x + width - (value ? 35.0f : 54.0f), y + 18, 15, 15));
    hit(id, x, y, width, 50);
}

void draw_header(Graphics& g, REAL width) {
    SolidBrush header(Color(242, 2, 12, 22));
    g.FillRectangle(&header, 0.0f, 0.0f, width, 78.0f);
    SolidBrush line(Color(80, 82, 207, 244));
    g.FillRectangle(&line, 0.0f, 77.0f, width, 1.0f);
    image_cover(g, app.launcher_icon.get(), 20, 15, 48, 48);
    text(g, L"PS4RT", 80, 14, 150, 28, 18, Color(255, 244, 249, 252), FontStyleBold);
    text(g, L"WINDOWS", 80, 40, 150, 20, 10, Color(255, 82, 207, 244), FontStyleBold);

    const UiWords w = words();
    struct Nav { Page page; int id; const wchar_t* label; } nav[] = {
        {Page::Home, HitHome, w.home}, {Page::Game, HitGame, w.game},
        {Page::Video, HitVideo, w.video}, {Page::Controls, HitControls, w.controls},
        {Page::Advanced, HitAdvanced, w.advanced}, {Page::Logs, HitLogs, w.logs}
    };
    const REAL nav_x = 238, nav_width = width - nav_x - 92;
    const REAL tab_width = nav_width / 6.0f;
    for (int i = 0; i < 6; ++i) {
        const REAL x = nav_x + i * tab_width;
        const bool selected = app.page == nav[i].page;
        if (selected || app.hover == nav[i].id) {
            SolidBrush selected_brush(selected ? Color(150, 25, 69, 91) : Color(75, 25, 69, 91));
            rounded(g, RectF(x + 3, 16, tab_width - 6, 46), 9, selected_brush);
        }
        text(g, nav[i].label, x + 5, 16, tab_width - 10, 46, 13,
             selected ? Color(255, 244, 249, 252) : Color(255, 142, 165, 179),
             selected ? FontStyleBold : FontStyleRegular, StringAlignmentCenter);
        if (selected) g.FillRectangle(&line, RectF(x + 16, 74, tab_width - 32, 3));
        hit(nav[i].id, x + 2, 13, tab_width - 4, 52);
    }
    text(g, L"—", width - 85, 15, 36, 38, 17, Color(255, 205, 218, 226), FontStyleRegular, StringAlignmentCenter);
    text(g, L"×", width - 45, 15, 36, 38, 23, Color(255, 205, 218, 226), FontStyleRegular, StringAlignmentCenter);
    hit(HitMinimize, width - 85, 15, 36, 38);
    hit(HitClose, width - 45, 15, 36, 38);
}

void draw_home(Graphics& g, REAL width, REAL height) {
    const REAL logo_width = std::min<REAL>(600, width * .50f);
    image_fit(g, app.game_logo.get(), (width - logo_width) / 2, 102, logo_width, 310);
    const bool valid = !app.pkg_selected && app.game_verified;
    const UiWords w = words();
    const std::wstring play_label = app.busy ? L"■   STOP" : valid ? std::wstring(L"▶   ") + w.play : w.select_game;
    const REAL button_width = std::min<REAL>(390, width * .34f);
    button(g, app.busy ? HitStop : HitPlay, play_label, (width - button_width) / 2,
           std::max<REAL>(430, height - 255), button_width, 78, true);

    const REAL dock_x = 28, dock_y = height - 105, dock_width = width - 56, item_width = dock_width / 5.0f;
    card(g, dock_x, dock_y, dock_width, 78, 238);
    const std::array<std::pair<std::wstring, std::wstring>, 5> items = {{
        {loc(L"ÉTAT",L"STATUS"), app.prepared ? loc(L"Jeu prêt",L"Game ready") : loc(L"Préparation requise",L"Preparation required")},
        {loc(L"AFFICHAGE",L"DISPLAY"), app.resolution + L" · " + display_mode_label()},
        {loc(L"RECONSTRUCTION",L"UPSCALING"), fsr_label()},
        {loc(L"SAUVEGARDE",L"SAVES"), app.backup_before_launch ? loc(L"Backup automatique",L"Automatic backup") : loc(L"Backup désactivé",L"Backup disabled")},
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

    card(g, x, top + 164, left_width, 145);
    text(g, loc(L"DOSSIER DES SAUVEGARDES",L"SAVE DIRECTORY"), x + 18, top + 176, left_width - 36, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    const std::wstring save_path = app.save_root.empty() ? loc(L"Emplacement PS4RT par défaut",L"Default PS4RT location") : app.save_root;
    text(g, save_path, x + 18, top + 204, left_width - 36, 28, 12, Color(255, 226, 238, 244));
    button(g, HitSaveFolder, loc(L"Modifier",L"Change"), x + 18, top + 248, 122, 42);
    button(g, HitDefaultSaveFolder, loc(L"Par défaut",L"Default"), x + 151, top + 248, 122, 42, false, !app.save_root.empty());
    button(g, HitOpenSaves, loc(L"Ouvrir",L"Open"), x + 284, top + 248, 112, 42);
    toggle_row(g, HitBackupToggle, loc(L"Backup avant chaque lancement",L"Backup before every launch"), app.backup_before_launch,
               x + 410, top + 244, left_width - 428);

    card(g, x, top + 323, left_width, std::max<REAL>(145, height - 393));
    text(g, loc(L"INFORMATIONS DÉTECTÉES",L"DETECTED INFORMATION"), x + 18, top + 335, left_width - 36, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    const REAL info_y = top + 368;
    const std::wstring empty_value;
    const bool identified = app.game_identity != GameIdentity::None;
    const bool star_ocean = app.game_identity == GameIdentity::Tested || app.game_identity == GameIdentity::Untested;
    const std::array<std::pair<std::wstring, std::wstring>, 5> info = {{
        {loc(L"Titre",L"Title"), identified ? (star_ocean ? L"STAR OCEAN: Integrity and Faithlessness" : loc(L"Titre PS4 non pris en charge",L"Unsupported PS4 title")) : empty_value},
        {L"ID", identified ? app.detected_title_id : empty_value},
        {L"Version", star_ocean ? L"01.01" : empty_value},
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
           x + 18, top + 510, 190, 38, false,
           !app.busy && !app.pkg_selected && app.game_verified);

    card(g, right_x, top, right_width, 500);
    text(g, loc(L"DERNIÈRE SAUVEGARDE",L"LATEST SAVE"), right_x + 16, top + 11, right_width - 32, 24, 10, Color(255, 113, 147, 165), FontStyleBold);
    image_cover(g, app.save_preview.get(), right_x + 16, top + 42, right_width - 32, 305);
    text(g, app.save_title, right_x + 16, top + 354, right_width - 32, 30, 15, Color(255, 239, 246, 249), FontStyleBold);
    text(g, app.save_subtitle.empty() ? app.save_date : app.save_subtitle, right_x + 16, top + 384, right_width - 32, 25, 12, Color(255, 118, 151, 169));
    paragraph(g, app.save_detail, right_x + 16, top + 412, right_width - 32, 40, 12, Color(255, 169, 190, 201));
    button(g, HitBackupSave, loc(L"Créer une copie",L"Create backup"), right_x + 16, top + 458, right_width * .47f, 34, false, app.selected_save >= 0);
    button(g, HitOpenSaves, loc(L"Ouvrir le dossier",L"Open folder"), right_x + right_width * .52f, top + 458, right_width * .43f, 34);

    card(g, right_x, top + 514, right_width, std::max<REAL>(176, height - 584));
    setting_row(g, HitGameLanguage, loc(L"Langue du jeu",L"Game language"), app.game_language, right_x + 12, top + 527, right_width - 24);
    button(g, HitShortcut, loc(L"Créer un raccourci Bureau",L"Create desktop shortcut"), right_x + 12, top + 587, right_width - 24, 42);
    button(g, HitImportSave, loc(L"Importer une sauvegarde",L"Import a save"), right_x + 12, top + 638, right_width - 24, 42);
}

void draw_video(Graphics& g, REAL x, REAL y, REAL width, REAL) {
    page_title(g, loc(L"AFFICHAGE ET QUALITÉ",L"DISPLAY AND QUALITY"), loc(L"Vidéo",L"Video"), x, y, width);
    const REAL top = y + 70, gap = 18, column = (width - gap) / 2.0f;
    card(g, x, top, column, 410);
    card(g, x + column + gap, top, column, 410);
    text(g, loc(L"AFFICHAGE",L"DISPLAY"), x + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitDisplayMode, loc(L"Mode",L"Display mode"), display_mode_label(), x + 12, top + 47, column - 24);
    toggle_row(g, HitVsync, loc(L"Synchronisation verticale",L"Vertical synchronization"), app.vsync, x + 12, top + 105, column - 24);
    setting_row(g, HitFpsLimit, loc(L"Limite d’IPS",L"Frame-rate limit"), app.fps_limit ? std::to_wstring(app.fps_limit) + L" FPS" : loc(L"Illimitée",L"Unlimited"), x + 12, top + 163, column - 24);
    setting_row(g, HitResolution, loc(L"Résolution de sortie",L"Output resolution"), app.resolution, x + 12, top + 221, column - 24);
    paragraph(g, loc(L"L’écran et sa fréquence sont sélectionnés automatiquement par SDL3. Le plein écran utilise les modes exposés par Windows.",
                     L"SDL3 selects the monitor and refresh rate automatically. Fullscreen uses the modes exposed by Windows."), x + 18, top + 294, column - 36, 80, 12, Color(255, 128, 154, 168));

    const REAL middle = x + column + gap;
    text(g, loc(L"RENDU",L"RENDERING"), middle + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    setting_row(g, HitFsr, loc(L"Reconstruction",L"Upscaling"), fsr_label(), middle + 12, top + 47, column - 24);
    setting_row(g, HitRenderResolution, loc(L"Résolution de rendu",L"Render resolution"), app.render_resolution, middle + 12, top + 105, column - 24);
    setting_row(g, HitAnisotropy, loc(L"Filtrage anisotrope",L"Anisotropic filtering"), app.anisotropy ? L"×" + std::to_wstring(app.anisotropy) : loc(L"Réglage du jeu",L"Game setting"), middle + 12, top + 163, column - 24);
    toggle_row(g, HitAsyncShaders, loc(L"Shaders asynchrones",L"Asynchronous shaders"), app.async_shaders, middle + 12, top + 221, column - 24);
    paragraph(g, app.async_shaders ? loc(L"Gameplay plus fluide et moins de saccades de compilation. De brefs scintillements visuels peuvent apparaître.",
                                         L"Smoother gameplay with fewer shader compilation stutters. Brief visual flicker may occasionally appear.")
                                   : loc(L"Compilation synchrone : moins d’artefacts temporaires, mais des saccades peuvent survenir lors du premier passage.",
                                         L"Synchronous compilation: fewer temporary artifacts, but first-time shader compilation may stutter."),
              middle + 16, top + 294, column - 32, 85, 10, Color(255, 128, 154, 168));

    const REAL lower = top + 428;
    card(g, x, lower, width, 225);
    text(g, loc(L"COMPATIBILITÉ SHADPS4",L"SHADPS4 COMPATIBILITY"), x + 18, lower + 12, width - 36, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    const REAL chip_width = (width - 86) / 4.0f;
    chip(g, L"Vulkan 1.3", app.vulkan_ok, x + 16, lower + 50, chip_width);
    chip(g, loc(L"FSR 3.1 intégré",L"Integrated FSR 3.1"), true, x + 34 + chip_width, lower + 50, chip_width);
    chip(g, loc(L"FSR 4 avec repli sûr",L"FSR 4 with safe fallback"), true, x + 52 + chip_width * 2, lower + 50, chip_width);
    chip(g, loc(L"Cache de shaders",L"Shader cache"), fs::is_directory(cache_directory()), x + 70 + chip_width * 3, lower + 50, chip_width);
    paragraph(g, loc(L"FSR 4 est vérifié par le runtime. Si le GPU ou les shaders fournis ne sont pas compatibles, PS4RT revient automatiquement à FSR 3.1 avant la première image.",
                     L"FSR 4 is validated by the runtime. If the GPU or supplied shaders are incompatible, PS4RT safely falls back to FSR 3.1 before the first frame."),
              x + 18, lower + 108, width * .63f, 88, 12, Color(255, 147, 174, 188));
    paragraph(g, loc(L"Les options non encore validées pour ce jeu ne sont pas exposées comme fonctionnelles.",
                     L"Options that have not been validated for this game are not presented as functional."),
              x + width * .67f, lower + 108, width * .30f, 88, 12, Color(255, 96, 134, 155));
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
    button(g, HitOpenRoot, loc(L"Ouvrir le dossier PS4RT",L"Open PS4RT folder"), x + 14, top + 174, column - 28, 37);

    const REAL right = x + column + gap;
    card(g, right, top, column, 225);
    text(g, loc(L"MISES À JOUR",L"UPDATES"), right + 16, top + 12, column - 32, 24, 10, Color(255, 82, 207, 244), FontStyleBold);
    toggle_row(g, HitUpdateToggle, loc(L"Rechercher au démarrage",L"Check on startup"), app.update_on_start, right + 12, top + 48, column - 24);
    paragraph(g, loc(L"La vérification interroge uniquement les paquets Windows publiés. Aucun téléchargement ni aucune installation ne sont automatiques.",
                     L"The check only queries published Windows packages. Downloads and installation are never automatic."), right + 17, top + 112, column - 34, 55, 10, Color(255, 135, 161, 175));
    button(g, HitCheckUpdate, loc(L"Vérifier maintenant",L"Check now"), right + 14, top + 174, column - 28, 37);

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
    chip(g, L"Runtime Windows x64", fs::exists(runtime_path()), right + 15, top + 390, column - 30);
    button(g, HitRefresh, loc(L"Relancer les vérifications",L"Run checks again"), right + 14, top + 439, column - 28, 37);
}

void draw_logs(Graphics& g, REAL x, REAL y, REAL width, REAL height) {
    page_title(g, loc(L"SUPPORT ET ANALYSE",L"SUPPORT AND ANALYSIS"), loc(L"Journal et diagnostic",L"Logs and diagnostics"), x, y, width);
    const REAL top = y + 70;
    chip(g, loc(L"Préparateur natif",L"Native preparer"), app.native_preparer_ok, x, top, 190);
    chip(g, L"Vulkan 1.3", app.vulkan_ok, x + 202, top, 160);
    if (app.game_identity != GameIdentity::None) {
        const Indicator indicator = app.game_identity == GameIdentity::Tested ? Indicator::Green :
                                    app.game_identity == GameIdentity::Untested ? Indicator::Orange : Indicator::Red;
        chip(g, app.detected_title_id, indicator, x + 374, top, 170);
    }
    button(g, HitExportLogs, loc(L"Exporter le rapport",L"Export report"), x + width - 350, top, 165, 38);
    button(g, HitClearLogs, loc(L"Effacer l’affichage",L"Clear display"), x + width - 174, top, 174, 38);
    SolidBrush terminal(Color(242, 2, 10, 16));
    rounded(g, RectF(x, top + 55, width, height - 125), 15, terminal);
    text(g, app.busy ? loc(L"●  PROCESSUS ACTIF",L"●  PROCESS ACTIVE") : loc(L"●  PRÊT",L"●  READY"), x + 18, top + 64, width - 36, 28, 10,
         app.busy ? Color(255, 65, 226, 170) : Color(255, 112, 145, 162), FontStyleBold);
    std::vector<std::wstring> lines;
    { std::lock_guard guard(app.mutex); lines = app.logs; }
    int max_lines = std::max(4, static_cast<int>((height - 180) / 19));
    size_t first = lines.size() > static_cast<size_t>(max_lines) ? lines.size() - max_lines : 0;
    REAL line_y = top + 98;
    for (size_t i = first; i < lines.size(); ++i) {
        const bool error = lines[i].find(L"ERROR") != std::wstring::npos || lines[i].find(L"STOP:") != std::wstring::npos;
        text(g, lines[i], x + 18, line_y, width - 36, 19, 11,
             error ? Color(255, 247, 119, 119) : Color(255, 158, 204, 193));
        line_y += 19;
    }
}

void paint(HWND window) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    RECT client{}; GetClientRect(window, &client);
    const int width = client.right, height = client.bottom;
    Bitmap canvas(width, height, PixelFormat32bppPARGB);
    Graphics g(&canvas);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    image_cover(g, app.background.get(), 0, 0, static_cast<REAL>(width), static_cast<REAL>(height));
    LinearGradientBrush shade(RectF(0, 0, static_cast<REAL>(width), static_cast<REAL>(height)),
                              Color(80, 1, 8, 17), Color(178, 2, 10, 20), LinearGradientModeVertical);
    g.FillRectangle(&shade, 0, 0, width, height);
    if (app.page != Page::Home) {
        SolidBrush content_shade(Color(150, 1, 10, 20));
        g.FillRectangle(&content_shade, 0, 78, width, height - 78);
    }
    app.hits.clear();
    draw_header(g, static_cast<REAL>(width));
    const REAL x = 42, y = 104, content_width = width - 84, content_height = height - y - 34;
    const int page_index = static_cast<int>(app.page);
    const int scroll = app.page == Page::Home ? 0 : app.scroll[page_index];
    const REAL page_y = y - static_cast<REAL>(scroll);
    const GraphicsState content_state = g.Save();
    g.SetClip(RectF(0, 79, static_cast<REAL>(width), static_cast<REAL>(height - 79)));
    app.clipping_content_hits = true;
    switch (app.page) {
    case Page::Home: draw_home(g, static_cast<REAL>(width), static_cast<REAL>(height)); break;
    case Page::Game: draw_game(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Video: draw_video(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Controls: draw_controls(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Advanced: draw_advanced(g, x, page_y, content_width, content_height + scroll); break;
    case Page::Logs: draw_logs(g, x, page_y, content_width, content_height + scroll); break;
    }
    app.clipping_content_hits = false;
    g.Restore(content_state);
    const int required = app.page == Page::Controls ? 1200 : app.page == Page::Game ? 900 :
                         app.page == Page::Advanced ? 690 : app.page == Page::Video ? 900 : 0;
    const int viewport = height - 106;
    if (required > viewport && app.page != Page::Home) {
        const REAL track_x = width - 15.0f, track_y = 94.0f, track_height = height - 132.0f;
        SolidBrush track(Color(100, 20, 46, 62)); rounded(g, RectF(track_x, track_y, 6, track_height), 3, track);
        const REAL thumb_height = std::max<REAL>(48, track_height * viewport / required);
        const REAL thumb_y = track_y + (track_height - thumb_height) * scroll / std::max(1, required - viewport);
        SolidBrush thumb(Color(230, 82, 207, 244)); rounded(g, RectF(track_x, thumb_y, 6, thumb_height), 3, thumb);
    }
    draw_selector(g, static_cast<REAL>(width), static_cast<REAL>(height));
    draw_modal(g, static_cast<REAL>(width), static_cast<REAL>(height));
    text(g, std::wstring(L"Version ") + kVersion, width - 130, height - 25, 110, 18, 9,
         Color(255, 101, 127, 142), FontStyleRegular, StringAlignmentFar);
    Graphics target(dc);
    target.DrawImage(&canvas, 0, 0);
    EndPaint(window, &ps);
}

int hit_at(POINT point) {
    for (auto it = app.hits.rbegin(); it != app.hits.rend(); ++it)
        if (PtInRect(&it->second, point)) return it->first;
    return HitNone;
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
    { std::lock_guard guard(app.mutex); for (const auto& line : app.logs) fwprintf(file, L"%ls\n", line.c_str()); }
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

void check_for_updates(bool interactive) {
    std::wstring result = std::wstring(loc(L"Version Windows installée : ",L"Installed Windows version: ")) + kVersion +
                          loc(L" · aucune version Windows plus récente détectée",L" · no newer Windows version detected");
    std::wstring english_result = std::wstring(L"Installed Windows version: ") + kVersion + L" · no newer Windows version detected";
    HINTERNET session = WinHttpOpen(L"PS4RT-Windows/0.3.5", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
        HINTERNET connection = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connection) {
            HINTERNET request = WinHttpOpenRequest(connection, L"GET",
                L"/repos/HekamiAeon/Star-Ocean-Integrity-and-Faithlessness-PS4RT/releases?per_page=20",
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (request) {
                const wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
                if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
                    std::string body;
                    DWORD available = 0;
                    while (WinHttpQueryDataAvailable(request, &available) && available) {
                        const size_t old_size = body.size();
                        body.resize(old_size + available);
                        DWORD received = 0;
                        if (!WinHttpReadData(request, body.data() + old_size, available, &received)) break;
                        body.resize(old_size + received);
                    }
                    // Upstream Linux tags are unrelated to this Windows port. Only a
                    // published Windows ZIP asset is allowed to advertise an update.
                    const std::string marker = "ps4rt-star-ocean-5-windows-x64-v";
                    size_t position = body.find(marker);
                    if (position != std::string::npos) {
                        position += marker.size();
                        size_t end = position;
                        while (end < body.size() && ((body[end] >= '0' && body[end] <= '9') || body[end] == '.')) ++end;
                        const std::wstring available = from_utf8(body.data() + position, static_cast<int>(end - position));
                        if (!available.empty() && available != kVersion) {
                            result = std::wstring(loc(L"Mise à jour Windows disponible : ",L"Windows update available: ")) + available +
                                     loc(L" · installée : ",L" · installed: ") + kVersion;
                            english_result = L"Windows update available: " + available + L" · installed: " + kVersion;
                        }
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connection);
        }
        WinHttpCloseHandle(session);
    }
    append_log(english_result);
    if (interactive) show_modal(loc(L"Mises à jour PS4RT",L"PS4RT updates"), result, L"OK");
    PostMessageW(app.window, WM_LOG_UPDATED, 0, 0);
}

void apply_selector(int index) {
    if (index < 0 || index >= static_cast<int>(app.popup_values.size())) return;
    const std::wstring value = app.popup_values[index];
    switch (app.popup_hit) {
    case HitDisplayMode: app.display_mode = value; write_runtime_settings(); break;
    case HitFpsLimit: app.fps_limit = (value == L"Illimitée" || value == L"Unlimited") ? 0 : _wtoi(value.c_str()); write_runtime_settings(); break;
    case HitFsr: app.fsr = value; write_runtime_settings(); break;
    case HitResolution: app.resolution = value; write_runtime_settings(); break;
    case HitRenderResolution: app.render_resolution = value; write_runtime_settings(); break;
    case HitAnisotropy: app.anisotropy = (value == L"Réglage du jeu" || value == L"Game setting") ? 0 : _wtoi(value.c_str() + 1); write_runtime_settings(); break;
    case HitGameLanguage: app.game_language = value; break;
    case HitController: app.controller = value; break;
    case HitKeyboardProfile:
        app.keyboard_profile = (value == L"Personnalisé" || value == L"Custom") ? L"Custom" : value;
        if (value == L"AZERTY" || value == L"QWERTY") apply_keyboard_defaults(value == L"AZERTY");
        break;
    case HitLauncherLanguage: app.launcher_language = value; update_game_status(); refresh_saves(); refresh_controllers(); break;
    default: break;
    }
    app.popup_hit = HitNone; app.popup_values.clear(); save_settings();
}

void activate(int id) {
    if (app.modal_visible) {
        if (id == HitModalBackdrop) return;
        if (id == HitModalTertiary && app.modal_action == ModalAction::ConfirmExtraction) {
            const fs::path pkg = app.pending_pkg;
            app.modal_visible = false; app.modal_action = ModalAction::None;
            const std::wstring selected = pick_path(true, loc(L"Choisir le dossier d’extraction",L"Choose the extraction folder"));
            if (!selected.empty()) { app.pkg_extract_root = selected; save_settings(); }
            confirm_pkg_extraction(pkg);
            return;
        }
        if (id == HitModalSecondary) {
            if (app.modal_action == ModalAction::LaunchWithoutBackup)
                app.status = loc(L"Lancement annulé · backup impossible",L"Launch cancelled · backup failed");
            if (app.modal_action == ModalAction::ConfirmExtraction) app.pending_pkg.clear();
            app.modal_visible = false; app.modal_action = ModalAction::None; return;
        }
        if (id != HitModalPrimary) return;
        const ModalAction action = app.modal_action;
        app.modal_visible = false; app.modal_action = ModalAction::None;
        if (action == ModalAction::ClearCache) clear_cache_confirmed();
        else if (action == ModalAction::LaunchWithoutBackup) { app.skip_backup_once = true; run_game(false); }
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
        else if (action == ModalAction::CloseBusy) {
            if (app.job) TerminateJobObject(app.job, 0);
            DestroyWindow(app.window);
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
    if (id >= 2000) { apply_selector(id - 2000); return; }
    if (id == 1999) { app.popup_hit = HitNone; app.popup_values.clear(); return; }
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
    case HitGame: app.page = Page::Game; refresh_saves(); break;
    case HitVideo: app.page = Page::Video; break;
    case HitControls: app.page = Page::Controls; refresh_controllers(); break;
    case HitAdvanced: app.page = Page::Advanced; break;
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
    case HitDisplayMode:
        open_selector(HitDisplayMode, {loc(L"Fenêtré",L"Windowed"),loc(L"Plein écran",L"Fullscreen"),loc(L"Sans bordure",L"Borderless")}); break;
    case HitVsync: app.vsync = !app.vsync; save_settings(); write_runtime_settings(); break;
    case HitFpsLimit:
        open_selector(HitFpsLimit, {L"30 FPS",L"60 FPS",L"120 FPS",L"144 FPS",L"165 FPS",L"240 FPS",loc(L"Illimitée",L"Unlimited")}); break;
    case HitFsr:
        open_selector(HitFsr, {loc(L"Désactivé",L"Disabled"),L"FSR 3.1",L"FSR 4"}); break;
    case HitResolution:
        open_selector(HitResolution, {L"1280 × 720",L"1920 × 1080",L"2560 × 1440",L"3840 × 2160"}); break;
    case HitRenderResolution:
        open_selector(HitRenderResolution, {L"Native 1080p",L"720p",L"1080p",L"1440p",L"1800p",L"2160p"}); break;
    case HitAnisotropy:
        open_selector(HitAnisotropy, {loc(L"Réglage du jeu",L"Game setting"),L"×2",L"×4",L"×8",L"×16"}); break;
    case HitAsyncShaders: app.async_shaders = !app.async_shaders; save_settings(); write_runtime_settings(); break;
    case HitBackupToggle: app.backup_before_launch = !app.backup_before_launch; save_settings(); break;
    case HitOpenSaves: open_path(save_directory()); break;
    case HitBackupSave: backup_selected_save(); break;
    case HitImportSave: import_save(); break;
    case HitShortcut: create_shortcut(); break;
    case HitGameLanguage:
        open_selector(HitGameLanguage, {L"English",L"Français",L"Deutsch",L"Italiano",L"Español"}); break;
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
        std::thread([] { check_for_updates(true); }).detach();
        break;
    case HitOpenCache: open_path(cache_directory()); break;
    case HitClearCache: clear_cache(); break;
    case HitOpenRoot: open_path(app.root); break;
    case HitRefresh: detect_environment(); update_game_status(); break;
    case HitExportLogs: export_logs(); break;
    case HitClearLogs: { std::lock_guard guard(app.mutex); app.logs.clear(); break; }
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

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if (wparam) return 0;
        break;
    case WM_PAINT: paint(window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
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
    case WM_SIZE: InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_MOUSEWHEEL: {
        if (app.page == Page::Home || app.popup_hit != HitNone) return 0;
        RECT client{}; GetClientRect(window, &client);
        const int required = app.page == Page::Controls ? 1200 : app.page == Page::Game ? 900 :
                             app.page == Page::Advanced ? 690 : app.page == Page::Video ? 900 : 0;
        const int maximum = std::max(0, required - (static_cast<int>(client.bottom) - 106));
        int& offset = app.scroll[static_cast<int>(app.page)];
        offset = std::clamp(offset - GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA * 72, 0, maximum);
        InvalidateRect(window, nullptr, FALSE); return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = POINT{1280, 820};
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        int now = hit_at(point);
        if (now != app.hover) { app.hover = now; InvalidateRect(window, nullptr, FALSE); }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking);
        return 0;
    }
    case WM_MOUSELEAVE: app.hover = HitNone; InvalidateRect(window, nullptr, FALSE); return 0;
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
        if (point.y < 78 && point.x < 225) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
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
    case WM_LOG_UPDATED: InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_TIMER:
        if (wparam == 1) {
            if (app.extraction_active) InvalidateRect(window, nullptr, FALSE);
            else KillTimer(window, 1);
            return 0;
        }
    case WM_AUTOPLAY: run_game(false); InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_CLOSE:
        if (app.busy) {
            show_modal(loc(L"Processus encore actif",L"Process still active"),
                       loc(L"Le jeu, la préparation ou l’extraction est encore actif. Fermer le launcher et arrêter le processus ?",
                           L"The game, preparation, or extraction is still active. Close the launcher and stop the process?"),
                       loc(L"Fermer et arrêter",L"Close and stop"), loc(L"Annuler",L"Cancel"), ModalAction::CloseBusy);
            return 0;
        }
        DestroyWindow(window); return 0;
    case WM_DESTROY:
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
                              fs::is_regular_file(app.root / "tools" / "pkg-extractor" / "pkg_extractor.exe") &&
                              fs::is_regular_file(app.root / "tools" / "pkg-extractor" / "libc++.dll") &&
                              fs::is_regular_file(app.root / "tools" / "pkg-extractor" / "zlib1.dll") &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "launcher-background.png") &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "launcher-icon.png") &&
                              fs::is_regular_file(app.root / "launcher" / "assets" / "star-ocean-logo.png");
        return complete ? 0 : 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input;
    GdiplusStartup(&app.gdiplus_token, &input, nullptr);
    app.config = app.root / "out" / "launcher.ini";
    load_settings();
    detect_environment();
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
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::max(1280, std::min(1720, static_cast<int>(work.right - work.left - 34)));
    int height = std::max(820, std::min(1040, static_cast<int>(work.bottom - work.top - 34)));
    app.window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"STAR OCEAN 5 · PS4RT Windows 0.3.5",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        work.left + (work.right - work.left - width) / 2,
        work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, instance, nullptr);
    const BOOL dark_frame = TRUE;
    const COLORREF no_border = 0xFFFFFFFE;
    const DWORD dark_attribute = 20;
    const DWORD border_attribute = 34;
    DwmSetWindowAttribute(app.window, dark_attribute, &dark_frame, sizeof(dark_frame));
    DwmSetWindowAttribute(app.window, border_attribute, &no_border, sizeof(no_border));
    SDL_SetAppMetadata("PS4RT Windows Launcher", "0.3.5", "org.ps4rt.star-ocean-5.launcher");
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    DragAcceptFiles(app.window, TRUE);
    ShowWindow(app.window, show);
    UpdateWindow(app.window);
    if (app.update_on_start) std::thread([] { check_for_updates(false); }).detach();
    if (std::wstring(GetCommandLineW()).find(L"--play") != std::wstring::npos)
        PostMessageW(app.window, WM_AUTOPLAY, 0, 0);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    app.save_preview.reset();
    app.game_logo.reset();
    app.launcher_icon.reset();
    app.background.reset();
    SDL_Quit();
    GdiplusShutdown(app.gdiplus_token);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
