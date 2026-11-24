#include "pch.h"
#include "app.h"
#include "flyout.h"
#include <uiautomation.h>
#pragma comment(lib, "uiautomationcore.lib")

// Accessibility for the whole UI toolkit, in one place:
//  - the keyboard: Tab / Shift+Tab and the arrow keys walk everything you can click, Enter or Space clicks it, the menu key or Shift+F10 right-clicks it,
//    with a focus ring while the keyboard is in use; open menus take Up / Down / Enter / Esc; sliders move with the arrows, Page Up / Down, Home and End
//  - screen readers: every window answers UI Automation (WM_GETOBJECT) with its views as buttons, toggle switches, sliders, text boxes and text,
//    named from their text, their tooltip or their icon; providers are made only when a screen reader asks, so nothing changes for everyone else
//  - high contrast: Col() paints with Windows' high contrast colours while it is on, and DeskBar restarts to wear them when it is switched
//  - text size: tooltips and menus follow Settings > Accessibility > Text size (TextScale)

// ================= high contrast and text size =================
static int g_hc = -1; static float g_ts = 0;
bool HighContrastOn() {
    if (g_hc < 0) { HIGHCONTRASTW h{sizeof h}; g_hc = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof h, &h, 0) && (h.dwFlags & HCF_HIGHCONTRASTON) ? 1 : 0; }
    return g_hc == 1;
}
float TextScale() {
    if (g_ts <= 0) {
        DWORD v = 100, n = sizeof v;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor", RRF_RT_REG_DWORD, nullptr, &v, &n) != ERROR_SUCCESS) v = 100;
        g_ts = std::clamp((float)v, 100.f, 225.f) / 100;
    }
    return g_ts;
}
static D2D1_COLOR_F SysCol(int idx, float a) { COLORREF c = GetSysColor(idx); return D2D1_COLOR_F{GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a}; }
// The UI's colours are written as DeskBar's blues, whites and greys; in high contrast each kind becomes the system colour for that job:
// light greys and whites (text, icons, outlines) -> window text, light colours (accents, status colours) -> highlight, black (text on an accent) -> highlight text,
// dark colours (every background) -> window. Faint fills (hover, cards) keep their faintness; anything meant to be read is made solid.
D2D1_COLOR_F HighContrastCol(uint32_t rgb, int a) {
    float r = ((rgb >> 16) & 255) / 255.f, g = ((rgb >> 8) & 255) / 255.f, b = (rgb & 255) / 255.f, al = a / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), l = (mx + mn) / 2, s = mx == mn ? 0 : (l > 0.5f ? (mx - mn) / (2 - mx - mn) : (mx - mn) / (mx + mn));
    float solid = al >= 0.35f ? 1.f : al;
    if (rgb == 0x000000) return SysCol(COLOR_HIGHLIGHTTEXT, solid);
    if (l >= 0.5f) return SysCol(s < 0.15f ? COLOR_WINDOWTEXT : COLOR_HIGHLIGHT, solid);
    return SysCol(COLOR_WINDOW, al >= 0.2f ? 1.f : al);
}
void AccSettingChange(WPARAM wp, LPARAM) {
    // every top-level window hears each change: only the first one that sees high contrast flip acts on it
    int hcWas = g_hc; g_hc = -1; g_ts = 0;
    if (hcWas >= 0 && HighContrastOn() != (hcWas == 1)) {
        Log(L"high contrast turned %s: restarting to wear its colours", HighContrastOn() ? L"on" : L"off");
        SettingsReopenAfterRestart(); AppQuit(true);   // every brush is made once from the theme, as a colour change does: a restart repaints all of it
    }
    (void)wp;
}

