// ============================================================================
// ByAzen.exe — Native C++20 Win32 + GDI+ Per-Pixel Alpha Overlay
// Minimalist Esports Macro Overlay for Roblox (Blade Ball) — Zero Dependencies
// ============================================================================

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winreg.h>
#include <commdlg.h>
#include <mmsystem.h>

#include <algorithm>
using std::min;
using std::max;
#include <objidl.h>
#include <gdiplus.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <cwchar>
#include <random>
#include <string>
#include <vector>

#include "click_engine.h"

using namespace Gdiplus;

static constexpr int CPS_MIN = clickengine::kMinCps;
static constexpr int CPS_MAX = clickengine::kMaxCps;

static constexpr UINT WM_TRAYICON      = WM_APP + 10;
static constexpr UINT WM_SHOW_EXISTING = WM_APP + 42;

static constexpr UINT ID_TRAY_TOGGLE_MENU = 2001;
static constexpr UINT ID_TRAY_TOGGLE_MAC  = 2002;
static constexpr UINT ID_TRAY_RESET_POS   = 2003;
static constexpr UINT ID_TRAY_EXIT        = 2004;

static constexpr int HOTKEY_MENU_BASE  = 100;
static constexpr int HOTKEY_PANIC_BASE = 110;

struct CsLock {
    CRITICAL_SECTION* cs;
    explicit CsLock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~CsLock() { LeaveCriticalSection(cs); }
};

static constexpr UINT_PTR TIMER_UI_TICK   = 1;
static constexpr UINT_PTR TIMER_TOPMOST   = 2;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static constexpr double PI_VAL = 3.14159265358979323846;
static bool gUndoAvailable = false;
static int gRenderAccentOverride = -1;
static bool gRenderHighContrast = false;

// ============================================================================
// Virtual Key Helpers
// ============================================================================

static std::wstring vkToName(int vk) {
    switch (vk) {
        case 0x01: return L"LMB";
        case 0x02: return L"RMB";
        case 0x04: return L"MMB";
        case 0x05: return L"M4";
        case 0x06: return L"M5";
        case 0x08: return L"BKSP";
        case 0x09: return L"TAB";
        case 0x0D: return L"ENTER";
        case 0x10: return L"SHIFT";
        case 0xA0: return L"LSHIFT";
        case 0xA1: return L"RSHIFT";
        case 0x11: return L"CTRL";
        case 0xA2: return L"LCTRL";
        case 0xA3: return L"RCTRL";
        case 0x12: return L"ALT";
        case 0xA4: return L"LALT";
        case 0xA5: return L"RALT";
        case 0x14: return L"CAPS";
        case 0x1B: return L"ESC";
        case 0x20: return L"SPACE";
        case 0x21: return L"PGUP";
        case 0x22: return L"PGDN";
        case 0x23: return L"END";
        case 0x24: return L"HOME";
        case 0x25: return L"LEFT";
        case 0x26: return L"UP";
        case 0x27: return L"RIGHT";
        case 0x28: return L"DOWN";
        case 0x2D: return L"INS";
        case 0x2E: return L"DEL";
        case 0xBF: return L"/";
        case 0xC0: return L"~";
        case 0xBD: return L"-";
        case 0xBB: return L"=";
        case 0xDB: return L"[";
        case 0xDD: return L"]";
        case 0xBA: return L";";
        case 0xBC: return L",";
        case 0xBE: return L".";
        default: break;
    }
    if (vk >= 0x30 && vk <= 0x39) {
        wchar_t buf[2] = { static_cast<wchar_t>(vk), 0 };
        return buf;
    }
    if (vk >= 0x41 && vk <= 0x5A) {
        wchar_t buf[2] = { static_cast<wchar_t>(vk), 0 };
        return buf;
    }
    if (vk >= 0x70 && vk <= 0x7B) {
        wchar_t buf[8];
        swprintf(buf, 8, L"F%d", vk - 0x70 + 1);
        return buf;
    }
    wchar_t hexBuf[12];
    swprintf(hexBuf, 12, L"VK_%02X", vk);
    return hexBuf;
}

static std::wstring hotkeyToName(int vk, UINT mods) {
    std::wstring out;
    if (mods & MOD_CONTROL) out += L"Ctrl+";
    if (mods & MOD_SHIFT) out += L"Shift+";
    if (mods & MOD_ALT) out += L"Alt+";
    out += vkToName(vk);
    return out;
}

static bool isExtendedVk(int vk) {
    switch (vk) {
        case 0x21: case 0x22: case 0x23: case 0x24:
        case 0x25: case 0x26: case 0x27: case 0x28:
        case 0x2D: case 0x2E: case 0xA3: case 0xA5:
            return true;
        default:
            return false;
    }
}

static bool isVkPressed(int vk) {
    if (vk <= 0) return false;
    if (vk == 0x12) {
        return ((GetAsyncKeyState(0x12) & 0x8000) != 0) ||
               ((GetAsyncKeyState(0xA4) & 0x8000) != 0) ||
               ((GetAsyncKeyState(0xA5) & 0x8000) != 0);
    }
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}



// ============================================================================
// Profiles & Persistent INI Configuration
// ============================================================================

struct ProfilePreset {
    std::wstring name;
    int targetCps;
    int delayMs;
    int holdMs;
    int jitterPct;
    bool pingSync;
};

struct DisplayLayout {
    int menuX = 36, menuY = 155;
    int hudX = -1, hudY = 36;
    int pillX = -1, pillY = -1;
};

static constexpr int PHOTO_WIDGET_COUNT = 8;
static constexpr uint64_t PHOTO_MAX_PIXELS = 30000000ULL;
static constexpr uint64_t PHOTO_TOTAL_PIXELS = 60000000ULL;

struct PhotoWidgetConfig {
    std::wstring path;
    int x = 100, y = 100;
    int width = 240, height = 160;
    int opacity = 100;
    int fitMode = 1; // 0=cover, 1=contain, 2=stretch
};

struct AppConfig {
    bool macroEnabled   = true;
    int  targetCps      = 48;
    int  delayMs        = 18;
    int  holdMs         = 3;
    int  jitterPct      = 0;
    bool  pingSync       = true;
    int   serverPingMs   = 42;
    bool  dualOutput     = false;
    bool  burstEnabled   = true;
    int   uiScale        = 100;
    int   fontScale      = 100;
    int   themeIndex     = 0;
    bool  toastEnabled   = true;
    bool  testMode       = false;
    std::wstring backgroundPath;
    int backgroundOpacity = 42;
    int backgroundFit = 0; // 0=cover, 1=contain, 2=stretch
    int customAccentRgb = 0x004539E5;
    bool customAccentEnabled = false;
    bool highContrast = false;
    bool compactMode = false;
    bool snapToGrid = true;
    bool sessionHistoryEnabled = false;
    bool startWithWindows = false;
    bool  preciseTiming  = false;
    bool  animationsEnabled = true;
    bool noAltTabLock   = true;
    bool onlyInRoblox   = true;
    bool chatSafePause  = true;
    bool toggleMode     = false; // false = Hold, true = Toggle

    int  triggerVk      = 0x05;  // M4 (XButton1)
    int  outputVk       = 0x46;  // F key
    int  menuVk         = 0x23;  // END
    int  radialVk       = 0x12;  // ALT
    int  panicVk        = 0x79;  // F10
    UINT menuMods       = 0;
    UINT panicMods      = 0;

    int  activeMonitor  = 0;
    std::wstring activeMonitorDevice;
    DisplayLayout displayLayouts[8] = {};
    PhotoWidgetConfig photoWidgets[8] = {};

    int  activeProfile  = 1;     // 0=Low Ping, 1=Clash Pro, 2=Safe, 3=Custom
    bool showHudBar     = true;
    bool showKeyPill    = true;
    int  hudOpacity     = 95;

    int  menuX          = 36;
    int  menuY          = 155;
    int  hudX           = -1;
    int  hudY           = 36;
    int  pillX          = -1;
    int  pillY          = -1;

    ProfilePreset profiles[4] = {
        { L"Low Ping",  56, 15, 3, 0, true  },
        { L"Clash Pro", 48, 18, 3, 0, true  },
        { L"Safe",      26, 34, 5, 9, false },
        { L"Custom",    40, 22, 4, 2, true  },
    };
};

static std::wstring getConfigFilePath() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring portableDir(exePath);
    const size_t slash = portableDir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) portableDir.resize(slash);
    const std::wstring portableConfig = portableDir + L"\\byazen_config.ini";

    wchar_t localAppData[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, localAppData))) {
        const std::wstring appDir = std::wstring(localAppData) + L"\\ByAzen";
        if (!CreateDirectoryW(appDir.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return portableConfig;
        const std::wstring appConfig = appDir + L"\\byazen_config.ini";
        if (GetFileAttributesW(appConfig.c_str()) != INVALID_FILE_ATTRIBUTES) return appConfig;
        // One-time migration for existing portable installs.
        if (GetFileAttributesW(portableConfig.c_str()) != INVALID_FILE_ATTRIBUTES &&
            CopyFileW(portableConfig.c_str(), appConfig.c_str(), TRUE)) return appConfig;
        return appConfig;
    }
    return portableConfig;
}

static void appendDiagnostic(const std::wstring& message) {
    std::wstring path = getConfigFilePath();
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    path += L"byazen.log";
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t line[1024] = {};
    swprintf(line, 1024, L"[%04u-%02u-%02u %02u:%02u:%02u] %ls\r\n",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, message.c_str());
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, NULL, NULL);
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(bytes - 1), &written, NULL);
    CloseHandle(file);
}

static std::wstring getUndoSnapshotPath() {
    return getConfigFilePath() + L".undo.ini";
}

static void writeIniInt(const std::wstring& path, const wchar_t* sec, const wchar_t* key, int val) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", val);
    WritePrivateProfileStringW(sec, key, buf, path.c_str());
}

static void writeIniString(const std::wstring& path, const wchar_t* sec, const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(sec, key, val.c_str(), path.c_str());
}

static void saveConfig(const AppConfig& c) {
    static ULONGLONG lastLogMs = 0;
    static ULONGLONG lastUndoSnapshotMs = 0;
    const ULONGLONG nowMs = GetTickCount64();
    const std::wstring existingConfig = getConfigFilePath();
    if (GetFileAttributesW(existingConfig.c_str()) != INVALID_FILE_ATTRIBUTES &&
        nowMs - lastUndoSnapshotMs >= 600) {
        gUndoAvailable = CopyFileW(existingConfig.c_str(), getUndoSnapshotPath().c_str(), FALSE) != FALSE;
        lastUndoSnapshotMs = nowMs;
    }
    if (nowMs - lastLogMs >= 1000) {
        appendDiagnostic(L"Settings saved (values and key presses are not recorded)");
        lastLogMs = nowMs;
    }
    std::wstring path = existingConfig;
    writeIniInt(path, L"General", L"configVer",     10);
    writeIniInt(path, L"General", L"macroEnabled",  c.macroEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"targetCps",     c.targetCps);
    writeIniInt(path, L"General", L"delayMs",       c.delayMs);
    writeIniInt(path, L"General", L"holdMs",        c.holdMs);
    writeIniInt(path, L"General", L"jitterPct",     c.jitterPct);
    writeIniInt(path, L"General", L"pingSync",      c.pingSync ? 1 : 0);
    writeIniInt(path, L"General", L"serverPingMs",  c.serverPingMs);
    writeIniInt(path, L"General", L"dualOutput",    c.dualOutput ? 1 : 0);
    writeIniInt(path, L"General", L"burstEnabled",  c.burstEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"uiScale",       c.uiScale);
    writeIniInt(path, L"General", L"fontScale",     c.fontScale);
    writeIniInt(path, L"General", L"themeIndex",    c.themeIndex);
    writeIniInt(path, L"General", L"toastEnabled",  c.toastEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"testMode",      c.testMode ? 1 : 0);
    writeIniInt(path, L"General", L"preciseTiming", c.preciseTiming ? 1 : 0);
    writeIniInt(path, L"General", L"animationsEnabled", c.animationsEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"noAltTabLock",  c.noAltTabLock ? 1 : 0);
    writeIniInt(path, L"General", L"onlyInRoblox",  c.onlyInRoblox ? 1 : 0);
    writeIniInt(path, L"General", L"chatSafePause", c.chatSafePause ? 1 : 0);
    writeIniInt(path, L"General", L"toggleMode",    c.toggleMode ? 1 : 0);
    writeIniString(path, L"General", L"backgroundPath", c.backgroundPath);
    writeIniInt(path, L"General", L"backgroundOpacity", c.backgroundOpacity);
    writeIniInt(path, L"General", L"backgroundFit", c.backgroundFit);
    writeIniInt(path, L"General", L"customAccentRgb", c.customAccentRgb);
    writeIniInt(path, L"General", L"customAccentEnabled", c.customAccentEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"highContrast", c.highContrast ? 1 : 0);
    writeIniInt(path, L"General", L"compactMode", c.compactMode ? 1 : 0);
    writeIniInt(path, L"General", L"snapToGrid", c.snapToGrid ? 1 : 0);
    writeIniInt(path, L"General", L"sessionHistoryEnabled", c.sessionHistoryEnabled ? 1 : 0);
    writeIniInt(path, L"General", L"startWithWindows", c.startWithWindows ? 1 : 0);

    writeIniInt(path, L"Binds", L"triggerVk", c.triggerVk);
    writeIniInt(path, L"Binds", L"outputVk",  c.outputVk);
    writeIniInt(path, L"Binds", L"menuVk",    c.menuVk);
    writeIniInt(path, L"Binds", L"radialVk",  c.radialVk);
    writeIniInt(path, L"Binds", L"panicVk",   c.panicVk);
    writeIniInt(path, L"Binds", L"menuMods",  static_cast<int>(c.menuMods));
    writeIniInt(path, L"Binds", L"panicMods", static_cast<int>(c.panicMods));

    writeIniInt(path, L"HUD", L"activeProfile", c.activeProfile);
    writeIniInt(path, L"HUD", L"activeMonitor", c.activeMonitor);
    writeIniString(path, L"HUD", L"activeMonitorDevice", c.activeMonitorDevice);
    writeIniInt(path, L"HUD", L"showHudBar",    c.showHudBar ? 1 : 0);
    writeIniInt(path, L"HUD", L"showKeyPill",   c.showKeyPill ? 1 : 0);
    writeIniInt(path, L"HUD", L"hudOpacity",    c.hudOpacity);
    writeIniInt(path, L"HUD", L"menuX",         c.menuX);
    writeIniInt(path, L"HUD", L"menuY",         c.menuY);
    writeIniInt(path, L"HUD", L"hudX",          c.hudX);
    writeIniInt(path, L"HUD", L"hudY",          c.hudY);
    writeIniInt(path, L"HUD", L"pillX",         c.pillX);
    writeIniInt(path, L"HUD", L"pillY",         c.pillY);

    for (int i = 0; i < 4; ++i) {
        wchar_t sec[24];
        swprintf(sec, 24, L"Profile%d", i);
        writeIniInt(path, sec, L"targetCps", c.profiles[i].targetCps);
        writeIniInt(path, sec, L"delayMs",   c.profiles[i].delayMs);
        writeIniInt(path, sec, L"holdMs",    c.profiles[i].holdMs);
        writeIniInt(path, sec, L"jitterPct", c.profiles[i].jitterPct);
        writeIniInt(path, sec, L"pingSync",  c.profiles[i].pingSync ? 1 : 0);
        writeIniString(path, sec, L"name", c.profiles[i].name);
    }
    for (int i = 0; i < 8; ++i) {
        wchar_t sec[24];
        swprintf(sec, 24, L"Display%d", i);
        const auto& d = c.displayLayouts[i];
        writeIniInt(path, sec, L"menuX", d.menuX); writeIniInt(path, sec, L"menuY", d.menuY);
        writeIniInt(path, sec, L"hudX", d.hudX); writeIniInt(path, sec, L"hudY", d.hudY);
        writeIniInt(path, sec, L"pillX", d.pillX); writeIniInt(path, sec, L"pillY", d.pillY);
    }
    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        wchar_t sec[32];
        swprintf(sec, 32, L"PhotoWidget%d", i);
        const auto& w = c.photoWidgets[i];
        writeIniString(path, sec, L"path", w.path);
        writeIniInt(path, sec, L"x", w.x); writeIniInt(path, sec, L"y", w.y);
        writeIniInt(path, sec, L"width", w.width); writeIniInt(path, sec, L"height", w.height);
        writeIniInt(path, sec, L"opacity", w.opacity); writeIniInt(path, sec, L"fitMode", w.fitMode);
    }
}

static int normalizeConfig(AppConfig& c);

static void loadConfig(AppConfig& c, const std::wstring& sourcePath = L"") {
    std::wstring path = sourcePath.empty() ? getConfigFilePath() : sourcePath;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    c.macroEnabled  = GetPrivateProfileIntW(L"General", L"macroEnabled",  1, path.c_str()) != 0;
    c.targetCps     = std::clamp((int)GetPrivateProfileIntW(L"General", L"targetCps", 48, path.c_str()), CPS_MIN, CPS_MAX);
    c.delayMs       = std::clamp((int)GetPrivateProfileIntW(L"General", L"delayMs",   18, path.c_str()), 1, 120);
    c.holdMs        = std::clamp((int)GetPrivateProfileIntW(L"General", L"holdMs",     3, path.c_str()), 1, 20);
    c.jitterPct     = std::clamp((int)GetPrivateProfileIntW(L"General", L"jitterPct",  0, path.c_str()), 0, 20);
    c.pingSync      = GetPrivateProfileIntW(L"General", L"pingSync",      1, path.c_str()) != 0;
    c.serverPingMs  = std::clamp((int)GetPrivateProfileIntW(L"General", L"serverPingMs", 42, path.c_str()), 0, 300);
    c.dualOutput    = GetPrivateProfileIntW(L"General", L"dualOutput", 0, path.c_str()) != 0;
    c.burstEnabled  = GetPrivateProfileIntW(L"General", L"burstEnabled", 1, path.c_str()) != 0;
    c.uiScale       = std::clamp((int)GetPrivateProfileIntW(L"General", L"uiScale", 100, path.c_str()), 75, 125);
    c.fontScale     = std::clamp((int)GetPrivateProfileIntW(L"General", L"fontScale", 100, path.c_str()), 85, 115);
    c.themeIndex    = std::clamp((int)GetPrivateProfileIntW(L"General", L"themeIndex", 0, path.c_str()), 0, 4);
    c.toastEnabled  = GetPrivateProfileIntW(L"General", L"toastEnabled", 1, path.c_str()) != 0;
    c.testMode      = GetPrivateProfileIntW(L"General", L"testMode", 0, path.c_str()) != 0;
    c.preciseTiming = GetPrivateProfileIntW(L"General", L"preciseTiming", 0, path.c_str()) != 0;
    c.animationsEnabled = GetPrivateProfileIntW(L"General", L"animationsEnabled", 1, path.c_str()) != 0;
    c.noAltTabLock  = GetPrivateProfileIntW(L"General", L"noAltTabLock",  1, path.c_str()) != 0;
    c.onlyInRoblox  = GetPrivateProfileIntW(L"General", L"onlyInRoblox",  1, path.c_str()) != 0;
    c.chatSafePause = GetPrivateProfileIntW(L"General", L"chatSafePause", 1, path.c_str()) != 0;
    c.toggleMode    = GetPrivateProfileIntW(L"General", L"toggleMode",    0, path.c_str()) != 0;
    wchar_t bgPath[512] = {};
    GetPrivateProfileStringW(L"General", L"backgroundPath", L"", bgPath, 512, path.c_str());
    c.backgroundPath = bgPath;
    c.backgroundOpacity = std::clamp((int)GetPrivateProfileIntW(L"General", L"backgroundOpacity", 42, path.c_str()), 0, 100);
    c.backgroundFit = std::clamp((int)GetPrivateProfileIntW(L"General", L"backgroundFit", 0, path.c_str()), 0, 2);
    c.customAccentRgb = GetPrivateProfileIntW(L"General", L"customAccentRgb", 0x004539E5, path.c_str()) & 0x00FFFFFF;
    c.customAccentEnabled = GetPrivateProfileIntW(L"General", L"customAccentEnabled", 0, path.c_str()) != 0;
    c.highContrast = GetPrivateProfileIntW(L"General", L"highContrast", 0, path.c_str()) != 0;
    c.compactMode = GetPrivateProfileIntW(L"General", L"compactMode", 0, path.c_str()) != 0;
    c.snapToGrid = GetPrivateProfileIntW(L"General", L"snapToGrid", 1, path.c_str()) != 0;
    c.sessionHistoryEnabled = GetPrivateProfileIntW(L"General", L"sessionHistoryEnabled", 0, path.c_str()) != 0;
    c.startWithWindows = GetPrivateProfileIntW(L"General", L"startWithWindows", 0, path.c_str()) != 0;

    c.triggerVk = GetPrivateProfileIntW(L"Binds", L"triggerVk", 0x05, path.c_str());
    c.outputVk  = GetPrivateProfileIntW(L"Binds", L"outputVk",  0x46, path.c_str());
    int cfgVer  = GetPrivateProfileIntW(L"General", L"configVer", 1, path.c_str());
    c.menuVk    = GetPrivateProfileIntW(L"Binds", L"menuVk",    0x23, path.c_str());
    if (cfgVer < 2 && c.menuVk == 0x2D) c.menuVk = 0x23;
    c.radialVk  = GetPrivateProfileIntW(L"Binds", L"radialVk",  0x12, path.c_str());
    c.panicVk   = GetPrivateProfileIntW(L"Binds", L"panicVk",   0x79, path.c_str());
    c.menuMods  = static_cast<UINT>(GetPrivateProfileIntW(L"Binds", L"menuMods", 0, path.c_str())) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT);
    c.panicMods = static_cast<UINT>(GetPrivateProfileIntW(L"Binds", L"panicMods", 0, path.c_str())) & (MOD_ALT | MOD_CONTROL | MOD_SHIFT);

    c.activeProfile = std::clamp((int)GetPrivateProfileIntW(L"HUD", L"activeProfile", 1, path.c_str()), 0, 3);
    c.activeMonitor = std::clamp((int)GetPrivateProfileIntW(L"HUD", L"activeMonitor", 0, path.c_str()), 0, 7);
    wchar_t monitorDevice[64] = {};
    GetPrivateProfileStringW(L"HUD", L"activeMonitorDevice", L"", monitorDevice, 64, path.c_str());
    c.activeMonitorDevice = monitorDevice;
    c.showHudBar    = GetPrivateProfileIntW(L"HUD", L"showHudBar",  1, path.c_str()) != 0;
    c.showKeyPill   = GetPrivateProfileIntW(L"HUD", L"showKeyPill", 1, path.c_str()) != 0;
    c.hudOpacity    = std::clamp((int)GetPrivateProfileIntW(L"HUD", L"hudOpacity", 95, path.c_str()), 40, 100);
    c.menuX         = GetPrivateProfileIntW(L"HUD", L"menuX",  36, path.c_str());
    c.menuY         = GetPrivateProfileIntW(L"HUD", L"menuY", 155, path.c_str());
    c.hudX          = GetPrivateProfileIntW(L"HUD", L"hudX",   -1, path.c_str());
    c.hudY          = GetPrivateProfileIntW(L"HUD", L"hudY",   36, path.c_str());
    c.pillX         = GetPrivateProfileIntW(L"HUD", L"pillX",  -1, path.c_str());
    c.pillY         = GetPrivateProfileIntW(L"HUD", L"pillY",  -1, path.c_str());

    for (int i = 0; i < 8; ++i) {
        wchar_t sec[24]; swprintf(sec, 24, L"Display%d", i);
        auto& d = c.displayLayouts[i];
        int defaultMenuX = (i == 0) ? c.menuX : d.menuX;
        int defaultMenuY = (i == 0) ? c.menuY : d.menuY;
        int defaultHudX  = (i == 0) ? c.hudX  : d.hudX;
        int defaultHudY  = (i == 0) ? c.hudY  : d.hudY;
        int defaultPillX = (i == 0) ? c.pillX : d.pillX;
        int defaultPillY = (i == 0) ? c.pillY : d.pillY;
        d.menuX = GetPrivateProfileIntW(sec, L"menuX", defaultMenuX, path.c_str());
        d.menuY = GetPrivateProfileIntW(sec, L"menuY", defaultMenuY, path.c_str());
        d.hudX  = GetPrivateProfileIntW(sec, L"hudX", defaultHudX, path.c_str());
        d.hudY  = GetPrivateProfileIntW(sec, L"hudY", defaultHudY, path.c_str());
        d.pillX = GetPrivateProfileIntW(sec, L"pillX", defaultPillX, path.c_str());
        d.pillY = GetPrivateProfileIntW(sec, L"pillY", defaultPillY, path.c_str());
    }

    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        wchar_t sec[32], widgetPath[32768] = {};
        swprintf(sec, 32, L"PhotoWidget%d", i);
        GetPrivateProfileStringW(sec, L"path", L"", widgetPath, 32768, path.c_str());
        auto& w = c.photoWidgets[i];
        w.path = widgetPath;
        w.x = GetPrivateProfileIntW(sec, L"x", 100 + (i % 4) * 24, path.c_str());
        w.y = GetPrivateProfileIntW(sec, L"y", 100 + (i % 4) * 24, path.c_str());
        w.width = GetPrivateProfileIntW(sec, L"width", 240, path.c_str());
        w.height = GetPrivateProfileIntW(sec, L"height", 160, path.c_str());
        w.opacity = GetPrivateProfileIntW(sec, L"opacity", 100, path.c_str());
        w.fitMode = GetPrivateProfileIntW(sec, L"fitMode", 1, path.c_str());
    }

    for (int i = 0; i < 4; ++i) {
        wchar_t sec[24];
        swprintf(sec, 24, L"Profile%d", i);
        c.profiles[i].targetCps = std::clamp((int)GetPrivateProfileIntW(sec, L"targetCps", c.profiles[i].targetCps, path.c_str()), CPS_MIN, CPS_MAX);
        c.profiles[i].delayMs   = std::clamp((int)GetPrivateProfileIntW(sec, L"delayMs",   c.profiles[i].delayMs,   path.c_str()), 1, 120);
        c.profiles[i].holdMs    = std::clamp((int)GetPrivateProfileIntW(sec, L"holdMs",    c.profiles[i].holdMs,    path.c_str()), 1, 20);
        c.profiles[i].jitterPct = std::clamp((int)GetPrivateProfileIntW(sec, L"jitterPct", c.profiles[i].jitterPct, path.c_str()), 0, 20);
        c.profiles[i].pingSync  = GetPrivateProfileIntW(sec, L"pingSync", c.profiles[i].pingSync ? 1 : 0, path.c_str()) != 0;
        wchar_t profileName[64] = {};
        GetPrivateProfileStringW(sec, L"name", c.profiles[i].name.c_str(), profileName, 64, path.c_str());
        c.profiles[i].name = profileName;
        // CPS is a global setting, not a per-profile preset value.
        c.profiles[i].targetCps = c.targetCps;
    }
    const int repairs = normalizeConfig(c);
    if (sourcePath.empty() && (cfgVer < 2 || repairs > 0)) {
        if (repairs > 0) appendDiagnostic(L"Configuration values normalized to safe ranges");
        saveConfig(c);
    }
}

