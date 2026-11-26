#include "pch.h"
#include <hidusage.h>
#include <hidpi.h>
#include "app.h"

// Three-finger swipe down on a precision touchpad = minimise everything. Windows does this through its own taskbar, which is hidden while
// DeskBar runs and ignores show-desktop commands (see ToggleDesktop in shellx.cpp), so the gesture did nothing. We read the touchpad
// ourselves (raw input, works in the background) and, if windows are still open just after the swipe, put them away with ToggleDesktop.
// A PC where Windows' gesture still works has nothing open by then, and we do nothing.

int SwipeTracker::Feed(int fingers, float x, float y) {
    if (fingers > 3) { armed = false; return 0; }   // four fingers is Windows' desktop switch, not ours
    if (fingers == 3) { if (!armed) { armed = true; sx = x; sy = y; } lx = x; ly = y; return 0; }
    if (!armed) return 0;
    armed = false; float dx = lx - sx, dy = ly - sy;   // pad coordinates, 0..1, y grows downwards
    return dy > 0.15f && std::fabs(dx) < dy ? 1 : 0;
}

namespace {
struct Pad { std::vector<BYTE> pp; std::vector<USHORT> links; float xmax = 1, ymax = 1; bool screen = false; };   // links: one per contact slot in a report; screen = the touch screen, not the touchpad
std::map<HANDLE, Pad> g_pads;
// The touch screen, for the touch-resize log only: where each finger really came down, how big it was, and where it was last. Windows moves the cursor to its own
// idea of the touch point, so the move/size hook's cursor can't say whether a finger that resized a window landed on the guard strip or above it.
struct Down { ULONG id; uint64_t at; float x, y, lx, ly; ULONG w, h; bool up; };
std::mutex g_tdMu; std::deque<Down> g_tds; ULONG g_sframe = 0; float g_sxmax = 1; std::map<ULONG, std::pair<float, float>> g_down; ULONG g_frame = 0; SwipeTracker g_swipe;

Pad& PadOf(HANDLE dev) {
    auto it = g_pads.find(dev); if (it != g_pads.end()) return it->second;
    Pad& p = g_pads[dev]; UINT sz = 0;
    RID_DEVICE_INFO di{sizeof di}; UINT ds = sizeof di; p.screen = GetRawInputDeviceInfoW(dev, RIDI_DEVICEINFO, &di, &ds) != (UINT)-1 && di.dwType == RIM_TYPEHID && di.hid.usUsagePage == HID_USAGE_PAGE_DIGITIZER && di.hid.usUsage == HID_USAGE_DIGITIZER_TOUCH_SCREEN;
    if (GetRawInputDeviceInfoW(dev, RIDI_PREPARSEDDATA, nullptr, &sz) != 0 || !sz) return p;
    p.pp.resize(sz); if (GetRawInputDeviceInfoW(dev, RIDI_PREPARSEDDATA, p.pp.data(), &sz) == (UINT)-1) { p.pp.clear(); return p; }
    auto pp = (PHIDP_PREPARSED_DATA)p.pp.data(); HIDP_CAPS caps{};
    if (HidP_GetCaps(pp, &caps) != HIDP_STATUS_SUCCESS) { p.pp.clear(); return p; }
    std::vector<HIDP_VALUE_CAPS> vc(caps.NumberInputValueCaps); USHORT n = caps.NumberInputValueCaps;
    if (n && HidP_GetValueCaps(HidP_Input, vc.data(), &n, pp) == HIDP_STATUS_SUCCESS)
        for (USHORT i = 0; i < n; i++) {
            auto& c = vc[i]; if (c.UsagePage != HID_USAGE_PAGE_GENERIC || c.IsRange) continue;
            if (c.NotRange.Usage == HID_USAGE_GENERIC_X) p.xmax = (float)(std::max)(1L, c.LogicalMax);
            if (c.NotRange.Usage == HID_USAGE_GENERIC_Y) { p.ymax = (float)(std::max)(1L, c.LogicalMax); p.links.push_back(c.LinkCollection); }
        }
    if (p.links.empty()) p.pp.clear();
    return p;
}
void OnInput(HRAWINPUT h) {
    UINT sz = 0; GetRawInputData(h, RID_INPUT, nullptr, &sz, sizeof(RAWINPUTHEADER)); if (!sz) return;
    std::vector<BYTE> buf(sz); if (GetRawInputData(h, RID_INPUT, buf.data(), &sz, sizeof(RAWINPUTHEADER)) != sz) return;
    auto* ri = (RAWINPUT*)buf.data(); if (ri->header.dwType != RIM_TYPEHID) return;
    Pad& pad = PadOf(ri->header.hDevice); if (pad.pp.empty()) return;
    auto pp = (PHIDP_PREPARSED_DATA)pad.pp.data(); ULONG len = ri->data.hid.dwSizeHid;
    for (DWORD r = 0; r < ri->data.hid.dwCount; r++) {
        PCHAR rep = (PCHAR)ri->data.hid.bRawData + r * len;
        // hybrid pads split a frame over several reports: only the first carries the contact count, later ones count 0
        ULONG& frame = pad.screen ? g_sframe : g_frame;
        ULONG count = 0; if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, 0x54 /*contact count*/, &count, pp, rep, len) == HIDP_STATUS_SUCCESS && count) frame = count;
        for (USHORT link : pad.links) {
            if (!frame) break; frame--;   // slots past the frame's contacts hold stale data
            ULONG id = 0, x = 0, y = 0; USAGE us[16]; ULONG nu = 16; bool tip = false;
            HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, 0x51 /*contact id*/, &id, pp, rep, len);
            if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, link, HID_USAGE_GENERIC_X, &x, pp, rep, len) != HIDP_STATUS_SUCCESS) continue;
            if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, link, HID_USAGE_GENERIC_Y, &y, pp, rep, len) != HIDP_STATUS_SUCCESS) continue;
            if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, us, &nu, pp, rep, len) == HIDP_STATUS_SUCCESS)
                for (ULONG i = 0; i < nu; i++) if (us[i] == 0x42 /*tip switch*/) tip = true;
            if (pad.screen) {
                ULONG cw = 0, ch = 0; HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, 0x48 /*width*/, &cw, pp, rep, len); HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, 0x49 /*height*/, &ch, pp, rep, len);
                float fx = x / pad.xmax, fy = y / pad.ymax; std::lock_guard<std::mutex> lk(g_tdMu); g_sxmax = pad.xmax;
                Down* d = nullptr; for (auto it = g_tds.rbegin(); it != g_tds.rend(); ++it) if (it->id == id && !it->up) { d = &*it; break; }
                if (tip && !d) { g_tds.push_back({id, GetTickCount64(), fx, fy, fx, fy, cw, ch, false}); if (g_tds.size() > 12) g_tds.pop_front(); }
                else if (d) { d->lx = fx; d->ly = fy; d->w = (std::max)(d->w, cw); d->h = (std::max)(d->h, ch); if (!tip) d->up = true; }
                continue;
            }
            if (tip) g_down[id] = {x / pad.xmax, y / pad.ymax}; else g_down.erase(id);
        }
        if (pad.screen) continue;
        float ax = 0, ay = 0; for (auto& [id, p] : g_down) { ax += p.first; ay += p.second; }
        int n = (int)g_down.size(); if (n) { ax /= n; ay /= n; }
        if (g_swipe.Feed(n, ax, ay) == 1) UiOnce(450, [] {   // give Windows' own gesture its chance first
            int open = 0; if (auto snap = WinSnapshot()) for (auto& w : snap->list) if (IsWindow(w.h) && IsWindowVisible(w.h) && !IsIconic(w.h)) open++;
            Log(L"touchpad: three-finger swipe down, %d window(s) still open%s", open, open ? L", minimising" : L"");
            if (open) ToggleDesktop();
        });
    }
}
LRESULT CALLBACK GestureProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INPUT) { OnInput((HRAWINPUT)l); }   // fall through: DefWindowProc frees the input
    return DefWindowProcW(h, m, w, l);
}
}

