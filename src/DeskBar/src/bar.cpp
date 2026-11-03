#include "pch.h"
#include "bar.h"
#include "search.h"
#include <shobjidl.h>

ws g_devScreen = L"surface";
static void BarRegisterDrop(Bar* b);
static std::vector<std::unique_ptr<Bar>> g_bars;
static std::vector<ws> g_order;                       // first-seen order of unpinned groups, so buttons don't reshuffle
static ws g_monSig;
static const float kBarH = 48;

// ======================================================================= entries
static std::vector<Entry> BuildEntries(const WinSnap& snap) {
    std::vector<Entry> out; std::vector<char> used(snap.list.size(), 0);
    for (auto& pe : S.taskbarPins) {
        Entry e; e.pin = ResolvePin(pe); e.key = L"p:" + pe; e.pinned = true; e.name = e.pin.name; e.iconKey = e.pin.iconKey;
        for (size_t j = 0; j < snap.list.size(); j++) if (!used[j] && PinMatchesWindow(e.pin, snap.list[j])) { used[j] = 1; e.wins.push_back(snap.list[j]); }
        out.push_back(std::move(e));
    }
    std::map<ws, Entry> groups; std::vector<ws> present;
    for (size_t j = 0; j < snap.list.size(); j++) if (!used[j]) {
        auto& w = snap.list[j]; ws k = WinGroupKey(w); auto it = groups.find(k);
        if (it == groups.end()) { Entry e; e.key = k; e.name = WinGroupName(w); e.iconKey = WinIconKey(w); it = groups.emplace(k, e).first; present.push_back(k); }
        it->second.wins.push_back(w);
    }
    std::erase_if(g_order, [&](const ws& k) { return !groups.count(k); });
    for (auto it = present.rbegin(); it != present.rend(); ++it) if (std::find(g_order.begin(), g_order.end(), *it) == g_order.end()) g_order.push_back(*it);
    for (auto& k : g_order) out.push_back(std::move(groups[k]));
    return out;
}

// ======================================================================= Start button
struct StartBtn : View {
    float s = 1; bool was = false;
    StartBtn() { fw = 44; fh = 40; margin = Thick(2, 0); va = Al::Center; ha = Al::Start; hit = true; radius = 4; bgHover = SolidB(K::White, 0x1A); bgPress = SolidB(K::White, 0x10); tip = L"Start"; }
    void PaintContent(Gfx& g) override {
        if (pressed != was) { was = pressed; Anim(this, &s, pressed ? 0.84f : 1.f, pressed ? 80 : 140); }
        float cx = rc.cx(), cy = rc.cy(), u = 22 * s; Rf r = Rf::XYWH(cx - u / 2, cy - u / 2, u, u);
        float q = 10.2f * s, gp = 11.8f * s; Brush b = LinearB(0, 0, 1, 1, {{0, Col(0x7BD3FF)}, {1, Col(0x1F9CF0)}});
        for (int i = 0; i < 4; i++) { Rf c = Rf::XYWH(r.l + (i & 1) * gp, r.t + (i >> 1) * gp, q, q); g.Fill(c, b, 1); }
    }
};
struct Strip : View {       // show-desktop strip at the far right
    Strip() { fw = 8; hit = true; bg = SolidB(K::White, 0x01); tip = L"Show desktop"; onClick = [] { ToggleDesktop(); }; }
    void PaintContent(Gfx& g) override { if (hovered) g.FillC({rc.l, rc.t, rc.l + 1, rc.b}, Col(K::White, 0x50)); }
};

