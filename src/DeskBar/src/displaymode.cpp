#include "pch.h"
#include "app.h"
#include "bar.h"
#include "flyout.h"
#include "quick.h"

// DeskMode's core, inside DeskBar (one process less): every screen's refresh rate, the power mode, and the two presets. The same calls DeskMode used and proved
// safe on this PC (ChangeDisplaySettingsEx, queued with CDS_NORESET and applied in one commit, then checked and retried); nothing here touches the GPU driver directly.

namespace {
bool IsInternalTarget(const ws& gdiName) {   // the built-in panel: Windows' own display topology says which output is internal
    UINT32 np = 0, nm = 0; if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np); std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return false;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{}; src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; src.header.size = sizeof src; src.header.adapterId = paths[i].sourceInfo.adapterId; src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || !EqI(src.viewGdiDeviceName, gdiName)) continue;
        auto t = paths[i].targetInfo.outputTechnology;
        return t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
    }
    return false;
}
ws FriendlyName(const ws& gdiName, const ws& fallback) {   // the monitor's own name from its EDID ("LG ULTRAGEAR"), not "Generic PnP Monitor"
    UINT32 np = 0, nm = 0; if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return fallback;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np); std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return fallback;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{}; src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; src.header.size = sizeof src; src.header.adapterId = paths[i].sourceInfo.adapterId; src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || !EqI(src.viewGdiDeviceName, gdiName)) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{}; tn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME; tn.header.size = sizeof tn; tn.header.adapterId = paths[i].targetInfo.adapterId; tn.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS && tn.monitorFriendlyDeviceName[0]) return tn.monitorFriendlyDeviceName;
    }
    return fallback;
}
const GUID kModes[3] = {{0, 0, 0, {0}}, {0x3af9b8d9, 0x7c97, 0x431d, {0xad, 0x78, 0x34, 0xa8, 0xbf, 0xea, 0x43, 0x9f}}, {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}}};
using PowerGetFn = DWORD(WINAPI*)(GUID*); using PowerSetFn = DWORD(WINAPI*)(const GUID*);
bool OnAc() { SYSTEM_POWER_STATUS s{}; return !GetSystemPowerStatus(&s) || s.ACLineStatus != 0; }
}

std::vector<ScreenMode> ScreenModes() {
    std::vector<ScreenMode> out;
    for (DWORD i = 0;; i++) {
        DISPLAY_DEVICEW ad{sizeof ad}; if (!EnumDisplayDevicesW(nullptr, i, &ad, 0)) break;
        if (!(ad.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        DEVMODEW cur{}; cur.dmSize = sizeof cur; if (!EnumDisplaySettingsW(ad.DeviceName, ENUM_CURRENT_SETTINGS, &cur)) continue;
        DISPLAY_DEVICEW mon{sizeof mon}; EnumDisplayDevicesW(ad.DeviceName, 0, &mon, 0);
        ScreenMode s; s.device = ad.DeviceName; s.hz = (int)cur.dmDisplayFrequency; s.internal = IsInternalTarget(ad.DeviceName);
        s.name = s.internal ? L"Built-in screen" : FriendlyName(ad.DeviceName, mon.DeviceString[0] ? ws(mon.DeviceString) : ws(L"Monitor"));
        std::set<int> rates;
        for (DWORD m = 0;; m++) { DEVMODEW dm{}; dm.dmSize = sizeof dm; if (!EnumDisplaySettingsW(ad.DeviceName, m, &dm)) break; if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight && dm.dmDisplayFrequency > 1) rates.insert((int)dm.dmDisplayFrequency); }
        s.rates.assign(rates.begin(), rates.end()); out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), [](const ScreenMode& a, const ScreenMode& b) { return a.internal && !b.internal; });
    return out;
}

