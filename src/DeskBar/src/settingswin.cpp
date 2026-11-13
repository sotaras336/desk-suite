#include "pch.h"
#include "bar.h"
#include "flyout.h"
#include "quick.h"
#include "widgets.h"
#include "dmstyle.h"

// The DeskBar settings window, in DeskMode's look: a rail of four pages on the left, each page a few cards of switches.
// Everything applies the moment it changes; there is no OK button.
namespace {
using dm::Raw; using dm::Fill;
const uint32_t kSoft = 0x7FD3FF;

struct SettingsWnd : Wnd { SettingsWnd() { noActivate = false; } };
std::unique_ptr<SettingsWnd> g_sw;
Scroll* g_scroll = nullptr; std::vector<View*> g_nav; int g_cur = 0;
View *g_railUpd = nullptr, *g_pageUpd = nullptr; Label* g_updStatus = nullptr;   // the Update now buttons (rail: every page) and the Updates page's status line

ImgP IconRes(int id, int px) {   // the exe's own icons: 1 DeskBar, 2 DeskMode
    static std::map<int, ImgP> c; auto& m = c[id * 1000 + px];
    if (!m) if (HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_ICON, px, px, 0)) { m = ImgFromHIcon(h, px); DestroyIcon(h); }
    return m;
}
Pic* MkPic(View* parent, ImgP img, float sz) { auto* p = parent->Add(new Pic()); p->fw = p->fh = sz; p->img = img; p->va = Al::Center; return p; }
Label* Dim(View* parent, const ws& t, float sz = 12.5f) { auto* l = MkLabel(parent, t, FText(sz), Col(K::White, 0x9C)); l->wrap = true; return l; }

// a card of rows; rows are separated by a hairline that starts after the icon
View* Card(View* parent, const ws& title = L"") {
    if (!title.empty()) dm::Title(parent, title)->margin = Thick(4, 10, 0, 8);
    auto* c = dm::Card(parent); c->pad = Thick(6); c->margin = Thick(0, 0, 0, 6); return c;
}
void Sep(View* card) { if (card->kids.empty()) return; auto* d = MkBox(card); d->fh = 1; d->bg = Fill(0x0E); d->margin = Thick(58, 0, 10, 0); }
View* RowBase(View* card, wchar_t glyph, ImgP img, const ws& title, const ws& desc) {
    Sep(card);
    auto* r = MkBox(card, Lay::H); r->gap = 14; r->pad = Thick(10, 10, 12, 10); r->radius = 10;
    auto* disc = MkBox(r, Lay::Overlay); disc->fw = disc->fh = 34; disc->radius = 10; disc->bg = Fill(0x10); disc->va = Al::Center;
    if (img) { auto* p = MkPic(disc, img, 18); p->ha = Al::Center; } else { auto* g = MkGlyph(disc, glyph, 16, Raw(kSoft)); g->ha = g->va = Al::Center; }
    auto* tx = MkBox(r, Lay::V); tx->star = 1; tx->va = Al::Center; tx->gap = 2;
    MkLabel(tx, title, FText(14)); if (!desc.empty()) Dim(tx, desc);
    return r;
}
// a switch row: a tap anywhere on the row flips it
Toggle* Row(View* card, wchar_t glyph, const ws& title, const ws& desc, bool v, std::function<void(bool)> f, ImgP img = nullptr) {
    auto* r = RowBase(card, glyph, img, title, desc); r->hit = true; r->bgHover = Fill(0x08);
    auto* t = r->Add(new Toggle()); t->va = Al::Center; t->Set(v);
    auto fn = [f](bool on) { f(on); SaveSettingsSoon(); };
    t->onChange = fn; r->onClick = [t, fn] { t->Set(!t->on); fn(t->on); };
    return t;
}
// a row that opens something (a chevron instead of a switch)
View* LinkRow(View* card, wchar_t glyph, const ws& title, const ws& desc, std::function<void()> fn, ImgP img = nullptr) {
    auto* r = RowBase(card, glyph, img, title, desc); r->hit = true; r->bgHover = Fill(0x08); r->onClick = std::move(fn);
    auto* g = MkGlyph(r, 0xE76C, 12, Col(K::White, 0x90)); g->va = Al::Center; g->margin = Thick(0, 0, 6, 0);
    return r;
}
View* Btn(View* parent, const ws& text, wchar_t glyph, bool primary, std::function<void()> fn) {
    auto* b = MkBox(parent, Lay::H); b->hit = true; b->radius = 10; b->pad = Thick(16, 9); b->gap = 8; b->onClick = std::move(fn);
    if (primary) { b->bg = dm::Glow(); b->bgHover = dm::Glow(); b->opacity = 1; } else { b->bg = Fill(0x10); b->bgHover = Fill(0x1C); b->borderW = 1; b->border = Raw(0xFFFFFF, 0x14); }
    if (glyph) { auto* g = MkGlyph(b, glyph, 13); g->va = Al::Center; }
    auto* l = MkLabel(b, text, FText(13.5f, primary ? 600 : 400)); l->va = Al::Center; return b;
}
View* Keys(View* parent, std::initializer_list<const wchar_t*> keys) {   // Ctrl + Alt + D, drawn as keycaps
    auto* k = MkBox(parent, Lay::H); k->gap = 4; k->va = Al::Center;
    for (auto* s : keys) { auto* c = MkBox(k); c->radius = 6; c->pad = Thick(8, 3, 8, 4); c->bg = Fill(0x12); c->borderW = 1; c->border = Raw(0xFFFFFF, 0x1C); MkLabel(c, s, FText(12, 600)); }
    return k;
}
// page header: a gradient tile, the page name, one line on what is here
View* Head(View* col, wchar_t glyph, ImgP img, const ws& title, const ws& sub) {
    auto* h = MkBox(col, Lay::H); h->gap = 16; h->margin = Thick(0, 0, 0, 12);
    auto* tile = MkBox(h, Lay::Overlay); tile->fw = tile->fh = 48; tile->radius = 14; tile->bg = dm::Glow(); tile->va = Al::Center;
    if (img) { auto* p = MkPic(tile, img, 26); p->ha = Al::Center; } else { auto* g = MkGlyph(tile, glyph, 22, Raw(0xFFFFFF)); g->ha = g->va = Al::Center; }
    auto* tx = MkBox(h, Lay::V); tx->star = 1; tx->va = Al::Center; tx->gap = 3;
    MkLabel(tx, title, FDisp(24, 600)); Dim(tx, sub, 13);
    return h;
}

