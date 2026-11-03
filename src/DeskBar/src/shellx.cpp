#include "pch.h"
#include "app.h"

bool g_dev = true; HINSTANCE g_inst;

// ---------- monitors ----------
std::vector<Mon> Monitors() {
    std::vector<Mon> v;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL {
        MONITORINFOEXW mi{}; mi.cbSize = sizeof mi; GetMonitorInfoW(h, &mi);
        UINT dx = 96, dy = 96; GetDpiForMonitor(h, MDT_EFFECTIVE_DPI, &dx, &dy);
        ((std::vector<Mon>*)p)->push_back({h, mi.rcMonitor, mi.rcWork, dx / 96.f, mi.szDevice, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
        return TRUE;
    }, (LPARAM)&v);
    std::sort(v.begin(), v.end(), [](const Mon& a, const Mon& b) { return a.bounds.left != b.bounds.left ? a.bounds.left < b.bounds.left : a.bounds.top < b.bounds.top; });
    return v;
}
ws MonSignature() {
    ws s; for (auto& m : Monitors()) s += Fmt(L"%s:%d,%d,%d,%d@%.2f;", m.device.c_str(), m.bounds.left, m.bounds.top, m.bounds.right, m.bounds.bottom, m.scale);
    return s;
}
ws ClassOf(HWND h) { wchar_t b[128]; int n = GetClassNameW(h, b, 128); return ws(b, n); }
ws ExePathOfPid(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid); if (!p) return {};
    wchar_t b[1024]; DWORD n = 1024; ws r; if (QueryFullProcessImageNameW(p, 0, b, &n)) r.assign(b, n);
    CloseHandle(p); return r;
}

// ---------- Windows' own taskbar ----------
static std::vector<HWND> ExplorerTrays() {
    std::vector<HWND> v; DWORD me = GetCurrentProcessId();   // FindWindowEx by class: cheap enough to run every tick (a full EnumWindows was not)
    for (const wchar_t* cls : {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        for (HWND h = nullptr; (h = FindWindowExW(nullptr, h, cls, nullptr)) != nullptr;) { DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid != me) v.push_back(h); }
    return v;
}
HWND MainTray() { for (HWND h : ExplorerTrays()) if (ClassOf(h) == L"Shell_TrayWnd") return h; return nullptr; }
static void SetAutoHide(bool on) {
    APPBARDATA abd{sizeof abd}; abd.hWnd = MainTray(); abd.lParam = on ? ABS_AUTOHIDE : ABS_ALWAYSONTOP;
    SHAppBarMessage(ABM_SETSTATE, &abd);
}
void Explorer::Hide() { SetAutoHide(true); KeepHidden(); }   // auto-hide is remembered by Windows: next sign-in its taskbar starts out of the way
void Explorer::KeepHidden() {
    static std::atomic<uint64_t> lastLog{0};   // the stand-in bar's thread calls this too
    for (HWND h : ExplorerTrays()) if (IsWindowVisible(h)) { ShowWindowAsync(h, SW_HIDE); uint64_t n = Now(); if (n - lastLog > 5000) { lastLog = n; Log(L"Windows taskbar popped up, hidden again"); } }
}
void Explorer::Restore() {   // always back to a visible, docked taskbar (a stale "auto-hide" must never survive a quit)
    SetAutoHide(false);
    for (HWND h : ExplorerTrays()) ShowWindowAsync(h, SW_SHOWNOACTIVATE);
    SendNotifyMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);   // apps re-register their tray icons with Windows' own tray
    Log(L"Windows taskbar restored");
}
void Dock::Register(HWND h, const RECT& mon, int hpx) {
    APPBARDATA abd{sizeof abd}; abd.hWnd = h; abd.uCallbackMessage = WM_APP + 0x55; UINT_PTR ok = SHAppBarMessage(ABM_NEW, &abd); Log(L"appbar register: %s", ok ? L"ok" : L"REFUSED"); Reserve(h, mon, hpx);
}
void Dock::Reserve(HWND h, const RECT& mon, int hpx) {
    APPBARDATA abd{sizeof abd}; abd.hWnd = h; abd.uEdge = ABE_BOTTOM; abd.rc = {mon.left, mon.bottom - hpx, mon.right, mon.bottom};
    SHAppBarMessage(ABM_QUERYPOS, &abd);
    abd.rc = {mon.left, mon.bottom - hpx, mon.right, mon.bottom};   // QUERYPOS moves us above a dead bar's stale reservation (after a kill): always take the very bottom
    SHAppBarMessage(ABM_SETPOS, &abd);
}
void Dock::Remove(HWND h) { APPBARDATA abd{sizeof abd}; abd.hWnd = h; SHAppBarMessage(ABM_REMOVE, &abd); }