static int normalizeConfig(AppConfig& c) {
    int fixes = 0;
    auto clampValue = [&](int& value, int low, int high) {
        const int old = value; value = std::clamp(value, low, high); fixes += old != value;
    };
    clampValue(c.targetCps, CPS_MIN, CPS_MAX); clampValue(c.delayMs, 1, 120); clampValue(c.holdMs, 1, 20);
    clampValue(c.jitterPct, 0, 20); clampValue(c.serverPingMs, 0, 300); clampValue(c.uiScale, 75, 125);
    clampValue(c.fontScale, 85, 115); clampValue(c.themeIndex, 0, 4); clampValue(c.hudOpacity, 40, 100); clampValue(c.activeProfile, 0, 3);
    clampValue(c.activeMonitor, 0, 7); clampValue(c.backgroundOpacity, 0, 100); clampValue(c.backgroundFit, 0, 2);
    clampValue(c.customAccentRgb, 0, 0x00FFFFFF);
    auto validVk = [&](int& vk, int fallback) { if (vk < 1 || vk > 0xFF) { vk = fallback; ++fixes; } };
    validVk(c.triggerVk, 0x05); validVk(c.outputVk, 0x46); validVk(c.menuVk, 0x23);
    validVk(c.radialVk, 0x12); validVk(c.panicVk, 0x79);
    const UINT validMods = MOD_ALT | MOD_CONTROL | MOD_SHIFT;
    if ((c.menuMods & ~validMods) != 0) { c.menuMods &= validMods; ++fixes; }
    if ((c.panicMods & ~validMods) != 0) { c.panicMods &= validMods; ++fixes; }
    for (auto& profile : c.profiles) {
        clampValue(profile.targetCps, CPS_MIN, CPS_MAX); clampValue(profile.delayMs, 1, 120);
        clampValue(profile.holdMs, 1, 20); clampValue(profile.jitterPct, 0, 20);
        if (profile.name.size() > 28) { profile.name.resize(28); ++fixes; }
    }
    for (auto& w : c.photoWidgets) {
        clampValue(w.x, 0, 100000); clampValue(w.y, 0, 100000);
        clampValue(w.width, 40, 4000); clampValue(w.height, 40, 4000);
        clampValue(w.opacity, 0, 100); clampValue(w.fitMode, 0, 2);
        if (w.path.size() > 32760) { w.path.resize(32760); ++fixes; }
    }
    return fixes;
}

// ============================================================================
// High-Precision Timing & SendInput Helpers
// ============================================================================