void PageTaskbar(View* col) {
    Head(col, 0xE90C, nullptr, L"Taskbar", L"What the bar shows and what the Windows key does.");
    auto* c = Card(col, L"Start");
    Row(c, 0xE71D, L"Windows key opens DeskBar's Start", L"Off: the Windows key opens Windows' own Start.", S.winKeyOpensStart, [](bool v) { S.winKeyOpensStart = v; });
    c = Card(col, L"On the bar");
    Row(c, 0xE83F, L"Battery percentage", L"Next to the battery icon.", S.showBatteryPercent, [](bool v) { S.showBatteryPercent = v; });
    Row(c, 0xE768, L"Media player", L"Song, artist and play controls while something plays.", S.showMedia, [](bool v) { S.showMedia = v; });
    Row(c, 0, L"DeskMode button", L"Next to the keyboard language; opens DeskMode.", S.dmButton, [](bool v) { S.dmButton = v; for (auto* b : AllBars()) if (b->dmBtn) b->dmBtn->Show(v); }, IconRes(2, 32));
    for (int i = 0; i < kWidgetCount; i++) { ws id = kWidgets[i].id; Row(c, 0xE721, L"Search box", id == L"search" ? ws(L"Next to the Windows key. Type, press Enter: searches Google.") : ws(kWidgets[i].name), HasWidget(id), [id](bool v) { SetWidget(id, v); }); }
}

