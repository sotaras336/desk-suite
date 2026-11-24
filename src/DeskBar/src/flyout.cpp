#include "pch.h"
#include "flyout.h"

Brush PanelBrush(bool right) {
    float g = ThemeGrad() > 0 && CurrentTheme().kind == 0 ? ThemeGrad() : 0;
    auto top = Col(0x1257B8), bar = Col(K::Bar);
    if (g > 0) top = ShiftColor(top, -0.5f * g, 0.002f * g);   // the fade: the panel's light corner turns toward a lighter shade
    if (g > 0 && right) bar = ShiftColor(bar, -0.5f * g, 0.0015f * g);   // the bar fades left to right (FadeB): the panel's bottom ends on the bar colour right under it
    // Start (left): light top fading down to the bar colour. Side panels (right): the bar's right-end colour at the bottom, darkest at the top.
    if (right) top = ShiftColor(bar, 0, -0.12f);
    return LinearB(0, 0, 0, 1, {{0, top}, {1, bar}});
}

void Flyout::Build() {
    bool glass = ThemeIsGlass(CurrentTheme()); transparent = glass;   // glass: translucent and not layered (a layered window cannot show the acrylic backdrop)
    Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (glass ? 0 : WS_EX_LAYERED), WS_POPUP, -30000, -30000, 100, 100);
    StyleFlyout(hwnd, true); if (glass) ApplyGlass(hwnd); else SetAlpha(0);
    root->bg = PanelBrush(anchorRight); root->hit = true;
    onKeyDown = [this](UINT vk, bool c, bool s) { if (OnKey(vk, c, s)) return true; if (vk == VK_ESCAPE) { if (!OnEsc()) Close(); return true; } return false; };
    onDeactivate = [this] { if (shown && !hiding && !keepOpen) Close(); };
}
float Flyout::ContentHeight(float maxH) { Sz d = root->Measure({widthDip, maxH}); return std::min(d.h, maxH); }