// ======================================================================= TaskBtn
TaskBtn::TaskBtn() {
    fw = 44; fh = 40; margin = Thick(2, 0); va = Al::Center; ha = Al::Start; hit = true; radius = 4;
    bgHover = SolidB(K::White, 0x1A); bgPress = SolidB(K::White, 0x10);
    onClick = [this] { Click(GetKeyState(VK_SHIFT) & 0x8000); };
    onMiddle = [this] { Launch_(); };
    onRight = [this] { Menu(); }; onLong = [this] { Menu(); };
    onHover = [this](bool in) {
        if (hoverTimer) { UiKillTimer(hoverTimer); hoverTimer = 0; }
        if (in) {
            if (wnd && wnd->touchLast) return;
            auto open = [this] { if (hoverTimer) { UiKillTimer(hoverTimer); hoverTimer = 0; } PreviewOpen(Req(false)); };
            if (PreviewShowingFor(L"*")) open(); else hoverTimer = UiTimer(300, open);
        } else PreviewHideSoon();
    };
}
TaskBtn::~TaskBtn() { if (hoverTimer) UiKillTimer(hoverTimer); }
PreviewReq TaskBtn::Req(bool touch) { return {wnd, this, e.key, e.name, e.iconKey, e.wins, touch}; }
void TaskBtn::Update(const Entry& n, HWND fg) {
    e = n; bool act = false, fl = false;
    for (auto& w : e.wins) { if (w.h == fg) act = true; if (w.flashing) fl = true; }
    active = act; flashing = fl && !act;
    float t = e.wins.empty() ? 0 : (active || flashing) ? 16.f : 6.f;
    if (t != indW) { if (indW == 0 || t == 0) { indW = t; } else Anim(this, &indW, t, 160); }
    Invalidate();
}
void TaskBtn::PaintContent(Gfx& g) {
    if (wasP != pressed) { wasP = pressed; Anim(this, &iconScale, pressed ? 0.84f : 1.f, pressed ? 80 : 140); }
    float cx = rc.cx(), cy = rc.cy(); bool stack = e.wins.size() > 1;
    if (flashing) g.FillC(rc, Col(K::Flash, 0x55), 4);
    if (stack) {
        g.FillC(Rf::XYWH(rc.l + 3.5f, rc.cy() - 17.5f, 33, 35), Col(K::White, 0x15), 5);
        g.FillC(Rf::XYWH(rc.l + 38.5f, rc.cy() - 17.5f, 2, 35), Col(K::White, 0x15), 2);
    }
    float isz = 24 * iconScale; Rf ir = Rf::XYWH(cx - isz / 2, cy - isz / 2, isz, isz);
    ImgP im = IconGet(e.iconKey, (int)lroundf(24 * g.scale));
    if (im) g.Image(*im, ir);
    else { g.Circle(cx, cy, 13 * iconScale, Col(K::White, 0x40)); ws l = e.name.empty() ? L"?" : ws(1, towupper(e.name[0])); g.Text(l, Rf::XYWH(cx - 13, cy - 13, 26, 26), FText(13), Col(K::White), TA::Center); }
    if (indW > 0.1f) {
        float y = rc.b - (stack ? 2 : 1) - 3; D2D1_COLOR_F c = flashing ? Col(K::Flash) : active ? Col(K::Accent) : Col(K::White, 0xB0);
        g.FillC({cx - indW / 2, y, cx + indW / 2, y + 3}, c, 1.5f);
    }
}
void TaskBtn::Launch_() {
    uint64_t t = Now(); if (t - launchedAt < 1500) return; launchedAt = t;
    ws tgt = e.pinned ? e.pin.launch : ws();
    if (tgt.empty() && !e.wins.empty()) { auto& w = e.wins[0]; tgt = (!w.aumid.empty() && w.aumid.find(L'!') != ws::npos) ? L"shell:AppsFolder\\" + w.aumid : w.exe; }
    if (!tgt.empty()) Launch(tgt);
}
void TaskBtn::Click(bool shift) {
    PreviewClose();
    if (e.wins.empty() || shift) { Launch_(); return; }
    HWND fg = GetForegroundWindow();
    if (e.wins.size() == 1) {
        auto& w = e.wins[0];
        if (fg == w.h && !IsIconic(w.h)) ShowWindow(w.h, SW_MINIMIZE); else ForceForeground(w.h);
        WinTrackerPoke(); return;
    }
    bool inGroup = false; for (auto& w : e.wins) if (w.h == fg) inGroup = true;
    if (wnd->touchLast || inGroup) { PreviewOpen(Req(wnd->touchLast)); return; }
    ForceForeground(e.wins[0].h); WinTrackerPoke();   // most recently used (top of the z-order)
}
void TaskBtn::CycleWindow(float dir) {
    if (e.wins.size() < 2) return;
    std::vector<HWND> hs; for (auto& w : e.wins) hs.push_back(w.h); std::sort(hs.begin(), hs.end());
    HWND fg = GetForegroundWindow(); size_t i = std::find(hs.begin(), hs.end(), fg) - hs.begin();
    i = i >= hs.size() ? 0 : (i + (dir > 0 ? hs.size() - 1 : 1)) % hs.size();   // wheel up = previous
    ForceForeground(hs[i]); WinTrackerPoke();
}
bool TaskBtn::OnWheel(float d, float, float) { if (e.wins.size() < 2) return false; CycleWindow(d); return true; }
void TaskBtn::Menu() {
    PreviewClose();
    std::vector<MenuItem> m; auto self = e; auto* bar = (Bar*)wnd;
    m.push_back({self.name, 0xE8A7, [this] { Launch_(); }});
    if (e.pinned) m.push_back({L"Run as administrator", 0xE7EF, [self] { Launch(self.pin.launch, L"", true); }});
    {   // up to 6 recent files that this app opens, from Windows' Recent folder (cached, never blocks)
        ws exeName = Lower(FileNameOf(e.pinned && !e.pin.exe.empty() ? e.pin.exe : (e.wins.empty() ? ws() : e.wins[0].exe))); int n = 0; bool head = false;
        if (!exeName.empty()) for (auto& r : RecentCached()) {
            if (Lower(FileNameOf(ExecutableFor(r.ext))) != exeName) continue; if (!head) { m.push_back({L"", 0, {}, true}); head = true; }
            ws path = r.path; m.push_back({r.name, 0xE8A5, [path] { Launch(path); }}); if (++n >= 6) break;
        }
    }
    m.push_back({L"", 0, {}, true});
    if (e.pinned) m.push_back({L"Unpin from taskbar", 0xE77A, [self] { std::erase(S.taskbarPins, self.pin.entry); SaveSettingsSoon(); BarsSync(); }});
    else if (!e.wins.empty() && !e.wins[0].exe.empty()) {
        ws ent = (!e.wins[0].aumid.empty() && e.wins[0].aumid.find(L'!') != ws::npos) ? L"app:" + e.wins[0].aumid : e.wins[0].exe;
        m.push_back({L"Pin to taskbar", 0xE718, [ent] { S.taskbarPins.push_back(ent); SaveSettingsSoon(); BarsSync(); }});
    }
    if (!e.wins.empty()) {
        if (Monitors().size() > 1) { auto ws_ = e.wins; m.push_back({L"Send to other screen", 0xE7F4, [ws_] { for (auto& w : ws_) { SendToNextScreen(w.h); break; } }}); }
        auto wins = e.wins;
        m.push_back({L"End task", 0xE71A, [wins] {
            std::set<DWORD> done;
            for (auto& w : wins) {
                ws n = Lower(FileNameOf(w.exe));
                if (n == L"explorer.exe" || n == L"applicationframehost.exe" || n == L"csrss.exe" || n == L"dwm.exe" || n == L"deskbar.exe" || !done.insert(w.pid).second) continue;
                ws cmd = Fmt(L"taskkill.exe /PID %lu /T /F", w.pid); STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESHOWWINDOW; PROCESS_INFORMATION pi{};
                if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
            }
        }});
        m.push_back({e.wins.size() > 1 ? Fmt(L"Close all %d windows", (int)e.wins.size()) : L"Close window", 0xE711, [wins] { for (auto& w : wins) PostMessageW(w.h, WM_CLOSE, 0, 0); }});
    }
    MenuShow(std::move(m), wnd->ScreenRect(this), !g_dev, wnd->scale); (void)bar;
}
// drag to reorder (pinned only)
bool TaskBtn::OnDown(float, float, bool) { return false; }
bool TaskBtn::OnPressMove(float x, float) {
    if (!e.pinned || std::fabs(x - wnd->downX) < 10) return false;
    dragging = true; opacity = 0.7f; dragFrom = wnd->downX; PreviewClose(); return true;
}
void TaskBtn::OnDrag(float x, float) { tx = x - dragFrom; Invalidate(); }
void TaskBtn::OnUp(float x, float) {
    float dx = x - dragFrom; dragging = false; opacity = 1; tx = 0; Invalidate();
    int shift = (int)lroundf(dx / 48); if (!shift) return;
    auto it = std::find(S.taskbarPins.begin(), S.taskbarPins.end(), e.pin.entry); if (it == S.taskbarPins.end()) return;
    int from = (int)(it - S.taskbarPins.begin()), to = std::clamp(from + shift, 0, (int)S.taskbarPins.size() - 1);
    ws v = *it; S.taskbarPins.erase(it); S.taskbarPins.insert(S.taskbarPins.begin() + to, v);
    SaveSettingsSoon(); UiPost([] { BarsSync(); });
}