static double qpcSeconds() {
    static LARGE_INTEGER freq = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

static INPUT makeActionInput(int vk, bool down) {
    INPUT inp = {};
    if (vk == 0x01) {
        inp.type = INPUT_MOUSE;
        inp.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (vk == 0x02) {
        inp.type = INPUT_MOUSE;
        inp.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (vk == 0x04) {
        inp.type = INPUT_MOUSE;
        inp.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else {
        inp.type = INPUT_KEYBOARD;
        UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        inp.ki.wVk = scan ? 0 : static_cast<WORD>(vk);
        inp.ki.wScan = static_cast<WORD>(scan);
        DWORD flags = scan ? KEYEVENTF_SCANCODE : 0;
        if (!down) flags |= KEYEVENTF_KEYUP;
        if (isExtendedVk(vk)) flags |= KEYEVENTF_EXTENDEDKEY;
        inp.ki.dwFlags = flags;
    }
    return inp;
}

// Clock, sleep and input adapter the click engine (click_engine.h) runs on.
struct MacroIo {
    HANDLE timer      = NULL;
    bool   sendInput  = true;   // false in test mode
    bool   dualOutput = false;
    int    outputVk   = 0x46;

    double now() const { return qpcSeconds(); }
    void pause() const { YieldProcessor(); }

    void sleepMs(double ms) const {
        if (timer) {
            LARGE_INTEGER due = {};
            due.QuadPart = -static_cast<LONGLONG>(ms * 10000.0);
            if (due.QuadPart < 0 && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
                WaitForSingleObject(timer, static_cast<DWORD>(ms) + 50);
                return;
            }
        }
        Sleep(static_cast<DWORD>(std::max(1.0, ms)));
    }

    void emit(bool down) const {
        if (!sendInput) return;
        INPUT inputs[2] = {};
        UINT count = 0;
        if (dualOutput) {
            inputs[count++] = makeActionInput(0x46, down);
            inputs[count++] = makeActionInput(0x01, down);
        } else {
            inputs[count++] = makeActionInput(outputVk, down);
        }
        SendInput(count, inputs, sizeof(INPUT));
    }
    void press() const { emit(true); }
    void release() const { emit(false); }
};

// ============================================================================
// Global Application State
// ============================================================================

struct HitZone {
    int   id;
    RectF rect;
    int   minVal = 0;
    int   maxVal = 0;
};

enum HitId {
    HIT_NONE = 0,
    HIT_HEADER_DRAG,
    HIT_BTN_CLOSE_MENU,
    HIT_BTN_QUIT_APP,
    HIT_HUD_DRAG,
    HIT_PILL_DRAG,
    HIT_TAB_0,
    HIT_TAB_1,
    HIT_TAB_2,
    HIT_TAB_3,
    HIT_TAB_4,
    HIT_TAB_5,
    HIT_TAB_6,
    // Macro Page
    HIT_TOG_MACRO,
    HIT_TOG_TEST_MODE,
    HIT_TOG_PRECISE_TIMING,
    HIT_TOG_ANIMATIONS,
    HIT_BTN_EMERGENCY_STOP,
    HIT_SLD_CPS,
    HIT_SLD_DELAY,
    HIT_TOG_PING_SYNC,
    HIT_TOG_NO_ALTTAB,
    // Clash Page
    HIT_SEG_OUT_F,
    HIT_SEG_OUT_LMB,
    HIT_SEG_OUT_CUSTOM,
    HIT_SEG_MODE_HOLD,
    HIT_SEG_MODE_TOGGLE,
    HIT_SLD_HOLD,
    HIT_SLD_JITTER,
    HIT_TOG_ONLY_ROBLOX,
    HIT_TOG_CHAT_SAFE,
    // Widgets Page
    HIT_TOG_SHOW_HUD,
    HIT_TOG_SHOW_PILL,
    HIT_SLD_OPACITY,
    HIT_SLD_PING,
    HIT_SLD_UI_SCALE,
    HIT_SLD_BACKGROUND_OPACITY,
    HIT_SLD_FONT_SCALE,
    HIT_SLD_PHOTO_OPACITY,
    HIT_TOG_DUAL_OUTPUT,
    HIT_TOG_BURST,
    HIT_TOG_TOAST,
    HIT_BTN_THEME,
    HIT_BTN_RESET_POS,
    HIT_BTN_CONFIG_BACKUP,
    HIT_BTN_CONFIG_EXPORT,
    HIT_BTN_CONFIG_IMPORT,
    HIT_BTN_RESET_DEFAULTS,
    HIT_BTN_NEXT_MONITOR,
    HIT_BTN_RESET_SESSION,
    HIT_BTN_CHOOSE_BACKGROUND,
    HIT_BTN_BACKGROUND_FIT,
    HIT_BTN_RESET_BACKGROUND,
    HIT_BTN_CUSTOM_ACCENT,
    HIT_TOG_HIGH_CONTRAST,
    HIT_TOG_COMPACT_MODE,
    HIT_TOG_SNAP_GRID,
    HIT_TOG_SESSION_HISTORY,
    HIT_TOG_AUTOSTART,
    HIT_BTN_SEARCH_SETTINGS,
    HIT_BTN_UNDO,
    HIT_BTN_EXPORT_STATS,
    HIT_BTN_DIAGNOSTICS,
    HIT_BTN_RESTORE_BACKUP,
    HIT_BTN_CLEAR_LOG,
    HIT_BTN_VALIDATE_CONFIG,
    HIT_BTN_ONBOARDING,
    HIT_BTN_OPEN_CONFIG_FOLDER,
    HIT_BTN_ABOUT,
    // Profiles Page
    HIT_PROF_0,
    HIT_PROF_1,
    HIT_PROF_2,
    HIT_PROF_3,
    HIT_BTN_SAVE_PROF,
    HIT_BTN_RENAME_PROFILE,
    // Binds Page
    HIT_BIND_TRIGGER,
    HIT_BIND_OUTPUT,
    HIT_BIND_MENU,
    HIT_BIND_RADIAL,
    HIT_BIND_PANIC,
    HIT_QUICK_F,
    HIT_QUICK_LMB,
    HIT_QUICK_M4,
    // Radial Slices
    HIT_RADIAL_0,
    HIT_RADIAL_1,
    HIT_RADIAL_2,
    HIT_RADIAL_3,
};

static constexpr int PHOTO_HIT_BASE = 1000;
static constexpr int PHOTO_HIT_STRIDE = 9; // body + eight resize handles
static constexpr int PHOTO_SLOT_HIT_BASE = 1100;
static constexpr int PHOTO_ADD_HIT = 1110;
static constexpr int PHOTO_DELETE_HIT = 1111;
static constexpr int PHOTO_FIT_HIT = 1112;
static bool isPhotoWidgetHit(int id) { return id >= PHOTO_HIT_BASE && id < PHOTO_HIT_BASE + PHOTO_WIDGET_COUNT * PHOTO_HIT_STRIDE; }
static int photoWidgetIndexFromHit(int id) { return (id - PHOTO_HIT_BASE) / PHOTO_HIT_STRIDE; }
static int photoHandleFromHit(int id) { return (id - PHOTO_HIT_BASE) % PHOTO_HIT_STRIDE; }
static int photoHitId(int index, int handle = 0) { return PHOTO_HIT_BASE + index * PHOTO_HIT_STRIDE + handle; }

struct MonitorRecord {
    HMONITOR handle = NULL;
    RECT bounds = {0, 0, 0, 0};
    wchar_t device[CCHDEVICENAME] = {};
    bool primary = false;
};

struct GlobalApp {
    HINSTANCE hInst = NULL;
    HWND      hwnd  = NULL;
    HICON     hAppIcon = NULL;
    NOTIFYICONDATAW nid = {};
    bool      trayAdded = false;

    int   screenW = 1920;
    int   screenH = 1080;
    float S       = 1.0f;
    float uiScalePx = 1.0f;
    float fontScalePx = 1.0f;
    int   monitorHz = 240;

    HDC     memDC     = NULL;
    HBITMAP dibBmp    = NULL;
    HGDIOBJ oldBmp    = NULL;
    void*   dibBits   = nullptr;

    CRITICAL_SECTION cfgCs;
    AppConfig  cfg;

    std::atomic<bool> running{true};
    std::atomic<bool> isFiring{false};
    std::atomic<int>  measuredCps{0};
    std::atomic<uint64_t> sessionCycles{0};
    double sessionStartedAt = 0.0;
    SYSTEMTIME sessionWallStart = {};
    int lastSessionSecond = -1;
    uint64_t lastSessionCycles = UINT64_MAX;
    std::wstring toastText;
    double toastUntil = 0.0;
    Bitmap* customBackground = nullptr;
    Bitmap* defaultBackground = nullptr;
    std::wstring loadedBackgroundPath;
    Bitmap* photoBitmaps[PHOTO_WIDGET_COUNT] = {};
    std::wstring loadedPhotoPaths[PHOTO_WIDGET_COUNT] = {};
    int selectedPhotoWidget = 0;
    int photoDragStartX = 0, photoDragStartY = 0, photoDragStartW = 240, photoDragStartH = 160;
    double menuOpenedAt = 0.0;
    double lastAnimFrame = 0.0;
    bool timerResolutionEnabled = false;
    bool renameActive = false;
    int renameProfileIndex = -1;
    std::wstring renameBuffer;
    std::wstring hotkeyWarning;
    int virtualX = 0;
    int   virtualY = 0;
    MonitorRecord monitors[8] = {};
    int monitorCount = 0;
    int activeMonitor = 0;
    std::wstring monitorLabel = L"Дисплей 1";
    double uiScaleFactor = 1.0;
    std::atomic<HWND> lastRobloxHwnd{NULL};

    bool menuOpen      = true;
    bool radialOpen    = false;
    int  radialHovered = 1;
    POINT radialOrigin = {0, 0};
    int  currentTab    = 0;
    int  hoveredHitId  = HIT_NONE;

    // Drag / Slider interaction
    int   activeDragHit = HIT_NONE;
    POINT dragStartMouse = {0, 0};
    POINT dragStartElem  = {0, 0};

    // Interactive Bind Capture
    int    capturingHitId = HIT_NONE;
    double captureReadyAt = 0.0;
    std::wstring bindStatusText = L"Нажми на любую кнопку справа, чтобы назначить клавишу или кнопку мыши (ESC — отмена).";
    int    bindStatusKind = 0; // 0=neutral, 1=green, 2=crimson
    double profSavedUntil = 0.0;

    // Edge detection for global hotkeys
    bool prevMenuDown   = false;
    bool prevPanicDown  = false;
    bool prevRadialDown = false;
    bool prevTrigVis    = false;
    bool prevOutVis     = false;
    int  lastShownCps   = -1;
    int  lastShownPing  = -1;
    bool lastFiringVis  = false;

    bool needsRedraw    = true;
    double configSaveDueAt = 0.0; // deferred save after mouse-wheel slider steps
    std::vector<HitZone> hitZones;
};

static GlobalApp gApp;

static void renderOverlay();
static void refreshRegisteredHotkeys();
static void setToast(const std::wstring& text, double seconds = 1.8);

static BOOL CALLBACK collectMonitorInfo(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* app = reinterpret_cast<GlobalApp*>(data);
    if (app->monitorCount >= 8) return TRUE;
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return TRUE;
    MonitorRecord& record = app->monitors[app->monitorCount++];
    record.handle = monitor;
    record.bounds = info.rcMonitor;
    wcscpy_s(record.device, info.szDevice);
    record.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    return TRUE;
}

static void refreshMonitorList() {
    int priorIndex = 0;
    std::wstring priorDevice;
    { CsLock lk(&gApp.cfgCs); priorIndex = gApp.cfg.activeMonitor; priorDevice = gApp.cfg.activeMonitorDevice; }
    gApp.monitorCount = 0;
    EnumDisplayMonitors(NULL, NULL, collectMonitorInfo, reinterpret_cast<LPARAM>(&gApp));
    if (gApp.monitorCount == 0) {
        gApp.monitorCount = 1;
        gApp.monitors[0].bounds = { gApp.virtualX, gApp.virtualY,
            gApp.virtualX + std::max(1, gApp.screenW), gApp.virtualY + std::max(1, gApp.screenH) };
        wcscpy_s(gApp.monitors[0].device, L"DISPLAY1");
        gApp.monitors[0].primary = true;
    }
    int selected = -1;
    if (!priorDevice.empty()) {
        for (int i = 0; i < gApp.monitorCount; ++i) if (priorDevice == gApp.monitors[i].device) selected = i;
    }
    if (selected < 0) {
        for (int i = 0; i < gApp.monitorCount; ++i) if (gApp.monitors[i].primary) selected = i;
    }
    if (selected < 0 && priorIndex >= 0 && priorIndex < gApp.monitorCount) selected = priorIndex;
    gApp.activeMonitor = std::clamp(selected < 0 ? 0 : selected, 0, gApp.monitorCount - 1);
    gApp.monitorLabel = gApp.monitors[gApp.activeMonitor].device;
    {
        CsLock lk(&gApp.cfgCs);
        gApp.cfg.activeMonitor = gApp.activeMonitor;
        gApp.cfg.activeMonitorDevice = gApp.monitorLabel;
    }
    int height = gApp.monitors[gApp.activeMonitor].bounds.bottom - gApp.monitors[gApp.activeMonitor].bounds.top;
    gApp.S = (height >= 1350) ? static_cast<float>(height) / 1080.0f : 1.0f;
}

static void selectMonitorAtCursor() {
    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR underCursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    for (int i = 0; i < gApp.monitorCount; ++i) {
        if (gApp.monitors[i].handle == underCursor) {
            bool changed = (i != gApp.activeMonitor);
            gApp.activeMonitor = i;
            gApp.monitorLabel = gApp.monitors[i].device;
            int height = gApp.monitors[i].bounds.bottom - gApp.monitors[i].bounds.top;
            gApp.S = (height >= 1350) ? static_cast<float>(height) / 1080.0f : 1.0f;
            if (changed) {
                CsLock lk(&gApp.cfgCs);
                gApp.cfg.activeMonitor = i;
                gApp.cfg.activeMonitorDevice = gApp.monitorLabel;
                saveConfig(gApp.cfg);
            }
            return;
        }
    }
}

static void selectNextMonitor() {
    if (gApp.monitorCount <= 1) {
        setToast(L"Обнаружен один дисплей");
        return;
    }
    gApp.activeMonitor = (gApp.activeMonitor + 1) % gApp.monitorCount;
    gApp.monitorLabel = gApp.monitors[gApp.activeMonitor].device;
    int height = gApp.monitors[gApp.activeMonitor].bounds.bottom - gApp.monitors[gApp.activeMonitor].bounds.top;
    gApp.S = (height >= 1350) ? static_cast<float>(height) / 1080.0f : 1.0f;
    {
        CsLock lk(&gApp.cfgCs);
        gApp.cfg.activeMonitor = gApp.activeMonitor;
        gApp.cfg.activeMonitorDevice = gApp.monitorLabel;
        saveConfig(gApp.cfg);
    }
    setToast(L"Выбран дисплей: " + gApp.monitorLabel);
    renderOverlay();
}

static void syncTimerResolution(bool precise) {
    if (precise && !gApp.timerResolutionEnabled) {
        timeBeginPeriod(1);
        gApp.timerResolutionEnabled = true;
    } else if (!precise && gApp.timerResolutionEnabled) {
        timeEndPeriod(1);
        gApp.timerResolutionEnabled = false;
    }
}


// ============================================================================
// Window Focus & Click-Through Helpers
// ============================================================================

static bool checkAndTrackRobloxForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    wchar_t title[256] = {0};
    int len = GetWindowTextW(fg, title, 255);
    if (len <= 0) return false;
    for (int i = 0; i < len; ++i) title[i] = towlower(title[i]);
    if (wcsstr(title, L"roblox") != nullptr) {
        gApp.lastRobloxHwnd.store(fg);
        return true;
    }
    return false;
}

static void refocusRobloxWindow() {
    HWND rbx = gApp.lastRobloxHwnd.load();
    if (!rbx || !IsWindow(rbx)) {
        rbx = FindWindowW(NULL, L"Roblox");
    }
    if (rbx && IsWindow(rbx)) {
        SetForegroundWindow(rbx);
    }
}

static void applyClickThrough(bool clickThrough) {
    if (!gApp.hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(gApp.hwnd, GWL_EXSTYLE);
    ex |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    if (clickThrough) {
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    } else {
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(gApp.hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(
        gApp.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED
    );
}

// ============================================================================
// Offline Win32 Macro Engine
// ============================================================================

// The settings the worker needs, copied out of AppConfig under the lock. Kept
// small so that the 1 ms idle poll does not copy the whole configuration.
struct MacroParams {
    bool macroEnabled  = false;
    bool dualOutput    = false;
    bool chatSafePause = true;
    bool toggleMode    = false;
    bool onlyInRoblox  = true;
    bool testMode      = false;
    int  triggerVk     = 0;
    int  outputVk      = 0;
    clickengine::EngineSettings engine;
};

static MacroParams readMacroParams() {
    CsLock lk(&gApp.cfgCs);
    const AppConfig& c = gApp.cfg;
    MacroParams p;
    p.macroEnabled  = c.macroEnabled;
    p.dualOutput    = c.dualOutput;
    p.chatSafePause = c.chatSafePause;
    p.toggleMode    = c.toggleMode;
    p.onlyInRoblox  = c.onlyInRoblox;
    p.testMode      = c.testMode;
    p.triggerVk     = c.triggerVk;
    p.outputVk      = c.outputVk;
    p.engine.targetCps     = c.targetCps;
    p.engine.holdMs        = c.holdMs;
    p.engine.jitterPct     = c.jitterPct;
    p.engine.pingSync      = c.pingSync;
    p.engine.pingMs        = c.serverPingMs;
    p.engine.burstEnabled  = c.burstEnabled;
    p.engine.preciseTiming = c.preciseTiming;
    return p;
}

static DWORD WINAPI macroWorkerThread(LPVOID) {
    MacroIo io;
    io.timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const bool highResTimer = (io.timer != NULL);
    clickengine::WaitCaps caps;
    if (!highResTimer) {
        // Windows older than 10 (1803): a normal timer at 1 ms system resolution,
        // with busy-waiting covering what that timer cannot resolve.
        io.timer = CreateWaitableTimerW(NULL, FALSE, NULL);
        timeBeginPeriod(1);
        caps.minSleepMs = 1.0;
        caps.slackMs = 2.0;
        caps.spinBelowPeriodMs = 12.0;
    }
    clickengine::ClickEngine engine(caps);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    std::mt19937 rng(static_cast<unsigned int>(GetTickCount64()));
    std::uniform_real_distribution<double> jitterUnit(-1.0, 1.0);

    bool prevTrigger = false;
    bool toggleLatched = false;
    bool chatPaused = false;
    bool prevSlash = false;
    bool prevExitChat = false;
    bool robloxFg = false;
    double lastHousekeeping = -1.0;

    auto goIdle = [&]() {
        engine.stop();
        gApp.isFiring.store(false);
        gApp.measuredCps.store(0);
    };

    while (gApp.running.load()) {
        const MacroParams p = readMacroParams();

        if (!p.macroEnabled || (!p.dualOutput && p.triggerVk == p.outputVk)) {
            goIdle();
            toggleLatched = false;
            Sleep(10);
            continue;
        }

        // The trigger is polled every pass; the foreground-window and chat-key
        // checks are refreshed at most every 4 ms, and at once when the trigger changes.
        const double nowPoll = qpcSeconds();
        const bool trigDown = isVkPressed(p.triggerVk);
        if (trigDown != prevTrigger || nowPoll - lastHousekeeping >= 0.004) {
            lastHousekeeping = nowPoll;
            robloxFg = checkAndTrackRobloxForeground();

            if (p.chatSafePause) {
                bool slash = isVkPressed(0xBF);
                if (slash && !prevSlash && robloxFg) chatPaused = true;
                prevSlash = slash;

                bool exitChat = isVkPressed(0x0D) || isVkPressed(0x1B);
                if (exitChat && !prevExitChat) chatPaused = false;
                prevExitChat = exitChat;
            } else {
                chatPaused = false;
            }
        }

        bool shouldFire = false;
        if (p.toggleMode) {
            if (trigDown && !prevTrigger) toggleLatched = !toggleLatched;
            shouldFire = toggleLatched;
        } else {
            toggleLatched = false;
            shouldFire = trigDown;
        }
        prevTrigger = trigDown;

        if (shouldFire && p.onlyInRoblox && !robloxFg) {
            shouldFire = false;
        }
        if (chatPaused || gApp.menuOpen) {
            shouldFire = false;
        }

        if (!shouldFire) {
            goIdle();
            // 1 ms poll: the first click leaves about a millisecond after the trigger goes down.
            io.sleepMs(1.0);
            continue;
        }

        gApp.isFiring.store(true);
        io.sendInput  = !p.testMode;
        io.dualOutput = p.dualOutput;
        io.outputVk   = p.outputVk;

        // One click: press, hold, release, then wait until the next press is due.
        // The first click after the trigger goes out immediately, at full rate.
        engine.cycle(io, p.engine, p.engine.jitterPct > 0 ? jitterUnit(rng) : 0.0);

        gApp.measuredCps.store(engine.measuredCps());
        gApp.sessionCycles.fetch_add(1, std::memory_order_relaxed);
    }
    if (io.timer) CloseHandle(io.timer);
    if (!highResTimer) timeEndPeriod(1);
    return 0;
}

// ============================================================================
// GDI+ Vector Geometry & Minimalist Esports Renderer
// ============================================================================

static void makeRoundRectPath(GraphicsPath& path, const RectF& r, float radius) {
    float rad = std::min({ radius, r.Width * 0.5f, r.Height * 0.5f });
    float d = rad * 2.0f;
    if (d <= 0.5f) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

static void fillStrokeRoundRect(Graphics& g, const RectF& r, float radius, Color fillCol, Color borderCol, float borderW = 1.0f) {
    GraphicsPath path;
    makeRoundRectPath(path, r, radius);
    if (fillCol.GetA() > 0) {
        SolidBrush br(fillCol);
        g.FillPath(&br, &path);
    }
    if (borderW > 0.0f && borderCol.GetA() > 0) {
        Pen pen(borderCol, borderW);
        g.DrawPath(&pen, &path);
    }
}

static Color withAlpha(Color c, BYTE alpha) {
    return Color(alpha, c.GetR(), c.GetG(), c.GetB());
}

static void drawVectorIcon(Graphics& g, const char* kind, PointF center, float size, Color col, BYTE alpha = 255) {
    Color c = withAlpha(col, alpha);
    Pen pen(c, std::max(1.5f, size * 0.085f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    SolidBrush br(c);
    SolidBrush bgBr(Color(alpha, 17, 19, 24));

    float cx = center.X;
    float cy = center.Y;
    float half = size * 0.5f;

    if (strcmp(kind, "sliders") == 0) {
        float ys[3] = { cy - half * 0.62f, cy, cy + half * 0.62f };
        float xs[3] = { cx - half * 0.25f, cx + half * 0.32f, cx - half * 0.15f };
        float kr = std::max(2.1f, size * 0.12f);
        for (int i = 0; i < 3; ++i) {
            g.DrawLine(&pen, PointF(cx - half * 0.85f, ys[i]), PointF(cx + half * 0.85f, ys[i]));
            RectF er(xs[i] - kr, ys[i] - kr, kr * 2.0f, kr * 2.0f);
            g.FillEllipse(&bgBr, er);
            g.DrawEllipse(&pen, er);
        }
    } else if (strcmp(kind, "target") == 0) {
        float r = half * 0.68f;
        g.DrawEllipse(&pen, RectF(cx - r, cy - r, r * 2.0f, r * 2.0f));
        g.DrawLine(&pen, PointF(cx, cy - half * 0.95f), PointF(cx, cy - r * 0.55f));
        g.DrawLine(&pen, PointF(cx, cy + r * 0.55f), PointF(cx, cy + half * 0.95f));
        g.DrawLine(&pen, PointF(cx - half * 0.95f, cy), PointF(cx - r * 0.55f, cy));
        g.DrawLine(&pen, PointF(cx + r * 0.55f, cy), PointF(cx + half * 0.95f, cy));
        g.FillEllipse(&br, RectF(cx - 1.7f, cy - 1.7f, 3.4f, 3.4f));
    } else if (strcmp(kind, "grid") == 0) {
        float gsz = half * 0.72f;
        float gap = size * 0.12f;
        float s = gsz - gap * 0.5f;
        float dxs[2] = { -gsz, gap * 0.5f };
        for (float dx : dxs) {
            for (float dy : dxs) {
                GraphicsPath gp;
                makeRoundRectPath(gp, RectF(cx + dx, cy + dy, s, s), 1.8f);
                g.DrawPath(&pen, &gp);
            }
        }
    } else if (strcmp(kind, "profile") == 0) {
        float hr = half * 0.34f;
        g.DrawEllipse(&pen, RectF(cx - hr, cy - half * 0.34f - hr, hr * 2.0f, hr * 2.0f));
        g.DrawArc(&pen, RectF(cx - half * 0.72f, cy + half * 0.12f, half * 1.44f, half * 1.25f), 195.0f, 150.0f);
    } else if (strcmp(kind, "keyboard") == 0) {
        GraphicsPath kp;
        makeRoundRectPath(kp, RectF(cx - half * 0.88f, cy - half * 0.56f, half * 1.76f, half * 1.12f), 2.5f);
        g.DrawPath(&pen, &kp);
        g.DrawLine(&pen, PointF(cx - half * 0.42f, cy + half * 0.24f), PointF(cx + half * 0.42f, cy + half * 0.24f));
        float kxs[4] = { -0.52f, -0.18f, 0.18f, 0.52f };
        for (float kx : kxs) {
            g.FillEllipse(&br, RectF(cx + half * kx - 1.3f, cy - half * 0.16f - 1.3f, 2.6f, 2.6f));
        }
    } else if (strcmp(kind, "pulse") == 0) {
        PointF pts[7] = {
            PointF(cx - half * 0.95f, cy),
            PointF(cx - half * 0.42f, cy),
            PointF(cx - half * 0.20f, cy - half * 0.65f),
            PointF(cx + half * 0.08f, cy + half * 0.70f),
            PointF(cx + half * 0.30f, cy - half * 0.28f),
            PointF(cx + half * 0.48f, cy),
            PointF(cx + half * 0.95f, cy),
        };
        g.DrawLines(&pen, pts, 7);
    } else if (strcmp(kind, "crown") == 0) {
        PointF poly[7] = {
            PointF(cx - half * 0.72f, cy + half * 0.45f),
            PointF(cx - half * 0.85f, cy - half * 0.42f),
            PointF(cx - half * 0.32f, cy + half * 0.02f),
            PointF(cx,                cy - half * 0.62f),
            PointF(cx + half * 0.32f, cy + half * 0.02f),
            PointF(cx + half * 0.85f, cy - half * 0.42f),
            PointF(cx + half * 0.72f, cy + half * 0.45f),
        };
        g.FillPolygon(&br, poly, 7);
        g.DrawLine(&pen, PointF(cx - half * 0.70f, cy + half * 0.68f), PointF(cx + half * 0.70f, cy + half * 0.68f));
    } else if (strcmp(kind, "shield") == 0) {
        PointF sh[6] = {
            PointF(cx,                cy - half * 0.82f),
            PointF(cx + half * 0.70f, cy - half * 0.48f),
            PointF(cx + half * 0.56f, cy + half * 0.34f),
            PointF(cx,                cy + half * 0.86f),
            PointF(cx - half * 0.56f, cy + half * 0.34f),
            PointF(cx - half * 0.70f, cy - half * 0.48f),
        };
        g.DrawPolygon(&pen, sh, 6);
    } else if (strcmp(kind, "sword") == 0) {
        PointF blade[5] = {
            PointF(cx - half * 0.16f, cy + half * 0.16f),
            PointF(cx + half * 0.62f, cy - half * 0.62f),
            PointF(cx + half * 0.82f, cy - half * 0.82f),
            PointF(cx + half * 0.76f, cy - half * 0.46f),
            PointF(cx + half * 0.02f, cy + half * 0.28f),
        };
        g.FillPolygon(&br, blade, 5);
        Pen hiltPen(c, std::max(2.2f, size * 0.10f));
        hiltPen.SetStartCap(LineCapRound);
        hiltPen.SetEndCap(LineCapRound);
        g.DrawLine(&hiltPen, PointF(cx - half * 0.36f, cy - half * 0.04f), PointF(cx + half * 0.08f, cy + half * 0.40f));
        g.DrawLine(&hiltPen, PointF(cx - half * 0.14f, cy + half * 0.18f), PointF(cx - half * 0.68f, cy + half * 0.72f));
    }
}

static void drawToggleSwitch(Graphics& g, RectF r, bool checked) {
    Color trackFill = checked ? Color(255, 90, 96, 112) : Color(255, 35, 39, 50);
    Color trackBorder(255, 62, 68, 82);
    fillStrokeRoundRect(g, r, r.Height * 0.5f, trackFill, trackBorder, 1.0f);

    float knobD = r.Height - 6.0f;
    float kx = checked ? (r.X + r.Width - knobD - 3.5f) : (r.X + 3.5f);
    float ky = r.Y + 3.0f;
    SolidBrush knobBr(checked ? Color(255, 250, 251, 255) : Color(255, 150, 156, 170));
    g.FillEllipse(&knobBr, RectF(kx, ky, knobD, knobD));
}

static void drawLabeledSlider(
    Graphics& g,
    const FontFamily* ff,
    RectF area,
    const wchar_t* title,
    int val,
    int minV,
    int maxV,
    const wchar_t* suffix,
    float S
) {
    Font font(ff, 14.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);

    std::wstring prefix = std::wstring(title) + L": ";
    RectF measured;
    g.MeasureString(prefix.c_str(), -1, &font, PointF(area.X, area.Y), &sf, &measured);

    SolidBrush whiteBr(Color(255, 244, 246, 250));
    SolidBrush crimsonBr(Color(255, 229, 57, 69));
    g.DrawString(prefix.c_str(), -1, &font, PointF(area.X, area.Y), &sf, &whiteBr);

    wchar_t valBuf[32];
    swprintf(valBuf, 32, L"%d%ls", val, suffix);
    g.DrawString(valBuf, -1, &font, PointF(area.X + measured.Width - 3.0f * S, area.Y), &sf, &crimsonBr);

    float pad = 9.0f * S;
    float trackY = area.Y + 31.0f * S;
    float trackH = 5.0f * S;
    float span = std::max(1.0f, area.Width - 2.0f * pad);
    float ratio = static_cast<float>(val - minV) / static_cast<float>(std::max(1, maxV - minV));
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    float thumbX = area.X + pad + span * ratio;

    fillStrokeRoundRect(g, RectF(area.X + pad, trackY, span, trackH), trackH * 0.5f,
                        Color(255, 66, 71, 84), Color(0, 0, 0, 0), 0.0f);
    if (thumbX > area.X + pad + 1.0f) {
        fillStrokeRoundRect(g, RectF(area.X + pad, trackY, thumbX - (area.X + pad), trackH), trackH * 0.5f,
                            Color(255, 238, 241, 247), Color(0, 0, 0, 0), 0.0f);
    }

    float tr = 8.5f * S;
    float tcy = trackY + trackH * 0.5f;
    SolidBrush shadowBr(Color(95, 0, 0, 0));
    SolidBrush thumbBr(Color(255, 250, 251, 255));
    g.FillEllipse(&shadowBr, RectF(thumbX - tr, tcy - tr + 1.2f * S, tr * 2.0f, tr * 2.0f));
    g.FillEllipse(&thumbBr,  RectF(thumbX - tr, tcy - tr, tr * 2.0f, tr * 2.0f));
}

// ============================================================================
// Master Frame Renderer -> UpdateLayeredWindow
// ============================================================================

static bool reloadCustomBackground(const std::wstring& path) {
    if (path.empty()) {
        delete gApp.customBackground;
        gApp.customBackground = nullptr;
        gApp.loadedBackgroundPath.clear();
        return true;
    }
    if (path == gApp.loadedBackgroundPath && gApp.customBackground) return true;
    Bitmap* candidate = new Bitmap(path.c_str());
    if (!candidate || candidate->GetLastStatus() != Ok || candidate->GetWidth() == 0 || candidate->GetHeight() == 0 ||
        static_cast<uint64_t>(candidate->GetWidth()) * candidate->GetHeight() > 100000000ULL) {
        delete candidate;
        return false;
    }
    delete gApp.customBackground;
    gApp.customBackground = candidate;
    gApp.loadedBackgroundPath = path;
    return true;
}

static bool loadEmbeddedBackground() {
    HRSRC resource = FindResourceW(gApp.hInst, MAKEINTRESOURCEW(102), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(gApp.hInst, resource);
    const DWORD byteCount = SizeofResource(gApp.hInst, resource);
    const void* sourceBytes = loaded ? LockResource(loaded) : nullptr;
    if (!sourceBytes || byteCount == 0) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (!memory) return false;
    void* destination = GlobalLock(memory);
    if (!destination) { GlobalFree(memory); return false; }
    std::memcpy(destination, sourceBytes, byteCount);
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) { GlobalFree(memory); return false; }
    Bitmap* source = Bitmap::FromStream(stream, FALSE);
    if (!source || source->GetLastStatus() != Ok) {
        delete source;
        stream->Release();
        return false;
    }
    Bitmap* detached = source->Clone(0, 0, static_cast<INT>(source->GetWidth()),
                                     static_cast<INT>(source->GetHeight()), PixelFormat32bppARGB);
    delete source;
    stream->Release();
    if (!detached || detached->GetLastStatus() != Ok) { delete detached; return false; }
    delete gApp.defaultBackground;
    gApp.defaultBackground = detached;
    return true;
}

static void drawCustomBackground(Graphics& g, Bitmap* image, const RectF& frame, int opacityPct, int fitMode) {
    if (!image || opacityPct <= 0 || image->GetLastStatus() != Ok) return;
    const float iw = static_cast<float>(image->GetWidth());
    const float ih = static_cast<float>(image->GetHeight());
    if (iw <= 0.0f || ih <= 0.0f || frame.Width <= 0.0f || frame.Height <= 0.0f) return;
    const GraphicsState saved = g.Save();
    GraphicsPath clip;
    makeRoundRectPath(clip, frame, 12.0f * gApp.uiScalePx);
    g.SetClip(&clip, CombineModeIntersect);
    RectF dst = frame;
    if (fitMode != 2) {
        const float sx = frame.Width / iw;
        const float sy = frame.Height / ih;
        const float scale = (fitMode == 1) ? std::min(sx, sy) : std::max(sx, sy);
        dst.Width = iw * scale;
        dst.Height = ih * scale;
        dst.X = frame.X + (frame.Width - dst.Width) * 0.5f;
        dst.Y = frame.Y + (frame.Height - dst.Height) * 0.5f;
    }
    ColorMatrix matrix = {};
    matrix.m[0][0] = 1.0f; matrix.m[1][1] = 1.0f; matrix.m[2][2] = 1.0f;
    matrix.m[3][3] = std::clamp(opacityPct, 0, 100) / 100.0f;
    matrix.m[4][4] = 1.0f;
    ImageAttributes attributes;
    attributes.SetColorMatrix(&matrix, ColorMatrixFlagsDefault, ColorAdjustTypeBitmap);
    g.DrawImage(image, dst, 0.0f, 0.0f, iw, ih, UnitPixel, &attributes);
    g.Restore(saved);
}

static Bitmap* createDetachedPhotoBitmap(const std::wstring& path) {
    if (path.empty()) return nullptr;
    Bitmap* source = new Bitmap(path.c_str());
    if (!source || source->GetLastStatus() != Ok || source->GetWidth() == 0 || source->GetHeight() == 0 ||
        static_cast<uint64_t>(source->GetWidth()) * source->GetHeight() > PHOTO_MAX_PIXELS) {
        delete source;
        return nullptr;
    }
    Bitmap* detached = source->Clone(0, 0, static_cast<INT>(source->GetWidth()),
                                     static_cast<INT>(source->GetHeight()), PixelFormat32bppARGB);
    delete source;
    if (!detached || detached->GetLastStatus() != Ok) { delete detached; return nullptr; }
    return detached;
}

static void syncPhotoWidgetBitmaps(const AppConfig& cfg) {
    uint64_t usedPixels = 0;
    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        const std::wstring& path = cfg.photoWidgets[i].path;
        if (gApp.loadedPhotoPaths[i] == path) {
            if (gApp.photoBitmaps[i]) usedPixels += static_cast<uint64_t>(gApp.photoBitmaps[i]->GetWidth()) * gApp.photoBitmaps[i]->GetHeight();
            continue;
        }
        delete gApp.photoBitmaps[i];
        gApp.photoBitmaps[i] = nullptr;
        gApp.loadedPhotoPaths[i] = path;
        if (path.empty()) continue;
        Bitmap* candidate = createDetachedPhotoBitmap(path);
        if (candidate) {
            const uint64_t pixels = static_cast<uint64_t>(candidate->GetWidth()) * candidate->GetHeight();
            if (usedPixels + pixels <= PHOTO_TOTAL_PIXELS) {
                gApp.photoBitmaps[i] = candidate;
                usedPixels += pixels;
            } else {
                delete candidate;
            }
        }
    }
}

static Color accentStroke(int theme, BYTE alpha = 255) {
    if (gRenderAccentOverride >= 0) {
        COLORREF custom = static_cast<COLORREF>(gRenderAccentOverride);
        return Color(alpha, GetRValue(custom), GetGValue(custom), GetBValue(custom));
    }
    static const BYTE rgb[5][3] = { {229,57,69}, {171,100,255}, {56,189,248}, {52,211,153}, {238,242,255} };
    theme = std::clamp(theme, 0, 4);
    return Color(alpha, rgb[theme][0], rgb[theme][1], rgb[theme][2]);
}
static Color accentFill(int theme, BYTE alpha = 225) {
    Color c = accentStroke(theme, alpha);
    return Color(alpha, static_cast<BYTE>(c.GetR() / 3), static_cast<BYTE>(c.GetG() / 5), static_cast<BYTE>(c.GetB() / 5));
}

static void renderOverlay() {
    if (!gApp.hwnd || !gApp.dibBits) return;
    gApp.needsRedraw = false;
    gApp.hitZones.clear();

    AppConfig snap;
    {
        CsLock lk(&gApp.cfgCs);
        snap = gApp.cfg;
    }

    syncPhotoWidgetBitmaps(snap);
    gRenderAccentOverride = snap.customAccentEnabled ? snap.customAccentRgb : -1;
    gRenderHighContrast = snap.highContrast;
    const int monitorIndex = std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1));
    const RECT displayRect = (gApp.monitorCount > 0) ? gApp.monitors[monitorIndex].bounds : RECT{0, 0, gApp.screenW, gApp.screenH};
    const float offsetX = static_cast<float>(displayRect.left - gApp.virtualX);
    const float offsetY = static_cast<float>(displayRect.top - gApp.virtualY);
    const int W = displayRect.right - displayRect.left;
    const int H = displayRect.bottom - displayRect.top;
    const DisplayLayout& displayLayout = snap.displayLayouts[monitorIndex];
    const float fitScale = std::min((H - 16.0f) / (720.0f * gApp.S), (W - 16.0f) / (624.0f * gApp.S));
    const float compactFactor = snap.compactMode ? 0.90f : 1.0f;
    const float S = gApp.S * compactFactor * std::min(static_cast<float>(snap.uiScale) / 100.0f, std::max(0.45f, fitScale));
    gApp.uiScalePx = S;
    gApp.fontScalePx = static_cast<float>(snap.fontScale) / 100.0f;

    // Clear 32-bit Premultiplied ARGB surface
    uint32_t bgPixel = (gApp.menuOpen && snap.noAltTabLock) ? 0x02000000u : 0x00000000u;
    std::fill_n(static_cast<uint32_t*>(gApp.dibBits), static_cast<size_t>(gApp.screenW) * static_cast<size_t>(gApp.screenH), bgPixel);

    Bitmap surface(gApp.screenW, gApp.screenH, gApp.screenW * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(gApp.dibBits));
    Graphics g(&surface);
    g.TranslateTransform(offsetX, offsetY);
    g.SetCompositingMode(CompositingModeSourceOver);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    FontFamily ffSansRaw(L"Segoe UI");
    const FontFamily* pSans = ffSansRaw.IsAvailable() ? &ffSansRaw : FontFamily::GenericSansSerif();
    FontFamily ffMonoRaw(L"Consolas");
    const FontFamily* pMono = ffMonoRaw.IsAvailable() ? &ffMonoRaw : pSans;

    StringFormat sfCenter;
    sfCenter.SetAlignment(StringAlignmentCenter);
    sfCenter.SetLineAlignment(StringAlignmentCenter);

    StringFormat sfLeftVCenter;
    sfLeftVCenter.SetAlignment(StringAlignmentNear);
    sfLeftVCenter.SetLineAlignment(StringAlignmentCenter);

    const BYTE hudAlpha = static_cast<BYTE>(std::clamp(snap.hudOpacity * 255 / 100, 90, 255));

    // User photo widgets sit behind the HUD/menu. In edit mode each photo body can be dragged.
    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        const auto& w = snap.photoWidgets[i];
        if (w.path.empty() || !gApp.photoBitmaps[i]) continue;
        float px = std::clamp(static_cast<float>(w.x), 8.0f, std::max(8.0f, static_cast<float>(W) - 48.0f));
        float py = std::clamp(static_cast<float>(w.y), 8.0f, std::max(8.0f, static_cast<float>(H) - 40.0f));
        float pw = std::clamp(static_cast<float>(w.width), 40.0f, std::max(40.0f, static_cast<float>(W) - px - 8.0f));
        float ph = std::clamp(static_cast<float>(w.height), 40.0f, std::max(40.0f, static_cast<float>(H) - py - 8.0f));
        RectF photoRect(px, py, pw, ph);
        drawCustomBackground(g, gApp.photoBitmaps[i], photoRect, w.opacity, w.fitMode);
        if (gApp.menuOpen) gApp.hitZones.push_back({ photoHitId(i), photoRect });
    }

    // ------------------------------------------------------------------------
    // 1. Top-Right Draggable HUD Status Bar
    // ------------------------------------------------------------------------
    if (snap.showHudBar) {
        float bw = 468.0f * S;
        float bh = 44.0f * S;
        float bx = (displayLayout.hudX < 0) ? (W - bw - 28.0f * S) : static_cast<float>(displayLayout.hudX);
        float by = static_cast<float>(displayLayout.hudY);
        bx = std::clamp(bx, 8.0f, W - bw - 8.0f);
        by = std::clamp(by, 8.0f, H - bh - 8.0f);

        RectF barRect(bx, by, bw, bh);
        Color bBorder = gApp.menuOpen ? Color(hudAlpha, 82, 90, 110) : Color(hudAlpha, 44, 49, 61);
        fillStrokeRoundRect(g, barRect, 7.0f * S, Color(std::min<int>(hudAlpha, 238), 15, 17, 22), bBorder, 1.2f);
        if (gApp.menuOpen) {
            gApp.hitZones.push_back({ HIT_HUD_DRAG, barRect });
        }

        bool firing = gApp.isFiring.load();
        int mCps = gApp.measuredCps.load();
        int cpsVal = (firing && mCps > 0) ? mCps : snap.targetCps;
        int pingVal = snap.serverPingMs;

        const wchar_t* stTxt = L"ACTIVE";
        Color stCol(hudAlpha, 62, 212, 112);
        if (snap.testMode) {
            stTxt = L"TEST";
            stCol = accentStroke(snap.themeIndex, hudAlpha);
        } else if (!snap.macroEnabled) {
            stTxt = L"PAUSED";
            stCol = Color(hudAlpha, 168, 175, 190);
        } else if (firing) {
            stTxt = L"CLASH";
            stCol = accentStroke(snap.themeIndex, hudAlpha);
        }

        wchar_t cpsBuf[24], pingBuf[24], fpsBuf[24];
        swprintf(cpsBuf,  24, L"%d CPS", cpsVal);
        swprintf(pingBuf, 24, L"%d ms",  pingVal);
        swprintf(fpsBuf,  24, L"%d Hz", gApp.monitorHz);

        Font fBrand(pSans, 15.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fStat(pSans,  13.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fNorm(pSans,  13.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);

        struct Seg { const wchar_t* txt; Color col; Font* fnt; float w; };
        Seg segs[5] = {
            { L"ByAzen", Color(hudAlpha, 244, 246, 250), &fBrand, 92.0f * S },
            { stTxt, (!snap.macroEnabled ? Color(hudAlpha, 168, 175, 190) : (firing ? accentStroke(snap.themeIndex, hudAlpha) : Color(hudAlpha, 62, 212, 112))), &fStat, 82.0f * S },
            { cpsBuf,  Color(hudAlpha, 244, 246, 250), &fNorm, 82.0f * S },
            { pingBuf, accentStroke(snap.themeIndex, hudAlpha),   &fStat, 76.0f * S },
            { fpsBuf,  Color(hudAlpha, 244, 246, 250), &fNorm, 82.0f * S },
        };

        Pen divPen(Color(hudAlpha, 46, 51, 64), 1.0f);
        float sx = bx + 6.0f * S;
        for (int i = 0; i < 5; ++i) {
            SolidBrush sb(segs[i].col);
            g.DrawString(segs[i].txt, -1, segs[i].fnt, RectF(sx, by, segs[i].w, bh), &sfCenter, &sb);
            sx += segs[i].w;
            g.DrawLine(&divPen, PointF(sx, by + 12.0f * S), PointF(sx, by + bh - 12.0f * S));
        }

        // 5-dot grip cluster on far right
        float gcx = sx + (bx + bw - sx) * 0.5f;
        float gcy = by + bh * 0.5f;
        SolidBrush dotBr(Color(hudAlpha, 225, 230, 242));
        float offs[5][2] = { {0, 0}, {-5.5f, -5.5f}, {5.5f, -5.5f}, {-5.5f, 5.5f}, {5.5f, 5.5f} };
        float dr = 1.75f * S;
        for (auto& o : offs) {
            g.FillEllipse(&dotBr, RectF(gcx + o[0] * S - dr, gcy + o[1] * S - dr, dr * 2.0f, dr * 2.0f));
        }
    }

    // ------------------------------------------------------------------------
    // 2. Bottom-Right Draggable Keystroke Pill ([F] [M4])
    // ------------------------------------------------------------------------
    if (snap.showKeyPill) {
        float pw = 242.0f * S;
        float ph = 40.0f * S;
        float px = (displayLayout.pillX < 0) ? (W - 274.0f * S) : static_cast<float>(displayLayout.pillX);
        float py = (displayLayout.pillY < 0) ? (H * 0.74f)       : static_cast<float>(displayLayout.pillY);
        px = std::clamp(px, 8.0f, W - pw - 8.0f);
        py = std::clamp(py, 8.0f, H - ph - 8.0f);

        RectF pillRect(px, py, pw, ph);
        bool firing = gApp.isFiring.load();

        Color pBorder = firing ? accentStroke(snap.themeIndex, hudAlpha) : (
            gApp.menuOpen ? Color(hudAlpha, 82, 90, 110) : Color(hudAlpha, 44, 49, 61)
        );
        fillStrokeRoundRect(g, pillRect, 7.0f * S, Color(std::min<int>(hudAlpha, 238), 15, 17, 22), pBorder, 1.2f);
        if (gApp.menuOpen) {
            gApp.hitZones.push_back({ HIT_PILL_DRAG, pillRect });
        }

        Font fPill(pSans, 14.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        const int keyVks[6] = { 0x41, 0x57, 0x53, 0x44, snap.outputVk, snap.triggerVk };
        std::wstring outputLabel = vkToName(snap.outputVk);
        std::wstring triggerLabel = vkToName(snap.triggerVk);
        const wchar_t* keyLabels[6] = { L"A", L"W", L"S", L"D", outputLabel.c_str(), triggerLabel.c_str() };
        float keyW = 34.0f * S;
        float gap = 4.0f * S;
        float total = keyW * 6 + gap * 5;
        float startX = px + (pw - total) * 0.5f;
        for (int i = 0; i < 6; ++i) {
            bool active = isVkPressed(keyVks[i]) || (firing && (i == 4 || i == 5));
            RectF kr(startX + i * (keyW + gap), py + 5.0f * S, keyW, ph - 10.0f * S);
            Color edge = active ? accentStroke(snap.themeIndex) : Color(hudAlpha, 53, 59, 74);
            fillStrokeRoundRect(g, kr, 4.0f * S, active ? accentFill(snap.themeIndex) : Color(std::min<int>(hudAlpha, 230), 22, 25, 32), edge, 1.0f);
            SolidBrush kb(active ? Color(hudAlpha, 255, 255, 255) : Color(hudAlpha, 190, 198, 213));
            g.DrawString(keyLabels[i], -1, &fPill, kr, &sfCenter, &kb);
        }
    }

    // ------------------------------------------------------------------------
    // 3. 4-Slice Minimalist Radial Profile Wheel (Hold [ALT] in-game)
    // ------------------------------------------------------------------------
    if (gApp.radialOpen) {
        float rw = 292.0f * S;
        float rx = W - rw - 56.0f * S;
        float ry = H * 0.50f - rw * 0.5f;
        float cx = rx + rw * 0.5f;
        float cy = ry + rw * 0.5f;
        float rOut = 138.0f * S;
        float rIn  = 50.0f * S;
        float gap  = 3.6f;
        float span = 90.0f - gap;

        SolidBrush ringBg(Color(218, 14, 16, 21));
        Pen ringPen(Color(225, 42, 46, 58), 1.2f);
        g.FillEllipse(&ringBg, RectF(cx - rOut - 4.0f * S, cy - rOut - 4.0f * S, (rOut + 4.0f * S) * 2.0f, (rOut + 4.0f * S) * 2.0f));
        g.DrawEllipse(&ringPen, RectF(cx - rOut - 4.0f * S, cy - rOut - 4.0f * S, (rOut + 4.0f * S) * 2.0f, (rOut + 4.0f * S) * 2.0f));

        // Clockwise GDI+ angles: Top=270, Right=0, Bottom=90, Left=180
        struct SliceSpec { int idx; const wchar_t* name; const char* icon; float deg; int hitId; };
        SliceSpec slices[4] = {
            { 0, L"Low Ping",  "pulse",   270.0f, HIT_RADIAL_0 },
            { 1, L"Clash Pro", "crown",     0.0f, HIT_RADIAL_1 },
            { 2, L"Safe",      "shield",   90.0f, HIT_RADIAL_2 },
            { 3, L"Custom",    "sliders", 180.0f, HIT_RADIAL_3 },
        };

        Font fSliceSel(pSans, 13.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fSlice(pSans,    13.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
        SolidBrush txtBr(Color(255, 244, 246, 250));

        RectF outBox(cx - rOut, cy - rOut, rOut * 2.0f, rOut * 2.0f);
        RectF inBox(cx - rIn,   cy - rIn,  rIn * 2.0f,  rIn * 2.0f);

        for (auto& sl : slices) {
            float startDeg = sl.deg - span * 0.5f;
            GraphicsPath sp;
            sp.AddArc(outBox, startDeg, span);
            sp.AddArc(inBox,  startDeg + span, -span);
            sp.CloseFigure();

            bool sel = (sl.idx == gApp.radialHovered);
            SolidBrush sFill(sel ? accentFill(snap.themeIndex, 238) : Color(240, 24, 27, 34));
            Pen sPen(sel ? accentStroke(snap.themeIndex) : Color(255, 54, 59, 73), sel ? 1.6f : 1.1f);
            g.FillPath(&sFill, &sp);
            g.DrawPath(&sPen, &sp);

            float midR = (rIn + rOut) * 0.5f;
            float rad = static_cast<float>(sl.deg * PI_VAL / 180.0);
            float ix = cx + midR * std::cos(rad);
            float iy = cy + midR * std::sin(rad);

            drawVectorIcon(g, sl.icon, PointF(ix, iy - 10.0f * S), 24.0f * S, Color(255, 244, 246, 250));
            g.DrawString(sl.name, -1, sel ? &fSliceSel : &fSlice,
                         RectF(ix - 48.0f * S, iy + 5.0f * S, 96.0f * S, 22.0f * S), &sfCenter, &txtBr);
        }

        // Center dark circle + Katana vector icon
        float cR = rIn - 6.0f * S;
        SolidBrush cFill(Color(250, 20, 22, 28));
        Pen cPen(Color(255, 52, 57, 70), 1.3f);
        g.FillEllipse(&cFill, RectF(cx - cR, cy - cR, cR * 2.0f, cR * 2.0f));
        g.DrawEllipse(&cPen,  RectF(cx - cR, cy - cR, cR * 2.0f, cR * 2.0f));
        drawVectorIcon(g, "sword", PointF(cx, cy), 34.0f * S, Color(255, 244, 246, 250));
    }

    // ------------------------------------------------------------------------
    // 4. Main ByAzen Settings Panel (5 Tabs) + Bottom Edit-Mode Banner
    // ------------------------------------------------------------------------
    if (gApp.menuOpen) {
        float mw = 624.0f * S;
        float mh = 720.0f * S;
        float mx = std::clamp(static_cast<float>(displayLayout.menuX), 8.0f, W - mw - 8.0f);
        float my = std::clamp(static_cast<float>(displayLayout.menuY), 8.0f, H - mh - 8.0f);
        float headerH = 68.0f * S;
        float sideW   = 168.0f * S;

        RectF winRect(mx, my, mw, mh);
        fillStrokeRoundRect(g, RectF(mx + 1.0f * S, my + 7.0f * S, mw, mh), 12.0f * S,
                            Color(100, 0, 0, 0), Color(0, 0, 0, 0), 0.0f);
        fillStrokeRoundRect(g, winRect, 12.0f * S, Color(249, 13, 17, 26), Color(255, 53, 62, 82), 1.2f);
        Bitmap* panelArtwork = gApp.customBackground ? gApp.customBackground : gApp.defaultBackground;
        drawCustomBackground(g, panelArtwork, winRect,
                             gApp.customBackground ? snap.backgroundOpacity : 68,
                             gApp.customBackground ? snap.backgroundFit : 0);
        RectF sidebarRect(mx + 1.0f * S, my + headerH, sideW, mh - headerH - 1.0f * S);
        LinearGradientBrush sidebarBrush(PointF(sidebarRect.X, sidebarRect.Y),
                                         PointF(sidebarRect.X + sidebarRect.Width, sidebarRect.Y + sidebarRect.Height),
                                         Color(gRenderHighContrast ? 250 : 226, 17, 21, 31), Color(gRenderHighContrast ? 250 : 226, 12, 15, 22));
        g.FillRectangle(&sidebarBrush, sidebarRect);
        RectF contentCanvas(mx + sideW + 9.0f * S, my + headerH + 8.0f * S,
                            mw - sideW - 18.0f * S, mh - headerH - 16.0f * S);
        LinearGradientBrush contentBrush(PointF(contentCanvas.X, contentCanvas.Y),
                                         PointF(contentCanvas.X + contentCanvas.Width, contentCanvas.Y + contentCanvas.Height),
                                         Color(gRenderHighContrast ? 250 : 218, 20, 24, 34), Color(gRenderHighContrast ? 250 : 218, 16, 19, 27));
        GraphicsPath contentPath;
        makeRoundRectPath(contentPath, contentCanvas, 9.0f * S);
        g.FillPath(&contentBrush, &contentPath);
        Pen contentEdge(Color(255, 34, 40, 54), 1.0f * S);
        g.DrawPath(&contentEdge, &contentPath);
        // Animated accent rail and a moving glint: subtle, inexpensive, and theme-aware.
        const double phase = snap.animationsEnabled ? qpcSeconds() : 0.0;
        const BYTE pulse = static_cast<BYTE>(150 + 70 * (0.5 + 0.5 * std::sin(phase * 2.4)));
        fillStrokeRoundRect(g, RectF(mx + 18.0f * S, my + 1.0f * S, mw - 36.0f * S, 2.0f * S),
                            1.0f * S, accentStroke(snap.themeIndex, 70), Color(0, 0, 0, 0), 0.0f);
        float railW = 92.0f * S;
        float travel = std::max(1.0f, mw - railW - 36.0f * S);
        float railX = mx + 18.0f * S + static_cast<float>(std::fmod(phase * 90.0, travel));
        fillStrokeRoundRect(g, RectF(railX, my + 0.5f * S, railW, 3.0f * S),
                            1.5f * S, accentStroke(snap.themeIndex, pulse), Color(0, 0, 0, 0), 0.0f);

        Pen divPen(Color(255, 36, 40, 50), 1.0f);
        g.DrawLine(&divPen, PointF(mx + 1.0f, my + headerH), PointF(mx + mw - 1.0f, my + headerH));
        g.DrawLine(&divPen, PointF(mx + sideW, my + headerH), PointF(mx + sideW, my + mh - 1.0f));

        // ---- Header ----
        Font fTitle(pSans, 24.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fMeta(pSans, 10.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        SolidBrush whiteBr(Color(255, 244, 246, 250));
        RectF logoRect(mx + 17.0f * S, my + 18.0f * S, 32.0f * S, 32.0f * S);
        fillStrokeRoundRect(g, logoRect, 9.0f * S, accentFill(snap.themeIndex, 235), accentStroke(snap.themeIndex, 235), 1.0f);
        drawVectorIcon(g, "sword", PointF(logoRect.X + logoRect.Width * 0.5f, logoRect.Y + logoRect.Height * 0.5f),
                       19.0f * S, Color(255, 250, 251, 255));
        g.DrawString(L"ByAzen", -1, &fTitle, RectF(mx + 58.0f * S, my, 88.0f * S, headerH), &sfLeftVCenter, &whiteBr);

        std::wstring menuKeyName = hotkeyToName(snap.menuVk, snap.menuMods);
        std::wstring badgeTxt = L"[" + menuKeyName + L"]  OVERLAY";
        Font fMonoBadge(pMono, 11.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        RectF badgeRect(mx + 151.0f * S, my + (headerH - 28.0f * S) * 0.5f, 114.0f * S, 28.0f * S);
        fillStrokeRoundRect(g, badgeRect, 4.0f * S, Color(255, 23, 26, 33), Color(255, 50, 55, 70), 1.0f);
        SolidBrush badgeBr(Color(255, 232, 236, 244));
        g.DrawString(badgeTxt.c_str(), -1, &fMonoBadge, badgeRect, &sfCenter, &badgeBr);
        const wchar_t* stateLabel = snap.testMode ? L"ТЕСТОВЫЙ РЕЖИМ" :
                                   (snap.macroEnabled ? L"МАКРОС ВКЛЮЧЁН" : L"МАКРОС НА ПАУЗЕ");
        Color stateColor = snap.testMode ? Color(255, 235, 190, 92) :
                           (snap.macroEnabled ? Color(255, 86, 218, 157) : Color(255, 166, 176, 194));
        Color stateFill = snap.testMode ? Color(255, 52, 42, 25) :
                          (snap.macroEnabled ? Color(255, 22, 49, 39) : Color(255, 31, 35, 44));
        Color stateEdge = snap.testMode ? Color(255, 119, 91, 43) :
                          (snap.macroEnabled ? Color(255, 52, 111, 83) : Color(255, 57, 64, 79));
        SolidBrush metaBr(Color(255, 154, 164, 184));
        Font fMetaValue(pSans, 10.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        RectF metaRect(mx + 277.0f * S, my + 12.0f * S, 145.0f * S, 20.0f * S);
        g.DrawString(L"BYAZEN  /  3.6.0", -1, &fMeta, metaRect, &sfLeftVCenter, &metaBr);
        RectF stateRect(mx + 277.0f * S, my + 31.0f * S, 157.0f * S, 25.0f * S);
        fillStrokeRoundRect(g, stateRect, 5.0f * S, stateFill, stateEdge, 1.0f * S);
        fillStrokeRoundRect(g, RectF(stateRect.X + 8.0f * S, stateRect.Y + 9.0f * S, 7.0f * S, 7.0f * S),
                            3.5f * S, stateColor, Color(0, 0, 0, 0), 0.0f);
        SolidBrush stateBr(stateColor);
        g.DrawString(stateLabel, -1, &fMetaValue,
                     RectF(stateRect.X + 20.0f * S, stateRect.Y + 1.0f * S, stateRect.Width - 25.0f * S, stateRect.Height - 2.0f * S),
                     &sfLeftVCenter, &stateBr);

        // Close-to-game button & Quit button
        std::wstring backTxt = L"В игру [" + menuKeyName + L"]";
        RectF btnBackRect(mx + mw - 166.0f * S, my + (headerH - 28.0f * S) * 0.5f, 112.0f * S, 28.0f * S);
        bool backHov = (gApp.hoveredHitId == HIT_BTN_CLOSE_MENU);
        fillStrokeRoundRect(g, btnBackRect, 5.0f * S,
                            backHov ? Color(255, 38, 43, 56) : Color(255, 27, 31, 40),
                            Color(255, 48, 53, 68), 1.0f);
        Font fBtnSm(pSans, 12.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        g.DrawString(backTxt.c_str(), -1, &fBtnSm, btnBackRect, &sfCenter, &whiteBr);

        RectF btnQuitRect(mx + mw - 46.0f * S, my + (headerH - 28.0f * S) * 0.5f, 28.0f * S, 28.0f * S);
        bool quitHov = (gApp.hoveredHitId == HIT_BTN_QUIT_APP);
        fillStrokeRoundRect(g, btnQuitRect, 5.0f * S,
                            quitHov ? Color(255, 74, 24, 30) : Color(255, 24, 27, 34),
                            quitHov ? accentStroke(snap.themeIndex) : Color(255, 44, 49, 62), 1.0f);
        Font fQuit(pSans, 17.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        SolidBrush quitTxtBr(quitHov ? Color(255, 255, 255, 255) : Color(255, 165, 173, 190));
        g.DrawString(L"×", -1, &fQuit, btnQuitRect, &sfCenter, &quitTxtBr);

        // Register buttons BEFORE header drag so buttons get click priority
        gApp.hitZones.push_back({ HIT_BTN_CLOSE_MENU, btnBackRect });
        gApp.hitZones.push_back({ HIT_BTN_QUIT_APP,   btnQuitRect });
        gApp.hitZones.push_back({ HIT_HEADER_DRAG,    RectF(mx, my, mw, headerH) });

        // ---- Left Sidebar Tabs ----
        struct TabDef { const wchar_t* label; const char* icon; int hitId; };
        TabDef tabs[7] = {
            { L"Макрос",    "sliders",  HIT_TAB_0 },
            { L"Ввод",      "target",   HIT_TAB_1 },
            { L"Оверлей",   "grid",     HIT_TAB_2 },
            { L"Профили",   "profile",  HIT_TAB_3 },
            { L"Клавиши",   "keyboard", HIT_TAB_4 },
            { L"Сервис",    "sliders",  HIT_TAB_5 },
            { L"Фото",      "grid",     HIT_TAB_6 },
        };

        Font fTabAct(pSans, 14.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fTab(pSans,    14.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
        for (int i = 0; i < 7; ++i) {
            RectF tr(mx + 12.0f * S, my + headerH + 13.0f * S + i * 45.0f * S, sideW - 24.0f * S, 39.0f * S);
            bool active = (gApp.currentTab == i);
            bool hov    = (gApp.hoveredHitId == tabs[i].hitId);

            if (active) {
                fillStrokeRoundRect(g, tr, 7.0f * S, accentFill(snap.themeIndex, 70), accentStroke(snap.themeIndex, 48), 0.8f * S);
                fillStrokeRoundRect(g, RectF(tr.X, tr.Y + 8.0f * S, 3.0f * S, tr.Height - 16.0f * S), 1.5f * S,
                                    accentStroke(snap.themeIndex), Color(0, 0, 0, 0), 0.0f);
            } else if (hov) {
                fillStrokeRoundRect(g, tr, 6.0f * S, Color(255, 26, 29, 37), Color(0, 0, 0, 0), 0.0f);
            }

            Color icCol = (active || hov) ? Color(255, 244, 246, 250) : Color(255, 198, 204, 216);
            drawVectorIcon(g, tabs[i].icon, PointF(tr.X + 24.0f * S, tr.Y + tr.Height * 0.5f), 18.0f * S, icCol);
            SolidBrush tb(icCol);
            g.DrawString(tabs[i].label, -1, active ? &fTabAct : &fTab,
                         RectF(tr.X + 46.0f * S, tr.Y, tr.Width - 50.0f * S, tr.Height), &sfLeftVCenter, &tb);

            gApp.hitZones.push_back({ tabs[i].hitId, tr });
        }

        // Session mini-card: elapsed time and completed macro cycles. Click to reset.
        RectF sessionCard(mx + 12.0f * S, my + mh - 106.0f * S, sideW - 24.0f * S, 82.0f * S);
        bool sessionHover = gApp.hoveredHitId == HIT_BTN_RESET_SESSION;
        fillStrokeRoundRect(g, sessionCard, 8.0f * S,
                            sessionHover ? Color(255, 29, 35, 49) : Color(255, 20, 24, 34),
                            sessionHover ? accentStroke(snap.themeIndex, 180) : Color(255, 39, 46, 62), 1.0f);
        Font fSessionLabel(pSans, 10.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fSessionValue(pMono, 16.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fSessionHint(pSans, 9.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
        SolidBrush sessionDim(Color(255, 148, 159, 180));
        SolidBrush sessionAccent(accentStroke(snap.themeIndex));
        g.DrawString(L"СЕССИЯ  •  НАЖМИ СБРОС", -1, &fSessionLabel,
                     RectF(sessionCard.X + 10.0f * S, sessionCard.Y + 6.0f * S, sessionCard.Width - 18.0f * S, 17.0f * S),
                     &sfLeftVCenter, &sessionDim);
        const int sessionElapsed = std::max(0, static_cast<int>(qpcSeconds() - gApp.sessionStartedAt));
        const uint64_t sessionCycles = gApp.sessionCycles.load(std::memory_order_relaxed);
        wchar_t sessionTime[16], sessionCount[32];
        swprintf(sessionTime, 16, L"%02d:%02d", sessionElapsed / 60, sessionElapsed % 60);
        swprintf(sessionCount, 32, L"%llu", static_cast<unsigned long long>(sessionCycles));
        g.DrawString(sessionTime, -1, &fSessionValue,
                     RectF(sessionCard.X + 10.0f * S, sessionCard.Y + 24.0f * S, 72.0f * S, 24.0f * S),
                     &sfLeftVCenter, &sessionAccent);
        g.DrawString(sessionCount, -1, &fSessionValue,
                     RectF(sessionCard.X + 87.0f * S, sessionCard.Y + 24.0f * S, sessionCard.Width - 96.0f * S, 24.0f * S),
                     &sfLeftVCenter, &whiteBr);
        g.DrawString(L"ВРЕМЯ  •  ЦИКЛЫ", -1, &fSessionHint,
                     RectF(sessionCard.X + 10.0f * S, sessionCard.Y + 53.0f * S, sessionCard.Width - 18.0f * S, 16.0f * S),
                     &sfLeftVCenter, &sessionDim);
        gApp.hitZones.push_back({ HIT_BTN_RESET_SESSION, sessionCard });

        // ---- Right Content Area ----
        float cx0 = mx + sideW + 24.0f * S;
        float cy0 = my + headerH + 62.0f * S;
        float cw  = mw - sideW - 50.0f * S;
        const wchar_t* pageTitles[7] = {
            L"КОНТРОЛЬ МАКРОСА", L"ПАРАМЕТРЫ ВВОДА", L"ВНЕШНИЙ ВИД",
            L"ПРОФИЛИ", L"СОЧЕТАНИЯ КЛАВИШ", L"ИНСТРУМЕНТЫ И ДИАГНОСТИКА", L"ФОТО-ВИДЖЕТЫ"
        };
        const wchar_t* pageSubtitles[7] = {
            L"Поведение, темп и безопасный режим", L"Выход и условия срабатывания",
            L"Тема, фон и расположение виджетов", L"Сохраняй и переключай пресеты",
            L"Глобальные клавиши и проверка конфликтов", L"Локальные данные • восстановление • отчёты",
            L"До 8 изображений • размер и положение мышью"
        };
        const int pageIndex = std::clamp(gApp.currentTab, 0, 6);
        Font fPageTitle(pSans, 16.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fPageSubtitle(pSans, 10.5f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
        SolidBrush pageTitleBr(gRenderHighContrast ? Color(255, 255, 255, 255) : Color(255, 245, 247, 252));
        SolidBrush pageSubtitleBr(gRenderHighContrast ? Color(255, 218, 225, 238) : Color(255, 150, 160, 180));
        fillStrokeRoundRect(g, RectF(cx0, my + headerH + 14.0f * S, 3.0f * S, 34.0f * S),
                            1.5f * S, accentStroke(snap.themeIndex), Color(0, 0, 0, 0), 0.0f);
        g.DrawString(pageTitles[pageIndex], -1, &fPageTitle,
                     RectF(cx0 + 12.0f * S, my + headerH + 10.0f * S, cw - 12.0f * S, 25.0f * S),
                     &sfLeftVCenter, &pageTitleBr);
        g.DrawString(pageSubtitles[pageIndex], -1, &fPageSubtitle,
                     RectF(cx0 + 12.0f * S, my + headerH + 34.0f * S, cw - 12.0f * S, 22.0f * S),
                     &sfLeftVCenter, &pageSubtitleBr);

        Font fRow(pSans, 14.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fSub(pSans, 13.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        Font fHint(pSans, 12.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
        SolidBrush subBr(Color(255, 200, 207, 222));
        SolidBrush dimBr(gRenderHighContrast ? Color(255, 230, 235, 245) : Color(255, 155, 163, 182));

        auto drawToggleRow = [&](float ry, const std::wstring& label, bool val, int hitId) {
            g.DrawString(label.c_str(), -1, &fRow, RectF(cx0, ry, cw - 56.0f * S, 34.0f * S), &sfLeftVCenter, &whiteBr);
            RectF sw(cx0 + cw - 46.0f * S, ry + 5.0f * S, 46.0f * S, 24.0f * S);
            drawToggleSwitch(g, sw, val);
            gApp.hitZones.push_back({ hitId, RectF(cx0, ry, cw, 34.0f * S) });
        };

        if (gApp.currentTab == 0) {
            // === TAB 0: MACRO (Exact 1:1 Concept Layout) ===
            std::wstring mTitle = L"Clash Macro [" + vkToName(snap.triggerVk) + L"]";
            drawToggleRow(cy0, mTitle, snap.macroEnabled, HIT_TOG_MACRO);

            RectF rCps(cx0, cy0 + 52.0f * S, cw, 50.0f * S);
            wchar_t cpsTitle[64];
            swprintf(cpsTitle, 64, L"Target CPS (до %d, колесо ±1)", CPS_MAX);
            drawLabeledSlider(g, pSans, rCps, cpsTitle, snap.targetCps, CPS_MIN, CPS_MAX, L"", S);
            gApp.hitZones.push_back({ HIT_SLD_CPS, rCps, CPS_MIN, CPS_MAX });

            RectF rDel(cx0, cy0 + 118.0f * S, cw, 50.0f * S);
            drawLabeledSlider(g, pSans, rDel, L"Пауза цикла", snap.delayMs, 1, 120, L" ms", S);
            gApp.hitZones.push_back({ HIT_SLD_DELAY, rDel, 1, 120 });

            RectF rPing(cx0, cy0 + 174.0f * S, cw, 46.0f * S);
            drawLabeledSlider(g, pSans, rPing, L"Ручной пинг сервера", snap.serverPingMs, 0, 300, L" ms", S);
            gApp.hitZones.push_back({ HIT_SLD_PING, rPing, 0, 300 });
            drawToggleRow(cy0 + 226.0f * S, L"Ping Sync", snap.pingSync, HIT_TOG_PING_SYNC);
            drawToggleRow(cy0 + 268.0f * S, L"No Alt-Tab Lock", snap.noAltTabLock, HIT_TOG_NO_ALTTAB);
            drawToggleRow(cy0 + 308.0f * S, L"Тестовый режим (без ввода)", snap.testMode, HIT_TOG_TEST_MODE);
            drawToggleRow(cy0 + 348.0f * S, L"Точное ожидание (выше нагрузка)", snap.preciseTiming, HIT_TOG_PRECISE_TIMING);
            drawToggleRow(cy0 + 388.0f * S, L"Анимации интерфейса", snap.animationsEnabled, HIT_TOG_ANIMATIONS);
            RectF stopRect(cx0, cy0 + 428.0f * S, cw, 34.0f * S);
            bool stopHover = (gApp.hoveredHitId == HIT_BTN_EMERGENCY_STOP);
            fillStrokeRoundRect(g, stopRect, 6.0f * S, stopHover ? Color(255, 115, 28, 39) : Color(255, 58, 24, 30),
                                accentStroke(snap.themeIndex), 1.2f);
            g.DrawString(L"АВАРИЙНАЯ ПАУЗА", -1, &fSub, stopRect, &sfCenter, &whiteBr);
            gApp.hitZones.push_back({ HIT_BTN_EMERGENCY_STOP, stopRect });
        }
        else if (gApp.currentTab == 1) {
            // === TAB 1: CLASH ===
            g.DrawString(L"Что спамит макрос (быстрый выбор):", -1, &fSub, RectF(cx0, cy0 - 4.0f * S, cw, 22.0f * S), &sfLeftVCenter, &subBr);

            RectF seg1(cx0, cy0 + 22.0f * S, cw, 32.0f * S);
            fillStrokeRoundRect(g, seg1, 6.0f * S, Color(255, 23, 26, 33), Color(255, 44, 49, 61), 1.0f);
            int curOutIdx = (snap.outputVk == 0x46) ? 0 : ((snap.outputVk == 0x01) ? 1 : 2);
            const wchar_t* outLabels[3] = { L"Клавиша [F]", L"Клик [LMB]", L"Своя кнопка" };
            int outHits[3] = { HIT_SEG_OUT_F, HIT_SEG_OUT_LMB, HIT_SEG_OUT_CUSTOM };
            float sw3 = (cw - 4.0f * S) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                RectF sr(cx0 + 2.0f * S + i * sw3, seg1.Y + 2.0f * S, sw3, seg1.Height - 4.0f * S);
                if (i == curOutIdx) {
                    fillStrokeRoundRect(g, sr, 4.5f * S, accentFill(snap.themeIndex, 235), accentStroke(snap.themeIndex), 1.0f);
                    g.DrawString(outLabels[i], -1, &fBtnSm, sr, &sfCenter, &whiteBr);
                } else {
                    g.DrawString(outLabels[i], -1, &fBtnSm, sr, &sfCenter, &dimBr);
                }
                gApp.hitZones.push_back({ outHits[i], sr });
            }

            g.DrawString(L"Режим активации кнопки:", -1, &fSub, RectF(cx0, cy0 + 62.0f * S, cw, 22.0f * S), &sfLeftVCenter, &subBr);
            RectF seg2(cx0, cy0 + 88.0f * S, cw, 32.0f * S);
            fillStrokeRoundRect(g, seg2, 6.0f * S, Color(255, 23, 26, 33), Color(255, 44, 49, 61), 1.0f);
            const wchar_t* modeLabels[2] = { L"Удержание (Hold)", L"По нажатию (Toggle)" };
            int modeHits[2] = { HIT_SEG_MODE_HOLD, HIT_SEG_MODE_TOGGLE };
            float sw2 = (cw - 4.0f * S) * 0.5f;
            for (int i = 0; i < 2; ++i) {
                RectF sr(cx0 + 2.0f * S + i * sw2, seg2.Y + 2.0f * S, sw2, seg2.Height - 4.0f * S);
                bool active = (snap.toggleMode ? 1 : 0) == i;
                if (active) {
                    fillStrokeRoundRect(g, sr, 4.5f * S, accentFill(snap.themeIndex, 235), accentStroke(snap.themeIndex), 1.0f);
                    g.DrawString(modeLabels[i], -1, &fBtnSm, sr, &sfCenter, &whiteBr);
                } else {
                    g.DrawString(modeLabels[i], -1, &fBtnSm, sr, &sfCenter, &dimBr);
                }
                gApp.hitZones.push_back({ modeHits[i], sr });
            }

            RectF rHold(cx0, cy0 + 130.0f * S, cw, 48.0f * S);
            drawLabeledSlider(g, pSans, rHold, L"Удержание клика", snap.holdMs, 1, 20, L" ms", S);
            gApp.hitZones.push_back({ HIT_SLD_HOLD, rHold, 1, 20 });

            RectF rJit(cx0, cy0 + 186.0f * S, cw, 48.0f * S);
            drawLabeledSlider(g, pSans, rJit, L"Рандомизация (Jitter)", snap.jitterPct, 0, 20, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_JITTER, rJit, 0, 20 });

            drawToggleRow(cy0 + 244.0f * S, L"Только в окне Roblox", snap.onlyInRoblox, HIT_TOG_ONLY_ROBLOX);
            drawToggleRow(cy0 + 282.0f * S, L"Пауза в чате [/]", snap.chatSafePause, HIT_TOG_CHAT_SAFE);
            drawToggleRow(cy0 + 316.0f * S, L"Двойной выход: F + ЛКМ", snap.dualOutput, HIT_TOG_DUAL_OUTPUT);
            drawToggleRow(cy0 + 350.0f * S, L"Burst-старт 200+ CPS / 350 мс", snap.burstEnabled, HIT_TOG_BURST);
        }
        else if (gApp.currentTab == 2) {
            // === TAB 2: WIDGETS ===
            drawToggleRow(cy0, L"Верхний статус-бар ByAzen", snap.showHudBar, HIT_TOG_SHOW_HUD);
            drawToggleRow(cy0 + 40.0f * S, L"Индикатор клавиш", snap.showKeyPill, HIT_TOG_SHOW_PILL);
            drawToggleRow(cy0 + 80.0f * S, L"Игровые уведомления + звук", snap.toastEnabled, HIT_TOG_TOAST);

            std::wstring themeNames[5] = { L"Красный", L"Фиолетовый", L"Голубой", L"Изумруд", L"Белый" };
            RectF themeRect(cx0, cy0 + 124.0f * S, cw, 34.0f * S);
            g.DrawString(L"Цветовая тема", -1, &fRow, RectF(cx0, themeRect.Y, cw - 190.0f * S, themeRect.Height), &sfLeftVCenter, &whiteBr);
            RectF themeBtn(cx0 + cw - 184.0f * S, themeRect.Y, 105.0f * S, themeRect.Height);
            fillStrokeRoundRect(g, themeBtn, 5.0f * S, Color(255, 25, 29, 38), accentStroke(snap.themeIndex), 1.2f);
            g.DrawString(themeNames[snap.themeIndex].c_str(), -1, &fBtnSm, themeBtn, &sfCenter, &whiteBr);
            gApp.hitZones.push_back({ HIT_BTN_THEME, themeBtn });
            RectF customAccentBtn(cx0 + cw - 72.0f * S, themeRect.Y, 72.0f * S, themeRect.Height);
            fillStrokeRoundRect(g, customAccentBtn, 5.0f * S,
                                snap.customAccentEnabled ? Color(255, GetRValue(snap.customAccentRgb), GetGValue(snap.customAccentRgb), GetBValue(snap.customAccentRgb)) : Color(255, 26, 30, 40),
                                snap.customAccentEnabled ? Color(255, 245, 247, 255) : Color(255, 54, 61, 79), 1.0f);
            g.DrawString(L"Своя…", -1, &fBtnSm, customAccentBtn, &sfCenter, &whiteBr);
            gApp.hitZones.push_back({ HIT_BTN_CUSTOM_ACCENT, customAccentBtn });

            RectF rScale(cx0, cy0 + 166.0f * S, cw, 48.0f * S);
            drawLabeledSlider(g, pSans, rScale, L"Масштаб интерфейса", snap.uiScale, 75, 125, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_UI_SCALE, rScale, 75, 125 });
            RectF rOp(cx0, cy0 + 218.0f * S, cw, 48.0f * S);
            drawLabeledSlider(g, pSans, rOp, L"Прозрачность HUD", snap.hudOpacity, 40, 100, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_OPACITY, rOp, 40, 100 });

            RectF infoCard(cx0, cy0 + 272.0f * S, cw, 48.0f * S);
            fillStrokeRoundRect(g, infoCard, 6.0f * S, Color(255, 22, 25, 32), Color(255, 38, 43, 54), 1.0f);
            std::wstring displayInfo = L"Выбран: " + gApp.monitorLabel + L" • раскладка сохраняется для каждого дисплея.";
            g.DrawString(displayInfo.c_str(), -1, &fHint,
                         RectF(infoCard.X + 10.0f * S, infoCard.Y + 4.0f * S, infoCard.Width - 20.0f * S, infoCard.Height - 8.0f * S), &sfCenter, &dimBr);

            RectF btnRes(cx0, cy0 + 326.0f * S, cw, 34.0f * S);
            bool resHov = (gApp.hoveredHitId == HIT_BTN_RESET_POS);
            fillStrokeRoundRect(g, btnRes, 6.0f * S,
                                resHov ? Color(255, 41, 47, 61) : Color(255, 30, 34, 44),
                                resHov ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72), 1.0f);
            g.DrawString(L"Сбросить позиции виджетов", -1, &fSub, btnRes, &sfCenter, &whiteBr);
            gApp.hitZones.push_back({ HIT_BTN_RESET_POS, btnRes });

            const wchar_t* utilLabels[5] = { L"Backup", L"Экспорт", L"Импорт", L"Сброс", L"Дисплей ↻" };
            const int utilHits[5] = { HIT_BTN_CONFIG_BACKUP, HIT_BTN_CONFIG_EXPORT, HIT_BTN_CONFIG_IMPORT, HIT_BTN_RESET_DEFAULTS, HIT_BTN_NEXT_MONITOR };
            float utilGap = 5.0f * S;
            float utilW = (cw - utilGap * 4.0f) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                RectF ur(cx0 + i * (utilW + utilGap), cy0 + 372.0f * S, utilW, 34.0f * S);
                bool uh = (gApp.hoveredHitId == utilHits[i]);
                fillStrokeRoundRect(g, ur, 5.0f * S, uh ? accentFill(snap.themeIndex, 210) : Color(255, 25, 29, 38),
                                    uh ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72), 1.0f);
                g.DrawString(utilLabels[i], -1, &fHint, ur, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ utilHits[i], ur });
            }

            RectF bgChoose(cx0, cy0 + 412.0f * S, 178.0f * S, 34.0f * S);
            RectF bgFit(cx0 + 184.0f * S, cy0 + 412.0f * S, 125.0f * S, 34.0f * S);
            RectF bgReset(cx0 + 315.0f * S, cy0 + 412.0f * S, cw - 315.0f * S, 34.0f * S);
            auto drawSmallAction = [&](RectF r, const wchar_t* label, int id) {
                bool hov = gApp.hoveredHitId == id;
                fillStrokeRoundRect(g, r, 5.0f * S, hov ? accentFill(snap.themeIndex, 165) : Color(255, 25, 29, 38),
                                    hov ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72), 1.0f);
                g.DrawString(label, -1, &fHint, r, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ id, r });
            };
            drawSmallAction(bgChoose, L"Выбрать фон…", HIT_BTN_CHOOSE_BACKGROUND);
            const wchar_t* fitNames[3] = { L"Заполнить", L"Вписать", L"Растянуть" };
            drawSmallAction(bgFit, fitNames[std::clamp(snap.backgroundFit, 0, 2)], HIT_BTN_BACKGROUND_FIT);
            drawSmallAction(bgReset, L"Сброс", HIT_BTN_RESET_BACKGROUND);
            std::wstring bgName = snap.backgroundPath.empty() ? L"Фон не выбран" : snap.backgroundPath;
            size_t slash = bgName.find_last_of(L"\\/");
            if (slash != std::wstring::npos) bgName = bgName.substr(slash + 1);
            if (bgName.size() > 52) bgName = L"…" + bgName.substr(bgName.size() - 49);
            g.DrawString(bgName.c_str(), -1, &fHint, RectF(cx0, cy0 + 448.0f * S, cw, 22.0f * S), &sfLeftVCenter, &dimBr);
            RectF bgOpacity(cx0, cy0 + 468.0f * S, cw, 46.0f * S);
            drawLabeledSlider(g, pSans, bgOpacity, L"Прозрачность своего фона", snap.backgroundOpacity, 0, 100, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_BACKGROUND_OPACITY, bgOpacity, 0, 100 });
            RectF rFont(cx0, cy0 + 518.0f * S, cw, 46.0f * S);
            drawLabeledSlider(g, pSans, rFont, L"Размер шрифта", snap.fontScale, 85, 115, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_FONT_SCALE, rFont, 85, 115 });


        }
        else if (gApp.currentTab == 6) {
            // === TAB 6: PHOTO WIDGETS ===
            const int selectedPhoto = std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1);
            const auto& selectedWidget = snap.photoWidgets[selectedPhoto];
            g.DrawString(L"До 8 фото поверх игры. Выбери слот, добавь PNG / JPG / BMP.", -1, &fSub,
                         RectF(cx0, cy0 - 3.0f * S, cw, 24.0f * S), &sfLeftVCenter, &subBr);

            const wchar_t* fitLabels[3] = { L"Заполнить", L"Вписать", L"Растянуть" };
            RectF photoAdd(cx0, cy0 + 34.0f * S, cw * 0.48f, 34.0f * S);
            RectF photoDelete(photoAdd.X + photoAdd.Width + 5.0f * S, photoAdd.Y,
                              cw * 0.19f, photoAdd.Height);
            RectF photoFit(photoDelete.X + photoDelete.Width + 5.0f * S, photoAdd.Y,
                           cw - (photoDelete.X - cx0) - photoDelete.Width - 5.0f * S, photoAdd.Height);
            auto drawPhotoAction = [&](RectF r, const wchar_t* label, int id) {
                const bool hov = gApp.hoveredHitId == id;
                fillStrokeRoundRect(g, r, 5.0f * S,
                    hov ? accentFill(snap.themeIndex, 205) : Color(255, 25, 29, 38),
                    hov ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72), 1.0f);
                g.DrawString(label, -1, &fHint, r, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ id, r });
            };
            drawPhotoAction(photoAdd, L"Добавить / заменить фото", PHOTO_ADD_HIT);
            drawPhotoAction(photoDelete, L"Удалить", PHOTO_DELETE_HIT);
            drawPhotoAction(photoFit, fitLabels[std::clamp(selectedWidget.fitMode, 0, 2)], PHOTO_FIT_HIT);

            const float slotGap = 5.0f * S;
            const float slotW = (cw - slotGap * 3.0f) * 0.25f;
            for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
                const int row = i / 4;
                const int col = i % 4;
                RectF slotRect(cx0 + col * (slotW + slotGap), cy0 + (82.0f + row * 38.0f) * S,
                               slotW, 32.0f * S);
                const bool active = selectedPhoto == i;
                const bool filled = !snap.photoWidgets[i].path.empty();
                const bool hov = gApp.hoveredHitId == PHOTO_SLOT_HIT_BASE + i;
                fillStrokeRoundRect(g, slotRect, 5.0f * S,
                    active ? accentFill(snap.themeIndex, 220) :
                    (hov ? Color(255, 37, 42, 55) : Color(255, 23, 27, 36)),
                    active ? accentStroke(snap.themeIndex) : Color(255, 48, 54, 69), active ? 1.4f : 1.0f);
                std::wstring slotLabel = std::to_wstring(i + 1) + (filled ? L"  • Фото" : L"  + пусто");
                g.DrawString(slotLabel.c_str(), -1, &fHint, slotRect, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ PHOTO_SLOT_HIT_BASE + i, slotRect });
            }

            std::wstring photoName = selectedWidget.path.empty() ? L"Слот пуст — нажми «Добавить / заменить фото»" : selectedWidget.path;
            const size_t photoSlash = photoName.find_last_of(L"\\/");
            if (photoSlash != std::wstring::npos) photoName = photoName.substr(photoSlash + 1);
            if (photoName.size() > 56) photoName = L"…" + photoName.substr(photoName.size() - 53);
            g.DrawString(photoName.c_str(), -1, &fHint,
                         RectF(cx0, cy0 + 164.0f * S, cw, 22.0f * S), &sfLeftVCenter, &dimBr);

            RectF opacityRect(cx0, cy0 + 194.0f * S, cw, 48.0f * S);
            drawLabeledSlider(g, pSans, opacityRect, L"Прозрачность выбранного фото", selectedWidget.opacity, 0, 100, L"%", S);
            gApp.hitZones.push_back({ HIT_SLD_PHOTO_OPACITY, opacityRect, 0, 100 });

            std::wstring dims = L"Размер: " + std::to_wstring(selectedWidget.width) + L" × " +
                                std::to_wstring(selectedWidget.height) + L" px  •  позиция " +
                                std::to_wstring(selectedWidget.x) + L", " + std::to_wstring(selectedWidget.y);
            g.DrawString(dims.c_str(), -1, &fHint,
                         RectF(cx0, cy0 + 250.0f * S, cw, 22.0f * S), &sfLeftVCenter, &dimBr);

            RectF instructions(cx0, cy0 + 284.0f * S, cw, 70.0f * S);
            fillStrokeRoundRect(g, instructions, 6.0f * S, Color(255, 22, 25, 32), Color(255, 41, 47, 61), 1.0f);
            const std::wstring howto = L"Когда меню открыто: тяни фото за центр, чтобы переместить.\n"
                                       L"Потяни любой из 8 маркеров — угол или сторону — для свободного изменения ширины и высоты.\n"
                                       L"Размер и положение сохраняются автоматически.";
            g.DrawString(howto.c_str(), -1, &fHint,
                         RectF(instructions.X + 10.0f * S, instructions.Y + 5.0f * S,
                               instructions.Width - 20.0f * S, instructions.Height - 10.0f * S), &sfLeftVCenter, &whiteBr);
        }
        else if (gApp.currentTab == 3) {
            // === TAB 3: PROFILES ===
            std::wstring pHint = L"[" + vkToName(snap.radialVk) + L"] — пресеты • целевой CPS общий для всех:";
            g.DrawString(pHint.c_str(), -1, &fSub, RectF(cx0, cy0 - 4.0f * S, cw, 24.0f * S), &sfLeftVCenter, &subBr);

            const wchar_t* notes[4] = {
                L"(под низкий пинг)",
                L"(темп для клинча)",
                L"(плавный с Jitter)",
                L"(настраиваемый пресет)"
            };
            int pHits[4] = { HIT_PROF_0, HIT_PROF_1, HIT_PROF_2, HIT_PROF_3 };

            for (int i = 0; i < 4; ++i) {
                RectF pr(cx0, cy0 + 28.0f * S + i * 54.0f * S, cw, 44.0f * S);
                bool act = (snap.activeProfile == i);
                bool hov = (gApp.hoveredHitId == pHits[i]);

                fillStrokeRoundRect(
                    g, pr, 6.0f * S,
                    act ? accentFill(snap.themeIndex, 238) : (hov ? Color(255, 33, 38, 50) : Color(255, 24, 28, 36)),
                    act ? accentStroke(snap.themeIndex) : Color(255, 43, 48, 61),
                    act ? 1.5f : 1.0f
                );

                wchar_t line[128];
                const std::wstring& displayName = (gApp.renameActive && gApp.renameProfileIndex == i)
                    ? gApp.renameBuffer : snap.profiles[i].name;
                swprintf(line, 128, L"%ls   —   %d CPS • %d ms  %ls",
                         displayName.c_str(),
                         snap.targetCps,
                         snap.profiles[i].delayMs,
                         notes[i]);
                SolidBrush pb(act ? Color(255, 255, 255, 255) : Color(255, 212, 218, 230));
                g.DrawString(line, -1, &fSub, RectF(pr.X + 14.0f * S, pr.Y, pr.Width - 20.0f * S, pr.Height), &sfLeftVCenter, &pb);
                gApp.hitZones.push_back({ pHits[i], pr });
            }

            RectF btnSave(cx0, cy0 + 252.0f * S, cw, 38.0f * S);
            bool svHov = (gApp.hoveredHitId == HIT_BTN_SAVE_PROF);
            bool recentlySaved = (qpcSeconds() < gApp.profSavedUntil);
            fillStrokeRoundRect(
                g, btnSave, 6.0f * S,
                recentlySaved ? Color(255, 22, 58, 36) : (svHov ? Color(255, 68, 22, 27) : Color(255, 29, 33, 43)),
                recentlySaved ? Color(255, 62, 212, 112) : (svHov ? accentStroke(snap.themeIndex) : Color(255, 52, 58, 74)),
                1.0f
            );
            g.DrawString(
                recentlySaved ? L"Сохранено в активный профиль!" : L"Сохранить настройки в активный профиль",
                -1, &fSub, btnSave, &sfCenter, &whiteBr
            );
            gApp.hitZones.push_back({ HIT_BTN_SAVE_PROF, btnSave });

            RectF btnRename(cx0, cy0 + 296.0f * S, cw, 34.0f * S);
            bool renHov = (gApp.hoveredHitId == HIT_BTN_RENAME_PROFILE);
            fillStrokeRoundRect(g, btnRename, 6.0f * S,
                                renHov ? accentFill(snap.themeIndex, 220) : Color(255, 25, 29, 38),
                                renHov ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72), 1.0f);
            g.DrawString(gApp.renameActive ? L"Вводи имя профиля • Enter — сохранить" : L"Переименовать активный профиль",
                         -1, &fSub, btnRename, &sfCenter, &whiteBr);
            gApp.hitZones.push_back({ HIT_BTN_RENAME_PROFILE, btnRename });
            if (gApp.renameActive) {
                g.DrawString(L"Только текст • Esc отмена", -1, &fHint,
                             RectF(cx0, cy0 + 334.0f * S, cw, 24.0f * S), &sfCenter, &dimBr);
            }
        }
        else if (gApp.currentTab == 4) {
            // === TAB 4: BINDS ===
            struct BindRow { const wchar_t* title; int vk; UINT mods; int hitId; };
            BindRow rows[5] = {
                { L"Кнопка активации макроса",      snap.triggerVk, 0, HIT_BIND_TRIGGER },
                { L"Кнопка спама (F / LMB / любая)", snap.outputVk,  0, HIT_BIND_OUTPUT  },
                { L"Открыть / скрыть меню ByAzen",  snap.menuVk,    snap.menuMods, HIT_BIND_MENU },
                { L"Колесо профилей (удержание)",   snap.radialVk,  0, HIT_BIND_RADIAL  },
                { L"Пауза макроса прямо в бою",     snap.panicVk,   snap.panicMods, HIT_BIND_PANIC },
            };

            Font fRowMed(pSans, 13.0f * S * gApp.fontScalePx, FontStyleRegular, UnitPixel);
            Font fKey(pMono,    12.0f * S * gApp.fontScalePx, FontStyleBold,    UnitPixel);

            for (int i = 0; i < 5; ++i) {
                float ry = cy0 + i * 46.0f * S;
                g.DrawString(rows[i].title, -1, &fRowMed, RectF(cx0, ry, cw - 126.0f * S, 34.0f * S), &sfLeftVCenter, &whiteBr);

                RectF br(cx0 + cw - 118.0f * S, ry + 1.0f * S, 118.0f * S, 32.0f * S);
                bool cap = (gApp.capturingHitId == rows[i].hitId);
                bool hov = (gApp.hoveredHitId   == rows[i].hitId);

                fillStrokeRoundRect(
                    g, br, 5.0f * S,
                    cap ? Color(255, 68, 22, 27) : (hov ? Color(255, 35, 40, 52) : Color(255, 25, 29, 38)),
                    (cap || hov) ? accentStroke(snap.themeIndex) : Color(255, 50, 56, 72),
                    cap ? 1.5f : 1.0f
                );

                std::wstring kTxt = cap ? L"[ Модификатор + клавиша ]" : (L"[ " + hotkeyToName(rows[i].vk, rows[i].mods) + L" ]");
                g.DrawString(kTxt.c_str(), -1, &fKey, br, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ rows[i].hitId, br });
            }

            // 3 Quick 1-click preset pills
            const wchar_t* qLabels[3] = { L"Спам: [F]", L"Спам: [LMB]", L"Триггер: [M4]" };
            int qHits[3] = { HIT_QUICK_F, HIT_QUICK_LMB, HIT_QUICK_M4 };
            float qw = (cw - 16.0f * S) / 3.0f;
            float qy = cy0 + 236.0f * S;
            for (int i = 0; i < 3; ++i) {
                RectF qr(cx0 + i * (qw + 8.0f * S), qy, qw, 30.0f * S);
                bool qHov = (gApp.hoveredHitId == qHits[i]);
                fillStrokeRoundRect(
                    g, qr, 5.0f * S,
                    qHov ? Color(255, 42, 22, 27) : Color(255, 25, 29, 38),
                    qHov ? accentStroke(snap.themeIndex) : Color(255, 45, 51, 66),
                    1.0f
                );
                g.DrawString(qLabels[i], -1, &fBtnSm, qr, &sfCenter, &whiteBr);
                gApp.hitZones.push_back({ qHits[i], qr });
            }

            // Status hint text
            Color stColor = (gApp.bindStatusKind == 1) ? Color(255, 62, 212, 112)
                          : (gApp.bindStatusKind == 2) ? accentStroke(snap.themeIndex)
                                                       : Color(255, 143, 151, 170);
            SolidBrush stBr(stColor);
            StringFormat sfStatus;
            sfStatus.SetAlignment(StringAlignmentNear);
            sfStatus.SetLineAlignment(StringAlignmentNear);
            const std::wstring& statusText = gApp.hotkeyWarning.empty() ? gApp.bindStatusText : gApp.hotkeyWarning;
            g.DrawString(
                statusText.c_str(), -1, &fHint,
                RectF(cx0, cy0 + 276.0f * S, cw, 40.0f * S), &sfStatus, &stBr
            );
        }

        else if (gApp.currentTab == 5) {
            g.DrawString(L"Сервис, доступность и локальная диагностика", -1, &fSub,
                         RectF(cx0, cy0 - 4.0f * S, cw, 24.0f * S), &sfLeftVCenter, &subBr);
            RectF diagCard(cx0, cy0 + 24.0f * S, cw, 56.0f * S);
            fillStrokeRoundRect(g, diagCard, 7.0f * S, Color(255, 22, 26, 36), Color(255, 42, 50, 67), 1.0f);
            wchar_t diagText[160];
            swprintf(diagText, 160, L"Дисплеев: %d   •   Активный: %ls   •   Частота: %d Hz",
                     gApp.monitorCount, gApp.monitorLabel.c_str(), gApp.monitorHz);
            g.DrawString(diagText, -1, &fHint, RectF(diagCard.X + 10.0f * S, diagCard.Y, diagCard.Width - 20.0f * S, diagCard.Height),
                         &sfLeftVCenter, &whiteBr);
            struct ServiceAction { const wchar_t* label; int id; };
            ServiceAction actions[10] = {
                { L"Поиск по настройкам", HIT_BTN_SEARCH_SETTINGS },
                { gUndoAvailable ? L"Отменить изменение" : L"Отмена недоступна", HIT_BTN_UNDO },
                { L"Экспорт сессии CSV", HIT_BTN_EXPORT_STATS },
                { L"Диагностика", HIT_BTN_DIAGNOSTICS },
                { L"Восстановить копию", HIT_BTN_RESTORE_BACKUP },
                { L"Очистить журнал", HIT_BTN_CLEAR_LOG },
                { L"Проверить настройки", HIT_BTN_VALIDATE_CONFIG },
                { L"Быстрый старт", HIT_BTN_ONBOARDING },
                { L"Папка настроек", HIT_BTN_OPEN_CONFIG_FOLDER },
                { L"О программе", HIT_BTN_ABOUT },
            };
            const float gap = 8.0f * S;
            const float bw = (cw - gap) * 0.5f;
            for (int i = 0; i < 10; ++i) {
                const int col = i % 2, row = i / 2;
                RectF ar(cx0 + col * (bw + gap), cy0 + 78.0f * S + row * 39.0f * S, bw, 34.0f * S);
                const bool disabled = actions[i].id == HIT_BTN_UNDO && !gUndoAvailable;
                const bool hov = gApp.hoveredHitId == actions[i].id;
                fillStrokeRoundRect(g, ar, 5.0f * S,
                    disabled ? Color(255, 20, 23, 30) : (hov ? accentFill(snap.themeIndex, 150) : Color(255, 25, 29, 38)),
                    hov && !disabled ? accentStroke(snap.themeIndex) : Color(255, 46, 54, 72), 1.0f);
                SolidBrush actionBr(disabled ? Color(255, 120, 127, 141) : Color(255, 231, 235, 244));
                g.DrawString(actions[i].label, -1, &fHint, ar, &sfCenter, &actionBr);
                gApp.hitZones.push_back({ actions[i].id, ar });
            }
            drawToggleRow(cy0 + 288.0f * S, L"Повышенная контрастность", snap.highContrast, HIT_TOG_HIGH_CONTRAST);
            drawToggleRow(cy0 + 326.0f * S, L"Компактный интерфейс", snap.compactMode, HIT_TOG_COMPACT_MODE);
            drawToggleRow(cy0 + 364.0f * S, L"Привязка к сетке 8 px", snap.snapToGrid, HIT_TOG_SNAP_GRID);
            drawToggleRow(cy0 + 402.0f * S, L"История сессий (только локально)", snap.sessionHistoryEnabled, HIT_TOG_SESSION_HISTORY);
            drawToggleRow(cy0 + 440.0f * S, L"Запускать при входе в Windows", snap.startWithWindows, HIT_TOG_AUTOSTART);
            g.DrawString(L"История — opt-in; автозапуск добавляет запись в HKCU Run.", -1, &fHint,
                         RectF(cx0, cy0 + 482.0f * S, cw, 28.0f * S), &sfLeftVCenter, &dimBr);
        }

        // ---- Bottom-Center Config Mode Hint Banner ----
        std::wstring bannerTxt = L"Режим настройки ByAzen • Перетаскивай виджеты мышкой • [" + menuKeyName + L"] или клик мимо — в игру";
        RectF bm;
        g.MeasureString(bannerTxt.c_str(), -1, &fHint, PointF(0, 0), &sfCenter, &bm);
        float banW = bm.Width + 36.0f * S;
        float banH = 30.0f * S;
        RectF banRect((W - banW) * 0.5f, H - 46.0f * S, banW, banH);
        fillStrokeRoundRect(g, banRect, 6.0f * S, Color(228, 15, 17, 22), Color(255, 55, 61, 76), 1.0f);
        SolidBrush banTxtBr(Color(255, 210, 216, 228));
        g.DrawString(bannerTxt.c_str(), -1, &fHint, banRect, &sfCenter, &banTxtBr);
    }

    if (gApp.menuOpen) {
        const int selected = std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1);
        const auto& w = snap.photoWidgets[selected];
        if (!w.path.empty() && gApp.photoBitmaps[selected]) {
            const float px = std::clamp(static_cast<float>(w.x), 8.0f, std::max(8.0f, static_cast<float>(W) - 48.0f));
            const float py = std::clamp(static_cast<float>(w.y), 8.0f, std::max(8.0f, static_cast<float>(H) - 40.0f));
            const float pw = std::clamp(static_cast<float>(w.width), 40.0f, std::max(40.0f, static_cast<float>(W) - px - 8.0f));
            const float ph = std::clamp(static_cast<float>(w.height), 40.0f, std::max(40.0f, static_cast<float>(H) - py - 8.0f));
            RectF frame(px, py, pw, ph);
            Pen framePen(accentStroke(snap.themeIndex), 2.0f * S);
            framePen.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&framePen, frame);
            const float handleSize = 10.0f * S;
            const float centers[8][2] = {
                {px, py}, {px + pw * 0.5f, py}, {px + pw, py}, {px + pw, py + ph * 0.5f},
                {px + pw, py + ph}, {px + pw * 0.5f, py + ph}, {px, py + ph}, {px, py + ph * 0.5f}
            };
            for (int h = 0; h < 8; ++h) {
                RectF grip(centers[h][0] - handleSize * 0.5f, centers[h][1] - handleSize * 0.5f, handleSize, handleSize);
                fillStrokeRoundRect(g, grip, 2.0f * S, Color(255, 245, 247, 252), accentStroke(snap.themeIndex), 1.3f * S);
                gApp.hitZones.push_back({ photoHitId(selected, h + 1), grip });
            }
        }
    }

    if (snap.toastEnabled && qpcSeconds() < gApp.toastUntil && !gApp.toastText.empty()) {
        const double remaining = std::max(0.0, gApp.toastUntil - qpcSeconds());
        const float reveal = snap.animationsEnabled ? std::clamp(static_cast<float>((1.8 - remaining) / 0.18), 0.0f, 1.0f) : 1.0f;
        const float eased = reveal * reveal * (3.0f - 2.0f * reveal);
        const BYTE toastAlpha = static_cast<BYTE>(220 + 35 * eased);
        RectF tr((W - 300.0f * S) * 0.5f, (72.0f - 12.0f * (1.0f - eased)) * S, 300.0f * S, 42.0f * S);
        fillStrokeRoundRect(g, tr, 7.0f * S, Color(toastAlpha, 18, 21, 28), accentStroke(snap.themeIndex, toastAlpha), 1.2f);
        Font toastFont(pSans, 14.0f * S * gApp.fontScalePx, FontStyleBold, UnitPixel);
        SolidBrush toastBr(Color(255, 244, 246, 250));
        g.DrawString(gApp.toastText.c_str(), -1, &toastFont, tr, &sfCenter, &toastBr);
    }

    for (auto& zone : gApp.hitZones) {
        zone.rect.X += offsetX;
        zone.rect.Y += offsetY;
    }

    // Push premultiplied ARGB buffer to Windows DWM
    HDC screenDC = GetDC(NULL);
    POINT ptDst = { gApp.virtualX, gApp.virtualY };
    SIZE  szWnd = { gApp.screenW, gApp.screenH };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(gApp.hwnd, screenDC, &ptDst, &szWnd, gApp.memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screenDC);
}

// ============================================================================
// UI State Actions & Interactive Controllers
// ============================================================================

static int hitTestZones(float x, float y, HitZone* outZone = nullptr) {
    // Ordinary controls and menu chrome stay above photos; image handles/body are secondary hits.
    for (const auto& hz : gApp.hitZones) {
        if (!isPhotoWidgetHit(hz.id) && hz.rect.Contains(x, y)) {
            if (outZone) *outZone = hz;
            return hz.id;
        }
    }
    for (auto it = gApp.hitZones.rbegin(); it != gApp.hitZones.rend(); ++it) {
        if (isPhotoWidgetHit(it->id) && it->rect.Contains(x, y)) {
            if (outZone) *outZone = *it;
            return it->id;
        }
    }
    return HIT_NONE;
}

static void flushPendingConfigSave();

static void setMenuOpenState(bool openState) {
    if (gApp.menuOpen == openState) return;
    gApp.menuOpen = openState;
    if (openState) {
        selectMonitorAtCursor();
        gApp.menuOpenedAt = qpcSeconds();
    }
    if (!openState) {
        flushPendingConfigSave();
        const bool restoreHotkeys = gApp.capturingHitId != HIT_NONE;
        gApp.renameActive = false;
        gApp.renameProfileIndex = -1;
        gApp.renameBuffer.clear();
        gApp.capturingHitId = HIT_NONE;
        gApp.activeDragHit  = HIT_NONE;
        if (restoreHotkeys) refreshRegisteredHotkeys();
    }
    if (openState) {
        checkAndTrackRobloxForeground();
        applyClickThrough(false);
        SetForegroundWindow(gApp.hwnd);
    } else {
        applyClickThrough(true);
        bool lockRefocus = true;
        {
            CsLock lk(&gApp.cfgCs);
            lockRefocus = gApp.cfg.noAltTabLock;
        }
        if (lockRefocus) {
            refocusRobloxWindow();
        }
    }
    gApp.needsRedraw = true;
}

static void applyProfileByIndex(int idx) {
    if (idx < 0 || idx > 3) return;
    CsLock lk(&gApp.cfgCs);
    gApp.cfg.activeProfile = idx;
    const auto& p = gApp.cfg.profiles[idx];
    // Preserve the user's global CPS across profile changes.
    gApp.cfg.delayMs   = p.delayMs;
    gApp.cfg.holdMs    = p.holdMs;
    gApp.cfg.jitterPct = p.jitterPct;
    gApp.cfg.pingSync  = p.pingSync;
    gApp.toastText = L"Профиль: " + p.name;
    gApp.toastUntil = qpcSeconds() + 1.5;
    saveConfig(gApp.cfg);
    gApp.needsRedraw = true;
}

static void applySliderValue(const HitZone& hz, int val) {
    val = std::clamp(val, hz.minVal, hz.maxVal);

    CsLock lk(&gApp.cfgCs);
    if (hz.id == HIT_SLD_CPS) {
        gApp.cfg.targetCps = val;
        gApp.cfg.delayMs = std::max(1, static_cast<int>(std::round(1000.0 / std::max(1, val))) - gApp.cfg.holdMs);
    } else if (hz.id == HIT_SLD_DELAY) {
        gApp.cfg.delayMs = val;
        gApp.cfg.targetCps = std::clamp(static_cast<int>(std::round(1000.0 / std::max(2, val + gApp.cfg.holdMs))), CPS_MIN, CPS_MAX);
    } else if (hz.id == HIT_SLD_HOLD) {
        gApp.cfg.holdMs = val;
    } else if (hz.id == HIT_SLD_JITTER) {
        gApp.cfg.jitterPct = val;
    } else if (hz.id == HIT_SLD_OPACITY) {
        gApp.cfg.hudOpacity = val;
    } else if (hz.id == HIT_SLD_PING) {
        gApp.cfg.serverPingMs = val;
    } else if (hz.id == HIT_SLD_UI_SCALE) {
        gApp.cfg.uiScale = val;
    } else if (hz.id == HIT_SLD_BACKGROUND_OPACITY) {
        gApp.cfg.backgroundOpacity = val;
    } else if (hz.id == HIT_SLD_FONT_SCALE) {
        gApp.cfg.fontScale = val;
    } else if (hz.id == HIT_SLD_PHOTO_OPACITY) {
        gApp.cfg.photoWidgets[std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1)].opacity = val;
    }
    if (hz.id == HIT_SLD_CPS || hz.id == HIT_SLD_DELAY) {
        for (auto& profile : gApp.cfg.profiles) profile.targetCps = gApp.cfg.targetCps;
    }
    gApp.needsRedraw = true;
}

static void updateSliderFromMouseX(const HitZone& hz, float mouseX) {
    float pad = 9.0f * gApp.uiScalePx;
    float span = std::max(1.0f, hz.rect.Width - 2.0f * pad);
    float ratio = std::clamp((mouseX - (hz.rect.X + pad)) / span, 0.0f, 1.0f);
    applySliderValue(hz, static_cast<int>(std::round(hz.minVal + ratio * (hz.maxVal - hz.minVal))));
}

// Mouse wheel over a slider: exact steps that dragging a wide range cannot hit.
static bool nudgeSliderValue(const HitZone& hz, int delta) {
    int current = 0;
    {
        CsLock lk(&gApp.cfgCs);
        const AppConfig& c = gApp.cfg;
        switch (hz.id) {
            case HIT_SLD_CPS:                current = c.targetCps; break;
            case HIT_SLD_DELAY:              current = c.delayMs; break;
            case HIT_SLD_HOLD:               current = c.holdMs; break;
            case HIT_SLD_JITTER:             current = c.jitterPct; break;
            case HIT_SLD_OPACITY:            current = c.hudOpacity; break;
            case HIT_SLD_PING:               current = c.serverPingMs; break;
            case HIT_SLD_UI_SCALE:           current = c.uiScale; break;
            case HIT_SLD_BACKGROUND_OPACITY: current = c.backgroundOpacity; break;
            case HIT_SLD_FONT_SCALE:         current = c.fontScale; break;
            case HIT_SLD_PHOTO_OPACITY:
                current = c.photoWidgets[std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1)].opacity;
                break;
            default:
                return false;
        }
    }
    applySliderValue(hz, current + delta);
    return true;
}

// Wheel steps arrive in quick succession; the INI file is written once they stop.
static void flushPendingConfigSave() {
    if (gApp.configSaveDueAt <= 0.0) return;
    gApp.configSaveDueAt = 0.0;
    CsLock lk(&gApp.cfgCs);
    saveConfig(gApp.cfg);
}

static void updatePhotoWidgetFromDrag(int hit, int dx, int dy, int displayW, int displayH) {
    if (!isPhotoWidgetHit(hit)) return;
    const int index = std::clamp(photoWidgetIndexFromHit(hit), 0, PHOTO_WIDGET_COUNT - 1);
    const int handle = photoHandleFromHit(hit);
    CsLock lk(&gApp.cfgCs);
    auto& w = gApp.cfg.photoWidgets[index];
    int x = gApp.photoDragStartX;
    int y = gApp.photoDragStartY;
    int width = gApp.photoDragStartW;
    int height = gApp.photoDragStartH;
    if (handle == 0) {
        x = std::clamp(x + dx, 8, std::max(8, displayW - width - 8));
        y = std::clamp(y + dy, 8, std::max(8, displayH - height - 8));
    } else {
        const bool left = handle == 1 || handle == 7 || handle == 8;
        const bool top = handle == 1 || handle == 2 || handle == 3;
        const bool right = handle == 3 || handle == 4 || handle == 5;
        const bool bottom = handle == 5 || handle == 6 || handle == 7;
        if (left) {
            const int rightEdge = gApp.photoDragStartX + gApp.photoDragStartW;
            x = std::clamp(gApp.photoDragStartX + dx, 8, rightEdge - 40);
            width = rightEdge - x;
        } else if (right) {
            width = std::clamp(gApp.photoDragStartW + dx, 40,
                               std::max(40, displayW - gApp.photoDragStartX - 8));
        }
        if (top) {
            const int bottomEdge = gApp.photoDragStartY + gApp.photoDragStartH;
            y = std::clamp(gApp.photoDragStartY + dy, 8, bottomEdge - 40);
            height = bottomEdge - y;
        } else if (bottom) {
            height = std::clamp(gApp.photoDragStartH + dy, 40,
                                std::max(40, displayH - gApp.photoDragStartY - 8));
        }
    }
    width = std::clamp(width, 40, std::max(40, displayW - x - 8));
    height = std::clamp(height, 40, std::max(40, displayH - y - 8));
    w.x = x; w.y = y; w.width = width; w.height = height;
    gApp.needsRedraw = true;
}

static void resetWidgetPositions() {
    {
        CsLock lk(&gApp.cfgCs);
        auto& d = gApp.cfg.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
        d.menuX = 36; d.menuY = 155;
        d.hudX = -1; d.hudY = 36;
        d.pillX = -1; d.pillY = -1;
        for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
            auto& photo = gApp.cfg.photoWidgets[i];
            photo.x = 100 + (i % 4) * 24; photo.y = 100 + (i % 4) * 24;
            photo.width = 240; photo.height = 160;
        }
        saveConfig(gApp.cfg);
    }
    gApp.needsRedraw = true;
}

static void startInteractiveCapture(int hitId) {
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(gApp.hwnd, HOTKEY_MENU_BASE + i);
        UnregisterHotKey(gApp.hwnd, HOTKEY_PANIC_BASE + i);
    }
    gApp.capturingHitId = hitId;
    gApp.captureReadyAt = qpcSeconds() + 0.22;
    gApp.bindStatusKind = 1;
    gApp.bindStatusText = (hitId == HIT_BIND_MENU || hitId == HIT_BIND_PANIC)
        ? L"Удерживай Ctrl/Shift/Alt для сочетания, затем нажми клавишу • ESC отмена"
        : L"Нажми клавишу или кнопку мыши (M4, M5, F, LMB...) • ESC для отмены";
    gApp.needsRedraw = true;
}

static void tryAssignQuickVk(bool isTrigger, int vk) {
    CsLock lk(&gApp.cfgCs);
    int other = isTrigger ? gApp.cfg.outputVk : gApp.cfg.triggerVk;
    if (other == vk) {
        gApp.bindStatusKind = 2;
        gApp.bindStatusText = L"Кнопка активации и кнопка спама должны отличаться (например M4 -> F)!";
    } else {
        if (isTrigger) gApp.cfg.triggerVk = vk;
        else           gApp.cfg.outputVk  = vk;
        saveConfig(gApp.cfg);
        gApp.bindStatusKind = 1;
        gApp.bindStatusText = L"Успешно назначено: [" + vkToName(vk) + L"]";
    }
    gApp.needsRedraw = true;
}

// ============================================================================
// System Tray Setup & Menu
// ============================================================================

static void initSystemTray() {
    gApp.nid.cbSize = sizeof(NOTIFYICONDATAW);
    gApp.nid.hWnd   = gApp.hwnd;
    gApp.nid.uID    = 1;
    gApp.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gApp.nid.uCallbackMessage = WM_TRAYICON;
    gApp.nid.hIcon  = gApp.hAppIcon ? gApp.hAppIcon : LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(gApp.nid.szTip, L"ByAzen — Roblox Blade Ball Overlay");
    gApp.trayAdded = Shell_NotifyIconW(NIM_ADD, &gApp.nid) != FALSE;
}

static void removeSystemTray() {
    if (gApp.trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &gApp.nid);
        gApp.trayAdded = false;
    }
}

