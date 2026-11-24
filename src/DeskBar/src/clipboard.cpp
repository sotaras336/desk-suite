#include "pch.h"
#include "flyout.h"
#include "widgets.h"
#include <wincrypt.h>
#include <ctime>

// Clipboard history (Desk 4.0): like Windows' Win+V, in DeskBar's look.
// - What you copy (text only) is kept in memory, newest first, at most kClipMaxRecent items of at most kClipMaxChars characters each.
//   Nothing unpinned ever touches the disk, so a restart or sign-out forgets it, as Windows does.
// - Pinned snippets stay until you unpin them. They are saved in DataDir()\clip-pins.dat, encrypted for this Windows account (DPAPI).
// - Private copies are never kept: anything a password manager marks as private ("ExcludeClipboardContentFromMonitorProcessing",
//   "CanIncludeInClipboardHistory" = 0, "Clipboard Viewer Ignore"), and anything copied by a known password manager app.
// - Win+V opens it (the input helper swallows the V, hooks.cpp), and the bar's right-click menu has it. Pick an item: it goes on the
//   clipboard and is pasted into the window you were in.
// - The listener is one small thread with a message-only window; the panel itself is built the first time it is opened.

// ---------------------------------------------------------------- the list (UI thread only)
bool ClipStore::Add(const ws& t, uint64_t at) {
    if (Trim(t).empty() || t.size() > kClipMaxChars) return false;
    for (auto& p : pinned) if (p.text == t) { p.at = at; return true; }   // already pinned: stays where it is, no copy in Recent
    for (size_t i = 0; i < recent.size(); i++) if (recent[i].text == t) { recent.erase(recent.begin() + i); break; }   // copied again: moves to the top
    recent.insert(recent.begin(), ClipItem{t, false, at});
    if (recent.size() > kClipMaxRecent) recent.resize(kClipMaxRecent);
    return true;
}
bool ClipStore::Pin(const ws& t, bool on) {
    if (on) {
        for (auto& p : pinned) if (p.text == t) return true;
        if (pinned.size() >= kClipMaxPinned || Trim(t).empty() || t.size() > kClipMaxChars) return false;
        uint64_t at = 0; for (size_t i = 0; i < recent.size(); i++) if (recent[i].text == t) { at = recent[i].at; recent.erase(recent.begin() + i); break; }
        pinned.insert(pinned.begin(), ClipItem{t, true, at}); return true;
    }
    for (size_t i = 0; i < pinned.size(); i++) if (pinned[i].text == t) {
        ClipItem it = pinned[i]; it.pinned = false; pinned.erase(pinned.begin() + i);
        recent.insert(recent.begin(), it); if (recent.size() > kClipMaxRecent) recent.resize(kClipMaxRecent); return true;
    }
    return false;
}
void ClipStore::Remove(const ws& t) {
    for (auto* v : {&pinned, &recent}) for (size_t i = 0; i < v->size(); i++) if ((*v)[i].text == t) { v->erase(v->begin() + i); return; }
}
void ClipStore::ClearRecent() { recent.clear(); recent.shrink_to_fit(); }
std::vector<const ClipItem*> ClipStore::Find(const ws& q) const {
    std::vector<const ClipItem*> r; ws k = Trim(q);
    for (auto* v : {&pinned, &recent}) for (auto& i : *v) if (k.empty() || ContainsI(i.text, k)) r.push_back(&i);
    return r;
}
size_t ClipStore::Bytes() const { size_t b = 0; for (auto* v : {&pinned, &recent}) for (auto& i : *v) b += (i.text.size() + 1) * sizeof(wchar_t); return b; }

