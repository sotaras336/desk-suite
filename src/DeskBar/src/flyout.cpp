#include "pch.h"
#include "flyout.h"

Brush PanelBrush() { return LinearB(0, 0, 0.55f, 1, {{0, Col(0x1257B8)}, {0.5f, Col(0x0A4096)}, {1, Col(0x072C70)}}); }

void Flyout::Build() {
    bool glass = ThemeIsGlass(CurrentTheme()); transparent = glass;   // glass: translucent and not layered (a layered window cannot show the acrylic backdrop)
    Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | (glass ? 0 : WS_EX_LAYERED), WS_POPUP, -30000, -30000, 100, 100);
    StyleFlyout(hwnd, true); if (glass) ApplyGlass(hwnd); else SetAlpha(0);
    root->bg = PanelBrush(); root->hit = true;
    onKeyDown = [this](UINT vk, bool c, bool s) { if (OnKey(vk, c, s)) return true; if (vk == VK_ESCAPE) { if (!OnEsc()) Close(); return true; } return false; };
    onDeactivate = [this] { if (shown && !hiding && !keepOpen) Close(); };
}
float Flyout::ContentHeight(float maxH) { Sz d = root->Measure({widthDip, maxH}); return std::min(d.h, maxH); }

void Flyout::Place(bool animate) {
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    // move onto the target monitor first so the window takes that monitor's DPI before we measure
    SetWindowPos(hwnd, nullptr, mi.rcWork.left + 20, mi.rcWork.top + 20, 100, 100, SWP_NOACTIVATE | SWP_NOZORDER); UpdateScale();
    float sc = scale, gap = 12 * sc; RECT w = mi.rcWork;
    float maxH = (w.bottom - w.top - 2 * gap) / sc;
    float h = std::min(fixedH > 0 ? fixedH : ContentHeight(maxH), maxH);
    curW = (int)ceilf(widthDip * sc); curH = (int)ceilf(h * sc);
    curX = anchorRight ? (int)(w.right - gap - curW) : (int)(w.left + gap); curY = (int)(w.bottom - gap - curH);
    curX = std::clamp(curX, (int)mi.rcMonitor.left, std::max((int)mi.rcMonitor.left, (int)mi.rcMonitor.right - curW));
    SetWindowPos(hwnd, nullptr, curX, curY, curW, curH, SWP_NOACTIVATE | SWP_NOZORDER); needLayout = true;
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
static std::unique_ptr<Flyout> g_fly[3];
Flyout* Panel_(Panel p) {
    int i = (int)p - 1; if (i < 0 || i > 2) return nullptr;
    if (!g_fly[i]) {
        g_fly[i].reset(p == Panel::Start ? MakeStartPanel() : p == Panel::Quick ? MakeQuickPanel() : MakeClockPanel());
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
Panel OpenPanel() { for (int i = 0; i < 3; i++) if (g_fly[i] && g_fly[i]->shown && !g_fly[i]->hiding) return (Panel)(i + 1); return Panel::None; }
void ClosePanels() { for (auto& f : g_fly) if (f) f->Close(); }
void TogglePanel(Panel p, Wnd* from, const char* page) {
    Flyout* f = Panel_(p); if (!f) return; (void)page;
    if (f->shown && !f->hiding) { f->Close(); return; }
    if (Now() - f->closedAt < 300) return;   // the click that just dismissed it must not reopen it
    for (auto& o : g_fly) if (o && o.get() != f) o->Close();
    PreviewClose();
    HMONITOR m = nullptr;
    if (p == Panel::Start && !g_dev) { HWND fg = GetForegroundWindow(); if (fg && ClassOf(fg) != kWndClass) m =MonitorFromWindow(fg, MONITOR_DEFAULTTONULL); if (!m) { POINT c; GetCursorPos(&c); m = MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST); } }
    else m = from && from->hwnd ? MonitorFromWindow(from->hwnd, MONITOR_DEFAULTTONEAREST) : MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    f->Open(m);
}