// All the changes queued and applied in one commit (one blink), then checked: Windows reshuffles the screens while the power mode changes and can drop a request,
// so it looks again and retries a few times (DeskMode's "Go easy stayed at 120 Hz" fix).
bool SetScreenRates(const std::vector<std::pair<ws, int>>& want) {
    for (int attempt = 0; attempt < 4; attempt++) {
        int queued = 0;
        for (auto& [dev, hz] : want) {
            DEVMODEW dm{}; dm.dmSize = sizeof dm; if (!EnumDisplaySettingsW(dev.c_str(), ENUM_CURRENT_SETTINGS, &dm) || (int)dm.dmDisplayFrequency == hz) continue;
            dm.dmDisplayFrequency = hz; dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
            if (ChangeDisplaySettingsExW(dev.c_str(), &dm, nullptr, CDS_UPDATEREGISTRY | CDS_NORESET, nullptr) == DISP_CHANGE_SUCCESSFUL) queued++;
        }
        if (!queued) return true;   // everything is where it should be
        LONG r = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr); Log(L"display: %d rate change(s) committed, result %ld (try %d)", queued, r, attempt + 1);
        Sleep(600);
    }
    for (auto& [dev, hz] : want) { DEVMODEW dm{}; dm.dmSize = sizeof dm; if (EnumDisplaySettingsW(dev.c_str(), ENUM_CURRENT_SETTINGS, &dm) && (int)dm.dmDisplayFrequency != hz) return false; }
    return true;
}

int PowerModeNow() {   // 0 recommended, 1 better, 2 best performance; -1 unknown
    static auto get = (PowerGetFn)GetProcAddress(LoadLibraryW(L"powrprof.dll"), OnAc() ? "PowerGetUserConfiguredACPowerMode" : "PowerGetUserConfiguredDCPowerMode");
    auto fn = (PowerGetFn)GetProcAddress(GetModuleHandleW(L"powrprof.dll"), OnAc() ? "PowerGetUserConfiguredACPowerMode" : "PowerGetUserConfiguredDCPowerMode"); (void)get;
    GUID g{}; if (!fn || fn(&g) != 0) return -1;
    for (int i = 0; i < 3; i++) if (IsEqualGUID(g, kModes[i])) return i;
    return -1;
}
void SetPowerModeNow(int mode) {
    if (mode < 0 || mode > 2) return; LoadLibraryW(L"powrprof.dll");
    auto fn = (PowerSetFn)GetProcAddress(GetModuleHandleW(L"powrprof.dll"), OnAc() ? "PowerSetUserConfiguredACPowerMode" : "PowerSetUserConfiguredDCPowerMode");
    if (fn) fn(&kModes[mode]);
}

// Go max: every screen at its highest rate, Best performance, Energy saver off. Go easy: 60 Hz (or the nearest below), Recommended, Energy saver on.
// The rate a preset wants for a screen: 0 = its highest; otherwise that rate, or the nearest one below it (or its lowest).
int PickRate(const ScreenMode& s, int want) { if (s.rates.empty()) return s.hz; if (want <= 0) return s.rates.back(); int hz = s.rates.front(); for (int r : s.rates) if (r <= want) hz = r; return hz; }
// Go max / Go easy, as chosen in DeskMode's settings: the rates, the power mode, Energy saver.
void DeskModeGo(bool max) {
    std::vector<std::pair<ws, int>> want;
    for (auto& s : ScreenModes()) { if (s.rates.empty()) continue; want.push_back({s.device, PickRate(s, max ? (s.internal ? S.dmMaxLaptopHz : S.dmMaxMonitorHz) : S.dmEasyHz)}); }
    SetPowerModeNow(max ? S.dmMaxMode : S.dmEasyMode);   // first: Windows reshuffles the screens when the power mode changes
    if (EnergySaverIsOn() >= 0) { if (max && S.dmMaxSaverOff) EnergySaverSet(false); if (!max && S.dmEasySaverOn) EnergySaverSet(true); }
    SetScreenRates(want); Log(L"deskmode: %s", max ? L"Go max" : L"Go easy");
}

// The old DeskMode app still running would fight over the same screens: while it runs, DeskBar's copy only shows and acts when asked, never by itself.
bool DeskModeAppRunning() {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return false; PROCESSENTRY32W pe{sizeof pe}; bool found = false;
    for (BOOL ok = Process32FirstW(s, &pe); ok && !found; ok = Process32NextW(s, &pe)) found = EqI(pe.szExeFile, L"DeskMode.exe");
    CloseHandle(s); return found;
}