// ---------------------------------------------------------------- privacy
static const wchar_t* const kPasswordApps[] = {
    L"keepass", L"keepassxc", L"keepassxc-proxy", L"1password", L"agilebits.onepassword", L"bitwarden", L"lastpass", L"dashlane", L"keeper", L"keeperpasswordmanager",
    L"enpass", L"roboform", L"robotaskbaricon", L"nordpass", L"proton pass", L"protonpass", L"pwsafe", L"passwordsafe", L"stpass", L"stickypassword", L"passbolt",
    L"credentialuibroker", L"keychain", L"psono", L"buttercup", L"padloc", L"zoho vault", L"securesafe",
};
bool ClipIsPasswordApp(const ws& exe) {
    ws s = Lower(StemOf(exe)); if (s.empty()) return false;
    for (auto* p : kPasswordApps) if (s == p) return true;
    return false;
}
ws ClipSkipReason(const ClipFlags& f, const ws& ownerExe, const ws& text) {
    if (f.exclude || f.viewerIgnore) return L"marked private";
    if (f.canInclude == 0) return L"marked not for history";
    if (ClipIsPasswordApp(ownerExe)) return L"from a password manager";
    if (Trim(text).empty()) return L"no text";
    if (text.size() > kClipMaxChars) return L"too long";
    return L"";
}

// ---------------------------------------------------------------- pinned snippets on disk (encrypted for this Windows account)
bool ClipSavePins(const std::vector<ClipItem>& pins, const ws& path) {
    Json j = Json::O(); Json a = Json::A(); for (auto& p : pins) a.a.push_back(Json::S(p.text)); j.Set(L"Pins", a);
    std::string plain = j.Dump();
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()}, out{};
    bool ok = CryptProtectData(&in, L"DeskBar pinned clips", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) != FALSE;
    SecureZeroMemory(plain.data(), plain.size());
    if (!ok) return false;
    std::string blob((const char*)out.pbData, out.cbData); LocalFree(out.pbData);
    return WriteAtomic(path, blob);
}
bool ClipLoadPins(std::vector<ClipItem>& pins, const ws& path) {
    pins.clear(); std::string blob; if (!ReadAll(path, blob) || blob.empty()) return false;
    DATA_BLOB in{(DWORD)blob.size(), (BYTE*)blob.data()}, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    std::string plain((const char*)out.pbData, out.cbData); SecureZeroMemory(out.pbData, out.cbData); LocalFree(out.pbData);
    Json j = Json::Parse(plain); SecureZeroMemory(plain.data(), plain.size());
    if (auto a = j.Find(L"Pins")) if (a->t == Json::Arr) for (auto& s : a->a) if (s.t == Json::Str && !s.s.empty() && pins.size() < kClipMaxPinned) pins.push_back(ClipItem{s.s, true, 0});
    return true;
}

// ---------------------------------------------------------------- Win+V (the input helper calls this for every key while Windows is held)
bool ClipKeyIsOurs(UINT vk, bool winDown, bool ctrl, bool alt, bool shift, bool on) { return on && winDown && vk == 'V' && !ctrl && !alt && !shift; }