ws TouchDownsText(uint64_t withinMs) {   // the fingers that came down on the touch screen in the last withinMs, in screen pixels of the primary (built-in) screen
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi); const RECT& m = mi.rcMonitor;
    auto X = [&](float f) { return (int)(m.left + f * (m.right - m.left)); }; auto Y = [&](float f) { return (int)(m.top + f * (m.bottom - m.top)); };
    std::lock_guard<std::mutex> lk(g_tdMu); uint64_t now = GetTickCount64(); ws out;
    for (auto& d : g_tds) if (now - d.at <= withinMs)
        out += Fmt(L" [down %llu ms ago at %d,%d, %s %d,%d, size %lux%lu of %d]", now - d.at, X(d.x), Y(d.y), d.up ? L"lifted at" : L"now at", X(d.lx), Y(d.ly), d.w, d.h, (int)g_sxmax);
    return out.empty() ? L" none seen" : out;
}

bool LastTouchDown(POINT& p) {   // where the newest finger came down, in screen pixels of the primary screen
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi); const RECT& m = mi.rcMonitor;
    std::lock_guard<std::mutex> lk(g_tdMu); if (g_tds.empty()) return false;
    p = {(LONG)(m.left + g_tds.back().x * (m.right - m.left)), (LONG)(m.top + g_tds.back().y * (m.bottom - m.top))}; return true;
}

void GesturesStart() {
    WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = GestureProc; wc.hInstance = g_inst; wc.lpszClassName = L"DB3.Gestures"; RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    RAWINPUTDEVICE d[2]{{HID_USAGE_PAGE_DIGITIZER, HID_USAGE_DIGITIZER_TOUCH_PAD, RIDEV_INPUTSINK, w}, {HID_USAGE_PAGE_DIGITIZER, HID_USAGE_DIGITIZER_TOUCH_SCREEN, RIDEV_INPUTSINK, w}};
    Log(L"touchpad gestures: %s", w && RegisterRawInputDevices(d, 1, sizeof d[0]) ? L"listening" : L"not available");
    if (w && !RegisterRawInputDevices(d + 1, 1, sizeof d[0])) Log(L"touch screen watch for the resize log: not available (%lu)", GetLastError());
}
