#include "pch.h"
#include "bar.h"
#include "flyout.h"

// The notification area. Apps announce their icons by sending a message to the window called "Shell_TrayWnd"; we create our own window
// with that name (above Windows' hidden one), ask every app to re-announce itself ("TaskbarCreated"), draw their icons on our bars and
// forward clicks. Known limit: taskbar progress bars and overlay badges can't be shown (apps send them to Windows' internal taskbar).
namespace {
struct TrayIcon {
    HWND hwnd = nullptr; UINT id = 0; GUID guid{}; UINT callback = 0; HICON hicon = nullptr; ws tip, exe; bool hidden = false; UINT version = 0; ImgP img;
    ws Key() const { if (guid != GUID{}) { wchar_t b[64]; StringFromGUID2(guid, b, 64); return b; } return Fmt(L"%p:%u", hwnd, id); }
};
std::vector<std::shared_ptr<TrayIcon>> g_icons; HWND g_wnd = nullptr; UINT g_wmTaskbarCreated, g_wmShellHook; bool g_changePending = false;
struct CopyData { ULONG_PTR dwData; DWORD cbData; void* lpData; };

HICON IconOf(DWORD v) { return (HICON)(INT_PTR)(int)v; }   // HICON/HWND are 32-bit significant, sign-extended
bool IsPromoted(const TrayIcon& i) { for (auto& p : S.promotedTrayIcons) if (EqI(p, i.exe)) return true; return false; }

void QueueChanged() {   // icons change in bursts: never rebuild inside the message another program is waiting on
    if (g_changePending) return; g_changePending = true;
    UiOnce(60, [] { g_changePending = false; TrayRefresh(); });
}
bool HandleNotify(const BYTE* p, DWORD size) {
    if (size < 8 + 24) return false;
    auto U = [&](int off) -> DWORD { return off + 4 <= (int)size ? *(const DWORD*)(p + off) : 0; };
    const int N = 8; DWORD op = U(4); HWND hwnd = (HWND)(INT_PTR)(int)U(N + 4); UINT id = U(N + 8), flags = U(N + 12);
    GUID guid{}; if ((flags & NIF_GUID) && N + 936 + 16 <= (int)size) memcpy(&guid, p + N + 936, 16);
    TrayIcon probe; probe.hwnd = hwnd; probe.id = id; probe.guid = guid; ws key = probe.Key();
    auto it = std::find_if(g_icons.begin(), g_icons.end(), [&](auto& i) { return i->Key() == key; });
    switch (op) {
    case NIM_ADD: case NIM_MODIFY: {
        bool isNew = it == g_icons.end(); std::shared_ptr<TrayIcon> ic = isNew ? std::make_shared<TrayIcon>() : *it;
        if (isNew) { ic->hwnd = hwnd; ic->id = id; ic->guid = guid; DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid); ic->exe = ExePathOfPid(pid); }
        if (flags & NIF_MESSAGE) ic->callback = U(N + 16);
        if (flags & NIF_ICON) { HICON h = IconOf(U(N + 20)); if (h != ic->hicon || isNew) { ic->hicon = h; ic->img = ImgFromHIcon(h, 32); } }
        if ((flags & NIF_TIP) && N + 24 + 256 <= (int)size) { ic->tip.assign((const wchar_t*)(p + N + 24), wcsnlen((const wchar_t*)(p + N + 24), 128)); }
        if (flags & NIF_STATE) { DWORD st = U(N + 280), mask = U(N + 284); if (mask & NIS_HIDDEN) ic->hidden = (st & NIS_HIDDEN) != 0; }
        if (ic->tip.size() > 127) ic->tip.resize(127);
        if (isNew) { g_icons.push_back(ic); Log(L"tray +%s \"%s\" promoted=%d", FileNameOf(ic->exe).c_str(), ic->tip.c_str(), (int)IsPromoted(*ic)); }
        break; }
    case NIM_DELETE: if (it == g_icons.end()) return false; g_icons.erase(it); break;
    case NIM_SETVERSION: if (it == g_icons.end()) return false; (*it)->version = U(N + 800); break;
    default: return true;
    }
    QueueChanged(); return true;
}
LRESULT CALLBACK TrayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_wmShellHook && m) {
        if ((UINT)w == 0x8006 /*HSHELL_FLASH*/) SetFlashing((HWND)l, true); else if ((UINT)w == 6 /*HSHELL_REDRAW*/) SetFlashing((HWND)l, false);
        return 0;
    }
    if (m == WM_COMMAND && LOWORD(w) == 407) { ToggleDesktop(); return 0; }   // other apps' "show desktop" (Shell.Application.ToggleDesktop) reaches us, not Explorer
    if (m == WM_COPYDATA) {
        auto* cds = (COPYDATASTRUCT*)l;
        if (cds->dwData == 1) return HandleNotify((const BYTE*)cds->lpData, cds->cbData);
        if (cds->dwData == 0) {   // appbar messages (docking apps, our own bars): Windows' own tray does the work; give up after 400 ms if it is busy
            HWND ex = MainTray(); DWORD_PTR res = 0; if (!ex) return 0;
            return SendMessageTimeoutW(ex, WM_COPYDATA, w, l, SMTO_ABORTIFHUNG, 400, &res) ? (LRESULT)res : 0;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
HWND MakeChild(const wchar_t* cls, HWND parent, const wchar_t* text) {
    WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = g_inst; wc.lpszClassName = cls; RegisterClassExW(&wc);
    return CreateWindowExW(0, cls, text, WS_CHILD, 0, 0, 1, 1, parent, nullptr, g_inst, nullptr);
}
void Mouse(TrayIcon& ic, bool left, bool down, bool dbl = false) {
    if (!ic.callback || !IsWindow(ic.hwnd)) return;
    POINT pt; GetCursorPos(&pt); DWORD pid = 0; GetWindowThreadProcessId(ic.hwnd, &pid); AllowSetForegroundWindow(pid);
    UINT msg = left ? (dbl ? WM_LBUTTONDBLCLK : down ? WM_LBUTTONDOWN : WM_LBUTTONUP) : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
    auto post = [&](UINT m) {
        if (ic.version >= 4) PostMessageW(ic.hwnd, ic.callback, (WPARAM)(((pt.y & 0xFFFF) << 16) | (pt.x & 0xFFFF)), (LPARAM)((ic.id << 16) | (m & 0xFFFF)));
        else PostMessageW(ic.hwnd, ic.callback, ic.id, m);
    };
    post(msg); if (!down && ic.version >= 4) post(left ? NIN_SELECT : WM_CONTEXTMENU);
}
void ImportPromoted() {
    if (S.trayImported) return;
    HKEY root; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\NotifyIconSettings", 0, KEY_READ, &root) == 0) {
        wchar_t name[128]; for (DWORD i = 0;; i++) {
            DWORD nl = 128; if (RegEnumKeyExW(root, i, name, &nl, nullptr, nullptr, nullptr, nullptr)) break;
            HKEY k; if (RegOpenKeyExW(root, name, 0, KEY_READ, &k)) continue;
            DWORD pr = 0, sz = 4; wchar_t path[1024]; DWORD ps = sizeof path;
            if (RegGetValueW(k, nullptr, L"IsPromoted", RRF_RT_DWORD, nullptr, &pr, &sz) == 0 && pr == 1 && RegGetValueW(k, nullptr, L"ExecutablePath", RRF_RT_REG_SZ, nullptr, path, &ps) == 0) {
                ws p = path;   // known-folder GUIDs at the start of the path
                static const std::pair<const wchar_t*, const KNOWNFOLDERID*> map[] = {{L"{6D809377-6AF0-444B-8957-A3773F02200E}", &FOLDERID_ProgramFilesX64}, {L"{7C5A40EF-A0FB-4BFC-874A-C0F2E0B9FA8E}", &FOLDERID_ProgramFilesX86},
                    {L"{F38BF404-1D43-42F2-9305-67DE0B28FC23}", &FOLDERID_Windows}, {L"{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}", &FOLDERID_System}, {L"{F1B32785-6FBA-4FCF-9D55-7B8E7F157091}", &FOLDERID_LocalAppData}, {L"{3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}", &FOLDERID_RoamingAppData}};
                for (auto& m : map) if (StartsI(p, m.first)) { p = KnownFolder(*m.second) + p.substr(wcslen(m.first)); break; }
                S.promotedTrayIcons.push_back(p);
            }
            RegCloseKey(k);
        }
        RegCloseKey(root);
    }
    S.trayImported = true; SaveSettingsSoon();
}

// ---------------- overflow flyout ----------------
struct OverflowWnd : Wnd {
    UINT_PTR poll = 0; bool downPrev = false; HWND fg0 = nullptr; RECT anchor{};
    OverflowWnd() { noActivate = true; }
};
std::unique_ptr<OverflowWnd> g_ov; uint64_t g_ovGrace = 0;   // after a right-click the app takes the foreground a moment before its menu appears (and some menus aren't "#32768" at all): no dismissing until the next click or 15 s
void OverflowClose() { if (!g_ov || !g_ov->hwnd) return; if (g_ov->poll) { UiKillTimer(g_ov->poll); g_ov->poll = 0; } g_ov->CancelInput(); TipHide(); ShowWindow(g_ov->hwnd, SW_HIDE); }
View* TrayCell(View* parent, const std::shared_ptr<TrayIcon>& ic, float w) {
    auto* c = MkBox(parent); c->fw = w; c->fh = 40; c->radius = 4; c->hit = true; c->bgHover = SolidB(K::White, 0x22); if (!EqI(FileNameOf(ic->exe), L"DeskMode.exe")) c->tip = ic->tip; c->va = Al::Center; c->ha = Al::Start;   // DeskMode's status line is too long to be useful as a tooltip
    auto* p = c->Add(new Pic()); p->fw = p->fh = 16; p->ha = p->va = Al::Center; p->img = ic->img;
    if (!ic->img) { auto* g = MkGlyph(c, 0xE713, 14); g->ha = g->va = Al::Center; }
    std::weak_ptr<TrayIcon> wk = ic; auto last = std::make_shared<uint64_t>(0);
    c->onClick = [wk, last] {
        auto i = wk.lock(); if (!i) return; uint64_t t = Now(); bool dbl = *last && t - *last < GetDoubleClickTime(); *last = dbl ? 0 : t;
        Mouse(*i, true, true); Mouse(*i, true, false); if (dbl) { Mouse(*i, true, true, true); Mouse(*i, true, false); } OverflowClose();
    };
    auto rc = [wk] { auto i = wk.lock(); if (!i) return; g_ovGrace = Now() + 15000; Mouse(*i, false, true); Mouse(*i, false, false); };   // the popup stays: it is the app's menu you are after, and the popup closes itself once that menu is gone
    c->onRight = rc; c->onLong = rc; return c;
}
}

