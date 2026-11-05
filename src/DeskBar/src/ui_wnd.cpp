#include "pch.h"
#include "ui.h"
void PreviewClose();   // app.h: the taskbar hover preview

const wchar_t* kWndClass = L"DB3.Wnd";

// ================= animation =================
namespace {
struct Tw {
    void* owner; View* v; Wnd* w; float* p; float from, to; double start; int ms, delay; Ease e;
    std::function<void(float)> fn; std::function<void()> done;
};
std::vector<Tw> g_tw; std::vector<Tw>* g_cur = nullptr; UINT_PTR g_animTimer;
double NowMs() { static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }(); LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
void Step();
void Kick() { if (!g_animTimer) g_animTimer = UiTimer(12, Step); }
void Step() {
    std::vector<Tw> cur = std::move(g_tw); g_tw.clear(); g_cur = &cur;
    std::set<Wnd*> dirty; std::vector<std::function<void()>> dones; double now = NowMs();
    for (auto& t : cur) {
        if (!t.owner) continue;
        double el = now - t.start - t.delay; if (el < 0) continue;
        float k = t.ms > 0 ? (float)(el / t.ms) : 1.f; bool fin = k >= 1; float eased = EaseF(t.e, k);
        if (t.p) *t.p = t.from + (t.to - t.from) * eased;
        if (t.fn) t.fn(eased);
        Wnd* w = t.w ? t.w : (t.v ? t.v->wnd : nullptr); if (w) dirty.insert(w);
        if (fin) { if (t.done) dones.push_back(std::move(t.done)); t.owner = nullptr; }
    }
    g_cur = nullptr;
    for (auto& t : cur) if (t.owner) g_tw.push_back(std::move(t));
    if (g_tw.empty() && g_animTimer) { UiKillTimer(g_animTimer); g_animTimer = 0; }
    for (auto* w : dirty) { if (!Wnd::From(w->hwnd) || Wnd::From(w->hwnd) != w) continue; w->Invalidate(); UpdateWindow(w->hwnd); }
    for (auto& d : dones) d();
}
}
void Anim(View* v, float* p, float to, int ms, Ease e, int delay) {
    for (auto& t : g_tw) if (t.p == p) t.owner = nullptr;
    if (g_cur) for (auto& t : *g_cur) if (t.p == p) t.owner = nullptr;
    if (ms <= 0) { *p = to; v->Invalidate(); return; }
    g_tw.push_back({v, v, nullptr, p, *p, to, NowMs(), ms, delay, e, {}, {}});
    std::erase_if(g_tw, [](const Tw& t) { return !t.owner; });
    Kick();
}
void AnimFn(Wnd* w, int ms, Ease e, std::function<void(float)> fn, std::function<void()> done) {
    g_tw.push_back({w, nullptr, w, nullptr, 0, 0, NowMs(), ms, 0, e, std::move(fn), std::move(done)});
    Kick();
}
void AnimCancel(void* owner) {
    for (auto& t : g_tw) if (t.owner == owner) t.owner = nullptr;
    if (g_cur) for (auto& t : *g_cur) if (t.owner == owner) t.owner = nullptr;
    std::erase_if(g_tw, [](const Tw& t) { return !t.owner; });
}
bool AnimActive(void* owner) {
    for (auto& t : g_tw) if (t.owner == owner) return true;
    if (g_cur) for (auto& t : *g_cur) if (t.owner == owner) return true;
    return false;
}

