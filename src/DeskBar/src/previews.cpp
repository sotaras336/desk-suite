#include "pch.h"
#include "bar.h"
#include "widgets.h"

// Live window previews above a taskbar button (real DWM thumbnails) and "peek": hovering a card with the mouse dims every screen and
// shows that window at full size where it sits.
namespace {
const float kThumbW = 174, kThumbH = 110;
RECT VisibleRect(HWND h) { RECT r{}; if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(h, &r); return r; }
Sz FitToWindow(HWND h) {
    RECT r; if (IsIconic(h)) { WINDOWPLACEMENT wp{sizeof wp}; GetWindowPlacement(h, &wp); r = wp.rcNormalPosition; } else r = VisibleRect(h);
    int w = r.right - r.left, hh = r.bottom - r.top; if (w <= 0 || hh <= 0) return {kThumbW, kThumbH};
    float a = (float)w / hh, tw = kThumbW, th = kThumbW / a; if (th > kThumbH) { th = kThumbH; tw = kThumbH * a; }
    return {roundf(std::max(tw, 90.f)), roundf(std::max(th, 50.f))};
}

// ---------------- peek ----------------
const wchar_t* kPeekCls = L"DB3.Peek";
std::vector<HWND> g_dims; ws g_dimKey; HWND g_peekWin = nullptr, g_peekTarget = nullptr; HTHUMBNAIL g_peekThumb = nullptr; UINT_PTR g_fadeTimer = 0; uint64_t g_fadeT0 = 0;
void PeekHide();
LRESULT CALLBACK PeekProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // A finger or pen is not let through like the mouse is: a screen-sized peek once caught every tap and the taskbar locked up.
    if ((m >= 0x0240 && m <= 0x024F) || (m >= 0x0201 && m <= 0x0209)) { PeekHide(); return 0; }
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, m, w, l);
}
HWND MakePeek(const RECT& r, bool layered) {
    static bool reg = [] { WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = PeekProc; wc.hInstance = g_inst; wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = kPeekCls; return RegisterClassExW(&wc) != 0; }(); (void)reg;
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT | (layered ? WS_EX_LAYERED : 0), kPeekCls, L"DeskBar Peek", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, g_inst, nullptr);
    if (layered && h) SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA); return h;
}
void SetDim(float t) { for (HWND d : g_dims) SetLayeredWindowAttributes(d, 0, (BYTE)lroundf(150 * t), LWA_ALPHA); }
void PeekHide() {
    if (g_fadeTimer) { UiKillTimer(g_fadeTimer); g_fadeTimer = 0; }
    g_peekTarget = nullptr; if (g_peekThumb) { DwmUnregisterThumbnail(g_peekThumb); g_peekThumb = nullptr; }
    if (g_peekWin) ShowWindow(g_peekWin, SW_HIDE);
    for (HWND d : g_dims) { SetLayeredWindowAttributes(d, 0, 0, LWA_ALPHA); ShowWindow(d, SW_HIDE); }
}
void PeekShow(HWND target, HWND keepAbove) {
    if (!target || !IsWindow(target) || IsIconic(target) || !IsWindowVisible(target)) { PeekHide(); return; }
    RECT vis = VisibleRect(target), outer; if (!GetWindowRect(target, &outer) || vis.right <= vis.left) { PeekHide(); return; }
    auto mons = Monitors(); ws key; for (auto& m : mons) key += Fmt(L"%d,%d,%d,%d;", m.bounds.left, m.bounds.top, m.bounds.right, m.bounds.bottom);
    bool ok = key == g_dimKey; for (HWND d : g_dims) if (!IsWindow(d)) ok = false;
    if (!ok) { for (HWND d : g_dims) DestroyWindow(d); g_dims.clear(); for (auto& m : mons) g_dims.push_back(MakePeek(m.bounds, true)); g_dimKey = key; }
    if (!g_peekWin || !IsWindow(g_peekWin)) g_peekWin = MakePeek(vis, false);
    bool fresh = !g_peekTarget; if (g_peekThumb) { DwmUnregisterThumbnail(g_peekThumb); g_peekThumb = nullptr; }
    g_peekTarget = target; SetWindowPos(g_peekWin, nullptr, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (FAILED(DwmRegisterThumbnail(g_peekWin, target, &g_peekThumb))) { PeekHide(); return; }
    SIZE src{}; DwmQueryThumbnailSourceSize(g_peekThumb, &src);
    RECT crop{std::max(0L, vis.left - outer.left), std::max(0L, vis.top - outer.top), std::min(src.cx, vis.right - outer.left), std::min(src.cy, vis.bottom - outer.top)};
    if (crop.right <= crop.left || crop.bottom <= crop.top) crop = {0, 0, src.cx, src.cy};
    DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
    p.rcDestination = {0, 0, vis.right - vis.left, vis.bottom - vis.top}; p.rcSource = crop; p.opacity = 255; p.fVisible = TRUE; p.fSourceClientAreaOnly = FALSE; DwmUpdateThumbnailProperties(g_peekThumb, &p);
    int round = IsZoomed(target) ? 1 : 2; DwmSetWindowAttribute(g_peekWin, 33, &round, 4);
    // Z-order: dim sheets, the peeked window, the taskbars, then the preview popup on top
    UINT fl = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    for (HWND d : g_dims) SetWindowPos(d, HWND_TOPMOST, 0, 0, 0, 0, fl); SetWindowPos(g_peekWin, HWND_TOPMOST, 0, 0, 0, 0, fl);
    for (auto* b : AllBars()) SetWindowPos(b->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (keepAbove) SetWindowPos(keepAbove, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (fresh) { g_fadeT0 = Now(); if (g_fadeTimer) UiKillTimer(g_fadeTimer); g_fadeTimer = UiTimer(16, [] { float t = std::min(1.f, (Now() - g_fadeT0) / 160.f); SetDim(1 - powf(1 - t, 3)); if (t >= 1) { UiKillTimer(g_fadeTimer); g_fadeTimer = 0; } }); }
    else SetDim(1);
}

// ---------------- the popup ----------------
struct PreviewWnd : Wnd {
    PreviewReq req; std::vector<std::pair<HTHUMBNAIL, HWND>> thumbs; std::vector<std::pair<View*, HWND>> wells; HWND fg0 = nullptr, peekFor = nullptr;
    UINT_PTR hideT = 0, peekT = 0, endPeekT = 0, watch = 0; bool open = false;
    PreviewWnd() { noActivate = true; transparent = true; }
    ~PreviewWnd() { if (hideT) UiKillTimer(hideT); if (peekT) UiKillTimer(peekT); if (endPeekT) UiKillTimer(endPeekT); if (watch) UiKillTimer(watch); }
    void Init(POINT at) {
        Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_POPUP, at.x, at.y, 10, 10);
        MARGINS mg{-1, -1, -1, -1}; DwmExtendFrameIntoClientArea(hwnd, &mg);
        int round = 2; DwmSetWindowAttribute(hwnd, 33, &round, 4); DWORD border = 0x003D3D3D; DwmSetWindowAttribute(hwnd, 34, &border, 4);
        int dark = CurrentTheme().kind == 2 ? 0 : 1; DwmSetWindowAttribute(hwnd, 20, &dark, 4); int bd = 3; DwmSetWindowAttribute(hwnd, 38 /*SYSTEMBACKDROP_TYPE: transient acrylic*/, &bd, 4);   // the white theme gets light acrylic (its text is dark)
        root->onHover = [this](bool in) { if (in) PreviewCancelHide(); else PreviewHideSoon(); };
        watch = UiTimer(150, [this] { if (open && req.touch && GetForegroundWindow() != fg0) Close(); });
    }
    void ClearThumbs() { for (auto& t : thumbs) DwmUnregisterThumbnail(t.first); thumbs.clear(); wells.clear(); }
    void Close() { if (hideT) { UiKillTimer(hideT); hideT = 0; } if (peekT) { UiKillTimer(peekT); peekT = 0; } PeekHide(); ClearThumbs(); CancelInput(); if (hwnd) ShowWindow(hwnd, SW_HIDE); open = false; }
    void PeekSoon(HWND target) {
        if (endPeekT) { UiKillTimer(endPeekT); endPeekT = 0; } peekFor = target; if (peekT) { UiKillTimer(peekT); peekT = 0; }
        if (g_peekTarget) PeekShow(target, hwnd); else peekT = UiTimer(450, [this, target] { UiKillTimer(peekT); peekT = 0; PeekShow(target, hwnd); });
    }
    void PeekEndSoon() { if (peekT) { UiKillTimer(peekT); peekT = 0; } if (endPeekT) UiKillTimer(endPeekT); endPeekT = UiTimer(120, [this] { UiKillTimer(endPeekT); endPeekT = 0; PeekHide(); }); }
    void Refresh() {
        auto key = req.key; bool touch = req.touch; UiOnce(400, [key, touch] { TaskBtn* b = FindBtn(key); if (!b || b->e.wins.empty()) PreviewClose(); else PreviewOpen(b->Req(touch)); });
    }
    View* Card(View* parent, const WinInfo& w) {
        Sz t = FitToWindow(w.h); bool active = w.h == fg0; HWND hw = w.h;
        auto* c = MkBox(parent, Lay::V); c->fw = t.w + 16; c->pad = Thick(8, 0, 8, 8); c->margin = Thick(2); c->radius = 8; c->hit = true; c->borderW = 1;
        auto setRest = [c, active] { c->bg = active ? SolidB(K::White, 0x1A) : Brush{}; c->border = active ? Col(K::White, K::CardLine) : Col(K::White, 0); }; setRest();
        auto* hd = MkBox(c, Lay::H); hd->fh = 34; hd->margin = Thick(2, 0, 0, 0);
        ws ik = WinIconKey(w); auto* ic = hd->Add(new Pic()); ic->fw = ic->fh = 16; ic->va = Al::Center; ic->provider = [ik](int px) { return IconGet(ik, px); };
        auto* tt = MkLabel(hd, w.title, FText(12.5f)); tt->margin = Thick(10, 0, 4, 1); tt->maxW = std::max(t.w - 40, 60.f); tt->va = Al::Center; tt->star = 1;
        auto* cl = IconBtn(hd, 0xE711, 28, 10, [this, hw] { PostMessageW(hw, WM_CLOSE, 0, 0); Refresh(); }); cl->bg = Brush{}; cl->bgHover = SolidB(K::White, 0x20); cl->opacity = req.touch ? 1.f : 0.f; cl->va = Al::Center;
        auto* well = MkBox(c); well->fw = t.w; well->fh = t.h; well->radius = 6; well->bg = SolidB(K::Black, 0x26); wells.push_back({well, hw});
        c->onHover = [this, c, cl, hw, setRest](bool in) {
            if (touchLast) return;
            if (in) { c->bg = SolidB(K::White, 0x28); c->border = Col(K::White, K::CardLine); cl->opacity = 1; PeekSoon(hw); } else { setRest(); cl->opacity = req.touch ? 1.f : 0.f; PeekEndSoon(); }
            Invalidate();
        };
        c->onClick = [this, hw] { PreviewClose(); ForceForeground(hw); WinTrackerPoke(); };
        c->onMiddle = [this, hw] { PostMessageW(hw, WM_CLOSE, 0, 0); Refresh(); };
        return c;
    }
    View* ListRow(View* parent, const WinInfo& w) {
        HWND hw = w.h; auto* r = MkBox(parent, Lay::H); r->fh = 34; r->pad = Thick(10, 0, 6, 0); r->radius = 6; r->hit = true; r->gap = 10;
        ws ik = WinIconKey(w); auto* ic = r->Add(new Pic()); ic->fw = ic->fh = 16; ic->va = Al::Center; ic->provider = [ik](int px) { return IconGet(ik, px); };
        auto* tt = MkLabel(r, w.title, FText(13)); tt->star = 1; tt->va = Al::Center;
        auto* cl = IconBtn(r, 0xE711, 26, 10, [this, hw] { PostMessageW(hw, WM_CLOSE, 0, 0); Refresh(); }); cl->bg = Brush{}; cl->opacity = 0; cl->va = Al::Center;
        r->onHover = [this, r, cl, hw](bool in) { if (touchLast) return; r->bg = in ? SolidB(0x383838) : Brush{}; cl->opacity = in ? 1.f : 0.f; if (in) PeekSoon(hw); else PeekEndSoon(); Invalidate(); };
        r->onClick = [this, hw] { PreviewClose(); ForceForeground(hw); WinTrackerPoke(); }; r->onMiddle = [this, hw] { PostMessageW(hw, WM_CLOSE, 0, 0); Refresh(); };
        return r;
    }
    void Show(const PreviewReq& rq) {
        if (hideT) { UiKillTimer(hideT); hideT = 0; } req = rq; ClearThumbs(); PeekHide(); fg0 = GetForegroundWindow();
        RECT br = rq.wnd->ScreenRect(rq.anchor); POINT mid{(br.left + br.right) / 2, (br.top + br.bottom) / 2};
        HMONITOR mon = MonitorFromPoint(mid, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
        if (!hwnd) Init(mid);
        SetWindowPos(hwnd, nullptr, mid.x, mid.y, 10, 10, SWP_NOACTIVATE | SWP_NOZORDER); UpdateScale(); float sc = scale;
        View* r = root; r->Clear(); r->lay = Lay::V; r->bg = SolidB(0x1C1C1C, 0xE6); r->pad = Thick(4); r->radius = 0;   // nearly solid over the blur: at 0x60 it looked see-through
        float monW = (mi.rcMonitor.right - mi.rcMonitor.left) / sc;
        if (rq.wins.empty()) { auto* l = MkLabel(r, rq.name, FText(12)); l->margin = Thick(10, 6, 10, 7); }
        else {
            float need = 8; for (auto& w : rq.wins) need += FitToWindow(w.h).w + 20;
            if (need > monW - 40) {   // a row would not fit across the screen: a plain list of titles
                auto* sv = r->Add(new Scroll()); sv->fw = 380; sv->fh = std::min(34.f * rq.wins.size(), (mi.rcWork.bottom - mi.rcWork.top) / sc - 40); auto* col = MkBox(nullptr, Lay::V); sv->content = sv->Add(col);
                for (auto& w : rq.wins) ListRow(col, w);
            } else { auto* row = MkBox(r, Lay::H); for (auto& w : rq.wins) Card(row, w); }
        }
        Sz d = r->Measure({monW - 16, INF}); int pw = (int)ceilf(d.w * sc), ph = (int)ceilf(d.h * sc);
        int x = std::clamp((int)(mid.x - pw / 2), (int)mi.rcMonitor.left + (int)(8 * sc), std::max((int)mi.rcMonitor.left + (int)(8 * sc), (int)mi.rcMonitor.right - pw - (int)(8 * sc)));
        int y = (int)(br.top - ph - 8 * sc); if (y < mi.rcMonitor.top) y = (int)(br.bottom + 8 * sc);   // a bar docked at the top: open below it
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW); open = true; Layout(); PaintNow();
        for (auto& wl : wells) {   // thumbnails are drawn by Windows on top of this window
            HTHUMBNAIL th; if (FAILED(DwmRegisterThumbnail(hwnd, wl.second, &th))) continue;
            SIZE src{}; DwmQueryThumbnailSourceSize(th, &src); RECT crop{0, 0, src.cx, src.cy}, outer;
            if (!IsIconic(wl.second) && GetWindowRect(wl.second, &outer)) {   // crop to the visible window: the raw thumbnail includes invisible resize borders
                RECT vis = VisibleRect(wl.second);
                if (vis.right > vis.left && vis.bottom > vis.top) crop = {std::max(0L, vis.left - outer.left), std::max(0L, vis.top - outer.top), std::min(src.cx, vis.right - outer.left), std::min(src.cy, vis.bottom - outer.top)};
            }
            float bw = wl.first->rc.w() * sc, bh = wl.first->rc.h() * sc, cw = (float)std::max(1L, crop.right - crop.left), ch = (float)std::max(1L, crop.bottom - crop.top), k = std::min(bw / cw, bh / ch), tw = cw * k, tH = ch * k;
            int left = (int)lroundf(wl.first->rc.l * sc + (bw - tw) / 2), top = (int)lroundf(wl.first->rc.t * sc + (bh - tH) / 2);
            DWM_THUMBNAIL_PROPERTIES p{}; p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY | DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
            p.rcDestination = {left, top, left + (int)lroundf(tw), top + (int)lroundf(tH)}; p.rcSource = crop; p.opacity = 255; p.fVisible = TRUE; DwmUpdateThumbnailProperties(th, &p);
            thumbs.push_back({th, wl.second});
        }
    }
};
std::unique_ptr<PreviewWnd> g_pv;
}

void PreviewOpen(const PreviewReq& r) { Log(L"preview open key=%s wins=%d", r.key.c_str(), (int)r.wins.size()); if (!g_pv) g_pv = std::make_unique<PreviewWnd>(); if (r.wnd && r.wnd->hwnd) g_pv->Show(r); }
void PreviewHideSoon() { if (!g_pv || !g_pv->open) return; if (g_pv->hideT) UiKillTimer(g_pv->hideT); g_pv->hideT = UiTimer(300, [] { if (g_pv) g_pv->Close(); }); }
void PreviewCancelHide() { if (g_pv && g_pv->hideT) { UiKillTimer(g_pv->hideT); g_pv->hideT = 0; } }
void PreviewClose() { if (g_pv) g_pv->Close(); }
bool PreviewShowingFor(const ws& key) { return g_pv && g_pv->open && (key == L"*" || g_pv->req.key == key); }
void PreviewSnapshot(const ws& path) { if (g_pv && g_pv->open) g_pv->Snapshot(path); }
