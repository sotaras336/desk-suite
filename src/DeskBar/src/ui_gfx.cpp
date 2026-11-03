#include "pch.h"
#include "ui.h"

static ComPtr<ID2D1Factory> g_d2d; static ComPtr<IDWriteFactory> g_dw; static ComPtr<IWICImagingFactory> g_wic;
static ws g_famText = L"Segoe UI Variable Text", g_famDisp = L"Segoe UI Variable Display", g_famIcon = L"Segoe Fluent Icons";
ID2D1Factory* D2DFactory() { return g_d2d.Get(); }

static bool HasFamily(const wchar_t* name) {
    ComPtr<IDWriteFontCollection> c; if (FAILED(g_dw->GetSystemFontCollection(&c, FALSE))) return false;
    UINT32 i; BOOL ex = FALSE; c->FindFamilyName(name, &i, &ex); return ex;
}
void UiGfxInit() {
    if (g_d2d) return;
    D2D1_FACTORY_OPTIONS fo{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &fo, (void**)g_d2d.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)g_dw.GetAddressOf());
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic));
    if (!HasFamily(g_famText.c_str())) g_famText = L"Segoe UI";
    if (!HasFamily(g_famDisp.c_str())) g_famDisp = g_famText == L"Segoe UI" ? L"Segoe UI" : L"Segoe UI Variable Display";
    if (!HasFamily(g_famIcon.c_str())) g_famIcon = L"Segoe MDL2 Assets";
}
Font FText(float s, int w) { return {g_famText.c_str(), s, w}; }
Font FDisp(float s, int w) { return {g_famDisp.c_str(), s, w}; }
Font FIcon(float s) { return {g_famIcon.c_str(), s, 400}; }

float EaseF(Ease e, float t) {
    t = std::clamp(t, 0.f, 1.f);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::CubicOut: return 1 - powf(1 - t, 3);
    case Ease::QuintOut: return 1 - powf(1 - t, 5);
    case Ease::CubicIn: return t * t * t;
    case Ease::SineInOut: return -(cosf(3.14159265f * t) - 1) / 2;
    }
    return t;
}

Brush SolidB(uint32_t rgb, int a) { Brush b; b.k = Brush::Solid; b.c = Col(rgb, a); return b; }
Brush SolidC(D2D1_COLOR_F c) { Brush b; b.k = Brush::Solid; b.c = c; return b; }
Brush LinearB(float x0, float y0, float x1, float y1, std::initializer_list<Stop> s) {
    Brush b; b.k = Brush::Linear; b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1; for (auto& e : s) if (b.ns < 3) b.st[b.ns++] = e; return b;
}
Brush RadialB(float cx, float cy, float rx, float ry, std::initializer_list<Stop> s) {
    Brush b; b.k = Brush::Radial; b.x0 = cx; b.y0 = cy; b.rx = rx; b.ry = ry; for (auto& e : s) if (b.ns < 3) b.st[b.ns++] = e; return b;
}

// ---------- text ----------
static std::map<std::tuple<std::wstring, int, int, int>, ComPtr<IDWriteTextFormat>> g_fmt;
static IDWriteTextFormat* GetFmt(const Font& f, TA ta, bool wrap, bool trim) {
    auto key = std::make_tuple(ws(f.fam), (int)(f.size * 100) * 1000 + f.weight, (int)ta * 2 + (wrap ? 1 : 0), trim ? 1 : 0);
    auto it = g_fmt.find(key); if (it != g_fmt.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> tf;
    g_dw->CreateTextFormat(f.fam, nullptr, (DWRITE_FONT_WEIGHT)f.weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, f.size, L"en-us", &tf);
    tf->SetTextAlignment(ta == TA::Left ? DWRITE_TEXT_ALIGNMENT_LEADING : ta == TA::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING);
    tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    tf->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (trim) {
        ComPtr<IDWriteInlineObject> sign; g_dw->CreateEllipsisTrimmingSign(tf.Get(), &sign);
        DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; tf->SetTrimming(&tr, sign.Get());
    }
    return (g_fmt[key] = tf).Get();
}
static ComPtr<IDWriteTextLayout> MkLayout(const ws& s, const Font& f, float w, float h, TA ta, bool wrap, bool trim) {
    ComPtr<IDWriteTextLayout> l;
    g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), GetFmt(f, ta, wrap, trim), std::max(1.f, std::min(w, 100000.f)), std::max(1.f, std::min(h, 100000.f)), &l);
    return l;
}
Sz MeasureText(const ws& s, const Font& f, float maxW, bool wrap) {
    auto l = MkLayout(s.empty() ? L" " : s, f, maxW, 100000, TA::Left, wrap, false);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m);
    return {ceilf(s.empty() ? 0 : m.widthIncludingTrailingWhitespace), ceilf(m.height)};
}
float TextCaretX(const ws& s, const Font& f, int pos) {
    if (s.empty() || pos <= 0) return 0;
    auto l = MkLayout(s, f, 100000, 1000, TA::Left, false, false);
    float x, y; DWRITE_HIT_TEST_METRICS m;
    l->HitTestTextPosition(std::min(pos, (int)s.size()), FALSE, &x, &y, &m); return x;
}
int TextHit(const ws& s, const Font& f, float x) {
    if (s.empty()) return 0;
    auto l = MkLayout(s, f, 100000, 1000, TA::Left, false, false);
    BOOL trailing, inside; DWRITE_HIT_TEST_METRICS m;
    l->HitTestPoint(x, 1, &trailing, &inside, &m);
    return (int)m.textPosition + (trailing ? 1 : 0);
}