// ================= window =================
// Windows tags mouse messages made from touch or pen with 0xFF515700 in the upper three bytes; the low byte then holds the pointer id and bit 7 (0x80) is set for touch,
// clear for pen. The old test masked with 0xFFFFFF80, which let only PEN through: a finger (e.g. 0xFF5157AD) never counted, so none of the touch handling ever ran.
bool IsTouchMsg() { return ((unsigned long)GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700; }
static LRESULT CALLBACK WProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Wnd* self = (Wnd*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) { self = (Wnd*)((CREATESTRUCTW*)l)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self); if (self) self->hwnd = h; }
    if (!self) return DefWindowProcW(h, m, w, l);
    LRESULT r = self->Proc(m, w, l);
    if (m == WM_NCDESTROY) { SetWindowLongPtrW(h, GWLP_USERDATA, 0); self->hwnd = nullptr; }
    return r;
}
Wnd* Wnd::From(HWND h) { return h ? (Wnd*)GetWindowLongPtrW(h, GWLP_USERDATA) : nullptr; }
static std::vector<Wnd*> g_wnds;
void InvalidateAllWnds() { for (auto* w : g_wnds) if (w->hwnd && IsWindowVisible(w->hwnd)) w->Invalidate(); }
Wnd::Wnd() { root = new View(); root->wnd = this; g_wnds.push_back(this); }
Wnd::~Wnd() {
    std::erase(g_wnds, this);
    AnimCancel(this); if (longTimer) UiKillTimer(longTimer); if (tipTimer) UiKillTimer(tipTimer);
    delete root; root = nullptr;
    if (hwnd) { SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); DestroyWindow(hwnd); hwnd = nullptr; }
}
void Wnd::Destroy() { if (hwnd) { HWND h = hwnd; CancelInput(); DestroyWindow(h); } }
void Wnd::SetRoot(View* r) { delete root; root = r; root->SetWnd(this); Relayout(); }
bool Wnd::Create(DWORD ex, DWORD style, int x, int y, int w, int h, HWND owner) {
    static bool reg = [] {
        WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc = WProc; wc.hInstance = GetModuleHandleW(nullptr); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kWndClass; return RegisterClassExW(&wc) != 0;
    }();
    (void)reg;
    CreateWindowExW(ex, kWndClass, L"DeskBar", style, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd) return false;
    UpdateScale(); return true;
}
void Wnd::UpdateScale() {
    if (!hwnd) return;
    float s = GetDpiForWindow(hwnd) / 96.f; if (s < 0.5f) s = 1;
    if (s != scale) { scale = s; needLayout = true; if (rt) rt->SetDpi(96 * s, 96 * s); }
    gfx.scale = scale;
}
// Glass: the window shows a blurred copy of what is behind it (acrylic), with the translucent theme colours painted over it.
// Needs a window that is NOT layered, which is why SetAlpha does nothing in the glass theme (no fade, the slide still plays).
void ApplyGlass(HWND h) {
    MARGINS mg{-1, -1, -1, -1}; DwmExtendFrameIntoClientArea(h, &mg);
    int dark = 1; DwmSetWindowAttribute(h, 20, &dark, 4); int bd = 3; DwmSetWindowAttribute(h, 38 /*SYSTEMBACKDROP_TYPE: transient acrylic*/, &bd, 4);
}
void Wnd::SetAlpha(BYTE a) {
    if (ThemeIsGlass(CurrentTheme())) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(hwnd, 0, a, LWA_ALPHA);
}
void Wnd::Layout() {
    RECT cr; GetClientRect(hwnd, &cr); client = {cr.right / scale, cr.bottom / scale};
    root->Measure(client); root->Arrange({0, 0, client.w, client.h});
    needLayout = false; if (onLayoutDone) onLayoutDone();
}
void Wnd::PaintNow() {
    RECT cr; GetClientRect(hwnd, &cr); if (cr.right <= 0 || cr.bottom <= 0 || inPaint) return;
    inPaint = true;
    if (!rt) {
        float dpi = 96 * scale;
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, transparent ? D2D1_ALPHA_MODE_PREMULTIPLIED : D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
        if (FAILED(D2DFactory()->CreateHwndRenderTarget(rp, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(cr.right, cr.bottom), D2D1_PRESENT_OPTIONS_NONE), &rt))) { inPaint = false; return; }
        gfx.Reset(rt.Get()); gfx.scale = scale; needLayout = true;
    }
    D2D1_SIZE_U ps = rt->GetPixelSize(); if (ps.width != (UINT)cr.right || ps.height != (UINT)cr.bottom) { rt->Resize(D2D1::SizeU(cr.right, cr.bottom)); needLayout = true; }
    if (needLayout) Layout();
    gfx.alpha = 1; rt->BeginDraw();
    rt->Clear(transparent ? D2D1::ColorF(0, 0.f) : D2D1::ColorF(0, 1.f));
    root->Paint(gfx);
    HRESULT hr = rt->EndDraw(); frames++;
    if (hr == D2DERR_RECREATE_TARGET) { rt.Reset(); gfx.Reset(nullptr); InvalidateRect(hwnd, nullptr, FALSE); }
    inPaint = false;
}
static void Dump(View* v, int d, int maxd) {
    if (d > maxd) return; ws extra; if (auto* l = dynamic_cast<Label*>(v)) extra = L" '" + l->text.substr(0, 30) + L"'";
    Log(L"%*s%S [%.0f,%.0f %.0fx%.0f]%s%s", d * 2, L"", typeid(*v).name() + 7, v->rc.l, v->rc.t, v->rc.w(), v->rc.h(), v->visible ? L"" : L" HIDDEN", extra.c_str());
    for (auto* k : v->kids) Dump(k, d + 1, maxd);
}
bool Wnd::Snapshot(const ws& path) {
    wchar_t e[8]; if (GetEnvironmentVariableW(L"DB3_DUMP", e, 8)) Dump(root, 0, _wtoi(e));   // debug: PNG of this window's client area (never touches other windows)
    RECT r; GetClientRect(hwnd, &r); int w = r.right, h = r.bottom; if (w <= 0 || h <= 0) return false;
    PaintNow();
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC dc = CreateCompatibleDC(nullptr); HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib); bool ok = PrintWindow(hwnd, dc, 3 /*CLIENTONLY|RENDERFULLCONTENT*/) != 0; if (!ok) ok = PrintWindow(hwnd, dc, 2) != 0; Log(L"snapshot printwindow ok=%d", (int)ok); GdiFlush();
    if (ok) { auto im = ImgMake(w, h); memcpy(im->px.data(), bits, (size_t)w * h * 4); for (auto& p : im->px) p |= 0xFF000000; ok = ImgSavePng(*im, path); }
    SelectObject(dc, old); DeleteObject(dib); DeleteDC(dc); return ok;
}
POINT Wnd::ToScreen(float dx, float dy) { POINT p{(LONG)lroundf(dx * scale), (LONG)lroundf(dy * scale)}; ClientToScreen(hwnd, &p); return p; }
RECT Wnd::ScreenRect(const View* v) {
    POINT a = ToScreen(v->rc.l, v->rc.t), b = ToScreen(v->rc.r, v->rc.b); return {a.x, a.y, b.x, b.y};
}
View* Wnd::HitIn(View* v, float x, float y) {
    if (!v->visible) return nullptr;
    x -= v->tx; y -= v->ty;
    bool inside = v->rc.has(x, y);
    if (v->clip && !inside) return nullptr;
    for (size_t i = v->kids.size(); i-- > 0;) if (auto* h = HitIn(v->kids[i], x, y)) return h;
    return inside && (v->hit || v->Clickable()) ? v : nullptr;
}
View* Wnd::HitTest(float x, float y) { return root ? HitIn(root, x, y) : nullptr; }
void Wnd::Forget(View* v) {
    if (hover == v) hover = nullptr; if (press == v) press = nullptr; if (capture == v) capture = nullptr;
    if (focus == v) focus = nullptr; if (panView == v) panView = nullptr;
    if (tipView == v) { tipView = nullptr; if (tipTimer) { UiKillTimer(tipTimer); tipTimer = 0; } }
}
void Wnd::SetFocus(View* v) {
    if (focus == v) return; View* o = focus; focus = v;
    if (o) o->OnFocus(false); if (v) v->OnFocus(true);
}
void Wnd::SetHover(View* h) {
    if (h == hover) return;
    std::vector<View*> oldc, newc;
    for (View* v = hover; v; v = v->parent) oldc.push_back(v);
    for (View* v = h; v; v = v->parent) newc.push_back(v);
    hover = h;
    for (auto* v : oldc) if (std::find(newc.begin(), newc.end(), v) == newc.end()) { v->hovered = false; if (v->onHover) { auto f = v->onHover; f(false); } }
    for (auto* v : newc) if (std::find(oldc.begin(), oldc.end(), v) == oldc.end()) { v->hovered = true; if (v->onHover) { auto f = v->onHover; f(true); } }
    Invalidate();
    TipHide(); if (tipTimer) { UiKillTimer(tipTimer); tipTimer = 0; }
    for (View* v = h; v; v = v->parent) if (!v->tip.empty()) {
        tipView = v; tipTimer = UiTimer(450, [this] { UiKillTimer(tipTimer); tipTimer = 0; if (tipView && hover) TipShow(this, tipView); });
        break;
    }
}
void Wnd::CancelInput() {
    if (longTimer) { UiKillTimer(longTimer); longTimer = 0; }
    if (press) press->pressed = false; press = nullptr; capture = nullptr; panView = nullptr; panMode = false;
    SetHover(nullptr); if (GetCapture() == hwnd) ReleaseCapture();
}
static View* Up(View* v, bool (*pred)(View*)) { for (; v; v = v->parent) if (pred(v)) return v; return nullptr; }

