#include "pch.h"
#include "quick.h"

// ---------------------------------------------------------------- tile
namespace {
// Two shapes. A connection CARD (Wi-Fi, Bluetooth): wide, the round icon switches it on and off, the rest (name, status, arrow) opens its page. A round
// BUTTON for everything else: filled in the theme's colour when on; a shortcut that only opens something (moreOnly) is drawn as an outline, so it never
// looks like a switch that is off. Set(on, text, enabled) is the same for both: text is the card's status line, or the button's label.
struct Tile : View {
    View *box, *bubble; Glyph* g1; Label *lab, *title = nullptr; bool on = false, card, link; ws offText;
    Tile(wchar_t glyph, const ws& label, std::function<void()> toggle, std::function<void()> more, bool moreOnly = false, bool isCard = false) : card(isCard), link(moreOnly) {
        if (card) {
            lay = Lay::H; fh = 56; radius = 18; pad = Thick(10, 0, 14, 0); gap = 12; hit = true; onClick = more ? more : toggle; offText = L"Off";
            bubble = MkBox(this); bubble->fw = bubble->fh = 36; bubble->radius = 18; bubble->va = Al::Center; bubble->hit = true; bubble->onClick = toggle; bubble->tip = L"Turn " + label + L" on or off";
            g1 = MkGlyph(bubble, glyph, 17); g1->ha = g1->va = Al::Center;
            auto* tx = MkBox(this, Lay::V); tx->star = 1; tx->va = Al::Center;
            title = MkLabel(tx, label, FText(13, 600)); lab = MkLabel(tx, L"", FText(11.5f), Col(K::White, K::Dim));
            auto* a = MkGlyph(this, 0xE76C, 10, Col(K::White, K::Dim)); a->va = Al::Center;
            box = this;
        } else {
            lay = Lay::V; fw = 78; ha = Al::Center; box = this;
            bubble = MkBox(this); bubble->fw = bubble->fh = 50; bubble->radius = 25; bubble->ha = Al::Center; bubble->hit = true; bubble->onClick = moreOnly && more ? more : toggle; bubble->tip = label;
            g1 = MkGlyph(bubble, glyph, 17); g1->ha = g1->va = Al::Center;
            lab = MkLabel(this, label, FText(11.5f)); lab->ta = TA::Center; lab->wrap = true; lab->maxW = 78; lab->margin = Thick(0, 6, 0, 0); lab->ha = Al::Center;
        }
        Set(false, card ? L"" : label);
    }
    void Set(bool o, const ws& text = L"", bool enabled = true) {
        on = o;
        if (on) { bubble->bg = LinearB(0, 0, 1, 1, {{0, Col(K::Accent)}, {1, Col(K::Accent, 0xC8)}}); bubble->bgHover = SolidB(K::Accent); bubble->borderW = 0; }
        else if (link) { bubble->bg = Brush{}; bubble->bgHover = SolidB(K::White, K::CardHover); bubble->borderW = 1.2f; bubble->border = Col(K::White, 0x40); }
        else { bubble->bg = SolidB(K::White, 0x1E); bubble->bgHover = SolidB(K::White, 0x30); bubble->borderW = 0; }
        g1->col = on ? Col(K::Black) : Col(K::White);
        if (card) { bg = Brush{}; bgHover = SolidB(K::White, 0x12); lab->Set(on ? (text.empty() ? ws(L"On") : text) : offText); }   // the row sits on the shared connections surface
        else if (!text.empty()) lab->Set(text);
        opacity = enabled ? 1.f : 0.5f; bubble->enabled = enabled; Invalidate();
    }
};
}

