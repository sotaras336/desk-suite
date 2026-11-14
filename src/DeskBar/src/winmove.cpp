#include "pch.h"
#include "bar.h"

// Window helpers: snap a dragged window onto one screen, send to the other screen, and rescue windows when a screen is unplugged.
namespace {
RECT VisibleRect(HWND h) { RECT r{}; if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(h, &r); return r; }
int W(const RECT& r) { return r.right - r.left; } int H(const RECT& r) { return r.bottom - r.top; }
bool IsAppWindow(HWND h) {   // normal app windows only (not tool windows, popups, our own UI, the desktop...)
    if (!IsWindowVisible(h)) return false; int cl = 0; if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cl, 4)) && cl) return false;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE), ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((st & WS_CHILD) || (ex & WS_EX_TOOLWINDOW)) return false; if ((st & WS_CAPTION) != WS_CAPTION && !(ex & WS_EX_APPWINDOW)) return false;
    DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId()) return false;
    ws c = ClassOf(h); return !(c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Shell_SecondaryTrayWnd");
}
long long Overlap(const RECT& a, const RECT& b) { long long w = std::min(a.right, b.right) - std::max(a.left, b.left), h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top); return w > 0 && h > 0 ? w * h : 0; }
void SetVisibleRect(HWND h, const RECT& v) {   // moves a window so its VISIBLE frame lands on v (accounts for invisible resize borders)
    RECT o; GetWindowRect(h, &o); RECT vis = VisibleRect(h); int l = vis.left - o.left, t = vis.top - o.top, r = o.right - vis.right, b = o.bottom - vis.bottom;
    SetWindowPos(h, nullptr, v.left - l, v.top - t, W(v) + l + r, H(v) + t + b, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}
RECT FitInside(const RECT& r, const RECT& work) {   // keeps r's size (shrinking only if it doesn't fit) and pushes it fully inside work
    int w = std::min(W(r), W(work)), h = std::min(H(r), H(work)); int x = std::clamp((int)r.left, (int)work.left, (int)work.right - w), y = std::clamp((int)r.top, (int)work.top, (int)work.bottom - h); return {x, y, x + w, y + h};
}
RECT Proportional(const RECT& r, const RECT& fw, const RECT& tw) {   // same place and size relative to the screen on another screen
    double fx = (r.left - fw.left) / (double)W(fw), fy = (r.top - fw.top) / (double)H(fw), fW = W(r) / (double)W(fw), fH = H(r) / (double)H(fw);
    int w = (int)lround(std::min(1.0, fW) * W(tw)), h = (int)lround(std::min(1.0, fH) * H(tw)), x = tw.left + (int)lround(fx * W(tw)), y = tw.top + (int)lround(fy * H(tw)); return FitInside({x, y, x + w, y + h}, tw);
}

// ---- snapper: when you let go of a dragged window ----
//  - hanging across two screens: it moves fully onto the screen with most of it; moved to a different screen: it keeps the same size
//  - RELATIVE to that screen. Hold Alt while releasing to leave it exactly where it is. Snapped / maximised windows are left alone.
struct Start { RECT rect; HMONITOR mon; RECT work; }; std::map<HWND, Start> g_starts; std::map<HWND, WINDOWPLACEMENT> g_keepMax;
void Animate(HWND h, const RECT& from, const RECT& to) {   // a short ease so the window visibly slides into place
    const int steps = 7;
    for (int i = 1; i <= steps; i++) {
        double t = 1 - pow(1 - i / (double)steps, 3); auto L = [&](int a, int b) { return a + (int)lround((b - a) * t); };
        RECT s{L(from.left, to.left), L(from.top, to.top), L(from.right, to.right), L(from.bottom, to.bottom)};
        if (i < steps) { RECT o; GetWindowRect(h, &o); RECT v = VisibleRect(h); SetWindowPos(h, nullptr, s.left + (o.left - v.left), s.top + (o.top - v.top), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }   // only move; resize once at the end
        else SetVisibleRect(h, to);
        Sleep(12);
    }
}
void CALLBACK OnMoveSize(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || !h) return;
    if (ev == EVENT_SYSTEM_MOVESIZESTART) {
        // A finger on the bar's top edge gets re-targeted by Windows to the window above (its bottom resize edge). Cancel that resize (Esc puts the size back) and replay the tap onto the bar.
        POINT p; HMONITOR pm; MONITORINFO pi{sizeof pi};
        { RECT wr = VisibleRect(h); GetCursorPos(&p); Log(L"movesize start: %s win=%d,%d,%d,%d cursor=%d,%d", ClassOf(h).c_str(), (int)wr.left, (int)wr.top, (int)wr.right, (int)wr.bottom, (int)p.x, (int)p.y); }
        if (GetCursorPos(&p) &&(pm = MonitorFromPoint(p, MONITOR_DEFAULTTONULL)) && GetMonitorInfoW(pm, &pi) && p.y >= pi.rcWork.bottom && p.y < pi.rcMonitor.bottom) {
            HWND bar = WindowFromPoint(p); SendKeys({VK_ESCAPE}); DWORD pid = 0; if (bar) GetWindowThreadProcessId(bar, &pid);
            if (pid == GetCurrentProcessId() && ClassOf(bar) == kWndClass) { POINT c = p; ScreenToClient(bar, &c); LPARAM lp = MAKELPARAM(c.x, c.y); Sleep(50); PostMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, lp); Sleep(60); PostMessageW(bar, WM_LBUTTONUP, 0, lp); }
            return;
        }
        if (IsZoomed(h)) {   // a maximized window is not resized from its left, right or bottom edge (dragging the title bar off still works): cancel that resize
            RECT r = VisibleRect(h); UINT dpi = GetDpiForWindow(h); int band = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) + MulDiv(6, (int)dpi, 96);
            if (p.x <= r.left + band || p.x >= r.right - band || p.y >= r.bottom - band) {
                // apps that draw their own frame (Chrome, WPF) un-maximize themselves as the resize starts, so Esc alone leaves a restored window
                // that only looks maximized: keep the placement and put it back when the cancelled drag ends
                WINDOWPLACEMENT wp{sizeof wp}; GetWindowPlacement(h, &wp); g_keepMax[h] = wp;
                SendKeys({VK_ESCAPE}); Log(L"resize of maximized %s cancelled", ClassOf(h).c_str()); return; }
        }
        HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(m, &mi); g_starts[h] = {VisibleRect(h), m, mi.rcWork}; return; }
    if (ev != EVENT_SYSTEM_MOVESIZEEND) return;
    if (auto k = g_keepMax.find(h); k != g_keepMax.end()) {
        WINDOWPLACEMENT wp = k->second; g_keepMax.erase(k);
        std::thread([h, wp] { for (int i = 0; i < 4; i++) { if (!IsWindow(h)) return; if (!IsZoomed(h)) { SetWindowPlacement(h, &wp); Log(L"re-maximized after cancelled resize"); } Sleep(100); } }).detach();   // some apps restore again just after the drag ends
        return;
    }
    auto it = g_starts.find(h); if (it == g_starts.end()) return; Start st = it->second; g_starts.erase(it);
    if (!S.snapOnRelease && !S.keepSizeRatio) return; if (GetAsyncKeyState(VK_MENU) & 0x8000) return;
    if (!IsAppWindow(h) || IsZoomed(h) || IsIconic(h)) return;
    typedef BOOL(WINAPI * Arr)(HWND); static Arr arranged = (Arr)GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged"); if (arranged && arranged(h)) return;
    RECT r = VisibleRect(h);
    if (abs(H(r) - H(st.rect)) > 2) {   // a resize that ended over the bar: pull the bottom edge back up to the work area
        MONITORINFO wi{sizeof wi}; if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &wi) && wi.rcWork.bottom < wi.rcMonitor.bottom && r.bottom > wi.rcWork.bottom && r.top < wi.rcWork.bottom - 40) { RECT c = r; c.bottom = wi.rcWork.bottom; SetVisibleRect(h, c); return; }
    }
    if (abs(W(r) - W(st.rect)) > 2 || abs(H(r) - H(st.rect)) > 2) { if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) == st.mon) return; }   // a resize: leave it (unless DPI changed the size crossing screens)
    auto mons = Monitors(); if (mons.empty()) return;
    const Mon* target = &mons[0]; long long best = -1; int touching = 0; for (auto& m : mons) { long long o = Overlap(r, m.bounds); if (o > best) { best = o; target = &m; } if (o > 0) touching++; }
    bool changed = target->h != st.mon, spills = touching > 1; RECT res = r;
    if (changed && S.keepSizeRatio) {   // same size relative to the screen, centred on where you dropped it
        double fw = W(st.rect) / (double)W(st.work), fh = H(st.rect) / (double)H(st.work); int w = (int)lround(std::min(1.0, fw) * W(target->work)), hh = (int)lround(std::min(1.0, fh) * H(target->work));
        int cx = (r.left + r.right) / 2, cy = std::max((int)target->work.top + hh / 2, (int)r.top + hh / 2); res = FitInside({cx - w / 2, cy - hh / 2, cx - w / 2 + w, cy - hh / 2 + hh}, target->work);
    } else if (S.snapOnRelease && spills) res = FitInside(r, target->work); else return;
    if (EqualRect(&res, &r)) return; Animate(h, r, res);
}
// ---- popups: a dialog or popup shows up on the screen of the window that opened it (Quartus II's popups opened on the other screen than its main window) ----
void MoveOnly(HWND h, const RECT& v) { RECT o; GetWindowRect(h, &o); RECT vis = VisibleRect(h); SetWindowPos(h, nullptr, v.left + (o.left - vis.left), v.top + (o.top - vis.top), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER); }   // position only: a different DPI on the other screen resizes it the Windows way
// Which windows count as a program's popup. It used to be only "normal" windows with a caption, which left out exactly the ones that strayed: tool windows and
// frameless panels (Quartus' message and progress windows, Qt tool windows), and dialogs whose owner is a hidden helper window.
static bool PopupCandidate(HWND h) {
    if (!IsWindowVisible(h) || IsZoomed(h) || IsIconic(h)) return false;
    LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE); if (st & WS_CHILD) return false;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId()) return false;
    ws c = Lower(ClassOf(h)); for (const wchar_t* bad : {L"#32768", L"tooltip", L"popup", L"dropdown", L"combolbox", L"shadow", L"progman", L"workerw", L"shell_", L"ime", L"notify", L"toast"}) if (c.find(bad) != ws::npos) return false;   // menus, tooltips, drop-downs: they open at the pointer
    RECT r = VisibleRect(h); if (W(r) < 150 || H(r) < 100) return false;
    if (!GetWindow(h, GW_OWNER) && (st & WS_MAXIMIZEBOX)) return false;   // an unowned window that can be maximised is a main window (a browser tab dragged out): it stays where it was put
    return true;
}
bool MovePopup(HWND h) {
    if (!PopupCandidate(h)) return false;
    HWND ref = GetWindow(h, GW_OWNER);   // its owner; a hidden owner (a helper window) -> the program's visible top window; else the foreground window of the same program
    if (ref && !IsAppWindow(ref)) { HWND root = GetAncestor(h, GA_ROOTOWNER); ref = (root && root != h && IsAppWindow(root)) ? root : nullptr; }
    if (!ref) { HWND fg = GetForegroundWindow(); DWORD a = 0, b = 0; GetWindowThreadProcessId(h, &a); if (fg && fg != h) GetWindowThreadProcessId(fg, &b); if (a && a == b) ref = fg; }
    if (!ref || ref == h || !IsAppWindow(ref) || IsIconic(ref)) return false;
    HMONITOR mr = MonitorFromWindow(ref, MONITOR_DEFAULTTONEAREST); if (mr == MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST)) return false;
    MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(mr, &mi)) return false;
    RECT to = PopupSpot(VisibleRect(h), VisibleRect(ref), mi.rcWork); MoveOnly(h, to);
    Log(L"popup %s moved to the screen of its window %s", ClassOf(h).c_str(), ClassOf(ref).c_str()); return true;
}
void CALLBACK OnShow(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != 0 || !h || !S.popupsFollowOwner || (GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD)) return;
    if (MovePopup(h)) std::thread([h] { Sleep(300); if (IsWindow(h)) MovePopup(h); }).detach();   // some apps place the window again just after showing it
}
void SnapperThread() {
    HWINEVENTHOOK hk = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, nullptr, OnMoveSize, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    HWINEVENTHOOK hs = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnShow, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); } UnhookWinEvent(hk); UnhookWinEvent(hs);
}