void TrayStart() {
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated"); g_wmShellHook = RegisterWindowMessageW(L"SHELLHOOK"); ImportPromoted();
    WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = TrayProc; wc.hInstance = g_inst; wc.lpszClassName = L"Shell_TrayWnd"; RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT, L"Shell_TrayWnd", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_inst, nullptr);
    if (!g_wnd) { Log(L"Tray: cannot create Shell_TrayWnd"); return; }
    SetLayeredWindowAttributes(g_wnd, 0, 0, LWA_ALPHA);
    HWND notify = MakeChild(L"TrayNotifyWnd", g_wnd, L""), pager = MakeChild(L"SysPager", notify, L""); MakeChild(L"ToolbarWindow32", pager, L"Notification Area");   // children some apps look for
    ShowWindow(g_wnd, SW_SHOWNOACTIVATE); RegisterShellHookWindow(g_wnd); TrayKeepOnTop();
    SendNotifyMessageW(HWND_BROADCAST, g_wmTaskbarCreated, 0, 0);    // ask every running app to register its icon with us
    UiTimer(3000, [] { if (std::erase_if(g_icons, [](auto& i) { return !IsWindow(i->hwnd); })) TrayRefresh(); TrayKeepOnTop(); });   // rebuilding every 3 s flickered the tray and closed the overflow popup
    Log(L"Tray: taking over Shell_TrayWnd");
}
void TrayStop() {
    if (!g_wnd) return; DestroyWindow(g_wnd); g_wnd = nullptr;
    SendNotifyMessageW(HWND_BROADCAST, g_wmTaskbarCreated, 0, 0);    // Windows' own tray is the first Shell_TrayWnd again: apps re-register
}
void TrayKeepOnTop() { if (g_wnd && FindWindowW(L"Shell_TrayWnd", nullptr) != g_wnd) SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
void TrayPlace(const RECT& r) { if (g_wnd) SetWindowPos(g_wnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE); }
void TrayRefresh() {
    for (auto* bar : AllBars()) {
        bar->trayBox->Clear(); bool any = false;
        for (auto& ic : g_icons) { if (ic->hidden) continue; if (IsPromoted(*ic)) TrayCell(bar->trayBox, ic, 32); else any = true; }
        bar->overflowBtn->Show(any); bar->Relayout();
        bar->overflowBtn->onClick = [bar] { TrayToggleOverflow(bar); };
    }   // shortcut: an open overflow popup keeps its icon list until reopened (any tray update used to close it within a second), upgrade by rebuilding it here
}
void TrayToggleOverflow(Bar* bar) {
    if (g_ov && g_ov->hwnd && IsWindowVisible(g_ov->hwnd)) { OverflowClose(); return; }
    std::vector<std::shared_ptr<TrayIcon>> list; for (auto& ic : g_icons) if (!ic->hidden && !IsPromoted(*ic)) list.push_back(ic);
    if (list.empty()) return;
    RECT a = bar->ScreenRect(bar->overflowBtn); POINT mid{(a.left + a.right) / 2, (a.top + a.bottom) / 2};
    if (!g_ov) { g_ov = std::make_unique<OverflowWnd>(); g_ov->Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_POPUP, mid.x, mid.y, 10, 10); StyleFlyout(g_ov->hwnd, true); }
    OverflowWnd& o = *g_ov; SetWindowPos(o.hwnd, nullptr, mid.x, mid.y, 10, 10, SWP_NOACTIVATE | SWP_NOZORDER); o.UpdateScale(); float sc = o.scale;
    View* r = o.root; r->Clear(); r->lay = Lay::V; r->bg = PanelBrush(); r->borderW = 1; r->border = Col(K::White, K::CardLine); r->radius = 10; r->pad = Thick(8);
    auto* grid = MkBox(r, Lay::Uniform); grid->cols = std::min<int>(4, (int)list.size()); grid->gap = 2; grid->cellH = 40;
    for (auto& ic : list) TrayCell(grid, ic, 40)->ha = Al::Center;
    float w = grid->cols * 40.f + (grid->cols - 1) * 2 + 16; Sz d = r->Measure({w, INF}); int pw = (int)ceilf(d.w * sc), ph = (int)ceilf(d.h * sc);
    HMONITOR mon = MonitorFromPoint(mid, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
    int x = std::clamp((int)a.right - pw, (int)mi.rcMonitor.left + 4, (int)mi.rcMonitor.right - pw - 4), y = g_dev ? a.bottom + (int)(8 * sc) : a.top - ph - (int)(8 * sc);
    SetWindowPos(o.hwnd, HWND_TOPMOST, x, y, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW); o.Relayout(); o.fg0 = GetForegroundWindow(); o.anchor = a;
    o.downPrev = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
    o.poll = UiTimer(40, [] {
        if (!g_ov) return; OverflowWnd& w = *g_ov; POINT p; GetCursorPos(&p); RECT r2; GetWindowRect(w.hwnd, &r2); RECT ar = w.anchor;
        bool down = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
        HWND appMenu = FindWindowW(L"#32768", nullptr); if (appMenu && IsWindowVisible(appMenu)) { w.downPrev = down; return; }   // an icon's own context menu is open: leave everything alone until it is gone
        bool grace = Now() < g_ovGrace;
        if (down && !w.downPrev && !PtInRect(&r2, p) && !PtInRect(&ar, p) && !MenuContains(p)) { if (grace) { g_ovGrace = 0; w.downPrev = down; return; } OverflowClose(); return; }   // grace: that click is on the app's menu
        w.downPrev = down; if (!grace && GetForegroundWindow() != w.fg0 && ClassOf(GetForegroundWindow()) != kWndClass) OverflowClose();   // switching app closes it
    });
}
