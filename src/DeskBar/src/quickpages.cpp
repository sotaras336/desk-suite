#include "pch.h"
#include "quick.h"

// Quick Settings detail pages: Wi-Fi, Bluetooth, Sound (output devices, "also play", per-app volume and routing).
namespace {
struct Bars : View {      // four signal bars
    int n; D2D1_COLOR_F on, off; float s;
    Bars(int bars, D2D1_COLOR_F o, D2D1_COLOR_F f, float sc) : n(std::clamp(bars, 1, 4)), on(o), off(f), s(sc) { fw = (3.5f * 4 + 2 * 3) * sc; fh = 15 * sc; va = Al::Center; }
    void PaintContent(Gfx& g) override { for (int i = 0; i < 4; i++) { float h = (5 + i * 3.3f) * s, x = rc.l + i * (3.5f + 2) * s; g.FillC({x, rc.b - h, x + 3.5f * s, rc.b}, i < n ? on : off, 1.5f * s); } }
};
View* Bubble(View* parent, wchar_t glyph, Brush bg, D2D1_COLOR_F fg, float size) {
    auto* b = MkBox(parent); b->fw = b->fh = size; b->radius = size / 2; b->bg = bg; b->va = Al::Center; auto* g = MkGlyph(b, glyph, size * 0.42f, fg); g->ha = g->va = Al::Center; return b;
}
View* ClickCard(View* parent, std::function<void()> fn, bool highlight = false) {
    auto* c = Card(parent); c->hit = true; c->bgHover = SolidB(K::White, K::CardHover); c->onClick = std::move(fn);
    if (highlight) { c->bg = SolidB(K::Accent, 0x22); c->borderW = 1; c->border = Col(K::Accent, 0x70); }
    return c;
}
View* Skeleton(View* parent, int rows) {
    auto* p = MkBox(parent, Lay::V);
    for (int i = 0; i < rows; i++) {
        auto* c = Card(p); auto* r = MkBox(c, Lay::H); r->fh = 22; r->gap = 16;
        auto* a = MkBox(r); a->fw = 18; a->fh = 14; a->radius = 3; a->bg = SolidB(K::White, K::CardHover); a->va = Al::Center; a->margin = Thick(0, 0, 0, 0);
        auto* b = MkBox(r); b->fw = (float)(120 + i * 37 % 80); b->fh = 10; b->radius = 5; b->bg = SolidB(K::White, K::CardHover); b->va = Al::Center; b->ha = Al::Start;
    }
    return p;
}
View* Empty(View* parent, wchar_t glyph, const ws& title, const ws& sub) {
    auto* p = MkBox(parent, Lay::V); p->margin = Thick(0, 14); p->ha = Al::Center;
    auto* b = Bubble(p, glyph, SolidB(K::White, K::Card), Col(K::White, K::Dim), 54); b->ha = Al::Center;
    auto* t = MkLabel(p, title, FText(14, 600)); t->margin = Thick(0, 10, 0, 2); t->ha = Al::Center;
    auto* s = MkLabel(p, sub, FText(12), Col(K::White, K::Dim)); s->wrap = true; s->ta = TA::Center; s->maxW = 280; s->ha = Al::Center; return p;
}
ws Dots(const wchar_t* base, int step) { static const wchar_t* d[] = {L"", L".", L"..", L"..."}; return ws(base) + d[step % 4]; }   // "Connecting", "Connecting.", ... so the user can see it is working
View* Footer(View* parent) { auto* f = MkBox(parent, Lay::H); f->margin = Thick(0, 10, 0, 0); f->gap = 8; return f; }
View* MiniPill(View* parent, const ws& t, wchar_t glyph, bool accent, std::function<void()> fn) { auto* p = Pill(parent, t, glyph, accent, std::move(fn)); p->pad = Thick(10, 4, 11, 5); return p; }
void CloseAnd(const wchar_t* uri) { ClosePanels(); Launch(uri); }

struct PageBase : View {
    QuickPanel* q; std::shared_ptr<int> life = std::make_shared<int>(0);
    PageBase(QuickPanel* p) : q(p) { lay = Lay::V; pad = Thick(18, 16, 18, 14); }
    std::weak_ptr<int> Alive() { return life; }
};
#define GUARD auto w_ = Alive();
#define IFALIVE if (w_.expired()) return;

// ======================================================= Wi-Fi
struct WifiPage : PageBase {
    ws connecting, justConnected; int step = 0; Label* connLabel = nullptr;   // connecting: the network being joined right now (the form stays put until the answer arrives)
    View *hero, *list; ws expanded; std::vector<WifiNet> nets; Toggle* tg = nullptr; TextBox *pwd = nullptr, *user = nullptr; Label* err = nullptr; View *pwdBox = nullptr, *userBox = nullptr, *pwdHint = nullptr, *userHint = nullptr, *btn = nullptr;
    WifiPage(QuickPanel* p) : PageBase(p) {
        auto* h = PageHeader(this, L"Wi-Fi", [p] { p->Back(); }, &tg); (void)h;
        tg->Set(NetSnap().wifiOn); tg->onChange = [this](bool on) { NetSetWifi(on); GUARD UiOnce(on ? 1500 : 200, [this, w_, on] { IFALIVE Load(on); }); };
        hero = MkBox(this, Lay::V);
        auto* head = MkBox(this, Lay::H); auto* sl = SectionLabel(head, L"Available networks"); sl->star = 1; sl->va = Al::Center;
        auto* rf = IconBtn(head, 0xE72C, 26, 11, [this] { Load(true, true); }); rf->bg = Brush{}; rf->tip = L"Scan again";
        list = MkBox(this, Lay::V);
        auto* f = Footer(this); MiniPill(f, L"Wi-Fi settings", 0xE713, false, [] { CloseAnd(L"ms-settings:network-wifi"); }); MiniPill(f, L"Hidden network", 0xE710, false, [] { CloseAnd(L"ms-settings:network-wifi"); });
        Load(NetSnap().wifiOn);
    }
    void Load(bool on, bool forceFresh = false) {
        if (!connecting.empty()) return;   // rebuilding now would throw away the password box mid-connect
        connLabel = nullptr; list->Clear(); hero->Clear(); NetState ns = NetSnap();
        if (!on) { Empty(list, 0xEB5E, L"Wi-Fi is off", L"Turn it on with the switch above."); q->Relayout(); return; }
        if (ns.kind == NetState::WiFi && !ns.name.empty()) BuildHero(ns.name, ns.bars, true, nullptr);
        if (nets.empty()) nets = WifiLast();
        GUARD
        if (nets.empty()) {
            WifiScan(false, [this, w_](bool, std::vector<WifiNet> v) { IFALIVE if (nets.empty() && !v.empty()) { nets = v; Render(); } });
            Skeleton(list, 4);
        } else Render();
        WifiScan(true, [this, w_](bool access, std::vector<WifiNet> v) {
            IFALIVE
            if (!access) {
                list->Clear(); Empty(list, 0xE707, L"Windows blocked the network list", L"Allow desktop apps to use location, then try again.");
                Pill(list, L"Open location settings", 0xE707, true, [] { CloseAnd(L"ms-settings:privacy-location"); }); q->Relayout(); return;
            }
            nets = v; Render();
        });
        q->Relayout(); (void)forceFresh;
    }
    void Render() {
        if (!connecting.empty()) return;
        connLabel = nullptr; list->Clear(); hero->Clear(); NetState ns = NetSnap();
        const WifiNet* conn = nullptr; for (auto& n : nets) if (n.connected) conn = &n;
        if (conn) BuildHero(conn->ssid, conn->bars, conn->secured, conn); else if (ns.kind == NetState::WiFi) BuildHero(ns.name, ns.bars, true, nullptr);
        int others = 0;
        for (auto& n : nets) if (!n.connected) { BuildRow(n); others++; }
        if (!others) { auto* l = MkLabel(list, conn ? L"No other networks nearby" : L"No networks found", FText(12), Col(K::White, K::Dim)); l->margin = Thick(4, 0, 0, 8); }
        q->Relayout();
    }
    void BuildHero(const ws& ssid, int bars, bool secured, const WifiNet* d) {
        NetState ns = NetSnap();
        if (justConnected == ssid) {   // a few seconds of confirmation after "Connect" succeeded
            auto* ok = MkBox(hero, Lay::H); ok->gap = 8; ok->margin = Thick(4, 0, 0, 8); auto* g = MkGlyph(ok, 0xE73E, 13, Col(K::Green)); g->va = Al::Center;
            auto* t = MkLabel(ok, L"Connected to " + ssid, FText(13, 600), Col(K::Green)); t->va = Al::Center;
        }
        auto* h = MkBox(hero, Lay::V); h->radius = 14; h->pad = Thick(16, 14); h->margin = Thick(0, 0, 0, 16); h->bg = LinearB(0, 0, 1, 1, {{0, Col(0x3AA6F5)}, {1, Col(0x1B58C6)}});
        auto* top = MkBox(h, Lay::H); top->gap = 14;
        Bubble(top, 0xE701, SolidB(K::White, 0x40), Col(K::White), 46);
        auto* nm = MkBox(top, Lay::V); nm->star = 1; nm->va = Al::Center;
        auto* l = MkLabel(nm, ssid, FText(16, 600)); (void)l;
        auto* st = MkBox(nm, Lay::H); st->gap = 6;
        auto* dot = MkBox(st); dot->fw = dot->fh = 7; dot->radius = 3.5f; dot->va = Al::Center; dot->bg = SolidB(ns.internet ? K::Green : ns.needsSignIn ? 0xFFD700 : 0xFFA500);
        MkLabel(st, ns.status + (secured ? L" · Secured" : L" · Open"), FText(12), Col(K::White, 0xDD));
        top->Add(new Bars(bars, Col(K::White), Col(K::White, 0x50), 1.3f));
        auto* chips = MkBox(h, Lay::Wrap); chips->margin = Thick(60, 10, 0, 0); chips->gap = 6;
        auto chip = [&](wchar_t g, const ws& t) {
            if (t.empty()) return; auto* c = MkBox(chips, Lay::H); c->radius = 9; c->pad = Thick(8, 2, 9, 3); c->bg = SolidB(K::White, 0x30); c->gap = 5;
            auto* gg = MkGlyph(c, g, 10); gg->va = Al::Center; auto* tt = MkLabel(c, t, FText(11)); tt->va = Al::Center;
        };
        if (d) { chip(0xE701, d->band); chip(0xE72E, d->security); chip(0xE9D9, Fmt(L"%d%%", d->pct)); }
        chip(0xE968, ns.ip);
        auto* act = Footer(h); act->margin = Thick(0, 14, 0, 0);
        GUARD
        MiniPill(act, L"Disconnect", 0xE711, false, [this, w_] { WifiDisconnect(); UiOnce(900, [this, w_] { IFALIVE Load(true); }); });
        MiniPill(act, L"Properties", 0xE946, false, [] { CloseAnd(L"ms-settings:network-wifi"); });
        if (ns.needsSignIn) { auto* s = MiniPill(h, L"Open sign-in page", 0xE774, true, [] { CloseAnd(L"http://www.msftconnecttest.com/redirect"); }); s->margin = Thick(60, 8, 0, 0); }
    }
    void BuildRow(const WifiNet& n) {
        bool open = expanded == n.ssid; ws ssid = n.ssid;
        auto* c = ClickCard(list, [this, ssid] { if (!connecting.empty()) return; expanded = expanded == ssid ? ws() : ssid; Render(); }, open);
        auto* top = MkBox(c, Lay::H); top->gap = 0;
        auto* b = top->Add(new Bars(n.bars, Col(K::Accent), Col(K::White, K::CardHover), 1)); b->margin = Thick(0, 0, 17, 0);
        auto* nm = MkBox(top, Lay::V); nm->star = 1; nm->va = Al::Center;
        MkLabel(nm, n.ssid, FText(13));
        MkLabel(nm, n.band + (n.band.empty() ? L"" : L" · ") + n.security + Fmt(L" · %d%%", n.pct), FText(11), Col(K::White, K::Dim));
        auto* r = MkBox(top, Lay::H); r->gap = 8; r->va = Al::Center;
        auto* badge = MkBox(r, Lay::H); badge->radius = 8; badge->pad = Thick(7, 1, 7, 2); badge->gap = 4; badge->va = Al::Center; badge->bg = n.secured ? SolidB(K::White, K::CardHover) : SolidB(0xF2C64B, 0x40);
        auto* bg = MkGlyph(badge, n.secured ? 0xE72E : 0xE785, 9); bg->va = Al::Center; auto* bt = MkLabel(badge, n.secured ? L"Secured" : L"Open", FText(10.5f)); bt->va = Al::Center;
        MkGlyph(r, open ? 0xE70D : 0xE70E, 10, Col(K::White, K::Dim));
        if (open) BuildConnect(c, n);
    }
    void BuildConnect(View* card, const WifiNet& n) {
        auto* p = MkBox(card, Lay::V); p->margin = Thick(34, 12, 0, 2); p->hit = true;   // swallow clicks so the form doesn't collapse the row
        auto field = [&](const ws& hint, bool secret, View** boxOut, TextBox** tbOut, View** hintOut) {
            auto* h = MkLabel(p, hint, FText(11), Col(K::White, K::Dim)); h->margin = Thick(2, 0, 0, 4); h->visible = false; *hintOut = h;
            auto* box = MkBox(p); box->fh = 34; box->radius = 6; box->bg = SolidB(K::White, 0x18); box->borderW = 1; box->border = Col(K::White, 0x30); box->pad = Thick(10, 0); box->margin = Thick(0, 0, 0, 8); box->visible = false; box->hit = true;
            auto* tb = box->Add(new TextBox()); tb->font = FText(13); tb->password = secret; tb->va = Al::Center; *boxOut = box; *tbOut = tb;
            if (secret) {   // eye button: show what you typed (hard to get a long password right blind)
                tb->margin = Thick(0, 0, 30, 0); auto* eye = IconBtn(box, 0xE7B3, 26, 12); eye->ha = Al::End; eye->va = Al::Center; eye->bg = Brush{}; eye->tip = L"Show password";
                eye->onClick = [tb, eye] { tb->password = !tb->password; static_cast<Glyph*>(eye->kids[0])->Set(tb->password ? 0xE7B3 : 0xED1A); eye->tip = tb->password ? L"Show password" : L"Hide password"; tb->Invalidate(); };
            }
        };
        field(L"User name (e.g. your university email)", false, &userBox, &user, &userHint);
        field(L"Network password", true, &pwdBox, &pwd, &pwdHint);
        err = MkLabel(p, L"", FText(12), Col(0xFFA3A3)); err->wrap = true; err->visible = false; err->margin = Thick(2, 0, 0, 8);
        bool ent = n.enterprise; ws ssid = n.ssid; GUARD
        auto connect = [this, w_, ent, ssid] {
            if (!connecting.empty()) return;
            err->visible = false; bool usePass = pwdBox->visible;
            connecting = ssid; SetConnecting(true);
            WifiConnect(ssid, usePass ? pwd->text : L"", userBox->visible ? Trim(user->text) : L"", usePass, [this, w_, ent, ssid](ws res) {
                IFALIVE
                connecting.clear(); SetConnecting(false);
                if (res.empty()) {
                    justConnected = ssid; expanded.clear(); UiOnce(700, [this, w_] { IFALIVE Load(true); UiOnce(4000, [this, w_] { IFALIVE justConnected.clear(); Load(true); }); }); return;
                }
                if (res == L"password") {
                    if (pwdBox->visible) { err->Set(L"That password didn't work. Try again."); err->visible = true; }
                    pwdBox->visible = pwdHint->visible = true; if (ent) { userBox->visible = userHint->visible = true; q->SetFocus(user); } else q->SetFocus(pwd);
                } else { err->Set(res); err->visible = true; }
                q->Relayout();
            });
        };
        btn = Pill(p, L"Connect", 0xE71B, true, connect); btn->ha = Al::End; btn->pad = Thick(18, 8);
        pwd->onEnter = connect;
    }
    // While the answer is pending the button turns into a "Connecting..." label with moving dots and ignores clicks.
    void SetConnecting(bool on) {
        if (!btn) return; auto* lab = static_cast<Label*>(btn->kids.back());
        btn->enabled = !on; btn->opacity = on ? 0.8f : 1.f; step = 0; lab->Set(on ? Dots(L"Connecting", 0) : ws(L"Connect")); connLabel = on ? lab : nullptr;
        if (on) {
            GUARD auto id = std::make_shared<UINT_PTR>(0);
            *id = UiTimer(350, [this, w_, id] { if (w_.expired() || connecting.empty()) { UiKillTimer(*id); return; } step++; if (connLabel) connLabel->Set(Dots(L"Connecting", step)); });
        }
        q->Relayout();
    }
};

// ======================================================= Bluetooth
wchar_t BtGlyph(const ws& name) {
    ws n = Lower(name); auto has = [&](const wchar_t* s) { return n.find(s) != ws::npos; };
    if (has(L"pen")) return 0xEDC6; if (has(L"mouse")) return 0xE962; if (has(L"keyboard")) return 0xE765;
    if (has(L"speaker") || has(L"flip") || has(L"charge") || has(L"boom") || has(L"soundlink") || (n.size() > 3 && n.substr(n.size() - 3) == L" go")) return 0xE7F5;
    if (has(L"bud") || has(L"pod") || has(L"head") || has(L"jbl") || has(L"ear") || has(L"sony")) return 0xE7F6;
    if (has(L"phone") || has(L"galaxy") || has(L"pixel") || has(L"ultra")) return 0xE8EA;
    if (has(L"controller") || has(L"xbox") || has(L"pad")) return 0xE7FC;
    return 0xE702;
}
struct BtPage : PageBase {
    View* content; std::vector<BtDev> devs; ws selected; std::map<ws, ws> status; Toggle* tg = nullptr; int step = 0; bool dots = false;
    ws Show(const ws& s) { return !s.empty() && s.back() == L'…' ? Dots(s.substr(0, s.size() - 1).c_str(), step) : s; }   // "Connecting…" animates its dots
    void EnsureDots() {   // while anything is connecting, redraw the status text every 400 ms
        if (dots) return; dots = true; GUARD auto id = std::make_shared<UINT_PTR>(0);
        *id = UiTimer(400, [this, w_, id] { if (w_.expired()) { UiKillTimer(*id); return; } if (status.empty()) { UiKillTimer(*id); dots = false; return; } step++; Render(devs); });
    }
    BtPage(QuickPanel* p) : PageBase(p) {
        PageHeader(this, L"Bluetooth", [p] { p->Back(); }, &tg); tg->Set(NetSnap().btOn);
        GUARD tg->onChange = [this, w_](bool on) { NetSetBt(on); UiOnce(on ? 1200 : 200, [this, w_] { IFALIVE Load(); }); };
        content = MkBox(this, Lay::V);
        auto* f = Footer(this); MiniPill(f, L"Add device", 0xE710, true, [] { CloseAnd(L"ms-settings-connectabledevices:devicediscovery"); }); MiniPill(f, L"Bluetooth settings", 0xE713, false, [] { CloseAnd(L"ms-settings:bluetooth"); });
        MiniPill(f, L"Refresh", 0xE72C, false, [this] { Load(); });
        Load();
    }
    void Load() {
        content->Clear(); if (!NetSnap().btOn) { Empty(content, 0xE702, L"Bluetooth is off", L"Turn it on to use your pen, earbuds and other devices."); q->Relayout(); return; }
        auto cached = BtCached(); GUARD
        if (!cached.empty()) Render(cached); else { SectionLabel(content, L"Connected"); Skeleton(content, 2); }
        BtDevices([this, w_](std::vector<BtDev> v) { IFALIVE Render(v); });
        q->Relayout();
    }
    void Poll(const ws& id, bool wantConnected, int tries) {
        GUARD
        BtDevices([this, w_, id, wantConnected, tries](std::vector<BtDev> v) {
            IFALIVE
            bool ok = false; for (auto& d : v) if (d.id == id && d.connected == wantConnected) ok = true;
            if (ok) { status.erase(id); if (!wantConnected) selected.clear(); Render(v); NetRefresh(); return; }
            if (tries <= 0) { if (wantConnected) status[id] = L"Couldn't connect · is it on and nearby?"; else status.erase(id); Render(devs); UiOnce(4000, [this, w_, id] { IFALIVE status.erase(id); Render(devs); }); return; }
            UiOnce(500, [this, w_, id, wantConnected, tries] { IFALIVE Poll(id, wantConnected, tries - 1); });
        });
    }
    void Connect(const BtDev& d) {
        if (status.count(d.id)) return; ws id = d.id; status[id] = L"Connecting…"; Render(devs); EnsureDots(); GUARD
        BtConnectAudio(id, true, [this, w_, id](int r) {
            IFALIVE
            if (r == 0) { Poll(id, true, 16); return; }
            status[id] = L"Couldn't connect · is it on, nearby and not connected to another device?"; Render(devs);
            UiOnce(4000, [this, w_, id] { IFALIVE status.erase(id); Render(devs); });
        });
    }
    void Disconnect(const BtDev& d) {
        ws id = d.id; status[id] = L"Disconnecting…"; Render(devs); EnsureDots(); GUARD
        BtConnectAudio(id, false, [this, w_, id](int r) {
            IFALIVE
            if (r != 0) { status.erase(id); selected.clear(); ClosePanels(); Launch(L"ms-settings:bluetooth"); return; }
            Poll(id, false, 10);
        });
    }
    void Render(const std::vector<BtDev>& v) {
        devs = v; content->Clear();
        SectionLabel(content, L"Connected");
        std::vector<BtDev> conn, paired; for (auto& d : v) (d.connected ? conn : paired).push_back(d);
        if (conn.empty()) { auto* l = MkLabel(content, L"Nothing connected right now", FText(12), Col(K::White, K::Dim)); l->margin = Thick(4, 0, 0, 12); }
        else {
            auto* grid = MkBox(content, Lay::Uniform); grid->cols = 2; grid->gap = 6; grid->margin = Thick(0, 0, 0, 10);
            for (auto& d : conn) {
                BtDev dev = d; auto* t = ClickCard(grid, [this, dev] { selected = selected == dev.id ? ws() : dev.id; Render(devs); });
                t->pad = Thick(12); t->margin = Thick(0); t->bg = LinearB(0.5f, 0, 0.5f, 1, {{0, Col(K::Accent, 0x30)}, {1, Col(K::White, 0x10)}});
                Bubble(t, BtGlyph(d.name), SolidB(K::Accent), Col(K::Black), 40)->ha = Al::Start;
                auto* nm = MkLabel(t, d.name, FText(13, 600)); nm->margin = Thick(0, 10, 0, 2);
                auto* st = MkBox(t, Lay::H); st->gap = 6; auto* dot = MkBox(st); dot->fw = dot->fh = 7; dot->radius = 3.5f; dot->bg = SolidB(K::Green); dot->va = Al::Center;
                auto it = status.find(d.id); MkLabel(st, it != status.end() ? Show(it->second) : ws(L"Connected"), FText(11.5f), Col(K::White, K::Dim));
                if (selected == d.id && it == status.end()) {
                    auto* row = MkBox(t, Lay::H); row->gap = 6; row->margin = Thick(0, 10, 0, 0);
                    MiniPill(row, L"Disconnect", 0, false, [this, dev] { Disconnect(dev); }); MiniPill(row, L"Settings", 0, false, [] { CloseAnd(L"ms-settings:bluetooth"); });
                }
            }
        }
        if (!paired.empty()) {
            SectionLabel(content, L"Paired");
            for (auto& d : paired) {
                BtDev dev = d; auto it = status.find(d.id); bool busy = it != status.end();
                auto* c = ClickCard(content, [this, dev] { Connect(dev); }); auto* row = MkBox(c, Lay::H); row->gap = 12;
                Bubble(row, BtGlyph(d.name), SolidB(K::White, K::CardHover), Col(K::White), 32);
                auto* nm = MkBox(row, Lay::V); nm->star = 1; nm->va = Al::Center; MkLabel(nm, d.name, FText(13)); MkLabel(nm, busy ? Show(it->second) : ws(L"Not connected"), FText(11.5f), Col(busy ? K::Accent : K::White, busy ? 255 : K::Dim));
                if (!busy) { auto* b = MiniPill(row, L"Connect", 0xE702, true, [this, dev] { Connect(dev); }); b->va = Al::Center; b->ha = Al::End; }   // an actual button: the whole row also works, but nothing said so
            }
        }
        q->Relayout();
    }
};

// ======================================================= Color
struct ThemePage : PageBase {
    ThemePage(QuickPanel* p) : PageBase(p) {
        PageHeader(this, L"Color", [p] { p->Back(); });
        auto* sub = MkLabel(this, L"Pick the color of the bar and the panels. DeskBar restarts for a second to apply it.", FText(12), Col(K::White, K::Dim)); sub->wrap = true; sub->margin = Thick(4, 0, 0, 14);
        auto* grid = MkBox(this, Lay::Uniform); grid->cols = 4; grid->gap = 6;
        for (int i = 0; i < kThemeCount; i++) {
            const ThemeDef& t = kThemes[i]; bool cur = &t == &CurrentTheme(); ws id = t.id;
            auto* c = ClickCard(grid, [id] { S.theme = id; SaveSettingsNow(); ClosePanels(); UiOnce(300, [] { AppQuit(true); }); }, cur);
            c->pad = Thick(4, 10); c->margin = Thick(0);
            auto* sw = MkBox(c); sw->fw = sw->fh = 40; sw->radius = 20; sw->ha = Al::Center; sw->bg = LinearB(0, 0, 1, 1, {{0, ColFor(t, K::Bar)}, {1, ColFor(t, K::Accent)}});
            if (cur) { auto* g = MkGlyph(sw, 0xE73E, 20, Col(K::White)); g->ha = g->va = Al::Center; }
            auto* l = MkLabel(c, t.name, FText(12.5f, cur ? 600 : 400)); l->ha = Al::Center; l->margin = Thick(0, 8, 0, 0);
        }
    }
};

// ======================================================= Sound
struct SoundPage : PageBase {
    View *outputs, *mixer; std::vector<AudioDev> devs;
    SoundPage(QuickPanel* p) : PageBase(p) {
        PageHeader(this, L"Sound", [p] { p->Back(); });
        SectionLabel(this, L"Output"); outputs = MkBox(this, Lay::V);
        auto* sp = MkBox(this); sp->fh = 8; SectionLabel(this, L"Volume mixer"); mixer = MkBox(this, Lay::V);
        auto* f = Footer(this); MiniPill(f, L"Sound settings", 0xE713, false, [] { CloseAnd(L"ms-settings:sound"); }); MiniPill(f, L"App volumes", 0xE9E9, false, [] { CloseAnd(L"ms-settings:apps-volume"); });
        GUARD
        AudioDevices([this, w_](std::vector<AudioDev> d) {
            IFALIVE devs = d;
            AudioApps([this, w_, d](std::vector<AppAudio> a) { IFALIVE Build(d, a); });
        });
    }
    void Reload(int ms) { GUARD UiOnce(ms, [this, w_] { IFALIVE q->ShowDetail(MakeSoundPage(q)); }); }
    void Build(const std::vector<AudioDev>& d, const std::vector<AppAudio>& apps) {
        for (auto& dev : d) { OutputRow(dev); if (dev.vol >= 0 && !S.mirrorDevices.empty() && (dev.def || MirrorIsOn(dev.id))) DeviceVolume(dev); }
        if (apps.empty()) { auto* l = MkLabel(mixer, L"No apps are playing sound right now", FText(12), Col(K::White, K::Dim)); l->margin = Thick(4, 0, 0, 8); }
        for (auto& a : apps) AppRow(a, d);
        q->Relayout();
    }
    void DeviceVolume(const AudioDev& d) {
        auto* g = MkBox(outputs, Lay::H); g->margin = Thick(4, 0, 4, 2); g->gap = 0;
        auto* n = MkLabel(g, AudioShortName(d.name), FText(11.5f), Col(K::White, K::Dim)); n->fw = 100; n->va = Al::Center;
        auto* s = g->Add(new Slider()); s->star = 1; s->value = roundf(d.vol * 100); s->va = Al::Center;
        auto* pct = MkLabel(g, Fmt(L"%d", (int)lroundf(d.vol * 100)), FText(11.5f), Col(K::White, K::Dim)); pct->fw = 34; pct->ta = TA::Right; pct->va = Al::Center;
        ws id = d.id; s->onChange = [id, pct](float v, bool) { AudioSetDeviceVolume(id, v / 100); pct->Set(Fmt(L"%d", (int)lroundf(v))); };
    }
    void OutputRow(const AudioDev& d) {
        ws id = d.id; bool head = ContainsI(d.name, L"Head") || ContainsI(d.name, L"Buds") || ContainsI(d.name, L"JBL"); bool mir = MirrorIsOn(id);
        GUARD
        auto* c = ClickCard(outputs, [this, w_, id] { if (MirrorIsOn(id)) MirrorRemove(id); AudioSetDefault(id, [this, w_] { IFALIVE Reload(350); }); }, d.def);
        auto* row = MkBox(c, Lay::H); row->gap = 12;
        Bubble(row, head ? 0xE7F6 : 0xE7F5, d.def ? SolidB(K::Accent) : mir ? SolidB(K::Green) : SolidB(K::White, K::CardHover), d.def || mir ? Col(K::Black) : Col(K::White), 32);
        auto* nm = MkBox(row, Lay::V); nm->star = 1; nm->va = Al::Center; MkLabel(nm, AudioShortName(d.name), FText(13));
        if (d.def) MkLabel(nm, L"Main output", FText(11), Col(K::White, K::Dim)); else if (mir) MkLabel(nm, L"Also playing here", FText(11), Col(K::White, K::Dim));
        if (!d.def) {
            auto* also = Pill(row, mir ? L"Playing too" : L"Also play", mir ? 0xE73E : 0xE710, false, [this, w_, id, mir] { MirrorSet(id, !mir); Reload(100); });
            also->pad = Thick(9, 3, 10, 4); also->radius = 11; also->va = Al::Center; if (mir) also->bg = SolidB(K::Green, 0x40); also->tip = mir ? L"Stop playing on this device too" : L"Play the same sound on this device too";
        }
        auto* radio = MkBox(row); radio->fw = radio->fh = 18; radio->radius = 9; radio->va = Al::Center; radio->borderW = d.def ? 5.f : 1.5f; radio->border = d.def ? Col(K::Accent) : Col(K::White, K::Dim); if (d.def) radio->bg = SolidB(K::Black);
    }
    void AppRow(const AppAudio& a, const std::vector<AudioDev>& d) {
        auto* c = Card(mixer); auto* row = MkBox(c, Lay::H);
        bool muted = a.muted; ws exe = a.exe; DWORD pid = a.pid;
        auto* mb = IconBtn(row, muted ? 0xE74F : 0xE767, 30, 16, {}); mb->bg = Brush{}; mb->va = Al::Start; mb->margin = Thick(0, 0, 10, 0); auto* mg = (Glyph*)mb->kids[0]; mb->tip = L"Mute";
        mb->onClick = [mg, exe, pid, muted]() mutable { muted = !muted; AudioSetAppMute(exe, pid, muted); mg->Set(muted ? 0xE74F : 0xE767); };
        auto* col = MkBox(row, Lay::V); col->star = 1;
        MkLabel(col, a.name, FText(12));
        auto* s = col->Add(new Slider()); s->value = roundf(a.vol * 100);
        auto* pct = MkLabel(row, Fmt(L"%d", (int)lroundf(a.vol * 100)), FText(11.5f), Col(K::White, K::Dim)); pct->fw = 34; pct->ta = TA::Right; pct->va = Al::Start; pct->margin = Thick(0, 18, 0, 0);
        s->onChange = [exe, pid, pct](float v, bool) { AudioSetAppVolume(exe, pid, v / 100); pct->Set(Fmt(L"%d", (int)lroundf(v))); };
        // "Plays on: <device>" chip: pick which output this app uses
        const AudioDev* cur = nullptr; for (auto& x : d) if (!a.routed.empty() && x.id == a.routed) cur = &x;
        auto* chip = MkBox(col, Lay::H); chip->ha = Al::Start; chip->radius = 10; chip->pad = Thick(8, 2, 8, 3); chip->margin = Thick(0, 2, 0, 0); chip->gap = 4; chip->hit = true; chip->tip = L"Which speakers or headphones this app plays on";
        chip->bg = cur ? SolidB(K::Accent, 0x40) : SolidB(K::White, 0x18); chip->bgHover = SolidB(K::White, 0x30);
        auto* g1 = MkGlyph(chip, cur ? 0xE7F6 : 0xE7F5, 10); g1->va = Al::Center; auto* lb = MkLabel(chip, cur ? AudioShortName(cur->name) : ws(L"Default output"), FText(11)); lb->maxW = 190; lb->va = Al::Center; auto* g2 = MkGlyph(chip, 0xE70D, 8); g2->va = Al::Center;
        std::vector<AudioDev> devs2 = d; ws routed = a.routed; GUARD
        chip->onClick = [this, w_, chip, devs2, routed, exe, pid] {
            std::vector<MenuItem> m; ws defName; for (auto& x : devs2) if (x.def) defName = L" (" + AudioShortName(x.name) + L")";
            m.push_back({L"Default output" + defName, 0, [this, w_, exe, pid] { AudioRouteApp(exe, pid, L""); IFALIVE Reload(300); }, false, false, routed.empty()});
            m.push_back({L"", 0, {}, true});
            for (auto& x : devs2) { ws id = x.id; m.push_back({AudioShortName(x.name), 0, [this, w_, exe, pid, id] { AudioRouteApp(exe, pid, id); IFALIVE Reload(300); }, false, false, routed == id}); }
            MenuShow(std::move(m), q->ScreenRect(chip), false, q->scale);
        };
    }
};
}
View* MakeWifiPage(QuickPanel* q) { return new WifiPage(q); }
View* MakeBtPage(QuickPanel* q) { return new BtPage(q); }
View* MakeSoundPage(QuickPanel* q) { return new SoundPage(q); }
View* MakeThemePage(QuickPanel* q) { return new ThemePage(q); }
