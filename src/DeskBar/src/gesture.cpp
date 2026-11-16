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
struct Pad { std::vector<BYTE> pp; std::vector<USHORT> links; float xmax = 1, ymax = 1; };   // links: one per contact slot in a report
std::map<HANDLE, Pad> g_pads; std::map<ULONG, std::pair<float, float>> g_down; ULONG g_frame = 0; SwipeTracker g_swipe;

Pad& PadOf(HANDLE dev) {
    auto it = g_pads.find(dev); if (it != g_pads.end()) return it->second;
    Pad& p = g_pads[dev]; UINT sz = 0;
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
        ULONG count = 0; if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, 0, 0x54 /*contact count*/, &count, pp, rep, len) == HIDP_STATUS_SUCCESS && count) g_frame = count;
        for (USHORT link : pad.links) {
            if (!g_frame) break; g_frame--;   // slots past the frame's contacts hold stale data
            ULONG id = 0, x = 0, y = 0; USAGE us[16]; ULONG nu = 16; bool tip = false;
            HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, 0x51 /*contact id*/, &id, pp, rep, len);
            if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, link, HID_USAGE_GENERIC_X, &x, pp, rep, len) != HIDP_STATUS_SUCCESS) continue;
            if (HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, link, HID_USAGE_GENERIC_Y, &y, pp, rep, len) != HIDP_STATUS_SUCCESS) continue;
            if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_DIGITIZER, link, us, &nu, pp, rep, len) == HIDP_STATUS_SUCCESS)
                for (ULONG i = 0; i < nu; i++) if (us[i] == 0x42 /*tip switch*/) tip = true;
            if (tip) g_down[id] = {x / pad.xmax, y / pad.ymax}; else g_down.erase(id);
        }
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

void GesturesStart() {
    WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = GestureProc; wc.hInstance = g_inst; wc.lpszClassName = L"DB3.Gestures"; RegisterClassExW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    RAWINPUTDEVICE d{HID_USAGE_PAGE_DIGITIZER, HID_USAGE_DIGITIZER_TOUCH_PAD, RIDEV_INPUTSINK, w};
    Log(L"touchpad gestures: %s", w && RegisterRawInputDevices(&d, 1, sizeof d) ? L"listening" : L"not available");
}
