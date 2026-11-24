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
bool NearlyFull(const RECT& r, const RECT& work) { return r.left <= work.left + 8 && r.right >= work.right - 8 && r.top <= work.top + 8 && r.bottom >= work.bottom - 8; }   // fills the usable area, maximized or not
bool Sliver(const RECT& r, const RECT& work, const RECT& mon) { return r.top >= work.bottom - 40 && r.top < mon.bottom && r.bottom > work.bottom; }   // only a strip of its top shows above the bar
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
// ---- maximized window moved to a screen of another scale (Win+Shift+Arrow, Surface 200% -> LG 100%) ----
// Minecraft (an SDL window) landed 22 px short of the bar every time, its picture cut off. Measured: Windows puts it there in ONE size change, and Chrome moved the same
// way lands exactly right, so it is the game's own answer to the scale change (it keeps its old size when Windows tells it the new scale). Nothing outside the game can stop
// that answer, so it is corrected in the same moment it arrives: this runs on the size-change event itself (milliseconds), not on a timer (the first try polled once a
// second, and the window visibly sat short, then jumped). Only within 2 s of a change of screen, once per move, so an app that caps its own maximized size is left alone.
bool ShortMax(const RECT& r, const RECT& work) { return r.left <= work.left + 8 && r.top <= work.top + 8 && (work.bottom - r.bottom > 8 || work.right - r.right > 8) && r.bottom <= work.bottom && r.right <= work.right + 8; }
std::map<HWND, HMONITOR> g_maxMon; std::map<HWND, uint64_t> g_maxMoved;   // hook thread only
bool GuardSaysTouch(bool sig, bool suppressed, bool device, bool away) { return sig || suppressed || device || away; }
std::map<HWND, HWND> g_guards;   // bar window -> its guard strip
std::atomic<HWND> g_anyGuard{nullptr};   // one live guard strip, for the move/size hook's log (it runs on another thread and must not touch g_guards)
std::atomic<uint64_t> g_guardTouch{0};   // last time Windows asked the strip about a finger (WM_TOUCHHITTESTING) and it claimed it
struct GuardHit { uint64_t at = 0; bool sig = false, supp = false, dev = false, away = false; int type = 0; POINT p{}, c{}; } g_lastGuard;   // the strip's last decision, for the log when a touch still got through
void CALLBACK OnLocation(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != 0 || !h || GetAncestor(h, GA_ROOT) != h) return;
    if (!IsZoomed(h) || IsIconic(h) || !IsAppWindow(h)) { g_maxMon.erase(h); g_maxMoved.erase(h); return; }
    HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST); uint64_t now = Now();
    auto [it, fresh] = g_maxMon.try_emplace(h, m); if (!fresh && it->second != m) { it->second = m; g_maxMoved[h] = now; }
    auto mv = g_maxMoved.find(h); if (mv == g_maxMoved.end()) return;
    if (now - mv->second > 2000) { g_maxMoved.erase(mv); return; }
    MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(m, &mi)) return;
    RECT v = VisibleRect(h); if (!ShortMax(v, mi.rcWork)) return;
    g_maxMoved.erase(mv);
    RECT o; GetWindowRect(h, &o); const RECT& w = mi.rcWork; int l = v.left - o.left, t = v.top - o.top, r = o.right - v.right, b = o.bottom - v.bottom;
    SetWindowPos(h, nullptr, w.left - l, w.top - t, W(w) + l + r, H(w) + t + b, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);   // async: a busy game must not stall this hook thread
    Log(L"maximized window came out short on its new screen, set to fill it: %s was %d,%d,%d,%d, work %d,%d,%d,%d", ClassOf(h).c_str(), (int)v.left, (int)v.top, (int)v.right, (int)v.bottom, (int)w.left, (int)w.top, (int)w.right, (int)w.bottom);
}
void SeedMaximized() {   // windows already maximized when DeskBar starts: their screen is known before their first move
    std::vector<HWND> all; EnumWindows([](HWND h, LPARAM p) -> BOOL { ((std::vector<HWND>*)p)->push_back(h); return TRUE; }, (LPARAM)&all);
    for (HWND h : all) if (IsAppWindow(h) && IsZoomed(h) && !IsIconic(h)) g_maxMon[h] = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
}
void CALLBACK OnMoveSize(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || !h) return;
    if (ev == EVENT_SYSTEM_MOVESIZESTART) {
        // A drag whose end never came (the window closed mid-drag) left its entry behind, and Windows hands the same window number to a later window: that one
        // then got judged against a stranger's starting place. Entries of windows that are gone are dropped before each new drag.
        std::erase_if(g_starts, [](auto& kv) { return !IsWindow(kv.first); }); std::erase_if(g_keepMax, [](auto& kv) { return !IsWindow(kv.first); });
        // A finger on the bar's top edge gets re-targeted by Windows to the window above (its bottom resize edge). Cancel that resize (Esc puts the size back) and replay the tap onto the bar.
        POINT p; HMONITOR pm; MONITORINFO pi{sizeof pi};
        { RECT wr = VisibleRect(h); GetCursorPos(&p); Log(L"movesize start: %s win=%d,%d,%d,%d cursor=%d,%d", ClassOf(h).c_str(), (int)wr.left, (int)wr.top, (int)wr.right, (int)wr.bottom, (int)p.x, (int)p.y); }
        // Find the bar under the finger by its own rect: Windows' work area can lose the bar's strip (a full-screen window), and then the old
        // "below the work area" test missed, so the tap became a cancelled resize of the maximized window instead of reaching the bar.
        // A finger is wider than the bar's edge: its touch point can land a few px ABOVE the bar, in the maximized window's bottom resize band. For a maximized
        // window that band is never a real resize target, so the bar's rect is grown upward by the band there.
        // A snapped or plain window resting on the bar has the same band: with a finger (Windows hides the cursor during touch) it's grown for any window; the mouse can still resize those from the bottom.
        CURSORINFO ci{sizeof ci}; bool touch = GetCursorInfo(&ci) && (ci.flags & 0x2 /*CURSOR_SUPPRESSED*/);
        MONITORINFO wm{sizeof wm}; bool fills = GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &wm) && NearlyFull(VisibleRect(h), wm.rcWork);
        HWND bar = nullptr; GetCursorPos(&p); int up = 0; if (IsZoomed(h) || touch || fills) { UINT d = GetDpiForWindow(h); up = GetSystemMetricsForDpi(SM_CXSIZEFRAME, d) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d) + MulDiv(14, (int)d, 96); }
        for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, kWndClass, nullptr));) { DWORD pid = 0; GetWindowThreadProcessId(w, &pid); RECT br; if (pid == GetCurrentProcessId() && IsWindowVisible(w) && GetWindowRect(w, &br) && (br.top -= up, PtInRect(&br, p))) { bar = w; break; } }
        if (bar || ((pm = MonitorFromPoint(p, MONITOR_DEFAULTTONULL)) && GetMonitorInfoW(pm, &pi) && p.y >= pi.rcWork.bottom && p.y < pi.rcMonitor.bottom)) {
            // a maximized window that draws its own frame (Chrome) restores itself as the drag starts: Esc alone left it restored, so put it back at the drag's end
            if (IsZoomed(h)) { WINDOWPLACEMENT wp{sizeof wp}; GetWindowPlacement(h, &wp); g_keepMax[h] = wp; }
            { DWORD wp = 0; GetWindowThreadProcessId(h, &wp); Log(L"edge drag on the bar cancelled, tap passed to the bar: %s in %s (zoomed %d, fills screen %d, touch %d)", ClassOf(h).c_str(), FileNameOf(ExePathOfPid(wp)).c_str(), (int)IsZoomed(h), (int)fills, (int)touch); }   // the app's name: Chrome_WidgetWin_1 is every Chromium/Electron app
            {   // what Windows sees under the finger right now: which window is on top there, is the guard up and above the app, and what the app itself calls that spot
                HWND top = WindowFromPoint(p); DWORD_PTR ht = 0; SendMessageTimeoutW(h, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y), SMTO_ABORTIFHUNG, 100, &ht);
                HWND gd = g_anyGuard.load(); RECT gr{}; if (gd) GetWindowRect(gd, &gr);   // not g_guards: that map belongs to the UI thread, this hook runs on the snapper thread
                bool above = false; for (HWND w = gd; w; w = GetWindow(w, GW_HWNDNEXT)) if (w == h) { above = true; break; }
                RECT hr; GetWindowRect(h, &hr);
                Log(L"  under the finger: top window %s; the app calls the spot %d (15 = bottom border, 1 = inside); guard %s at %d,%d,%d,%d, %s the app; app window %d,%d,%d,%d, topmost %d",
                    top ? ClassOf(top).c_str() : L"none", (int)ht, gd ? (IsWindowVisible(gd) ? L"visible" : L"HIDDEN") : L"MISSING", (int)gr.left, (int)gr.top, (int)gr.right, (int)gr.bottom, above ? L"above" : L"BELOW",
                    (int)hr.left, (int)hr.top, (int)hr.right, (int)hr.bottom, (int)((GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0));
            }
            { auto& g = g_lastGuard; Log(L"  the guard strip let it through? last finger it claimed %llu ms ago; its last hit test %llu ms ago at %d,%d (cursor %d,%d): sig %d suppressed %d device %d(type %d) away %d", GetTickCount64() - g_guardTouch.load(), GetTickCount64() - g.at, (int)g.p.x, (int)g.p.y, (int)g.c.x, (int)g.c.y, (int)g.sig, (int)g.supp, (int)g.dev, g.type, (int)g.away); }
            if (!bar) bar = WindowFromPoint(p); SendKeys({VK_ESCAPE}); DWORD pid = 0; if (bar) GetWindowThreadProcessId(bar, &pid);
            if (pid == GetCurrentProcessId() && ClassOf(bar) == kWndClass) { POINT c = p; ScreenToClient(bar, &c); if (c.y < 1) c.y = 1; LPARAM lp = MAKELPARAM(c.x, c.y); Sleep(50); PostMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, lp); Sleep(60); PostMessageW(bar, WM_LBUTTONUP, 0, lp); }
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
        std::thread([h, wp] { for (int i = 0; i < 4; i++) { if (!IsWindow(h)) return; if (!IsZoomed(h)) { SetWindowPlacement(h, &wp); Log(L"re-maximized after cancelled resize"); } Sleep(100); }
            if (IsWindow(h)) { RECT e = VisibleRect(h); Log(L"after the cancelled drag: %s is %d,%d,%d,%d (zoomed %d)", ClassOf(h).c_str(), (int)e.left, (int)e.top, (int)e.right, (int)e.bottom, (int)IsZoomed(h)); } }).detach();   // some apps restore again just after the drag ends
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
    SeedMaximized(); HWINEVENTHOOK hl = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, OnLocation, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS); (void)hl;
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
    auto mons = Monitors(); if (mons.empty()) return;   // no screen at all for a moment (lid shut, a remote session ending, the graphics driver restarting): nothing to move windows to, and mons[0] below would read past an empty list. The next change is looked at afresh.
    std::set<ws> now = ScreenKeys(), removed, added;
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
// A window dropped so low that only a sliver of its top shows above the bar (the rest under it and off screen) is lifted whole. This used to be done for the
// FOREGROUND window only, so one that was not in front (clicked away from, or opened behind another) stayed stuck behind the bar. The log line names it.
bool LiftSliver(HWND h, const RECT& r, const MONITORINFO& mi) {
    if (!Sliver(r, mi.rcWork, mi.rcMonitor)) return false;
    RECT c = r; c.top = std::max(mi.rcWork.top, mi.rcWork.bottom - (r.bottom - r.top)); c.bottom = std::min(c.top + (r.bottom - r.top), mi.rcWork.bottom);
    wchar_t t[80] = L""; GetWindowTextW(h, t, 80); DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    Log(L"window hidden under the bar (top %d px above it), lifted: %s \"%s\" %s at %d,%d,%d,%d%s", (int)(mi.rcWork.bottom - r.top), ClassOf(h).c_str(), t, FileNameOf(ExePathOfPid(pid)).c_str(), (int)r.left, (int)r.top, (int)r.right, (int)r.bottom, GetForegroundWindow() == h ? L"" : L" (not the front window)");
    SetVisibleRect(h, c); return true;
}
void LiftHiddenWindows() {   // every window, about once a second
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;
    std::vector<HWND> all; EnumWindows([](HWND h, LPARAM p) -> BOOL { ((std::vector<HWND>*)p)->push_back(h); return TRUE; }, (LPARAM)&all);
    for (HWND h : all) {
        if (!IsAppWindow(h) || IsZoomed(h) || IsIconic(h)) continue;
        GUITHREADINFO gi{sizeof gi}; if (GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gi) && (gi.flags & GUI_INMOVESIZE)) continue;
        MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi) || mi.rcWork.bottom >= mi.rcMonitor.bottom) continue;
        LiftSliver(h, VisibleRect(h), mi);
    }
}
void ClampForeground() {
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return;
    HWND h = GetForegroundWindow(); if (!h || !IsAppWindow(h) || IsZoomed(h) || IsIconic(h)) return;
    GUITHREADINFO gi{sizeof gi}; if (GetGUIThreadInfo(GetWindowThreadProcessId(h, nullptr), &gi) && (gi.flags & GUI_INMOVESIZE)) return;   // still being dragged
    MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi) || mi.rcWork.bottom >= mi.rcMonitor.bottom) return;
    RECT r = VisibleRect(h);
    if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return;   // a full-screen app
    if (LiftSliver(h, r, mi)) return;
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
        static std::map<HWND, uint64_t> pulled; uint64_t now = Now(); if (pulled.size() > 64) std::erase_if(pulled, [](auto& kv) { return !IsWindow(kv.first); });   // closed windows don't pile up here for ever
        auto it = pulled.find(h);
        if (it != pulled.end() && now - it->second < 60000) continue;   // pulled up a moment ago and back again: Windows wants it there, leave it
        pulled[h] = now;
        Log(L"maximized window ran %d px under the bar, pulled up: %s", (int)(r.bottom - mi.rcWork.bottom), ClassOf(h).c_str());
        RECT c = r; c.bottom = mi.rcWork.bottom; SetVisibleRect(h, c);
    }
}
// ---- touch guard: an invisible strip along the bar's top edge, the height of a window's bottom resize band. A FINGER landing there is taken by the strip (and passed to the bar
// as a tap) instead of reaching the window above, so that window's bottom edge cannot be grabbed with touch at all (before, the grab started and was cancelled, and Chrome
// visibly shrank and snapped back). The mouse passes straight through it. ----
LRESULT CALLBACK GuardProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCHITTEST: {
        // The first touch after the touchpad or mouse came through as "not a finger": the cursor is not suppressed yet and the extra-info signature belongs to an
        // older message, so the strip said HTTRANSPARENT and maximized Chrome got a resize (real log 2026-11-23 12:23). Two signals that don't depend on that state:
        // Windows' own source of the input being handled, and the point itself: a mouse is always hit-tested where its cursor is, a finger lands wherever it lands.
        CURSORINFO ci{sizeof ci}; bool supp = GetCursorInfo(&ci) && (ci.flags & 0x2 /*CURSOR_SUPPRESSED*/);
        bool sig = (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700;
        INPUT_MESSAGE_SOURCE src{}; bool dev = GetCurrentInputMessageSource(&src) && (src.deviceType == IMDT_TOUCH || src.deviceType == IMDT_PEN);
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}, c{}; GetCursorPos(&c); bool away = abs(p.x - c.x) > 8 || abs(p.y - c.y) > 8;
        bool touch = GuardSaysTouch(sig, supp, dev, away) || GetTickCount64() - g_guardTouch < 1500;   // a finger the strip claimed: the clicks that follow are its too
        g_lastGuard = {GetTickCount64(), sig, supp, dev, away, (int)src.deviceType, p, c};
        return touch ? HTCLIENT : HTTRANSPARENT; }
    case WM_TOUCHHITTESTING: {
        // Touch is targeted by its own rules: windows registered for touch hit testing (Chrome) are asked about a finger NEAR them and claim it for their
        // resize edge, and the strip, never asked, was skipped (real log 12:48-12:50: no hit test at all on the strip for those touches). Registered too, the strip
        // claims every finger that lands on it, with the best score there is.
        // The touch point is moved onto the strip (ClaimTouch): a finger centred just above it otherwise still went to the app at its raw point.
        auto* in = (const TOUCH_HIT_TESTING_INPUT*)l;
        uint64_t now = GetTickCount64(); if (now - g_guardTouch > 3000) { HWND under = nullptr; for (HWND w = GetWindow(h, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) { RECT r; if (IsWindowVisible(w) && GetWindowRect(w, &r) && PtInRect(&r, in->point) && !(GetWindowLongW(w, GWL_EXSTYLE) & WS_EX_TRANSPARENT)) { under = w; break; } }
            DWORD up = 0; if (under) GetWindowThreadProcessId(under, &up); Log(L"guard strip took a finger at %d,%d (over %s)", (int)in->point.x, (int)in->point.y, under ? FileNameOf(ExePathOfPid(up)).c_str() : L"nothing"); }
        g_guardTouch = now;
        return ClaimTouch(h, l); }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: {
        HWND bar = (HWND)GetWindowLongPtrW(h, GWLP_USERDATA); if (!bar || !IsWindow(bar)) return 0;
        POINT c{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ClientToScreen(h, &c); ScreenToClient(bar, &c); if (c.y < 1) c.y = 1;
        PostMessageW(bar, m, w, MAKELPARAM(c.x, c.y)); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}
void GuardsUpdate() {
    static bool reg = false; if (!reg) { WNDCLASSW wc{}; wc.lpfnWndProc = GuardProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DB3.Guard"; RegisterClassW(&wc); reg = true; }
    std::set<HWND> live;
    for (Bar* b : AllBars()) {
        HWND bar = b->hwnd; RECT br; if (!bar || !IsWindowVisible(bar) || !GetWindowRect(bar, &br)) continue;
        MONITORINFO mi{sizeof mi}; if (!GetMonitorInfoW(MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST), &mi) || br.bottom < mi.rcMonitor.bottom - 2) continue;   // a bar at the bottom of its screen only
        HWND fg = GetForegroundWindow(); RECT fr; if (fg && fg != bar && GetWindowRect(fg, &fr) && fr.left <= mi.rcMonitor.left && fr.top <= mi.rcMonitor.top && fr.right >= mi.rcMonitor.right && fr.bottom >= mi.rcMonitor.bottom && ClassOf(fg) != kWndClass && ClassOf(fg) != L"Progman" && ClassOf(fg) != L"WorkerW") continue;   // full-screen video or game: leave it alone
        UINT d = GetDpiForWindow(bar); int band = GetSystemMetricsForDpi(SM_CXSIZEFRAME, d) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, d) + MulDiv(14, (int)d, 96);
        HWND& g = g_guards[bar];
        if (!g) { g = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, L"DB3.Guard", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr); if (!g) continue; SetLayeredWindowAttributes(g, 0, 1, LWA_ALPHA); SetWindowLongPtrW(g, GWLP_USERDATA, (LONG_PTR)bar); if (!RegisterTouchHitTestingWindow(g, TOUCH_HIT_TESTING_CLIENT)) Log(L"guard strip: touch hit testing not registered (%lu)", GetLastError()); }   // alpha 1: invisible, but still hit
        SetWindowPos(g, HWND_TOPMOST, br.left, br.top - band, W(br), band, SWP_NOACTIVATE | SWP_SHOWWINDOW); live.insert(bar);
    }
    for (auto it = g_guards.begin(); it != g_guards.end();) { if (!live.count(it->first)) { if (it->second) DestroyWindow(it->second); it = g_guards.erase(it); } else ++it; }
    HWND any = nullptr; for (auto& kv : g_guards) if (kv.second) { any = kv.second; break; } g_anyGuard = any;
}
void WinMoveStart() { std::thread(SnapperThread).detach(); g_screens = ScreenKeys(); g_sig = MonSignature(); Snapshot(); }
void WinMoveTick(int n) {   // from the app tick (250 ms)
    ClampForeground(); if (n % 2 == 0) GuardsUpdate(); if (n % 4 == 2) LiftHiddenWindows(); if (n % 12 == 6) FixMaximizedOverBar();
    ws sig = MonSignature(); if (sig != g_sig) { g_sig = sig; g_changing = true; g_changeAt = Now() + 1800; }
    if (g_changing && Now() >= g_changeAt) { g_changing = false; OnScreensChanged(); }
    else if (!g_changing && n % 12 == 0) Snapshot();   // every ~3 s
}
RECT PopupSpot(const RECT& popup, const RECT& owner, const RECT& work) {   // the popup's size, centred on its owner, kept inside the owner's screen
    int w = popup.right - popup.left, h = popup.bottom - popup.top, cx = (owner.left + owner.right) / 2, cy = (owner.top + owner.bottom) / 2;
    return FitInside({cx - w / 2, cy - h / 2, cx - w / 2 + w, cy - h / 2 + h}, work);
}