// ---------- Gfx ----------
void Gfx::Reset(ID2D1RenderTarget* t) {
    rt = t; bmp.clear(); sb.Reset(); clips.clear();
    if (rt) { rt->CreateSolidColorBrush(D2D1::ColorF(0, 0), &sb); rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); }
}
static void FillGeom(Gfx& g, const Rf& r, ID2D1Brush* b, float rad) {
    if (rad > 0.5f) g.rt->FillRoundedRectangle(D2D1::RoundedRect(r.d(), rad, rad), b); else g.rt->FillRectangle(r.d(), b);
}
void Gfx::FillC(const Rf& r, D2D1_COLOR_F c, float rad) {
    c = A(c); if (c.a <= 0 || r.w() <= 0 || r.h() <= 0) return;
    sb->SetColor(c); FillGeom(*this, r, sb.Get(), rad);
}
void Gfx::Fill(const Rf& r, const Brush& b, float rad) {
    if (b.k == Brush::None || r.w() <= 0 || r.h() <= 0) return;
    if (b.k == Brush::Solid) { FillC(r, b.c, rad); return; }
    D2D1_GRADIENT_STOP gs[3]; for (int i = 0; i < b.ns; i++) { gs[i].position = b.st[i].pos; gs[i].color = A(b.st[i].c); }
    ComPtr<ID2D1GradientStopCollection> col; rt->CreateGradientStopCollection(gs, (UINT)b.ns, &col); if (!col) return;
    ComPtr<ID2D1Brush> br;
    if (b.k == Brush::Linear) {
        ComPtr<ID2D1LinearGradientBrush> lb;
        rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.l + b.x0 * r.w(), r.t + b.y0 * r.h()), D2D1::Point2F(r.l + b.x1 * r.w(), r.t + b.y1 * r.h())), col.Get(), &lb);
        br = lb;
    } else {
        ComPtr<ID2D1RadialGradientBrush> rb;
        rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(r.l + b.x0 * r.w(), r.t + b.y0 * r.h()), D2D1::Point2F(0, 0), b.rx, b.ry), col.Get(), &rb);
        br = rb;
    }
    if (br) FillGeom(*this, r, br.Get(), rad);
}
void Gfx::Stroke(const Rf& r, D2D1_COLOR_F c, float rad, float w) {
    c = A(c); if (c.a <= 0 || w <= 0) return;
    sb->SetColor(c); Rf i = r.Inset(w / 2);
    if (rad > 0.5f) rt->DrawRoundedRectangle(D2D1::RoundedRect(i.d(), std::max(0.f, rad - w / 2), std::max(0.f, rad - w / 2)), sb.Get(), w);
    else rt->DrawRectangle(i.d(), sb.Get(), w);
}
void Gfx::Circle(float cx, float cy, float rad, D2D1_COLOR_F c) {
    c = A(c); if (c.a <= 0) return; sb->SetColor(c); rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rad, rad), sb.Get());
}
void Gfx::Line(float x0, float y0, float x1, float y1, D2D1_COLOR_F c, float w) {
    c = A(c); if (c.a <= 0) return; sb->SetColor(c); rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), sb.Get(), w);
}
void Gfx::Text(const ws& s, const Rf& r, const Font& f, D2D1_COLOR_F c, TA ta, Al va, bool wrap) {
    c = A(c); if (c.a <= 0 || s.empty() || r.w() <= 0) return;
    auto l = MkLayout(s, f, r.w(), r.h(), ta, wrap, true);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m);
    float y = r.t; if (va == Al::Center) y = r.t + (r.h() - m.height) / 2; else if (va == Al::End) y = r.b - m.height;
    sb->SetColor(c); rt->DrawTextLayout(D2D1::Point2F(r.l, floorf(y * scale) / scale), l.Get(), sb.Get(), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
}
void Gfx::Glyph(wchar_t ch, const Rf& r, float size, D2D1_COLOR_F c) {
    c = A(c); if (c.a <= 0) return;
    ws s(1, ch); Font f = FIcon(size);
    auto l = MkLayout(s, f, 1000, 1000, TA::Left, false, false);
    DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m);
    sb->SetColor(c); rt->DrawTextLayout(D2D1::Point2F(r.cx() - m.width / 2, r.cy() - m.height / 2), l.Get(), sb.Get(), D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
}
void Gfx::Image(const Img& im, const Rf& r, float opacity) {
    if (im.w <= 0 || im.px.empty()) return;
    ComPtr<ID2D1Bitmap> b; auto it = bmp.find(im.id);
    if (it != bmp.end()) b = it->second;
    else {
        D2D1_BITMAP_PROPERTIES p = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        rt->CreateBitmap(D2D1::SizeU(im.w, im.h), im.px.data(), im.w * 4, &p, &b);
        if (bmp.size() > 400) bmp.clear();
        bmp[im.id] = b;
    }
    if (b) rt->DrawBitmap(b.Get(), r.d(), opacity * alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}
void Gfx::PushClip(const Rf& r, float radius) {
    if (radius > 0.5f) {
        ComPtr<ID2D1RoundedRectangleGeometry> geo; g_d2d->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r.d(), radius, radius), &geo);
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get()), nullptr); clips.push_back(1);
    } else { rt->PushAxisAlignedClip(r.d(), D2D1_ANTIALIAS_MODE_ALIASED); clips.push_back(0); }
}
void Gfx::PopClip() {
    if (clips.empty()) return;
    if (clips.back()) rt->PopLayer(); else rt->PopAxisAlignedClip();
    clips.pop_back();
}