// A thick rounded bar that fills with the theme's colour from the left as far as the value; its icon sits inside the filled end and the value at the right.
// Drag anywhere on it, or turn the mouse wheel over it. With onIcon set, a tap on the icon does that instead (volume: mute).
struct FatSlider : View {
    float value = 0; bool dragging = false, muted = false, iconDown = false; wchar_t glyph; ws text; std::function<void(float)> onChange; std::function<void()> onIcon;
    FatSlider(wchar_t g) : glyph(g) { hit = true; fh = 44; }
    Sz MeasureContent(Sz) override { return {120, 44}; }
    void Set(float v) { if (!dragging && v != value) { value = v; Invalidate(); } }
    float Fill() const { float h = rc.h(); return h + (rc.w() - h) * std::clamp(value, 0.f, 100.f) / 100; }
    void From(float x) { float h = rc.h(), w = rc.w() - h; value = w > 0 ? std::clamp((x - rc.l - h / 2) / w, 0.f, 1.f) * 100 : 0; Invalidate(); if (onChange) onChange(value); }
    bool OnIcon(float x) const { return onIcon && x < rc.l + rc.h(); }
    bool OnDown(float x, float, bool) override { if (!enabled) return false; pressed = true; if (OnIcon(x)) { iconDown = true; return true; } dragging = true; From(x); return true; }
    void OnDrag(float x, float) override { if (dragging) From(x); }
    void OnUp(float x, float) override { pressed = false; if (iconDown) { iconDown = false; if (OnIcon(x) && onIcon) onIcon(); Invalidate(); return; } if (dragging) { From(x); dragging = false; } }
    bool OnWheel(float d, float, float) override { if (!enabled) return false; value = std::clamp(value + WheelPercent(d), 0.f, 100.f); Invalidate(); if (onChange) onChange(value); return true; }
    void PaintContent(Gfx& g) override {
        float h = rc.h(), r = h / 2, fill = Fill(); float oa = g.alpha; if (!enabled) g.alpha *= 0.5f;
        g.FillC(rc, Col(K::White, hovered ? 0x26 : 0x1C), r);
        Rf f{rc.l, rc.t, rc.l + fill, rc.b};
        if (muted) g.FillC(f, Col(K::White, 0x30), r); else g.Fill(f, LinearB(0, 0.5f, 1, 0.5f, {{0, Col(K::Accent, 0xD8)}, {1, Col(K::Accent)}}), r);
        { float x = rc.l + fill - 12; if (x > rc.l + h && (text.empty() || x < rc.r - 72)) g.FillC({x - 1.5f, rc.t + 13, x + 1.5f, rc.b - 13}, Col(K::Black, pressed && !iconDown ? 0x90 : 0x50), 1.5f); }   // a slim grip at the end of the fill
        g.Glyph(glyph, {rc.l, rc.t, rc.l + h, rc.b}, 16, muted ? Col(K::White) : Col(K::Black));
        bool onFill = rc.l + fill > rc.r - 56;
        if (!text.empty()) g.Text(text, {rc.r - 66, rc.t, rc.r - 16, rc.b}, FText(12, 600), onFill && !muted ? Col(K::Black) : Col(K::White, K::Dim), TA::Right);
        g.alpha = oa;
    }
};
// What is playing (any app that reports to Windows' media controls): cover, title, artist, and the three buttons. Hidden when nothing plays.
struct NowPlaying : View {
    Pic* art; Label *title, *artist; Glyph* playG; ws key;
    NowPlaying() {
        lay = Lay::H; radius = 18; pad = Thick(8, 8, 8, 8); gap = 12; bg = SolidB(K::White, 0x12); margin = Thick(0, 0, 0, 10); visible = false;
        auto* frame = MkBox(this); frame->fw = frame->fh = 46; frame->radius = 10; frame->clip = true; frame->bg = SolidB(K::White, 0x1C); frame->va = Al::Center;
        { auto* g = MkGlyph(frame, 0xE8D6, 18, Col(K::White, K::Dim)); g->ha = g->va = Al::Center; }
        art = frame->Add(new Pic()); art->fw = art->fh = 46;
        auto* tx = MkBox(this, Lay::V); tx->star = 1; tx->va = Al::Center; tx->hit = true; tx->tip = L"Open the app that is playing";
        tx->onClick = [] { ClosePanels(); MediaOpenSource(); };
        title = MkLabel(tx, L"", FText(13, 600)); artist = MkLabel(tx, L"", FText(11.5f), Col(K::White, K::Dim));
        auto btn = [&](wchar_t gl, float sz, int cmd, const wchar_t* tip) { auto* b = IconBtn(this, gl, 34, sz, [cmd] { MediaCmd(cmd); UiOnce(300, [] { SysNotify(); }); }); b->bg = Brush{}; b->va = Al::Center; b->tip = tip; return b; };
        btn(0xE892, 12, 2, L"Previous"); auto* p = btn(0xE768, 15, 0, L"Play / pause"); playG = (Glyph*)p->kids[0]; p->bg = SolidB(K::White, 0x22); btn(0xE893, 12, 1, L"Next");
    }
    void Update(bool room) {
        MediaState m = Media();
        if (GetEnvironmentVariableW(L"DB3_FAKE_MEDIA", nullptr, 0)) { m.active = true; m.playing = true; m.title = L"Midnight Drive"; m.artist = L"Lumen Park"; }   // dev: see the card in a snapshot
        bool show = room && m.active && !m.title.empty();
        if (show != visible) { visible = show; Relayout(); } if (!show) return;
        title->Set(m.title); artist->Set(m.artist.empty() ? ws(L"Playing") : m.artist); playG->Set(m.playing ? 0xE769 : 0xE768);
        if (art->img != m.art) { art->img = m.art; Invalidate(); }
    }
};

