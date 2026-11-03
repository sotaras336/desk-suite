#pragma once
#include "ui.h"
// Small builders shared by the panels (Start, Quick Settings, clock).
inline View* IconBtn(View* parent, wchar_t glyph, float size = 34, float gsz = 14, std::function<void()> fn = {}) {
    auto* b = MkBox(parent); b->fw = b->fh = size; b->radius = size / 2; b->bg = SolidB(K::White, K::Card); b->bgHover = SolidB(K::White, K::CardHover); b->bgPress = SolidB(K::White, 0x10); b->hit = true;
    auto* g = MkGlyph(b, glyph, gsz); g->ha = Al::Center; g->va = Al::Center; b->onClick = std::move(fn); return b;
}
inline View* Card(View* parent) {      // radius 10, padding 12,10, margin-bottom 6
    auto* c = MkBox(parent, Lay::V); c->radius = 10; c->pad = Thick(12, 10); c->margin = Thick(0, 0, 0, 6); c->bg = SolidB(K::White, K::Card);
    c->borderW = 1; c->border = Col(K::White, K::CardLine); return c;
}
inline View* Pill(View* parent, const ws& text, wchar_t glyph = 0, bool accent = false, std::function<void()> fn = {}) {
    auto* p = MkBox(parent, Lay::H); p->radius = 16; p->pad = Thick(14, 7); p->gap = 8; p->hit = true; p->ha = Al::Start;
    if (accent) { p->bg = SolidB(K::Accent); p->bgHover = SolidB(0x6CD2FF); }
    else { p->borderW = 1; p->border = Col(K::White, 0x55); p->bgHover = SolidB(K::White, 0x20); }
    D2D1_COLOR_F c = accent ? Col(K::Black) : Col(K::White);
    if (glyph) { auto* g = MkGlyph(p, glyph, 12, c); g->va = Al::Center; }
    auto* l = MkLabel(p, text, FText(12.5f), c); l->va = Al::Center; p->onClick = std::move(fn); return p;
}
inline Label* SectionLabel(View* parent, const ws& t) {
    ws u = t; for (auto& c : u) c = towupper(c);
    auto* l = MkLabel(parent, u, FText(10.5f, 600), Col(K::White, K::Dim)); l->margin = Thick(4, 2, 2, 8); return l;
}
inline View* Divider(View* parent, Thick m = {}) { auto* d = MkBox(parent); d->fh = 1; d->bg = SolidB(K::White, K::CardLine); d->margin = m; return d; }