// ================= the view tree as a screen reader and the keyboard see it =================
static Rf VisualRect(const View* v) {   // rc moved by every tx / ty on the way up (a scrolled list, a page sliding in), the way it is painted and hit
    float dx = 0, dy = 0; for (const View* p = v; p; p = p->parent) { dx += p->tx; dy += p->ty; }
    return {v->rc.l + dx, v->rc.t + dy, v->rc.r + dx, v->rc.b + dy};
}
static bool Shown(const View* v) { for (; v; v = v->parent) if (!v->visible || v->opacity < 0.05f) return false; return true; }
static bool OffSideways(const View* v) {   // a page slid out to the side is still "visible": leave it out
    if (!v->wnd) return true; Rf r = VisualRect(v); if (r.w() <= 0) return false;
    return r.r <= 0 || r.l >= v->wnd->client.w;
}
static bool IsTextBox(const View* v) { return dynamic_cast<const TextBox*>(v) != nullptr; }
static bool TabStop(View* v) {
    return v->enabled && (v->Clickable() || v->Focusable()) && v->rc.w() > 1 && v->rc.h() > 1;
}
static void CollectStops(View* v, std::vector<View*>& out) {
    if (!v->visible || v->opacity < 0.05f || OffSideways(v)) return;
    if (TabStop(v)) out.push_back(v);
    for (auto* k : v->kids) CollectStops(k, out);
}
static Scroll* ScrollAbove(View* v) { for (View* p = v->parent; p; p = p->parent) if (auto* s = dynamic_cast<Scroll*>(p)) return s; return nullptr; }
static void ScrollIntoView(View* v) {
    Scroll* s = ScrollAbove(v); if (!s || !s->content) return;
    Rf r = VisualRect(v), sr = VisualRect(s); float cur = AnimActive(s->content) ? s->target : -s->content->ty;
    if (r.t < sr.t + s->pad.t) s->ScrollTo(cur - (sr.t + s->pad.t - r.t) - 8);
    else if (r.b > sr.b - s->pad.b) s->ScrollTo(cur + (r.b - (sr.b - s->pad.b)) + 8);
}
static void FocusByKeyboard(Wnd* w, View* v) {
    w->kbCue = true; w->SetFocus(v); ScrollIntoView(v); TipHide(); w->Invalidate();
}
static bool MoveFocus(Wnd* w, int dir) {
    std::vector<View*> st; if (w->root) CollectStops(w->root, st);
    if (st.empty()) return false;
    int n = (int)st.size(), i = (int)(std::find(st.begin(), st.end(), w->focus) - st.begin());
    i = i >= n ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    FocusByKeyboard(w, st[i]); return true;
}
static bool Activate(View* v, bool right) {
    if (!v || !v->enabled) return false;
    auto f = right ? (v->onRight ? v->onRight : v->onLong) : (v->onClick ? v->onClick : nullptr);
    if (!f) return false;
    f(); return true;
}
bool AccKeyDown(Wnd* w, UINT vk, bool ctrl, bool shift, bool beforeWindow) {
    View* f = w->focus; bool alive = f && Shown(f);
    if (beforeWindow) {   // the focused button's own keys, before the window's shortcuts can take Enter or Space for themselves
        if (!alive || IsTextBox(f) || !w->kbCue) return false;
        if ((vk == VK_RETURN || vk == VK_SPACE) && !ctrl && f->onClick) return Activate(f, false);
        if ((vk == VK_APPS || (vk == VK_F10 && shift)) && (f->onRight || f->onLong)) return Activate(f, true);
        return false;
    }
    if (vk == VK_TAB && !ctrl) return MoveFocus(w, shift ? -1 : 1);
    if (ctrl || shift) return false;
    if (!alive || IsTextBox(f)) return false;   // arrows in a text box are the text box's; with nothing focused they stay the window's
    if (vk == VK_DOWN || vk == VK_RIGHT) return MoveFocus(w, 1);
    if (vk == VK_UP || vk == VK_LEFT) return MoveFocus(w, -1);
    if (vk == VK_HOME || vk == VK_END) {
        std::vector<View*> st; CollectStops(w->root, st); if (st.empty()) return false;
        FocusByKeyboard(w, vk == VK_HOME ? st.front() : st.back()); return true;
    }
    return false;
}
bool AccMenuKey(UINT vk) {
    Wnd* m = MenuWindow(); if (!m || !MenuOpen()) return false;
    switch (vk) {
    case VK_DOWN: case VK_UP: case VK_TAB: return MoveFocus(m, vk == VK_UP ? -1 : 1);
    case VK_HOME: case VK_END: { std::vector<View*> st; CollectStops(m->root, st); if (st.empty()) return true; FocusByKeyboard(m, vk == VK_HOME ? st.front() : st.back()); return true; }
    case VK_RETURN: case VK_SPACE: if (m->focus && Shown(m->focus)) Activate(m->focus, false); return true;
    case VK_ESCAPE: case VK_LEFT: MenuClose(); return true;
    }
    return false;
}
bool AccSliderKey(Slider* s, UINT vk, bool ctrl) {
    if (!s->enabled) return false;
    float v = s->value, step = ctrl ? 10.f : 2.f;
    switch (vk) {
    case VK_RIGHT: case VK_UP: v += step; break;
    case VK_LEFT: case VK_DOWN: v -= step; break;
    case VK_PRIOR: v += 10; break;
    case VK_NEXT: v -= 10; break;
    case VK_HOME: v = 0; break;
    case VK_END: v = 100; break;
    default: return false;
    }
    v = std::clamp(v, 0.f, 100.f); if (v == s->value) return true;
    s->value = v; s->Invalidate(); if (s->onChange) s->onChange(v, true); AccValueChanged(s); return true;
}
void AccPaintFocus(Wnd* w) {
    View* f = w->focus; if (!w->kbCue || !f || !Shown(f) || f == w->root) return;
    Rf r = VisualRect(f).Inset(-2); float rad = f->radius > 0 ? f->radius + 2 : 4;
    Gfx& g = w->gfx; float oa = g.alpha; g.alpha = 1;
    g.Stroke(r, Col(K::White), rad, 2);   // white on the dark themes, near black on White, the window text colour in high contrast
    g.Stroke(r.Inset(2), HighContrastOn() ? Col(0x000000) : D2D1::ColorF(0, 0.55f), std::max(0.f, rad - 2), 1);   // a dark inner line keeps it visible on a light accent
    g.alpha = oa;
}

