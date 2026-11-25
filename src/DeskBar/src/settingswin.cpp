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
    if (!m) if (HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), IMAGE_ICON, px, px, 0)) { m = ImgFromHIcon(h, px); if (id == 2) m = ImgForTheme(m); DestroyIcon(h); }
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

enum { kPgTaskbar, kPgScreens, kPgDeskMode, kPgColours, kPgDrawer, kPgTune, kPgApps, kPgStartup };   // the order of kPages below
void PageTaskbar(View* col) {
    Head(col, 0xE90C, nullptr, L"Taskbar", L"What the bar shows and what the Windows key does.");
    auto* c = Card(col, L"Start");
    Row(c, 0xE71D, L"Windows key opens DeskBar's Start", L"Off: the Windows key opens Windows' own Start.", S.winKeyOpensStart, [](bool v) { S.winKeyOpensStart = v; });
    c = Card(col, L"On the bar");
    Row(c, 0xE83F, L"Battery percentage", L"Next to the battery icon.", S.showBatteryPercent, [](bool v) { S.showBatteryPercent = v; });
    Row(c, 0xE768, L"Media player", L"Song, artist and play controls while something plays.", S.showMedia, [](bool v) { S.showMedia = v; });
    Row(c, 0, L"DeskMode button", L"Next to the keyboard language; opens DeskMode.", S.dmButton, [](bool v) { S.dmButton = v; for (auto* b : AllBars()) if (b->dmBtn) b->dmBtn->Show(v); }, IconRes(2, 32));
    for (int i = 0; i < kWidgetCount; i++) { ws id = kWidgets[i].id; Row(c, 0xE721, L"Search box", id == L"search" ? ws(L"Next to the Windows key. Type, press Enter: searches Google.") : ws(kWidgets[i].name), HasWidget(id), [id](bool v) { SetWidget(id, v); }); }
    c = Card(col, L"Clipboard");
    Row(c, 0xE77F, L"Clipboard history", L"Keeps the last 25 texts you copied, until you sign out. Pin what you use often. Copies from password managers are never kept.", S.clipHistory, [](bool v) { ClipSetOn(v); });
    Row(c, 0xE765, L"Windows key + V opens it", L"Off: Windows key + V opens Windows' own clipboard.", S.clipWinV, [](bool v) { S.clipWinV = v; });
    Sep(c);
    { auto* r = MkBox(c, Lay::H); r->pad = Thick(12, 10); r->gap = 10;
      auto* d = Dim(r, L"Pinned snippets are saved on this PC, locked to your Windows account.", 13); d->star = 1; d->va = Al::Center;
      Btn(r, L"Open", 0xE77F, false, [] { if (Bar* b = BarHere()) OpenClipboardPanel(b); })->va = Al::Center;
      Btn(r, L"Clear history", 0xE74D, false, [] { ClipClearHistory(); })->va = Al::Center; }
    c = Card(col, L"Effects");
    Row(c, 0xE71E, L"Magnify apps under the mouse", L"The icon you point at grows smoothly, the others stay put.", S.hoverMagnify, [](bool v) { S.hoverMagnify = v; for (auto* b : AllBars()) for (auto* t : b->btns) t->Invalidate(); });
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
        MkLabel(b, L"Open DeskMode", FText(13.5f, 600), Raw(0x0B3A66)); b->onClick = [] { if (Bar* bb = BarHere()) OpenDeskModePanel(bb); };
    }
    auto* c = Card(col, L"By itself");
    Row(c, 0xE945, L"Go max when docked", L"A monitor and the charger connected: every screen at its top rate, Best performance.", S.dmAutoMax, [](bool v) { S.dmAutoMax = v; });
    Row(c, 0xE7A7, L"Undo when undocked", L"Monitor or charger unplugged: the screens and power mode go back to how they were.", S.dmRestore, [](bool v) { S.dmRestore = v; });
    Row(c, 0xE7FC, L"Hands off during games", L"Nothing changes by itself while a full-screen game is up.", S.dmGameLock, [](bool v) { S.dmGameLock = v; });
    c = Card(col, L"Presets");
    LinkRow(c, 0xE713, L"Choose what Go max and Go easy do", L"The rate for each screen, the power mode, Energy saver.", [] {
        Bar* bb = BarHere(); if (!bb) return; OpenDeskModePanel(bb); if (auto* q = (QuickPanel*)Panel_(Panel::Quick)) if (q->shown) q->ShowDetail(MakeDeskModeSettings(q), true); });
    c = Card(col, L"Shortcuts");
    auto key = [&](const ws& what, const wchar_t* k) { Sep(c); auto* r = MkBox(c, Lay::H); r->pad = Thick(12, 9); auto* l = MkLabel(r, what, FText(13.5f)); l->star = 1; l->va = Al::Center; Keys(r, {L"Ctrl", L"Alt", k}); };
    key(L"Open DeskMode", L"D"); key(L"Go max", L"M"); key(L"Go easy", L"B"); key(L"Borderless full screen for the window in front", L"F");
}