// ======================================================================= Bar
static ws TimeText() { SYSTEMTIME st; GetLocalTime(&st); wchar_t b[64]; GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, b, 64); return b; }
static ws DateText() { SYSTEMTIME st; GetLocalTime(&st); wchar_t b[64]; GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, b, 64, nullptr); return b; }

static View* BarBtn(View* parent) {
    auto* b = MkBox(parent, Lay::H); b->fh = 40; b->va = Al::Center; b->ha = Al::Start; b->hit = true; b->radius = 4;
    b->bgHover = SolidB(K::White, 0x22); b->bgPress = SolidB(K::White, 0x14); return b;
}

Bar::Bar(HMONITOR m, int index) : hmon(m), idx(index) { noActivate = true; transparent = ThemeIsGlass(CurrentTheme()); }
RECT Bar::Target() const {
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(hmon, &mi); int h = HeightPx(); RECT b = mi.rcMonitor;
    return g_dev ? RECT{b.left, b.top, b.right, b.top + h} : RECT{b.left, b.bottom - h, b.right, b.bottom};
}
bool Bar::Init() {
    MONITORINFO mi{sizeof mi}; GetMonitorInfoW(hmon, &mi); mon = mi.rcMonitor; created = Now();
    // born inside its own monitor so Windows gives it that monitor's DPI
    if (!Create(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_POPUP, mon.left + 10, g_dev ? mon.top + 10 : mon.bottom - 60, 100, 40)) return false;
    if (transparent) ApplyGlass(hwnd);
    View* r = root; r->bg = SolidB(K::Bar); r->lay = Lay::Overlay; r->hit = true;
    r->onRight = [this] {
        MenuShow({{L"Task Manager", 0xE9D9, [] { Launch(L"taskmgr.exe"); }}, {L"DeskBar settings", 0xE713, [] { OpenSettingsWindow(); }},
                  {L"Windows taskbar settings", 0xE713, [] { Launch(L"ms-settings:taskbar"); }}}, [] { POINT p; GetCursorPos(&p); return RECT{p.x, p.y, p.x, p.y}; }(), !g_dev, scale);
    };
    auto* row = MkBox(r, Lay::H);
    left = MkBox(row, Lay::H); left->star = 1; left->pad = Thick(12, 0, 0, 0);
    right = MkBox(row, Lay::H);
    auto* line = MkBox(r); line->fh = 1; line->va = Al::Start; line->bg = SolidB(K::White, 0x14);

    startBtn = left->Add(new StartBtn()); startBtn->onClick = [this] { TogglePanel(Panel::Start, this); };
    startBtn->onRight = [this] { SendKeys({VK_LWIN, 'X'}); };

    // right cluster, left to right
    mediaBox = MkBox(right, Lay::H); mediaBox->visible = false; mediaBox->margin = Thick(0, 0, 8, 0); mediaBox->va = Al::Center; mediaBox->ha = Al::Start; mediaBox->gap = 2;
    {
        auto* info = BarBtn(mediaBox); info->pad = Thick(5); info->gap = 8; info->fh = 36;
        mArt = info->Add(new Pic()); mArt->fw = mArt->fh = 22; mArt->va = Al::Center; mArt->radius = 4; mArt->bg = SolidB(K::White, 0x30);
        auto* col = MkBox(info, Lay::V); col->va = Al::Center;
        mTitle = MkLabel(col, L"", FText(11.5f)); mTitle->maxW = 120; mArtist = MkLabel(col, L"", FText(10), Col(K::White, K::Dim)); mArtist->maxW = 120;
        info->onClick = [] { MediaOpenSource(); };
        auto mk = [&](wchar_t ch, float sz, void (*fn)()) {
            auto* b = MkBox(mediaBox, Lay::Overlay); b->fw = 24; b->fh = 28; b->va = Al::Center; b->hit = true; b->radius = 4; b->bgHover = SolidB(K::White, 0x22);
            auto* g = MkGlyph(b, ch, sz); b->onClick = fn; return g;
        };
        mPrev = mk(0xE892, 10.5f, [] { MediaCmd(2); });
        mPlay = mk(0xE768, 12, [] { MediaCmd(0); });
        mNext = mk(0xE893, 10.5f, [] { MediaCmd(1); });
    }
    trayBox = MkBox(right, Lay::H); trayBox->va = Al::Center; trayBox->ha = Al::Start;
    overflowBtn = BarBtn(right); overflowBtn->fw = 28; overflowBtn->visible = false; overflowBtn->lay = Lay::Overlay; { auto* g = MkGlyph(overflowBtn, 0xE70E, 12); g->ha = Al::Center; g->va = Al::Center; }   // Overlay: the H layout ignores the child's ha
    kbBtn = BarBtn(right); kbBtn->pad = Thick(10, 0); kbBtn->tip = L"Touch keyboard";
    { auto* g = MkGlyph(kbBtn, 0xE765, 16); g->va = Al::Center; }
    kbBtn->visible = GetSystemMetrics(SM_MAXIMUMTOUCHES) > 0 && scale >= 1.5f;
    kbBtn->onClick = [] { ToggleTouchKeyboard(); };
    langBtn = BarBtn(right); langBtn->pad = Thick(12, 0, 12, 0); langBtn->visible = false; langBtn->onClick = [] { SendKeys({VK_LWIN, VK_SPACE}); };
    lang = MkLabel(langBtn, L"", FText(12)); lang->va = Al::Center;
    statusBtn = BarBtn(right); statusBtn->pad = Thick(10, 0, 10, 0); statusBtn->margin = Thick(0, 0, 2, 0); statusBtn->gap = 12;
    statusBtn->onClick = [this] { TogglePanel(Panel::Quick, this); };
    gNet = MkGlyph(statusBtn, 0xE774, 16); gNet->va = Al::Center; gVol = MkGlyph(statusBtn, 0xE995, 16); gVol->va = Al::Center;
    {
        auto* bt = MkBox(statusBtn, Lay::H); bt->va = Al::Center; bt->gap = 4;
        for (View* v : {(View*)gNet, (View*)gVol, bt}) { v->hit = true; v->onClick = statusBtn->onClick; }   // each icon is its own hover target (its own tooltip) and still opens Quick Settings
        auto* st = MkBox(bt, Lay::Overlay); st->fw = 20; st->fh = 20; st->va = Al::Center;
        gBattFill = MkGlyph(st, 0xEBA0, 16); gBattOutline = MkGlyph(st, 0xEBA0, 16);
        battPct = MkLabel(bt, L"", FText(12)); battPct->va = Al::Center; bt->tip = L"";
    }
    clockBtn = BarBtn(right); clockBtn->margin = Thick(0, 0, 8, 0); clockBtn->pad = Thick(10, 0, 10, 0); clockBtn->onClick = [this] { TogglePanel(Panel::Clock, this); };
    { auto* c = MkBox(clockBtn, Lay::V); c->va = Al::Center;
      clockT = MkLabel(c, TimeText(), FText(12)); clockT->ta = TA::Right; clockD = MkLabel(c, DateText(), FText(12)); clockD->ta = TA::Right; }
    right->Add(new Strip())->ha = Al::Start;

    EnsurePlaced(); ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    BarRegisterDrop(this);
    if (!g_dev) Dock::Register(hwnd, mon, HeightPx());
    UpdateStatus(); return true;
}
void Bar::EnsurePlaced() {
    UpdateScale(); RECT t = Target(), c; GetWindowRect(hwnd, &c);
    if (idx == 0 && !g_dev) TrayPlace(t);
    if (!EqualRect(&t, &c)) SetWindowPos(hwnd, HWND_TOPMOST, t.left, t.top, t.right - t.left, t.bottom - t.top, SWP_NOACTIVATE | (hiddenFs ? SWP_HIDEWINDOW : 0));
    UpdateScale();
}
void Bar::Sync(const std::vector<Entry>& es, HWND fg) {
    std::vector<TaskBtn*> next;
    for (auto& e : es) {
        TaskBtn* b = nullptr; for (auto* x : btns) if (x->e.key == e.key) { b = x; break; }
        if (!b) b = new TaskBtn();
        next.push_back(b);
    }
    std::vector<View*> kids{startBtn};
    for (size_t i = 0; i < es.size(); i++) kids.push_back(next[i]);
    for (auto* x : btns) if (std::find(next.begin(), next.end(), x) == next.end()) { x->parent = nullptr; delete x; }
    for (auto* b : next) if (!b->parent) { b->parent = left; b->SetWnd(this); }
    left->kids = kids; btns = next;
    for (size_t i = 0; i < es.size(); i++) { btns[i]->Update(es[i], fg); btns[i]->pinIndex = es[i].pinned ? (int)i : -1; }
    Relayout();
}
void Bar::UpdateStatus() {
    NetState n = NetSnap(); wchar_t gn;
    if (n.airplane) gn = 0xE709;
    else switch (n.kind) { case NetState::WiFi: gn = n.bars <= 1 ? 0xE872 : n.bars == 2 ? 0xE873 : n.bars == 3 ? 0xE874 : 0xE701; break; case NetState::Ether: gn = 0xE839; break; case NetState::Other: gn = 0xE774; break; default: gn = 0xF384; }
    gNet->Set(gn);
    VolState v = Volume(); gVol->Set(v.muted || v.vol < 0.001f ? 0xE74F : v.vol < 0.34f ? 0xE993 : v.vol < 0.67f ? 0xE994 : 0xE995);
    BattState b = Battery(); View* bt = battPct->parent;
    bt->Show(b.present);
    if (b.present) {
        int lv = (int)lroundf(b.pct / 10.f); wchar_t base = b.charging ? 0xEBAB : 0xEBA0;
        gBattFill->Set(base + lv); gBattOutline->Set(base); gBattOutline->visible = b.charging || b.saver;
        gBattFill->col = b.charging ? Col(K::Charge) : b.saver ? Col(K::Amber) : Col(K::White);
        battPct->visible = S.showBatteryPercent; battPct->Set(Fmt(L"%d%%", b.pct));
    }
    // one tooltip per icon: a bold title, then details (see TipShow)
    ws nt = n.airplane ? L"Airplane mode" : n.kind == NetState::None ? L"Not connected" : n.name.empty() ? (n.kind == NetState::WiFi ? L"Wi-Fi" : L"Ethernet") : n.name;
    if (!n.airplane && n.kind != NetState::None) { nt += L"\n" + n.status; if (n.kind == NetState::WiFi) nt += Fmt(L" \u00B7 %d of 4 bars", n.bars); else if (n.kind == NetState::Ether) nt += L" \u00B7 Ethernet"; if (!n.ip.empty()) nt += L" \u00B7 " + n.ip; }
    gNet->tip = nt;
    gVol->tip = (v.ok ? (v.device.empty() ? ws(L"Speakers") : v.device) : ws(L"Volume")) + L"\n" + (!v.ok ? ws(L"No output device") : v.muted ? ws(L"Muted") : Fmt(L"%d%%", (int)lroundf(v.vol * 100)));
    if (b.present) {
        ws d = b.charging ? L"Charging" : b.ac ? L"Plugged in" : b.secsLeft > 0 ? Fmt(L"%d hr %d min left", b.secsLeft / 3600, b.secsLeft % 3600 / 60) : L"On battery";
        bt->tip = Fmt(L"Battery\n%d%% \u00B7 %s", b.pct, d.c_str()) + (b.saver ? L" \u00B7 Energy saver" : L"");
    }
}
void Bar::UpdateMedia() {
    MediaState m = Media(); bool show = S.showMedia && m.active; mediaBox->Show(show); if (!show) { lastMedia.clear(); return; }
    bool compact = (mon.right - mon.left) / scale < 1600;
    ws sig = Fmt(L"%d|%d|%d|%p|", (int)compact, (int)m.playing, (int)m.artist.empty(), (void*)m.art.get()) + m.title + L"|" + m.artist;   // runs 4x a second: only re-lay-out when something changed
    if (sig == lastMedia) return; lastMedia = sig;
    mArt->fw = mArt->fh = compact ? 18.f : 22.f; mTitle->maxW = mArtist->maxW = compact ? 84.f : 120.f; mArtist->visible = !compact && !m.artist.empty();
    mTitle->Set(m.title); mArtist->Set(m.artist); mArt->img = m.art; mPlay->Set(m.playing ? 0xE769 : 0xE768); Relayout();
}
void Bar::Tick() {
    ws c = TimeText() + DateText();
    if (c != lastClock) { lastClock = c; clockT->Set(TimeText()); clockD->Set(DateText()); }
    ws k = KeyboardLabel(GetForegroundWindow()); langBtn->Show(!k.empty()); lang->Set(k);
    UpdateMedia();
    ws who; bool fs = IsFullscreenOn(mon, &who, hiddenFs);
    if (fs != hiddenFs) { hiddenFs = fs; Log(L"bar %d: %s %s", idx, fs ? L"hidden by fullscreen window" : L"shown again, fullscreen window gone", who.c_str()); if (fs) SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW); else SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW); }
    if (!hiddenFs && !IsWindowVisible(hwnd)) { Log(L"bar %d was hidden without a reason (rotation / display change?), shown again", idx); ShowWindow(hwnd, SW_SHOWNOACTIVATE); EnsurePlaced(); }
    if (!hiddenFs && !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)) { Log(L"bar %d lost its topmost status, restored", idx); SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }   // an app window drawn over the bar (even just its shadow) means the bar dropped out of the topmost layer
    if (!hiddenFs && (Now() - created < 6000 || (Now() / 250) % 4 == 0)) EnsurePlaced();
}
LRESULT Bar::Proc(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_LBUTTONDOWN: swipeX = GET_X_LPARAM(l) / scale; swipeY = GET_Y_LPARAM(l) / scale; swipeTouch = IsTouchMsg(); break;
    case WM_LBUTTONUP: {
        float x = GET_X_LPARAM(l) / scale, y = GET_Y_LPARAM(l) / scale;
        if (swipeTouch && swipeY - y > 24 && std::fabs(x - swipeX) < 80) {   // swipe up: Start, or Quick Settings near the right edge
            bool quick = (client.w - swipeX) < 300; swipeTouch = false;
            Wnd::Proc(m, w, l); TogglePanel(quick ? Panel::Quick : Panel::Start, this); return 0;
        }
        break;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_APP + 0x55:   // appbar notification from Windows; ABN_POSCHANGED (1): the work area was recomputed (rotation, sleep, screen change), an appbar must claim its strip again or Windows leaves it out
        if (w == 1 && !g_dev) Dock::Reserve(hwnd, mon, HeightPx());
        return 0;
    }
    return Wnd::Proc(m, w, l);
}

