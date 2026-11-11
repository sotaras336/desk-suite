#include "pch.h"
#include "ui.h"

// ================= View: tree, layout, paint =================
View::~View() {
    AnimCancel(this);
    if (wnd) wnd->Forget(this);
    for (auto* k : kids) { k->parent = nullptr; delete k; }
}
void View::SetWnd(Wnd* w) { wnd = w; for (auto* k : kids) k->SetWnd(w); }
void View::AddView(View* k) { k->parent = this; k->SetWnd(wnd); kids.push_back(k); Relayout(); }
void View::Remove(View* k, bool del) {
    auto it = std::find(kids.begin(), kids.end(), k); if (it == kids.end()) return;
    kids.erase(it); k->parent = nullptr; if (del) delete k; Relayout();
}
void View::Clear() { auto old = std::move(kids); kids.clear(); for (auto* k : old) { k->parent = nullptr; delete k; } Relayout(); }
void View::Invalidate() { if (wnd) wnd->Invalidate(); }
void View::Relayout() { if (wnd) wnd->Relayout(); }

Sz View::MeasureContent(Sz avail) {
    Sz r{0, 0};
    std::vector<View*> v; for (auto* k : kids) if (k->visible) v.push_back(k);
    if (v.empty()) return r;
    switch (lay) {
    case Lay::Overlay:
        for (auto* k : v) { Sz d = k->Measure(avail); r.w = std::max(r.w, d.w); r.h = std::max(r.h, d.h); }
        break;
    case Lay::H: case Lay::V: {
        bool H = lay == Lay::H; float main = 0, cross = 0, used = 0;
        for (auto* k : v) if (k->star <= 0) {
            Sz d = H ? k->Measure({INF, avail.h}) : k->Measure({avail.w, INF});
            main += H ? d.w : d.h; cross = std::max(cross, H ? d.h : d.w);
        }
        used = main + gap * (v.size() - 1);
        float left = std::max(0.f, (H ? avail.w : avail.h) - used);
        for (auto* k : v) if (k->star > 0) {
            Sz d = H ? k->Measure({left > INF / 2 ? INF : left, avail.h}) : k->Measure({avail.w, left > INF / 2 ? INF : left});
            main += H ? d.w : d.h; cross = std::max(cross, H ? d.h : d.w);
        }
        main += gap * (v.size() - 1);
        r = H ? Sz{main, cross} : Sz{cross, main}; break;
    }
    case Lay::Wrap: {
        float x = 0, y = 0, rowH = 0, maxX = 0;
        for (auto* k : v) {
            Sz d = k->Measure({avail.w, INF});
            if (x > 0 && x + d.w > avail.w) { y += rowH + gap; x = 0; rowH = 0; }
            x += d.w + gap; rowH = std::max(rowH, d.h); maxX = std::max(maxX, x - gap);
        }
        r = {maxX, y + rowH}; break;
    }
    case Lay::Uniform: {
        float cw = avail.w > INF / 2 ? 0 : (avail.w - gap * (cols - 1)) / cols; float rows = 0, rowH = 0, total = 0; int c = 0;
        for (auto* k : v) {
            Sz d = k->Measure({cw > 0 ? cw : INF, INF}); if (cw <= 0) cw = d.w;
            rowH = std::max(rowH, isnan(cellH) ? d.h : cellH);
            if (++c == cols) { total += rowH + gap; rowH = 0; c = 0; rows++; }
        }
        if (c) { total += rowH + gap; rows++; }
        r = {cw * cols + gap * (cols - 1), std::max(0.f, total - gap)}; break;
    }
    }
    return r;
}
void View::ArrangeContent(Rf in) {
    std::vector<View*> v; for (auto* k : kids) if (k->visible) v.push_back(k);
    if (v.empty()) return;
    switch (lay) {
    case Lay::Overlay: for (auto* k : v) k->Arrange(in); break;
    case Lay::H: case Lay::V: {
        bool H = lay == Lay::H; float used = gap * (v.size() - 1), stars = 0;
        for (auto* k : v) { if (k->star > 0) stars += k->star; else used += H ? k->desired.w : k->desired.h; }
        float left = std::max(0.f, (H ? in.w() : in.h()) - used), pos = H ? in.l : in.t;
        for (auto* k : v) {
            float sz = k->star > 0 ? left * k->star / stars : (H ? k->desired.w : k->desired.h);
            k->Arrange(H ? Rf{pos, in.t, pos + sz, in.b} : Rf{in.l, pos, in.r, pos + sz});
            pos += sz + gap;
        }
        break;
    }
    case Lay::Wrap: {
        float x = in.l, y = in.t, rowH = 0;
        for (auto* k : v) {
            Sz d = k->desired;
            if (x > in.l && x + d.w > in.r + 0.5f) { y += rowH + gap; x = in.l; rowH = 0; }
            k->Arrange({x, y, x + d.w, y + d.h}); x += d.w + gap; rowH = std::max(rowH, d.h);
        }
        break;
    }
    case Lay::Uniform: {
        float cw = (in.w() - gap * (cols - 1)) / cols, y = in.t, rowH = 0; int c = 0;
        for (auto* k : v) {
            float h = isnan(cellH) ? k->desired.h : cellH; rowH = std::max(rowH, h);
            float x = in.l + c * (cw + gap);
            k->Arrange({x, y, x + cw, y + h});
            if (++c == cols) { y += rowH + gap; rowH = 0; c = 0; }
        }
        break;
    }
    }
}
Sz View::Measure(Sz avail) {
    if (!visible) return desired = {0, 0};
    float aw = std::max(0.f, avail.w - margin.w()), ah = std::max(0.f, avail.h - margin.h());
    float cw = !isnan(fw) ? fw : std::min(aw, maxW), ch = !isnan(fh) ? fh : ah;
    Sz cs = MeasureContent({std::max(0.f, cw - pad.w()), std::max(0.f, ch - pad.h())});
    float w = !isnan(fw) ? fw : cs.w + pad.w(), h = !isnan(fh) ? fh : cs.h + pad.h();
    w = std::clamp(w, minW, std::max(minW, maxW)); h = std::max(h, minH);
    return desired = {w + margin.w(), h + margin.h()};
}
void View::Arrange(Rf slot) {
    if (!visible) return;
    float aw = std::max(0.f, slot.w() - margin.w()), ah = std::max(0.f, slot.h() - margin.h());
    float dw = desired.w - margin.w(), dh = desired.h - margin.h(), w, h;
    Al h_ = ha, v_ = va;
    if (!isnan(fw)) { w = fw; if (h_ == Al::Stretch) h_ = Al::Center; }
    else if (h_ == Al::Stretch) { w = aw; if (w > maxW) { w = maxW; h_ = Al::Center; } } else w = std::min(dw, aw);
    if (!isnan(fh)) { h = fh; if (v_ == Al::Stretch) v_ = Al::Center; }
    else if (v_ == Al::Stretch) h = ah; else h = std::min(dh, ah);
    float x = slot.l + margin.l, y = slot.t + margin.t;
    if (h_ == Al::Center) x += (aw - w) / 2; else if (h_ == Al::End) x += aw - w;
    if (v_ == Al::Center) y += (ah - h) / 2; else if (v_ == Al::End) y += ah - h;
    rc = {x, y, x + w, y + h};
    ArrangeContent({rc.l + pad.l, rc.t + pad.t, rc.r - pad.r, rc.b - pad.b});
}
void View::Paint(Gfx& g) {
    if (!visible) return;
    float oa = g.alpha; g.alpha *= opacity;
    if (g.alpha <= 0.002f) { g.alpha = oa; return; }
    bool xf = scale != 1 || tx != 0 || ty != 0; D2D1_MATRIX_3X2_F old{};
    if (xf) {
        g.rt->GetTransform(&old);
        g.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(rc.cx(), rc.cy())) * D2D1::Matrix3x2F::Translation(tx, ty) * old);
    }
    const Brush* b = &bg;
    if (enabled && pressed && !bgPress.none()) b = &bgPress; else if (enabled && hovered && !bgHover.none()) b = &bgHover;
    if (!b->none()) g.Fill(rc, *b, radius);
    if (borderW > 0) g.Stroke(rc, (hovered && !borderHover.none()) ? borderHover.c : border, radius, borderW);
    if (clip) g.PushClip(rc, radius);
    PaintContent(g);
    for (auto* k : kids) k->Paint(g);
    PaintOver(g);
    if (clip) g.PopClip();
    if (xf) g.rt->SetTransform(old);
    g.alpha = oa;
}