// ---------- images ----------
static std::atomic<uint64_t> g_imgId{1};
ImgP ImgMake(int w, int h) { auto i = std::make_shared<Img>(); i->w = w; i->h = h; i->px.assign((size_t)w * h, 0); i->id = g_imgId++; return i; }
ImgP ImgFromHBitmap(HBITMAP hbm) {
    BITMAP bm{}; if (!GetObjectW(hbm, sizeof bm, &bm) || bm.bmWidth <= 0) return nullptr;
    auto im = ImgMake(bm.bmWidth, std::abs(bm.bmHeight));
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = im->w; bi.bmiHeader.biHeight = -im->h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(nullptr); GetDIBits(dc, hbm, 0, im->h, im->px.data(), &bi, DIB_RGB_COLORS); ReleaseDC(nullptr, dc);
    // shell icons come as straight alpha: premultiply (if every alpha is 0 the bitmap has no alpha channel: make it opaque)
    bool anyA = false; for (auto p : im->px) if (p >> 24) { anyA = true; break; }
    for (auto& p : im->px) {
        uint32_t a = anyA ? p >> 24 : 255; uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        p = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
    }
    return im;
}
ImgP ImgFromHIcon(HICON ic, int px) {   // draw on black and on white: the difference is the alpha, the black pass is the premultiplied colour (right for any icon, with or without an alpha channel)
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = px; bi.bmiHeader.biHeight = -px; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HDC dc = CreateCompatibleDC(nullptr); HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { DeleteDC(dc); return nullptr; }
    HGDIOBJ old = SelectObject(dc, dib); size_t n = (size_t)px * px; std::vector<uint32_t> blk(n), wht(n);
    memset(bits, 0, n * 4); DrawIconEx(dc, 0, 0, ic, px, px, 0, nullptr, DI_NORMAL); GdiFlush(); memcpy(blk.data(), bits, n * 4);
    memset(bits, 0xFF, n * 4); DrawIconEx(dc, 0, 0, ic, px, px, 0, nullptr, DI_NORMAL); GdiFlush(); memcpy(wht.data(), bits, n * 4);
    auto im = ImgMake(px, px);
    for (size_t i = 0; i < n; i++) {
        int dr = (int)((wht[i] >> 16) & 255) - (int)((blk[i] >> 16) & 255), dg = (int)((wht[i] >> 8) & 255) - (int)((blk[i] >> 8) & 255), db = (int)(wht[i] & 255) - (int)(blk[i] & 255);
        int a = std::clamp(255 - (dr + dg + db) / 3, 0, 255); uint32_t c = blk[i] & 0xFFFFFF;
        im->px[i] = a ? ((uint32_t)a << 24) | c : 0;
    }
    SelectObject(dc, old); DeleteObject(dib); DeleteDC(dc); return im;
}
ImgP ImgFromFile(const ws& path) {
    if (!g_wic) return nullptr;
    ComPtr<IWICBitmapDecoder> dec; if (FAILED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(dec->GetFrame(0, &fr))) return nullptr;
    ComPtr<IWICFormatConverter> cv; g_wic->CreateFormatConverter(&cv);
    if (FAILED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    UINT w, h; cv->GetSize(&w, &h); auto im = ImgMake((int)w, (int)h);
    if (FAILED(cv->CopyPixels(nullptr, w * 4, (UINT)im->px.size() * 4, (BYTE*)im->px.data()))) return nullptr;
    return im;
}
bool ImgSavePng(const Img& img, const ws& path) {
    if (!g_wic || img.w <= 0) return false;
    ComPtr<IWICBitmapEncoder> enc; if (FAILED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return false;
    ComPtr<IWICStream> st; g_wic->CreateStream(&st); if (FAILED(st->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return false;
    enc->Initialize(st.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr; ComPtr<IPropertyBag2> pb; enc->CreateNewFrame(&fr, &pb); fr->Initialize(pb.Get());
    fr->SetSize(img.w, img.h); GUID fmt = GUID_WICPixelFormat32bppPBGRA; fr->SetPixelFormat(&fmt);
    fr->WritePixels(img.h, img.w * 4, (UINT)img.px.size() * 4, (BYTE*)img.px.data());
    return SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
}