void Show(int page);
void PageScreens(View* col) {
    Head(col, 0xE7F4, nullptr, L"Screens and windows", L"How the mouse and windows move between your screens.");
    auto* c = Card(col, L"The mouse");
    Row(c, 0xE962, L"Same place on the next screen", L"Crossing to a screen of another size, the mouse lands at the same relative height.", S.mouseBridge, [](bool v) { S.mouseBridge = v; });
    {   // edge resistance: a segmented strip under its row
        auto* r = RowBase(c, 0xE7F7, nullptr, L"Edge resistance", L"How hard you push past a screen's edge before the mouse crosses.");
        (void)r; auto* sw = MkBox(c, Lay::V); sw->margin = Thick(58, 0, 12, 10);
        dm::Strip(sw, {L"None", L"Light", L"Medium", L"Strong"}, std::clamp(S.edgeResistance, 0, 3), 4, [](int i) { S.edgeResistance = i; SaveSettingsSoon(); UiPost([] { Show(1); }); });
    }
    c = Card(col, L"Windows");
    Row(c, 0xE740, L"Snap onto one screen", L"A window let go across two screens moves fully onto the one holding most of it.", S.snapOnRelease, [](bool v) { S.snapOnRelease = v; });
    Row(c, 0xE73F, L"Keep the size in proportion", L"A window dragged to a bigger or smaller screen grows or shrinks with it.", S.keepSizeRatio, [](bool v) { S.keepSizeRatio = v; });
    Row(c, 0xE777, L"Rescue windows", L"When a screen is unplugged, its windows come back to one you can see.", S.rescueWindows, [](bool v) { S.rescueWindows = v; });
    Row(c, 0xE8A7, L"Popups follow their window", L"A program's dialogs open on the same screen as the program.", S.popupsFollowOwner, [](bool v) { S.popupsFollowOwner = v; });
}

void PageDeskMode(View* col) {
    Head(col, 0, IconRes(2, 32), L"DeskMode", L"Refresh rate, power mode and brightness for every screen.");
    {   // the hero: open it
        auto* h = MkBox(col, Lay::H); h->radius = 16; h->pad = Thick(18, 16); h->gap = 16; h->margin = Thick(0, 0, 0, 6); h->bg = dm::Glow();
        auto* tx = MkBox(h, Lay::V); tx->star = 1; tx->va = Al::Center; tx->gap = 3;
        MkLabel(tx, L"Go max, Go easy, or pick each screen's rate", FText(15, 600));
        auto* s = MkLabel(tx, L"Opens from the DeskMode button on the taskbar, or Ctrl+Alt+D.", FText(12.5f), Raw(0xFFFFFF, 0xC8)); s->wrap = true;
        auto* b = MkBox(h, Lay::H); b->hit = true; b->radius = 10; b->pad = Thick(16, 9); b->va = Al::Center; b->bg = SolidC(Raw(0xFFFFFF, 0xF0)); b->bgHover = SolidC(Raw(0xFFFFFF));
        MkLabel(b, L"Open DeskMode", FText(13.5f, 600), Raw(0x0B3A66)); b->onClick = [] { if (!AllBars().empty()) OpenDeskModePanel(AllBars()[0]); };
    }
    auto* c = Card(col, L"By itself");
    Row(c, 0xE945, L"Go max when docked", L"A monitor and the charger connected: every screen at its top rate, Best performance.", S.dmAutoMax, [](bool v) { S.dmAutoMax = v; });
    Row(c, 0xE7A7, L"Undo when undocked", L"Monitor or charger unplugged: the screens and power mode go back to how they were.", S.dmRestore, [](bool v) { S.dmRestore = v; });
    Row(c, 0xE7FC, L"Hands off during games", L"Nothing changes by itself while a full-screen game is up.", S.dmGameLock, [](bool v) { S.dmGameLock = v; });
    c = Card(col, L"Presets");
    LinkRow(c, 0xE713, L"Choose what Go max and Go easy do", L"The rate for each screen, the power mode, Energy saver.", [] {
        if (AllBars().empty()) return; OpenDeskModePanel(AllBars()[0]); if (auto* q = (QuickPanel*)Panel_(Panel::Quick)) if (q->shown) q->ShowDetail(MakeDeskModeSettings(q), true); });
    c = Card(col, L"Shortcuts");
    auto key = [&](const ws& what, const wchar_t* k) { Sep(c); auto* r = MkBox(c, Lay::H); r->pad = Thick(12, 9); auto* l = MkLabel(r, what, FText(13.5f)); l->star = 1; l->va = Al::Center; Keys(r, {L"Ctrl", L"Alt", k}); };
    key(L"Open DeskMode", L"D"); key(L"Go max", L"M"); key(L"Go easy", L"B"); key(L"Borderless full screen for the window in front", L"F");
}