void PageStartup(View* col) {
    Head(col, 0xE7E8, nullptr, L"Start-up and updates", L"When DeskBar starts, and keeping it current.");
    auto* c = Card(col, L"Starting up");
    auto* sign = Row(c, 0xE7E8, L"Start DeskBar when I sign in", L"Runs as a scheduled task, so it starts before the desktop shows.", true, [](bool on) { std::thread([on] { SetSignInTask(on); }).detach(); });
    static int signGen = 0; int gen = ++signGen;   // the page can be rebuilt (and this switch deleted) while PowerShell answers: only the newest build may be touched
    std::thread([sign, gen] { ws s = SignInTaskState(); bool on = s == L"?" || (!s.empty() && !ContainsI(s, L"Disabled"));
        UiPost([sign, on, gen] { if (gen == signGen && g_sw && g_sw->hwnd && g_cur == kPgStartup) { sign->Set(on); sign->Invalidate(); } }); }).detach();
    c = Card(col, L"Updates");
    Row(c, 0xE895, L"Look for updates once a day", L"Only looks. Nothing installs until you press Update.", S.updateCheck, [](bool v) { S.updateCheck = v; });
    Sep(c);
    auto* st = MkBox(c, Lay::H); st->pad = Thick(12, 10); st->gap = 10;
    auto* us = Dim(st, UpdateStatus().empty() ? ws(L"Not checked yet.") : UpdateStatus(), 13); us->star = 1; us->va = Al::Center;
    Btn(st, L"Check now", 0xE72C, false, [] { UpdateCheck(); })->va = Al::Center;
    g_pageUpd = Btn(st, L"Update now", 0xE896, true, [] { UpdateNow(); }); g_pageUpd->va = Al::Center; g_pageUpd->visible = UpdateNewer();
    g_updStatus = us;
    c = Card(col, L"Your settings");
    auto* sx = MkBox(c, Lay::H); sx->pad = Thick(12, 10); sx->gap = 10;
    auto* sm = Dim(sx, L"Everything you set in DeskBar, DeskDrawer and DeskTune, kept in Documents\\Desk settings: for a new PC or before a reinstall.", 13); sm->star = 1; sm->va = Al::Center; sm->wrap = true;
    Btn(sx, L"Save", 0xE74E, false, [sm] { sm->Set(DeskSettingsExport()); })->va = Al::Center;
    Btn(sx, L"Bring back", 0xE777, false, [sm] { sm->Set(DeskSettingsImport()); })->va = Al::Center;
    c = Card(col, L"Help make it better");
    Row(c, 0xEBE8, L"Report a bug", L"Adds \"Report a bug to Claude\" to the bar's right-click menu. It sends what you type, with DeskBar's recent log, to the developer. Nothing is sent unless you press Send.", S.bugReports, [](bool v) { S.bugReports = v; });
    c = Card(col, L"DeskBar itself");
    auto* b = MkBox(c, Lay::H); b->pad = Thick(10); b->gap = 8;
    Btn(b, L"Restart DeskBar", 0xE777, false, [] { AppQuit(true); });
    Btn(b, L"Quit", 0xE7E8, false, [] { AppQuit(false); });
    auto* q = Dim(c, L"Quitting gives Windows its own taskbar back.", 12); q->margin = Thick(12, 0, 12, 10);
}