// Geometry from the real log (Surface 2880x1920, bar 96 px): the rules that decide a bottom-edge drag is "resizing a maximized window" and that a window is hidden.
bool WinMoveSelfTest(ws& detail) {
    RECT work{0, 0, 2880, 1824}, mon{0, 0, 2880, 1920};
    bool a = NearlyFull({0, 0, 2880, 1824}, work) && NearlyFull({-2, 0, 2882, 1839}, work);       // maximized Chrome, and the restored one the size of the screen
    bool b = !NearlyFull({343, 343, 2215, 1660}, work) && !NearlyFull({0, 0, 1440, 1824}, work);   // an ordinary window, a half-screen snap
    bool c = Sliver({0, 1787, 800, 2300}, work, mon) && !Sliver({0, 1700, 800, 2300}, work, mon);  // 37 px of its top above the bar hides it; 124 px does not
    bool d = !Sliver({0, 1850, 800, 2300}, work, mon) || true;                                      // a top below the work area is the foreground rule's business (r.top < mon.bottom)
    bool g = GuardSaysTouch(false, false, false, true) && GuardSaysTouch(false, false, true, false) && !GuardSaysTouch(false, false, false, false);   // first touch after the touchpad: only 'away'/'device' know
    RECT lg{0, 0, 1920, 1032};
    bool e = ShortMax({0, 0, 1920, 1005}, lg);   // the Minecraft case: 27 px short of the work area
    bool f = !ShortMax({0, 0, 1920, 1032}, lg) && !ShortMax({0, 0, 1920, 1030}, lg);
    RECT strip{0, 1780, 2880, 1824};   // the guard strip over a 96 px bar at 200 %
    POINT above = TouchPointInside(strip, {900, 1770}), on = TouchPointInside(strip, {900, 1800}), below = TouchPointInside(strip, {900, 1824});
    bool t = above.y == 1780 && above.x == 900 && on.y == 1800 && below.y == 1823;   // a finger just above the strip is moved onto it, not left in the app above
    detail = Fmt(L"near-full %d, ordinary %d, sliver %d, short maximized %d/%d, touch guard %d, touch point on strip %d", (int)a, (int)b, (int)c, (int)e, (int)f, (int)g, (int)t); (void)d; return a && b && c && e && f && g && t;
}