// ---- rescue: unplugging the LG brings its windows to the Surface at the same relative size; plugging it back puts them where they were ----
struct Spot { ws screen; double fx, fy, fw, fh; bool max; };
std::map<HWND, Spot> g_spots; std::map<HWND, std::pair<Spot, RECT>> g_displaced; std::set<ws> g_screens; ws g_sig; uint64_t g_changeAt = 0; bool g_changing = false;
ws ScreenKey(const Mon& m) { return DisplayInfoFor(m.device).friendly + L"|" + Fmt(L"%dx%d", W(m.bounds), H(m.bounds)); }
std::set<ws> ScreenKeys() { std::set<ws> s; for (auto& m : Monitors()) s.insert(ScreenKey(m)); return s; }
void Snapshot() {
    if (!S.rescueWindows) return; auto mons = Monitors(); if (mons.size() < 2) return; std::set<HWND> seen;
    std::map<HMONITOR, ws> keys; for (auto& m : mons) keys[m.h] = ScreenKey(m);
    std::vector<HWND> all; EnumWindows([](HWND h, LPARAM p) -> BOOL { ((std::vector<HWND>*)p)->push_back(h); return TRUE; }, (LPARAM)&all);
    for (HWND h : all) {
        if (!IsAppWindow(h) || IsIconic(h)) continue; HMONITOR mh = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST); const Mon* m = nullptr; for (auto& x : mons) if (x.h == mh) m = &x; if (!m) continue;
        bool mx = IsZoomed(h); RECT r = VisibleRect(h), w = m->work;
        g_spots[h] = {keys[mh], (r.left - w.left) / (double)W(w), (r.top - w.top) / (double)H(w), W(r) / (double)W(w), H(r) / (double)H(w), mx}; seen.insert(h);
    }
    for (auto it = g_spots.begin(); it != g_spots.end();) it = !seen.count(it->first) && !IsWindow(it->first) ? g_spots.erase(it) : std::next(it);
}
RECT SpotRect(const Spot& s, const RECT& w) { int x = w.left + (int)(s.fx * W(w)), y = w.top + (int)(s.fy * H(w)); return FitInside({x, y, x + (int)(std::min(1.0, s.fw) * W(w)), y + (int)(std::min(1.0, s.fh) * H(w))}, w); }
void OnScreensChanged() {
    auto mons = Monitors(); std::set<ws> now = ScreenKeys(), removed, added;
    for (auto& s : g_screens) if (!now.count(s)) removed.insert(s); for (auto& s : now) if (!g_screens.count(s)) added.insert(s); g_screens = now;
    if (!S.rescueWindows) return;
    if (!removed.empty()) {
        const Mon* home = &mons[0]; for (auto& m : mons) if (DisplayInfoFor(m.device).internal) home = &m;
        for (auto& [h, spot] : std::map<HWND, Spot>(g_spots)) {
            if (!removed.count(spot.screen) || !IsWindow(h)) continue;
            if (spot.max) { g_displaced[h] = {spot, VisibleRect(h)}; continue; }
            if (IsZoomed(h)) ShowWindow(h, SW_RESTORE); SetVisibleRect(h, SpotRect(spot, home->work)); g_displaced[h] = {spot, VisibleRect(h)};
        }
        Log(L"WindowRescue: moved %d window(s)", (int)g_displaced.size());
    }
    if (!added.empty()) {
        for (auto& [h, sp] : std::map<HWND, std::pair<Spot, RECT>>(g_displaced)) {
            g_displaced.erase(h); if (!IsWindow(h) || !added.count(sp.first.screen)) continue;
            RECT cur = VisibleRect(h); if (abs(cur.left - sp.second.left) > 12 || abs(cur.top - sp.second.top) > 12) continue;   // you moved it yourself since: leave it
            const Mon* m = nullptr; for (auto& x : mons) if (ScreenKey(x) == sp.first.screen) m = &x; if (!m) continue;
            if (IsZoomed(h)) ShowWindow(h, SW_RESTORE); SetVisibleRect(h, SpotRect(sp.first, m->work)); if (sp.first.max) ShowWindow(h, SW_MAXIMIZE);
        }
    }
    Snapshot();
}
}

