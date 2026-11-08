#include "pch.h"
#include "bar.h"
#include "widgets.h"

// Bar widgets: small extras you switch on in DeskBar settings (all off by default). kWidgets is the list; WidgetsBuild adds each one's view to a bar.
const WidgetDef kWidgets[] = {{L"search", L"Search box next to the Windows key (type, press Enter: searches Google)"}};
const int kWidgetCount = (int)(sizeof kWidgets / sizeof kWidgets[0]);

bool HasWidget(const ws& id) { for (auto& w : S.widgets) if (w == id) return true; return false; }
void SetWidget(const ws& id, bool on) {
    std::erase(S.widgets, id); if (on) S.widgets.push_back(id);
    SaveSettingsSoon(); WidgetsApply();
}
void WidgetsApply() {   // show or hide each bar's widgets to match the settings; no restart, no flicker
    for (auto* b : AllBars()) for (auto& [id, v] : b->widgetViews) { bool on = HasWidget(id); if (v->visible != on) { v->visible = on; v->Relayout(); } }
}

// What to open for text typed in the search box: a web address as it is, anything else as a Google search.
ws WebSearchUrl(const ws& typed) {
    ws q = typed; while (!q.empty() && iswspace(q.front())) q.erase(0, 1); while (!q.empty() && iswspace(q.back())) q.pop_back();
    if (q.empty()) return {};
    if (q.find_first_of(L" \t") == ws::npos) {
        if (StartsI(q, L"http://") || StartsI(q, L"https://")) return q;
        ws host = q.substr(0, q.find_first_of(L"/?#")); size_t dot = host.rfind(L'.');   // "example.com", "github.com/user": a dot and a letters-only ending
        if (dot != ws::npos && dot > 0 && host.size() - dot - 1 >= 2 && std::all_of(host.begin() + dot + 1, host.end(), [](wchar_t c) { return iswalpha(c); })) return L"https://" + q;
    }
    ws out = L"https://www.google.com/search?q=";
    for (unsigned char c : Narrow(q)) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (wchar_t)c; else out += Fmt(L"%%%02X", c); }
    return out;
}

namespace {
// The bar never takes the keyboard (WS_EX_NOACTIVATE), so the box lets it while it is being typed in, and hands the keyboard back when it is done.
struct BarSearch : TextBox {
    Bar* bar; HWND prev = nullptr;
    explicit BarSearch(Bar* b) : bar(b) {}
    void OnFocus(bool f) override {
        TextBox::OnFocus(f);
        if (f) { HWND fg = GetForegroundWindow(); if (fg != bar->hwnd) prev = fg; bar->noActivate = false; ForceForeground(bar->hwnd); }
        else bar->noActivate = true;
        if (parent) { parent->border = f ? Col(K::Accent) : Col(K::White, 0x22); parent->bg = SolidB(K::White, f ? 0x2C : 0x1E); }
        Invalidate();
    }
    void Leave(bool restore) { SetText(L"", false); bar->SetFocus(nullptr); if (restore && prev && IsWindow(prev)) ForceForeground(prev); }
};
}

void WidgetsBuild(Bar* b, View* parent) {
    auto* p = MkBox(parent, Lay::H); p->fh = 32; p->fw = 220; p->va = Al::Center; p->radius = 16; p->pad = Thick(12, 0, 14, 0); p->gap = 8; p->margin = Thick(6, 0, 8, 0);
    p->bg = SolidB(K::White, 0x1E); p->bgHover = SolidB(K::White, 0x2C); p->borderW = 1; p->border = Col(K::White, 0x22); p->hit = true; p->visible = HasWidget(L"search");
    auto* g = MkGlyph(p, 0xE721, 13, Col(K::White, K::Dim)); g->va = Al::Center;
    auto* t = new BarSearch(b); p->Add(t); t->star = 1; t->va = Al::Center; t->pad = Thick(0); t->font = FText(12.5f); t->placeholder = L"Search the web";
    t->onEnter = [t] { ws u = WebSearchUrl(t->text); if (!u.empty()) { Launch(u); t->Leave(false); } };
    t->onKeyHook = [t](UINT vk) { if (vk == VK_ESCAPE) { t->Leave(true); return true; } return false; };
    p->onClick = [b, t] { b->SetFocus(t); };   // a click on the rounded frame, not just on the text
    b->widgetViews.push_back({L"search", p});
    b->onDeactivate = [b] { if (b->focus) b->SetFocus(nullptr); };   // clicked elsewhere: stop typing; the text stays for a moment, Escape or Enter clears it
}