static void showTrayContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool macEn = true;
    std::wstring mKey;
    {
        CsLock lk(&gApp.cfgCs);
        macEn = gApp.cfg.macroEnabled;
        mKey  = hotkeyToName(gApp.cfg.menuVk, gApp.cfg.menuMods);
    }

    std::wstring toggleLbl = (gApp.menuOpen ? L"Скрыть меню [" : L"Открыть меню [") + mKey + L"]";
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_MENU, toggleLbl.c_str());
    AppendMenuW(hMenu, MF_STRING | (macEn ? MF_CHECKED : 0), ID_TRAY_TOGGLE_MAC, L"Макрос активен");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESET_POS, L"Сбросить позиции виджетов");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Выход из ByAzen");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(gApp.hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, gApp.hwnd, NULL);
    PostMessageW(gApp.hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// ============================================================================
// Global UI Tick (12 ms): Hotkeys, Bind Capture, Radial Wheel, Live HUD Sync
// ============================================================================


static void unregisterAppHotkeys() {
    if (!gApp.hwnd) return;
    for (int i = 0; i < 4; ++i) {
        UnregisterHotKey(gApp.hwnd, HOTKEY_MENU_BASE + i);
        UnregisterHotKey(gApp.hwnd, HOTKEY_PANIC_BASE + i);
    }
}

static void refreshRegisteredHotkeys() {
    if (!gApp.hwnd) return;
    unregisterAppHotkeys();
    AppConfig snap;
    { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
    gApp.hotkeyWarning.clear();
    if (snap.menuVk >= 0x08) {
        if (!RegisterHotKey(gApp.hwnd, HOTKEY_MENU_BASE, snap.menuMods | MOD_NOREPEAT, static_cast<UINT>(snap.menuVk)))
            gApp.hotkeyWarning = L"Не удалось зарегистрировать меню: сочетание занято или зарезервировано Windows.";
    }
    if (snap.panicVk >= 0x08) {
        if (!RegisterHotKey(gApp.hwnd, HOTKEY_PANIC_BASE, snap.panicMods | MOD_NOREPEAT, static_cast<UINT>(snap.panicVk))) {
            if (!gApp.hotkeyWarning.empty()) gApp.hotkeyWarning += L" ";
            gApp.hotkeyWarning += L"Не удалось зарегистрировать паузу: сочетание занято или зарезервировано Windows.";
        }
    }
    if (snap.menuVk >= 0x08 && snap.panicVk >= 0x08 && snap.menuVk == snap.panicVk && snap.menuMods == snap.panicMods) {
        gApp.hotkeyWarning = L"Сочетания меню и паузы совпадают; назначь разные клавиши или модификаторы.";
    }
    if (!gApp.hotkeyWarning.empty()) {
        appendDiagnostic(gApp.hotkeyWarning);
        gApp.bindStatusText = gApp.hotkeyWarning;
        gApp.bindStatusKind = 2;
    } else if (gApp.bindStatusKind == 2 &&
               (gApp.bindStatusText.find(L"сочет") != std::wstring::npos || gApp.bindStatusText.find(L"клавиша меню") != std::wstring::npos)) {
        gApp.bindStatusText = L"Глобальные сочетания зарегистрированы.";
        gApp.bindStatusKind = 1;
    }
}

static bool chooseIniPath(bool saveDialog, std::wstring& path) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gApp.hwnd;
    ofn.lpstrFilter = L"ByAzen INI (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"ini";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (saveDialog ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = saveDialog ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return false;
    path = fileName;
    return true;
}

static bool chooseImagePath(std::wstring& path) {
    wchar_t fileName[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gApp.hwnd;
    ofn.lpstrFilter = L"Изображения (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0Bitmap (*.bmp)\0*.bmp\0\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = 32768;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = fileName;
    return true;
}

static bool chooseCustomAccentColor(COLORREF& color) {
    static COLORREF customColors[16] = {};
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc); cc.hwndOwner = gApp.hwnd;
    cc.rgbResult = color; cc.lpCustColors = customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    color = cc.rgbResult;
    return true;
}

static bool chooseOutputPath(const wchar_t* filter, const wchar_t* extension, std::wstring& path) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gApp.hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = extension;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = fileName;
    return true;
}

