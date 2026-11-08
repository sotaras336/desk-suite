#include "pch.h"
#include "core.h"
#include <bcrypt.h>

// ================= strings / paths =================
ws Widen(const std::string& u) {
    if (u.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0);
    ws w(n, 0); MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), w.data(), n); return w;
}
std::string Narrow(const ws& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), u.data(), n, nullptr, nullptr); return u;
}
ws Fmt(const wchar_t* f, ...) {
    va_list va; va_start(va, f);
    wchar_t buf[1024]; int n = _vsnwprintf_s(buf, _TRUNCATE, f, va); va_end(va);
    return n < 0 ? ws(buf) : ws(buf, n);
}
ws Lower(const ws& s) { ws r = s; for (auto& c : r) c = (wchar_t)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)c); return r; }
bool EqI(const ws& a, const ws& b) { return a.size() == b.size() && _wcsicmp(a.c_str(), b.c_str()) == 0; }
bool StartsI(const ws& s, const ws& p) { return s.size() >= p.size() && _wcsnicmp(s.c_str(), p.c_str(), p.size()) == 0; }
bool ContainsI(const ws& s, const ws& p) { return p.empty() || Lower(s).find(Lower(p)) != ws::npos; }
ws Trim(const ws& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) a++;
    while (b > a && iswspace(s[b - 1])) b--;
    return s.substr(a, b - a);
}
std::vector<ws> Split(const ws& s, wchar_t sep) {
    std::vector<ws> r; size_t i = 0;
    for (;;) { size_t j = s.find(sep, i); if (j == ws::npos) { r.push_back(s.substr(i)); break; } r.push_back(s.substr(i, j - i)); i = j + 1; }
    return r;
}
ws ExpandEnv(const ws& s) {
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0); if (!n) return s;
    ws r(n, 0); ExpandEnvironmentStringsW(s.c_str(), r.data(), n); r.resize(n - 1); return r;
}
ws FileNameOf(const ws& p) { size_t i = p.find_last_of(L"\\/"); return i == ws::npos ? p : p.substr(i + 1); }
ws DirOf(const ws& p) { size_t i = p.find_last_of(L"\\/"); return i == ws::npos ? ws() : p.substr(0, i); }
ws StemOf(const ws& p) { ws f = FileNameOf(p); size_t i = f.find_last_of(L'.'); return i == ws::npos || i == 0 ? f : f.substr(0, i); }
ws ExtOf(const ws& p) { ws f = FileNameOf(p); size_t i = f.find_last_of(L'.'); return i == ws::npos ? ws() : Lower(f.substr(i)); }
ws KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; ws r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &p))) r = p;
    CoTaskMemFree(p); return r;
}
ws DataDir() {
    static ws d = [] {
        wchar_t e[MAX_PATH]; ws r;
        if (GetEnvironmentVariableW(L"DB3_DATA", e, MAX_PATH)) r = e;
        else r = KnownFolder(FOLDERID_LocalAppData) + L"\\DeskBar3";
        SHCreateDirectoryExW(nullptr, r.c_str(), nullptr); return r;
    }();
    return d;
}
ws DataPath(const wchar_t* leaf) { return DataDir() + L"\\" + leaf; }
bool FileExists(const ws& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
bool DirExists(const ws& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
bool ReadAll(const ws& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz); out.resize((size_t)sz.QuadPart);
    DWORD got = 0; bool ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h); out.resize(got); return ok;
}
bool WriteAtomic(const ws& path, const std::string& data) {
    ws tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0; bool ok = (data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr)) && FlushFileBuffers(h);   // flushed before the rename: a power cut must not leave an empty settings file
    CloseHandle(h);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