// ================= Label / Glyph / Pic =================
Sz Label::MeasureContent(Sz avail) {
    Sz m = MeasureText(text, font, wrap ? avail.w : INF, wrap);
    if (!wrap && avail.w < INF / 2 && m.w > avail.w) m.w = avail.w;
    return m;
}
void Label::PaintContent(Gfx& g) { g.Text(text, {rc.l + pad.l, rc.t + pad.t, rc.r - pad.r, rc.b - pad.b}, font, col, ta, tva, wrap); }
void Glyph::PaintContent(Gfx& g) { g.Glyph(ch, rc, size, col); }
void Pic::PaintContent(Gfx& g) {
    ImgP im = provider ? provider((int)std::lround(std::min(rc.w(), rc.h()) * g.scale)) : img;
    if (im) g.Image(*im, rc, opacityImg);
}
Label* MkLabel(View* p, const ws& t, Font f, D2D1_COLOR_F c) { auto* l = new Label(t, f, c); if (p) p->Add(l); return l; }
Glyph* MkGlyph(View* p, wchar_t ch, float s, D2D1_COLOR_F c) { auto* g = new Glyph(ch, s, c); if (p) p->Add(g); return g; }
View* MkBox(View* p, Lay l) { auto* v = new View(); v->lay = l; if (p) p->Add(v); return v; }