void PageApps(View* col) {
    Head(col, 0xE71D, nullptr, L"Desk apps", L"The whole Desk suite in one place: install, update and open each app.");
    auto* c = Card(col, L"One look");
    auto* t = Dim(c, L"The colour theme you pick in Quick settings, or in DeskDrawer's View menu, is worn by all three apps.", 13); t->margin = Thick(12, 10, 12, 10);
    c = Card(col, L"Apps");
    for (int i = 0; i < DeskAppCount(); i++) {
        ws name = DeskAppName(i), state = DeskAppState(name); bool have = FileExists(DeskAppExe(name));
        auto* r = RowBase(c, DeskAppGlyph(i), nullptr, name, DeskAppWhat(i));
        if (have) Btn(r, L"Open", 0xE8A7, false, [name] { Launch(DeskAppExe(name)); })->va = Al::Center;
        if (!have || ContainsI(state, L"new version")) Btn(r, have ? L"Update" : L"Install", have ? 0xE896 : 0xE896, true, [name] { DeskAppInstall(name); })->va = Al::Center;
        auto* s = Dim(c, state.empty() ? ws(have ? L"Installed" : L"Not installed") : state, 12); s->margin = Thick(58, 0, 12, 10);
    }
    auto* b = MkBox(col, Lay::H); b->margin = Thick(0, 6, 0, 0); Btn(b, L"Look for updates", 0xE72C, false, [] { DeskAppsCheck(); });
    if (DeskAppState(DeskAppName(0)).empty()) UiPost([] { DeskAppsCheck(); });   // first visit: look once
}