// By itself (DeskMode's main job): a monitor and the charger connected -> Go max; one of them gone -> back to how the screens and power mode were before.
namespace { bool g_docked = false, g_haveSnap = false; std::vector<std::pair<ws, int>> g_snapRates; int g_snapMode = -1; uint64_t g_lastCheck = 0; }
void DeskModeTick() {
    uint64_t now = Now(); if (now - g_lastCheck < 2000) return; g_lastCheck = now;
    if (!S.dmAutoMax && !S.dmRestore) return;
    auto scr = ScreenModes(); bool ext = std::any_of(scr.begin(), scr.end(), [](const ScreenMode& m) { return !m.internal; }), docked = ext && OnAc();
    if (docked == g_docked) return;
    if (DeskModeAppRunning()) { g_docked = docked; return; }
    if (S.dmGameLock) for (auto& m : Monitors()) if (IsFullscreenOn(m.bounds)) return;   // a full-screen game: leave the screens alone, look again later
    g_docked = docked;
    if (docked && S.dmAutoMax) {
        g_snapRates.clear(); for (auto& s : scr) g_snapRates.push_back({s.device, s.hz}); g_snapMode = PowerModeNow(); g_haveSnap = true;
        std::thread([] { DeskModeGo(true); }).detach();
    } else if (!docked && S.dmRestore && g_haveSnap) {
        auto rates = g_snapRates; int mode = g_snapMode; g_haveSnap = false;
        std::thread([rates, mode] { if (mode >= 0) SetPowerModeNow(mode); SetScreenRates(rates); Log(L"deskmode: back to how it was"); }).detach();
    }
}

// DeskMode's "monitor first": the monitor at its highest rate, the built-in screen at 60 Hz, Best performance.
void DeskModeFirst() {
    std::vector<std::pair<ws, int>> want;
    for (auto& s : ScreenModes()) { if (s.rates.empty()) continue; want.push_back({s.device, PickRate(s, s.internal ? S.dmEasyHz : S.dmMaxMonitorHz)}); }
    SetPowerModeNow(S.dmMaxMode); if (EnergySaverIsOn() >= 0) EnergySaverSet(false); SetScreenRates(want); Log(L"deskmode: monitor first");
}

// DeskMode's Ctrl+Alt+F: the window in front without its frame, covering its whole screen, so a game in "windowed" mode never switches the monitor's mode
// (no blink when alt-tabbing). Pressed again, the window gets its frame and place back.
namespace { std::map<HWND, std::pair<LONG_PTR, RECT>> g_framed; }
void ToggleBorderless(HWND h) {
    if (!h || !IsWindow(h)) return; DWORD pid = 0; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId()) return;
    if (auto it = g_framed.find(h); it != g_framed.end()) {
        SetWindowLongPtrW(h, GWL_STYLE, it->second.first); RECT r = it->second.second;
        SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_FRAMECHANGED); g_framed.erase(it); return;
    }
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE); RECT r; GetWindowRect(h, &r); g_framed[h] = {st, r};
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowLongPtrW(h, GWL_STYLE, st & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU));
    SetWindowPos(h, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_FRAMECHANGED);
}

// The DeskMode panel (the taskbar's DeskMode button, Ctrl+Alt+D): Quick Settings opened straight on the DeskMode page; again closes it.
void OpenDeskModePanel(Wnd* from) {
    auto* q = (QuickPanel*)Panel_(Panel::Quick); if (!q) return;
    bool onIt = q->shown && !q->hiding && q->page && q->foot && !q->foot->visible;   // already showing a DeskMode page
    if (q->shown && !q->hiding && !onIt) { q->ShowDetail(MakeDisplayPage(q), true); return; }
    TogglePanel(Panel::Quick, from);
    if (q->shown && !q->hiding) q->ShowDetail(MakeDisplayPage(q), true);
}

// DeskMode's shortcuts: Ctrl+Alt+D the panel, Ctrl+Alt+M Go max, Ctrl+Alt+B Go easy. While the old DeskMode runs it owns them, and the registration simply fails.
void DeskModeHotkeysStart() {
    std::thread([] {
        bool any = false; for (int i = 0; i < 4; i++) any |= RegisterHotKey(nullptr, 0xD300 + i, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, "DMBF"[i]) != 0;
        if (!any) return;
        MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            if (m.message != WM_HOTKEY) continue;
            int id = (int)m.wParam - 0xD300;
            if (id == 0) UiPost([] { if (!AllBars().empty()) OpenDeskModePanel(AllBars()[0]); });
            else if (id == 3) ToggleBorderless(GetForegroundWindow());
            else std::thread([id] { DeskModeGo(id == 1); }).detach();
        }
    }).detach();
}