static std::wstring getProgramDirectory() {
    std::wstring path = getConfigFilePath();
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    return path;
}

static std::wstring getBackupDirectory() {
    const std::wstring dir = getProgramDirectory() + L"\\ByAzenBackups";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir;
}

static std::wstring getLogFilePath() {
    return getProgramDirectory() + L"\\byazen.log";
}

static bool createTimestampedBackup(std::wstring* createdPath = nullptr) {
    const std::wstring source = getConfigFilePath();
    if (GetFileAttributesW(source.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t name[128] = {};
    swprintf(name, 128, L"ByAzen_%04u%02u%02u_%02u%02u%02u_%03u.ini",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    const std::wstring target = getBackupDirectory() + L"\\" + name;
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) return false;
    if (createdPath) *createdPath = target;
    return true;
}

static bool writeUtf8File(const std::wstring& path, const std::wstring& contents, bool append = false) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, contents.c_str(), -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) return false;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, contents.c_str(), -1, utf8.data(), bytes, NULL, NULL);
    HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE,
                              FILE_SHARE_READ, NULL, append ? OPEN_ALWAYS : CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(bytes - 1), &written, NULL);
    CloseHandle(file);
    return ok != FALSE && written == static_cast<DWORD>(bytes - 1);
}

struct TextPromptState {
    HWND edit = NULL;
    std::wstring prompt;
    std::wstring value;
    bool accepted = false;
    bool done = false;
};
static constexpr int PROMPT_EDIT_ID = 4101;