void PageStartup(View* col) {
    Head(col, 0xE7E8, nullptr, L"Start-up and updates", L"When DeskBar starts, and keeping it current.");
    auto* c = Card(col, L"Starting up");
    auto* sign = Row(c, 0xE7E8, L"Start DeskBar when I sign in", L"Runs as a scheduled task, so it starts before the desktop shows.", true, [](bool on) { std::thread([on] { SetSignInTask(on); }).detach(); });
    std::thread([sign] { ws s = SignInTaskState(); bool on = s == L"?" || (!s.empty() && !ContainsI(s, L"Disabled"));
        UiPost([sign, on] { if (g_sw && g_sw->hwnd && g_cur == 3) { sign->Set(on); sign->Invalidate(); } }); }).detach();
    c = Card(col, L"Updates");
    Row(c, 0xE895, L"Look for updates once a day", L"Only looks. Nothing installs until you press Update.", S.updateCheck, [](bool v) { S.updateCheck = v; });
    Sep(c);
    auto* st = MkBox(c, Lay::H); st->pad = Thick(12, 10); st->gap = 10;
    auto* us = Dim(st, UpdateStatus().empty() ? ws(L"Not checked yet.") : UpdateStatus(), 13); us->star = 1; us->va = Al::Center;
    Btn(st, L"Check now", 0xE72C, false, [] { UpdateCheck(); })->va = Al::Center;
    g_pageUpd = Btn(st, L"Update now", 0xE896, true, [] { UpdateNow(); }); g_pageUpd->va = Al::Center; g_pageUpd->visible = UpdateNewer();
    g_updStatus = us;
    c = Card(col, L"DeskBar itself");
    auto* b = MkBox(c, Lay::H); b->pad = Thick(10); b->gap = 8;
    Btn(b, L"Restart DeskBar", 0xE777, false, [] { AppQuit(true); });
    Btn(b, L"Quit", 0xE7E8, false, [] { AppQuit(false); });
    auto* q = Dim(c, L"Quitting gives Windows its own taskbar back.", 12); q->margin = Thick(12, 0, 12, 10);
}

const struct { wchar_t glyph; int icon; const wchar_t* name; void (*build)(View*); } kPages[] = {
    {0xE90C, 0, L"Taskbar", PageTaskbar}, {0xE7F4, 0, L"Screens and windows", PageScreens}, {0, 2, L"DeskMode", PageDeskMode}, {0xE7E8, 0, L"Start-up and updates", PageStartup}};

void Show(int page) {
    if (!g_sw || !g_sw->hwnd) return;
    g_cur = page; g_pageUpd = nullptr; g_updStatus = nullptr;   // the old page's views go away below
    for (int i = 0; i < (int)g_nav.size(); i++) { bool on = i == page; g_nav[i]->bg = on ? Fill(0x14) : Brush{}; g_nav[i]->kids[0]->visible = on; g_nav[i]->kids[0]->bg = on ? dm::Glow() : Brush{}; }
    if (g_scroll->content) g_scroll->Remove(g_scroll->content);
    auto* col = MkBox(nullptr, Lay::V); col->margin = Thick(32, 28, 32, 28); kPages[page].build(col);
    g_scroll->content = g_scroll->Add(col); g_scroll->ScrollTo(0, false); g_sw->Relayout();
}
}