// ---- Colours: one for the suite, or one per app (deskapps.cpp keeps them in %LOCALAPPDATA%\Desk)
D2D1_COLOR_F SwatchColour(const ThemeDef& t) {   // what a theme looks like, as one dot
    if (t.kind == 1) return Raw(0x1A1A1A); if (t.kind == 2) return Raw(0xF2F2F2); if (t.kind == 3) return Raw(0x9AA4B4, 0x90);
    return ColFor(t, 0x1167C4);
}
void Swatches(View* col, const ws& title, const ws& app) {
    auto* c = Card(col, title); ws cur = ThemeOf(app);
    auto* row = MkBox(c, Lay::Wrap); row->gap = 10; row->pad = Thick(10, 10, 10, 6);
    bool known = false;
    for (int i = 0; i < kThemeCount; i++) {
        const ThemeDef& t = kThemes[i]; ws id = t.id; bool on = cur == id || StartsI(cur, id + L":"); known |= on;
        auto* sw = MkBox(row, Lay::Overlay); sw->fw = sw->fh = 34; sw->radius = 17; sw->bg = SolidC(SwatchColour(t)); sw->hit = true; sw->tip = t.name;
        sw->borderW = on ? 3 : 1; sw->border = on ? Raw(0xFFFFFF) : Raw(0xFFFFFF, 0x30);
        sw->onClick = [app, id] { ThemeSet(app, id); UiPost([] { Show(kPgColours); }); };
    }
    ws name = known ? ws() : cur.empty() ? ws(L"the bar's colour") : StartsI(cur, L"custom") ? ws(L"a custom colour") : StartsI(cur, L"accent") ? ws(L"Windows' accent colour") : cur;
    auto* d = Dim(c, known ? ws(L"Point at a dot for its name.") : L"Now: " + name + L". Any colour and fades: Quick settings > Colour.", 12); d->margin = Thick(12, 0, 12, 10);
}
void PageColours(View* col) {
    Head(col, 0xE790, nullptr, L"Colours", L"One colour for the whole suite, or a different one for each app.");
    bool sep = ThemesSeparate();
    auto* c = Card(col, L"");
    Row(c, 0xE8B9, L"Same colour for every app", L"Off: DeskBar, DeskMode, DeskDrawer and DeskTune can each wear their own.", !sep, [](bool v) { ThemesSetSeparate(!v); UiPost([] { Show(kPgColours); }); });
    if (!sep) Swatches(col, L"Every app", L"");
    else { const wchar_t* ids[] = {L"deskbar", L"deskmode", L"deskdrawer", L"desktune"}, *names[] = {L"DeskBar", L"DeskMode", L"DeskDrawer", L"DeskTune"}; for (int i = 0; i < 4; i++) Swatches(col, names[i], ids[i]); }
    auto* n = Dim(col, L"DeskBar and DeskMode take a second to restart into a new colour; this window comes back.", 12); n->margin = Thick(4, 4, 4, 0);
}
// ---- DeskDrawer and DeskTune: their settings, applied by the apps themselves (deskapps.cpp)
bool NotInstalled(View* col, const ws& name) {
    if (FileExists(DeskAppExe(name))) return false;
    auto* c = Card(col, L""); auto* r = RowBase(c, 0xE896, nullptr, name + L" isn't installed", L"Install it from the newest Desk release.");
    Btn(r, L"Install", 0xE896, true, [name] { DeskAppInstall(name); UiPost([] { Show(kPgApps); }); })->va = Al::Center; return true;
}
void PageDrawer(View* col) {
    Head(col, 0xEC50, nullptr, L"DeskDrawer", L"Files and folders. Changes reach DeskDrawer right away.");
    if (NotInstalled(col, L"DeskDrawer")) return;
    Json j = DeskDrawerSettings(); auto B = [&](const wchar_t* k, bool def) { auto* v = j.Find(k); return v && v->t == Json::Bool ? v->b : def; };
    auto set = [](const wchar_t* k) { ws key = k; return [key](bool v) { DeskDrawerSet(key, v ? L"1" : L"0"); }; };
    auto* c = Card(col, L"Instead of File Explorer");
    Row(c, 0xE8B7, L"Open folders with DeskDrawer", L"Folders, drives and Win+E open in DeskDrawer. Off gives them back to File Explorer.", DeskDrawerIsDefault(), set(L"Default"));
    Row(c, 0xE8E5, L"Answer other apps' Open and Save", L"When Word, Chrome or any app asks where to open or save, DeskDrawer answers instead of Windows' window.", B(L"TakeOverDialogs", false), set(L"TakeOverDialogs"));
    c = Card(col, L"What you see");
    Row(c, 0xE7B3, L"Show hidden items", L"Hidden and system files are listed, dimmed. Ctrl+H flips it from any folder.", B(L"ShowHidden", false), set(L"ShowHidden"));
    c = Card(col, L"Speed");
    Row(c, 0xE945, L"Keep ready in the background", L"Closing the window keeps DeskDrawer out of sight, so the next folder opens in about half a second. Uses some memory.", B(L"KeepReady", true), set(L"KeepReady"));
    {
        RowBase(c, 0xE8CB, nullptr, L"Scrolling speed", L"How far a turn of the wheel or a touchpad swipe moves the folder.");
        double speeds[] = {0.4, 0.7, 1, 1.5}; double now = 1; if (auto* v = j.Find(L"ScrollSpeed")) if (v->t == Json::Num) now = v->n;
        int chosen = 2; for (int i = 0; i < 4; i++) if (fabs(speeds[i] - now) < 0.01) chosen = i;
        auto* sw = MkBox(c, Lay::V); sw->margin = Thick(58, 0, 12, 10);
        dm::Strip(sw, {L"Slower", L"Slow", L"Normal", L"Fast"}, chosen, 4, [](int i) { const wchar_t* v[] = {L"0.4", L"0.7", L"1", L"1.5"}; DeskDrawerSet(L"ScrollSpeed", v[i]); UiOnce(400, [] { Show(kPgDrawer); }); });
    }
    Row(c, 0xE7F4, L"Use the graphics card", L"Smoother on very large screens, about 85 MB more memory. From DeskDrawer's next start.", B(L"Gpu", false), set(L"Gpu"));
    c = Card(col, L"");
    LinkRow(c, 0xE8A7, L"Open DeskDrawer", L"", [] { Launch(DeskAppExe(L"DeskDrawer")); });
}
void PageTune(View* col) {
    Head(col, 0xE9F5, nullptr, L"DeskTune", L"Makes Windows lighter. Everything it changes is written down and can be put back.");
    if (NotInstalled(col, L"DeskTune")) return;
    auto* c = Card(col, L"Your changes");
    LinkRow(c, 0xE8A7, L"Open DeskTune", L"Choose what to switch off.", [] { Launch(DeskAppExe(L"DeskTune")); });
    LinkRow(c, 0xE777, L"Put everything back", L"Opens DeskTune and undoes every change it made (it asks first).", [] { Launch(DeskAppExe(L"DeskTune"), L"--undoall"); });
    c = Card(col, L"Administrator");
    LinkRow(c, 0xE7EF, L"Unlock services", L"Opens DeskTune as administrator: services and machine-wide switches need it.", [] { Launch(DeskAppExe(L"DeskTune"), L"", true); });
}