// A window covering a whole screen (game, video) hides that screen's bar. Scans from the top of the z-order down to the desktop;
// small windows floating above a full-screen video don't matter.
bool IsFullscreenOn(const RECT& mb, ws* who, bool barHidden) {
    struct Ctx { RECT mb; bool full; DWORD me; ws* who; bool barHidden; } c{mb, false, GetCurrentProcessId(), who, barHidden};
    EnumWindows([](HWND h, LPARAM p) -> BOOL {
        auto& c = *(Ctx*)p;
        if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
        DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == c.me) return TRUE;
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) return TRUE;   // click-through overlays are not a game or a video
        int cloaked = 0; if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, 4)) && cloaked) return TRUE;
        RECT r; if (!GetWindowRect(h, &r)) return TRUE;
        if (r.right <= c.mb.left || r.left >= c.mb.right || r.bottom <= c.mb.top || r.top >= c.mb.bottom) return TRUE;
        ws cls = ClassOf(h);
        if (cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd") return FALSE;
        if (!c.barHidden && IsZoomed(h) && (GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) {   // only when deciding to hide, never to un-hide: with the bar hidden the work area is empty too, and a real full-screen Chrome would make the bar flash on and off, resizing the window each time. An ordinary maximized window: it only fills the screen while Windows' work area has lost our bar (after a rotation or a screen change)
            MONITORINFO mi{sizeof mi};   // ...and then hiding the bar would block the fix, which skips hidden bars: so leave it, the work-area upkeep repairs it within 3 s
            if (GetMonitorInfoW(MonitorFromRect(&c.mb, MONITOR_DEFAULTTONEAREST), &mi) && EqualRect(&mi.rcWork, &mi.rcMonitor)) return TRUE;
        }
        if (r.left <= c.mb.left && r.top <= c.mb.top && r.right >= c.mb.right && r.bottom >= c.mb.bottom
            &&r.left >= c.mb.left - 16 && r.top >= c.mb.top - 16 && r.right <= c.mb.right + 16 && r.bottom <= c.mb.bottom + 16) {   // must fit THIS screen: a window spanning several screens is not fullscreen on any
            c.full = true; if (c.who) { wchar_t t[80]{}; GetWindowTextW(h, t, 80); *c.who = cls + L" \"" + t + L"\" " + Lower(FileNameOf(ExePathOfPid(pid))); } return FALSE;
        }
        {   // a normal window that fills most of the screen in front of a full-screen one (Claude over a borderless game): what is behind it is not what you are looking at
            long long w = (long long)std::min(r.right, c.mb.right) - std::max(r.left, c.mb.left), hh = (long long)std::min(r.bottom, c.mb.bottom) - std::max(r.top, c.mb.top);
            if (w > 0 && hh > 0 && w * hh * 10 >= (long long)(c.mb.right - c.mb.left) * (c.mb.bottom - c.mb.top) * 6) return FALSE;
        }
        return TRUE;
    }, (LPARAM)&c);
    return c.full;
}

// ---------- launching ----------
void Launch(const ws& target, const ws& args, bool admin, const ws& dir) {
    std::thread([=] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW e{sizeof e}; e.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC; e.lpFile = target.c_str();
        e.lpParameters = args.empty() ? nullptr : args.c_str(); e.lpDirectory = dir.empty() ? nullptr : dir.c_str();
        if (admin) e.lpVerb = L"runas"; e.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&e)) Log(L"launch failed (%lu): %s", GetLastError(), target.c_str());
        CoUninitialize();
    }).detach();
}
void OpenProps(const ws& path) {
    std::thread([path] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        SHELLEXECUTEINFOW e{sizeof e}; e.fMask = SEE_MASK_INVOKEIDLIST; e.lpVerb = L"properties"; e.lpFile = path.c_str(); e.nShow = SW_SHOW;
        ShellExecuteExW(&e); Sleep(60000); CoUninitialize();   // the property sheet dies with the thread
    }).detach();
}
// Show desktop, done by us: Explorer's own taskbar (hidden while DeskBar runs) ignores its show-desktop commands on this Windows build (tested:
// WM_COMMAND 407/419/416 do nothing), Shell.Application.ToggleDesktop() finds OUR stand-in Shell_TrayWnd, and a faked Win+D can be eaten by our
// Windows-key hook. So: minimise every taskbar window and remember them; the next press, while nothing is open, brings exactly those back in order.
static std::vector<HWND> g_putAway;   // top of the z-order first
void ToggleDesktop() {
    std::vector<HWND> open;
    if (auto snap = WinSnapshot()) for (auto& w : snap->list) if (IsWindow(w.h) && IsWindowVisible(w.h) && !IsIconic(w.h)) open.push_back(w.h);
    if (open.empty()) {
        for (auto it = g_putAway.rbegin(); it != g_putAway.rend(); ++it) if (IsWindow(*it) && IsIconic(*it)) ShowWindow(*it, SW_RESTORE);   // bottom first, so the top one ends up on top
        if (!g_putAway.empty() && IsWindow(g_putAway.front())) ForceForeground(g_putAway.front());
        g_putAway.clear(); return;
    }
    g_putAway = open;
    for (HWND h : open) ShowWindowAsync(h, SW_MINIMIZE);
}
void SendKeys(std::initializer_list<WORD> vks) {
    std::vector<INPUT> in; for (WORD v : vks) { INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = v; in.push_back(i); }
    for (auto it = vks.end(); it != vks.begin();) { --it; INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = *it; i.ki.dwFlags = KEYEVENTF_KEYUP; in.push_back(i); }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}
void ForceForeground(HWND h) {
    if (IsIconic(h)) { ShowWindow(h, SW_RESTORE); if (IsIconic(h)) { SwitchToThisWindow(h, TRUE); return; } }   // admin apps and a waiting admin prompt refuse our restore (Windows guards higher-rights windows); Alt+Tab's call gets through
    if (SetForegroundWindow(h)) return;
    keybd_event(VK_F24, 0, 0, 0); keybd_event(VK_F24, 0, KEYEVENTF_KEYUP, 0);   // grants the right when Windows refuses
    SetForegroundWindow(h);
}