constexpr float kRowH = 86;   // one row of round buttons with their label
struct QuickPanel::Impl {
    Tile *wifi, *bt, *studio, *air, *rot, *saver, *night, *hotspot, *cast;
    Scroll* tiles; View *grid1, *dot1, *dot2; FatSlider *vol, *bright = nullptr; Glyph* volG = nullptr; View* brightRow = nullptr; Glyph *batG, *batFill; Label* batT; Glyph* brightG = nullptr; NowPlaying* media = nullptr; bool mediaRoom = true;
    ws brightFor; HMONITOR monitor = nullptr; UINT_PTR brightTimer = 0;
    bool tune = false;   // DeskTune is installed: its memory tile sits where Studio effects was
};

View* PageHeader(View* parent, const ws& title, std::function<void()> back, Toggle** toggle) {
    auto* h = MkBox(parent, Lay::H); h->margin = Thick(0, 0, 0, 12); h->fh = 38;
    auto* b = IconBtn(h, 0xE72B, 34, 13, std::move(back)); b->va = Al::Center;
    auto* t = MkLabel(h, title, FDisp(19, 600)); t->margin = Thick(12, 0, 0, 0); t->va = Al::Center; t->star = 1;
    if (toggle) { auto* tg = h->Add(new Toggle()); tg->va = Al::Center; *toggle = tg; }
    return h;
}