// ================= Slider =================
void Slider::From(float x, bool fin) {
    float a = rc.l + 10, b = rc.r - 10; float v = b > a ? std::clamp((x - a) / (b - a), 0.f, 1.f) * 100 : 0;
    value = v; Invalidate(); if (onChange) onChange(value, fin);
}
bool Slider::OnDown(float x, float, bool) { if (!enabled) return false; dragging = true; pressed = true; From(x, false); return true; }
void Slider::OnDrag(float x, float) { if (dragging) From(x, false); }
void Slider::OnUp(float x, float) { if (dragging) { dragging = false; From(x, true); } }
bool Slider::OnKey(UINT, bool, bool) { return false; }
void Slider::PaintContent(Gfx& g) {
    float a = rc.l + 10, b = rc.r - 10, cy = rc.cy(), x = a + (b - a) * std::clamp(value, 0.f, 100.f) / 100;
    float op = enabled ? 1 : 0.5f; float oa = g.alpha; g.alpha *= op;
    g.FillC({a, cy - 2, b, cy + 2}, Col(K::White, 0x5C), 2);
    if (x > a) g.FillC({a, cy - 2, x, cy + 2}, Col(K::Accent), 2);
    g.Circle(x, cy, 10, Col(K::White, 0xE6));
    g.Circle(x, cy, pressed ? 5.f : hovered ? 7.f : 6.f, Col(K::Accent));
    g.alpha = oa;
}

// ================= Toggle =================
Toggle::Toggle() {
    hit = true;
    onClick = [this] { if (!enabled) return; Set(!on); if (onChange) onChange(on); };
}
void Toggle::Set(bool v) { if (v == on) return; on = v; Anim(this, &knob, on ? 1.f : 0.f, 130); }
void Toggle::PaintContent(Gfx& g) {
    Rf r = rc; float op = enabled ? 1 : 0.5f; float oa = g.alpha; g.alpha *= op;
    D2D1_COLOR_F acc = rawOn ? D2D1::ColorF(rawOn) : Col(K::Accent), off = Col(K::White, 0xB0);
    auto mix = [&](float t) { return D2D1_COLOR_F{off.r + (acc.r - off.r) * t, off.g + (acc.g - off.g) * t, off.b + (acc.b - off.b) * t, 1.f}; };
    if (knob > 0.01f) g.FillC(r, mix(knob), 11);
    g.Stroke(r, mix(knob), 11, 1);
    float kx = r.l + 11 + (r.w() - 22) * knob; D2D1_COLOR_F kc = knob > 0.5f ? Col(K::Black) : Col(K::White);
    g.Circle(kx, r.cy(), pressed ? 7.f : 6.f, kc);
    g.alpha = oa;
}