// ---- names ----
static ws FirstLine(const ws& t) {   // the tooltip's title, as TipShow shows it
    size_t nl = t.find(L'\n'); if (nl != ws::npos) return t.substr(0, nl);
    if (size_t dot = t.find(L" · "); dot != ws::npos && t.size() > 40) return t.substr(0, dot);
    return t;
}
static const wchar_t* GlyphName(wchar_t c) {   // Segoe Fluent Icons / MDL2: the icon-only buttons
    switch (c) {
    case 0xE700: return L"Menu"; case 0xE710: return L"Add"; case 0xE711: return L"Close"; case 0xE712: return L"More options"; case 0xE713: return L"Settings";
    case 0xE721: return L"Search"; case 0xE72A: return L"Forward"; case 0xE72B: return L"Back"; case 0xE72C: return L"Refresh"; case 0xE74A: return L"Up";
    case 0xE74D: return L"Delete"; case 0xE74F: return L"Mute"; case 0xE767: return L"Volume"; case 0xE768: return L"Play"; case 0xE769: return L"Pause";
    case 0xE892: return L"Previous"; case 0xE893: return L"Next"; case 0xE7E8: return L"Power"; case 0xE701: return L"Wi-Fi"; case 0xE702: return L"Bluetooth";
    case 0xE706: return L"Brightness"; case 0xE8BB: return L"Close"; case 0xE921: return L"Minimise"; case 0xE922: return L"Maximise"; case 0xE923: return L"Restore";
    case 0xE946: return L"Information"; case 0xE8B7: return L"Folder"; case 0xE70D: return L"Expand"; case 0xE70E: return L"Collapse"; case 0xE76B: return L"Previous";
    case 0xE76C: return L"Next"; case 0xE73E: return L"Done"; case 0xE718: return L"Pin"; case 0xE77A: return L"Unpin"; case 0xEA8F: return L"Notifications";
    case 0xE7F4: return L"Display"; case 0xE8A7: return L"Open in new window"; case 0xE722: return L"Camera"; case 0xE720: return L"Microphone"; case 0xE7E7: return L"Notifications";
    case 0xE708: return L"Focus"; case 0xE7EF: return L"Project"; case 0xE945: return L"Airplane mode"; case 0xE83F: return L"Battery saver"; case 0xEBC6: return L"Mobile hotspot";
    case 0xEBA0: case 0xEBAA: case 0xEBB5: return L"Battery"; case 0xE77B: return L"Account"; case 0xE7E0: return L"Keyboard"; case 0xE8EC: return L"Tag"; case 0xE8E5: return L"Open file"; case 0xE838: return L"Open folder";
    }
    return nullptr;
}
static void Texts(const View* v, ws& out, int& n) {
    for (auto* k : v->kids) {
        if (n >= 3 || !k->visible) continue;
        if (auto* l = dynamic_cast<const Label*>(k)) { if (!l->text.empty()) { if (!out.empty()) out += L", "; out += l->text; n++; } continue; }
        if (k->Clickable()) continue;   // a button inside has a name of its own
        Texts(k, out, n);
    }
}
static wchar_t FirstGlyph(const View* v) {
    for (auto* k : v->kids) { if (!k->visible || k->Clickable()) continue; if (auto* g = dynamic_cast<const Glyph*>(k)) if (g->ch > L' ') return g->ch; if (wchar_t c = FirstGlyph(k)) return c; }
    return 0;
}
ws AccNameOf(View* v) {
    if (!v->accName.empty()) return v->accName;
    if (auto* l = dynamic_cast<Label*>(v)) return l->text;
    if (auto* t = dynamic_cast<TextBox*>(v)) if (!t->placeholder.empty()) return t->placeholder;
    ws s; int n = 0; Texts(v, s, n); if (!s.empty()) return s;   // the visible text first: what a sighted user reads is what they say
    if (!v->tip.empty()) return FirstLine(v->tip);
    if (auto* g = dynamic_cast<Glyph*>(v)) if (auto* gn = GlyphName(g->ch)) return gn;
    if (auto* gn = GlyphName(FirstGlyph(v))) return gn;
    if (dynamic_cast<Toggle*>(v) || dynamic_cast<Slider*>(v) || IsTextBox(v)) {   // a switch or slider is named by the text in its row
        for (View* p = v->parent; p; p = p->parent) { ws t; int m = 0; Texts(p, t, m); if (!t.empty()) { size_t c = t.find(L", "); return c == ws::npos ? t : t.substr(0, c); } if (p->Clickable()) break; }
    }
    return L"";
}

