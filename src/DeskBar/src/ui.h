#pragma once
// Tiny retained-mode UI toolkit on Direct2D/DirectWrite. All sizes are device-independent pixels (dip).
#include "core.h"

constexpr float INF = 1e9f;
struct Rf {
    float l = 0, t = 0, r = 0, b = 0;
    float w() const { return r - l; } float h() const { return b - t; }
    bool has(float x, float y) const { return x >= l && x < r && y >= t && y < b; }
    Rf Inset(float a) const { return {l + a, t + a, r - a, b - a}; }
    Rf Inset(float x, float y) const { return {l + x, t + y, r - x, b - y}; }
    static Rf XYWH(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
    float cx() const { return (l + r) / 2; } float cy() const { return (t + b) / 2; }
    D2D1_RECT_F d() const { return D2D1::RectF(l, t, r, b); }
};
struct Sz { float w = 0, h = 0; };
struct Thick {
    float l = 0, t = 0, r = 0, b = 0;
    Thick() = default; Thick(float a) : l(a), t(a), r(a), b(a) {} Thick(float x, float y) : l(x), t(y), r(x), b(y) {}
    Thick(float l_, float t_, float r_, float b_) : l(l_), t(t_), r(r_), b(b_) {}
    float w() const { return l + r; } float h() const { return t + b; }
};
enum class Al { Stretch, Start, Center, End };
enum class Lay { Overlay, H, V, Wrap, Uniform };
enum class TA { Left, Center, Right };
enum class Ease { Linear, CubicOut, QuintOut, CubicIn, SineInOut };
float EaseF(Ease e, float t);

// ---------- brushes / fonts ----------
struct Stop { float pos; D2D1_COLOR_F c; };
struct Brush {
    enum Kind { None, Solid, Linear, Radial } k = None;
    D2D1_COLOR_F c{};
    float x0 = 0, y0 = 0, x1 = 1, y1 = 1;   // linear: start/end in unit coordinates of the target rect; radial: centre unit coords
    float rx = 0, ry = 0;                    // radial radii (dip)
    Stop st[3]; int ns = 0;
    bool none() const { return k == None; }
};
Brush SolidB(uint32_t rgb, int a = 255);
Brush SolidC(D2D1_COLOR_F c);
Brush LinearB(float x0, float y0, float x1, float y1, std::initializer_list<Stop> s);
Brush RadialB(float cx, float cy, float rx, float ry, std::initializer_list<Stop> s);

struct Font { const wchar_t* fam; float size; int weight; };
Font FText(float size, int weight = 400);   // Segoe UI Variable Text
Font FDisp(float size, int weight = 400);   // Segoe UI Variable Display
Font FIcon(float size);                     // Segoe Fluent Icons / MDL2

// ---------- images ----------
struct Img { int w = 0, h = 0; std::vector<uint32_t> px; uint64_t id = 0; };   // premultiplied BGRA
typedef std::shared_ptr<Img> ImgP;
ImgP ImgFromHBitmap(HBITMAP hbm);
ImgP ImgFromFile(const ws& path);
ImgP ImgFromHIcon(HICON ic, int px);
bool ImgSavePng(const Img& img, const ws& path);
ImgP ImgMake(int w, int h);

// ---------- graphics context ----------
struct Gfx {
    ID2D1RenderTarget* rt = nullptr; float alpha = 1, scale = 1;
    ComPtr<ID2D1SolidColorBrush> sb;
    std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> bmp;
    std::vector<int> clips;                 // 0 = axis aligned, 1 = layer
    void Reset(ID2D1RenderTarget* t);
    D2D1_COLOR_F A(D2D1_COLOR_F c) const { c.a *= alpha; return c; }
    void Fill(const Rf& r, const Brush& b, float radius = 0);
    void FillC(const Rf& r, D2D1_COLOR_F c, float radius = 0);
    void Stroke(const Rf& r, D2D1_COLOR_F c, float radius = 0, float w = 1);   // drawn inside r
    void Circle(float cx, float cy, float rad, D2D1_COLOR_F c);
    void Line(float x0, float y0, float x1, float y1, D2D1_COLOR_F c, float w = 1);
    void Text(const ws& s, const Rf& r, const Font& f, D2D1_COLOR_F c, TA ta = TA::Left, Al va = Al::Center, bool wrap = false);
    void Glyph(wchar_t ch, const Rf& r, float size, D2D1_COLOR_F c);
    void Image(const Img& im, const Rf& r, float opacity = 1);
    void PushClip(const Rf& r, float radius = 0);
    void PopClip();
};
Sz MeasureText(const ws& s, const Font& f, float maxW = INF, bool wrap = false);
float TextCaretX(const ws& s, const Font& f, int pos);                // x of caret before char index
int TextHit(const ws& s, const Font& f, float x);                     // char index nearest to x
void UiGfxInit();                                                      // D2D + DWrite factories, font families (call once, on UI thread)
ID2D1Factory* D2DFactory();

// ---------- views ----------
struct Wnd;
struct View {
    Wnd* wnd = nullptr; View* parent = nullptr; std::vector<View*> kids;
    Rf rc;                                  // arranged rect, absolute window dips
    Sz desired;                             // last measured size, margins included
    float fw = NAN, fh = NAN, maxW = INF, minW = 0, minH = 0;
    float star = 0;                         // share of leftover space in H/V stacks
    Thick margin, pad; Al ha = Al::Stretch, va = Al::Stretch;
    Lay lay = Lay::Overlay; float gap = 0; int cols = 1; float cellH = NAN;
    bool visible = true, enabled = true, hit = false, clip = false, hovered = false, pressed = false;
    float opacity = 1, scale = 1, tx = 0, ty = 0;
    float radius = 0, borderW = 0; D2D1_COLOR_F border{};
    Brush bg, bgHover, bgPress, borderHover;
    ws tip;
    std::function<void()> onClick, onRight, onLong, onMiddle;
    std::function<void(bool)> onHover;
    bool ibeam = false;

