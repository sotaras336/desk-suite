#include "pch.h"
#include "flyout.h"
#include "widgets.h"
#include "search.h"
#define SECURITY_WIN32
#include <security.h>
#include <secext.h>
#include <powrprof.h>
#include <shobjidl.h>

// The Start menu: greeting, instant local search, pinned apps, folders, recent files, all apps, footer.
namespace {
const float kBodyH = 610, kMinBodyH = 200, kTileH = 84, kPageH = kTileH * 3;

struct SearchBox : TextBox { std::function<void(bool)> onFocusChange; void OnFocus(bool f) override { TextBox::OnFocus(f); if (onFocusChange) onFocusChange(f); } };
struct PagedScroll : Scroll {       // the pinned grid moves a page (three rows) per wheel notch
    bool OnWheel(float d, float, float) override {
        if (MaxOff() <= 0) return false; float page = kPageH; int cur = (int)lroundf(-content->ty / page), n = (int)ceilf(contentH / page);
        ScrollTo(std::clamp(cur + (d < 0 ? 1 : -1), 0, n - 1) * page); return true;
    }
};

void OpenApp(const ws& id, bool admin = false) { Launch(L"shell:AppsFolder\\" + id, L"", admin); RecordLaunch(id); }
void ShowInFolder(const ws& p) { Launch(L"explorer.exe", L"/select,\"" + p + L"\""); }
void OpenWith(const ws& p) { Launch(L"rundll32.exe", L"shell32.dll,OpenAs_RunDLL " + p); }
bool CanHibernate() { SYSTEM_POWER_CAPABILITIES c{}; return GetPwrCapabilities(&c) && c.HiberFilePresent; }
ws UserDisplayName() { wchar_t b[256]; ULONG n = 256; if (GetUserNameExW(NameDisplay, b, &n) && n) return ws(b, n); wchar_t u[256]; DWORD un = 256; GetUserNameW(u, &un); return u; }
// Shown until this PC's own Start pins have been read (and kept if they can't be: Windows 10 has no JSON export).
const wchar_t* kDefaultPins[] = {L"Microsoft Edge", L"File Explorer", L"Settings", L"Microsoft Store", L"Photos", L"Calculator", L"Notepad", L"Paint", L"Snipping Tool", L"Clock"};
// What 1.0 and 1.1 put on every PC: Start pins still exactly these get swapped for the PC's own.
const wchar_t* kOldDefaultPins[] = {L"Outlook", L"Microsoft Store", L"Photos", L"OneNote", L"Windows App", L"Surface", L"Settings", L"Microsoft Clipchamp", L"Microsoft To Do", L"To Do", L"LinkedIn", L"Calculator", L"Clock", L"Notepad", L"Paint", L"Snipping Tool", L"File Explorer", L"Instagram", L"Phone Link", L"Bambu Studio", L"Goodnotes"};

struct StartPanel : Flyout {
    SearchBox* search; View *home, *allApps, *results; Scroll *homeScroll, *allScroll, *resList; View *resCol, *preview, *glow[3]; Label *greet, *dateL, *userL; Glyph* dayG; View* clearBtn; Glyph* sIcon; View* sBox;
    PagedScroll* pinScroll; View *pinGrid, *pinDots, *folders, *recentFilters, *recentGrid, *allList;
    std::vector<RecentItem> recents; ws recentFilter = L"All"; ws homeKey; std::vector<std::pair<Result, View*>> rows; int sel = -1; std::vector<Result> local, files; UINT_PTR drift = 0; uint64_t driftT0 = 0;
    bool searchFocus = false, importing = false; View *col, *body;

    StartPanel() : Flyout(Panel::Start, 640, false) {
        root->lay = Lay::Overlay; root->hit = true;
        // soft drifting colour glows behind everything (only while open)
        auto* gl = MkBox(root); struct G { float x, y, sz; uint32_t c; }; G gs[3] = {{-120, -140, 520, K::Accent}, {380, 180, 460, 0x8A6CFF}, {-80, 420, 420, 0x357BF5}};
        for (int i = 0; i < 3; i++) { auto* e = MkBox(gl); e->fw = e->fh = gs[i].sz; e->ha = e->va = Al::Start; e->margin = Thick(gs[i].x, gs[i].y, 0, 0); e->radius = gs[i].sz / 2; e->bg = RadialB(0.5f, 0.5f, gs[i].sz / 2, gs[i].sz / 2, {{0, Col(gs[i].c, 0x55)}, {1, Col(gs[i].c, 0)}}); glow[i] = e; }
        col = MkBox(root, Lay::V);
        BuildGreeting(col); BuildSearch(col);
        body = MkBox(col); body->fh = kBodyH; body->clip = true;
        homeScroll = body->Add(new Scroll()); home = MkBox(nullptr, Lay::V); homeScroll->content = homeScroll->Add(home); home->margin = Thick(0, 0, 0, 8);
        allScroll = body->Add(new Scroll()); allScroll->visible = false; allApps = MkBox(nullptr, Lay::V); allScroll->content = allScroll->Add(allApps);
        results = MkBox(body, Lay::H); results->visible = false;
        BuildHome(); BuildAll(); BuildResults(); BuildFooter(col);
    }