// ---------------------------------------------------------------- the listener
namespace {
ClipStore g_clips; bool g_pinsLoaded = false; std::atomic<uint64_t> g_ownSeq{0};
std::function<void()> g_onChange;   // the panel, while it exists
void Changed() { if (g_onChange) g_onChange(); }
ws PinsPath() { return DataPath(L"clip-pins.dat"); }
void LoadPinsOnce() { if (g_pinsLoaded) return; g_pinsLoaded = true; ClipLoadPins(g_clips.pinned, PinsPath()); }
void SavePins() { auto pins = g_clips.pinned; std::thread([pins] { ClipSavePins(pins, PinsPath()); }).detach(); }

bool OpenClip(HWND h) { for (int i = 0; i < 10; i++) { if (OpenClipboard(h)) return true; Sleep(20 + i * 10); } return false; }   // another app may hold it a moment
void ReadNow(HWND h) {
    if (!S.clipHistory) return;
    DWORD seq = GetClipboardSequenceNumber(); if (seq && seq == g_ownSeq.load()) return;   // our own paste: the list already has it on top
    static UINT fExclude = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing"), fIgnore = RegisterClipboardFormatW(L"Clipboard Viewer Ignore"),
                fInclude = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
    ClipFlags f; f.exclude = IsClipboardFormatAvailable(fExclude) != FALSE; f.viewerIgnore = IsClipboardFormatAvailable(fIgnore) != FALSE;
    if (f.exclude || f.viewerIgnore || !IsClipboardFormatAvailable(CF_UNICODETEXT)) return;   // private, or not text: never opened, never read
    ws owner; if (HWND o = GetClipboardOwner()) { DWORD pid = 0; GetWindowThreadProcessId(o, &pid); if (pid) owner = ExePathOfPid(pid); }
    if (ClipIsPasswordApp(owner)) return;
    if (!OpenClip(h)) return;
    ws text;
    if (IsClipboardFormatAvailable(fInclude)) if (HANDLE d = GetClipboardData(fInclude)) { if (auto* p = (DWORD*)GlobalLock(d)) { if (GlobalSize(d) >= sizeof(DWORD)) f.canInclude = (int)*p; GlobalUnlock(d); } }
    if (f.canInclude != 0) if (HANDLE d = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = (const wchar_t*)GlobalLock(d)) { size_t cap = GlobalSize(d) / sizeof(wchar_t), n = 0; while (n < cap && n <= kClipMaxChars && p[n]) n++; if (n <= kClipMaxChars) text.assign(p, n); GlobalUnlock(d); }
    }
    CloseClipboard();
    if (!ClipSkipReason(f, owner, text).empty()) return;
    UiPost([text] { LoadPinsOnce(); if (g_clips.Add(text, (uint64_t)time(nullptr))) Changed(); });
}
LRESULT CALLBACK ListenProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLIPBOARDUPDATE) { ReadNow(h); return 0; }
    return DefWindowProcW(h, m, w, l);
}
}

void ClipStart() {
    static bool started = false; if (started) return; started = true;
    std::thread([] {
        WNDCLASSW wc{}; wc.lpfnWndProc = ListenProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DB3.ClipListen"; RegisterClassW(&wc);
        HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (!h || !AddClipboardFormatListener(h)) { Log(L"clipboard: listener failed %lu", GetLastError()); return; }
        MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    }).detach();
}
void ClipClearHistory() { g_clips.ClearRecent(); Changed(); TrimMemoryIfIdle(); }
void ClipSetOn(bool on) { S.clipHistory = on; SaveSettingsSoon(); if (!on) ClipClearHistory(); else Changed(); }
ClipStore& ClipHistory() { LoadPinsOnce(); return g_clips; }

// Put text on the clipboard. The listener skips this one copy (the list already moved it to the top).
static bool ClipPut(const ws& s) {
    if (!OpenClip(nullptr)) return false;
    EmptyClipboard(); bool ok = false;
    if (HGLOBAL m = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t))) {
        memcpy(GlobalLock(m), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(m);
        if (SetClipboardData(CF_UNICODETEXT, m)) ok = true; else GlobalFree(m);
    }
    CloseClipboard(); g_ownSeq = GetClipboardSequenceNumber(); return ok;
}

// ---------------------------------------------------------------- the panel
ws ClipPreview(const ws& t, size_t maxChars) {   // one tidy line: runs of spaces, tabs and line breaks become one space
    ws o; o.reserve(std::min(t.size(), maxChars + 1)); bool sp = false;
    for (wchar_t c : t) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == L' ' || c == 0xA0) { sp = !o.empty(); continue; }
        if (c < 0x20) continue;
        if (sp) { o += L' '; sp = false; }
        o += c; if (o.size() >= maxChars) { o += L"\u2026"; break; }
    }
    return o;
}
static ws Age(uint64_t at) {
    if (!at) return L""; int64_t s = (int64_t)time(nullptr) - (int64_t)at; if (s < 60) return L"Just now";
    if (s < 3600) return Fmt(L"%d min ago", (int)(s / 60)); if (s < 86400) return Fmt(L"%d h ago", (int)(s / 3600)); return Fmt(L"%d d ago", (int)(s / 86400));
}