// ======================================================================= drag and drop onto the bar
// Over a button: hold 550 ms to bring the app forward, drop = open the files with that app (the shortcut's own drop target does it,
// so Store apps work too). Drop a program (.exe / .lnk) anywhere else = pin it.
struct BarDrop : IDropTarget {
    Bar* bar; ULONG refs = 1; TaskBtn* over = nullptr; UINT_PTR hold = 0; ws fileKind;
    BarDrop(Bar* b) : bar(b) {}
    HRESULT QueryInterface(REFIID r, void** o) override { if (r == IID_IUnknown || r == IID_IDropTarget) { *o = this; AddRef(); return S_OK; } *o = nullptr; return E_NOINTERFACE; }
    ULONG AddRef() override { return ++refs; } ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
    TaskBtn* At(POINTL pt) { POINT p{pt.x, pt.y}; ScreenToClient(bar->hwnd, &p); View* v = bar->HitTest(p.x / bar->scale, p.y / bar->scale); for (; v; v = v->parent) for (auto* b : bar->btns) if (b == v) return b; return nullptr; }
    static std::vector<ws> Files(IDataObject* d) {
        std::vector<ws> out; FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; STGMEDIUM sm{};
        if (SUCCEEDED(d->GetData(&fe, &sm))) { HDROP h = (HDROP)sm.hGlobal; UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0); for (UINT i = 0; i < n; i++) { wchar_t b[MAX_PATH]; DragQueryFileW(h, i, b, MAX_PATH); out.push_back(b); } ReleaseStgMedium(&sm); }
        return out;
    }
    void Over(TaskBtn* b) {
        if (b == over) return; if (hold) { UiKillTimer(hold); hold = 0; } if (over) { over->hovered = false; } over = b; if (b) { b->hovered = true; hold = UiTimer(550, [this] { UiKillTimer(hold); hold = 0; if (over && !over->e.wins.empty()) { ForceForeground(over->e.wins[0].h); WinTrackerPoke(); } }); }
        bar->Invalidate();
    }
    HRESULT DragEnter(IDataObject* d, DWORD, POINTL pt, DWORD* ef) override { fileKind = L""; for (auto& f : Files(d)) { ws x = ExtOf(f); if (x == L".exe" || x == L".lnk") fileKind = x; } return DragOver(0, pt, ef); }
    HRESULT DragOver(DWORD, POINTL pt, DWORD* ef) override { TaskBtn* b = At(pt); Over(b); *ef = b ? DROPEFFECT_COPY : (!fileKind.empty() ? DROPEFFECT_LINK : DROPEFFECT_NONE); return S_OK; }
    HRESULT DragLeave() override { Over(nullptr); return S_OK; }
    HRESULT Drop(IDataObject* d, DWORD key, POINTL pt, DWORD* ef) override {
        TaskBtn* b = At(pt); Over(nullptr);
        if (b) {
            ws tgt = b->e.pinned ? b->e.pin.launch : (!b->e.wins.empty() ? b->e.wins[0].exe : ws()); ComPtr<IShellItem> it; ComPtr<IDropTarget> dt;
            if (!tgt.empty() && SUCCEEDED(SHCreateItemFromParsingName(tgt.c_str(), nullptr, IID_PPV_ARGS(&it))) && SUCCEEDED(it->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&dt)))) {
                DWORD e2 = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK; if (SUCCEEDED(dt->DragEnter(d, key, pt, &e2))) { e2 = *ef; dt->Drop(d, key, pt, &e2); *ef = e2; return S_OK; }
            }
            *ef = DROPEFFECT_NONE; return S_OK;
        }
        for (auto& f : Files(d)) { ws x = ExtOf(f); if ((x == L".exe" || x == L".lnk") && std::none_of(S.taskbarPins.begin(), S.taskbarPins.end(), [&](const ws& p) { return EqI(p, f); })) S.taskbarPins.push_back(f); }
        SaveSettingsSoon(); UiPost([] { BarsSync(); }); *ef = DROPEFFECT_LINK; return S_OK;
    }
};