    // ------------------------------------------------ layout
    // A short screen (1080p at 125% leaves ~806 dip, at 150% ~664) cut off the footer with the power button: the scrolling middle gets what's left.
    // Measures the column, not root: the glows behind it reach 840 dip whatever the content.
    float ContentHeight(float maxH) override {
        body->fh = kBodyH; float full = col->Measure({widthDip, 1e5f}).h;
        body->fh = std::max(kMinBodyH, kBodyH - std::max(0.f, full - maxH));
        return col->Measure({widthDip, 1e5f}).h;
    }
    void BuildGreeting(View* col) {
        auto* g = MkBox(col, Lay::V); g->margin = Thick(34, 26, 32, 0);
        auto* row = MkBox(g, Lay::H); dayG = MkGlyph(row, 0xE706, 20); dayG->margin = Thick(0, 2, 12, 0); dayG->va = Al::Center;
        greet = MkLabel(row, L"", FDisp(23, 600)); dateL = MkLabel(g, L"", FText(12.5f), Col(K::White, K::Dim)); dateL->margin = Thick(1, 2, 0, 0);
    }
    void UpdateGreeting() {
        SYSTEMTIME t; GetLocalTime(&t); int h = t.wHour; bool day = h >= 6 && h < 19;
        ws part = h < 5 ? L"Good night" : h < 12 ? L"Good morning" : h < 18 ? L"Good afternoon" : L"Good evening";
        dayG->Set(day ? 0xE706 : 0xE708); dayG->col = day ? Col(0xFFB55A) : Col(0xB4A4FF);
        ws first = userL->text.substr(0, userL->text.find(L' ')); greet->Set(first.empty() ? part : part + L", " + first);
        wchar_t b[96]; GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &t, L"dddd, d MMMM", b, 96, nullptr); dateL->Set(b);
    }
    void BuildSearch(View* col) {
        sBox = MkBox(col, Lay::H); sBox->margin = Thick(32, 18, 32, 18); sBox->fh = 44; sBox->radius = 22; sBox->bg = SolidB(K::White, 0x1E); sBox->borderW = 1.5f; sBox->border = Col(K::White, 0x2A); sBox->pad = Thick(16, 0, 8, 0); sBox->gap = 10;
        sBox->bgHover = SolidB(K::White, 0x2C);
        sIcon = MkGlyph(sBox, 0xE721, 16, Col(K::White, K::Dim)); sIcon->va = Al::Center; sIcon->fw = 22;
        search = sBox->Add(new SearchBox()); search->star = 1; search->va = Al::Center; search->placeholder = L"Type to search apps, files, settings"; search->pad = Thick(0);
        search->onChange = [this](const ws&) { clearBtn->Show(!search->text.empty()); sIcon->col = search->text.empty() ? Col(K::White, K::Dim) : Col(K::Accent); RunSearch(); };
        search->onKeyHook = [this](UINT vk) { return SearchKey(vk); };
        search->onFocusChange = [this](bool f) { searchFocus = f; sBox->border = f ? Col(K::Accent) : Col(K::White, 0x2A); sBox->bg = f ? SolidB(K::White, 0x2C) : SolidB(K::White, 0x1E); };
        clearBtn = IconBtn(sBox, 0xE711, 30, 11, [this] { search->SetText(L""); SetFocus(search); }); clearBtn->bg = Brush{}; clearBtn->va = Al::Center; clearBtn->visible = false; clearBtn->tip = L"Clear";
        ((Glyph*)clearBtn->kids[0])->col = Col(K::White, K::Dim);
    }
    void BuildFooter(View* col) {
        auto* f = MkBox(col); f->fh = 66; f->bg = SolidB(0x001030, 0x30);
        auto* row = MkBox(f, Lay::H); row->pad = Thick(34, 0, 30, 0);
        { auto* line = MkBox(f); line->fh = 1; line->va = Al::Start; line->bg = SolidB(K::White, K::CardLine); }
        auto* user = MkBox(row, Lay::H); user->ha = Al::Start; user->star = 0; user->va = Al::Center; user->pad = Thick(8, 6, 12, 6); user->hit = true; user->radius = 8; user->bgHover = SolidB(K::White, 0x16);
        auto* ring = MkBox(user); ring->fw = ring->fh = 40; ring->radius = 20; ring->pad = Thick(2); ring->bg = LinearB(0, 0, 1, 1, {{0, Col(0x7FDCFF)}, {1, Col(0x8A6CFF)}}); ring->va = Al::Center;
        auto* av = MkBox(ring); av->radius = 17; av->bg = SolidB(0xE8ECF2); { auto* g = MkGlyph(av, 0xE77B, 16, Col(0x5A6370)); g->ha = g->va = Al::Center; }
        userL = MkLabel(user, UserDisplayName(), FText(13)); userL->margin = Thick(14, 0, 4, 0); userL->va = Al::Center;
        user->onClick = [this, user] { MenuShow({{L"Change account settings", 0xE77B, [] { Launch(L"ms-settings:yourinfo"); }}, {L"Lock", 0xE72E, [] { Launch(L"rundll32.exe", L"user32.dll,LockWorkStation"); }}, {L"Sign out", 0xF3B1, [] { Launch(L"shutdown.exe", L"/l"); }}}, ScreenRect(user), true, scale); };
        auto* sp = MkBox(row); sp->star = 1;
        auto* right = MkBox(row, Lay::H); right->gap = 8; right->va = Al::Center;
        IconBtn(right, 0xEC50, 40, 15, [this] { Launch(L"explorer.exe"); Close(); })->tip = L"File Explorer";
        IconBtn(right, 0xE713, 40, 15, [this] { Launch(L"ms-settings:"); Close(); })->tip = L"Settings";
        auto* pw = IconBtn(right, 0xE7E8, 40, 15, {}); pw->tip = L"Power";
        pw->onClick = [this, pw] {
            std::vector<MenuItem> m{{L"Lock", 0xE72E, [] { Launch(L"rundll32.exe", L"user32.dll,LockWorkStation"); }}, {L"Sleep", 0xE708, [] { Launch(L"rundll32.exe", L"powrprof.dll,SetSuspendState 0,1,0"); }}};
            if (CanHibernate()) m.push_back({L"Hibernate", 0xE823, [] { Launch(L"shutdown.exe", L"/h"); }});
            m.push_back({L"Shut down", 0xE7E8, [] { Launch(L"shutdown.exe", L"/s /t 0"); }}); m.push_back({L"Restart", 0xE777, [] { Launch(L"shutdown.exe", L"/r /t 0"); }});
            MenuShow(std::move(m), ScreenRect(pw), true, scale);
        };
    }
    View* SectionCard(View* parent) { auto* c = MkBox(parent, Lay::V); c->margin = Thick(20, 0, 20, 12); c->pad = Thick(0, 4, 0, 10); c->radius = 12; c->bg = SolidB(K::White, 0x14); c->borderW = 1; c->border = Col(K::White, K::CardLine); return c; }
    View* SectionHeader(View* parent, const ws& title, const ws& btn = L"", wchar_t glyph = 0, std::function<void()> fn = {}, bool iconFirst = false) {
        auto* h = MkBox(parent, Lay::H); h->margin = Thick(20, 8, 14, 6); h->fh = 30; auto* t = MkLabel(h, title, FText(14, 600)); t->star = 1; t->va = Al::Center;
        if (!btn.empty()) {
            auto* b = MkBox(h, Lay::H); b->pad = Thick(10, 4); b->gap = 8; b->radius = 8; b->hit = true; b->bgHover = SolidB(K::White, K::CardHover); b->va = Al::Center; b->onClick = std::move(fn);
            if (iconFirst) { auto* g = MkGlyph(b, glyph, 10); g->va = Al::Center; auto* l = MkLabel(b, btn, FText(12)); l->va = Al::Center; } else { auto* l = MkLabel(b, btn, FText(12)); l->va = Al::Center; auto* g = MkGlyph(b, glyph, 10); g->va = Al::Center; }
        }
        return h;
    }
    View* Chip(View* parent, bool accent = false) { auto* c = MkBox(parent, Lay::H); c->pad = Thick(12, 6); c->radius = 8; c->gap = 8; c->hit = true; c->bg = accent ? SolidB(K::Accent) : SolidB(K::White, K::Card); c->bgHover = accent ? SolidB(0x6CD2FF) : SolidB(K::White, K::CardHover); return c; }
    void BuildHome() {
        auto* pc = SectionCard(home); SectionHeader(pc, L"Pinned", L"All apps", 0xE76C, [this] { ShowAll(); });
        pinScroll = pc->Add(new PagedScroll()); pinScroll->fh = kPageH; pinScroll->margin = Thick(11, 0, 11, 0);
        pinGrid = MkBox(nullptr, Lay::Uniform); pinGrid->cols = 6; pinGrid->cellH = kTileH; pinScroll->content = pinScroll->Add(pinGrid);
        pinDots = MkBox(pc, Lay::H); pinDots->ha = Al::Center; pinDots->gap = 2; pinDots->margin = Thick(0, 4, 0, 2);
        auto* fc = SectionCard(home); SectionHeader(fc, L"Folders"); folders = MkBox(fc, Lay::Wrap); folders->gap = 8; folders->margin = Thick(18, 0, 14, 0);
        auto* rc = SectionCard(home); SectionHeader(rc, L"Recent"); recentFilters = MkBox(rc, Lay::Wrap); recentFilters->gap = 6; recentFilters->margin = Thick(18, 0, 14, 8);
        recentGrid = MkBox(rc, Lay::Uniform); recentGrid->cols = 2; recentGrid->margin = Thick(12, 0, 12, 0); recentGrid->cellH = 52;
    }
    void BuildAll() {
        auto* h = SectionHeader(allApps, L"All apps", L"Back", 0xE76B, [this] { ShowHome(); }, true); h->margin = Thick(36, 4, 30, 6);
        allList = MkBox(allApps, Lay::V); allList->margin = Thick(24, 0, 24, 16);
    }
    void BuildResults() {
        resList = results->Add(new Scroll()); resList->fw = 318; resCol = MkBox(nullptr, Lay::V); resCol->margin = Thick(16, 0, 8, 12); resList->content = resList->Add(resCol);
        preview = MkBox(results, Lay::V); preview->star = 1; preview->margin = Thick(4, 0, 24, 16); preview->radius = 8; preview->bg = SolidB(K::White, K::Card); preview->borderW = 1; preview->border = Col(K::White, K::CardLine); preview->pad = Thick(20, 28, 20, 16);
    }
    // ------------------------------------------------ show / hide
    void ShowHome() { homeScroll->visible = true; allScroll->visible = false; results->visible = false; Relayout(); }
    void ShowAll() { RenderAll(); homeScroll->visible = false; results->visible = false; allScroll->visible = true; allScroll->ScrollTo(0, false); Relayout(); }
    void OnOpen() override {
        search->SetText(L"", false); clearBtn->visible = false; UpdateGreeting(); ShowHome();
        SeedPins();
        ws key; for (auto& p : S.startPins) key += p + L"|"; key += L"#"; for (auto& f : S.quickFolders) key += f + L"|"; key += Fmt(L"#%d", (int)CatalogApps().size());
        if (key != homeKey) { homeKey = key; RenderPinned(); RenderFolders(); } else Relayout();
        homeScroll->ScrollTo(0, false); SetFocus(search);
        std::thread([this] { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); auto items = RecentLoad(60); UiPost([this, items] { recents = items; RenderRecent(); }); }).detach();
        driftT0 = Now(); drift = UiTimer(66, [this] {
            double t = (Now() - driftT0) / 1000.0; struct D { float dx, dy, secs; }; static const D ds[3] = {{70, 55, 9}, {-60, 80, 11}, {80, -50, 13}};
            for (int i = 0; i < 3; i++) { float k = (float)((1 - cos(3.14159265 * t / ds[i].secs)) / 2); glow[i]->tx = ds[i].dx * k; glow[i]->ty = ds[i].dy * k; } Invalidate();
        });
    }
    void OnClose() override { if (drift) { UiKillTimer(drift); drift = 0; } }
    void OnShown() override {
        // wait until Windows really gave us the keyboard, then hand the search box whatever was typed meanwhile
        auto tries = std::make_shared<int>(0); auto id = std::make_shared<UINT_PTR>(0);
        *id = UiTimer(20, [this, tries, id] {
            (*tries)++;
            if (GetForegroundWindow() != hwnd && *tries <= 75) { if (*tries % 10 == 0) ForceForeground(hwnd); return; }
            UiKillTimer(*id); SetFocus(search);
            bool enter = false, esc = false; ws typed = TypeAheadTake(enter, esc); TypeAheadEnd();
            if (esc) { Close(); return; }
            if (!typed.empty()) search->SetText(typed);
            if (enter && !typed.empty() && !rows.empty()) OpenResult(rows[std::max(0, sel)].first);
        });
    }
    bool OnEsc() override { if (!search->text.empty()) { search->SetText(L""); return true; } if (allScroll->visible) { ShowHome(); return true; } return false; }
    bool OnKey(UINT vk, bool ctrl, bool shift) override {
        if (focus != search && (vk == VK_BACK || vk == VK_DELETE)) { SetFocus(search); return false; }
        return false;
    }
    bool SearchKey(UINT vk) {
        if (!results->visible) return false;
        if (vk == VK_DOWN) { Select(sel + 1); return true; } if (vk == VK_UP) { Select(sel - 1); return true; }
        if (vk == VK_RETURN) {
            if (sel >= 0 && sel < (int)rows.size()) {
                auto& r = rows[sel].first; bool admin = (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000);
                if (admin && r.kind == RKind::App) { OpenApp(r.appId, true); Close(); } else if (admin && r.kind == RKind::Run) RunCommand(r, true); else OpenResult(r);
            }
            return true;
        }
        return false;
    }
    // ------------------------------------------------ pinned
    template <size_t N> std::vector<ws> Matched(const wchar_t* const (&names)[N]) {   // catalog ids of these apps, in order, skipping what isn't installed
        auto& apps = CatalogApps(); std::vector<ws> ids;
        for (auto n : names) {
            const CatApp* a = CatalogFindByName(n); if (!a) for (auto& x : apps) if (StartsI(x.name, ws(n) + L" ")) { a = &x; break; }
            if (a && std::find(ids.begin(), ids.end(), a->id) == ids.end()) ids.push_back(a->id);
        }
        return ids;
    }
    // This PC's own Start pins, read once with Windows' Export-StartLayout (the pins file itself is encrypted). They replace only pins that are still
    // exactly as we seeded them; pins someone has changed stay.
    void ImportWindowsPins() {
        importing = true;
        std::thread([this] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            ws file = DataPath(L"startlayout.json"), q = file; for (size_t i = 0; (i = q.find(L'\'', i)) != ws::npos; i += 2) q.insert(i, 1, L'\'');   // quote for PowerShell
            ws cmd = L"powershell.exe -NoProfile -NonInteractive -Command \"Export-StartLayout -Path '" + q + L"'\""; DeleteFileW(file.c_str());
            STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { WaitForSingleObject(pi.hProcess, 20000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
            std::string raw; ReadAll(file, raw); DeleteFileW(file.c_str());
            std::vector<std::pair<ws, ws>> found;   // (app id, name to fall back on)
            Json j = Json::Parse(raw);
            if (auto* l = j.Find(L"pinnedList"); l && l->t == Json::Arr) for (auto& e : l->a) {
                if (auto* p = e.Find(L"packagedAppId")) found.push_back({p->s, L""});
                else if (auto* d = e.Find(L"desktopAppLink")) { ws lnk = ExpandEnv(d->s); found.push_back({AumidOfShortcut(lnk), StemOf(lnk)}); }
            }
            CoUninitialize();
            UiPost([this, found] {
                std::vector<ws> got;
                for (auto& [id, name] : found) {
                    const CatApp* a = id.empty() ? nullptr : CatalogFindById(id); if (!a && !name.empty()) a = CatalogFindByName(name);
                    if (a && std::find(got.begin(), got.end(), a->id) == got.end()) got.push_back(a->id);
                }
                if (!got.empty() && (S.startPins.empty() || S.startPins == Matched(kDefaultPins) || S.startPins == Matched(kOldDefaultPins))) S.startPins = got;
                S.startPinsImported = true; importing = false; SaveSettingsSoon(); RenderPinned();
            });
        }).detach();
    }
    void SeedPins() {
        if (CatalogApps().empty()) return;
        if (!S.startPinsImported && !importing) ImportWindowsPins();
        if (S.startPinsInit) return;
        S.startPins = Matched(kDefaultPins);
        if (S.quickFolders.empty()) for (auto id : {FOLDERID_Downloads, FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Pictures}) { ws p = KnownFolder(id); if (DirExists(p)) S.quickFolders.push_back(p); wchar_t od[512]; if (id == FOLDERID_Desktop && GetEnvironmentVariableW(L"OneDrive", od, 512) && DirExists(od)) S.quickFolders.push_back(od); }
        S.startPinsInit = S.foldersInit = true; SaveSettingsSoon();
    }
    std::vector<MenuItem> AppMenu(const ws& id, const ws& name, bool inStart) {
        bool pinned = std::find(S.startPins.begin(), S.startPins.end(), id) != S.startPins.end(); (void)name;
        std::vector<MenuItem> m{{L"Open", 0xE8A7, [this, id] { OpenApp(id); Close(); }}, {L"Run as administrator", 0xE7EF, [this, id] { OpenApp(id, true); Close(); }}, {L"", 0, {}, true}};
        m.push_back(pinned ? MenuItem{L"Unpin from Start", 0xE77A, [this, id] { std::erase(S.startPins, id); SaveSettingsSoon(); RenderPinned(); }} : MenuItem{L"Pin to Start", 0xE718, [this, id] { S.startPins.push_back(id); SaveSettingsSoon(); RenderPinned(); }});
        if (inStart && pinned && S.startPins[0] != id) m.push_back({L"Move to front", 0xE74A, [this, id] { std::erase(S.startPins, id); S.startPins.insert(S.startPins.begin(), id); SaveSettingsSoon(); RenderPinned(); }});
        bool onBar = TaskbarHasApp(id);
        m.push_back(onBar ? MenuItem{L"Unpin from taskbar", 0xE77A, [id] { TaskbarUnpinApp(id); }} : MenuItem{L"Pin to taskbar", 0xE718, [id] { TaskbarPinApp(id); }});
        m.push_back({L"", 0, {}, true}); m.push_back({L"Uninstall…", 0xE74D, [this] { Launch(L"ms-settings:appsfeatures"); Close(); }});
        return m;
    }
    View* IconPic(View* parent, const ws& key, float sz) { auto* p = parent->Add(new Pic()); p->fw = p->fh = sz; p->provider = [key](int px) { return IconGet(key, px); }; return p; }
    void RenderPinned() {
        pinGrid->Clear(); int n = 0;
        for (auto& id : std::vector<ws>(S.startPins)) {
            auto* a = CatalogFindById(id); if (!a && !CatalogApps().empty()) continue; ws name = a ? a->name : id; n++;
            auto* t = MkBox(pinGrid, Lay::V); t->fw = 94; t->fh = kTileH; t->radius = 8; t->hit = true; t->ha = Al::Center; t->bgHover = SolidB(K::White, K::CardHover); t->tip = name;
            auto* ic = IconPic(t, L"aum:" + id, 32); ic->margin = Thick(0, 10, 0, 8); ic->ha = Al::Center;
            auto* lb = MkLabel(t, name, FText(12)); lb->ta = TA::Center; lb->maxW = 88; lb->ha = Al::Center;
            t->onHover = [this, ic](bool in) { if (!touchLast) Anim(ic, &ic->scale, in ? 1.12f : 1.f, 100); };
            t->onClick = [this, id] { OpenApp(id); Close(); };
            auto menu = [this, t, id, name] { MenuShow(AppMenu(id, name, true), ScreenRect(t), false, scale); }; t->onRight = menu; t->onLong = menu;
        }
        if (!n) { auto* l = MkLabel(pinGrid, L"Right-click any app in All apps or search to pin it here.", FText(12), Col(K::White, K::Dim)); l->margin = Thick(24, 8, 0, 8); }
        int pages = std::max(1, (n + 17) / 18); pinScroll->fh = n <= 6 ? kTileH : n <= 12 ? kTileH * 2 : kPageH; pinScroll->ScrollTo(0, false);
        pinDots->Clear(); pinDots->visible = pages > 1;
        struct Dot : View { PagedScroll* s; int page; Dot(PagedScroll* sc, int p) : s(sc), page(p) { fw = fh = 7; } void PaintContent(Gfx& g) override { bool cur = (int)lroundf(-s->content->ty / kPageH) == page; g.Circle(rc.cx(), rc.cy(), cur ? 3.5f : 3.f, Col(K::White, cur ? 255 : K::Faint)); } };
        for (int i = 0; i < pages; i++) { auto* h = MkBox(pinDots); h->pad = Thick(5); h->hit = true; h->Add(new Dot(pinScroll, i)); h->onClick = [this, i] { pinScroll->ScrollTo(i * kPageH); }; }
        Relayout();
    }
    // ------------------------------------------------ folders
    void RenderFolders() {
        folders->Clear();
        for (auto& path : std::vector<ws>(S.quickFolders)) {
            if (!DirExists(path)) continue; ws name = FileNameOf(path.back() == L'\\' ? path.substr(0, path.size() - 1) : path); if (name.empty()) name = path;
            auto* c = Chip(folders); IconPic(c, L"file:" + path, 20); auto* l = MkLabel(c, name, FText(12.5f)); l->va = Al::Center; c->tip = path;
            c->onClick = [this, path] { Launch(path); Close(); };
            auto menu = [this, c, path] { MenuShow({{L"Open", 0xE8DA, [this, path] { Launch(path); Close(); }}, {L"Remove from Start", 0xE711, [this, path] { std::erase(S.quickFolders, path); SaveSettingsSoon(); RenderFolders(); }}}, ScreenRect(c), false, scale); };
            c->onRight = menu; c->onLong = menu;
        }
        auto* add = Chip(folders); auto* g = MkGlyph(add, 0xE710, 12); g->va = Al::Center; auto* l = MkLabel(add, L"Add folder", FText(12.5f)); l->va = Al::Center; add->onClick = [this] { AddFolder(); };
        Relayout();
    }
    void AddFolder() {
        keepOpen = true; HWND owner = hwnd;
        std::thread([this, owner] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); ws picked;
            ComPtr<IFileOpenDialog> d;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&d)))) {
                DWORD o = 0; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(d->Show(owner))) { ComPtr<IShellItem> it; PWSTR p = nullptr; if (SUCCEEDED(d->GetResult(&it)) && SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) picked = p; CoTaskMemFree(p); }
            }
            UiPost([this, picked] {
                keepOpen = false;
                if (!picked.empty() && std::none_of(S.quickFolders.begin(), S.quickFolders.end(), [&](const ws& f) { return EqI(f, picked); })) { S.quickFolders.push_back(picked); SaveSettingsSoon(); }
                RenderFolders(); ForceForeground(hwnd);
            });
        }).detach();
    }
    // ------------------------------------------------ recent
    void RenderRecent() {
        std::vector<RecentItem> all; std::vector<ws> pinned;
        for (auto& p : S.pinnedRecents) if (FileExists(p)) { pinned.push_back(p); RecentItem it{p, FileNameOf(p), FriendlyAppFor(ExtOf(p)), ExtOf(p), {}}; WIN32_FILE_ATTRIBUTE_DATA fa{}; if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) it.when = fa.ftLastWriteTime; all.push_back(it); }
        auto in = [](const std::vector<ws>& v, const ws& p) { return std::any_of(v.begin(), v.end(), [&](const ws& x) { return EqI(x, p); }); };
        for (auto& r : recents) if (!in(pinned, r.path) && !in(S.hiddenRecents, r.path)) all.push_back(r);
        std::map<ws, int> cnt; for (auto& r : all) cnt[r.appName]++; std::vector<std::pair<ws, int>> g(cnt.begin(), cnt.end());
        std::stable_sort(g.begin(), g.end(), [](auto& a, auto& b) { return a.second > b.second; }); std::vector<ws> groups; for (size_t i = 0; i < g.size() && i < 5; i++) groups.push_back(g[i].first);
        if (recentFilter != L"All" && std::find(groups.begin(), groups.end(), recentFilter) == groups.end()) recentFilter = L"All";
        recentFilters->Clear(); groups.insert(groups.begin(), L"All");
        for (auto& n : groups) { bool on = n == recentFilter; auto* c = Chip(recentFilters, on); c->pad = Thick(12, 4); auto* l = MkLabel(c, n, FText(12), on ? Col(K::Black) : Col(K::White)); l->va = Al::Center; c->onClick = [this, n] { recentFilter = n; RenderRecent(); }; }
        recentGrid->Clear(); int shown = 0;
        for (auto& r : all) {
            if (recentFilter != L"All" && r.appName != recentFilter) continue; if (shown++ >= 12) break;
            RecentRow(r, in(pinned, r.path));
        }
        if (!shown) { auto* l = MkLabel(recentGrid, L"Files you open will show up here.", FText(12.5f), Col(K::White, K::Dim)); l->margin = Thick(8, 0, 0, 0); }
        Relayout();
    }
    void RecentRow(const RecentItem& it, bool isPinned) {
        auto* row = MkBox(recentGrid, Lay::H); row->fh = 50; row->pad = Thick(4, 0); row->radius = 6; row->margin = Thick(0, 0, 6, 2); row->hit = true; row->bgHover = SolidB(K::White, K::CardHover); row->tip = it.path; row->gap = 0;
        auto* ic = IconPic(row, L"file:" + it.path, 28); ic->va = Al::Center; ic->margin = Thick(0, 0, 16, 0);
        auto* tx = MkBox(row, Lay::V); tx->star = 1; tx->va = Al::Center; MkLabel(tx, it.name, FText(12.5f));
        MkLabel(tx, (isPinned ? L"Pinned · " : L"") + it.appName + L" · " + Ago(it.when), FText(11), Col(K::White, K::Dim));
        auto* act = MkBox(row, Lay::H); act->visible = false; act->va = Al::Center; ws path = it.path;
        auto* loc = IconBtn(act, 0xE838, 30, 13, [this, path] { ShowInFolder(path); Close(); }); loc->bg = Brush{}; loc->tip = L"Open file location";
        auto* pin = IconBtn(act, isPinned ? 0xE77A : 0xE718, 30, 13, [this, path] { TogglePinRecent(path); }); pin->bg = Brush{}; pin->tip = isPinned ? L"Unpin" : L"Pin to top";
        row->onHover = [this, act](bool in) { act->Show(in && !touchLast); };
        row->onClick = [this, path] { Launch(path); Close(); };
        auto menu = [this, row, path, isPinned] {
            MenuShow({{L"Open", 0xE8E5, [this, path] { Launch(path); Close(); }}, {L"Open file location", 0xE838, [this, path] { ShowInFolder(path); Close(); }}, {isPinned ? L"Unpin" : L"Pin to top", isPinned ? (wchar_t)0xE77A : (wchar_t)0xE718, [this, path] { TogglePinRecent(path); }},
                {L"Open with…", 0xE7AC, [this, path] { Close(); OpenWith(path); }}, {L"Copy path", 0xE8C8, [path] { ClipboardSetText(path); }}, {L"Properties", 0xE946, [this, path] { Close(); OpenProps(path); }}, {L"", 0, {}, true},
                {L"Remove from list", 0xE711, [this, path] { std::erase(S.pinnedRecents, path); if (std::none_of(S.hiddenRecents.begin(), S.hiddenRecents.end(), [&](const ws& x) { return EqI(x, path); })) S.hiddenRecents.push_back(path); SaveSettingsSoon(); RenderRecent(); }}}, ScreenRect(row), false, scale);
        };
        row->onRight = menu; row->onLong = menu;
    }
    void TogglePinRecent(const ws& path) { size_t n = std::erase_if(S.pinnedRecents, [&](const ws& x) { return EqI(x, path); }); if (!n) S.pinnedRecents.insert(S.pinnedRecents.begin(), path); SaveSettingsSoon(); RenderRecent(); }
    // ------------------------------------------------ all apps
    void RenderAll() {
        allList->Clear(); wchar_t cur = 0;
        for (auto& a : CatalogApps()) {
            if (a.Initial() != cur) { cur = a.Initial(); auto* l = MkLabel(allList, ws(1, cur), FText(13, 600), Col(K::Accent)); l->margin = Thick(24, 10, 0, 4); }
            auto* b = MkBox(allList, Lay::H); b->pad = Thick(24, 6, 12, 6); b->radius = 6; b->hit = true; b->gap = 14; b->bgHover = SolidB(K::White, K::CardHover);
            IconPic(b, L"aum:" + a.id, 24); auto* l = MkLabel(b, a.name, FText(13)); l->va = Al::Center; ws id = a.id, nm = a.name;
            b->onClick = [this, id] { OpenApp(id); Close(); };
            auto menu = [this, b, id, nm] { MenuShow(AppMenu(id, nm, false), ScreenRect(b), false, scale); }; b->onRight = menu; b->onLong = menu;
        }
        Relayout();
    }
    // ------------------------------------------------ search
    void RunSearch() {
        Log(L"search text [%s] len %d first %04x", search->text.c_str(), (int)search->text.size(), search->text.empty() ? 0 : search->text[0]); ws q = Trim(search->text); if (q.empty()) { ShowHome(); return; }
        homeScroll->visible = false; allScroll->visible = false; results->visible = true;
        local = SearchLocal(q); files.clear(); RenderResults(false);
        SearchFiles(q, [this, q](std::vector<Result> f) { if (!shown || Trim(search->text) != q) return; for (auto& x : f) x.score += HabitBoost(q, x.kind, x.target); files = std::move(f); RenderResults(true); });
    }
    void RenderResults(bool keep) {
        ws prev = keep && sel >= 0 && sel < (int)rows.size() ? rows[sel].first.target : ws();
        resCol->Clear(); rows.clear(); std::vector<Result> all = local; all.insert(all.end(), files.begin(), files.end());
        std::stable_sort(all.begin(), all.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
        std::set<std::pair<int, ws>> seen; std::vector<Result> u; for (auto& r : all) if (seen.insert({(int)r.kind, Lower(r.target)}).second) u.push_back(r);
        if (u.empty()) { auto* l = MkLabel(resCol, L"No results for “" + Trim(search->text) + L"”", FText(13), Col(K::White, K::Dim)); l->margin = Thick(12, 12, 0, 0); preview->Clear(); sel = -1; Relayout(); return; }
        Result best = u[0]; Section(L"Best match"); AddRow(best, true);
        struct Sec { RKind k; const wchar_t* t; int max; }; static const Sec secs[] = {{RKind::Run, L"Run", 5}, {RKind::App, L"Apps", 5}, {RKind::Program, L"Programs", 5}, {RKind::Setting, L"Settings", 5}, {RKind::Folder, L"Folders", 6}, {RKind::File, L"Files", 14}};
        for (auto& s : secs) {
            int n = 0; bool head = false;
            for (auto& r : u) { if (r.kind != s.k || (&r == &u[0])) continue; if (n >= s.max) break; if (!head) { Section(s.t); head = true; } AddRow(r, false); n++; }
        }
        int idx = 0; if (!prev.empty()) for (size_t i = 0; i < rows.size(); i++) if (rows[i].first.target == prev) idx = (int)i;
        sel = -1; Select(idx); Relayout();
    }
    void Section(const ws& t) { auto* l = MkLabel(resCol, t, FText(12.5f, 600)); l->margin = Thick(12, 12, 0, 6); }
    void IconFor(View* parent, const Result& r, float size) {
        switch (r.kind) {
        case RKind::Setting: { auto* g = MkGlyph(parent, 0xE713, size * 0.7f); g->ha = g->va = Al::Center; break; }
        case RKind::Calc: { auto* g = MkGlyph(parent, 0xE8EF, size * 0.7f); g->ha = g->va = Al::Center; break; }
        case RKind::Run: if (r.appId.empty()) { auto* g = MkGlyph(parent, 0xE756, size * 0.7f); g->ha = g->va = Al::Center; } else { auto* p = IconPic(parent, L"exe:" + r.appId, size); p->ha = p->va = Al::Center; } break;
        case RKind::App: { auto* p = IconPic(parent, L"aum:" + r.appId, size); p->ha = p->va = Al::Center; break; }
        default: { auto* p = IconPic(parent, L"file:" + r.target, size); p->ha = p->va = Al::Center; }
        }
    }
    void AddRow(const Result& r, bool big) {
        auto* row = MkBox(resCol, Lay::H); row->fh = big ? 56.f : 40.f; row->radius = 5; row->pad = Thick(8, 0); row->margin = Thick(0, 1); row->hit = true; row->bgHover = SolidB(K::White, K::CardHover); row->gap = 0;
        auto* ih = MkBox(row); ih->fw = big ? 48.f : 36.f; IconFor(ih, r, big ? 32.f : 22.f);
        auto* tx = MkBox(row, Lay::V); tx->star = 1; tx->va = Al::Center; MkLabel(tx, r.title, FText(big ? 14.f : 13.f));
        if (big || r.kind == RKind::File || r.kind == RKind::Folder) MkLabel(tx, r.subtitle, FText(11), Col(K::White, K::Dim));
        int idx = (int)rows.size(); Result rc = r;
        row->onClick = [this, idx, rc] { Select(idx); OpenResult(rc); };
        if (r.kind == RKind::App) { auto menu = [this, row, rc] { MenuShow(AppMenu(rc.appId, rc.title, false), ScreenRect(row), false, scale); }; row->onRight = menu; row->onLong = menu; }
        rows.push_back({r, row});
    }
    void Select(int i) {
        if (rows.empty()) return; i = std::clamp(i, 0, (int)rows.size() - 1);
        if (sel >= 0 && sel < (int)rows.size()) rows[sel].second->bg = Brush{};
        sel = i; rows[i].second->bg = SolidB(K::Accent, 0x40);
        View* row = rows[i].second; float top = row->rc.t - resCol->rc.t, bot = top + row->rc.h(), vh = resList->rc.h(), off = -resList->content->ty;   // keep the selection in view
        if (top < off) resList->ScrollTo(top - 8, false); else if (bot > off + vh) resList->ScrollTo(bot - vh + 8, false);
        BuildPreview(rows[i].first); Invalidate();
    }
    void Act(View* parent, wchar_t glyph, const ws& label, std::function<void()> fn) {
        auto* b = MkBox(parent, Lay::H); b->pad = Thick(10, 8); b->radius = 6; b->hit = true; b->gap = 12; b->bgHover = SolidB(K::White, K::CardHover); b->onClick = std::move(fn);
        auto* g = MkGlyph(b, glyph, 14); g->va = Al::Center; auto* l = MkLabel(b, label, FText(13)); l->va = Al::Center;
    }
    void BuildPreview(const Result& r) {
        preview->Clear(); auto* ih = MkBox(preview); ih->fh = 64; ih->margin = Thick(0, 0, 0, 12); IconFor(ih, r, 56);
        auto* t = MkLabel(preview, r.title, FText(16, 600)); t->wrap = true; t->ta = TA::Center;
        ws kind = r.kind == RKind::App ? L"App" : r.kind == RKind::Setting ? L"System settings" : r.kind == RKind::Folder ? L"Folder" : r.kind == RKind::Calc ? L"Calculator" : r.kind == RKind::Run ? L"Command" : r.kind == RKind::Program ? L"Program" : FriendlyAppFor(ExtOf(r.target)) + L" file";
        auto* k = MkLabel(preview, kind, FText(12), Col(K::White, K::Dim)); k->ha = Al::Center;
        if (r.kind == RKind::File || r.kind == RKind::Folder) { auto* p = MkLabel(preview, Shorten(r.target), FText(11), Col(K::White, K::Faint)); p->wrap = true; p->ta = TA::Center; p->margin = Thick(0, 6, 0, 0); }
        Divider(preview, Thick(-20, 18, -20, 10));
        Result rc = r;
        switch (r.kind) {
        case RKind::App: {
            ws id = r.appId; Act(preview, 0xE8E5, L"Open", [this, id] { OpenApp(id); Close(); }); Act(preview, 0xE7EF, L"Run as administrator", [this, id] { OpenApp(id, true); Close(); });
            bool inStart = std::find(S.startPins.begin(), S.startPins.end(), id) != S.startPins.end();
            Act(preview, inStart ? 0xE77A : 0xE718, inStart ? L"Unpin from Start" : L"Pin to Start", [this, id, inStart, rc] { if (inStart) std::erase(S.startPins, id); else S.startPins.push_back(id); SaveSettingsSoon(); RenderPinned(); BuildPreview(rc); });
            bool onBar = TaskbarHasApp(id); Act(preview, onBar ? 0xE77A : 0xE718, onBar ? L"Unpin from taskbar" : L"Pin to taskbar", [this, id, onBar, rc] { if (onBar) TaskbarUnpinApp(id); else TaskbarPinApp(id); BuildPreview(rc); }); break; }
        case RKind::File: { ws p = r.target; Act(preview, 0xE8E5, L"Open", [this, p] { Launch(p); Close(); }); Act(preview, 0xE838, L"Open file location", [this, p] { ShowInFolder(p); Close(); }); Act(preview, 0xE7AC, L"Open with…", [this, p] { Close(); OpenWith(p); });
            Act(preview, 0xE8C8, L"Copy path", [p] { ClipboardSetText(p); }); Act(preview, 0xE946, L"Properties", [this, p] { Close(); OpenProps(p); }); break; }
        case RKind::Folder: { ws p = r.target; Act(preview, 0xE8DA, L"Open", [this, p] { Launch(p); Close(); }); Act(preview, 0xE838, L"Open location", [this, p] { ShowInFolder(p); Close(); });
            Act(preview, 0xE710, L"Add to Start folders", [this, p] { if (std::none_of(S.quickFolders.begin(), S.quickFolders.end(), [&](const ws& f) { return EqI(f, p); })) { S.quickFolders.push_back(p); SaveSettingsSoon(); RenderFolders(); } }); break; }
        case RKind::Program: { ws p = r.target; Act(preview, 0xE8E5, L"Open", [this, p] { Launch(p); Close(); }); Act(preview, 0xE838, L"Open location", [this, p] { ShowInFolder(p); Close(); }); break; }
        case RKind::Setting: Act(preview, 0xE8E5, L"Open", [this, rc] { OpenResult(rc); }); break;
        case RKind::Calc: Act(preview, 0xE8C8, L"Copy result", [this, rc] { OpenResult(rc); }); break;
        case RKind::Run: Act(preview, 0xE768, L"Run", [this, rc] { OpenResult(rc); }); if (!r.appId.empty()) Act(preview, 0xE7EF, L"Run as administrator", [this, rc] { RunCommand(rc, true); }); break;
        }
    }
    void RunCommand(const Result& r, bool admin) {
        ws raw = Trim(r.target); size_t sp = raw.find(L' ');
        if (!r.appId.empty()) Launch(r.appId, sp == ws::npos ? L"" : raw.substr(sp + 1), admin); else Launch(raw, L"", admin);
        Close();
    }
    void OpenResult(const Result& r) {
        HabitRecord(search->text, r.kind, r.target);
        switch (r.kind) {
        case RKind::App: OpenApp(r.appId); Close(); break;
        case RKind::Calc: ClipboardSetText(r.target); search->SetText(r.target); break;
        case RKind::Setting: { size_t sp = r.target.find(L' '); Launch(r.target.substr(0, sp), sp == ws::npos ? L"" : r.target.substr(sp + 1)); Close(); break; }
        case RKind::Run: RunCommand(r, false); break;
        default: Launch(r.target); Close();
        }
    }
};
}
Flyout* MakeStartPanel() { return new StartPanel(); }