void OpenSettingsWindow() {
    if (g_sw && g_sw->hwnd) { ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd); return; }
    g_sw = std::make_unique<SettingsWnd>(); POINT at{}; GetCursorPos(&at);   // opens on the screen you clicked on
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); float sc = dx / 96.f;
    int w = (int)(860 * sc), h = (int)(std::clamp((mi.rcWork.bottom - mi.rcWork.top) / sc - 120, 460.f, 680.f) * sc);
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2, y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    g_sw->Create(0, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, w, h); if (!g_sw->hwnd) return;
    SetWindowTextW(g_sw->hwnd, L"DeskBar settings"); int dark = 1; DwmSetWindowAttribute(g_sw->hwnd, 20, &dark, 4);
    COLORREF cap = RGB(0x20, 0x22, 0x26); DwmSetWindowAttribute(g_sw->hwnd, 35, &cap, 4);   // the title bar in the page's own colour: one surface
    RECT cr; GetClientRect(g_sw->hwnd, &cr); RECT wr; GetWindowRect(g_sw->hwnd, &wr); SetWindowPos(g_sw->hwnd, nullptr, x, y, w + (wr.right - wr.left) - cr.right, h + (wr.bottom - wr.top) - cr.bottom, SWP_NOZORDER);
    View* r = g_sw->root; r->bg = SolidC(Raw(0x202226)); r->lay = Lay::Overlay; r->pad = Thick(0);
    auto* glowA = MkBox(r); glowA->bg = RadialRel(0.0f, 0.0f, 0.55f, 0.45f, {{0, Raw(0x1167C4, 0x90)}, {1, Raw(0x1167C4, 0)}}); glowA->hit = false;   // DeskMode's two soft glows
    auto* glowB = MkBox(r); glowB->bg = RadialRel(1.0f, 1.0f, 0.6f, 0.5f, {{0, Raw(0x1FA7B9, 0x60)}, {1, Raw(0x1FA7B9, 0)}}); glowB->hit = false;
    auto* main = MkBox(r, Lay::H);
    auto* rail = MkBox(main, Lay::V); rail->fw = 232; rail->pad = Thick(16, 20, 14, 18); rail->gap = 4;
    {
        auto* id = MkBox(rail, Lay::H); id->gap = 12; id->margin = Thick(6, 0, 0, 22);
        auto* tile = MkBox(id, Lay::Overlay); tile->fw = tile->fh = 38; tile->radius = 10; tile->bg = SolidB(K::Accent); tile->va = Al::Center;
        { auto* p = MkPic(tile, IconRes(1, 32), 22); p->ha = Al::Center; }
        auto* tx = MkBox(id, Lay::V); tx->va = Al::Center; MkLabel(tx, L"DeskBar", FText(16, 600)); Dim(tx, L"Settings", 12);
    }
    g_nav.clear();
    for (int i = 0; i < 4; i++) {
        auto* n = MkBox(rail, Lay::H); n->hit = true; n->radius = 10; n->pad = Thick(0, 10, 12, 10); n->gap = 12; n->bgHover = Fill(0x0C);
        auto* bar = MkBox(n); bar->fw = 3; bar->fh = 18; bar->radius = 1.5f; bar->va = Al::Center; bar->margin = Thick(0, 0, 0, 0);
        if (kPages[i].icon) { MkPic(n, IconRes(kPages[i].icon, 32), 18); } else { auto* g = MkGlyph(n, kPages[i].glyph, 16, Raw(kSoft)); g->va = Al::Center; g->fw = 18; }
        auto* l = MkLabel(n, kPages[i].name, FText(14)); l->va = Al::Center;
        n->onClick = [i] { Show(i); }; g_nav.push_back(n);
    }
    { auto* sp = MkBox(rail); sp->star = 1; }
    // an update that is out shows on every page, not only on Start-up and updates
    g_railUpd = Btn(rail, L"Update now", 0xE896, true, [] { UpdateNow(); }); g_railUpd->margin = Thick(0, 0, 0, 12); g_railUpd->visible = UpdateNewer() && !UpdateBusy();
    UpdateListen([] {
        if (!g_sw || !g_sw->hwnd) return;
        if (g_railUpd) g_railUpd->Show(UpdateNewer() && !UpdateBusy());
        if (g_pageUpd) g_pageUpd->Show(UpdateNewer());
        if (g_updStatus) g_updStatus->Set(UpdateStatus().empty() ? ws(L"Not checked yet.") : UpdateStatus());
    });
    auto* foot = Dim(rail, L"Changes apply right away.", 12); foot->margin = Thick(8, 0, 0, 0);
    { auto* d = MkBox(main); d->fw = 1; d->bg = Fill(0x0E); }
    g_scroll = main->Add(new Scroll()); g_scroll->star = 1;
    int start = 0; wchar_t e[8]{}; if (GetEnvironmentVariableW(L"DB3_SETPAGE", e, 8)) start = std::clamp(_wtoi(e), 0, 3);   // dev: which page a snapshot shows
    Show(start);
    ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd);
    g_sw->onDeactivate = nullptr;
}
void SettingsSnapshot(const ws& file) { if (g_sw && g_sw->hwnd) g_sw->Snapshot(file); }   // dev: --open settings --snap ms