// ================= TextBox =================
TextBox::~TextBox() { if (blinkTimer) UiKillTimer(blinkTimer); }
void TextBox::Changed() { if (onChange) onChange(text); }
void TextBox::SetText(const ws& t, bool notify) {
    text = t; caret = anchor = (int)t.size(); scrollX = 0; EnsureVisible(); Invalidate(); if (notify) Changed();
}
void TextBox::Insert(const ws& s) {
    int a = std::min(caret, anchor), b = std::max(caret, anchor);
    text.replace(a, b - a, s); caret = anchor = a + (int)s.size(); blink = true; EnsureVisible(); Invalidate(); Changed();
}
void TextBox::EnsureVisible() {
    float w = rc.w() - pad.w(); if (w <= 0) return;
    float cx = TextCaretX(Shown(), font, caret);
    if (cx - scrollX > w - 2) scrollX = cx - w + 2; if (cx - scrollX < 0) scrollX = cx;
    float total = MeasureText(Shown(), font).w; if (total <= w) scrollX = 0;
}
void TextBox::OnFocus(bool f) {
    if (blinkTimer) { UiKillTimer(blinkTimer); blinkTimer = 0; }
    blink = true; if (f) blinkTimer = UiTimer(530, [this] { blink = !blink; Invalidate(); });
    Invalidate();
}
void TextBox::PaintContent(Gfx& g) {
    Rf a{rc.l + pad.l, rc.t, rc.r - pad.r, rc.b}; ws s = Shown(); bool focused = wnd && wnd->focus == this;
    g.PushClip(a);
    if (text.empty()) g.Text(placeholder, a, font, phCol);
    else {
        if (focused && caret != anchor) {
            float x0 = TextCaretX(s, font, std::min(caret, anchor)) - scrollX, x1 = TextCaretX(s, font, std::max(caret, anchor)) - scrollX;
            Sz m = MeasureText(L"Ag", font); g.FillC({a.l + x0, a.cy() - m.h / 2, a.l + x1, a.cy() + m.h / 2}, Col(K::Accent, 0x66), 2);
        }
        g.Text(s, {a.l - scrollX, a.t, a.r + 100000, a.b}, font, col);
    }
    if (focused && blink && caret == anchor) {
        float x = a.l + TextCaretX(s, font, caret) - scrollX; Sz m = MeasureText(L"Ag", font);
        g.FillC({x, a.cy() - m.h / 2 + 1, x + 1.5f, a.cy() + m.h / 2 - 1}, col);
    }
    g.PopClip();
}
bool TextBox::OnDown(float x, float, bool) {
    caret = TextHit(Shown(), font, x - (rc.l + pad.l) + scrollX);
    if (!(GetKeyState(VK_SHIFT) & 0x8000)) anchor = caret;
    blink = true; Invalidate(); return true;
}
void TextBox::OnDrag(float x, float) { caret = TextHit(Shown(), font, x - (rc.l + pad.l) + scrollX); EnsureVisible(); Invalidate(); }
static ws ClipGet(HWND h) {
    ws r; if (!OpenClipboard(h)) return r;
    if (HANDLE d = GetClipboardData(CF_UNICODETEXT)) { if (auto* p = (const wchar_t*)GlobalLock(d)) { r = p; GlobalUnlock(d); } }
    CloseClipboard(); return r;
}
static void ClipSet(HWND h, const ws& s) {
    if (!OpenClipboard(h)) return;
    EmptyClipboard(); HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * 2);
    if (m) { memcpy(GlobalLock(m), s.c_str(), (s.size() + 1) * 2); GlobalUnlock(m); SetClipboardData(CF_UNICODETEXT, m); }
    CloseClipboard();
}
void ClipboardSetText(const ws& s) { ClipSet(nullptr, s); }
bool TextBox::OnKey(UINT vk, bool ctrl, bool shift) {
    if (onKeyHook && onKeyHook(vk)) return true;
    int n = (int)text.size(), a = std::min(caret, anchor), b = std::max(caret, anchor);
    auto word = [&](int p, int dir) { while (p + dir >= 0 && p + dir <= n && text[dir < 0 ? p - 1 : p] == L' ') p += dir; while (p + dir >= 0 && p + dir <= n && text[dir < 0 ? p - 1 : p] != L' ') p += dir; return p; };
    auto move = [&](int to) { caret = std::clamp(to, 0, n); if (!shift) anchor = caret; blink = true; EnsureVisible(); Invalidate(); };
    switch (vk) {
    case VK_LEFT: if (!shift && a != b) move(a); else move(ctrl ? word(caret, -1) : caret - 1); return true;
    case VK_RIGHT: if (!shift && a != b) move(b); else move(ctrl ? word(caret, 1) : caret + 1); return true;
    case VK_HOME: move(0); return true;
    case VK_END: move(n); return true;
    case VK_BACK: if (a == b && a > 0) { anchor = ctrl ? word(a, -1) : a - 1; caret = a; } Insert(L""); return true;   // Ctrl: the whole word before the caret
    case VK_DELETE: if (a == b && b < n) { anchor = b; caret = ctrl ? word(b, 1) : b + 1; } Insert(L""); return true;
    case VK_RETURN: if (onEnter) { onEnter(); return true; } return false;
    case 'A': if (ctrl) { SelectAll(); return true; } break;
    case 'C': if (ctrl) { if (a != b && !password) ClipSet(wnd->hwnd, text.substr(a, b - a)); return true; } break;
    case 'X': if (ctrl) { if (a != b && !password) { ClipSet(wnd->hwnd, text.substr(a, b - a)); Insert(L""); } return true; } break;
    case 'V': if (ctrl) { ws s = ClipGet(wnd->hwnd); for (auto& c : s) if (c < 32) c = L' '; Insert(s); return true; } break;
    }
    return false;
}
void TextBox::OnChar(wchar_t c) { if (c >= 0x20 && c != 0x7f) Insert(ws(1, c)); }