// In the screen's work area, a 12-unit gap from its edges, bottom-aligned (left for Start, right for Quick Settings), never taller than the work area
RECT FlyoutRectOn(const RECT& w, const RECT& monitor, float sc, float widthDip, float hDip, bool anchorRight, float* maxHDip) {
    float gap = 12 * sc, maxH = (w.bottom - w.top - 2 * gap) / sc; if (maxHDip) *maxHDip = maxH;
    float h = std::min(hDip, maxH); int cw = (int)ceilf(widthDip * sc), ch = (int)ceilf(h * sc);
    int x = anchorRight ? (int)(w.right - gap - cw) : (int)(w.left + gap), y = (int)(w.bottom - gap - ch);
    x = std::clamp(x, (int)monitor.left, std::max((int)monitor.left, (int)monitor.right - cw));
    return {x, y, x + cw, y + ch};
}
void Flyout::Place(bool animate) {
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    // move onto the target monitor first so the window takes that monitor's DPI before we measure
    SetWindowPos(hwnd, nullptr, mi.rcWork.left + 20, mi.rcWork.top + 20, 100, 100, SWP_NOACTIVATE | SWP_NOZORDER); UpdateScale();
    float maxH = 0; FlyoutRectOn(mi.rcWork, mi.rcMonitor, scale, widthDip, 0, anchorRight, &maxH);
    RECT f = FlyoutRectOn(mi.rcWork, mi.rcMonitor, scale, widthDip, fixedH > 0 ? fixedH : ContentHeight(maxH), anchorRight);
    curX = f.left; curY = f.top; curW = f.right - f.left; curH = f.bottom - f.top;
    SetWindowPos(hwnd, nullptr, curX, curY, curW, curH, SWP_NOACTIVATE | SWP_NOZORDER); needLayout = true;
}
void Flyout::Refit() {
    if (!hwnd || !IsWindowVisible(hwnd)) return;
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); float sc = scale, gap = 12 * sc; RECT w = mi.rcWork;
    float maxH = (w.bottom - w.top - 2 * gap) / sc, h = std::min(fixedH > 0 ? fixedH : ContentHeight(maxH), maxH);
    curH = (int)ceilf(h * sc); curY = (int)(w.bottom - gap - curH);
    SetWindowPos(hwnd, nullptr, curX, curY, curW, curH, SWP_NOACTIVATE | SWP_NOZORDER); needLayout = true; Invalidate();
}
void Flyout::Open(HMONITOR m) {
    mon = m; bool wasHiding = hiding; hiding = false; AnimCancel(this);
    if (!wasHiding) { OnOpen(); Place(true); if (needLayout) Layout(); }
    float off = std::min(curH * 0.12f, 36 * scale); shown = true;
    SetWindowPos(hwnd, HWND_TOPMOST, curX, curY + (int)off, curW, curH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    PaintNow();
    ForceForeground(hwnd);
    AnimFn(this, 100, Ease::Linear, [this, off](float t) {
        float e = 1 - powf(1 - t, 5); SetWindowPos(hwnd, nullptr, curX, curY + (int)(off * (1 - e)), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetAlpha((BYTE)(std::min(1.f, 2 * t) * 255));
    });
    downPrev = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
    if (!poll) poll = UiTimer(40, [this] {
        bool down = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
        if (down && !downPrev && shown && !hiding) {
            POINT p; GetCursorPos(&p); RECT r; GetWindowRect(hwnd, &r);
            if (!PtInRect(&r, p) && !MenuContains(p) && !keepOpen) Close();
        }
        downPrev = down;
    });
    OnShown();
}
void Flyout::Close(bool instant) {
    if (!shown || hiding) return;
    TipHide(); MenuClose(); CancelInput();
    if (poll) { UiKillTimer(poll); poll = 0; }
    closedAt = Now(); AnimCancel(this);
    auto done = [this] { ShowWindow(hwnd, SW_HIDE); shown = false; hiding = false; OnClose(); };
    if (instant) { done(); return; }
    hiding = true; float off = 40 * scale;
    AnimFn(this, 80, Ease::CubicIn, [this, off](float t) {
        SetWindowPos(hwnd, nullptr, curX, curY + (int)(off * t), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); SetAlpha((BYTE)((1 - t) * 255));
    }, done);
}

// ---------- registry ----------
static std::unique_ptr<Flyout> g_fly[4];
Flyout* Panel_(Panel p) {
    int i = (int)p - 1; if (i < 0 || i > 3) return nullptr;
    if (!g_fly[i]) {
        g_fly[i].reset(p == Panel::Start ? MakeStartPanel() : p == Panel::Quick ? MakeQuickPanel() : p == Panel::Clip ? MakeClipPanel() : MakeClockPanel());   // Clip: built the first time it opens, never prewarmed
        g_fly[i]->Build();
    }
    return g_fly[i].get();
}
void PrewarmPanels() {   // built at idle after start-up so the first open is instant
    static int step = 0;
    UiOnce(900, [] { Panel_(Panel::Clock); UiOnce(300, [] { Panel_(Panel::Quick); UiOnce(300, [] { Panel_(Panel::Start); }); }); });
    (void)step;
}
void PanelsSysChanged() { for (auto& f : g_fly) if (f && f->shown) f->OnSysChange(); }
HMONITOR RecentPanelScreen() { for (auto& f : g_fly) if (f && f->mon && (f->shown || Now() - f->closedAt < 1500)) return f->mon; return nullptr; }   // Start closes as you pick an app: still counts for a moment
Panel OpenPanel() { for (int i = 0; i < 4; i++) if (g_fly[i] && g_fly[i]->shown && !g_fly[i]->hiding) return (Panel)(i + 1); return Panel::None; }
void ClosePanels() { for (auto& f : g_fly) if (f) f->Close(); }
void TogglePanel(Panel p, Wnd* from, const char* page) {
    Flyout* f = Panel_(p); if (!f) return; (void)page;
    if (f->shown && !f->hiding) { f->Close(); return; }
    if (Now() - f->closedAt < 300) return;   // the click that just dismissed it must not reopen it
    for (auto& o : g_fly) if (o && o.get() != f) o->Close();
    PreviewClose();
    HMONITOR m = nullptr;
    if ((p == Panel::Start || (p == Panel::Clip && !from)) && !g_dev) { HWND fg = GetForegroundWindow(); if (fg && ClassOf(fg) != kWndClass) m =MonitorFromWindow(fg, MONITOR_DEFAULTTONULL); if (!m) { POINT c; GetCursorPos(&c); m = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST); } }
    else m = from && from->hwnd ? MonitorFromWindow(from->hwnd, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    f->Open(m);
}