static void BarRegisterDrop(Bar* b) { auto* dt = new BarDrop(b); RegisterDragDrop(b->hwnd, dt); dt->Release(); }

// ======================================================================= manager
static bool WantMonitor(const Mon& m, const std::vector<Mon>& all) {
    if (!g_dev || g_devScreen == L"all" || all.size() < 2) return true;
    float best = 0; for (auto& a : all) best = std::max(best, a.scale);
    return g_devScreen == L"surface" ? m.scale >= best : m.scale < best || m.primary;
}
void BarsStart() {
    auto mons = Monitors(); g_monSig = MonSignature(); int i = 0;
    for (auto& m : mons) { if (!WantMonitor(m, mons)) continue; auto b = std::make_unique<Bar>(m.h, i++); if (b->Init()) g_bars.push_back(std::move(b)); }
    BarsSync(); TrayRefresh();
}
void BarsStop() { for (auto& b : g_bars) if (!g_dev) Dock::Remove(b->hwnd); g_bars.clear(); }
void BarsSync() {
    auto snap = WinSnapshot(); auto es = BuildEntries(*snap);
    for (auto& b : g_bars) b->Sync(es, snap->fg);
    std::vector<PinInfo> pins; for (auto& e : es) if (e.pinned) pins.push_back(e.pin);
    static size_t lastN = (size_t)-1; if (pins.size() != lastN) { lastN = pins.size(); SavePinCache(pins); }
}
void BarsTick() { for (auto& b : g_bars) b->Tick(); for (auto& b : g_bars) b->UpdateStatus(); }
void BarsRebuildIfMonitorsChanged() {
    ws sig = MonSignature(); if (sig == g_monSig) return;
    Log(L"monitors changed: %s", sig.c_str()); BarsStop(); BarsStart();
}
Bar* BarOnMonitor(HMONITOR m) { for (auto& b : g_bars) if (b->hmon == m) return b.get(); return g_bars.empty() ? nullptr : g_bars[0].get(); }
std::vector<Bar*> AllBars() { std::vector<Bar*> v; for (auto& b : g_bars) v.push_back(b.get()); return v; }
TaskBtn* FindBtn(const ws& key) { for (auto& b : g_bars) for (auto* t : b->btns) if (t->e.key == key) return t; return nullptr; }

