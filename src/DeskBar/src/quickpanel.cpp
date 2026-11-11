#include "pch.h"
#include "quick.h"

// ---------------------------------------------------------------- tile
namespace {
struct Tile : View {
    View *box, *mainB, *moreB = nullptr; Glyph *g1, *g2 = nullptr; Label* lab; bool on = false;
    Tile(wchar_t glyph, const ws& label, std::function<void()> toggle, std::function<void()> more, bool moreOnly = false) {
        lay = Lay::V; fw = 104; margin = Thick(0, 0, 0, 10); ha = Al::Center;
        box = MkBox(this, Lay::H); box->fh = 48; box->radius = 8; box->borderW = 1;
        mainB = MkBox(box); mainB->star = 1; mainB->hit = true; g1 = MkGlyph(mainB, glyph, 16); g1->ha = g1->va = Al::Center;
        if (moreOnly) {
            mainB->onClick = more ? more : toggle; auto* a = MkGlyph(mainB, 0xE76C, 10); a->ha = Al::Center; a->va = Al::Center;
            g1->margin = Thick(0, 0, 24, 0); a->margin = Thick(24, 0, 0, 0);
        } else if (more) {
            mainB->onClick = toggle;
            auto* sp = MkBox(box); sp->fw = 1; sp->margin = Thick(0, 10); sp->bg = SolidB(K::Black, 0x30);
            moreB = MkBox(box); moreB->star = 1; moreB->hit = true; g2 = MkGlyph(moreB, 0xE76C, 12); g2->ha = g2->va = Al::Center; moreB->onClick = more;
            mainB->star = 1;
        } else mainB->onClick = toggle;
        lab = MkLabel(this, label, FText(12)); lab->ta = TA::Center; lab->wrap = true; lab->maxW = 104; lab->margin = Thick(0, 7, 0, 0); lab->ha = Al::Center;
        Set(false, label);
    }
    void Set(bool o, const ws& label = L"", bool enabled = true) {
        on = o;
        if (on) { box->bg = LinearB(0, 0, 1, 1, {{0, Col(0x6CD2FF)}, {1, Col(0x359CF5)}}); box->bgHover = box->bg; box->border = Col(K::White, 0); box->borderHover = SolidB(K::White, 0x50); }
        else { box->bg = SolidB(K::White, 0x1C); box->bgHover = SolidB(K::White, 0x2C); box->border = Col(K::White, 0x22); box->borderHover = Brush{}; }
        D2D1_COLOR_F c = on ? Col(K::Black) : Col(K::White); g1->col = c; if (g2) g2->col = c;
        if (!label.empty()) lab->Set(label);
        opacity = enabled ? 1.f : 0.5f; mainB->enabled = enabled; if (moreB) moreB->enabled = enabled; Invalidate();
    }
};
}

struct QuickPanel::Impl {
    Tile *wifi, *bt, *studio, *air, *rot, *saver, *night, *hotspot, *cast;
    Scroll* tiles; View *grid1, *dot1, *dot2; Slider *vol, *bright = nullptr; Glyph* volG; View* brightRow; Glyph *batG, *batFill; Label* batT; Glyph* brightG;
    ws brightFor; HMONITOR monitor = nullptr;
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
    pages = MkBox(root); main = MkBox(pages, Lay::V); main->pad = Thick(20, 20, 20, 8);
    detail = pages->Add(new Scroll()); detail->visible = false; detail->pad = Thick(0, 0, 2, 0);