const struct { wchar_t glyph; int icon; const wchar_t* name; void (*build)(View*); } kPages[] = {
    {0xE90C, 0, L"Taskbar", PageTaskbar}, {0xE7F4, 0, L"Screens and windows", PageScreens}, {0, 2, L"DeskMode", PageDeskMode},
    {0xE790, 0, L"Colours", PageColours}, {0xEC50, 0, L"DeskDrawer", PageDrawer}, {0xE9F5, 0, L"DeskTune", PageTune},
    {0xE71D, 0, L"Desk apps", PageApps}, {0xE7E8, 0, L"Start-up and updates", PageStartup}};
void DeskAppsRefresh() { if (g_sw && g_sw->hwnd && g_cur == kPgApps) Show(kPgApps); }

void Show(int page) {
    if (!g_sw || !g_sw->hwnd) return;
    g_cur = page; g_pageUpd = nullptr; g_updStatus = nullptr;   // the old page's views go away below
    for (int i = 0; i < (int)g_nav.size(); i++) { bool on = i == page; g_nav[i]->bg = on ? Fill(0x14) : Brush{}; g_nav[i]->kids[0]->visible = on; g_nav[i]->kids[0]->bg = on ? dm::Glow() : Brush{}; }
    if (g_scroll->content) g_scroll->Remove(g_scroll->content);
    auto* col = MkBox(nullptr, Lay::V); col->margin = Thick(32, 28, 32, 28); kPages[page].build(col);
    g_scroll->content = g_scroll->Add(col); g_scroll->ScrollTo(0, false); g_sw->Relayout();
}
}