QuickPanel::QuickPanel() : Flyout(Panel::Quick, 360, true) {
    d = new Impl(); root->hit = true;
    pages = MkBox(root); main = MkBox(pages, Lay::V); main->pad = Thick(20, 16, 20, 6);
    detail = pages->Add(new Scroll()); detail->visible = false; detail->pad = Thick(0, 0, 2, 0);

    auto mk = [](wchar_t g, const ws& l, std::function<void()> t, std::function<void()> m = {}, bool mo = false, bool card = false) { return new Tile(g, l, std::move(t), std::move(m), mo, card); };
    auto closeAnd = [](const wchar_t* uri) { return [uri] { ClosePanels(); Launch(uri); }; };
    // the keys are copied into the closure: an initializer_list only points at a temporary array, which is gone by the time the tile is pressed
    auto win = [](std::initializer_list<WORD> k) { std::vector<WORD> keys(k); return [keys] { ClosePanels(); UiOnce(150, [keys] { if (keys.size() == 2) SendKeys({keys[0], keys[1]}); else if (keys.size() == 3) SendKeys({keys[0], keys[1], keys[2]}); }); }; };
    // a switch that Windows refused must say so: a second and a half after the press the real state is read back, and a tile that did not change shows it for a moment
    auto expect = [this](Tile** tile, bool want, std::function<int()> read, const wchar_t* what) {
        UiOnce(1500, [this, tile, want, read, what] { int now = read(); if (now < 0 || (now == 1) == want) return; Log(L"quick settings: %s did not switch %s", what, want ? L"on" : L"off"); (*tile)->Set(now == 1, L"Couldn't change"); UiOnce(2500, [this, tile, what] { (*tile)->Set((*tile)->on, what); Refresh(); }); });
    };
    d->wifi = mk(0xE701, L"Wi-Fi", [this] { NetSetWifi(!d->wifi->on); }, [this] { ShowDetail(MakeWifiPage(this)); }, false, true);
    d->bt = mk(0xE702, L"Bluetooth", [this] { NetSetBt(!d->bt->on); }, [this] { ShowDetail(MakeBtPage(this)); }, false, true);
    ws tuneExe = DeskAppExe(L"DeskTune");   // the one place that knows where DeskTune installs (deskapps.cpp)
    d->tune = GetFileAttributesW(tuneExe.c_str()) != INVALID_FILE_ATTRIBUTES;
    d->studio = d->tune ? mk(0xE9D9, L"DeskTune", [tuneExe] { ClosePanels(); Launch(tuneExe); })
                        : mk(0xE714, L"Studio effects", closeAnd(L"ms-settings:camera"), closeAnd(L"ms-settings:camera"), true);
    d->air = mk(0xE709, L"Airplane mode", [this, expect] { bool on = !d->air->on; d->air->Set(on); NetSetAirplane(on); expect(&d->air, on, [] { NetRefresh(); return (int)NetSnap().airplane; }, L"Airplane mode"); });
    d->rot = mk(0xE7AD, L"Rotation lock", [this, expect] { int l = RotationLocked(); if (l >= 0) { RotationSetLocked(!l); expect(&d->rot, !l, [] { return RotationLocked(); }, L"Rotation lock"); } Refresh(); });
    d->saver = mk(0xEBB6, L"Energy saver", [this, expect] { bool on = !d->saver->on; d->saver->Set(on); std::thread([on] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); EnergySaverSet(on); SysPoke(); }).detach(); expect(&d->saver, on, [] { return (int)Battery().saver; }, L"Energy saver"); });
    d->night = mk(0xE708, L"Night light", [this, expect] { bool on = !d->night->on; if (NightLightSet(on)) d->night->Set(on); expect(&d->night, on, [] { return NightLightIsOn(); }, L"Night light"); });
    d->hotspot = mk(0xE704, L"Mobile hotspot", [this] { bool on = !d->hotspot->on; d->hotspot->Set(on); NetHotspot(false, on, [this, on](int r) { if (r < 0) { d->hotspot->Set(!on); ClosePanels(); Launch(L"ms-settings:network-mobilehotspot"); } }); });
    d->cast = mk(0xEC15, L"Cast", win({VK_LWIN, 'K'}), win({VK_LWIN, 'K'}), true);   // these open something (a Windows panel or a Settings page): drawn with the arrow, not as a switch
    // what is playing, when something is; then Wi-Fi and Bluetooth as two rows of one surface (side by side, their names were cut to a few letters);
    // then the switches, four to a row: two rows show, the rest is a scroll away
    d->media = new NowPlaying(); main->Add(d->media);
    auto* conn = MkBox(main, Lay::V); conn->bg = SolidB(K::White, 0x12); conn->radius = 18; conn->margin = Thick(0, 0, 0, 12);
    conn->Add(d->wifi); { auto* ln = MkBox(conn); ln->fh = 1; ln->bg = SolidB(K::White, K::CardLine); ln->margin = Thick(58, 0, 14, 0); } conn->Add(d->bt);
    Tile* top[] = {d->air, d->saver, d->night, d->rot, d->hotspot, d->studio, d->cast};
    Tile* extra[] = {mk(0xE776, L"Accessibility", closeAnd(L"ms-settings:easeofaccess"), closeAnd(L"ms-settings:easeofaccess"), true), mk(0xE7F4, L"Project", win({VK_LWIN, 'P'}), win({VK_LWIN, 'P'}), true), mk(0xE72D, L"Nearby sharing", closeAnd(L"ms-settings:crossdevice"), closeAnd(L"ms-settings:crossdevice"), true),
                     mk(0xE705, L"VPN", closeAnd(L"ms-settings:network-vpn"), closeAnd(L"ms-settings:network-vpn"), true), mk(0xE823, L"Focus", closeAnd(L"ms-settings:quietmomentshome"), closeAnd(L"ms-settings:quietmomentshome"), true), mk(0xE7F0, L"Live captions", win({VK_LWIN, VK_CONTROL, 'L'}), win({VK_LWIN, VK_CONTROL, 'L'}), true)};
    d->tiles = main->Add(new Scroll()); auto* content = MkBox(nullptr, Lay::V); d->tiles->content = d->tiles->Add(content);
    d->grid1 = MkBox(content, Lay::Uniform); d->grid1->cols = 4; d->grid1->cellH = kRowH; for (auto* t : top) d->grid1->Add(t); for (auto* t : extra) d->grid1->Add(t);
    // brightness and volume: two fat bars, each with its value inside; the round button beside volume opens the sound page (outputs, mixer)
    d->brightRow = MkBox(main, Lay::H); d->brightRow->margin = Thick(0, 4, 0, 8);
    d->bright = d->brightRow->Add(new FatSlider(0xE706)); d->bright->star = 1; d->bright->enabled = false;
    {   // beside brightness: the display page (refresh rate, screen modes), so both bars have a button and line up
        auto* dp = MkBox(d->brightRow); dp->fw = dp->fh = 44; dp->radius = 22; dp->va = Al::Center; dp->hit = true; dp->bg = SolidB(K::White, 0x1C); dp->bgHover = SolidB(K::White, 0x30); dp->tip = L"Display"; dp->margin = Thick(8, 0, 0, 0);
        auto* a = MkGlyph(dp, 0xE7F4, 15); a->ha = a->va = Al::Center; dp->onClick = [this] { ShowDetail(MakeDisplayPage(this), true); };
    }
    auto* vr = MkBox(main, Lay::H); vr->margin = Thick(0, 0, 0, 12);
    d->vol = vr->Add(new FatSlider(0xE995)); d->vol->star = 1; d->vol->tip = L"Volume (tap the speaker to mute)";
    d->vol->onChange = [this](float v) { AudioSetVolume(v / 100); d->vol->muted = false; d->vol->text = Fmt(L"%d%%", (int)lroundf(v)); };
    d->vol->onIcon = [] { AudioToggleMute(); UiOnce(150, [] { SysNotify(); }); };
    auto* mx = MkBox(vr); mx->fw = mx->fh = 44; mx->radius = 22; mx->va = Al::Center; mx->hit = true; mx->bg = SolidB(K::White, 0x1C); mx->bgHover = SolidB(K::White, 0x30); mx->tip = L"Sound output and volume mixer"; mx->margin = Thick(8, 0, 0, 0);
    { auto* a = MkGlyph(mx, 0xE9E9, 15); a->ha = a->va = Al::Center; }
    mx->onClick = [this] { ShowDetail(MakeSoundPage(this)); };
    // footer
    auto* footBox = MkBox(root); footBox->fh = 48; this->foot = footBox;
    auto* foot = MkBox(footBox, Lay::H); foot->pad = Thick(20, 0, 12, 0);
    { auto* line = MkBox(footBox); line->fh = 1; line->va = Al::Start; line->bg = SolidB(K::White, K::CardLine); }
    auto* bat = MkBox(foot, Lay::H); bat->star = 1; bat->ha = Al::Start; bat->fh = 36; bat->va = Al::Center; bat->hit = true; bat->radius = 4; bat->pad = Thick(6, 0, 8, 0); bat->gap = 8; bat->bgHover = SolidB(K::White, 0x16);
    bat->onClick = [] { ClosePanels(); Launch(L"ms-settings:batterysaver"); };
    { auto* st = MkBox(bat, Lay::Overlay); st->fw = 20; st->va = Al::Center; d->batFill = MkGlyph(st, 0xEBA0, 16); d->batG = MkGlyph(st, 0xEBA0, 16); d->batT = MkLabel(bat, L"", FText(12)); d->batT->va = Al::Center; }
    auto* pal = IconBtn(foot, 0xE790, 36, 14, [this] { ShowDetail(MakeThemePage(this)); }); pal->bg = Brush{}; pal->va = Al::Center; pal->ha = Al::End; pal->tip = L"Color";
    auto* set = IconBtn(foot, 0xE713, 36, 14, [] { ClosePanels(); SettingsOpenFromApp(0); }); set->bg = Brush{}; set->va = Al::Center; set->ha = Al::End; set->tip = L"Settings";   // the one Desk settings window
}