static LRESULT CALLBACK textPromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TextPromptState* state = reinterpret_cast<TextPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<TextPromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (msg == WM_CREATE && state) {
        CreateWindowExW(0, L"STATIC", state->prompt.c_str(), WS_CHILD | WS_VISIBLE,
                        16, 12, 402, 24, hwnd, NULL, gApp.hInst, NULL);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      16, 42, 402, 28, hwnd, reinterpret_cast<HMENU>(PROMPT_EDIT_ID), gApp.hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Найти", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        236, 82, 86, 28, hwnd, reinterpret_cast<HMENU>(IDOK), gApp.hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"Отмена", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        332, 82, 86, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), gApp.hInst, NULL);
        SetFocus(state->edit);
        return 0;
    }
    if (msg == WM_COMMAND && state) {
        const int id = LOWORD(wParam);
        if (id == IDOK) {
            wchar_t text[512] = {};
            GetWindowTextW(state->edit, text, 512);
            state->value = text;
            state->accepted = !state->value.empty();
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        if (state) state->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY && state) state->done = true;
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool promptText(const wchar_t* title, const wchar_t* prompt, std::wstring& value) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = textPromptProc;
        wc.hInstance = gApp.hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"ByAzenTextPromptWindow";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        registered = true;
    }
    TextPromptState state;
    state.prompt = prompt;
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"ByAzenTextPromptWindow", title,
                               WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 450, 155,
                               gApp.hwnd, NULL, gApp.hInst, &state);
    if (!dlg) return false;
    RECT owner = {};
    GetWindowRect(gApp.hwnd, &owner);
    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (GetMonitorInfoW(monitor, &mi)) {
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - 450) / 2;
        y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - 155) / 2;
    }
    SetWindowPos(dlg, HWND_TOPMOST, x, y, 450, 155, SWP_SHOWWINDOW);
    EnableWindow(gApp.hwnd, FALSE);
    SetForegroundWindow(dlg);
    MSG message;
    while (!state.done && GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(gApp.hwnd, TRUE);
    SetForegroundWindow(gApp.hwnd);
    value = state.value;
    return state.accepted;
}

static void showOnboarding() {
    MessageBoxW(gApp.hwnd, L"Шаг 1 из 3 • Безопасность\n\nПервый запуск использует выключенный макрос и включённый тестовый режим. В тестовом режиме ввод не отправляется.",
                L"ByAzen — быстрый старт", MB_OK | MB_ICONINFORMATION);
    MessageBoxW(gApp.hwnd, L"Шаг 2 из 3 • Настройка интерфейса\n\nВо вкладке «Оверлей» выбери монитор, тему, свой фон и расположение HUD. Вкладка «Клавиши» позволяет изменить End и F10.",
                L"ByAzen — быстрый старт", MB_OK | MB_ICONINFORMATION);
    MessageBoxW(gApp.hwnd, L"Шаг 3 из 3 • Проверка\n\nПроверь профили и настройки в тестовом режиме. Используй ввод только если это разрешено правилами игры.",
                L"ByAzen — быстрый старт", MB_OK | MB_ICONINFORMATION);
}