// ================= Scroll =================
Sz Scroll::MeasureContent(Sz avail) {
    if (!content) return {};
    Sz d = content->Measure({avail.w, INF}); contentH = d.h;
    return {d.w, std::min(d.h, avail.h)};
}
void Scroll::ArrangeContent(Rf in) {
    if (!content) return;
    content->Arrange({in.l, in.t, in.r, in.t + contentH});
    float mx = MaxOff(); if (target > mx) target = mx;
    if (-content->ty > mx) content->ty = -mx;
}
void Scroll::ScrollTo(float y, bool animate) {
    if (!content) return;
    target = std::clamp(y, 0.f, MaxOff());
    if (animate) Anim(content, &content->ty, -target, 140); else { AnimCancel(content); content->ty = -target; Invalidate(); }
}
// One wheel notch moves what Windows' "lines per notch" says (3 lines of 16 = 48, the same as the other Desk apps), not a fixed 80.
static float WheelPx() {
    UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    return 16.f * (lines == 0 || lines > 20 ? 3u : lines);   // 0 = off, WHEEL_PAGESCROLL = a page: both fall back to 3
}
// A step per notch for lists that move in whole steps: a touchpad sends dozens of small deltas plus an inertia tail, which should be one step, not a dozen.
int WheelStep(float d) {
    static float acc = 0; static ULONGLONG last = 0, lastStep = 0; ULONGLONG now = GetTickCount64();
    if (now - last > 400 || (acc != 0 && (acc < 0) != (d < 0))) acc = 0;
    last = now; acc += d;
    if (std::fabs(acc) < 1) return 0;
    int s = acc > 0 ? 1 : -1; acc = 0;
    if (now - lastStep < 220) return 0;
    lastStep = now; return s;
}
bool Scroll::OnWheel(float d, float, float) {
    if (MaxOff() <= 0) return false;
    d = std::clamp(d, -3.f, 3.f);
    float cur = -content->ty; float t = std::clamp((AnimActive(content) ? target : cur) - d * WheelPx(), 0.f, MaxOff());
    if (t == target && !AnimActive(content)) return false;
    ScrollTo(t); return true;
}
bool Scroll::WantsPan(float dx, float dy) { return MaxOff() > 0 && std::fabs(dy) >= std::fabs(dx); }
void Scroll::OnPan(float, float dy) { AnimCancel(content); target = std::clamp(-content->ty - dy, 0.f, MaxOff()); content->ty = -target; Invalidate(); }
void Scroll::PaintContent(Gfx&) {}
void Scroll::PaintOver(Gfx& g) {
    float mx = MaxOff(); if (mx <= 0 || !hovered) return;
    float vh = rc.h(), th = std::max(24.f, vh * vh / (vh + mx)), y = rc.t + (vh - th) * (-content->ty / mx);
    g.FillC({rc.r - 5, y, rc.r - 2, y + th}, Col(K::White, 0x70), 1.5f);
}