void ThemePreviewEnd(QuickPanel* qp);
void QuickPanel::OnClose() { Back(); ThemePreviewEnd(this); if (d->brightTimer) { UiKillTimer(d->brightTimer); d->brightTimer = 0; } }   // a colour being tried on the Color page is put back
void QuickPanel::ShowDetail(View* p, bool ownHeight) {
    Back(); page = p; detail->content = detail->Add(p); detail->visible = true; main->visible = false; detail->ScrollTo(0, false);
    if (foot) foot->visible = !ownHeight;   // DeskMode's pages have no Quick Settings footer (it was a blue strip under the dark panel)
    if (ownHeight) {   // the DeskMode page: as tall as its content, like DeskMode's own panel, but never taller than the screen it opens on
        MONITORINFO mi{sizeof mi}; float k = 1; if (GetMonitorInfoW(mon, &mi)) { UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); k = dx / 96.f; }
        float maxH = (mi.rcWork.bottom - mi.rcWork.top) / k - 40;   // some air (no footer on these pages)
        Sz s = p->Measure({widthDip, INF}); pages->fh = std::min(ceilf(s.h), std::max(300.f, maxH)); Refit();
    }
    Relayout();
}
void QuickPanel::Back() {
    if (!page) { if (!main->visible) { main->visible = true; } return; }
    detail->content = nullptr; detail->Remove(page); page = nullptr; detail->visible = false; main->visible = true; if (foot) foot->visible = true;
    { float before = pages->fh; pages->fh = NAN; Sz s = main->Measure({widthDip, INF}); pages->fh = ceilf(s.h); if (before != pages->fh) Refit(); }   // back to the main page's height (the DeskMode page may have grown it)
    Refresh(); Relayout();
}
void QuickPanel::OnSysChange() { Refresh(); }
void QuickPanel::Refresh() {
    NetState n = NetSnap(); BattState b = Battery(); VolState v = Volume();
    if (d->tune) { MEMORYSTATUSEX m{sizeof m}; if (GlobalMemoryStatusEx(&m)) d->studio->Set(false, Fmt(L"Memory %lu%%", m.dwMemoryLoad)); }   // how full RAM is; open DeskTune to free some
    ws wl = L"Not connected"; if (n.kind == NetState::WiFi && !n.name.empty()) wl = n.internet ? n.name : n.name + L" · " + (n.needsSignIn ? L"Sign in" : L"No internet");
    d->wifi->Set(n.wifiOn, wl); d->bt->Set(n.btOn, n.btSubtitle.empty() ? L"No devices connected" : n.btSubtitle, n.btPresent);
    d->air->Set(n.airplane); int rl = RotationLocked(); d->rot->Set(rl == 1, L"", rl >= 0);
    d->saver->Set(b.saver, L"", b.present); int nl = NightLightIsOn(); d->night->Set(nl == 1, L"", nl >= 0);
    d->vol->Set(roundf(v.vol * 100));
    if (!d->vol->dragging) { d->vol->muted = v.muted; d->vol->text = v.muted ? ws(L"Muted") : Fmt(L"%d%%", (int)lroundf(v.vol * 100)); d->vol->glyph = v.muted || v.vol < 0.001f ? 0xE74F : v.vol < 0.34f ? 0xE993 : v.vol < 0.67f ? 0xE994 : 0xE995; d->vol->Invalidate(); }
    if (d->media) { bool was = d->media->visible; d->media->Update(d->mediaRoom); if (was != d->media->visible && !page) { pages->fh = NAN; Sz s = main->Measure({widthDip, INF}); pages->fh = ceilf(s.h); Refit(); } }
    int lv = (int)lroundf(b.pct / 10.f); wchar_t base = b.charging ? 0xEBAB : 0xEBA0;
    d->batFill->Set(b.present ? base + lv : 0xEBA0 + 10); d->batG->Set(base); d->batG->visible = b.present && (b.charging || b.saver);
    d->batFill->col = b.present && b.charging ? Col(K::Charge) : b.present && b.saver ? Col(K::Amber) : Col(K::White);
    ws desc = !b.present ? L"" : b.ac ? (b.pct >= 100 ? L"Fully charged" : L"Charging") : b.secsLeft > 0 ? (b.secsLeft >= 3600 ? Fmt(L"%d hr %d min left", b.secsLeft / 3600, b.secsLeft % 3600 / 60) : Fmt(L"%d min left", b.secsLeft / 60)) : L"On battery";
    d->batT->Set(b.present ? Fmt(L"%d%%  ·  %s", b.pct, desc.c_str()) : ws(L"Plugged in"));
    NetHotspot(true, false, [this](int r) { d->hotspot->Set(r == 1); });
}
void QuickPanel::BuildBrightness() {
    MONITORINFOEXW mi{}; mi.cbSize = sizeof mi; if (!GetMonitorInfoW(mon, &mi)) return;
    Mon m{}; for (auto& x : Monitors()) if (x.h == mon) m = x; ws dev = mi.szDevice;
    if (d->brightFor != dev) { d->brightFor = dev; d->bright->enabled = false; DispInfo di = DisplayInfoFor(dev); d->bright->tip = L"Brightness: " + di.friendly; }
    d->bright->onChange = [this, dev](float v) { BrightSet(dev, (int)lroundf(v)); d->bright->text = Fmt(L"%d%%", (int)lroundf(v)); };
    BrightOpen(m, [this, dev](bool ok, int v) {
        if (d->brightFor != dev) return;
        d->bright->enabled = ok; if (v >= 0) d->bright->Set((float)v); d->bright->text = ok && v >= 0 ? Fmt(L"%d%%", (int)lroundf(d->bright->value)) : ws();
        if (!ok) d->bright->tip = L"This screen doesn't allow brightness control"; Invalidate();
    });
}
void QuickPanel::OnOpen() {
    // now playing only when the screen has room for it: a short screen (1080p at 150%) keeps the panel as it was
    { MONITORINFO mi{sizeof mi}; UINT dx = 96, dy = 96; if (GetMonitorInfoW(mon, &mi)) { GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); d->mediaRoom = (mi.rcWork.bottom - mi.rcWork.top) * 96.f / dy >= 760; } }
    Back(); BuildBrightness(); Refresh();
    // while the panel is open the slider follows the screen: the brightness keys, automatic brightness and the charger all move it behind our back
    if (!d->brightTimer) d->brightTimer = UiTimer(2500, [this] { if (!d->bright->dragging && !page) BuildBrightness(); });
    // exactly two rows of buttons show; the rest is a scroll away
    d->tiles->fh = kRowH * 2;
    d->tiles->ScrollTo(0, false);
    // the same size on every page: the main page sets the height
    pages->fh = NAN; Sz s = main->Measure({widthDip, INF}); pages->fh = ceilf(s.h);
}
Flyout* MakeQuickPanel() { return new QuickPanel(); }
