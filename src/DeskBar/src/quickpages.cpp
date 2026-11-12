#include "pch.h"
#include "dmstyle.h"
#include "quick.h"

// Quick Settings detail pages: Wi-Fi, Bluetooth, Sound (output devices, "also play", per-app volume and routing).
ws g_wifiPreview;   // --wifipreview <mode>: see quick.h
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

struct ConnectArt : View {   // the Wi-Fi mark in a soft disc: rings spread out and fade while connecting; a check and one burst when it worked; a warning and a shake when it did not
    int mode = 0; float pop = 1; uint64_t t0 = Now(), tDone = 0;
    ConnectArt() { fw = fh = 104; ha = Al::Center; }
    void Finish(int m) {
        mode = m; tDone = Now(); pop = 0.55f; Anim(this, &pop, 1.f, 380, Ease::CubicOut);
        if (m == 2) AnimFn(wnd, 420, Ease::Linear, [this](float t) { tx = sinf(t * 6.28318f * 2.5f) * 7.f * (1 - t); Invalidate(); }, [this] { tx = 0; Invalidate(); });
    }
    void PaintContent(Gfx& g) override {
        float cx = (rc.l + rc.r) / 2, cy = (rc.t + rc.b) / 2; double s = (double)(Now() - t0) / 1000.0;
        auto ring = [&](float ph) { float rad = 25 + ph * 25, a = (1 - ph) * (1 - ph) * 0x6E; g.Stroke({cx - rad, cy - rad, cx + rad, cy + rad}, Col(K::White, (int)a), rad, 1.6f); };
        if (mode == 0) for (int k = 0; k < 3; k++) ring(fmodf((float)(s / 2.4) + k / 3.f, 1.f));
        else { float ph = std::min(1.f, (float)((double)(Now() - tDone) / 900.0)); if (ph < 1.f) ring(ph); }
        float r = 25 * pop; g.Circle(cx, cy, r, Col(K::White, 0x40));
        g.Glyph(mode == 1 ? 0xE73E : mode == 2 ? 0xE7BA : 0xE701, {cx - r, cy - r, cx + r, cy + r}, 26 * pop, Col(K::White));
    }
};
struct Sweep : View {   // a thin track with a bright stretch running along it: "working on it", without pretending to know how far along it is
    uint64_t t0 = Now();
    Sweep() { fh = 4; }
    void PaintContent(Gfx& g) override {
        g.FillC(rc, Col(K::White, 0x30), 2);
        float w = rc.w() * 0.34f, ph = fmodf((float)((double)(Now() - t0) / 1500.0), 1.f), e = ph < 0.5f ? 2 * ph * ph : 1 - 2 * (1 - ph) * (1 - ph), x = rc.l - w + (rc.w() + w) * e;
        g.PushClip(rc, 2); g.FillC({x, rc.t, x + w, rc.b}, Col(K::White, 0xE6), 2); g.PopClip();
    }
};
Brush CardBlue() { return LinearB(0, 0, 1, 1, {{0, Col(0x3AA6F5)}, {1, Col(0x1B58C6)}}); }
Brush CardGreen() { return LinearB(0, 0, 1, 1, {{0, Col(0x3CC77A)}, {1, Col(0x1E8450)}}); }
Brush CardRed() { return LinearB(0, 0, 1, 1, {{0, Col(0xE5675F)}, {1, Col(0xA13A3A)}}); }