// 860 wide, 460..680 tall (layout units), centred in the work area, never bigger than the screen: 860 at 150% is 1290 px, wider than a 1280 x 800 tablet, and at 225% wider than a 1080p screen
RECT SettingsRectOn(const RECT& wk, float sc) {
    int ww = wk.right - wk.left, wh = wk.bottom - wk.top, m = (int)(16 * sc);
    int w = std::min((int)(860 * sc), ww - 2 * m), h = std::min((int)(std::clamp(wh / sc - 120, 460.f, 680.f) * sc), wh - 2 * m);
    int x = wk.left + (ww - w) / 2, y = wk.top + (wh - h) / 2; return {x, y, x + w, y + h};
}
void OpenSettingsWindow() {
    if (g_sw && g_sw->hwnd) { ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd); return; }
    g_sw = std::make_unique<SettingsWnd>(); POINT at{}; GetCursorPos(&at);   // opens on the screen you clicked on
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); float sc = dx / 96.f;
    RECT sr = SettingsRectOn(mi.rcWork, sc); int x = sr.left, y = sr.top, w = sr.right - sr.left, h = sr.bottom - sr.top;
    g_sw->followDpi = true;   // you can drag it to another screen: take the size Windows suggests for that screen's scaling, or the page is cut off (or swims in space) there
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
    for (int i = 0; i < (int)(sizeof kPages / sizeof kPages[0]); i++) {
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
    { auto* w = LinkRow(rail, 0xE713, L"Windows Settings", L"", [] { Launch(L"ms-settings:"); }); w->margin = Thick(0, 0, 0, 8); }   // the gears open this window now: Windows' own stays a click away
    auto* foot = Dim(rail, L"Changes apply right away.", 12); foot->margin = Thick(8, 0, 0, 0);
    { auto* d = MkBox(main); d->fw = 1; d->bg = Fill(0x0E); }
    g_scroll = main->Add(new Scroll()); g_scroll->star = 1;
    int start = 0; wchar_t e[8]{}; if (GetEnvironmentVariableW(L"DB3_SETPAGE", e, 8)) start = std::clamp(_wtoi(e), 0, 3);   // dev: which page a snapshot shows
    Show(start);
    ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd);
    g_sw->onDeactivate = nullptr;
}
void SettingsSnapshot(const ws& file) { if (g_sw && g_sw->hwnd) g_sw->Snapshot(file); }   // dev: --open settings --snap ms

void DeskAppsChanged() { DeskAppsRefresh(); }   // deskapps.cpp: an app's state changed
int PageNamed(const ws& n) { return n == L"colours" ? kPgColours : n == L"drawer" ? kPgDrawer : n == L"tune" ? kPgTune : n == L"apps" ? kPgApps : n == L"deskmode" ? kPgDeskMode : kPgTaskbar; }
void SettingsShowPage(const ws& name) { Show(PageNamed(name)); }   // dev snapshots: --open settings --page colours
// Another Desk app's Settings (DeskDrawer, DeskTune) opens this window at its page: one settings window for the suite. code: 1 DeskDrawer, 2 DeskTune, 3 Colours.
// The codes are the other apps' (Theme.cs DeskSettings.Open: "deskdrawer" 1, "desktune" 2, "colours" 3); 4 is DeskMode's own gear; anything else the Taskbar page.
int SettingsPageForCode(int code) { return code == 1 ? kPgDrawer : code == 2 ? kPgTune : code == 3 ? kPgColours : code == 4 ? kPgDeskMode : kPgTaskbar; }
void SettingsOpenFromApp(int code) { OpenSettingsWindow(); Show(SettingsPageForCode(code)); if (g_sw && g_sw->hwnd) ForceForeground(g_sw->hwnd); }
// A colour change restarts DeskBar: the window comes back on the page it was on
void SettingsReopenAfterRestart() { if (g_sw && g_sw->hwnd && IsWindowVisible(g_sw->hwnd)) WriteAtomic(DataPath(L"reopen-settings.txt"), std::to_string(g_cur)); }
void SettingsReopenCheck() {
    std::string d; ws p = DataPath(L"reopen-settings.txt"); if (!ReadAll(p, d)) return; DeleteFileW(p.c_str());
    int page = atoi(d.c_str()); if (page < 0 || page >= (int)(sizeof kPages / sizeof kPages[0])) return;
    OpenSettingsWindow(); Show(page);
}