LRESULT Wnd::Proc(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); PaintNow(); EndPaint(hwnd, &ps); return 0; }
    case WM_DISPLAYCHANGE: case WM_DPICHANGED: UpdateScale(); Relayout(); return 0;
    case WM_SIZE: if (rt && w != SIZE_MINIMIZED) { rt->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l))); } needLayout = true; Invalidate(); return 0;
    case WM_MOUSEACTIVATE: return noActivate ? MA_NOACTIVATE : MA_ACTIVATE;
    case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && onDeactivate) { auto f = onDeactivate; f(); } return Def(m, w, l);
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, hover && hover->ibeam ? IDC_IBEAM : IDC_ARROW)); return TRUE; }
        return Def(m, w, l);
    case WM_MOUSELEAVE: tracking = false; if (!press) SetHover(nullptr); return 0;
    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale; if (!press) touchLast = IsTouchMsg(); lastX = x; lastY = y;   // during a press the kind of input is the one that started it: a stray untagged mouse-move must not turn a swipe into a mouse drag
        if (!tracking) { TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); tracking = true; }
        if (panMode && panView) { panView->OnPan(x - panX, y - panY); panX = x; panY = y; return 0; }
        if (press) {
            if (capture) { capture->OnDrag(x, y); return 0; }
            float dx = x - downX, dy = y - downY;
            if (press->OnPressMove(x, y)) { capture = press; if (longTimer) { UiKillTimer(longTimer); longTimer = 0; } return 0; }
            if (touchLast && (std::fabs(dx) > 8 || std::fabs(dy) > 8)) {
                for (View* v = press; v; v = v->parent) if (v->WantsPan(dx, dy)) {
                    panView = v; panMode = true; panX = x; panY = y; v->OnPan(dx, dy);
                    press->pressed = false; press = nullptr; if (longTimer) { UiKillTimer(longTimer); longTimer = 0; } Invalidate(); return 0;
                }
                if (longTimer) { UiKillTimer(longTimer); longTimer = 0; }
            }
            return 0;
        }
        SetHover(HitTest(x, y)); return 0;
    }
    case WM_LBUTTONDOWN: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale; touchLast = IsTouchMsg(); downX = lastX = x; downY = lastY = y;
        View* h = HitTest(x, y); if (touchLast) SetHover(h);
        TipHide(); if (tipTimer) { UiKillTimer(tipTimer); tipTimer = 0; }   // a click dismisses the tooltip (and keeps it from coming back while the pointer stays put)
        if (!h) return 0;
        for (View* v = h; v; v = v->parent) if (v->Focusable()) { SetFocus(v); break; }
        if (!h->enabled) return 0;
        press = h; h->pressed = true; longFired = false; panMode = false; SetCapture(hwnd);
        if (h->OnDown(x, y, touchLast)) capture = h;
        if (!capture && touchLast && (Up(h, [](View* v) { return (bool)v->onLong || (bool)v->onRight; }))) {
            longTimer = UiTimer(500, [this] {
                UiKillTimer(longTimer); longTimer = 0; if (!press) return;
                View* t = Up(press, [](View* v) { return (bool)v->onLong || (bool)v->onRight; }); longFired = true;
                press->pressed = false; if (t) { auto f = t->onLong ? t->onLong : t->onRight; f(); }
            });
        }
        Invalidate(); return 0;
    }
    case WM_LBUTTONUP: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale;
        if (longTimer) { UiKillTimer(longTimer); longTimer = 0; }
        View* p = press; View* cap = capture; bool ff = longFired; press = nullptr; capture = nullptr;   // before ReleaseCapture: it sends WM_CAPTURECHANGED synchronously, which clears these
        if (GetCapture() == hwnd) ReleaseCapture();
        if (panMode) { panMode = false; panView = nullptr; return 0; }
        if (!p) return 0;
        p->pressed = false;
        if (cap) { cap->OnUp(x, y); Invalidate(); return 0; }
        View* h = HitTest(x, y);
        if (!ff && h) {
            View* t = nullptr; for (View* v = h; v; v = v->parent) { if (v == p) { t = p; break; } }
            if (t && p->enabled && p->onClick) { auto f = p->onClick; f(); }
        }
        if (touchLast) SetHover(nullptr);
        Invalidate(); return 0;
    }
    case WM_CAPTURECHANGED: if (press) { press->pressed = false; press = nullptr; capture = nullptr; Invalidate(); } return 0;
    case WM_RBUTTONDOWN: {   // a right-click on a taskbar button closes its hover preview right away, so the menu doesn't open on top of it
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale;
        if (Up(HitTest(x, y), [](View* v) { return (bool)v->onRight; })) { PreviewClose(); TipHide(); }
        return 0;
    }
    case WM_RBUTTONUP: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale;
        View* t = Up(HitTest(x, y), [](View* v) { return (bool)v->onRight; });
        if (t) { auto f = t->onRight; f(); }
        return 0;
    }
    case WM_MBUTTONUP: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale;
        View* t = Up(HitTest(x, y), [](View* v) { return (bool)v->onMiddle; });
        if (t) { auto f = t->onMiddle; f(); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(hwnd, &p);
        float d = GET_WHEEL_DELTA_WPARAM(w) / 120.f;
        for (View* v = HitTest(p.x / scale, p.y / scale); v; v = v->parent) if (v->OnWheel(d, p.x / scale, p.y / scale)) return 0;
        if (root && root->OnWheel(d, 0, 0)) return 0;
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000, shift = GetKeyState(VK_SHIFT) & 0x8000;
        if (focus && focus->OnKey((UINT)w, ctrl, shift)) return 0;
        if (onKeyDown && onKeyDown((UINT)w, ctrl, shift)) return 0;
        return Def(m, w, l);
    }
    case WM_CHAR: if (focus && w >= 0x20) focus->OnChar((wchar_t)w); else if (focus && w == 0) {} return 0;
    }
    return Def(m, w, l);
}