uint64_t Now() { return GetTickCount64(); }
uint64_t Sha1Prefix(const ws& s) {
    ws l = Lower(s); BCRYPT_ALG_HANDLE alg = nullptr; uint8_t hash[20] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0) {
        BCryptHash(alg, nullptr, 0, (PUCHAR)l.data(), (ULONG)(l.size() * 2), hash, 20); BCryptCloseAlgorithmProvider(alg, 0);
    }
    uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | hash[i]; return v;
}
ws Hex16(uint64_t v) { wchar_t b[20]; swprintf_s(b, L"%016llx", v); return b; }

// ================= log =================
static std::mutex g_logMu; static std::deque<std::string> g_logQ; static HANDLE g_logEv; static ws g_logPath;
static FILETIME g_procStart;
static void LogWriter() {
    for (;;) {
        WaitForSingleObject(g_logEv, INFINITE);
        std::deque<std::string> q; { std::lock_guard<std::mutex> l(g_logMu); q.swap(g_logQ); }
        if (q.empty()) continue;
        HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        for (auto& s : q) { DWORD w; WriteFile(h, s.data(), (DWORD)s.size(), &w, nullptr); }
        CloseHandle(h);
    }
}
void LogInit(bool fresh) {
    g_logPath = DataPath(L"deskbar3.log");
    if (fresh) { WIN32_FILE_ATTRIBUTE_DATA d{}; if (GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &d) && d.nFileSizeLow > 512 * 1024) DeleteFileW(g_logPath.c_str()); }
    FILETIME a, b, c; GetProcessTimes(GetCurrentProcess(), &g_procStart, &a, &b, &c);
    g_logEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(LogWriter).detach();
}
void Log(const wchar_t* f, ...) {
    va_list va; va_start(va, f);
    wchar_t buf[1500]; _vsnwprintf_s(buf, _TRUNCATE, f, va); va_end(va);
    SYSTEMTIME t; GetLocalTime(&t);
    wchar_t line[1700]; swprintf_s(line, L"%02d:%02d:%02d.%03d [%lu] %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), buf);
    if (!g_logEv) return;
    { std::lock_guard<std::mutex> l(g_logMu); if (g_logQ.size() < 2000) g_logQ.push_back(Narrow(line)); }
    SetEvent(g_logEv);
}
void Boot(const wchar_t* step) {
    FILETIME n; GetSystemTimeAsFileTime(&n);
    uint64_t a = ((uint64_t)n.dwHighDateTime << 32 | n.dwLowDateTime), b = ((uint64_t)g_procStart.dwHighDateTime << 32 | g_procStart.dwLowDateTime);
    Log(L"boot %s +%llu ms", step, (a - b) / 10000);
}

// ================= UI thread =================
const UINT WM_UIPOST = WM_APP + 1;
static HWND g_ui; static DWORD g_uiTid;
static std::mutex g_uiMu; static std::deque<std::function<void()>> g_uiQ;
static std::map<UINT_PTR, std::function<void()>> g_timers; static UINT_PTR g_nextTimer = 100;
static LRESULT CALLBACK UiProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_UIPOST) {
        for (;;) {
            std::function<void()> f; { std::lock_guard<std::mutex> lk(g_uiMu); if (g_uiQ.empty()) break; f = std::move(g_uiQ.front()); g_uiQ.pop_front(); }
            f();
        }
        return 0;
    }
    if (m == WM_TIMER) {
        auto it = g_timers.find(w);
        if (it != g_timers.end()) { auto f = it->second; f(); }   // copy: callback may kill its own timer
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void UiInit() {
    g_uiTid = GetCurrentThreadId();
    WNDCLASSW wc{}; wc.lpfnWndProc = UiProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DB3.Ui";
    RegisterClassW(&wc);
    g_ui = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    timeBeginPeriod(1);
}
bool OnUiThread() { return GetCurrentThreadId() == g_uiTid; }
HWND UiHwnd() { return g_ui; }
void UiPost(std::function<void()> f) {
    { std::lock_guard<std::mutex> l(g_uiMu); g_uiQ.push_back(std::move(f)); }
    PostMessageW(g_ui, WM_UIPOST, 0, 0);
}
UINT_PTR UiTimer(UINT ms, std::function<void()> f) {
    UINT_PTR id = g_nextTimer++; g_timers[id] = std::move(f); SetTimer(g_ui, id, ms, nullptr); return id;
}
void UiKillTimer(UINT_PTR id) { KillTimer(g_ui, id); g_timers.erase(id); }
void UiOnce(UINT ms, std::function<void()> f) {
    auto id = std::make_shared<UINT_PTR>(0);
    *id = UiTimer(ms, [id, f = std::move(f)] { UiKillTimer(*id); f(); });
}

// ================= JSON =================
namespace {
struct JP {
    const std::string& s; size_t i = 0;
    void ws_() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || (unsigned char)s[i] == 0xEF || (unsigned char)s[i] == 0xBB || (unsigned char)s[i] == 0xBF)) i++; }
    ws str() {
        std::string out; i++;   // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char c = s[++i];
                switch (c) {
                case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break; case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16); i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i + 1, 2, "\\u") == 0) { unsigned lo = (unsigned)strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16); i += 6; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
                    wchar_t w[2]; int n = 0; if (cp >= 0x10000) { cp -= 0x10000; w[0] = (wchar_t)(0xD800 + (cp >> 10)); w[1] = (wchar_t)(0xDC00 + (cp & 0x3FF)); n = 2; } else { w[0] = (wchar_t)cp; n = 1; }
                    out += Narrow(ws(w, n)); break;
                }
                default: out += c;
                }
            } else out += s[i];
            i++;
        }
        i++; return Widen(out);
    }
    Json val() {
        ws_(); Json j; if (i >= s.size()) return j;
        char c = s[i];
        if (c == '{') {
            j.t = Json::Obj; i++; ws_();
            while (i < s.size() && s[i] != '}') { ws_(); if (s[i] != '"') break; ws k = str(); ws_(); if (s[i] == ':') i++; j.o.emplace_back(k, val()); ws_(); if (s[i] == ',') i++; ws_(); }
            i++;
        } else if (c == '[') {
            j.t = Json::Arr; i++; ws_();
            while (i < s.size() && s[i] != ']') { j.a.push_back(val()); ws_(); if (s[i] == ',') i++; ws_(); }
            i++;
        } else if (c == '"') { j.t = Json::Str; j.s = str(); }
        else if (s.compare(i, 4, "true") == 0) { j.t = Json::Bool; j.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { j.t = Json::Bool; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else { char* e; j.n = strtod(s.c_str() + i, &e); j.t = Json::Num; i = e - s.c_str(); if (e == s.c_str() + i && i < s.size() && !isdigit((unsigned char)s[i]) && s[i] != '-') i++; }
        return j;
    }
};
void DumpStr(std::string& o, const ws& w) {
    o += '"';
    for (char c : Narrow(w)) {
        switch (c) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default: if ((unsigned char)c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; } else o += c; }
    }
    o += '"';
}
void DumpJ(const Json& j, std::string& o, int ind, int lvl) {
    auto nl = [&](int l) { if (ind) { o += '\n'; o.append((size_t)l * ind, ' '); } };
    switch (j.t) {
    case Json::Null: o += "null"; break;
    case Json::Bool: o += j.b ? "true" : "false"; break;
    case Json::Num: { char b[40]; if (j.n == (long long)j.n) sprintf_s(b, "%lld", (long long)j.n); else sprintf_s(b, "%.17g", j.n); o += b; break; }
    case Json::Str: DumpStr(o, j.s); break;
    case Json::Arr: if (j.a.empty()) { o += "[]"; break; } o += '['; for (size_t k = 0; k < j.a.size(); k++) { nl(lvl + 1); DumpJ(j.a[k], o, ind, lvl + 1); if (k + 1 < j.a.size()) o += ','; } nl(lvl); o += ']'; break;
    case Json::Obj: if (j.o.empty()) { o += "{}"; break; } o += '{'; for (size_t k = 0; k < j.o.size(); k++) { nl(lvl + 1); DumpStr(o, j.o[k].first); o += ind ? ": " : ":"; DumpJ(j.o[k].second, o, ind, lvl + 1); if (k + 1 < j.o.size()) o += ','; } nl(lvl); o += '}'; break;
    }
}
}
Json Json::Parse(const std::string& u8) { JP p{u8}; return p.val(); }
std::string Json::Dump(int indent) const { std::string o; DumpJ(*this, o, indent, 0); return o; }
const Json* Json::Find(const wchar_t* k) const { for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
Json& Json::Set(const wchar_t* k, Json v) {
    t = Obj; for (auto& p : o) if (p.first == k) { p.second = std::move(v); return p.second; }
    o.emplace_back(k, std::move(v)); return o.back().second;
}

// ================= settings =================
Settings S;
static ULONGLONG g_setStamp; static UINT_PTR g_saveTimer;
static std::vector<ws> StrList(const Json* j) { std::vector<ws> r; if (j && j->t == Json::Arr) for (auto& e : j->a) if (e.t == Json::Str) r.push_back(e.s); return r; }
static Json ListJ(const std::vector<ws>& v) { Json a = Json::A(); for (auto& s : v) a.a.push_back(Json::S(s)); return a; }
static bool BoolOf(const Json* j, bool d) { return j && j->t == Json::Bool ? j->b : d; }
static void ApplySettings(const Json& j) {
    if (j.t != Json::Obj) return;
    S.taskbarPins = StrList(j.Find(L"TaskbarPins")); S.pinsImported = BoolOf(j.Find(L"PinsImported"), false);
    S.winKeyOpensStart = BoolOf(j.Find(L"WinKeyOpensStart"), true); S.mouseBridge = BoolOf(j.Find(L"MouseBridge"), true);
    if (auto p = j.Find(L"EdgeResistance")) if (p->t == Json::Num) S.edgeResistance = (int)p->n;
    S.snapOnRelease = BoolOf(j.Find(L"SnapOnRelease"), true); S.keepSizeRatio = BoolOf(j.Find(L"KeepSizeRatio"), true);
    S.rescueWindows = BoolOf(j.Find(L"RescueWindows"), true); S.popupsFollowOwner = BoolOf(j.Find(L"PopupsFollowOwner"), true); S.showMedia = BoolOf(j.Find(L"ShowMedia"), true);
    S.showBatteryPercent = BoolOf(j.Find(L"ShowBatteryPercent"), true);
    S.widgets = StrList(j.Find(L"Widgets"));
    S.updateCheck = BoolOf(j.Find(L"UpdateCheck"), true); if (auto p = j.Find(L"UpdateLast")) if (p->t == Json::Num) S.updateLast = p->n;
    if (auto p = j.Find(L"UpdateKnown")) if (p->t == Json::Str) S.updateKnown = p->s;
    S.mirrorDevices = StrList(j.Find(L"MirrorDevices"));
    S.quickFolders = StrList(j.Find(L"QuickFolders")); S.foldersInit = BoolOf(j.Find(L"FoldersInit"), !S.quickFolders.empty());
    S.startPins = StrList(j.Find(L"StartPins")); S.startPinsInit = BoolOf(j.Find(L"StartPinsInit"), !S.startPins.empty()); S.startPinsImported = BoolOf(j.Find(L"StartPinsImported"), false);
    S.pinnedRecents = StrList(j.Find(L"PinnedRecents")); S.hiddenRecents = StrList(j.Find(L"HiddenRecents"));
    S.promotedTrayIcons = StrList(j.Find(L"PromotedTrayIcons")); S.trayImported = BoolOf(j.Find(L"TrayImported"), false);
    if (auto t = j.Find(L"Theme")) if (t->t == Json::Str) S.theme = t->s;
    SetTheme(S.theme);
    S.launchCounts.clear();
    if (auto lc = j.Find(L"LaunchCounts")) if (lc->t == Json::Obj) for (auto& p : lc->o) S.launchCounts[p.first] = (int)p.second.n;
}
void LoadSettings() {
    std::string raw; ws path = DataPath(L"settings.json");
    if (ReadAll(path, raw)) { ApplySettings(Json::Parse(raw)); }
    else {   // first run: take what DeskBar2 had
        ws old = KnownFolder(FOLDERID_LocalAppData) + L"\\DeskBar2\\settings.json";
        if (ReadAll(old, raw)) { ApplySettings(Json::Parse(raw)); Log(L"settings imported from DeskBar2"); }
    }
    WIN32_FILE_ATTRIBUTE_DATA d{}; if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) g_setStamp = ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
void SaveSettingsNow() {
    Json j = Json::O();
    j.Set(L"TaskbarPins", ListJ(S.taskbarPins)); j.Set(L"PinsImported", Json::B(S.pinsImported));
    j.Set(L"WinKeyOpensStart", Json::B(S.winKeyOpensStart)); j.Set(L"MouseBridge", Json::B(S.mouseBridge)); j.Set(L"EdgeResistance", Json::N(S.edgeResistance));
    j.Set(L"SnapOnRelease", Json::B(S.snapOnRelease)); j.Set(L"KeepSizeRatio", Json::B(S.keepSizeRatio)); j.Set(L"RescueWindows", Json::B(S.rescueWindows)); j.Set(L"PopupsFollowOwner", Json::B(S.popupsFollowOwner));
    j.Set(L"ShowMedia", Json::B(S.showMedia)); j.Set(L"ShowBatteryPercent", Json::B(S.showBatteryPercent));
    j.Set(L"Widgets", ListJ(S.widgets)); j.Set(L"UpdateCheck", Json::B(S.updateCheck)); j.Set(L"UpdateLast", Json::N(S.updateLast)); j.Set(L"UpdateKnown", Json::S(S.updateKnown));
    j.Set(L"MirrorDevices", ListJ(S.mirrorDevices));
    j.Set(L"QuickFolders", ListJ(S.quickFolders)); j.Set(L"FoldersInit", Json::B(S.foldersInit));
    j.Set(L"StartPins", ListJ(S.startPins)); j.Set(L"StartPinsInit", Json::B(S.startPinsInit)); j.Set(L"StartPinsImported", Json::B(S.startPinsImported));
    j.Set(L"PinnedRecents", ListJ(S.pinnedRecents)); j.Set(L"HiddenRecents", ListJ(S.hiddenRecents));
    j.Set(L"PromotedTrayIcons", ListJ(S.promotedTrayIcons)); j.Set(L"TrayImported", Json::B(S.trayImported)); j.Set(L"Theme", Json::S(S.theme));
    Json lc = Json::O(); for (auto& p : S.launchCounts) lc.Set(p.first.c_str(), Json::N(p.second));
    j.Set(L"LaunchCounts", lc);
    ws path = DataPath(L"settings.json");
    WriteAtomic(path, j.Dump(2));
    WIN32_FILE_ATTRIBUTE_DATA d{}; if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) g_setStamp = ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
void SaveSettingsSoon() {
    if (g_saveTimer) UiKillTimer(g_saveTimer);
    g_saveTimer = UiTimer(400, [] { UiKillTimer(g_saveTimer); g_saveTimer = 0; SaveSettingsNow(); });
}
bool SettingsFileChanged() {
    WIN32_FILE_ATTRIBUTE_DATA d{}; if (!GetFileAttributesExW(DataPath(L"settings.json").c_str(), GetFileExInfoStandard, &d)) return false;
    ULONGLONG t = ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    if (t == g_setStamp) return false;
    LoadSettings(); return true;
}

// ================= palette =================
static void ToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}); l = (mx + mn) / 2; h = s = 0;
    if (mx == mn) return;
    float d = mx - mn; s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
    h *= 60;
}
static float Hue2(float p, float q, float t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1.f / 6) return p + (q - p) * 6 * t; if (t < .5f) return q; if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6; return p;
}
const ThemeDef kThemes[] = {
    {L"pink", L"Pink", 336, true, 0}, {L"blue", L"Blue", -1, false, 0}, {L"purple", L"Purple", 268, false, 0}, {L"teal", L"Teal", 176, false, 0},
    {L"green", L"Green", 140, false, 0}, {L"orange", L"Orange", 26, false, 0}, {L"red", L"Red", 354, false, 0},
    {L"black", L"Black", -1, false, 1}, {L"white", L"White", -1, false, 2}, {L"glass", L"Glass", -1, false, 3},
};
const int kThemeCount = (int)(sizeof kThemes / sizeof kThemes[0]);
#ifdef PINK
static const ThemeDef* g_theme = &kThemes[0];
#else
static const ThemeDef* g_theme = &kThemes[1];
#endif
const ThemeDef& CurrentTheme() { return *g_theme; }
void SetTheme(const ws& id) {
#ifdef PINK
    const ThemeDef* d = &kThemes[0];
#else
    const ThemeDef* d = &kThemes[1];
#endif
    for (int i = 0; i < kThemeCount; i++) if (EqI(kThemes[i].id, id)) d = &kThemes[i];
    g_theme = d;
}
D2D1_COLOR_F ColFor(const ThemeDef& t, uint32_t rgb, int a) {
    float r = ((rgb >> 16) & 255) / 255.f, g = ((rgb >> 8) & 255) / 255.f, b = (rgb & 255) / 255.f; float al = a / 255.f;
    if (t.kind > 0) {   // black / white / glass: the dark blues of the original become neutral; the light blues (accents) stay, darker on white
        float h, s, l; ToHsl(r, g, b, h, s, l); bool white = t.kind == 2, blue = h >= 185 && h <= 280 && s > 0.2f;
        auto grey = [&](float v) { r = g = b = v; };
        auto hsl = [&](float hh, float ss, float ll) { float q = ll < .5f ? ll * (1 + ss) : ll + ss - ll * ss, p = 2 * ll - q, x = hh / 360; r = Hue2(p, q, x + 1.f / 3); g = Hue2(p, q, x); b = Hue2(p, q, x - 1.f / 3); };
        if (white && rgb == 0xFFFFFF) grey(0x1B / 255.f);          // text and highlights: white -> near black
        else if (white && rgb == 0x000000) grey(1.f);              // text on accent fills: black -> white
        else if (blue && l < 0.5f) { grey(white ? 1 - l * 0.3f : l * (t.kind == 3 ? 0.4f : 0.5f)); if (t.kind == 3) al *= 0.55f; }   // backgrounds
        else if (blue && white) hsl(h, s, l * 0.72f);               // accents need contrast on a light bar
        else if (white && s < 0.1f && l < 0.35f) grey(1 - l);       // dark grey tooltips and menus
        return D2D1_COLOR_F{r, g, b, al};
    }
    if (t.hue >= 0) {
        float h, s, l; ToHsl(r, g, b, h, s, l);
        if (h >= 185 && h <= 280 && s > 0.2f) {   // only the blues move: greens, ambers and greys stay
            h = t.hue + (h - 225) * 0.25f; if (h < 0) h += 360; if (h >= 360) h -= 360;
            if (t.soft) { s = std::min(s * 0.5f, 0.5f); l = l < 0.5f ? 0.52f + l * 0.22f : std::min(0.9f, l + 0.14f); }
            float q = l < .5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q, hh = h / 360;
            r = Hue2(p, q, hh + 1.f / 3); g = Hue2(p, q, hh); b = Hue2(p, q, hh - 1.f / 3);
        }
    }
    return D2D1_COLOR_F{r, g, b, a / 255.f};
}
D2D1_COLOR_F Col(uint32_t rgb, int a) { return ColFor(*g_theme, rgb, a); }