namespace {
struct ClipSearch : TextBox { std::function<void(bool)> onFocusChange; void OnFocus(bool f) override { TextBox::OnFocus(f); if (onFocusChange) onFocusChange(f); } };

struct ClipPanel : Flyout {
    ClipSearch* search = nullptr; View *sBox = nullptr, *list = nullptr, *offBox = nullptr, *clearAll = nullptr; Scroll* scroll = nullptr; Label* hint = nullptr;
    std::vector<std::pair<ws, View*>> rows; int sel = -1; HWND prevFg = nullptr;
    ClipPanel() : Flyout(Panel::Clip, 380, true) {
        fixedH = 560;
        auto* head = MkBox(root, Lay::H); head->margin = Thick(20, 16, 12, 10); head->gap = 6;
        auto* t = MkLabel(head, L"Clipboard", FDisp(20, 600)); t->star = 1; t->va = Al::Center;
        clearAll = Pill(head, L"Clear", 0xE74D, false, [this] { ClipClearHistory(); SetFocus(search); }); clearAll->va = Al::Center; clearAll->tip = L"Clear history\nPinned items stay.";
        auto* gear = IconBtn(head, 0xE713, 34, 13, [this] { Close(); SettingsOpenFromApp(0); }); gear->bg = Brush{}; gear->va = Al::Center; gear->tip = L"Clipboard settings";

        sBox = MkBox(root, Lay::H); sBox->margin = Thick(16, 0, 16, 10); sBox->fh = 40; sBox->radius = 20; sBox->bg = SolidB(K::White, 0x1E); sBox->borderW = 1.5f; sBox->border = Col(K::White, 0x2A); sBox->pad = Thick(14, 0, 8, 0); sBox->gap = 10;
        auto* ic = MkGlyph(sBox, 0xE721, 14, Col(K::White, K::Dim)); ic->va = Al::Center;
        search = sBox->Add(new ClipSearch()); search->star = 1; search->va = Al::Center; search->placeholder = L"Search what you copied"; search->font = FText(13.5f); search->accName = L"Search clipboard history";
        search->onChange = [this](const ws&) { Render(); };
        search->onKeyHook = [this](UINT vk) { return ListKey(vk); };
        search->onFocusChange = [this](bool f) { sBox->border = f ? Col(K::Accent) : Col(K::White, 0x2A); Invalidate(); };

        scroll = root->Add(new Scroll()); scroll->star = 1; scroll->margin = Thick(8, 0, 8, 0);
        list = MkBox(nullptr, Lay::V); list->pad = Thick(4, 0, 4, 8); list->gap = 4; scroll->content = scroll->Add(list);

        offBox = MkBox(root, Lay::V); offBox->margin = Thick(24, 0, 24, 0); offBox->gap = 12; offBox->visible = false; offBox->star = 1;
        auto* ol = MkLabel(offBox, L"Clipboard history is off. Turn it on and the text you copy shows up here, so you can paste it again later.", FText(13), Col(K::White, K::Dim)); ol->wrap = true;
        Pill(offBox, L"Turn on", 0xE8C8, true, [this] { ClipSetOn(true); Render(); SetFocus(search); });

        Divider(root);
        hint = MkLabel(root, L"Enter pastes  \u00B7  Ctrl+P pins  \u00B7  Ctrl+Del removes", FText(11), Col(K::White, K::Dim)); hint->margin = Thick(20, 8, 16, 10);
    }
    void Pick(const ws& text) {
        ClipHistory().Add(text, (uint64_t)time(nullptr));   // to the top, as Windows does
        bool put = ClipPut(text); HWND to = prevFg; Close(true);
        if (!put || !to || !IsWindow(to)) return;
        ForceForeground(to);
        auto tries = std::make_shared<int>(0); auto id = std::make_shared<UINT_PTR>(0);
        *id = UiTimer(30, [to, tries, id] {   // wait for the Windows key to be up and the window to have the keyboard, then Ctrl+V
            bool win = (GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
            if (++*tries > 40) { UiKillTimer(*id); return; }
            if (win || GetForegroundWindow() != to) return;
            UiKillTimer(*id); SendKeys({VK_CONTROL, 'V'});
        });
    }
    void TogglePin(const ws& text) { auto& c = ClipHistory(); bool pinned = false; for (auto& p : c.pinned) if (p.text == text) pinned = true; if (c.Pin(text, !pinned)) { SavePins(); Render(text); } }
    void Drop(const ws& text) { auto& c = ClipHistory(); bool pinned = false; for (auto& p : c.pinned) if (p.text == text) pinned = true; c.Remove(text); if (pinned) SavePins(); Render(); }
    void Row(const ClipItem& it) {
        ws text = it.text;
        auto* r = MkBox(list, Lay::H); r->radius = 10; r->pad = Thick(12, 8, 6, 8); r->gap = 4; r->hit = true; r->minH = 52;
        r->bg = SolidB(K::White, K::Card); r->bgHover = SolidB(K::White, K::CardHover); r->bgPress = SolidB(K::White, 0x10);
        ws pv = ClipPreview(text, 120); r->accName = (it.pinned ? L"Pinned: " : L"") + ClipPreview(text, 200);
        auto* tx = MkBox(r, Lay::V); tx->star = 1; tx->va = Al::Center; tx->gap = 2;
        auto* l = MkLabel(tx, pv, FText(13)); l->wrap = true;
        ws sub = it.pinned ? ws(L"Pinned") : Age(it.at); size_t lines = std::count(text.begin(), text.end(), L'\n') + 1; if (lines > 1) sub += (sub.empty() ? L"" : L"  \u00B7  ") + Fmt(L"%d lines", (int)lines);
        if (!sub.empty()) MkLabel(tx, sub, FText(11), it.pinned ? Col(K::Accent) : Col(K::White, K::Dim));
        auto* pin = IconBtn(r, it.pinned ? 0xE77A : 0xE718, 36, 13, [this, text] { TogglePin(text); }); pin->bg = Brush{}; pin->va = Al::Center; pin->tip = it.pinned ? L"Unpin" : L"Pin";
        if (it.pinned) ((Glyph*)pin->kids[0])->col = Col(K::Accent);
        auto* del = IconBtn(r, 0xE74D, 36, 13, [this, text] { Drop(text); }); del->bg = Brush{}; del->va = Al::Center; del->tip = L"Remove";
        r->onClick = [this, text] { Pick(text); };
        auto menu = [this, r, text, pinned = it.pinned] {
            MenuShow({{L"Paste", 0xE77F, [this, text] { Pick(text); }}, {pinned ? L"Unpin" : L"Pin", (wchar_t)(pinned ? 0xE77A : 0xE718), [this, text] { TogglePin(text); }},
                      {L"Copy only", 0xE8C8, [this, text] { ClipHistory().Add(text, (uint64_t)time(nullptr)); ClipPut(text); Render(text); }}, {L"Remove", 0xE74D, [this, text] { Drop(text); }}}, ScreenRect(r), false, scale);
        };
        r->onRight = menu; r->onLong = menu;
        rows.push_back({text, r});
    }
    void Render(const ws& keep = L"") {
        ws prev = !keep.empty() ? keep : sel >= 0 && sel < (int)rows.size() ? rows[sel].first : ws();
        list->Clear(); rows.clear(); sel = -1;
        bool on = S.clipHistory; offBox->Show(!on); scroll->Show(on); sBox->Show(on); hint->Show(on); clearAll->Show(on && !ClipHistory().recent.empty());
        if (!on) { Relayout(); return; }
        auto found = ClipHistory().Find(search->text); bool pinHead = false, recHead = false;
        for (auto* it : found) {
            if (it->pinned && !pinHead) { pinHead = true; SectionLabel(list, L"Pinned")->margin = Thick(8, 6, 2, 4); }
            if (!it->pinned && !recHead) { recHead = true; SectionLabel(list, L"Recent")->margin = Thick(8, pinHead ? 12.f : 6.f, 2, 4); }
            Row(*it);
        }
        if (found.empty()) {
            ws msg = !Trim(search->text).empty() ? L"Nothing you copied matches \u201C" + Trim(search->text) + L"\u201D." : ws(L"Nothing copied yet. Copy some text and it shows up here. Pin what you use often.");
            auto* l = MkLabel(list, msg, FText(13), Col(K::White, K::Dim)); l->wrap = true; l->margin = Thick(12, 12, 12, 0);
        }
        int idx = -1; for (size_t i = 0; i < rows.size(); i++) if (rows[i].first == prev) idx = (int)i;
        Relayout(); if (!rows.empty()) Select(idx < 0 ? 0 : idx, false);
    }
    void Select(int i, bool scrollTo = true) {
        if (rows.empty()) return; i = std::clamp(i, 0, (int)rows.size() - 1);
        if (sel >= 0 && sel < (int)rows.size()) { rows[sel].second->bg = SolidB(K::White, K::Card); rows[sel].second->borderW = 0; }
        sel = i; View* r = rows[i].second; r->bg = SolidB(K::Accent, 0x40); r->borderW = 1; r->border = Col(K::Accent, 0x90);
        if (scrollTo && r->rc.h() > 0) {
            float top = r->rc.t - list->rc.t, bot = top + r->rc.h(), vh = scroll->rc.h(), off = scroll->off;
            if (top < off) scroll->ScrollTo(top - 8, false); else if (bot > off + vh) scroll->ScrollTo(bot - vh + 8, false);
        }
        if (hwnd) AccFocusChanged(this, r); Invalidate();
    }
    bool ListKey(UINT vk) {
        bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (vk == VK_DOWN) { Select(sel + 1); return true; } if (vk == VK_UP) { Select(sel - 1); return true; }
        if (vk == VK_NEXT) { Select(sel + 5); return true; } if (vk == VK_PRIOR) { Select(sel - 5); return true; }
        bool have = sel >= 0 && sel < (int)rows.size();
        if (vk == VK_RETURN) { if (have) Pick(rows[sel].first); return true; }
        if (ctrl && vk == 'P') { if (have) TogglePin(rows[sel].first); return true; }
        if (ctrl && vk == VK_DELETE) { if (have) { int keep = sel; Drop(rows[sel].first); if (!rows.empty()) Select(std::min(keep, (int)rows.size() - 1)); } return true; }
        return false;
    }
    bool OnKey(UINT vk, bool ctrl, bool shift) override { if (focus != search && (vk == VK_UP || vk == VK_DOWN || vk == VK_RETURN || (ctrl && (vk == 'P' || vk == VK_DELETE)))) return ListKey(vk); return false; }
    bool OnEsc() override { if (!search->text.empty()) { search->SetText(L""); return true; } return false; }
    void OnOpen() override {
        HWND fg = GetForegroundWindow(); prevFg = fg && ClassOf(fg) != kWndClass ? fg : nullptr;   // where Pick pastes
        g_onChange = [this] { if (shown) Render(); };
        search->SetText(L"", false); Render(); scroll->ScrollTo(0, false); SetFocus(search);
    }
    void OnShown() override {
        auto tries = std::make_shared<int>(0); auto id = std::make_shared<UINT_PTR>(0);
        *id = UiTimer(20, [this, tries, id] { (*tries)++; if (GetForegroundWindow() != hwnd && *tries <= 50) { if (*tries % 10 == 0) ForceForeground(hwnd); return; } UiKillTimer(*id); SetFocus(search); });
    }
    void OnClose() override { list->Clear(); rows.clear(); sel = -1; }   // the rows hold copies of the text: let them go while the panel is hidden
};
}
Flyout* MakeClipPanel() { return new ClipPanel(); }
void OpenClipboardPanel(Wnd* from) { TogglePanel(Panel::Clip, from); }

// ---------------------------------------------------------------- self-test (selftest.cpp)
bool ClipSelfTest(ws& detail) {
    ws bad; auto expect = [&](bool ok, const wchar_t* what) { if (!ok && bad.empty()) bad = what; };
    ClipStore c;
    expect(c.Add(L"one", 1) && c.Add(L"two", 2) && c.Add(L"one", 3) && c.recent.size() == 2 && c.recent[0].text == L"one", L"copied again moves to the top, no duplicate");
    expect(!c.Add(L"   \r\n", 4) && !c.Add(L"", 4), L"blank copies are not kept");
    expect(!c.Add(ws(kClipMaxChars + 1, L'x'), 5) && c.Add(ws(kClipMaxChars, L'x'), 5), L"size cap per item");
    c.ClearRecent(); for (int i = 0; i < (int)kClipMaxRecent + 10; i++) c.Add(Fmt(L"item %d", i), i);
    expect(c.recent.size() == kClipMaxRecent && c.recent[0].text == Fmt(L"item %d", (int)kClipMaxRecent + 9), L"history keeps only the newest items");
    expect(c.Pin(L"item 34", true) && c.pinned.size() == 1 && c.recent.size() == kClipMaxRecent - 1, L"pin moves an item out of Recent");
    for (int i = 0; i < 40; i++) c.Add(Fmt(L"more %d", i), 100 + i);
    expect(c.pinned.size() == 1 && c.pinned[0].text == L"item 34", L"pinned items are never pushed out");
    c.Add(L"item 34", 200); expect(c.recent.empty() || c.recent[0].text != L"item 34", L"copying a pinned item does not add a second copy");
    c.ClearRecent(); expect(c.recent.empty() && c.pinned.size() == 1, L"clear keeps pinned items");
    c.Add(L"Hello World", 1); c.Add(L"hello again", 2); c.Add(L"bye", 3);
    auto f = c.Find(L"HELLO"); expect(f.size() == 2, L"search ignores case"); expect(c.Find(L"").size() == 4 && c.Find(L"")[0]->pinned, L"empty search lists pinned first");
    expect(c.Pin(L"item 34", false) && c.pinned.empty() && c.recent[0].text == L"item 34", L"unpin puts it back on top of Recent");
    c.Remove(L"bye"); expect(c.Find(L"bye").empty(), L"remove");
    { ClipStore d; for (int i = 0; i < (int)kClipMaxPinned; i++) { d.Add(Fmt(L"p%d", i), i); d.Pin(Fmt(L"p%d", i), true); } d.Add(L"extra", 1); expect(!d.Pin(L"extra", true) && d.pinned.size() == kClipMaxPinned, L"pinned cap"); }
    // privacy
    ClipFlags none, ex, ign, noHist, yesHist; ex.exclude = true; ign.viewerIgnore = true; noHist.canInclude = 0; yesHist.canInclude = 1;
    expect(ClipSkipReason(none, L"C:\\Windows\\notepad.exe", L"hi").empty() && ClipSkipReason(yesHist, L"", L"hi").empty(), L"a normal copy is kept");
    expect(!ClipSkipReason(ex, L"", L"pw").empty() && !ClipSkipReason(ign, L"", L"pw").empty() && !ClipSkipReason(noHist, L"", L"pw").empty(), L"copies marked private are skipped");
    expect(!ClipSkipReason(none, L"C:\\Program Files\\KeePassXC\\KeePassXC.exe", L"pw").empty() && !ClipSkipReason(none, L"C:\\Users\\x\\AppData\\Local\\1Password\\app\\8\\1Password.exe", L"pw").empty()
           && !ClipSkipReason(none, L"C:\\Program Files\\Bitwarden\\Bitwarden.exe", L"pw").empty(), L"password managers are skipped");
    expect(!ClipIsPasswordApp(L"C:\\Windows\\explorer.exe") && !ClipIsPasswordApp(L""), L"other apps are not password managers");
    // Win+V
    expect(ClipKeyIsOurs('V', true, false, false, false, true) && !ClipKeyIsOurs('V', true, false, false, false, false) && !ClipKeyIsOurs('V', false, false, false, false, true)
           && !ClipKeyIsOurs('V', true, true, false, false, true) && !ClipKeyIsOurs('V', true, false, false, true, true) && !ClipKeyIsOurs('C', true, false, false, false, true), L"Win+V and only Win+V");
    // preview
    expect(ClipPreview(L"  a\r\n\tb   c ", 50) == L"a b c" && ClipPreview(L"abcdef", 3) == L"abc\u2026", L"preview tidies spaces and cuts long text");
    // pinned snippets on disk: encrypted, and they come back
    {
        ws dir = DataDir() + L"\\clip-selftest"; CreateDirectoryW(dir.c_str(), nullptr); ws p = dir + L"\\pins.dat";
        std::vector<ClipItem> pins{{L"secret snippet \u00E9\u4E2D", true, 0}, {L"line one\r\nline two", true, 0}}, back;
        bool saved = ClipSavePins(pins, p); std::string raw; ReadAll(p, raw);
        expect(saved && !raw.empty() && raw.find("secret snippet") == std::string::npos, L"pinned file is encrypted");
        expect(ClipLoadPins(back, p) && back.size() == 2 && back[0].text == pins[0].text && back[1].text == pins[1].text && back[0].pinned, L"pinned snippets come back after a restart");
        { std::string junk = "not a dpapi blob"; WriteAtomic(p, junk); } expect(!ClipLoadPins(back, p) && back.empty(), L"a damaged file loads as no pins");
        DeleteFileW(p.c_str()); RemoveDirectoryW(dir.c_str());
    }
    // the panel: fits a 1080p screen at 150 %, every button has a name, keyboard picks move the selection
    {
        bool keepOn = S.clipHistory; S.clipHistory = true;
        ClipStore keep = g_clips; bool keepLoaded = g_pinsLoaded; g_pinsLoaded = true; g_clips = ClipStore();
        g_clips.Add(L"alpha", 1); g_clips.Add(L"beta", 2); g_clips.Add(L"gamma", 3); g_clips.Pin(L"alpha", true);
        std::unique_ptr<ClipPanel> cp(new ClipPanel()); cp->Render();
        expect(cp->rows.size() == 3 && cp->rows[0].first == L"alpha" && cp->sel == 0, L"panel lists pinned first and selects the first");
        cp->ListKey(VK_DOWN); expect(cp->sel == 1 && cp->rows[1].first == L"gamma", L"Down moves the selection");
        cp->search->SetText(L"bet"); expect(cp->rows.size() == 1 && cp->rows[0].first == L"beta", L"typing filters the list");
        cp->search->SetText(L"");
        float h = std::min(cp->fixedH, cp->root->Measure({cp->widthDip, 1e5f}).h); expect(h <= 664.5f, L"panel fits 150% 1080p");
        bool named = true; std::function<void(View*)> walk = [&](View* v) { if (v->visible && v->Clickable() && AccNameOf(v).empty()) named = false; for (auto* k : v->kids) walk(k); }; walk(cp->root);
        expect(named, L"every button in the panel has a name for screen readers");
        S.clipHistory = false; cp->Render(); expect(cp->offBox->visible && !cp->scroll->visible, L"off: the panel says so and offers to turn it on");
        cp.reset(); g_clips = keep; g_pinsLoaded = keepLoaded; S.clipHistory = keepOn;
    }
    detail = bad.empty() ? Fmt(L"store, privacy, Win+V, encrypted pins, panel (%d KB for %d full items)", (int)(kClipMaxRecent * (kClipMaxChars + 1) * sizeof(wchar_t) / 1024), (int)kClipMaxRecent) : bad;
    return bad.empty();
}