// ================= flyout styling helper =================
void StyleFlyout(HWND h, bool shadow) {
    int pref = 2; DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof pref);
    DWORD none = 0xFFFFFFFE; DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &none, sizeof none);
    if (shadow) { MARGINS mg{0, 0, 0, 1}; DwmExtendFrameIntoClientArea(h, &mg); }
}

// ================= tooltip =================
namespace {
struct TipWnd : Wnd {
    Label *lab = nullptr, *body = nullptr;   // lab: first line (the title), body: the rest, one detail per line
    TipWnd() { noActivate = true; }
};
std::unique_ptr<TipWnd> g_tip;
}
void TipHide() { if (g_tip && g_tip->hwnd) ShowWindow(g_tip->hwnd, SW_HIDE); }
void TipShow(Wnd* w, View* v) {
    if (v->tip.empty() || !w->hwnd) return;
    RECT tr = w->ScreenRect(v); POINT mid{(tr.left + tr.right) / 2, (tr.top + tr.bottom) / 2};
    HMONITOR mon = MonitorFromPoint(mid, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
    if (!g_tip) {
        g_tip = std::make_unique<TipWnd>();
        g_tip->Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_POPUP, mid.x, mid.y, 10, 10);
        StyleFlyout(g_tip->hwnd, true);
        g_tip->root->lay = Lay::V; g_tip->root->bg = SolidB(0x0B3C85); g_tip->root->borderW = 1; g_tip->root->border = Col(K::White, 0x30); g_tip->root->radius = 9; g_tip->root->pad = Thick(13, 9, 13, 10);
        g_tip->lab = MkLabel(g_tip->root, L"", FText(13, 600));
        g_tip->body = MkLabel(g_tip->root, L"", FText(12), Col(K::White, K::Dim)); g_tip->body->margin = Thick(0, 4, 0, 0);
    }
    SetWindowPos(g_tip->hwnd, nullptr, mid.x, mid.y, 10, 10, SWP_NOACTIVATE | SWP_NOZORDER); g_tip->UpdateScale();
    {   // first line is the title; the rest are details, and " · " separators become line breaks
        ws t = v->tip, rest; size_t nl = t.find(L'\n'); if (nl != ws::npos) { rest = t.substr(nl + 1); t.resize(nl); }
        else if (size_t dot = t.find(L" · "); dot != ws::npos && t.size() > 40) { rest = t.substr(dot + 3); t.resize(dot); }   // one long "a · b · c" line
        for (size_t p; (p = rest.find(L" · ")) != ws::npos;) rest.replace(p, 3, L"\n");
        g_tip->lab->wrap = false; g_tip->lab->Set(t); g_tip->body->wrap = false; g_tip->body->Set(rest); g_tip->body->Show(!rest.empty());
    }
    Sz d = g_tip->root->Measure({420, INF}); float sc = g_tip->scale;
    int pw = (int)ceilf(d.w * sc), ph = (int)ceilf(d.h * sc);
    int x = mid.x - pw / 2, y = (tr.bottom + ph + 12 * sc < mi.rcMonitor.bottom && tr.top < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2) ? tr.bottom + (int)(8 * sc) : tr.top - ph - (int)(8 * sc);
    x = std::clamp(x, (int)mi.rcMonitor.left + 4, std::max((int)mi.rcMonitor.left + 4, (int)mi.rcMonitor.right - pw - 4));
    SetWindowPos(g_tip->hwnd, HWND_TOPMOST, x, y, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_tip->Relayout(); g_tip->Invalidate();
}

// ================= menu =================
namespace {
struct MenuWnd : Wnd {
    UINT_PTR poll = 0; uint64_t outSince = 0; RECT anchor{}; std::vector<MenuItem> items; bool downSeen = false;
    MenuWnd() { noActivate = true; }
    ~MenuWnd() { if (poll) UiKillTimer(poll); }
};
std::unique_ptr<MenuWnd> g_menu;
}
bool MenuContains(POINT p) { if (!MenuOpen()) return false; RECT r; GetWindowRect(g_menu->hwnd, &r); return PtInRect(&r, p); }
bool MenuOpen() { return g_menu && g_menu->hwnd && IsWindowVisible(g_menu->hwnd); }
void MenuClose() {
    if (!g_menu) return;
    if (g_menu->poll) { UiKillTimer(g_menu->poll); g_menu->poll = 0; }
    g_menu->CancelInput(); if (g_menu->hwnd) ShowWindow(g_menu->hwnd, SW_HIDE);
}
void MenuShow(std::vector<MenuItem> items, RECT a, bool above, float) {
    TipHide();
    POINT mid{(a.left + a.right) / 2, (a.top + a.bottom) / 2};
    HMONITOR mon = MonitorFromPoint(mid, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
    if (!g_menu) { g_menu = std::make_unique<MenuWnd>(); g_menu->Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_POPUP, mid.x, mid.y, 10, 10); StyleFlyout(g_menu->hwnd, true); }
    MenuWnd& m = *g_menu; MenuClose();
    SetWindowPos(m.hwnd, nullptr, mid.x, mid.y, 10, 10, SWP_NOACTIVATE | SWP_NOZORDER); m.UpdateScale(); float sc = m.scale;
    m.items = std::move(items);
    View* r = m.root; r->Clear(); r->lay = Lay::V; r->bg = SolidB(0x2C2C2C); r->borderW = 1; r->border = Col(0x434343); r->radius = 8; r->pad = Thick(0, 4, 0, 4); r->gap = 0;
    float wmax = 150;
    for (auto& it : m.items) if (!it.sep) wmax = std::max(wmax, MeasureText(it.text, FText(14)).w + 12 + 12 + 16 + 12 + 14);
    wmax = std::min(wmax, 360.f);
    for (size_t i = 0; i < m.items.size(); i++) {
        auto& it = m.items[i];
        if (it.sep) { auto* s = MkBox(r); s->fh = 1; s->margin = Thick(0, 4); s->bg = SolidB(K::White, 0x22); continue; }
        auto* row = MkBox(r, Lay::H); row->fh = 36; row->margin = Thick(4, 1); row->radius = 5; row->pad = Thick(8, 0, 10, 0); row->hit = true; row->enabled = !it.disabled;
        row->bgHover = SolidB(K::White, 0x16); row->bgPress = SolidB(K::White, 0x0E); row->gap = 12;
        auto* gl = MkGlyph(row, it.glyph ? it.glyph : L' ', 16, Col(K::White, it.disabled ? 0x60 : 255)); gl->fw = 20; gl->va = Al::Center;
        auto* tl = MkLabel(row, it.text, FText(14), Col(K::White, it.disabled ? 0x60 : 255)); tl->star = 1; tl->va = Al::Center;
        if (it.checked) { auto* ck = MkGlyph(row, 0xE73E, 14); ck->va = Al::Center; }
        row->onClick = [i, &m] { auto fn = m.items[i].fn; MenuClose(); if (fn) UiPost(fn); };
    }
    Sz d = r->Measure({wmax, INF}); int pw = (int)ceilf(wmax * sc), ph = (int)ceilf(d.h * sc);
    int x = mid.x - pw / 2, y = above ? a.top - ph - (int)(8 * sc) : a.bottom + (int)(8 * sc);
    if (y < mi.rcMonitor.top + 4) y = a.bottom + (int)(8 * sc);
    if (y + ph > mi.rcMonitor.bottom - 4) y = mi.rcMonitor.bottom - ph - 4;
    x = std::clamp(x, (int)mi.rcMonitor.left + 4, std::max((int)mi.rcMonitor.left + 4, (int)mi.rcMonitor.right - pw - 4));
    SetWindowPos(m.hwnd, HWND_TOPMOST, x, y, pw, ph, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    m.anchor = a; m.Relayout(); m.outSince = 0; m.downSeen = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON)) & 0x8000;
    m.poll = UiTimer(30, [] {
        if (!g_menu) return; MenuWnd& w = *g_menu; RECT wr; GetWindowRect(w.hwnd, &wr); POINT p; GetCursorPos(&p);
        RECT ar = w.anchor; InflateRect(&ar, 6, 6); bool inside = PtInRect(&wr, p) || PtInRect(&ar, p); bool down = (GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000;
        if (down && !w.downSeen && !PtInRect(&wr, p)) { MenuClose(); return; }
        w.downSeen = down;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { MenuClose(); return; }
        if (inside) w.outSince = 0; else if (!w.outSince) w.outSince = Now(); else if (Now() - w.outSince > 600) MenuClose();
    });
}