// ================= UI Automation =================
namespace {
enum class AK { Root, Button, MenuItem, Toggle, Slider, Edit, Text, Other };
bool IsMenuWnd(const Wnd* w) { return w && w == MenuWindow(); }
AK KindOf(View* v) {
    if (v->wnd && v->wnd->root == v) return AK::Root;
    if (IsTextBox(v)) return AK::Edit;
    if (dynamic_cast<Slider*>(v)) return AK::Slider;
    if (dynamic_cast<Toggle*>(v)) return AK::Toggle;
    if (v->Clickable()) return IsMenuWnd(v->wnd) ? AK::MenuItem : AK::Button;
    if (dynamic_cast<Label*>(v)) return AK::Text;
    return AK::Other;
}
bool Exposed(View* v, bool inButton) {
    if (!v->visible) return false;
    AK k = KindOf(v);
    if (k == AK::Text) return !inButton && !static_cast<Label*>(v)->text.empty();   // a button's own text is its name, not a child
    return k != AK::Other;
}
void KidsOf(View* v, bool inButton, std::vector<View*>& out) {
    for (auto* k : v->kids) {
        if (!k->visible || k->opacity < 0.05f || OffSideways(k)) continue;
        if (Exposed(k, inButton)) out.push_back(k); else KidsOf(k, inButton, out);
    }
}
std::vector<View*> Children(View* v) { AK k = KindOf(v); std::vector<View*> out; KidsOf(v, k == AK::Button || k == AK::MenuItem || k == AK::Toggle, out); return out; }
View* ParentOf(View* v) { for (View* p = v->parent; p; p = p->parent) if (KindOf(p) == AK::Root || Exposed(p, false)) return p; return nullptr; }

int g_nextId = 1;
struct Node : IRawElementProviderSimple, IRawElementProviderFragment, IRawElementProviderFragmentRoot, IInvokeProvider, IToggleProvider, IRangeValueProvider, IValueProvider {
    LONG refs = 1; View* v; int id = g_nextId++;
    explicit Node(View* view) : v(view) {}
    Wnd* W() const { return v && v->wnd && v->wnd->hwnd ? v->wnd : nullptr; }
    bool Gone() const { return !W(); }
    // IUnknown
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot) && v && KindOf(v) == AK::Root) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (iid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (iid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    // IRawElementProviderSimple
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override { if (!o) return E_POINTER; *o = ProviderOptions_ServerSideProvider; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID p, IUnknown** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        AK k = KindOf(v); IUnknown* u = nullptr;
        if (p == UIA_InvokePatternId && (k == AK::Button || k == AK::MenuItem)) u = static_cast<IInvokeProvider*>(this);
        else if (p == UIA_TogglePatternId && k == AK::Toggle) u = static_cast<IToggleProvider*>(this);
        else if (p == UIA_RangeValuePatternId && k == AK::Slider) u = static_cast<IRangeValueProvider*>(this);
        else if (p == UIA_ValuePatternId && k == AK::Edit) u = static_cast<IValueProvider*>(this);
        if (u) { u->AddRef(); *out = u; }
        return S_OK;
    }
    static void Str(VARIANT* r, const ws& s) { r->vt = VT_BSTR; r->bstrVal = SysAllocString(s.c_str()); }
    static void I4(VARIANT* r, int i) { r->vt = VT_I4; r->lVal = i; }
    static void B(VARIANT* r, bool b) { r->vt = VT_BOOL; r->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID p, VARIANT* r) override {
        if (!r) return E_POINTER; VariantInit(r); if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        AK k = KindOf(v); Wnd* w = v->wnd;
        switch (p) {
        case UIA_ControlTypePropertyId: {
            int t = k == AK::Root ? (IsMenuWnd(w) ? UIA_MenuControlTypeId : UIA_PaneControlTypeId) : k == AK::Button || k == AK::Toggle ? UIA_ButtonControlTypeId : k == AK::MenuItem ? UIA_MenuItemControlTypeId
                  : k == AK::Slider ? UIA_SliderControlTypeId : k == AK::Edit ? UIA_EditControlTypeId : k == AK::Text ? UIA_TextControlTypeId : UIA_GroupControlTypeId;
            I4(r, t); break;
        }
        case UIA_LocalizedControlTypePropertyId: if (k == AK::Toggle) Str(r, L"toggle switch"); break;
        case UIA_NamePropertyId: if (k != AK::Root || !v->accName.empty()) { ws n = AccNameOf(v); if (!n.empty()) Str(r, n); } break;
        case UIA_HelpTextPropertyId: if (!v->tip.empty() && v->tip != AccNameOf(v)) { ws t = v->tip; for (auto& c : t) if (c == L'\n') c = L' '; Str(r, t); } break;
        case UIA_IsKeyboardFocusablePropertyId: B(r, k != AK::Root && TabStop(v)); break;
        case UIA_HasKeyboardFocusPropertyId: B(r, w->focus == v && GetForegroundWindow() == w->hwnd); break;
        case UIA_IsEnabledPropertyId: B(r, v->enabled); break;
        case UIA_IsPasswordPropertyId: if (k == AK::Edit) B(r, static_cast<TextBox*>(v)->password); break;
        case UIA_IsOffscreenPropertyId: if (k != AK::Root) { Rf vr = VisualRect(v); B(r, !Shown(v) || vr.b <= 0 || vr.t >= w->client.h || vr.r <= 0 || vr.l >= w->client.w); } break;
        case UIA_FrameworkIdPropertyId: Str(r, L"DeskBar"); break;
        case UIA_ClassNamePropertyId: if (k != AK::Root) { const char* n = typeid(*v).name(); if (!strncmp(n, "struct ", 7)) n += 7; ws s; for (; *n; n++) s += (wchar_t)*n; Str(r, s); } break;
        case UIA_AutomationIdPropertyId: if (k == AK::Root && IsMenuWnd(w)) Str(r, L"DeskBarMenu"); break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (KindOf(v) == AK::Root) return UiaHostProviderFromHwnd(v->wnd->hwnd, out);
        return S_OK;
    }
    // IRawElementProviderFragment
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection d, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (KindOf(v) == AK::Root) return S_OK;   // the window's own id, from the host
        int ids[2] = {UiaAppendRuntimeId, id}; SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, 2); if (!sa) return E_OUTOFMEMORY;
        for (LONG i = 0; i < 2; i++) SafeArrayPutElement(sa, &i, &ids[i]);
        *out = sa; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER; *out = {}; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (KindOf(v) == AK::Root || !Shown(v)) return S_OK;
        Rf vr = VisualRect(v); POINT a = v->wnd->ToScreen(vr.l, vr.t), b = v->wnd->ToScreen(vr.r, vr.b);
        *out = {(double)a.x, (double)a.y, (double)(b.x - a.x), (double)(b.y - a.y)}; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { if (!out) return E_POINTER; *out = nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (KindOf(v) != AK::Root) FocusByKeyboard(v->wnd, v);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;
    // IRawElementProviderFragmentRoot
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;
    // IInvokeProvider
    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; if (!v->enabled) return UIA_E_ELEMENTNOTENABLED;
        AddRef(); Node* self = this;
        UiPost([self] { if (!self->Gone()) Activate(self->v, !self->v->onClick); self->Release(); });   // after the call returns, as a click would: the click may rebuild the page
        return S_OK;
    }
    // IToggleProvider
    HRESULT STDMETHODCALLTYPE Toggle() override {
        if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; if (!v->enabled) return UIA_E_ELEMENTNOTENABLED;
        auto f = v->onClick; if (f) f(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* s) override {
        if (!s) return E_POINTER; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
        auto* t = dynamic_cast<::Toggle*>(v); *s = t && t->on ? ToggleState_On : ToggleState_Off; return S_OK;
    }
    // IRangeValueProvider
    HRESULT STDMETHODCALLTYPE SetValue(double val) override {
        if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; auto* s = dynamic_cast<Slider*>(v); if (!s) return E_FAIL; if (!s->enabled) return UIA_E_ELEMENTNOTENABLED;
        if (val < 0 || val > 100) return E_INVALIDARG;
        s->value = (float)val; s->Invalidate(); if (s->onChange) s->onChange(s->value, true); AccValueChanged(s); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override { if (!out) return E_POINTER; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; auto* s = dynamic_cast<Slider*>(v); *out = s ? s->value : 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override { if (!out) return E_POINTER; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; *out = !v->enabled; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { if (!out) return E_POINTER; *out = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { if (!out) return E_POINTER; *out = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override { if (!out) return E_POINTER; *out = 10; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override { if (!out) return E_POINTER; *out = 2; return S_OK; }
    // IValueProvider (text boxes)
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR val) override {
        if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; auto* t = dynamic_cast<TextBox*>(v); if (!t) return E_FAIL; if (!t->enabled) return UIA_E_ELEMENTNOTENABLED;
        t->SetText(val ? val : L"", true); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE; auto* t = dynamic_cast<TextBox*>(v);
        *out = SysAllocString(t && !t->password ? t->text.c_str() : L""); return S_OK;
    }
};
Node* NodeFor(View* v) {
    if (!v) return nullptr;
    if (!v->acc) v->acc = new Node(v);   // the view holds one reference until it is deleted (AccViewGone)
    return static_cast<Node*>(v->acc);
}
template <class I> HRESULT Give(View* v, I** out) { Node* n = NodeFor(v); if (!n) { *out = nullptr; return S_OK; } n->AddRef(); *out = static_cast<I*>(n); return S_OK; }

HRESULT Node::Navigate(NavigateDirection d, IRawElementProviderFragment** out) {
    if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
    View* t = nullptr;
    if (d == NavigateDirection_Parent) { if (KindOf(v) != AK::Root) t = ParentOf(v); }
    else if (d == NavigateDirection_FirstChild || d == NavigateDirection_LastChild) { auto c = Children(v); if (!c.empty()) t = d == NavigateDirection_FirstChild ? c.front() : c.back(); }
    else if (KindOf(v) != AK::Root) {
        if (View* p = ParentOf(v)) {
            auto c = Children(p); auto it = std::find(c.begin(), c.end(), v);
            if (it != c.end()) { if (d == NavigateDirection_NextSibling && it + 1 != c.end()) t = *(it + 1); else if (d == NavigateDirection_PreviousSibling && it != c.begin()) t = *(it - 1); }
        }
    }
    return Give(t, out);
}
HRESULT Node::get_FragmentRoot(IRawElementProviderFragmentRoot** out) {
    if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
    return Give(v->wnd->root, out);
}
HRESULT Node::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) {
    if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
    Wnd* w = v->wnd; POINT p{(LONG)x, (LONG)y}; ScreenToClient(w->hwnd, &p);
    View* h = w->HitTest(p.x / w->scale, p.y / w->scale);
    while (h && KindOf(h) != AK::Root && !Exposed(h, false)) h = h->parent;
    if (!h || KindOf(h) == AK::Root) return S_OK;
    return Give(h, out);
}
HRESULT Node::GetFocus(IRawElementProviderFragment** out) {
    if (!out) return E_POINTER; *out = nullptr; if (Gone()) return UIA_E_ELEMENTNOTAVAILABLE;
    View* f = v->wnd->focus; if (!f || f == v->wnd->root || !Shown(f)) return S_OK;
    return Give(f, out);
}
}   // namespace

bool AccGetObject(Wnd* w, WPARAM wp, LPARAM lp, LRESULT& out) {
    if ((long)lp != UiaRootObjectId || !w->root || !w->hwnd) return false;
    out = UiaReturnRawElementProvider(w->hwnd, wp, lp, NodeFor(w->root)); return true;
}
void AccWindowGone(Wnd* w) { if (w->hwnd) UiaReturnRawElementProvider(w->hwnd, 0, 0, nullptr); }   // let UI Automation drop what it holds for this window
void AccViewGone(View* v) {
    auto* n = static_cast<Node*>(v->acc); v->acc = nullptr; if (!n) return;
    n->v = nullptr; UiaDisconnectProvider(n); n->Release();
}
void AccFocusChanged(Wnd* w, View* v) {
    if (!v || v == w->root || !w->hwnd || !UiaClientsAreListening()) return;
    UiaRaiseAutomationEvent(NodeFor(v), UIA_AutomationFocusChangedEventId);
}
void AccValueChanged(View* v) {
    auto* s = dynamic_cast<Slider*>(v); if (!s || !s->acc || !UiaClientsAreListening()) return;
    VARIANT o, n; VariantInit(&o); VariantInit(&n); o.vt = n.vt = VT_R8; o.dblVal = n.dblVal = s->value;   // the old value is not kept: screen readers read the new one
    UiaRaiseAutomationPropertyChangedEvent(static_cast<Node*>(s->acc), UIA_RangeValueValuePropertyId, o, n);
}

// ================= self-test =================
// Builds Quick Settings and Start as the user would see them (without showing them) and checks that the keyboard reaches every button and that
// every one has a name a screen reader can say. Reported: how many stops, and the ones with no name (their class and position) so they can be fixed.
bool AccSelfTest(ws& d) {
    bool ok = true; ws rep;
    auto check = [&](Flyout* f, const wchar_t* name) {
        Wnd& w = *f; if (!w.root) { rep += Fmt(L"%s: no root; ", name); ok = false; return; }
        Sz sz = w.root->Measure({f->widthDip, 2000}); w.client = {f->widthDip, sz.h}; w.root->Arrange({0, 0, f->widthDip, sz.h});
        std::vector<View*> st; CollectStops(w.root, st);
        int unnamed = 0; ws which;
        for (auto* v : st) if (AccNameOf(v).empty()) { if (unnamed++ < 4) which += Fmt(L" %S@%.0f,%.0f", typeid(*v).name() + 7, v->rc.l, v->rc.t); }
        // Tab from nothing lands on the first stop, and Tab from the last comes back round to the first
        bool cycles = true;
        if (!st.empty()) {
            w.focus = nullptr; MoveFocus(&w, 1); cycles = w.focus == st.front();
            w.focus = st.back(); MoveFocus(&w, 1); cycles = cycles && w.focus == st.front();
            w.focus = st.front(); MoveFocus(&w, -1); cycles = cycles && w.focus == st.back();
            w.focus = nullptr; w.kbCue = false;
        }
        rep += Fmt(L"%s: %d stops, %d unnamed%s%s; ", name, (int)st.size(), unnamed, which.c_str(), cycles ? L"" : L", Tab does not go round");
        if (st.size() < 3 || !cycles || unnamed * 10 > (int)st.size()) ok = false;   // nine in ten named at the very least; the report lists the rest
    };
    std::unique_ptr<Flyout> qp(MakeQuickPanel()), st(MakeStartPanel());
    check(qp.get(), L"Quick Settings"); check(st.get(), L"Start");
    // high contrast colours: text on a background must be the two system colours, whatever the theme did
    D2D1_COLOR_F t = HighContrastCol(K::White, 255), bg = HighContrastCol(K::Bar, 255), ac = HighContrastCol(K::Accent, 255);
    COLORREF wt = GetSysColor(COLOR_WINDOWTEXT), wb = GetSysColor(COLOR_WINDOW);
    auto same = [](D2D1_COLOR_F c, COLORREF r) { return std::fabs(c.r - GetRValue(r) / 255.f) < 0.01f && std::fabs(c.g - GetGValue(r) / 255.f) < 0.01f && std::fabs(c.b - GetBValue(r) / 255.f) < 0.01f && c.a == 1; };
    bool hcOk = same(t, wt) && same(bg, wb) && same(ac, GetSysColor(COLOR_HIGHLIGHT));
    rep += hcOk ? L"high contrast colours map to the system's" : L"high contrast colours WRONG";
    ok = ok && hcOk;
    d = rep; return ok;
}
