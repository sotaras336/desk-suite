#include "pch.h"
#include "app.h"
#include <regex>

// Window list: scanned on a below-normal STA thread (never on the UI thread), refreshed every second and instantly on window events.
static const PROPERTYKEY kAumid = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

ws PackageFamilyFromPath(const ws& p) {   // "...\WindowsApps\Claude_2.3_x64__pzs8sxrjxfjjc\app\claude.exe" -> "Claude_pzs8sxrjxfjjc"
    static const std::wregex re(LR"(\\WindowsApps\\([^_\\]+)_[^_\\]*_[^_\\]*_[^_\\]*_([a-z0-9]{13})\\)", std::regex::icase);
    std::wsmatch m; if (std::regex_search(p, m, re)) return m[1].str() + L"_" + m[2].str();
    return {};
}
static ws AumidOfStore(IPropertyStore* ps) {
    PROPVARIANT v; PropVariantInit(&v); ws r;
    if (SUCCEEDED(ps->GetValue(kAumid, &v)) && v.vt == VT_LPWSTR && v.pwszVal) r = v.pwszVal;
    PropVariantClear(&v); return r;
}
ws AumidOfShortcut(const ws& lnk) {
    ComPtr<IShellLinkW> sl; if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return {};
    ComPtr<IPersistFile> pf; if (FAILED(sl.As(&pf)) || FAILED(pf->Load(lnk.c_str(), STGM_READ))) return {};
    ComPtr<IPropertyStore> ps; if (FAILED(sl.As(&ps))) return {};
    return AumidOfStore(ps.Get());
}

namespace {
std::mutex g_mu; std::shared_ptr<const WinSnap> g_snap = std::make_shared<WinSnap>();
std::function<void()> g_onChange; std::atomic<bool> g_notifyPending{false};
DWORD g_tid = 0; std::set<HWND> g_flash; std::mutex g_flashMu;
std::unordered_map<DWORD, ws> g_exe;                                         // pid -> exe path
struct AumCache { ws v; uint64_t at; }; std::unordered_map<HWND, AumCache> g_aum;
bool g_dirty = true; UINT_PTR g_debounce = 0, g_tick = 0;

void CALLBACK OnEvent(HWINEVENTHOOK, DWORD ev, HWND h, LONG obj, LONG child, DWORD, DWORD) {
    if (obj != OBJID_WINDOW || child != CHILDID_SELF) return;
    g_dirty = true; if (!g_debounce) g_debounce = SetTimer(nullptr, 0, 30, nullptr);       // debounce: scan 30 ms after the first event (a thread timer's id is assigned by the system: keep it, or every event leaks a timer)
}

bool Candidate(HWND h) {
    if (!IsWindowVisible(h)) return false;
    if (GetWindow(h, GW_OWNER)) return false;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_APPWINDOW)) return false;
    if (ex & WS_EX_NOACTIVATE && !(ex & WS_EX_APPWINDOW)) return false;
    int cloaked = 0; if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, 4)) && cloaked) return false;
    if (GetWindowTextLengthW(h) == 0) return false;
    return true;
}

void Scan() {
    auto snap = std::make_shared<WinSnap>(); snap->fg = GetForegroundWindow(); DWORD me = GetCurrentProcessId(); uint64_t now = Now();
    std::set<DWORD> pids; std::set<HWND> seen;
    std::vector<HWND> all;
    EnumWindows([](HWND h, LPARAM p) -> BOOL { ((std::vector<HWND>*)p)->push_back(h); return TRUE; }, (LPARAM)&all);
    for (HWND h : all) {
        if (!Candidate(h)) continue;
        DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == me) continue;
        pids.insert(pid); seen.insert(h);
        WinInfo w; w.h = h; w.pid = pid;
        wchar_t t[512]; int n = GetWindowTextW(h, t, 512); w.title.assign(t, n);
        auto it = g_exe.find(pid); if (it == g_exe.end()) it = g_exe.emplace(pid, ExePathOfPid(pid)).first;
        w.exe = it->second;
        auto ac = g_aum.find(h);
        if (ac == g_aum.end() || (ac->second.v.empty() && now - ac->second.at > 5000)) {
            ws a; ComPtr<IPropertyStore> ps;
            if (SUCCEEDED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) a = AumidOfStore(ps.Get());
            ac = g_aum.insert_or_assign(h, AumCache{a, now}).first;
        }
        w.aumid = ac->second.v; w.family = PackageFamilyFromPath(w.exe);
        if (h == snap->fg) { std::lock_guard<std::mutex> l(g_flashMu); g_flash.erase(h); }   // looking at it ends the flashing
        w.minimized = IsIconic(h); w.flashing = IsFlashing(h);
        snap->list.push_back(std::move(w));
    }
    for (auto it = g_exe.begin(); it != g_exe.end();) it = pids.count(it->first) ? std::next(it) : g_exe.erase(it);
    for (auto it = g_aum.begin(); it != g_aum.end();) it = seen.count(it->first) ? std::next(it) : g_aum.erase(it);
    { std::lock_guard<std::mutex> l(g_flashMu); for (auto it = g_flash.begin(); it != g_flash.end();) it = seen.count(*it) ? std::next(it) : g_flash.erase(it); }

    bool changed;
    { std::lock_guard<std::mutex> l(g_mu);
      auto& o = *g_snap; changed = o.fg != snap->fg || o.list.size() != snap->list.size();
      for (size_t i = 0; !changed && i < o.list.size(); i++) {
          const WinInfo& a = o.list[i]; const WinInfo& b = snap->list[i];
          changed = a.h != b.h || a.title != b.title || a.minimized != b.minimized || a.flashing != b.flashing || a.aumid != b.aumid;
      }
      if (changed) g_snap = snap; }
    if (changed && !g_notifyPending.exchange(true))
        UiPost([] { g_notifyPending = false; if (g_onChange) g_onChange(); });
}

void Thread() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    g_tid = GetCurrentThreadId();
    struct R { DWORD lo, hi; } ranges[] = {{EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND}, {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_CREATE, EVENT_OBJECT_HIDE}, {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED}, {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE}};
    for (auto r : ranges) SetWinEventHook(r.lo, r.hi, nullptr, OnEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_tick = SetTimer(nullptr, 0, 1000, nullptr);
    Scan();
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (m.message == WM_TIMER && m.hwnd == nullptr && (m.wParam == g_tick || m.wParam == g_debounce)) {
            if (m.wParam == g_debounce) { KillTimer(nullptr, g_debounce); g_debounce = 0; } Scan(); g_dirty = false;
        } else if (m.message == WM_APP) { Scan(); }
        else { TranslateMessage(&m); DispatchMessageW(&m); }
    }
}
}

void WinTrackerStart() { std::thread(Thread).detach(); }
std::shared_ptr<const WinSnap> WinSnapshot() { std::lock_guard<std::mutex> l(g_mu); return g_snap; }
void WinTrackerOnChange(std::function<void()> f) { g_onChange = std::move(f); }
void WinTrackerPoke() { if (g_tid) PostThreadMessageW(g_tid, WM_APP, 0, 0); }
void SetFlashing(HWND h, bool on) {
    { std::lock_guard<std::mutex> l(g_flashMu); if (on) g_flash.insert(h); else g_flash.erase(h); }
    WinTrackerPoke();
}
bool IsFlashing(HWND h) { std::lock_guard<std::mutex> l(g_flashMu); return g_flash.count(h) != 0; }