    virtual ~View();
    template <class T> T* Add(T* k) { AddView(k); return k; }
    void AddView(View* k); void Remove(View* k, bool del = true); void Clear();
    void SetWnd(Wnd* w);
    Sz Measure(Sz avail);                   // returns desired size including margin
    void Arrange(Rf slot);                  // slot includes margin
    void Paint(Gfx& g);
    void Invalidate(); void Relayout();
    void Show(bool v) { if (visible != v) { visible = v; Relayout(); } }
    bool Clickable() const { return onClick || onRight || onLong; }
    virtual Sz MeasureContent(Sz avail);
    virtual void ArrangeContent(Rf inner);
    virtual void PaintContent(Gfx&) {}
    virtual void PaintOver(Gfx&) {}                                              // after children, inside the clip
    virtual bool OnDown(float x, float y, bool touch) { return false; }          // true = capture drag
    virtual bool OnPressMove(float x, float y) { return false; }                 // pointer moved while pressed (no capture yet); true = start capturing
    virtual void OnDrag(float x, float y) {}
    virtual void OnUp(float x, float y) {}
    virtual bool OnWheel(float delta, float x, float y) { return false; }
    virtual bool OnKey(UINT vk, bool ctrl, bool shift) { return false; }
    virtual void OnChar(wchar_t c) {}
    virtual bool WantsPan(float dx, float dy) { return false; }
    virtual void OnPan(float dx, float dy) {}
    virtual void OnFocus(bool) {}
    virtual bool Focusable() const { return false; }
};

struct Label : View {
    ws text; Font font; D2D1_COLOR_F col; TA ta = TA::Left; bool wrap = false; Al tva = Al::Center;
    Label(const ws& t = L"", Font f = FText(12), D2D1_COLOR_F c = Col(K::White)) : text(t), font(f), col(c) {}
    void Set(const ws& t) { if (t != text) { text = t; Relayout(); } }
    Sz MeasureContent(Sz avail) override; void PaintContent(Gfx& g) override;
};
struct Glyph : View {
    wchar_t ch; float size; D2D1_COLOR_F col;
    Glyph(wchar_t c, float s, D2D1_COLOR_F cl = Col(K::White)) : ch(c), size(s), col(cl) {}
    void Set(wchar_t c) { if (c != ch) { ch = c; Invalidate(); } }
    Sz MeasureContent(Sz) override { return {size, size}; } void PaintContent(Gfx& g) override;
};
struct Pic : View {
    ImgP img; std::function<ImgP(int px)> provider; float opacityImg = 1;
    Sz MeasureContent(Sz) override { return {0, 0}; } void PaintContent(Gfx& g) override;
};
struct Slider : View {
    float value = 0; bool dragging = false; std::function<void(float, bool)> onChange;   // (value, final)
    Slider() { hit = true; fh = 32; }
    void Set(float v) { if (!dragging && v != value) { value = v; Invalidate(); } }
    Sz MeasureContent(Sz) override { return {60, 32}; } void PaintContent(Gfx& g) override;
    bool OnDown(float x, float y, bool touch) override; void OnDrag(float x, float y) override; void OnUp(float x, float y) override;
    bool OnKey(UINT vk, bool ctrl, bool shift) override;
    bool Focusable() const override { return false; }
    void From(float x, bool fin);
};
struct Toggle : View {
    bool on = false; std::function<void(bool)> onChange; float knob = 0;
    Toggle();
    void Set(bool v);
    Sz MeasureContent(Sz) override { return {42, 22}; } void PaintContent(Gfx& g) override;
};
struct TextBox : View {
    ws text, placeholder; bool password = false; Font font = FText(14.5f); D2D1_COLOR_F col = Col(K::White), phCol = Col(K::White, K::Faint);
    int caret = 0, anchor = 0; float scrollX = 0; bool blink = true; UINT_PTR blinkTimer = 0;
    std::function<void(const ws&)> onChange; std::function<void()> onEnter; std::function<bool(UINT)> onKeyHook;
    TextBox() { hit = true; ibeam = true; clip = true; }
    ~TextBox() override;
    void SetText(const ws& t, bool notify = true);
    void SelectAll() { anchor = 0; caret = (int)text.size(); Invalidate(); }
    void Insert(const ws& s);
    ws Shown() const { return password ? ws(text.size(), (wchar_t)0x25CF) : text; }
    Sz MeasureContent(Sz) override { Sz m = MeasureText(L"Ag", font); return {40, m.h}; } void PaintContent(Gfx& g) override;
    bool OnDown(float x, float y, bool touch) override; void OnDrag(float x, float y) override;
    bool OnKey(UINT vk, bool ctrl, bool shift) override; void OnChar(wchar_t c) override;
    void OnFocus(bool f) override; bool Focusable() const override { return true; }
    void Changed();
    void EnsureVisible();
};
int WheelStep(float d);   // -1 / 0 / +1: one step per wheel notch or touchpad swipe (see ui_view.cpp)
struct Scroll : View {
    View* content = nullptr; float off = 0, target = 0, contentH = 0; bool panning = false;
    Scroll() { clip = true; hit = true; }
    float MaxOff() const { return std::max(0.f, contentH - rc.h() + pad.h()); }
    void ScrollTo(float y, bool animate = true);
    Sz MeasureContent(Sz avail) override; void ArrangeContent(Rf inner) override; void PaintContent(Gfx& g) override; void PaintOver(Gfx& g) override;
    bool OnWheel(float d, float x, float y) override; bool WantsPan(float dx, float dy) override; void OnPan(float dx, float dy) override;
};

// helpers to build views tersely
Label* MkLabel(View* parent, const ws& t, Font f, D2D1_COLOR_F c = Col(K::White));
Glyph* MkGlyph(View* parent, wchar_t ch, float size, D2D1_COLOR_F c = Col(K::White));
View* MkBox(View* parent, Lay l = Lay::Overlay);

// ---------- animation ----------
void Anim(View* v, float* p, float to, int ms, Ease e = Ease::CubicOut, int delay = 0);
void AnimFn(Wnd* w, int ms, Ease e, std::function<void(float)> fn, std::function<void()> done = {});
void AnimCancel(void* owner);
bool AnimActive(void* owner);

// ---------- window ----------
struct Wnd {
    HWND hwnd = nullptr; View* root = nullptr; float scale = 1; Gfx gfx;
    ComPtr<ID2D1HwndRenderTarget> rt; bool transparent = false, needLayout = true, noActivate = false, inPaint = false;
    Sz client;                              // dips
    View *hover = nullptr, *press = nullptr, *capture = nullptr, *focus = nullptr;
    bool touchLast = false; float downX = 0, downY = 0; UINT_PTR longTimer = 0; bool longFired = false, panMode = false; View* panView = nullptr;
    float lastX = 0, lastY = 0; int frames = 0;
    UINT_PTR tipTimer = 0; View* tipView = nullptr; bool tracking = false; float panX = 0, panY = 0;
    std::function<bool(UINT vk, bool ctrl, bool shift)> onKeyDown;      // after the focused view declined
    std::function<void()> onDeactivate;
    std::function<void()> onLayoutDone;