void SendToNextScreen(HWND h) {
    auto mons = Monitors(); if (mons.size() < 2) return; HMONITOR cur = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST); size_t i = 0; for (size_t k = 0; k < mons.size(); k++) if (mons[k].h == cur) i = k;
    const Mon &from = mons[i], &to = mons[(i + 1) % mons.size()]; bool mx = IsZoomed(h);
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE); if (mx) ShowWindow(h, SW_RESTORE);
    SetVisibleRect(h, Proportional(VisibleRect(h), from.work, to.work)); if (mx) ShowWindow(h, SW_MAXIMIZE); ForceForeground(h);
}
// A window left sticking into the bar (a touch on the bar's edge dragged the window's bottom edge down) goes back above it once the finger or mouse is up.
void ClampForeground() {
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;
    HWND h = GetForegroundWindow(); if (!h || !IsAppWindow(h) || IsZoomed(h) || IsIconic(h)) return;
    GUITHREADINFO gi{sizeof gi}; if (GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gi) && (gi.flags & GUI_INMOVESIZE)) return;   // still being dragged
    MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi) || mi.rcWork.bottom >= mi.rcMonitor.bottom) return;
    RECT r = VisibleRect(h);
    if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return;   // a full-screen app
    if (r.bottom - mi.rcWork.bottom <= (mi.rcMonitor.bottom - mi.rcWork.bottom) / 4 || r.bottom > mi.rcMonitor.bottom || r.top >= mi.rcWork.bottom - 40) return;   // a window's own invisible border hanging a few px over is not "in the bar" (pulling it back fought apps that draw their own frame)
    typedef BOOL(WINAPI * Arr)(HWND); static Arr arranged = (Arr)GetProcAddress(GetModuleHandleW(L"user32.dll"), "IsWindowArranged"); if (arranged && arranged(h)) return;   // snapped by Windows: it fits it to the work area itself
    Log(L"window overlapped the bar by %d px, pulled back: %s", (int)(r.bottom - mi.rcWork.bottom), ClassOf(h).c_str());
    RECT c = r; c.bottom = mi.rcWork.bottom; SetVisibleRect(h, c);
}
// Maximized windows only re-read the screen's usable area when it CHANGES. After a rotation, sleep or game mode switch they can stay as tall as
// the whole screen, behind the bar, even though Windows' work area is right again. Pull any such window's bottom edge up to the work area.
void FixMaximizedOverBar() {
    std::vector<HWND> all; EnumWindows([](HWND h, LPARAM p) -> BOOL { ((std::vector<HWND>*)p)->push_back(h); return TRUE; }, (LPARAM)&all);
    for (HWND h : all) {
        if (!IsAppWindow(h) || !IsZoomed(h) || IsIconic(h)) continue;
        HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(m, &mi) || mi.rcWork.bottom >= mi.rcMonitor.bottom) continue;
        Bar* b = BarOnMonitor(m); if (!b || b->hiddenFs || !IsWindowVisible(b->hwnd) || b->hmon != m) continue;   // bar hidden for a full-screen app: nothing to protect
        RECT r = VisibleRect(h); if (r.top >= mi.rcWork.bottom - 40) continue;
        // Every maximized window hangs a little past the work area: its invisible resize border (8 to 16 px by scale, more for apps that draw their own frame,
        // like DeskDrawer and Chrome). Pulling THAT up made Windows put it straight back, so a maximized window was resized every 3 s, forever (thousands of
        // times a day: Chrome drew half its page, drags glitched, Windows' taskbar popped up). Only a window that really covers the bar (the stale full-screen
        // height after a rotation runs under it by the whole bar) is pulled up, and never the same window twice within a minute.
        if (r.bottom - mi.rcWork.bottom <= (mi.rcMonitor.bottom - mi.rcWork.bottom) / 2) continue;
        static std::map<HWND, uint64_t> pulled; uint64_t now = Now(); auto it = pulled.find(h);
        if (it != pulled.end() && now - it->second < 60000) continue;   // pulled up a moment ago and back again: Windows wants it there, leave it
        pulled[h] = now;
        Log(L"maximized window ran %d px under the bar, pulled up: %s", (int)(r.bottom - mi.rcWork.bottom), ClassOf(h).c_str());
        RECT c = r; c.bottom = mi.rcWork.bottom; SetVisibleRect(h, c);
    }
}
void WinMoveStart() { std::thread(SnapperThread).detach(); g_screens = ScreenKeys(); g_sig = MonSignature(); Snapshot(); }
void WinMoveTick(int n) {   // from the app tick (250 ms)
    ClampForeground(); if (n % 12 == 6) FixMaximizedOverBar();
    ws sig = MonSignature(); if (sig != g_sig) { g_sig = sig; g_changing = true; g_changeAt = Now() + 1800; }
    if (g_changing && Now() >= g_changeAt) { g_changing = false; OnScreensChanged(); }
    else if (!g_changing && n % 12 == 0) Snapshot();   // every ~3 s
}
RECT PopupSpot(const RECT& popup, const RECT& owner, const RECT& work) {   // the popup's size, centred on its owner, kept inside the owner's screen
    int w = popup.right - popup.left, h = popup.bottom - popup.top, cx = (owner.left + owner.right) / 2, cy = (owner.top + owner.bottom) / 2;
    return FitInside({cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h}, work);
}