static bool chooseBackupFile(std::wstring& path) {
    wchar_t fileName[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = gApp.hwnd;
    ofn.lpstrFilter = L"Резервные копии ByAzen (*.ini)\0*.ini\0Все файлы (*.*)\0*.*\0\0";
    ofn.lpstrFile = fileName; ofn.nMaxFile = 32768;
    const std::wstring initialDirectory = getBackupDirectory();
    ofn.lpstrInitialDir = initialDirectory.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = fileName;
    return true;
}

static bool chooseImagePath(std::wstring& path); // definition above is intentionally reused

static bool setRunAtWindowsStartup(bool enabled) {
    HKEY key = NULL;
    const wchar_t* subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_SET_VALUE, &key);
    if (!enabled && result == ERROR_FILE_NOT_FOUND) return true;
    if (enabled && result == ERROR_FILE_NOT_FOUND)
        result = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (result != ERROR_SUCCESS) return false;
    if (enabled) {
        wchar_t exePath[MAX_PATH] = {};
        if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) { RegCloseKey(key); return false; }
        std::wstring command = L"\"" + std::wstring(exePath) + L"\" --autostart";
        result = RegSetValueExW(key, L"ByAzen", 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(key, L"ByAzen");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static void appendSessionHistory() {
    if (!gApp.cfg.sessionHistoryEnabled) return;
    SYSTEMTIME end = {}; GetLocalTime(&end);
    wchar_t row[256] = {};
    swprintf(row, 256, L"%04u-%02u-%02u %02u:%02u:%02u,%llu,%d\r\n",
             gApp.sessionWallStart.wYear, gApp.sessionWallStart.wMonth, gApp.sessionWallStart.wDay,
             gApp.sessionWallStart.wHour, gApp.sessionWallStart.wMinute, gApp.sessionWallStart.wSecond,
             static_cast<unsigned long long>(gApp.sessionCycles.load()),
             std::max(0, static_cast<int>(qpcSeconds() - gApp.sessionStartedAt)));
    const std::wstring file = getBackupDirectory() + L"\\session_history.csv";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
        writeUtf8File(file, L"started_at,macro_cycles,duration_seconds\r\n" + std::wstring(row));
    else
        writeUtf8File(file, row, true);
}

static void exportSessionCsv() {
    std::wstring path;
    if (!chooseOutputPath(L"CSV (*.csv)\0*.csv\0All files (*.*)\0*.*\0\0", L"csv", path)) return;
    SYSTEMTIME end = {}; GetLocalTime(&end);
    wchar_t csv[512] = {};
    swprintf(csv, 512, L"started_at,duration_seconds,completed_cycles,test_mode\r\n%04u-%02u-%02u %02u:%02u:%02u,%d,%llu,%d\r\n",
             gApp.sessionWallStart.wYear, gApp.sessionWallStart.wMonth, gApp.sessionWallStart.wDay,
             gApp.sessionWallStart.wHour, gApp.sessionWallStart.wMinute, gApp.sessionWallStart.wSecond,
             std::max(0, static_cast<int>(qpcSeconds() - gApp.sessionStartedAt)),
             static_cast<unsigned long long>(gApp.sessionCycles.load()), gApp.cfg.testMode ? 1 : 0);
    if (writeUtf8File(path, csv)) setToast(L"CSV-отчёт сохранён");
    else MessageBoxW(gApp.hwnd, L"Не удалось записать CSV-файл.", L"ByAzen", MB_OK | MB_ICONWARNING);
}

static void exportDiagnostics() {
    std::wstring path;
    if (!chooseOutputPath(L"Текстовый отчёт (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0", L"txt", path)) return;
    AppConfig snap; { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
    std::wstring report = L"ByAzen diagnostic report\r\nVersion: 3.6.0\r\nConfiguration storage: %LOCALAPPDATA%\\ByAzen\r\n";
    report += L"Monitors detected: " + std::to_wstring(gApp.monitorCount) + L"\r\n";
    for (int i = 0; i < gApp.monitorCount; ++i) {
        const auto& m = gApp.monitors[i];
        report += L"Display " + std::to_wstring(i + 1) + L": " + m.device + L" " +
                  std::to_wstring(m.bounds.right - m.bounds.left) + L"x" +
                  std::to_wstring(m.bounds.bottom - m.bounds.top) +
                  (m.primary ? L" (primary)" : L"") + L"\r\n";
    }
    report += L"Active monitor: " + gApp.monitorLabel + L"\r\n";
    report += L"Refresh rate: " + std::to_wstring(gApp.monitorHz) + L" Hz\r\n";
    HDC diagDc = GetDC(NULL);
    if (diagDc) {
        report += L"System DPI: " + std::to_wstring(GetDeviceCaps(diagDc, LOGPIXELSX)) + L" x " +
                  std::to_wstring(GetDeviceCaps(diagDc, LOGPIXELSY)) + L"\r\n";
        ReleaseDC(NULL, diagDc);
    }
    report += L"Test mode: " + std::wstring(snap.testMode ? L"on" : L"off") + L"\r\n";
    report += L"Macro enabled: " + std::wstring(snap.macroEnabled ? L"yes" : L"no") + L"\r\n";
    report += L"Session cycles: " + std::to_wstring(gApp.sessionCycles.load()) + L"\r\n";
    report += L"Start with Windows: " + std::wstring(snap.startWithWindows ? L"on" : L"off") + L"\r\n";
    report += L"Configuration schema: 10\r\nNetwork access: none\r\nKeypress logging: disabled\r\n";
    if (writeUtf8File(path, report)) setToast(L"Диагностический отчёт сохранён");
    else MessageBoxW(gApp.hwnd, L"Не удалось записать отчёт.", L"ByAzen", MB_OK | MB_ICONWARNING);
}

static void restoreBackup() {
    std::wstring source;
    if (!chooseBackupFile(source)) return;
    const std::wstring current = getConfigFilePath();
    createTimestampedBackup();
    const std::wstring fixedBackup = current + L".bak";
    CopyFileW(current.c_str(), fixedBackup.c_str(), FALSE);
    if (!CopyFileW(source.c_str(), current.c_str(), FALSE)) {
        MessageBoxW(gApp.hwnd, L"Не удалось восстановить эту копию.", L"ByAzen", MB_OK | MB_ICONWARNING);
        return;
    }
    AppConfig restored; loadConfig(restored);
    { CsLock lk(&gApp.cfgCs); gApp.cfg = restored; }
    reloadCustomBackground(restored.backgroundPath);
    refreshRegisteredHotkeys(); syncTimerResolution(restored.preciseTiming);
    refreshMonitorList(); setToast(L"Резервная копия восстановлена"); renderOverlay();
}

static void undoLastChange() {
    if (!gUndoAvailable) {
        MessageBoxW(gApp.hwnd, L"Пока нет сохранённого предыдущего состояния.", L"ByAzen", MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(gApp.hwnd, L"Вернуть предыдущую сохранённую конфигурацию? Текущая будет сохранена как резервная копия.",
                    L"Отмена последнего изменения", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    createTimestampedBackup();
    const std::wstring source = getUndoSnapshotPath();
    const std::wstring current = getConfigFilePath();
    if (!CopyFileW(source.c_str(), current.c_str(), FALSE)) return;
    AppConfig restored; loadConfig(restored);
    { CsLock lk(&gApp.cfgCs); gApp.cfg = restored; }
    reloadCustomBackground(restored.backgroundPath);
    refreshRegisteredHotkeys(); syncTimerResolution(restored.preciseTiming);
    refreshMonitorList(); gUndoAvailable = false;
    setToast(L"Изменения отменены"); renderOverlay();
}

static void clearDiagnosticLog() {
    if (MessageBoxW(gApp.hwnd, L"Удалить локальный журнал ошибок и уведомлений? Нажатия клавиш в него не записываются.",
                    L"Очистка журнала", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    DeleteFileW(getLogFilePath().c_str());
    gApp.toastText = L"Журнал очищен";
    gApp.toastUntil = qpcSeconds() + 1.8;
    renderOverlay();
}

static void searchSettings() {
    std::wstring query;
    if (!promptText(L"Поиск настроек ByAzen", L"Введите название настройки или раздела:", query)) return;
    std::transform(query.begin(), query.end(), query.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    int tab = -1;
    if (query.find(L"макрос") != std::wstring::npos || query.find(L"cps") != std::wstring::npos || query.find(L"пинг") != std::wstring::npos || query.find(L"таймер") != std::wstring::npos) tab = 0;
    else if (query.find(L"ввод") != std::wstring::npos || query.find(L"клик") != std::wstring::npos || query.find(L"режим") != std::wstring::npos) tab = 1;
    else if (query.find(L"фон") != std::wstring::npos || query.find(L"background") != std::wstring::npos || query.find(L"theme") != std::wstring::npos || query.find(L"цвет") != std::wstring::npos || query.find(L"дисп") != std::wstring::npos || query.find(L"виджет") != std::wstring::npos || query.find(L"масштаб") != std::wstring::npos) tab = 2;
    else if (query.find(L"фото") != std::wstring::npos || query.find(L"виджет") != std::wstring::npos) tab = 6;
    else if (query.find(L"проф") != std::wstring::npos) tab = 3;
    else if (query.find(L"клав") != std::wstring::npos || query.find(L"бинд") != std::wstring::npos || query.find(L"end") != std::wstring::npos) tab = 4;
    else if (query.find(L"сес") != std::wstring::npos || query.find(L"отч") != std::wstring::npos || query.find(L"журнал") != std::wstring::npos || query.find(L"история") != std::wstring::npos || query.find(L"backup") != std::wstring::npos || query.find(L"резерв") != std::wstring::npos || query.find(L"контраст") != std::wstring::npos) tab = 5;
    if (tab >= 0) {
        gApp.currentTab = tab;
        gApp.menuOpen = true;
        gApp.needsRedraw = true;
        renderOverlay();
        setToast(L"Открыт подходящий раздел");
    } else {
        MessageBoxW(gApp.hwnd, L"Раздел не найден. Попробуйте: макрос, ввод, фон, цвет, дисплей, профили, клавиши, сессия, резервная копия.",
                    L"Поиск настроек", MB_OK | MB_ICONINFORMATION);
    }
}

static void validateCurrentConfig() {
    AppConfig snap; { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
    const int repairs = normalizeConfig(snap);
    if (repairs > 0) {
        { CsLock lk(&gApp.cfgCs); gApp.cfg = snap; saveConfig(gApp.cfg); }
        refreshRegisteredHotkeys(); reloadCustomBackground(snap.backgroundPath);
        MessageBoxW(gApp.hwnd, (L"Исправлены некорректные значения: " + std::to_wstring(repairs) + L".\nБезопасные диапазоны применены.").c_str(),
                    L"Проверка конфигурации", MB_OK | MB_ICONWARNING);
    } else {
        MessageBoxW(gApp.hwnd, L"Проверка завершена: числовые значения, клавиши и диапазоны настроек корректны.",
                    L"Проверка конфигурации", MB_OK | MB_ICONINFORMATION);
    }
    renderOverlay();
}

static void setToast(const std::wstring& text, double seconds) {
    appendDiagnostic(L"UI notification: " + text);
    gApp.toastText = text;
    gApp.toastUntil = qpcSeconds() + seconds;
    gApp.needsRedraw = true;
}

static void copyConfigFile(bool exportFile) {
    std::wstring current = getConfigFilePath();
    saveConfig(gApp.cfg);
    if (!exportFile) {
        std::wstring backup = current + L".bak";
        const bool historyCopy = createTimestampedBackup();
        if (CopyFileW(current.c_str(), backup.c_str(), FALSE) || historyCopy) {
            setToast(L"Резервная копия сохранена: byazen_config.ini.bak");
        } else {
            MessageBoxW(gApp.hwnd, L"Не удалось создать резервную копию. Проверь доступ к папке.", L"ByAzen", MB_OK | MB_ICONWARNING);
        }
        return;
    }
    std::wstring target;
    if (!chooseIniPath(true, target)) return;
    if (CopyFileW(current.c_str(), target.c_str(), FALSE)) {
        setToast(L"Конфигурация экспортирована");
    } else {
        MessageBoxW(gApp.hwnd, L"Не удалось экспортировать INI-файл.", L"ByAzen", MB_OK | MB_ICONWARNING);
    }
}

static std::wstring configChangePreview(const AppConfig& before, const AppConfig& after) {
    std::wstring fields;
    int shown = 0, omitted = 0;
    auto add = [&](const std::wstring& label, const std::wstring& oldValue, const std::wstring& newValue) {
        if (oldValue == newValue) return;
        if (shown >= 18) { ++omitted; return; }
        if (!fields.empty()) fields += L"\n";
        fields += L"• " + label + L": " + oldValue + L"  →  " + newValue;
        ++shown;
    };
    auto number = [](int value) { return std::to_wstring(value); };
    auto yesNo = [](bool value) { return std::wstring(value ? L"Вкл" : L"Выкл"); };
    add(L"Макрос", yesNo(before.macroEnabled), yesNo(after.macroEnabled));
    add(L"Целевой CPS", number(before.targetCps), number(after.targetCps));
    add(L"Задержка, мс", number(before.delayMs), number(after.delayMs));
    add(L"Удержание, мс", number(before.holdMs), number(after.holdMs));
    add(L"Разброс, %", number(before.jitterPct), number(after.jitterPct));
    add(L"Пинг, мс", number(before.serverPingMs), number(after.serverPingMs));
    add(L"Тестовый режим", yesNo(before.testMode), yesNo(after.testMode));
    add(L"Анимации", yesNo(before.animationsEnabled), yesNo(after.animationsEnabled));
    add(L"Только в окне Roblox", yesNo(before.onlyInRoblox), yesNo(after.onlyInRoblox));
    add(L"Сочетание меню", hotkeyToName(before.menuVk, before.menuMods), hotkeyToName(after.menuVk, after.menuMods));
    add(L"Сочетание аварийной паузы", hotkeyToName(before.panicVk, before.panicMods), hotkeyToName(after.panicVk, after.panicMods));
    add(L"Кнопка активации", vkToName(before.triggerVk), vkToName(after.triggerVk));
    add(L"Кнопка выхода", vkToName(before.outputVk), vkToName(after.outputVk));
    add(L"Масштаб UI, %", number(before.uiScale), number(after.uiScale));
    add(L"Размер шрифта, %", number(before.fontScale), number(after.fontScale));
    add(L"Прозрачность фона, %", number(before.backgroundOpacity), number(after.backgroundOpacity));
    add(L"Путь к фону", before.backgroundPath.empty() ? L"Стандартный" : before.backgroundPath,
        after.backgroundPath.empty() ? L"Стандартный" : after.backgroundPath);
    const wchar_t* fitNames[3] = { L"Заполнить", L"Вписать", L"Растянуть" };
    add(L"Режим кадрирования", fitNames[std::clamp(before.backgroundFit, 0, 2)], fitNames[std::clamp(after.backgroundFit, 0, 2)]);
    add(L"Своя тема", yesNo(before.customAccentEnabled), yesNo(after.customAccentEnabled));
    add(L"Высокая контрастность", yesNo(before.highContrast), yesNo(after.highContrast));
    add(L"Компактный режим", yesNo(before.compactMode), yesNo(after.compactMode));
    add(L"Привязка к сетке", yesNo(before.snapToGrid), yesNo(after.snapToGrid));
    add(L"История сессий", yesNo(before.sessionHistoryEnabled), yesNo(after.sessionHistoryEnabled));
    add(L"Запуск с Windows", yesNo(before.startWithWindows), yesNo(after.startWithWindows));
    add(L"Выбранный дисплей", before.activeMonitorDevice.empty() ? number(before.activeMonitor + 1) : before.activeMonitorDevice,
        after.activeMonitorDevice.empty() ? number(after.activeMonitor + 1) : after.activeMonitorDevice);
    for (int i = 0; i < 4; ++i) {
        const auto& a = before.profiles[i]; const auto& b = after.profiles[i];
        if (a.name != b.name || a.targetCps != b.targetCps || a.delayMs != b.delayMs ||
            a.holdMs != b.holdMs || a.jitterPct != b.jitterPct || a.pingSync != b.pingSync) {
            const std::wstring oldProfile = a.name + L" (" + number(a.targetCps) + L" CPS)";
            const std::wstring newProfile = b.name + L" (" + number(b.targetCps) + L" CPS)";
            add(L"Профиль " + number(i + 1), oldProfile, newProfile);
        }
    }
    for (int i = 0; i < 8; ++i) {
        const auto& a = before.displayLayouts[i]; const auto& b = after.displayLayouts[i];
        if (a.menuX != b.menuX || a.menuY != b.menuY || a.hudX != b.hudX || a.hudY != b.hudY ||
            a.pillX != b.pillX || a.pillY != b.pillY)
            add(L"Позиции дисплея " + number(i + 1), L"Текущие", L"Импортируемые");
    }
    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        const auto& a = before.photoWidgets[i]; const auto& b = after.photoWidgets[i];
        if (a.path != b.path || a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height ||
            a.opacity != b.opacity || a.fitMode != b.fitMode)
            add(L"Фото-виджет " + number(i + 1), a.path.empty() ? L"Пусто" : L"Настроен",
                b.path.empty() ? L"Удаляется" : L"Импортируется");
    }
    if (shown == 0) return L"Изменений нет — импортируемая конфигурация совпадает с текущей.";
    if (omitted > 0) fields += L"\n…и ещё " + number(omitted) + L" отличий.";
    return L"Предварительный просмотр (старое → новое):\n\n" + fields;
}

static void importConfigFile() {
    std::wstring source;
    if (!chooseIniPath(false, source)) return;
    int version = GetPrivateProfileIntW(L"General", L"configVer", 0, source.c_str());
    if (version <= 0 || version > 10) {
        MessageBoxW(gApp.hwnd, L"Файл не похож на поддерживаемую конфигурацию ByAzen INI.", L"ByAzen", MB_OK | MB_ICONWARNING);
        return;
    }
    AppConfig importedPreview;
    AppConfig currentPreview;
    loadConfig(importedPreview, source);
    loadConfig(currentPreview);
    if (version < 10) {
        for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) importedPreview.photoWidgets[i] = currentPreview.photoWidgets[i];
    }
    std::wstring preview = L"Проверь изменения перед импортом:\n\n" + configChangePreview(currentPreview, importedPreview) +
                           L"\n\nНажми OK, чтобы выбрать объём импорта.";
    if (MessageBoxW(gApp.hwnd, preview.c_str(), L"Предпросмотр импорта", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) return;
    const int scope = MessageBoxW(gApp.hwnd,
        L"Да — импортировать все настройки.\nНет — только оформление, фон и расположение виджетов.\nОтмена — ничего не менять.",
        L"Объём импорта", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (scope == IDCANCEL) return;
    AppConfig imported = importedPreview;
    if (scope == IDNO) {
        imported = currentPreview;
        imported.uiScale = importedPreview.uiScale;
        imported.fontScale = importedPreview.fontScale;
        imported.themeIndex = importedPreview.themeIndex;
        imported.hudOpacity = importedPreview.hudOpacity;
        imported.showHudBar = importedPreview.showHudBar;
        imported.showKeyPill = importedPreview.showKeyPill;
        imported.toastEnabled = importedPreview.toastEnabled;
        imported.backgroundPath = importedPreview.backgroundPath;
        imported.backgroundOpacity = importedPreview.backgroundOpacity;
        imported.backgroundFit = importedPreview.backgroundFit;
        imported.customAccentRgb = importedPreview.customAccentRgb;
        imported.customAccentEnabled = importedPreview.customAccentEnabled;
        imported.highContrast = importedPreview.highContrast;
        imported.compactMode = importedPreview.compactMode;
        imported.snapToGrid = importedPreview.snapToGrid;
        imported.activeMonitor = importedPreview.activeMonitor;
        imported.activeMonitorDevice = importedPreview.activeMonitorDevice;
        for (int i = 0; i < 8; ++i) imported.displayLayouts[i] = importedPreview.displayLayouts[i];
        for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) imported.photoWidgets[i] = importedPreview.photoWidgets[i];
    }
    std::wstring current = getConfigFilePath();
    if (GetFileAttributesW(current.c_str()) != INVALID_FILE_ATTRIBUTES) {
        createTimestampedBackup();
        std::wstring backup = current + L".bak";
        CopyFileW(current.c_str(), backup.c_str(), FALSE);
    }
    if (scope == IDYES) {
        if (!CopyFileW(source.c_str(), current.c_str(), FALSE)) {
            MessageBoxW(gApp.hwnd, L"Не удалось применить настройки. Проверь доступ к папке.", L"ByAzen", MB_OK | MB_ICONWARNING);
            return;
        }
        loadConfig(imported);
    } else {
        saveConfig(imported);
    }
    {
        CsLock lk(&gApp.cfgCs);
        gApp.cfg = imported;
    }
    refreshMonitorList();
    reloadCustomBackground(imported.backgroundPath);
    refreshRegisteredHotkeys();
    syncTimerResolution(gApp.cfg.preciseTiming);
    setToast(L"Настройки импортированы");
    renderOverlay();
}

static void resetAllSettings() {
    AppConfig defaults;
    defaults.macroEnabled = false; // reset always returns to a safe paused state
    int primaryIndex = 0;
    for (int i = 0; i < gApp.monitorCount; ++i) if (gApp.monitors[i].primary) primaryIndex = i;
    gApp.activeMonitor = primaryIndex;
    gApp.monitorLabel = gApp.monitors[primaryIndex].device;
    defaults.activeMonitor = primaryIndex;
    defaults.activeMonitorDevice = gApp.monitorLabel;
    {
        CsLock lk(&gApp.cfgCs);
        gApp.cfg = defaults;
        saveConfig(gApp.cfg);
    }
    reloadCustomBackground(L"");
    refreshRegisteredHotkeys();
    syncTimerResolution(gApp.cfg.preciseTiming);
    int primaryHeight = gApp.monitors[primaryIndex].bounds.bottom - gApp.monitors[primaryIndex].bounds.top;
    gApp.S = (primaryHeight >= 1350) ? static_cast<float>(primaryHeight) / 1080.0f : 1.0f;
    gApp.renameActive = false;
    setToast(L"Настройки сброшены к исходным");
    MessageBeep(MB_ICONASTERISK);
    renderOverlay();
}

static void finishProfileRename(bool accept) {
    if (!gApp.renameActive) return;
    if (accept) {
        while (!gApp.renameBuffer.empty() && iswspace(gApp.renameBuffer.back())) gApp.renameBuffer.pop_back();
        size_t first = 0;
        while (first < gApp.renameBuffer.size() && iswspace(gApp.renameBuffer[first])) ++first;
        gApp.renameBuffer = gApp.renameBuffer.substr(first, 28);
        if (!gApp.renameBuffer.empty() && gApp.renameProfileIndex >= 0 && gApp.renameProfileIndex < 4) {
            CsLock lk(&gApp.cfgCs);
            gApp.cfg.profiles[gApp.renameProfileIndex].name = gApp.renameBuffer;
            saveConfig(gApp.cfg);
            setToast(L"Имя профиля сохранено");
        }
    }
    gApp.renameActive = false;
    gApp.renameProfileIndex = -1;
    gApp.renameBuffer.clear();
    gApp.needsRedraw = true;
    renderOverlay();
}

static UINT currentlyHeldModifiers() {
    UINT mods = 0;
    if (isVkPressed(VK_CONTROL)) mods |= MOD_CONTROL;
    if (isVkPressed(VK_SHIFT)) mods |= MOD_SHIFT;
    if (isVkPressed(VK_MENU)) mods |= MOD_ALT;
    return mods;
}

static bool handleWindowBindCapture(int vk) {
    if (gApp.capturingHitId == HIT_NONE) return false;
    if (qpcSeconds() < gApp.captureReadyAt) return true;

    if (vk == VK_ESCAPE) {
        gApp.capturingHitId = HIT_NONE;
        gApp.bindStatusKind = 0;
        gApp.bindStatusText = L"Назначение отменено.";
        refreshRegisteredHotkeys();
        gApp.needsRedraw = true;
        renderOverlay();
        return true;
    }
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
        gApp.bindStatusText = L"Удерживай Ctrl / Shift / Alt и нажми основную клавишу. Отдельный модификатор не назначается.";
        gApp.bindStatusKind = 0;
        gApp.needsRedraw = true;
        renderOverlay();
        return true;
    }
    if (gApp.capturingHitId == HIT_BIND_MENU && vk == 0x01) {
        return true;
    }

    bool needRefreshHotkeys = false;
    const UINT capturedMods = (vk >= 0x08) ? currentlyHeldModifiers() : 0;
    {
        CsLock lk(&gApp.cfgCs);
        bool conflict = false;
        if (gApp.capturingHitId == HIT_BIND_TRIGGER && vk == gApp.cfg.outputVk) conflict = true;
        if (gApp.capturingHitId == HIT_BIND_OUTPUT  && vk == gApp.cfg.triggerVk) conflict = true;

        if (conflict) {
            gApp.bindStatusKind = 2;
            gApp.bindStatusText = L"Эта кнопка уже занята парным биндом! Выбери другую клавишу.";
        } else {
            if (gApp.capturingHitId == HIT_BIND_TRIGGER)      gApp.cfg.triggerVk = vk;
            else if (gApp.capturingHitId == HIT_BIND_OUTPUT)  gApp.cfg.outputVk  = vk;
            else if (gApp.capturingHitId == HIT_BIND_MENU)  { gApp.cfg.menuVk = vk; gApp.cfg.menuMods = capturedMods; needRefreshHotkeys = true; }
            else if (gApp.capturingHitId == HIT_BIND_RADIAL)  gApp.cfg.radialVk  = vk;
            else if (gApp.capturingHitId == HIT_BIND_PANIC) { gApp.cfg.panicVk = vk; gApp.cfg.panicMods = capturedMods; needRefreshHotkeys = true; }
            saveConfig(gApp.cfg);
            gApp.bindStatusKind = 1;
            gApp.bindStatusText = L"Успешно назначено: [" + vkToName(vk) + L"]";
            gApp.prevMenuDown = true;
        }
    }
    gApp.capturingHitId = HIT_NONE;
    (void)needRefreshHotkeys;
    refreshRegisteredHotkeys();
    gApp.needsRedraw = true;
    renderOverlay();
    return true;
}

static void onUiTimerTick() {
    AppConfig snap;
    {
        CsLock lk(&gApp.cfgCs);
        snap = gApp.cfg;
    }
    const double animNow = qpcSeconds();
    if (gApp.menuOpen && snap.animationsEnabled && animNow - gApp.lastAnimFrame >= 0.08) {
        gApp.lastAnimFrame = animNow;
        gApp.needsRedraw = true;
    }
    if (gApp.toastUntil > 0.0 && qpcSeconds() >= gApp.toastUntil) {
        gApp.toastUntil = 0.0;
        gApp.needsRedraw = true;
    }
    if (gApp.configSaveDueAt > 0.0 && animNow >= gApp.configSaveDueAt) {
        flushPendingConfigSave();
    }
    if (gApp.capturingHitId != HIT_NONE) {
        return;
    }
    // Mouse-button fallback only if user explicitly bound Menu or Panic to M4/M5/MMB (< 0x08)
    if (snap.menuVk > 0 && snap.menuVk < 0x08) {
        bool mDown = isVkPressed(snap.menuVk);
        if (mDown && !gApp.prevMenuDown) setMenuOpenState(!gApp.menuOpen);
        gApp.prevMenuDown = mDown;
    }
    if (snap.panicVk > 0 && snap.panicVk < 0x08) {
        bool pDown = isVkPressed(snap.panicVk);
        if (pDown && !gApp.prevPanicDown) {
            CsLock lk(&gApp.cfgCs);
            gApp.cfg.macroEnabled = false;
            gApp.cfg.toggleMode = false;
            saveConfig(gApp.cfg);
            setToast(L"Аварийная пауза: макрос выключен");
            MessageBeep(MB_ICONEXCLAMATION);
            gApp.needsRedraw = true;
        }
        gApp.prevPanicDown = pDown;
    }

    // 4. Radial Quick-Profile Wheel Hold ([ALT] by default — 100% Click-Through!)
    bool rDown = isVkPressed(snap.radialVk) && !isVkPressed(0x09); // Ignore Alt+Tab
    if (rDown && !gApp.prevRadialDown) {
        gApp.radialOpen = true;
        GetCursorPos(&gApp.radialOrigin);
        gApp.radialHovered = snap.activeProfile;
        gApp.needsRedraw = true;
    } else if (rDown && gApp.radialOpen) {
        POINT cur;
        GetCursorPos(&cur);
        double dx = static_cast<double>(cur.x - gApp.radialOrigin.x);
        double dy = static_cast<double>(cur.y - gApp.radialOrigin.y);
        if (std::hypot(dx, dy) >= 18.0) {
            double deg = std::atan2(dy, dx) * 180.0 / PI_VAL;
            if (deg < 0.0) deg += 360.0;
            int sel = 1;
            if (deg >= 225.0 && deg < 315.0)      sel = 0; // Top: Low Ping
            else if (deg >= 315.0 || deg < 45.0)  sel = 1; // Right: Clash Pro
            else if (deg >= 45.0 && deg < 135.0)  sel = 2; // Bottom: Safe
            else                                  sel = 3; // Left: Custom
            if (sel != gApp.radialHovered) {
                gApp.radialHovered = sel;
                gApp.needsRedraw = true;
            }
        }
        // Quick keys 1-4 support while holding Radial key (Shift-Lock friendly!)
        for (int k = 0; k < 4; ++k) {
            if (isVkPressed(0x31 + k) && gApp.radialHovered != k) {
                gApp.radialHovered = k;
                gApp.needsRedraw = true;
            }
        }
    } else if (!rDown && gApp.prevRadialDown && gApp.radialOpen) {
        int chosen = gApp.radialHovered;
        gApp.radialOpen = false;
        applyProfileByIndex(chosen);
        gApp.needsRedraw = true;
    }
    gApp.prevRadialDown = rDown;

    // 5. Dirty check for live HUD values (Keystroke pill, CPS, Ping, Firing state)
    bool firingNow = gApp.isFiring.load();
    bool trigVis = firingNow || isVkPressed(snap.triggerVk);
    bool outVis  = firingNow || isVkPressed(snap.outputVk);
    const int measuredNow = gApp.measuredCps.load();
    int  cpsNow  = (firingNow && measuredNow > 0) ? measuredNow : snap.targetCps;
    int  pingNow = snap.serverPingMs;

    const int sessionSecond = std::max(0, static_cast<int>(qpcSeconds() - gApp.sessionStartedAt));
    const uint64_t sessionCycles = gApp.sessionCycles.load(std::memory_order_relaxed);
    if (sessionSecond != gApp.lastSessionSecond) {
        gApp.lastSessionSecond = sessionSecond;
        gApp.lastSessionCycles = sessionCycles;
        if (gApp.menuOpen) gApp.needsRedraw = true;
    }

    if (trigVis != gApp.prevTrigVis || outVis != gApp.prevOutVis ||
        firingNow != gApp.lastFiringVis || cpsNow != gApp.lastShownCps || pingNow != gApp.lastShownPing) {
        gApp.prevTrigVis   = trigVis;
        gApp.prevOutVis    = outVis;
        gApp.lastFiringVis = firingNow;
        gApp.lastShownCps  = cpsNow;
        gApp.lastShownPing = pingNow;
        if (snap.showHudBar || snap.showKeyPill) {
            gApp.needsRedraw = true;
        }
    }

    if (gApp.needsRedraw) {
        renderOverlay();
    }
}

// ============================================================================
// Window Procedure
// ============================================================================


static void recreateDibSurface() {
    gApp.virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    gApp.virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    gApp.screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    gApp.screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (gApp.screenW <= 0) gApp.screenW = GetSystemMetrics(SM_CXSCREEN);
    if (gApp.screenH <= 0) gApp.screenH = GetSystemMetrics(SM_CYSCREEN);
    refreshMonitorList();
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 30) {
        gApp.monitorHz = static_cast<int>(dm.dmDisplayFrequency);
    }

    HDC screenDC = GetDC(NULL);
    if (!gApp.memDC) {
        gApp.memDC = CreateCompatibleDC(screenDC);
    } else if (gApp.dibBmp) {
        SelectObject(gApp.memDC, gApp.oldBmp);
        DeleteObject(gApp.dibBmp);
        gApp.dibBmp = NULL;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = gApp.screenW;
    bmi.bmiHeader.biHeight      = -gApp.screenH;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    gApp.dibBmp = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &gApp.dibBits, NULL, 0);
    gApp.oldBmp = SelectObject(gApp.memDC, gApp.dibBmp);
    ReleaseDC(NULL, screenDC);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SHOW_EXISTING:
            setMenuOpenState(true);
            renderOverlay();
            return 0;

        case WM_DISPLAYCHANGE:
            recreateDibSurface();
            SetWindowPos(hwnd, HWND_TOPMOST, gApp.virtualX, gApp.virtualY, gApp.screenW, gApp.screenH, SWP_NOACTIVATE);
            renderOverlay();
            return 0;

        case WM_DPICHANGED: {
            UINT dpi = HIWORD(wParam);
            if (dpi >= 48) gApp.S = static_cast<float>(dpi) / 96.0f;
            SetWindowPos(hwnd, HWND_TOPMOST, gApp.virtualX, gApp.virtualY, gApp.screenW, gApp.screenH,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            gApp.needsRedraw = true;
            renderOverlay();
            return 0;
        }

        case WM_HOTKEY: {
            int hkId = static_cast<int>(wParam);
            if (hkId >= HOTKEY_MENU_BASE && hkId < HOTKEY_MENU_BASE + 4) {
                if (gApp.capturingHitId == HIT_NONE) {
                    setMenuOpenState(!gApp.menuOpen);
                    renderOverlay();
                }
                return 0;
            }
            if (hkId >= HOTKEY_PANIC_BASE && hkId < HOTKEY_PANIC_BASE + 4) {
                if (gApp.capturingHitId == HIT_NONE) {
                    {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.macroEnabled = false;
                        gApp.cfg.toggleMode = false;
                        gApp.toastText = L"Аварийная пауза: макрос выключен";
                        gApp.toastUntil = qpcSeconds() + 1.5;
                        saveConfig(gApp.cfg);
                    }
                    MessageBeep(MB_ICONASTERISK);
                    gApp.needsRedraw = true;
                    renderOverlay();
                }
                return 0;
            }
            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            int vk = static_cast<int>(wParam);
            if (gApp.renameActive && vk == VK_ESCAPE) { finishProfileRename(false); return 0; }
            if (handleWindowBindCapture(vk)) return 0;
            if (vk == VK_ESCAPE && gApp.menuOpen) {
                setMenuOpenState(false);
                renderOverlay();
                return 0;
            }
            break;
        }

        case WM_CHAR:
            if (gApp.renameActive) {
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch == L'\r') { finishProfileRename(true); return 0; }
                if (ch == L'\b') { if (!gApp.renameBuffer.empty()) gApp.renameBuffer.pop_back(); gApp.needsRedraw = true; renderOverlay(); return 0; }
                if (ch >= 32 && gApp.renameBuffer.size() < 28) { gApp.renameBuffer.push_back(ch); gApp.needsRedraw = true; renderOverlay(); }
                return 0;
            }
            break;

        case WM_RBUTTONDOWN:
            if (handleWindowBindCapture(0x02)) return 0;
            break;

        case WM_MBUTTONDOWN:
            if (handleWindowBindCapture(0x04)) return 0;
            break;

        case WM_XBUTTONDOWN: {
            WORD xb = GET_XBUTTON_WPARAM(wParam);
            int vk = (xb == XBUTTON1) ? 0x05 : 0x06;
            if (handleWindowBindCapture(vk)) return 0;
            break;
        }

        case WM_TIMER:
            if (wParam == TIMER_UI_TICK) {
                onUiTimerTick();
            } else if (wParam == TIMER_TOPMOST) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;

        case WM_SETCURSOR: {
            if (gApp.menuOpen) {
                const int cursorHit = gApp.activeDragHit != HIT_NONE ? gApp.activeDragHit : gApp.hoveredHitId;
                if (isPhotoWidgetHit(cursorHit)) {
                    const int handle = photoHandleFromHit(cursorHit);
                    LPCWSTR cursor = IDC_SIZEALL;
                    if (handle == 2 || handle == 6) cursor = IDC_SIZENS;
                    else if (handle == 4 || handle == 8) cursor = IDC_SIZEWE;
                    else if (handle == 1 || handle == 5) cursor = IDC_SIZENWSE;
                    else if (handle == 3 || handle == 7) cursor = IDC_SIZENESW;
                    SetCursor(LoadCursorW(NULL, cursor));
                    return TRUE;
                }
                if (gApp.activeDragHit == HIT_HEADER_DRAG || gApp.activeDragHit == HIT_HUD_DRAG || gApp.activeDragHit == HIT_PILL_DRAG ||
                    gApp.hoveredHitId  == HIT_HUD_DRAG    || gApp.hoveredHitId  == HIT_PILL_DRAG) {
                    SetCursor(LoadCursorW(NULL, IDC_SIZEALL));
                    return TRUE;
                }
                if (gApp.hoveredHitId != HIT_NONE && gApp.hoveredHitId != HIT_HEADER_DRAG) {
                    SetCursor(LoadCursorW(NULL, IDC_HAND));
                    return TRUE;
                }
            }
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return TRUE;
        }

        case WM_MOUSEMOVE: {
            if (!gApp.menuOpen) return 0;
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (gApp.activeDragHit != HIT_NONE) {
                POINT cur = { mx, my };
                int dx = cur.x - gApp.dragStartMouse.x;
                int dy = cur.y - gApp.dragStartMouse.y;

                const int monitorIndex = std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1));
                const int displayW = gApp.monitors[monitorIndex].bounds.right - gApp.monitors[monitorIndex].bounds.left;
                const int displayH = gApp.monitors[monitorIndex].bounds.bottom - gApp.monitors[monitorIndex].bounds.top;
                const bool movingPhotoWidget = isPhotoWidgetHit(gApp.activeDragHit);
                const bool movingWidget = gApp.activeDragHit == HIT_HEADER_DRAG ||
                                          gApp.activeDragHit == HIT_HUD_DRAG ||
                                          gApp.activeDragHit == HIT_PILL_DRAG;
                if (movingPhotoWidget) {
                    updatePhotoWidgetFromDrag(gApp.activeDragHit, dx, dy, displayW, displayH);
                } else if (movingWidget) {
                    CsLock lk(&gApp.cfgCs);
                    auto& d = gApp.cfg.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
                    auto snapGrid = [&](int value) {
                        if (!gApp.cfg.snapToGrid) return value;
                        constexpr int grid = 8;
                        return ((value + grid / 2) / grid) * grid;
                    };
                    if (gApp.activeDragHit == HIT_HEADER_DRAG) {
                        d.menuX = std::clamp(snapGrid((int)(gApp.dragStartElem.x + dx)), 8, displayW - (int)(624 * gApp.uiScalePx) - 8);
                        d.menuY = std::clamp(snapGrid((int)(gApp.dragStartElem.y + dy)), 8, displayH - (int)(720 * gApp.uiScalePx) - 8);
                    } else if (gApp.activeDragHit == HIT_HUD_DRAG) {
                        d.hudX = std::clamp(snapGrid((int)(gApp.dragStartElem.x + dx)), 8, displayW - (int)(468 * gApp.uiScalePx) - 8);
                        d.hudY = std::clamp(snapGrid((int)(gApp.dragStartElem.y + dy)), 8, displayH - (int)(44 * gApp.uiScalePx) - 8);
                    } else {
                        d.pillX = std::clamp(snapGrid((int)(gApp.dragStartElem.x + dx)), 8, displayW - (int)(242 * gApp.uiScalePx) - 8);
                        d.pillY = std::clamp(snapGrid((int)(gApp.dragStartElem.y + dy)), 8, displayH - (int)(40 * gApp.uiScalePx) - 8);
                    }
                    gApp.needsRedraw = true;
                } else {
                    for (const auto& hz : gApp.hitZones) {
                        if (hz.id == gApp.activeDragHit) {
                            updateSliderFromMouseX(hz, static_cast<float>(mx));
                            break;
                        }
                    }
                }
                if (gApp.needsRedraw) renderOverlay();
                return 0;
            }

            int hit = hitTestZones(static_cast<float>(mx), static_cast<float>(my));
            if (hit != gApp.hoveredHitId) {
                gApp.hoveredHitId = hit;
                gApp.needsRedraw = true;
                renderOverlay();
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (gApp.renameActive) return 0;
            if (!gApp.menuOpen) return 0;
            if (handleWindowBindCapture(0x01)) return 0;
            float mx = static_cast<float>(GET_X_LPARAM(lParam));
            float my = static_cast<float>(GET_Y_LPARAM(lParam));

            HitZone hz = {};
            int hit = hitTestZones(mx, my, &hz);

            if (hit == HIT_NONE) {
                // Check if click is inside main window non-interactive space vs completely outside
                AppConfig snap;
                {
                    CsLock lk(&gApp.cfgCs);
                    snap = gApp.cfg;
                }
                const auto& layout = snap.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
                const RECT mr = gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds;
                RectF winR(static_cast<float>(layout.menuX + mr.left - gApp.virtualX),
                           static_cast<float>(layout.menuY + mr.top - gApp.virtualY),
                           624.0f * gApp.uiScalePx, 720.0f * gApp.uiScalePx);
                if (!winR.Contains(mx, my) && snap.noAltTabLock) {
                    setMenuOpenState(false);
                    renderOverlay();
                }
                return 0;
            }

            if (hit >= PHOTO_SLOT_HIT_BASE && hit < PHOTO_SLOT_HIT_BASE + PHOTO_WIDGET_COUNT) {
                gApp.selectedPhotoWidget = hit - PHOTO_SLOT_HIT_BASE;
                gApp.needsRedraw = true;
                renderOverlay();
                return 0;
            }
            if (hit == PHOTO_ADD_HIT) {
                std::wstring imagePath;
                if (chooseImagePath(imagePath)) {
                    Bitmap* candidate = createDetachedPhotoBitmap(imagePath);
                    const int slot = std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1);
                    uint64_t otherPixels = 0;
                    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
                        if (i != slot && gApp.photoBitmaps[i])
                            otherPixels += static_cast<uint64_t>(gApp.photoBitmaps[i]->GetWidth()) * gApp.photoBitmaps[i]->GetHeight();
                    }
                    if (!candidate || otherPixels + static_cast<uint64_t>(candidate->GetWidth()) * candidate->GetHeight() > PHOTO_TOTAL_PIXELS) {
                        delete candidate;
                        MessageBoxW(hwnd, L"Не удалось загрузить фото: используй PNG, JPG или BMP. Лимит — 30 Мп на изображение и 60 Мп суммарно для виджетов.",
                                    L"Фото-виджет", MB_OK | MB_ICONWARNING);
                    } else {
                        {
                            CsLock lk(&gApp.cfgCs);
                            gApp.cfg.photoWidgets[slot].path = imagePath;
                            saveConfig(gApp.cfg);
                        }
                        delete gApp.photoBitmaps[slot];
                        gApp.photoBitmaps[slot] = candidate;
                        gApp.loadedPhotoPaths[slot] = imagePath;
                        setToast(L"Фото добавлено в слот " + std::to_wstring(slot + 1));
                    }
                }
                renderOverlay();
                return 0;
            }
            if (hit == PHOTO_DELETE_HIT) {
                const int slot = std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1);
                if (!gApp.cfg.photoWidgets[slot].path.empty()) {
                    {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.photoWidgets[slot].path.clear();
                        saveConfig(gApp.cfg);
                    }
                    delete gApp.photoBitmaps[slot];
                    gApp.photoBitmaps[slot] = nullptr;
                    gApp.loadedPhotoPaths[slot].clear();
                    setToast(L"Фото удалено");
                }
                renderOverlay();
                return 0;
            }
            if (hit == PHOTO_FIT_HIT) {
                const int slot = std::clamp(gApp.selectedPhotoWidget, 0, PHOTO_WIDGET_COUNT - 1);
                {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.photoWidgets[slot].fitMode = (gApp.cfg.photoWidgets[slot].fitMode + 1) % 3;
                    saveConfig(gApp.cfg);
                }
                renderOverlay();
                return 0;
            }
            if (isPhotoWidgetHit(hit)) {
                const int slot = std::clamp(photoWidgetIndexFromHit(hit), 0, PHOTO_WIDGET_COUNT - 1);
                gApp.selectedPhotoWidget = slot;
                const int monitorIndex = std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1));
                const int displayW = gApp.monitors[monitorIndex].bounds.right - gApp.monitors[monitorIndex].bounds.left;
                const int displayH = gApp.monitors[monitorIndex].bounds.bottom - gApp.monitors[monitorIndex].bounds.top;
                {
                    CsLock lk(&gApp.cfgCs);
                    auto& w = gApp.cfg.photoWidgets[slot];
                    gApp.photoDragStartX = std::clamp(w.x, 8, std::max(8, displayW - 48));
                    gApp.photoDragStartY = std::clamp(w.y, 8, std::max(8, displayH - 40));
                    gApp.photoDragStartW = std::clamp(w.width, 40, std::max(40, displayW - gApp.photoDragStartX - 8));
                    gApp.photoDragStartH = std::clamp(w.height, 40, std::max(40, displayH - gApp.photoDragStartY - 8));
                    w.x = gApp.photoDragStartX; w.y = gApp.photoDragStartY;
                    w.width = gApp.photoDragStartW; w.height = gApp.photoDragStartH;
                }
                gApp.activeDragHit = hit;
                gApp.dragStartMouse = { static_cast<LONG>(mx), static_cast<LONG>(my) };
                SetCapture(hwnd);
                renderOverlay();
                return 0;
            }

            switch (hit) {
                case HIT_BTN_CLOSE_MENU:
                    setMenuOpenState(false);
                    renderOverlay();
                    break;

                case HIT_BTN_QUIT_APP:
                    DestroyWindow(hwnd);
                    break;

                case HIT_HEADER_DRAG: {
                    gApp.activeDragHit = HIT_HEADER_DRAG;
                    gApp.dragStartMouse = { (LONG)mx, (LONG)my };
                    CsLock lk(&gApp.cfgCs);
                    const auto& d = gApp.cfg.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
                    gApp.dragStartElem = { d.menuX, d.menuY };
                    SetCapture(hwnd);
                    break;
                }

                case HIT_HUD_DRAG: {
                    gApp.activeDragHit = HIT_HUD_DRAG;
                    gApp.dragStartMouse = { (LONG)mx, (LONG)my };
                    CsLock lk(&gApp.cfgCs);
                    const auto& d = gApp.cfg.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
                    int displayW = gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.right - gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.left;
                    int curX = (d.hudX < 0) ? (int)(displayW - 468 * gApp.uiScalePx - 28 * gApp.uiScalePx) : d.hudX;
                    gApp.dragStartElem = { curX, d.hudY };
                    SetCapture(hwnd);
                    break;
                }

                case HIT_PILL_DRAG: {
                    gApp.activeDragHit = HIT_PILL_DRAG;
                    gApp.dragStartMouse = { (LONG)mx, (LONG)my };
                    CsLock lk(&gApp.cfgCs);
                    const auto& d = gApp.cfg.displayLayouts[std::clamp(gApp.activeMonitor, 0, 7)];
                    int displayW = gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.right - gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.left;
                    int displayH = gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.bottom - gApp.monitors[std::clamp(gApp.activeMonitor, 0, std::max(0, gApp.monitorCount - 1))].bounds.top;
                    int curX = (d.pillX < 0) ? (int)(displayW - 274 * gApp.uiScalePx) : d.pillX;
                    int curY = (d.pillY < 0) ? (int)(displayH * 0.74f) : d.pillY;
                    gApp.dragStartElem = { curX, curY };
                    SetCapture(hwnd);
                    break;
                }

                case HIT_TAB_0: case HIT_TAB_1: case HIT_TAB_2: case HIT_TAB_3: case HIT_TAB_4: case HIT_TAB_5: case HIT_TAB_6:
                    gApp.currentTab = hit - HIT_TAB_0;
                    gApp.capturingHitId = HIT_NONE;
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;

                case HIT_SLD_CPS:
                case HIT_SLD_DELAY:
                case HIT_SLD_HOLD:
                case HIT_SLD_JITTER:
                case HIT_SLD_OPACITY:
                case HIT_SLD_PING:
                case HIT_SLD_UI_SCALE:
                case HIT_SLD_BACKGROUND_OPACITY:
                case HIT_SLD_FONT_SCALE:
                case HIT_SLD_PHOTO_OPACITY:
                    gApp.activeDragHit = hit;
                    SetCapture(hwnd);
                    updateSliderFromMouseX(hz, mx);
                    renderOverlay();
                    break;

                case HIT_TOG_MACRO: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.macroEnabled = !gApp.cfg.macroEnabled;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_TEST_MODE:
                case HIT_TOG_PRECISE_TIMING:
                case HIT_TOG_ANIMATIONS: {
                    CsLock lk(&gApp.cfgCs);
                    if (hit == HIT_TOG_TEST_MODE) {
                        gApp.cfg.testMode = !gApp.cfg.testMode;
                        setToast(gApp.cfg.testMode ? L"ТЕСТ: ввод не отправляется" : L"Тестовый режим выключен");
                    } else if (hit == HIT_TOG_PRECISE_TIMING) {
                        gApp.cfg.preciseTiming = !gApp.cfg.preciseTiming;
                        syncTimerResolution(gApp.cfg.preciseTiming);
                        setToast(gApp.cfg.preciseTiming ? L"Точный режим: активное ожидание до 1 мс" : L"Экономный режим: ожидание таймером Windows");
                    } else {
                        gApp.cfg.animationsEnabled = !gApp.cfg.animationsEnabled;
                        setToast(gApp.cfg.animationsEnabled ? L"Анимации включены" : L"Анимации выключены");
                    }
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_PING_SYNC: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.pingSync = !gApp.cfg.pingSync;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_NO_ALTTAB: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.noAltTabLock = !gApp.cfg.noAltTabLock;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_SEG_OUT_F:
                    tryAssignQuickVk(false, 0x46);
                    renderOverlay();
                    break;
                case HIT_SEG_OUT_LMB:
                    tryAssignQuickVk(false, 0x01);
                    renderOverlay();
                    break;
                case HIT_SEG_OUT_CUSTOM:
                    gApp.currentTab = 4;
                    startInteractiveCapture(HIT_BIND_OUTPUT);
                    renderOverlay();
                    break;
                case HIT_SEG_MODE_HOLD:
                case HIT_SEG_MODE_TOGGLE: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.toggleMode = (hit == HIT_SEG_MODE_TOGGLE);
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_DUAL_OUTPUT:
                case HIT_TOG_BURST:
                case HIT_TOG_TOAST: {
                    CsLock lk(&gApp.cfgCs);
                    if (hit == HIT_TOG_DUAL_OUTPUT) gApp.cfg.dualOutput = !gApp.cfg.dualOutput;
                    else if (hit == HIT_TOG_BURST) gApp.cfg.burstEnabled = !gApp.cfg.burstEnabled;
                    else gApp.cfg.toastEnabled = !gApp.cfg.toastEnabled;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_BTN_THEME: {
                    {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.themeIndex = (gApp.cfg.themeIndex + 1) % 5;
                        gApp.cfg.customAccentEnabled = false;
                        saveConfig(gApp.cfg);
                    }
                    MessageBeep(MB_ICONASTERISK);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_BTN_CUSTOM_ACCENT: {
                    AppConfig snap; { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
                    COLORREF color = static_cast<COLORREF>(snap.customAccentRgb);
                    if (chooseCustomAccentColor(color)) {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.customAccentRgb = static_cast<int>(color);
                        gApp.cfg.customAccentEnabled = true;
                        saveConfig(gApp.cfg);
                    }
                    renderOverlay();
                    break;
                }
                case HIT_BTN_CHOOSE_BACKGROUND: {
                    std::wstring imagePath;
                    if (chooseImagePath(imagePath)) {
                        AppConfig before; { CsLock lk(&gApp.cfgCs); before = gApp.cfg; }
                        if (reloadCustomBackground(imagePath)) {
                            renderOverlay();
                            const int useImage = MessageBoxW(hwnd,
                                L"Предпросмотр нового фона показан под панелями интерфейса. Использовать это изображение?",
                                L"Предпросмотр фона", MB_YESNO | MB_ICONQUESTION);
                            if (useImage == IDYES) {
                                CsLock lk(&gApp.cfgCs);
                                gApp.cfg.backgroundPath = imagePath;
                                saveConfig(gApp.cfg);
                                setToast(L"Свой фон установлен");
                            } else {
                                reloadCustomBackground(before.backgroundPath);
                            }
                        } else {
                            MessageBoxW(hwnd, L"Не удалось загрузить изображение. Выберите корректный PNG, JPG или BMP размером не больше 100 Мп.", L"Свой фон", MB_OK | MB_ICONWARNING);
                        }
                    }
                    renderOverlay();
                    break;
                }
                case HIT_BTN_BACKGROUND_FIT: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.backgroundFit = (gApp.cfg.backgroundFit + 1) % 3;
                    saveConfig(gApp.cfg);
                    setToast(gApp.cfg.backgroundFit == 0 ? L"Фон: заполнить" : (gApp.cfg.backgroundFit == 1 ? L"Фон: вписать" : L"Фон: растянуть"));
                    renderOverlay();
                    break;
                }
                case HIT_BTN_RESET_BACKGROUND: {
                    {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.backgroundPath.clear();
                        gApp.cfg.backgroundOpacity = 42;
                        gApp.cfg.backgroundFit = 0;
                        saveConfig(gApp.cfg);
                    }
                    reloadCustomBackground(L"");
                    setToast(L"Установлен стандартный фон");
                    renderOverlay();
                    break;
                }
                case HIT_TOG_AUTOSTART: {
                    AppConfig snap; { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
                    const bool enable = !snap.startWithWindows;
                    if (setRunAtWindowsStartup(enable)) {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.startWithWindows = enable;
                        saveConfig(gApp.cfg);
                        setToast(enable ? L"Запуск с Windows включён" : L"Запуск с Windows отключён");
                    } else {
                        MessageBoxW(hwnd, L"Не удалось изменить запись автозапуска текущего пользователя.", L"ByAzen", MB_OK | MB_ICONWARNING);
                    }
                    renderOverlay();
                    break;
                }
                case HIT_TOG_COMPACT_MODE:
                case HIT_TOG_HIGH_CONTRAST:
                case HIT_TOG_SNAP_GRID:
                case HIT_TOG_SESSION_HISTORY: {
                    CsLock lk(&gApp.cfgCs);
                    if (hit == HIT_TOG_COMPACT_MODE) gApp.cfg.compactMode = !gApp.cfg.compactMode;
                    else if (hit == HIT_TOG_HIGH_CONTRAST) gApp.cfg.highContrast = !gApp.cfg.highContrast;
                    else if (hit == HIT_TOG_SNAP_GRID) gApp.cfg.snapToGrid = !gApp.cfg.snapToGrid;
                    else gApp.cfg.sessionHistoryEnabled = !gApp.cfg.sessionHistoryEnabled;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_BTN_SEARCH_SETTINGS:
                    searchSettings();
                    break;
                case HIT_BTN_UNDO:
                    undoLastChange();
                    break;
                case HIT_BTN_EXPORT_STATS:
                    exportSessionCsv();
                    break;
                case HIT_BTN_DIAGNOSTICS:
                    exportDiagnostics();
                    break;
                case HIT_BTN_RESTORE_BACKUP:
                    restoreBackup();
                    break;
                case HIT_BTN_CLEAR_LOG:
                    clearDiagnosticLog();
                    break;
                case HIT_BTN_VALIDATE_CONFIG:
                    validateCurrentConfig();
                    break;
                case HIT_BTN_ONBOARDING:
                    showOnboarding();
                    break;
                case HIT_BTN_OPEN_CONFIG_FOLDER:
                    ShellExecuteW(hwnd, L"open", getProgramDirectory().c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                case HIT_BTN_ABOUT:
                    MessageBoxW(hwnd, L"ByAzen 3.6.0\nНативный Windows overlay\nСборка x64 • конфигурация хранится локально\nПодпись издателя отсутствует.",
                                L"О программе", MB_OK | MB_ICONINFORMATION);
                    break;
                case HIT_TOG_ONLY_ROBLOX: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.onlyInRoblox = !gApp.cfg.onlyInRoblox;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_CHAT_SAFE: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.chatSafePause = !gApp.cfg.chatSafePause;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_SHOW_HUD: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.showHudBar = !gApp.cfg.showHudBar;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_TOG_SHOW_PILL: {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.showKeyPill = !gApp.cfg.showKeyPill;
                    saveConfig(gApp.cfg);
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_PROF_0: case HIT_PROF_1: case HIT_PROF_2: case HIT_PROF_3:
                    applyProfileByIndex(hit - HIT_PROF_0);
                    renderOverlay();
                    break;

                case HIT_BTN_SAVE_PROF: {
                    {
                        CsLock lk(&gApp.cfgCs);
                        int ap = std::clamp(gApp.cfg.activeProfile, 0, 3);
                        gApp.cfg.profiles[ap].targetCps = gApp.cfg.targetCps;
                        gApp.cfg.profiles[ap].delayMs   = gApp.cfg.delayMs;
                        gApp.cfg.profiles[ap].holdMs    = gApp.cfg.holdMs;
                        gApp.cfg.profiles[ap].jitterPct = gApp.cfg.jitterPct;
                        gApp.cfg.profiles[ap].pingSync  = gApp.cfg.pingSync;
                        saveConfig(gApp.cfg);
                    }
                    gApp.profSavedUntil = qpcSeconds() + 1.4;
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }

                case HIT_BTN_RENAME_PROFILE: {
                    AppConfig snap;
                    { CsLock lk(&gApp.cfgCs); snap = gApp.cfg; }
                    gApp.renameProfileIndex = std::clamp(snap.activeProfile, 0, 3);
                    gApp.renameBuffer = snap.profiles[gApp.renameProfileIndex].name;
                    gApp.renameActive = true;
                    gApp.bindStatusText = L"Введи название профиля: Enter — сохранить, Esc — отменить.";
                    gApp.needsRedraw = true;
                    renderOverlay();
                    break;
                }
                case HIT_BTN_EMERGENCY_STOP: {
                    {
                        CsLock lk(&gApp.cfgCs);
                        gApp.cfg.macroEnabled = false;
                        gApp.cfg.toggleMode = false;
                        saveConfig(gApp.cfg);
                    }
                    setToast(L"Аварийная пауза: макрос выключен");
                    MessageBeep(MB_ICONEXCLAMATION);
                    renderOverlay();
                    break;
                }
                case HIT_BTN_NEXT_MONITOR:
                    selectNextMonitor();
                    break;
                case HIT_BTN_RESET_SESSION:
                    gApp.sessionCycles.store(0, std::memory_order_relaxed);
                    gApp.sessionStartedAt = qpcSeconds();
                    GetLocalTime(&gApp.sessionWallStart);
                    gApp.lastSessionSecond = -1;
                    gApp.lastSessionCycles = UINT64_MAX;
                    setToast(L"Статистика сессии сброшена");
                    renderOverlay();
                    break;
                case HIT_BTN_CONFIG_BACKUP:
                    copyConfigFile(false);
                    renderOverlay();
                    break;
                case HIT_BTN_CONFIG_EXPORT:
                    copyConfigFile(true);
                    renderOverlay();
                    break;
                case HIT_BTN_CONFIG_IMPORT:
                    importConfigFile();
                    renderOverlay();
                    break;
                case HIT_BTN_RESET_DEFAULTS:
                    if (MessageBoxW(hwnd, L"Сбросить все привязки, профили и параметры к исходным? Текущий файл будет сохранён как .bak.", L"ByAzen", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        std::wstring configPath = getConfigFilePath();
                        createTimestampedBackup();
                        std::wstring backupPath = configPath + L".bak";
                        if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES) CopyFileW(configPath.c_str(), backupPath.c_str(), FALSE);
                        resetAllSettings();
                    }
                    break;
                case HIT_BTN_RESET_POS:
                    resetWidgetPositions();
                    renderOverlay();
                    break;

                case HIT_BIND_TRIGGER:
                case HIT_BIND_OUTPUT:
                case HIT_BIND_MENU:
                case HIT_BIND_RADIAL:
                case HIT_BIND_PANIC:
                    startInteractiveCapture(hit);
                    renderOverlay();
                    break;

                case HIT_QUICK_F:
                    tryAssignQuickVk(false, 0x46);
                    renderOverlay();
                    break;
                case HIT_QUICK_LMB:
                    tryAssignQuickVk(false, 0x01);
                    renderOverlay();
                    break;
                case HIT_QUICK_M4:
                    tryAssignQuickVk(true, 0x05);
                    renderOverlay();
                    break;

                default:
                    break;
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (!gApp.menuOpen || gApp.renameActive ||
                gApp.activeDragHit != HIT_NONE || gApp.capturingHitId != HIT_NONE) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; // screen coordinates
            ScreenToClient(hwnd, &pt);
            HitZone hz = {};
            hitTestZones(static_cast<float>(pt.x), static_cast<float>(pt.y), &hz);
            if (hz.maxVal > hz.minVal) { // only sliders carry a value range
                static int wheelRemainder = 0;
                wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
                const int notches = wheelRemainder / WHEEL_DELTA;
                wheelRemainder -= notches * WHEEL_DELTA;
                const int step = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) ? 10 : 1;
                if (notches != 0 && nudgeSliderValue(hz, notches * step)) {
                    gApp.configSaveDueAt = qpcSeconds() + 0.4;
                    renderOverlay();
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (gApp.activeDragHit != HIT_NONE) {
                gApp.activeDragHit = HIT_NONE;
                ReleaseCapture();
                CsLock lk(&gApp.cfgCs);
                saveConfig(gApp.cfg);
            }
            return 0;
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP) {
                setMenuOpenState(!gApp.menuOpen);
                renderOverlay();
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                showTrayContextMenu();
            }
            return 0;
        }

        case WM_COMMAND: {
            UINT cmd = LOWORD(wParam);
            if (cmd == ID_TRAY_TOGGLE_MENU) {
                setMenuOpenState(!gApp.menuOpen);
                renderOverlay();
            } else if (cmd == ID_TRAY_TOGGLE_MAC) {
                {
                    CsLock lk(&gApp.cfgCs);
                    gApp.cfg.macroEnabled = !gApp.cfg.macroEnabled;
                    gApp.toastText = gApp.cfg.macroEnabled ? L"Макрос включён" : L"Макрос на паузе";
                    gApp.toastUntil = qpcSeconds() + 1.5;
                    saveConfig(gApp.cfg);
                }
                MessageBeep(MB_ICONASTERISK);
                gApp.needsRedraw = true;
                renderOverlay();
            } else if (cmd == ID_TRAY_RESET_POS) {
                resetWidgetPositions();
                renderOverlay();
            } else if (cmd == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY:
            removeSystemTray();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Entrypoint (wWinMain)
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR commandLine, int) {
    // Single-instance check: if already running, reopen its End-key overlay menu!
    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Local\\ByAzenOverlaySingletonV2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ByAzenOverlayWndClass", NULL);
        if (existing) {
            PostMessageW(existing, WM_SHOW_EXISTING, 0, 0);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    gApp.hInst = hInstance;
    gApp.hAppIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    loadEmbeddedBackground();
    gApp.sessionStartedAt = qpcSeconds();
    GetLocalTime(&gApp.sessionWallStart);

    gApp.screenW = GetSystemMetrics(SM_CXSCREEN);
    gApp.screenH = GetSystemMetrics(SM_CYSCREEN);
    gApp.S       = (gApp.screenH >= 1350) ? (static_cast<float>(gApp.screenH) / 1080.0f) : 1.0f;

    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 30) {
        gApp.monitorHz = static_cast<int>(dm.dmDisplayFrequency);
    }

    const bool firstRun = (GetFileAttributesW(getConfigFilePath().c_str()) == INVALID_FILE_ATTRIBUTES);
    InitializeCriticalSection(&gApp.cfgCs);
    loadConfig(gApp.cfg);
    const bool autoLaunched = commandLine && wcsstr(commandLine, L"--autostart") != nullptr;
    reloadCustomBackground(gApp.cfg.backgroundPath);
    setRunAtWindowsStartup(gApp.cfg.startWithWindows);
    if (firstRun || autoLaunched) {
        gApp.cfg.macroEnabled = false;
        gApp.cfg.testMode = true;
        saveConfig(gApp.cfg);
    }
    gApp.activeMonitor = gApp.cfg.activeMonitor;
    syncTimerResolution(gApp.cfg.preciseTiming);

    recreateDibSurface();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = gApp.hAppIcon;
    wc.hIconSm       = gApp.hAppIcon;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"ByAzenOverlayWndClass";
    RegisterClassExW(&wc);

    gApp.menuOpenedAt = qpcSeconds();
    gApp.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ByAzenOverlayWndClass",
        L"ByAzen Overlay",
        WS_POPUP,
        gApp.virtualX, gApp.virtualY, gApp.screenW, gApp.screenH,
        NULL, NULL, hInstance, NULL
    );

    if (!gApp.hwnd) {
        if (gApp.timerResolutionEnabled) timeEndPeriod(1);
        DeleteCriticalSection(&gApp.cfgCs);
        GdiplusShutdown(gdiplusToken);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    ShowWindow(gApp.hwnd, SW_SHOW);
    applyClickThrough(false);
    initSystemTray();
    refreshRegisteredHotkeys();
    renderOverlay();
    if (firstRun) showOnboarding();

    HANDLE hMacroThr = CreateThread(NULL, 0, macroWorkerThread, NULL, 0, NULL);

    SetTimer(gApp.hwnd, TIMER_UI_TICK, 14, NULL);
    SetTimer(gApp.hwnd, TIMER_TOPMOST, 1500, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    flushPendingConfigSave();
    unregisterAppHotkeys();
    KillTimer(gApp.hwnd, TIMER_UI_TICK);
    KillTimer(gApp.hwnd, TIMER_TOPMOST);

    gApp.running.store(false);
    if (hMacroThr) {
        WaitForSingleObject(hMacroThr, 1500);
        CloseHandle(hMacroThr);
    }
    appendSessionHistory();
    delete gApp.customBackground;
    gApp.customBackground = nullptr;
    delete gApp.defaultBackground;
    gApp.defaultBackground = nullptr;
    for (int i = 0; i < PHOTO_WIDGET_COUNT; ++i) {
        delete gApp.photoBitmaps[i];
        gApp.photoBitmaps[i] = nullptr;
    }

    if (gApp.memDC) {
        SelectObject(gApp.memDC, gApp.oldBmp);
        if (gApp.dibBmp) DeleteObject(gApp.dibBmp);
        DeleteDC(gApp.memDC);
    }

    DeleteCriticalSection(&gApp.cfgCs);
    GdiplusShutdown(gdiplusToken);
    if (gApp.timerResolutionEnabled) timeEndPeriod(1);
    if (hMutex) CloseHandle(hMutex);
    return 0;
}