bool TaskbarHasApp(const ws& id) { for (auto& e : S.taskbarPins) if (EqI(e, L"app:" + id)) return true; return false; }
void TaskbarPinApp(const ws& id) { if (!TaskbarHasApp(id)) { S.taskbarPins.push_back(L"app:" + id); SaveSettingsSoon(); BarsSync(); } }
void TaskbarUnpinApp(const ws& id) { std::erase_if(S.taskbarPins, [&](const ws& e) { return EqI(e, L"app:" + id); }); SaveSettingsSoon(); BarsSync(); }

// Non-main screens: Windows doesn't always keep the work area the appbar reserved, so set it ourselves (skipped while a full-screen app is up)
void BarsWorkAreaUpkeep() {
    if (g_dev) return;
    for (auto& b : g_bars) {
        if (b->hiddenFs) continue; MONITORINFO mi{sizeof mi}; GetMonitorInfoW(b->hmon, &mi); RECT want = mi.rcMonitor; want.bottom -= b->HeightPx();
        if (b->idx == 0) {   // main screen: re-claim the strip through the appbar protocol, not SPI_SETWORKAREA (that made Explorer pop its taskbar up every ~6 s). Without the strip, maximized windows run under the bar.
            static int miss = 0; static uint64_t lastForce = 0;
            if (EqualRect(&want, &mi.rcWork)) { miss = 0; continue; }
            Log(L"bar 0: work area lost (%d..%d of %d), attempt %d", (int)mi.rcWork.top, (int)mi.rcWork.bottom, (int)mi.rcMonitor.bottom, miss + 1);
            if (++miss <= 2) { Dock::Remove(b->hwnd); Dock::Register(b->hwnd, b->mon, b->HeightPx()); }   // register again from scratch: re-sending only the position does nothing if Windows forgot the bar
            else if (Now() - lastForce > 60000) { lastForce = Now(); SystemParametersInfoW(SPI_SETWORKAREA, 0, &want, 0); SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0); }   // the appbar route was ignored: force it, but never more than once a minute
            continue;
        }
        if (!EqualRect(&want, &mi.rcWork)) { Log(L"bar %d: work area lost (%d..%d of %d), forced", b->idx, (int)mi.rcWork.top, (int)mi.rcWork.bottom, (int)mi.rcMonitor.bottom); SystemParametersInfoW(SPI_SETWORKAREA, 0, &want, 0); SendNotifyMessageW(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0); }
    }
}