    auto mk = [](wchar_t g, const ws& l, std::function<void()> t, std::function<void()> m = {}, bool mo = false) { return new Tile(g, l, std::move(t), std::move(m), mo); };
    auto closeAnd = [](const wchar_t* uri) { return [uri] { ClosePanels(); Launch(uri); }; };
    auto win = [](std::initializer_list<WORD> k) { return [k] { ClosePanels(); UiOnce(150, [k] { SendKeys(k); }); }; };
    d->wifi = mk(0xE701, L"Wi-Fi", [this] { NetSetWifi(!d->wifi->on); }, [this] { ShowDetail(MakeWifiPage(this)); });
    d->bt = mk(0xE702, L"Bluetooth", [this] { NetSetBt(!d->bt->on); }, [this] { ShowDetail(MakeBtPage(this)); });
    ws tuneExe = KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\DeskTune\\DeskTune.exe";
    d->tune = GetFileAttributesW(tuneExe.c_str()) != INVALID_FILE_ATTRIBUTES;
    d->studio = d->tune ? mk(0xE9D9, L"DeskTune", [tuneExe] { ClosePanels(); Launch(tuneExe); })
                        : mk(0xE714, L"Studio effects", closeAnd(L"ms-settings:camera"), closeAnd(L"ms-settings:camera"), true);
    d->air = mk(0xE709, L"Airplane mode", [this] { bool on = !d->air->on; d->air->Set(on); NetSetAirplane(on); });
    d->rot = mk(0xE7AD, L"Rotation lock", [this] { int l = RotationLocked(); if (l >= 0) RotationSetLocked(!l); Refresh(); });
    d->saver = mk(0xEBB6, L"Energy saver", [this] { bool on = !d->saver->on; d->saver->Set(on); std::thread([on] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); EnergySaverSet(on); SysPoke(); }).detach(); });
    d->night = mk(0xE708, L"Night light", [this] { bool on = !d->night->on; if (NightLightSet(on)) d->night->Set(on); });
    d->hotspot = mk(0xE704, L"Mobile hotspot", [this] { bool on = !d->hotspot->on; d->hotspot->Set(on); NetHotspot(false, on, [this, on](int r) { if (r < 0) { d->hotspot->Set(!on); ClosePanels(); Launch(L"ms-settings:network-mobilehotspot"); } }); });
    d->cast = mk(0xEC15, L"Cast", win({VK_LWIN, 'K'}));
    Tile* top[] = {d->wifi, d->bt, d->studio, d->air, d->rot, d->saver, d->night, d->hotspot, d->cast};
    Tile* extra[] = {mk(0xE776, L"Accessibility", closeAnd(L"ms-settings:easeofaccess")), mk(0xE7F4, L"Project", win({VK_LWIN, 'P'})), mk(0xE72D, L"Nearby sharing", closeAnd(L"ms-settings:crossdevice")),
                     mk(0xE705, L"VPN", closeAnd(L"ms-settings:network-vpn")), mk(0xE823, L"Focus", closeAnd(L"ms-settings:quietmomentshome")), mk(0xE7F0, L"Live captions", win({VK_LWIN, VK_CONTROL, 'L'}))};
    d->tiles = main->Add(new Scroll()); auto* content = MkBox(nullptr, Lay::V); d->tiles->content = d->tiles->Add(content);
    d->grid1 = MkBox(content, Lay::Uniform); d->grid1->cols = 3; for (auto* t : top) d->grid1->Add(t);
    auto* grid2 = MkBox(content, Lay::Uniform); grid2->cols = 3; for (auto* t : extra) grid2->Add(t);
    // the tiles scroll with their own slider: the two page dots that sat under it as well were removed (one control for one job)
    Divider(main, Thick(-20, 4, -20, 14));
    d->brightRow = MkBox(main, Lay::H); d->brightRow->margin = Thick(0, 0, 0, 10);
    d->brightG = MkGlyph(d->brightRow, 0xE706, 16); d->brightG->fw = 40; d->brightG->va = Al::Center; d->brightG->margin = Thick(0);
    d->bright = d->brightRow->Add(new Slider()); d->bright->star = 1; d->bright->enabled = false; d->bright->va = Al::Center;
    { auto* sp = MkBox(d->brightRow); sp->fw = 48; }
    auto* vr = MkBox(main, Lay::H); vr->margin = Thick(0, 2, 0, 10);
    auto* mute = IconBtn(vr, 0xE995, 34, 16, [] { AudioToggleMute(); }); mute->bg = Brush{}; mute->margin = Thick(3, 0, 3, 0); mute->tip = L"Mute"; d->volG = (Glyph*)mute->kids[0];
    d->vol = vr->Add(new Slider()); d->vol->star = 1; d->vol->va = Al::Center;
    d->vol->onChange = [this](float v, bool) { AudioSetVolume(v / 100); d->vol->tip = Fmt(L"%d%%", (int)lroundf(v)); };
    auto* mx = MkBox(vr, Lay::H); mx->fw = 48; mx->fh = 34; mx->va = Al::Center; mx->radius = 4; mx->hit = true; mx->gap = 4; mx->pad = Thick(8, 0); mx->bgHover = SolidB(K::White, 0x20); mx->tip = L"Sound output and volume mixer";
    { auto* a = MkGlyph(mx, 0xE9E9, 14); a->va = Al::Center; auto* b = MkGlyph(mx, 0xE76C, 10); b->va = Al::Center; }
    mx->onClick = [this] { ShowDetail(MakeSoundPage(this)); };
    // footer
    auto* footBox = MkBox(root); footBox->fh = 48; footBox->bg = SolidB(0x001030, 0x30); this->foot = footBox;
    auto* foot = MkBox(footBox, Lay::H); foot->pad = Thick(20, 0, 12, 0);
    { auto* line = MkBox(footBox); line->fh = 1; line->va = Al::Start; line->bg = SolidB(K::White, K::CardLine); }
    auto* bat = MkBox(foot, Lay::H); bat->star = 1; bat->ha = Al::Start; bat->fh = 36; bat->va = Al::Center; bat->hit = true; bat->radius = 4; bat->pad = Thick(6, 0, 8, 0); bat->gap = 8; bat->bgHover = SolidB(K::White, 0x16);
    bat->onClick = [] { ClosePanels(); Launch(L"ms-settings:batterysaver"); };
    { auto* st = MkBox(bat, Lay::Overlay); st->fw = 20; st->va = Al::Center; d->batFill = MkGlyph(st, 0xEBA0, 16); d->batG = MkGlyph(st, 0xEBA0, 16); d->batT = MkLabel(bat, L"", FText(12)); d->batT->va = Al::Center; }
    auto* pal = IconBtn(foot, 0xE790, 36, 14, [this] { ShowDetail(MakeThemePage(this)); }); pal->bg = Brush{}; pal->va = Al::Center; pal->ha = Al::End; pal->tip = L"Color";
    auto* set = IconBtn(foot, 0xE713, 36, 14, [] { ClosePanels(); Launch(L"ms-settings:"); }); set->bg = Brush{}; set->va = Al::Center; set->ha = Al::End; set->tip = L"All settings";
}

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
    ws wl = L"Wi-Fi"; if (n.kind == NetState::WiFi && !n.name.empty()) wl = n.internet ? n.name : n.name + L" · " + (n.needsSignIn ? L"Sign in" : L"No internet");
    d->wifi->Set(n.wifiOn, wl); d->bt->Set(n.btOn, n.btSubtitle.empty() ? L"Bluetooth" : n.btSubtitle, n.btPresent);
    d->air->Set(n.airplane); int rl = RotationLocked(); d->rot->Set(rl == 1, L"", rl >= 0);
    d->saver->Set(b.saver, L"", b.present); int nl = NightLightIsOn(); d->night->Set(nl == 1, L"", nl >= 0);
    d->vol->Set(roundf(v.vol * 100)); d->volG->Set(v.muted || v.vol < 0.001f ? 0xE74F : v.vol < 0.34f ? 0xE993 : v.vol < 0.67f ? 0xE994 : 0xE995);
    if (!d->vol->dragging) d->vol->tip = Fmt(L"%d%%", (int)lroundf(v.vol * 100));
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
    if (d->brightFor != dev) { d->brightFor = dev; d->bright->enabled = false; d->bright->opacity = 0.5f; DispInfo di = DisplayInfoFor(dev); d->brightG->tip = di.friendly; }
    d->bright->onChange = [dev](float v, bool) { BrightSet(dev, (int)lroundf(v)); };
    BrightOpen(m, [this, dev](bool ok, int v) {
        if (d->brightFor != dev) return;
        d->bright->enabled = ok; d->bright->opacity = ok ? 1.f : 0.5f; if (v >= 0) d->bright->Set((float)v);
        d->brightRow->tip = ok ? L"" : L"This screen doesn't allow brightness control"; Invalidate();
    });
}
void QuickPanel::OnOpen() {
    Back(); BuildBrightness(); Refresh();
    // exactly the nine tiles show; the rest is a scroll away
    d->tiles->fh = NAN; Sz g = d->grid1->Measure({320, INF}); d->tiles->fh = ceilf(g.h);
    d->tiles->ScrollTo(0, false);
    // the same size on every page: the main page sets the height
    pages->fh = NAN; Sz s = main->Measure({widthDip, INF}); pages->fh = ceilf(s.h);
}
Flyout* MakeQuickPanel() { return new QuickPanel(); }