// ======================================================= Wi-Fi
struct WifiPage : PageBase {
    ws connecting, justConnected, formErr, savedPass, savedUser; bool askPass = false, askUser = false; uint64_t connStart = 0; int step = 0; Label* connLabel = nullptr;
    ConnectArt* art = nullptr; Sweep* sweep = nullptr; Label *connTop = nullptr, *connSub = nullptr; View *connCard = nullptr, *connActs = nullptr;   // connecting: the network being joined right now (the form stays put until the answer arrives)
    Label* netsLabel = nullptr;
    View *hero, *list, *netsHead = nullptr; ws expanded; std::vector<WifiNet> nets; Toggle* tg = nullptr; TextBox *pwd = nullptr, *user = nullptr; Label* err = nullptr; View *pwdBox = nullptr, *userBox = nullptr, *pwdHint = nullptr, *userHint = nullptr, *btn = nullptr;
    WifiPage(QuickPanel* p) : PageBase(p) {
        auto* h = PageHeader(this, L"Wi-Fi", [p] { p->Back(); }, &tg); (void)h;
        tg->Set(NetSnap().wifiOn); tg->onChange = [this](bool on) { NetSetWifi(on); GUARD UiOnce(on ? 1500 : 200, [this, w_, on] { IFALIVE Load(on); }); };
        hero = MkBox(this, Lay::V);
        auto* head = MkBox(this, Lay::H); netsHead = head; auto* sl = SectionLabel(head, L"Available networks"); sl->star = 1; sl->va = Al::Center; netsLabel = sl;
        auto* rf = IconBtn(head, 0xE72C, 36, 13, [this] { Load(true, true); });   /* bigger: at 26 a finger often missed it */ rf->bg = Brush{}; rf->tip = L"Scan again";
        list = MkBox(this, Lay::V);
        auto* f = Footer(this); MiniPill(f, L"Wi-Fi settings", 0xE713, false, [] { CloseAnd(L"ms-settings:network-wifi"); }); MiniPill(f, L"Hidden network", 0xE710, false, [] { CloseAnd(L"ms-settings:network-wifi"); });
        Load(NetSnap().wifiOn);
        if (!g_wifiPreview.empty()) {   // developer check: --wifipreview connecting|done|failed shows that card without joining anything
            GUARD ws mode = g_wifiPreview; connecting = L"Ucywifi_6GHz"; connStart = Now(); phase = 0; BuildConnecting(connecting);
            auto id = std::make_shared<UINT_PTR>(0); *id = UiTimer(33, [this, w_, id] { if (w_.expired() || phase != 0) { UiKillTimer(*id); return; } ConnTick(); });
            if (mode != L"connecting") UiOnce(400, [this, w_, mode] { IFALIVE ConnectDone(mode == L"done" ? ws() : ws(L"Windows couldn't reach the network (preview)."), connecting, false, false); });
        }
    }
    void Load(bool on, bool forceFresh = false) {
        if (!connecting.empty()) return;   // rebuilding now would throw away the password box mid-connect
        connLabel = nullptr; netsHead->visible = true; list->Clear(); hero->Clear(); NetState ns = NetSnap();
        if (!on) { Empty(list, 0xEB5E, L"Wi-Fi is off", L"Turn it on with the switch above."); q->Relayout(); return; }
        if (ns.kind == NetState::WiFi && !ns.name.empty()) BuildHero(ns.name, ns.bars, true, nullptr);
        if (nets.empty()) nets = WifiLast();
        GUARD
        if (nets.empty()) {
            WifiScan(false, [this, w_](bool, std::vector<WifiNet> v) { IFALIVE if (nets.empty() && !v.empty()) { nets = v; Render(); } });
            Skeleton(list, 4);
        } else Render();
        if (forceFresh && netsLabel) netsLabel->Set(L"Searching 26");   // the press shows at once: the list often comes back the same, which looked like nothing happened
        WifiScan(true, [this, w_](bool access, std::vector<WifiNet> v) {
            IFALIVE if (netsLabel) netsLabel->Set(L"Available networks");
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
        connLabel = nullptr; netsHead->visible = true; list->Clear(); hero->Clear(); NetState ns = NetSnap();
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
        auto* c = ClickCard(list, [this, ssid] { if (!connecting.empty()) return; expanded = expanded == ssid ? ws() : ssid; askPass = askUser = false; formErr.clear(); savedPass.clear(); savedUser.clear(); Render(); }, open);
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
        if (askPass) { pwdBox->visible = pwdHint->visible = true; pwd->SetText(savedPass, false); }   // coming back after "that password didn't work": what was typed is still there
        if (askUser) { userBox->visible = userHint->visible = true; user->SetText(savedUser, false); }
        if (!formErr.empty()) { err->Set(formErr); err->visible = true; }
        bool ent = n.enterprise; ws ssid = n.ssid; GUARD
        auto connect = [this, w_, ent, ssid] {
            if (!connecting.empty() || !pwdBox) return;
            bool usePass = pwdBox->visible; ws pass = usePass ? pwd->text : ws(), who = userBox->visible ? Trim(user->text) : ws();
            connecting = ssid;   // shuts the door at once; the card is built a moment later, outside this click (it replaces the form that was clicked)
            UiOnce(0, [this, w_, ssid, pass, who, usePass, ent] { IFALIVE StartConnect(ssid, pass, who, usePass, ent); });
        };
        btn = Pill(p, L"Connect", 0xE71B, true, connect); btn->ha = Al::End; btn->pad = Thick(18, 8);
        pwd->onEnter = connect;
    }

    // ---- connecting: the page becomes one card that shows what is going on, then what happened ----
    int phase = 0;   // 0 connecting, 1 connected, 2 failed
    void StartConnect(const ws& ssid, const ws& pass, const ws& who, bool usePass, bool ent) {
        savedPass = pass; savedUser = who; formErr.clear(); connecting = ssid; connStart = Now(); phase = 0;
        BuildConnecting(ssid); GUARD auto id = std::make_shared<UINT_PTR>(0);
        *id = UiTimer(33, [this, w_, id] { if (w_.expired() || phase != 0 || connecting.empty()) { UiKillTimer(*id); return; } ConnTick(); });
        WifiConnect(ssid, pass, who, usePass, [this, w_, ssid, usePass, ent](ws res) { IFALIVE ConnectDone(res, ssid, usePass, ent); });
    }
    void BuildConnecting(const ws& ssid) {
        connLabel = nullptr; btn = nullptr; pwd = nullptr; user = nullptr; pwdBox = userBox = pwdHint = userHint = nullptr; err = nullptr;   // the form is going away
        netsHead->visible = false; list->Clear(); hero->Clear();   // just the card: the list of networks comes back when it is over
        auto* h = MkBox(hero, Lay::V); h->radius = 14; h->pad = Thick(16, 20, 16, 18); h->margin = Thick(0, 4, 0, 16); h->bg = CardBlue(); connCard = h;
        art = new ConnectArt(); h->Add(art);
        connTop = MkLabel(h, L"Connecting to", FText(11.5f), Col(K::White, 0xCC)); connTop->ha = Al::Center; connTop->margin = Thick(0, 10, 0, 2);
        auto* nm = MkLabel(h, ssid, FText(18, 600)); nm->ha = Al::Center; nm->wrap = true; nm->ta = TA::Center;
        connSub = MkLabel(h, L"Joining the network…", FText(12.5f), Col(K::White, 0xDD)); connSub->ha = Al::Center; connSub->wrap = true; connSub->ta = TA::Center; connSub->margin = Thick(0, 6, 0, 14);
        sweep = new Sweep(); h->Add(sweep); sweep->margin = Thick(24, 0);
        connActs = MkBox(h, Lay::H); connActs->gap = 8; connActs->ha = Al::Center; connActs->margin = Thick(0, 16, 0, 0); connActs->visible = false;
        q->Relayout();
    }
    void ConnTick() {   // every frame while waiting: the rings and the running light move; the words change as the wait gets longer (they only say what we know: it is still going)
        double s = (double)(Now() - connStart) / 1000.0;
        connSub->Set(s < 2 ? L"Joining the network…" : s < 6 ? L"Almost there…" : s < 10 ? L"Taking a little longer than usual…" : L"Still trying. A weak signal or a busy router can do this.");
        art->Invalidate();
    }
    void ConnectDone(const ws& res, const ws& ssid, bool usePass, bool ent) {
        GUARD
        if (res.empty()) {   // joined: the card turns green with a check and a ring, holds for a moment, then the page shows the network as connected
            phase = 1; art->Finish(1); sweep->visible = false; connCard->bg = CardGreen(); connTop->Set(L"Connected to"); connSub->Set(L"You're on the network."); q->Relayout();
            justConnected = ssid; for (auto& n : nets) n.connected = n.ssid == ssid;   // the list on this page is from before the connect: the new network is the connected one now
            UiOnce(1300, [this, w_] { IFALIVE connecting.clear(); expanded.clear(); Load(true); UiOnce(4000, [this, w_] { IFALIVE justConnected.clear(); Load(true); }); });
            return;
        }
        if (res == L"password") {   // it wants a password (or a different one): straight back to the form, with the field open and focused
            askPass = true; askUser = ent; formErr = usePass ? ws(L"That password didn't work. Try again.") : ws(); connecting.clear(); expanded = ssid; Render();
            UiOnce(80, [this, w_] { IFALIVE if (askUser && user) q->SetFocus(user); else if (pwd) q->SetFocus(pwd); });
            return;
        }
        phase = 2; art->Finish(2); sweep->visible = false; connCard->bg = CardRed(); connTop->Set(L"Couldn't connect to"); connSub->Set(res); connActs->visible = true;
        MiniPill(connActs, L"Try again", 0xE72C, false, [this, w_, ssid, usePass, ent] { UiOnce(0, [this, w_, ssid, usePass, ent] { IFALIVE connecting.clear(); StartConnect(ssid, savedPass, savedUser, usePass, ent); }); });
        MiniPill(connActs, L"Back", 0xE72B, false, [this, w_] { UiOnce(0, [this, w_] { IFALIVE connecting.clear(); expanded.clear(); Load(true); }); });
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
        MiniPill(f, L"Refresh", 0xE72C, false, [this] { Load(true); });
        Load();
    }
    void Load(bool asked = false) {
        content->Clear(); if (!NetSnap().btOn) { Empty(content, 0xE702, L"Bluetooth is off", L"Turn it on to use your pen, earbuds and other devices."); q->Relayout(); return; }
        auto cached = BtCached(); GUARD
        if (!cached.empty() && !asked) Render(cached); else { SectionLabel(content, asked ? L"Searching 26" : L"Connected"); Skeleton(content, 2); }   // Refresh: the placeholder rows show that the press was taken
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
    std::set<ws> dupNames;
    void Build(const std::vector<AudioDev>& d, const std::vector<AppAudio>& apps) {
        dupNames.clear(); { std::map<ws, int> n; for (auto& dev : d) n[AudioShortName(dev.name)]++; for (auto& [k, c] : n) if (c > 1) dupNames.insert(k); }   // one headset listed twice (USB + Bluetooth): show the full names so they can be told apart
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
        auto* nm = MkBox(row, Lay::V); nm->star = 1; nm->va = Al::Center; MkLabel(nm, dupNames.count(AudioShortName(d.name)) ? d.name : AudioShortName(d.name), FText(13));
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

// ======================================================= Screens and power (what DeskMode did, now here, in DeskMode's own look)
// DeskMode's panel, piece by piece: the big readout of each screen, the two preset cards, the power-mode strip, and one card per screen with its refresh rates.
namespace dm {
// DeskMode's colours: written as DeskMode's blues, then passed through the bar's theme (see Raw)
D2D1_COLOR_F Raw(uint32_t rgb, int a) { return CurrentTheme().kind == 0 ? Col(rgb, a) : D2D1::ColorF(rgb, a / 255.f); }   // DeskMode wears the bar's colour (pink, teal...); Blue and the colourless themes (Black, White, Glass) keep DeskMode's own blue
Brush Glow() { return LinearB(0, 0, 1, 1, {{0, Raw(0x1167C4)}, {1, Raw(0x1FA7B9)}}); }   // DeskMode's accent gradient
Brush Fill(int a) { return SolidC(Raw(0xFFFFFF, a)); }
View* Card(View* parent) { auto* c = MkBox(parent, Lay::V); c->bg = Fill(0x0C); c->radius = 14; c->pad = Thick(14, 12); c->borderW = 1; c->border = Raw(0xFFFFFF, 0x14); c->margin = Thick(0, 0, 0, 10); return c; }
View* Title(View* parent, const ws& t) { auto* l = MkLabel(parent, t, FText(13.5f), Col(K::White, 0xD8)); l->margin = Thick(4, 6, 0, 8); return l; }
// a strip of equal segments; the chosen one wears the gradient (DeskMode's segmented control)
void Strip(View* parent, const std::vector<ws>& items, int chosen, int cols, std::function<void(int)> pick) {
    auto* bg = MkBox(parent, Lay::Uniform); bg->cols = std::max(1, std::min((int)items.size(), cols)); bg->bg = Fill(0x0A); bg->radius = 10; bg->pad = Thick(4); bg->gap = 2;
    for (int i = 0; i < (int)items.size(); i++) {
        auto* it = MkBox(bg, Lay::H); it->hit = true; it->radius = 8; it->pad = items.size() <= 3 ? Thick(1, 8) : Thick(6, 8); it->bgHover = SolidB(K::White, 0x14);
        if (i == chosen) { it->bg = Glow(); it->bgHover = Glow(); }
        auto* l = MkLabel(it, items[i], FText(items.size() <= 3 ? 12.5f : 13, i == chosen ? 600 : 400)); l->ha = Al::Center; l->star = 1; l->ta = TA::Center;
        it->onClick = [pick, i] { pick(i); };
    }
}
}
struct DisplayPage : PageBase {
    DisplayPage(QuickPanel* p) : PageBase(p) {
        bg = SolidC(dm::Raw(0x202226)); radius = 0; lay = Lay::Overlay; pad = Thick(0);   // DeskMode's dark panel
        auto* glowA = MkBox(this); glowA->bg = RadialRel(0.05f, 0.0f, 0.75f, 0.32f, {{0, dm::Raw(0x1167C4, 0x80)}, {1, dm::Raw(0x1167C4, 0)}}); glowA->hit = false;   // DeskMode's two soft glows
        auto* glowB = MkBox(this); glowB->bg = RadialRel(1.0f, 1.0f, 0.8f, 0.35f, {{0, dm::Raw(0x1FA7B9, 0x5A)}, {1, dm::Raw(0x1FA7B9, 0)}}); glowB->hit = false;
        auto* col = MkBox(this, Lay::V); col->pad = Thick(18, 16, 18, 14);
        auto scr = ScreenModes(); int pm = PowerModeNow();
        bool ext0 = std::any_of(scr.begin(), scr.end(), [](const ScreenMode& m) { return !m.internal; }); ws extName; for (auto& m : scr) if (!m.internal) extName = m.name;
        {   // DeskMode's header: the app tile (back to Quick Settings), the name, the status pills, the gear
            auto* h = MkBox(col, Lay::H); h->gap = 12; h->margin = Thick(0, 0, 0, 14);
            auto* tile = MkBox(h); tile->fw = tile->fh = 34; tile->radius = 8; tile->bg = SolidC(dm::Raw(0x4CC2FF)); tile->hit = true; tile->va = Al::Start; tile->tip = L"Back to Quick Settings"; tile->onClick = [p] { p->Back(); };
            auto* tg = MkGlyph(tile, 0xE7F4, 17, dm::Raw(0x000000)); tg->ha = tg->va = Al::Center;
            auto* tx = MkBox(h, Lay::V); tx->star = 1;
            MkLabel(tx, L"DeskMode", FText(15, 600));
            auto* pills = MkBox(tx, Lay::H); pills->gap = 6; pills->margin = Thick(0, 5, 0, 0);
            auto pill = [&](const ws& t, bool dot) { auto* bx = MkBox(pills, Lay::H); bx->bg = dm::Fill(0x12); bx->radius = 9; bx->pad = Thick(8, 2, 9, 3); bx->gap = 6;
                if (dot) { auto* d = MkBox(bx); d->fw = d->fh = 7; d->radius = 3.5f; d->va = Al::Center; d->bg = SolidC(ext0 ? dm::Raw(0x4CC2FF) : dm::Raw(0xFFFFFF, 0x90)); }
                MkLabel(bx, t, FText(12), Col(K::White, 0xB0)); };
            pill(ext0 ? extName + L" connected" : ws(L"No external monitor"), true);
            SYSTEM_POWER_STATUS ps{}; GetSystemPowerStatus(&ps); bool ac = ps.ACLineStatus != 0; int pct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1;
            pill(ws(ac ? L"Plugged in" : L"On battery") + (pct >= 0 ? Fmt(L" \u00B7 %d%%", pct) : ws()), false);
            auto* gear = IconBtn(h, 0xE713, 32, 15, [p] { p->ShowDetail(MakeDeskModeSettings(p), true); }); gear->bg = Brush{}; gear->va = Al::Start; gear->tip = L"Settings";
        }
        // what each screen is doing, big
        auto* hero = MkBox(col, Lay::Uniform); hero->cols = std::max<int>(1, (int)scr.size()); hero->gap = 10; hero->margin = Thick(0, 4, 0, 6);
        for (auto& sc : scr) {
            auto* hc = MkBox(hero, Lay::V);
            auto* t = MkBox(hc, Lay::H); t->bg = dm::Glow(); t->radius = 14; t->pad = Thick(8, 8); t->hit = true; t->tip = L"Change its refresh rate"; t->onClick = [p] { p->ShowDetail(MakeDeskModeRates(p), true); }; t->borderW = 1.5f; t->border = Col(0x7FD3FF, 0xA0);
            auto* sp = MkBox(t, Lay::H); sp->ha = Al::Center; sp->star = 1;
            auto* n = MkLabel(sp, std::to_wstring(sc.hz), FDisp(34, 300)); n->va = Al::Center;
            auto* hz = MkLabel(sp, L"Hz", FText(12), Col(K::White, 0xB0)); hz->va = Al::End; hz->margin = Thick(4, 0, 0, 10);
            auto* stand = MkBox(hc); stand->fw = 34; stand->fh = 5; stand->radius = 2.5f; stand->ha = Al::Center; stand->bg = dm::Fill(0x22); stand->margin = Thick(0, 4, 0, 0);   // the monitor's little stand
            auto* nm = MkLabel(hc, sc.internal ? L"Built-in" : sc.name, FText(12), Col(K::White, 0xB0)); nm->ha = Al::Center; nm->margin = Thick(0, 6, 0, 0);
        }
        // the presets, as cards
        dm::Title(col, L"Apply a preset");
        bool atMax = !scr.empty(), atEasy = !scr.empty();
        for (auto& sc : scr) { if (sc.rates.empty()) continue; if (sc.hz != PickRate(sc, sc.internal ? S.dmMaxLaptopHz : S.dmMaxMonitorHz)) atMax = false; if (sc.hz != PickRate(sc, S.dmEasyHz)) atEasy = false; }
        auto* pr = MkBox(col, Lay::Uniform); pr->cols = ext0 ? 3 : 2; pr->gap = 10; pr->margin = Thick(0, 0, 0, 6);
        ws maxSub, easySub = std::to_wstring(S.dmEasyHz) + L"Hz" + (S.dmEasySaverOn ? L" \u00B7 saver" : L"");
        for (auto& sc : scr) if (!sc.rates.empty()) maxSub += (maxSub.empty() ? L"" : L" + ") + std::to_wstring(PickRate(sc, sc.internal ? S.dmMaxLaptopHz : S.dmMaxMonitorHz)); maxSub += L"Hz";
        Preset(pr, 0xEC4A, L"Go max", maxSub, atMax && pm == S.dmMaxMode, [this, scr] { Apply(scr, true); });
        if (ext0) {   // DeskMode's "monitor first": the monitor at its best, the built-in screen at 60
            ws firstSub = L"60 + "; for (auto& sc : scr) if (!sc.internal && !sc.rates.empty()) firstSub += std::to_wstring(sc.rates.back()); firstSub += L"Hz";
            bool atFirst = true; for (auto& sc : scr) { if (sc.rates.empty()) continue; if (sc.internal ? sc.hz > 60 : sc.hz != sc.rates.back()) atFirst = false; }
            ws shortName = extName.substr(0, extName.find(L' '));
            Preset(pr, 0xE7F4, shortName + L" first", firstSub, atFirst && !atMax && pm == 2, [this] { GUARD std::thread([this, w_] { DeskModeFirst(); UiPost([this, w_] { IFALIVE q->ShowDetail(MakeDisplayPage(q), true); }); }).detach(); });
        }
        Preset(pr, 0xEC48, L"Go easy", easySub, atEasy && pm == S.dmEasyMode, [this, scr] { Apply(scr, false); });
        // power mode
        dm::Title(col, L"Power mode right now");
        auto* pc = dm::Card(col);
        dm::Strip(pc, {L"Recommended", L"Better", L"Best"}, pm, 3, [this](int i) { Change({}, i); });

        { int es = EnergySaverIsOn(); if (es >= 0) {
            auto* r = MkBox(pc, Lay::H); r->margin = Thick(2, 12, 0, 0); auto* g = MkGlyph(r, 0xEBB4, 14, Col(K::White, 0xC0)); g->va = Al::Center; g->margin = Thick(0, 0, 10, 0);
            auto* l = MkLabel(r, L"Energy saver", FText(13.5f)); l->star = 1; l->va = Al::Center;
            auto* t = r->Add(new Toggle()); t->va = Al::Center; t->Set(es == 1); t->onChange = [](bool on) { std::thread([on] { EnergySaverSet(on); }).detach(); }; } }
        // the screens: each one's refresh rates and brightness, in one card (kept compact so the panel needs no scrolling)
        dm::Title(col, L"Screens");
        auto* bcard = dm::Card(col);
        for (size_t si = 0; si < scr.size(); si++) {
            auto& sc = scr[si];
            auto* hh = MkBox(bcard, Lay::H); hh->margin = Thick(2, si ? 14 : 0, 0, 8);
            auto* nm = MkLabel(hh, sc.internal ? L"Built-in" : sc.name, FText(13.5f, 600)); nm->star = 1;
            MkLabel(hh, Fmt(L"%d Hz", sc.hz), FText(12.5f), dm::Raw(0x7FD3FF));
            std::vector<ws> rs; int chosen = -1; for (size_t i = 0; i < sc.rates.size(); i++) { rs.push_back(std::to_wstring(sc.rates[i])); if (sc.rates[i] == sc.hz) chosen = (int)i; }
            ws dev = sc.device; auto rates = sc.rates;
            dm::Strip(bcard, rs, chosen, std::min<int>(8, (int)rs.size()), [this, dev, rates](int i) { Change({{dev, rates[i]}}, -1); });
            Brightness(bcard, sc.device, L"");
        }
        if (scr.size() > 1) {   // both screens at once, keeping the gap between them (DeskMode's "both" slider)
            auto* row = MkBox(bcard, Lay::H); row->gap = 10; row->margin = Thick(2, 12, 0, 0);
            auto* l = MkLabel(row, L"Both", FText(13.5f)); l->fw = 56; l->va = Al::Center;
            auto* s = row->Add(new Slider()); s->star = 1; s->va = Al::Center; both = s; s->value = 50;
            auto* pct = MkLabel(row, L"", FText(12.5f), Col(K::White, 0xB0)); pct->fw = 40; pct->ta = TA::Right; pct->va = Al::Center; bothPct = pct;
            s->onChange = [this](float v, bool) {
                if (bothBase < 0) bothBase = v;   // the first move: the gap is what the screens have now
                float d = v - bothBase; for (auto& [dev, sl] : sliders) { int to = std::clamp((int)lroundf(base[dev] + d), 0, 100); BrightSet(dev, to); sl->Set((float)to); }
                bothPct->Set(Fmt(L"%d%%", (int)lroundf(v)));
            };
        }

    }
    std::map<ws, Slider*> sliders; std::map<ws, float> base; Slider* both = nullptr; Label* bothPct = nullptr; float bothBase = -1;
    void Brightness(View* card, const ws& dev, const ws& label) {   // the screen's brightness (the monitor over DDC/CI, like DeskMode), greyed when the screen doesn't allow it
        auto* row = MkBox(card, Lay::H); row->gap = 10; row->margin = Thick(2, 8, 0, 0);
        if (label.empty()) { auto* gl = MkGlyph(row, 0xE706, 15, Col(K::White, 0xC0)); gl->va = Al::Center; } else { auto* g = MkLabel(row, label, FText(13)); g->fw = 56; g->va = Al::Center; }
        auto* s = row->Add(new Slider()); s->star = 1; s->va = Al::Center; s->enabled = false; s->opacity = 0.5f;
        auto* pct = MkLabel(row, L"", FText(12.5f), Col(K::White, 0xB0)); pct->fw = 40; pct->ta = TA::Right; pct->va = Al::Center;
        sliders[dev] = s;
        s->onChange = [this, dev, pct](float v, bool) { BrightSet(dev, (int)lroundf(v)); pct->Set(Fmt(L"%d%%", (int)lroundf(v))); base[dev] = v; bothBase = -1; };
        Mon m{}; bool found = false; for (auto& x : Monitors()) { MONITORINFOEXW mi{}; mi.cbSize = sizeof mi; if (GetMonitorInfoW(x.h, &mi) && EqI(mi.szDevice, dev)) { m = x; found = true; } }
        if (!found) return;
        GUARD BrightOpen(m, [this, w_, s, pct, dev](bool ok, int v) {
            IFALIVE s->enabled = ok; s->opacity = ok ? 1.f : 0.5f; if (v >= 0) { s->Set((float)v); pct->Set(Fmt(L"%d%%", v)); base[dev] = (float)v; }
            if (both && !base.empty()) { float avg = 0; for (auto& [k, b] : base) avg += b; avg /= base.size(); both->Set(avg); bothPct->Set(Fmt(L"%d%%", (int)lroundf(avg))); bothBase = -1; }
            Invalidate();
        });
    }
    void Preset(View* parent, wchar_t glyph, const ws& title, const ws& sub, bool on, std::function<void()> fn) {
        auto* c = MkBox(parent, Lay::V); c->hit = true; c->radius = 14; c->pad = Thick(12, 10); c->borderW = 1; c->border = Col(K::White, on ? 0x60 : 0x1C);
        c->bg = on ? dm::Glow() : dm::Fill(0x0C); c->bgHover = on ? dm::Glow() : dm::Fill(0x18); c->onClick = std::move(fn);
        auto* disc = MkBox(c); disc->fw = disc->fh = 32; disc->radius = 16; disc->ha = Al::Start; disc->bg = SolidB(K::White, on ? 0x30 : 0x18);
        auto* g = MkGlyph(disc, glyph, 16); g->ha = g->va = Al::Center;
        auto* t = MkLabel(c, title, FText(14.5f, 600)); t->margin = Thick(0, 8, 0, 1);
        MkLabel(c, sub, FText(12), Col(K::White, 0xB0));
    }
    void Apply(const std::vector<ScreenMode>&, bool max) { GUARD std::thread([this, w_, max] { DeskModeGo(max); UiPost([this, w_] { IFALIVE q->ShowDetail(MakeDisplayPage(q), true); }); }).detach(); }
    void Change(std::vector<std::pair<ws, int>> want, int mode) {
        GUARD std::thread([this, w_, want, mode] {
            if (mode >= 0) SetPowerModeNow(mode);   // the power mode first: Windows reshuffles the screens when it changes
            if (!want.empty()) SetScreenRates(want);
            UiPost([this, w_] { IFALIVE q->ShowDetail(MakeDisplayPage(q), true); });
        }).detach();
    }
};
View* MakeDisplayPage(QuickPanel* q) { return new DisplayPage(q); }

// Each screen's refresh rates (a tap on a screen in DeskMode's panel)
struct DeskModeRates : PageBase {
    DeskModeRates(QuickPanel* p) : PageBase(p) {
        bg = SolidC(dm::Raw(0x202226)); radius = 0;
        auto* h = MkBox(this, Lay::H); h->gap = 10; h->margin = Thick(0, 0, 0, 6);
        auto* bk = IconBtn(h, 0xE72B, 32, 14, [p] { p->ShowDetail(MakeDisplayPage(p), true); }); bk->bg = Brush{}; bk->tip = L"Back";
        auto* t = MkLabel(h, L"Refresh rate", FText(17, 600)); t->va = Al::Center;
        for (auto& sc : ScreenModes()) {
            auto* c = dm::Card(this);
            auto* hh = MkBox(c, Lay::H); hh->margin = Thick(0, 0, 0, 10);
            auto* nm = MkLabel(hh, sc.internal ? L"Built-in" : sc.name, FText(14, 600)); nm->star = 1;
            MkLabel(hh, Fmt(L"%d Hz", sc.hz), FText(13), dm::Raw(0x7FD3FF));
            std::vector<ws> rs; int chosen = -1; for (size_t i = 0; i < sc.rates.size(); i++) { rs.push_back(std::to_wstring(sc.rates[i])); if (sc.rates[i] == sc.hz) chosen = (int)i; }
            ws dev = sc.device; auto rates = sc.rates;
            dm::Strip(c, rs, chosen, std::min<int>(8, (int)rs.size()), [this, dev, rates](int i) {
                GUARD std::thread([this, w_, dev, rates, i] { SetScreenRates({{dev, rates[i]}}); UiPost([this, w_] { IFALIVE q->ShowDetail(MakeDeskModeRates(q), true); }); }).detach(); });
        }
        auto* note = MkLabel(this, L"Each change makes Windows blank the screen for a moment.", FText(11.5f), Col(K::White, 0xA0)); note->wrap = true; note->margin = Thick(4, 2, 0, 0);
    }
};
View* MakeDeskModeRates(QuickPanel* q) { return new DeskModeRates(q); }

// DeskMode's Settings page, in the same panel and the same look: what Go max and Go easy set, and what DeskMode does by itself.
struct DeskModeSettings : PageBase {
    DeskModeSettings(QuickPanel* p) : PageBase(p) {
        bg = SolidC(dm::Raw(0x202226)); radius = 0;
        auto* h = MkBox(this, Lay::H); h->gap = 10; h->margin = Thick(0, 0, 0, 6);
        auto* bk = IconBtn(h, 0xE72B, 32, 14, [p] { p->ShowDetail(MakeDisplayPage(p), true); }); bk->bg = Brush{}; bk->tip = L"Back";
        auto* t = MkLabel(h, L"Settings", FText(17, 600)); t->va = Al::Center;
        auto* sub = MkLabel(this, L"Everything here applies as soon as you change it.", FText(12), Col(K::White, 0xA0)); sub->margin = Thick(4, 0, 0, 4);
        auto scr = ScreenModes(); std::vector<int> lap, mon; for (auto& s : scr) (s.internal ? lap : mon) = s.rates;
        auto rateStrip = [&](View* c, const ws& label, const std::vector<int>& rates, int* val, bool withMax) {
            if (rates.empty()) return; auto* l = MkLabel(c, label, FText(12.5f), Col(K::White, 0xB0)); l->margin = Thick(2, 8, 0, 6);
            std::vector<ws> items; std::vector<int> vals; if (withMax) { items.push_back(L"Max"); vals.push_back(0); } for (int r : rates) { items.push_back(std::to_wstring(r)); vals.push_back(r); }
            int chosen = 0; for (size_t i = 0; i < vals.size(); i++) if (vals[i] == *val) chosen = (int)i;
            dm::Strip(c, items, chosen, 5, [this, val, vals](int i) { *val = vals[i]; SaveSettingsSoon(); Reload(); });
        };
        auto modeStrip = [&](View* c, int* val) { auto* l = MkLabel(c, L"Power mode", FText(12.5f), Col(K::White, 0xB0)); l->margin = Thick(2, 10, 0, 6);
            dm::Strip(c, {L"Recommended", L"Better", L"Best"}, *val, 3, [this, val](int i) { *val = i; SaveSettingsSoon(); Reload(); }); };
        auto sw = [&](View* c, const ws& text, bool* val) {
            auto* r = MkBox(c, Lay::H); r->margin = Thick(2, 10, 0, 0); auto* l = MkLabel(r, text, FText(13)); l->star = 1; l->wrap = true; l->va = Al::Center;
            auto* tg = r->Add(new Toggle()); tg->va = Al::Center; tg->Set(*val); tg->onChange = [val](bool on) { *val = on; SaveSettingsSoon(); }; };
        dm::Title(this, L"\u201CGo max\u201D"); auto* c1 = dm::Card(this);
        auto* d1 = MkLabel(c1, L"What the Go max button (and Ctrl+Alt+M) sets. \u201CMax\u201D uses the best refresh rate each screen offers.", FText(12), Col(K::White, 0xB0)); d1->wrap = true;
        rateStrip(c1, L"Built-in screen", lap, &S.dmMaxLaptopHz, true); rateStrip(c1, L"Monitor", mon, &S.dmMaxMonitorHz, true); modeStrip(c1, &S.dmMaxMode); sw(c1, L"Turn Energy saver off", &S.dmMaxSaverOff);
        dm::Title(this, L"\u201CGo easy\u201D"); auto* c2 = dm::Card(this);
        auto* d2 = MkLabel(c2, L"What the Go easy button (and Ctrl+Alt+B) sets: a gentle refresh rate on every screen, for battery life and quiet fans.", FText(12), Col(K::White, 0xB0)); d2->wrap = true;
        std::vector<int> all; for (auto& s : scr) for (int r : s.rates) if (std::find(all.begin(), all.end(), r) == all.end()) all.push_back(r); std::sort(all.begin(), all.end());
        rateStrip(c2, L"Refresh rate", all, &S.dmEasyHz, false); modeStrip(c2, &S.dmEasyMode); sw(c2, L"Turn Energy saver on", &S.dmEasySaverOn);
        dm::Title(this, L"By itself"); auto* c3 = dm::Card(this);
        sw(c3, L"Go max when a monitor and the charger are connected", &S.dmAutoMax);
        sw(c3, L"Go back to how things were when the PC runs on battery or the monitor is unplugged", &S.dmRestore);
        sw(c3, L"Leave screen modes alone while a full-screen game is up", &S.dmGameLock);
        dm::Title(this, L"Shortcuts"); auto* c4 = dm::Card(this);
        auto* k = MkLabel(c4, L"Ctrl+Alt+D the panel \u00B7 M Go max \u00B7 B Go easy \u00B7 F the window in front without its frame (borderless)", FText(12.5f), Col(K::White, 0xC0)); k->wrap = true;
    }
    void Reload() { GUARD UiOnce(0, [this, w_] { IFALIVE q->ShowDetail(MakeDeskModeSettings(q), true); }); }
};
View* MakeDeskModeSettings(QuickPanel* q) { return new DeskModeSettings(q); }