    Wnd();
    virtual ~Wnd();
    bool Create(DWORD ex, DWORD style, int x, int y, int w, int h, HWND owner = nullptr);   // pixels
    void Destroy();
    virtual LRESULT Proc(UINT m, WPARAM w, LPARAM l);
    LRESULT Def(UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(hwnd, m, w, l); }
    void Invalidate() { if (hwnd) InvalidateRect(hwnd, nullptr, FALSE); }
    void Relayout() { needLayout = true; Invalidate(); }
    void Layout(); void PaintNow();
    void UpdateScale();
    View* HitTest(float x, float y); View* HitIn(View* v, float x, float y);
    void SetFocus(View* v); void SetHover(View* h); void Forget(View* v);
    void SetRoot(View* r);
    POINT ToScreen(float dx, float dy);     // dip point -> screen px
    RECT ScreenRect(const View* v);         // view rect -> screen px
    float Dip(int px) const { return px / scale; }
    void Show(int cmd = SW_SHOWNA) { ShowWindow(hwnd, cmd); }
    bool Visible() const { return hwnd && IsWindowVisible(hwnd); }
    void SetAlpha(BYTE a);                  // layered window opacity (no-op in the glass theme)
    bool Snapshot(const ws& path);          // debug: save the client area as PNG
    void CancelInput();
    static Wnd* From(HWND h);
};
// Default window class registered once; Wnd::Create uses it.
extern const wchar_t* kWndClass;
bool IsTouchMsg();                          // is the current mouse message synthesised from touch/pen?

void InvalidateAllWnds();
void ClipboardSetText(const ws& s);
void StyleFlyout(HWND h, bool shadow);     // rounded corners, no light border, drop shadow
void ApplyGlass(HWND h);                   // acrylic backdrop for a window created with transparent = true and without WS_EX_LAYERED
// ---------- tooltips / menus ----------
void TipShow(Wnd* w, View* v);
void TipHide();
struct MenuItem {
    ws text; wchar_t glyph = 0; std::function<void()> fn; bool sep = false, disabled = false, checked = false;
};
// Pops a menu next to a screen rect (above it if there's room and above=true). Closes on click away, Esc or when the mouse leaves for ~0.6 s.
void MenuShow(std::vector<MenuItem> items, RECT anchorPx, bool above, float scale);
void MenuClose();
bool MenuOpen();

