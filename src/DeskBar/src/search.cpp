#include "pch.h"
#include "search.h"
#include <regex>
#include <shlwapi.h>

// ======================================================= scoring
ws Norm(const ws& s) {
    if (s.empty()) return {};
    int n = NormalizeString(NormalizationKD, s.c_str(), (int)s.size(), nullptr, 0); ws d(n > 0 ? n : 0, 0);
    if (n > 0) n = NormalizeString(NormalizationKD, s.c_str(), (int)s.size(), d.data(), n); if (n <= 0) d = s; else d.resize(n);
    ws r; r.reserve(d.size());
    for (wchar_t c : d) {
        WORD t = 0; GetStringTypeW(CT_CTYPE3, &c, 1, &t); if (t & C3_NONSPACING) continue;     // accents / Greek tonos
        if (iswalnum(c)) r += (wchar_t)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)c); else r += L' ';
    }
    for (auto& c : r) if (c == 0x3C2) c = 0x3C3;                                                    // final sigma
    return r;
}
static int Edit1(const ws& a, const ws& b) {
    if (std::abs((int)a.size() - (int)b.size()) > 1) return 2;
    size_t i = 0; while (i < a.size() && i < b.size() && a[i] == b[i]) i++;
    if (i == a.size() && i == b.size()) return 0;
    if (a.size() == b.size()) {
        if (a.compare(i + 1, ws::npos, b, i + 1, ws::npos) == 0) return 1;
        if (i + 1 < a.size() && a[i] == b[i + 1] && a[i + 1] == b[i] && a.compare(i + 2, ws::npos, b, i + 2, ws::npos) == 0) return 1; return 2;
    }
    const ws& lo = a.size() > b.size() ? a : b; const ws& sh = a.size() > b.size() ? b : a;
    return lo.compare(i + 1, ws::npos, sh, i, ws::npos) == 0 ? 1 : 2;
}
static double ScoreOne(const ws& t, const std::vector<ws>& words, const ws& tok) {
    if (t == tok) return 120;
    if (t.compare(0, tok.size(), tok) == 0) return 100 - std::min<double>(20, t.size() - tok.size()) * 0.3;
    for (auto& w : words) if (w.compare(0, tok.size(), tok) == 0) return 85;
    ws acr; for (auto& w : words) acr += w[0];
    if (tok.size() >= 2 && acr.compare(0, tok.size(), tok) == 0) return 75;
    if (t.find(tok) != ws::npos) return 55;
    if (tok.size() >= 5) for (auto& w : words) if (w.size() >= 4 && Edit1(w.size() > tok.size() ? w.substr(0, tok.size()) : w, tok) <= 1) return 40;   // a small typo still finds the app
    return 0;
}
static std::vector<ws> Words(const ws& s) { std::vector<ws> v; for (auto& w : Split(s, L' ')) if (!w.empty()) v.push_back(w); return v; }
double Score(const ws& text, const ws& q) {
    if (text.empty()) return 0;
    ws t = Trim(Norm(text)); auto toks = Words(Norm(q)); if (t.empty() || toks.empty()) return 0;
    auto words = Words(t); if (words.empty()) return 0;
    if (toks.size() == 1) return ScoreOne(t, words, toks[0]);
    double sum = 0; for (auto& tok : toks) { double s1 = ScoreOne(t, words, tok); if (s1 <= 0) return 0; sum += s1; }
    return std::min(119.0, sum / toks.size() + (t.compare(0, toks[0].size(), toks[0]) == 0 ? 8 : 0));
}
ws Shorten(const ws& p) { ws home = KnownFolder(FOLDERID_Profile); if (!home.empty() && StartsI(p, home)) return L"~" + p.substr(home.size()); return p; }
ws Ago(FILETIME when) {
    FILETIME now; GetSystemTimeAsFileTime(&now); ULARGE_INTEGER a, b; a.LowPart = now.dwLowDateTime; a.HighPart = now.dwHighDateTime; b.LowPart = when.dwLowDateTime; b.HighPart = when.dwHighDateTime;
    double mins = (double)(a.QuadPart - b.QuadPart) / 6e8;
    if (mins < 1) return L"Just now"; if (mins < 60) return Fmt(L"%dm ago", (int)mins); if (mins < 1440) return Fmt(L"%dh ago", (int)(mins / 60)); if (mins < 2880) return L"Yesterday";
    FILETIME lf; FileTimeToLocalFileTime(&when, &lf); SYSTEMTIME st; FileTimeToSystemTime(&lf, &st); wchar_t buf[64];
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, mins < 10080 ? L"dddd" : L"d MMM", buf, 64, nullptr); return buf;
}

// ======================================================= habits
namespace {
struct HEntry { ws q, kind, target; double count; long long last; };
std::mutex g_hmu; std::vector<HEntry> g_h; bool g_hLoaded = false;
long long NowDays100() { return (long long)(GetTickCount64() / 1000); }       // not wall clock: only used for decay between runs, so use FILETIME below
long long NowT() { FILETIME f; GetSystemTimeAsFileTime(&f); return (((long long)f.dwHighDateTime << 32) | f.dwLowDateTime); }
double Decay(long long last) { return pow(0.5, std::max(0.0, (NowT() - last) / 864000000000.0) / 30.0); }
void HLoad() {
    if (g_hLoaded) return; g_hLoaded = true; std::string raw; if (!ReadAll(DataPath(L"searchhabits.txt"), raw)) return;
    for (auto& line : Split(Widen(raw), L'\n')) { auto p = Split(Trim(line), L'\t'); if (p.size() == 5) g_h.push_back({p[0], p[1], p[2], _wtof(p[3].c_str()), _wtoi64(p[4].c_str())}); }
}
ws KindName(RKind k) { static const wchar_t* n[] = {L"App", L"Setting", L"File", L"Folder", L"Calc", L"Run", L"Program"}; return n[(int)k]; }
}
double HabitBoost(const ws& query, RKind kind, const ws& target) {
    ws q = Trim(Norm(query)); if (q.empty()) return 0; double sum = 0; std::lock_guard<std::mutex> l(g_hmu); HLoad(); ws kn = KindName(kind);
    for (auto& e : g_h) {
        if (e.target != target || e.kind != kn) continue;
        bool exact = e.q == q; if (!exact && !(q.size() >= 2 && (e.q.compare(0, q.size(), q) == 0 || q.compare(0, e.q.size(), e.q) == 0))) continue;
        sum += e.count * Decay(e.last) * (exact ? 1.0 : 0.5);
    }
    return std::min(160.0, sum * 45);   // two or three picks beat a close name match; it flattens out so one habit can't bury everything
}
void HabitRecord(const ws& query, RKind kind, const ws& target) {
    ws q = Trim(Norm(query)); if (q.empty() || target.empty() || kind == RKind::Calc) return;
    std::lock_guard<std::mutex> l(g_hmu); HLoad(); ws kn = KindName(kind);
    auto it = std::find_if(g_h.begin(), g_h.end(), [&](const HEntry& e) { return e.q == q && e.kind == kn && e.target == target; });
    if (it == g_h.end()) { g_h.push_back({q, kn, target, 0, 0}); it = g_h.end() - 1; }
    it->count = it->count * Decay(it->last ? it->last : NowT()) + 1; it->last = NowT();
    std::erase_if(g_h, [](const HEntry& e) { return e.count * Decay(e.last) < 0.05; });
    if (g_h.size() > 1500) g_h.erase(g_h.begin(), g_h.begin() + (g_h.size() - 1500));
    std::string out; for (auto& e : g_h) out += Narrow(e.q + L"\t" + e.kind + L"\t" + e.target + L"\t" + Fmt(L"%.3f", e.count) + L"\t" + std::to_wstring(e.last) + L"\n");
    ws path = DataPath(L"searchhabits.txt"); std::thread([path, out] { WriteAtomic(path, out); }).detach();
}
void RecordLaunch(const ws& id) { S.launchCounts[id]++; SaveSettingsSoon(); }

// ======================================================= calculator + units
namespace {
struct Calc {
    const ws& s; size_t p = 0; bool bad = false;
    double Expr() { double v = Term(); while (p < s.size() && (s[p] == L'+' || s[p] == L'-')) { wchar_t o = s[p++]; double r = Term(); v = o == L'+' ? v + r : v - r; } return v; }
    double Term() {
        double v = Pow();
        while (p < s.size() && (s[p] == L'*' || s[p] == L'/' || s[p] == L'%')) {
            wchar_t o = s[p++];
            if (o == L'%' && (p >= s.size() || wcschr(L"+-*/)", s[p]))) { v /= 100; continue; }   // "50%" on its own = 0.5
            double r = Pow(); v = o == L'*' ? v * r : o == L'/' ? v / r : fmod(v, r);
        }
        return v;
    }
    double Pow() { double b = Un(); if (p < s.size() && s[p] == L'^') { p++; return pow(b, Pow()); } return b; }
    double Un() { if (p < s.size() && s[p] == L'-') { p++; return -Un(); } if (p < s.size() && s[p] == L'+') { p++; return Un(); } return Atom(); }
    double Atom() {
        if (p < s.size() && s[p] == L'(') { p++; double v = Expr(); if (p < s.size() && s[p] == L')') p++; return v; }
        size_t st = p;
        if (p < s.size() && iswalpha(s[p])) {
            while (p < s.size() && iswalpha(s[p])) p++; ws n = Lower(s.substr(st, p - st));
            if (n == L"pi") return 3.14159265358979323846; if (n == L"e") return 2.71828182845904523536;
            double a = Atom(), d = 3.14159265358979323846 / 180;
            if (n == L"sqrt") return sqrt(a); if (n == L"sin") return sin(a * d); if (n == L"cos") return cos(a * d); if (n == L"tan") return tan(a * d);
            if (n == L"ln") return log(a); if (n == L"log") return log10(a); if (n == L"abs") return fabs(a); bad = true; return 0;
        }
        while (p < s.size() && (iswdigit(s[p]) || s[p] == L'.')) p++;
        if (st == p) { bad = true; return 0; } return _wtof(s.substr(st, p - st).c_str());
    }
};
}
static ws FormatNum(double v, const char* fmt) { char b[64]; sprintf_s(b, fmt, v); std::string t = b; if (t.find('.') != std::string::npos && t.find('e') == std::string::npos) { while (t.back() == '0') t.pop_back(); if (t.back() == '.') t.pop_back(); } return Widen(t); }
static ws TryCalc(const ws& q0) {
    ws s; for (wchar_t c : q0) { if (c == L' ') continue; s += c == 0xD7 ? L'*' : c == 0xF7 ? L'/' : c == L',' ? L'.' : c; }
    if (!s.empty() && s[0] == L'=') s = s.substr(1);
    if (s.size() < 2 || std::none_of(s.begin(), s.end(), iswdigit)) return {};
    if (std::none_of(s.begin(), s.end(), [](wchar_t c) { return wcschr(L"+-*/^%(", c) != nullptr; }) && s.find(L"sqrt") == ws::npos && s.find(L"pi") == ws::npos) return {};
    for (wchar_t c : s) if (!(iswalnum(c) || wcschr(L"+-*/^%().", c))) return {};
    Calc c{s}; double v = c.Expr(); if (c.bad || c.p != s.size() || std::isnan(v) || std::isinf(v)) return {};
    return FormatNum(round(v * 1e10) / 1e10, "%.12g");
}
static ws TryConvert(const ws& q) {
    struct U { const wchar_t* kind; double f; const wchar_t* label; };
    static const std::map<ws, U> units = {
        {L"mm", {L"len", 0.001, L"mm"}}, {L"cm", {L"len", 0.01, L"cm"}}, {L"m", {L"len", 1, L"m"}}, {L"km", {L"len", 1000, L"km"}}, {L"in", {L"len", 0.0254, L"in"}}, {L"inch", {L"len", 0.0254, L"in"}}, {L"inches", {L"len", 0.0254, L"in"}},
        {L"ft", {L"len", 0.3048, L"ft"}}, {L"foot", {L"len", 0.3048, L"ft"}}, {L"feet", {L"len", 0.3048, L"ft"}}, {L"yd", {L"len", 0.9144, L"yd"}}, {L"mi", {L"len", 1609.344, L"mi"}}, {L"mile", {L"len", 1609.344, L"mi"}}, {L"miles", {L"len", 1609.344, L"mi"}},
        {L"mg", {L"mass", 0.001, L"mg"}}, {L"g", {L"mass", 1, L"g"}}, {L"kg", {L"mass", 1000, L"kg"}}, {L"t", {L"mass", 1e6, L"t"}}, {L"lb", {L"mass", 453.59237, L"lb"}}, {L"lbs", {L"mass", 453.59237, L"lb"}}, {L"oz", {L"mass", 28.349523125, L"oz"}}, {L"st", {L"mass", 6350.29318, L"st"}},
        {L"b", {L"data", 1, L"B"}}, {L"kb", {L"data", 1024, L"KB"}}, {L"mb", {L"data", 1048576, L"MB"}}, {L"gb", {L"data", 1073741824.0, L"GB"}}, {L"tb", {L"data", 1099511627776.0, L"TB"}},
        {L"ms", {L"time", 0.001, L"ms"}}, {L"s", {L"time", 1, L"s"}}, {L"sec", {L"time", 1, L"s"}}, {L"min", {L"time", 60, L"min"}}, {L"h", {L"time", 3600, L"h"}}, {L"hr", {L"time", 3600, L"h"}}, {L"hours", {L"time", 3600, L"h"}},
        {L"day", {L"time", 86400, L"days"}}, {L"days", {L"time", 86400, L"days"}}, {L"week", {L"time", 604800, L"weeks"}}, {L"weeks", {L"time", 604800, L"weeks"}},
        {L"ml", {L"vol", 0.001, L"ml"}}, {L"l", {L"vol", 1, L"L"}}, {L"gal", {L"vol", 3.785411784, L"gal"}}, {L"cup", {L"vol", 0.2365882365, L"cups"}}, {L"cups", {L"vol", 0.2365882365, L"cups"}},
        {L"kmh", {L"speed", 1 / 3.6, L"km/h"}}, {L"km/h", {L"speed", 1 / 3.6, L"km/h"}}, {L"mph", {L"speed", 0.44704, L"mph"}}, {L"m/s", {L"speed", 1, L"m/s"}}};
    static const std::wregex re(LR"(^\s*(-?[\d.,]+)\s*(°?[a-z/]+)\s+(?:to|in|=|->)\s+(°?[a-z/]+)\s*$)", std::regex::icase);
    std::wsmatch m; if (!std::regex_match(q, m, re)) return {};
    double v = _wtof([&] { ws t = m[1].str(); for (auto& c : t) if (c == L',') c = L'.'; return t; }().c_str());
    auto strip = [](ws s) { if (!s.empty() && s[0] == 0xB0) s = s.substr(1); return Lower(s); };
    ws from = strip(m[2].str()), to = strip(m[3].str());
    auto T = [](const ws& u) -> wchar_t { return (u == L"c" || u == L"celsius") ? L'C' : (u == L"f" || u == L"fahrenheit") ? L'F' : (u == L"k" || u == L"kelvin") ? L'K' : 0; };
    if (T(from) && T(to)) { double c = T(from) == L'C' ? v : T(from) == L'F' ? (v - 32) * 5 / 9 : v - 273.15; double r = T(to) == L'C' ? c : T(to) == L'F' ? c * 9 / 5 + 32 : c + 273.15; return FormatNum(round(r * 1e4) / 1e4, "%.4f") + L" °" + T(to); }
    auto a = units.find(from), b = units.find(to); if (a == units.end() || b == units.end() || wcscmp(a->second.kind, b->second.kind)) return {};
    double r = v * a->second.f / b->second.f; bool sci = fabs(r) >= 1e6 || (fabs(r) < 0.001 && r != 0);
    return (sci ? FormatNum(r, "%.6g") : FormatNum(round(r * 1e4) / 1e4, "%.4f")) + L" " + b->second.label;
}

// ======================================================= Run box
ws ExpandPath(const ws& q) {
    ws s = Trim(q); if (!s.empty() && s.front() == L'"') s.erase(0, 1); if (!s.empty() && s.back() == L'"') s.pop_back();
    if (!s.empty() && (s[0] == L'/' || (s[0] == L'\\' && !(s.size() > 1 && s[1] == L'\\')))) s.erase(0, s.find_first_not_of(L"/\\") == ws::npos ? s.size() : s.find_first_not_of(L"/\\"));
    s = ExpandEnv(s);
    if (s == L"~" || StartsI(s, L"~\\") || StartsI(s, L"~/")) s = KnownFolder(FOLDERID_Profile) + s.substr(1);
    if (s.size() >= 2 && iswalpha(s[0]) && s[1] == L':' && (s.size() == 2 || (s[2] != L'\\' && s[2] != L'/'))) s = s.substr(0, 2) + L"\\" + s.substr(2);
    for (auto& c : s) if (c == L'/') c = L'\\'; return s;
}
ws ResolveCommand(const ws& cmd) {
    if (cmd.size() < 2 || cmd.find_first_of(L"<>:\"|?*\\/") != ws::npos) return {};
    std::vector<ws> names; if (!ExtOf(cmd).empty()) names.push_back(cmd); else for (auto e : {L".exe", L".msc", L".cpl", L".bat", L".cmd", L".com", L".lnk"}) names.push_back(cmd + e);
    for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) for (auto& n : names) {
        wchar_t v[1024]; DWORD sz = sizeof v; ws key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\" + n;
        if (RegGetValueW(hive, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, v, &sz) == 0) { ws p = v; if (!p.empty() && p.front() == L'"') p = Trim(p.substr(1, p.find(L'"', 1) - 1)); if (FileExists(p)) return p; }
    }
    wchar_t pathEnv[32768]; GetEnvironmentVariableW(L"PATH", pathEnv, 32768); std::vector<ws> dirs{KnownFolder(FOLDERID_Windows), KnownFolder(FOLDERID_System)};
    for (auto& d : Split(pathEnv, L';')) if (!d.empty()) dirs.push_back(ExpandEnv(Trim(d)));
    for (auto& d : dirs) for (auto& n : names) { ws full = d + L"\\" + n; if (FileExists(full)) return full; }
    return {};
}
static std::vector<Result> RunAndPaths(const ws& q) {
    std::vector<Result> list; ws raw = Trim(q); if (raw.empty()) return list;
    if (StartsI(raw, L"shell:") || StartsI(raw, L"ms-settings:") || raw.find(L"://") != ws::npos) { list.push_back({RKind::Run, raw, L"Run", raw, 960}); return list; }
    ws path = ExpandPath(raw); bool looks = path.find(L'\\') != ws::npos || raw.find(L'%') != ws::npos || raw[0] == L'~';
    if (looks) {
        if (DirExists(path)) { ws n = FileNameOf(path.back() == L'\\' ? path.substr(0, path.size() - 1) : path); list.push_back({RKind::Folder, n.empty() ? path : n, Shorten(path), path, 980}); }
        else if (FileExists(path)) list.push_back({RKind::File, FileNameOf(path), Shorten(DirOf(path)), path, 980});
        ws dir = path.back() == L'\\' ? path : DirOf(path), part = path.back() == L'\\' ? ws() : FileNameOf(path);
        if (!dir.empty() && DirExists(dir)) {
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + L"\\" + part + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0); int i = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
                    ws full = dir + L"\\" + fd.cFileName; if (EqI(full, path)) continue; bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
                    list.push_back({isDir ? RKind::Folder : RKind::File, fd.cFileName, Shorten(dir), full, 900.0 - i}); if (++i >= 8) break;
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
        bool relative = raw.size() < 2 || (raw[1] != L':' && raw[0] != L'\\' && raw[0] != L'%' && raw[0] != L'~' && raw[0] != L'/');
        if (relative && list.empty()) {   // "Dexplorer\out\Dexplorer.exe": a path that starts somewhere inside a usual folder. Try it under Desktop, Documents, Downloads, your profile, and one folder level below each
            ws rel = raw; while (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/')) rel.erase(0, 1); for (auto& c : rel) if (c == L'/') c = L'\\';
            const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Documents, &FOLDERID_Downloads, &FOLDERID_Profile};
            std::vector<ws> bases, deeper; for (auto* id : ids) bases.push_back(KnownFolder(*id)); deeper = bases;
            for (auto& b : bases) {
                WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((b + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0); int n = 0; if (h == INVALID_HANDLE_VALUE) continue;
                do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue; deeper.push_back(b + L"\\" + fd.cFileName); } while (++n < 300 && FindNextFileW(h, &fd));
                FindClose(h);
            }
            std::set<ws> seen;
            for (auto& b : deeper) {
                ws full = b + L"\\" + rel; if (!seen.insert(Lower(full)).second) continue;
                if (DirExists(full)) list.push_back({RKind::Folder, FileNameOf(full), Shorten(DirOf(full)), full, 970.0 - (double)list.size()});
                else if (FileExists(full)) list.push_back({RKind::File, FileNameOf(full), Shorten(DirOf(full)), full, 970.0 - (double)list.size()});
                if (list.size() >= 6) break;
            }
        }
        return list;
    }
    ws exe = ResolveCommand(raw.substr(0, raw.find(L' '))); if (!exe.empty()) list.push_back({RKind::Run, raw, L"Run command · " + Shorten(exe), raw, 940, exe});
    return list;
}
static std::vector<std::pair<ws, ws>> g_roots; static uint64_t g_rootsAt = 0;
static std::vector<Result> TopLevelFolders(const ws& q) {
    std::vector<Result> list; if (q.size() < 2 || q.find_first_of(L"\\/") != ws::npos) return list;
    if (g_roots.empty() || Now() - g_rootsAt > 300000) {
        g_roots.clear(); g_rootsAt = Now();
        auto add = [&](const ws& dir, bool skipHidden) {
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0); if (h == INVALID_HANDLE_VALUE) return;
            do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                 if (fd.dwFileAttributes & (skipHidden ? (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) : FILE_ATTRIBUTE_HIDDEN)) continue; g_roots.push_back({fd.cFileName, dir + L"\\" + fd.cFileName}); } while (FindNextFileW(h, &fd));
            FindClose(h);
        };
        wchar_t drives[256]; GetLogicalDriveStringsW(256, drives);
        for (wchar_t* d = drives; *d; d += wcslen(d) + 1) if (GetDriveTypeW(d) == DRIVE_FIXED) { ws r = d; r.pop_back(); add(r, true); }
        add(KnownFolder(FOLDERID_Profile), false);
        g_roots.push_back({L"AppData", KnownFolder(FOLDERID_RoamingAppData)}); g_roots.push_back({L"Local AppData", KnownFolder(FOLDERID_LocalAppData)});
        g_roots.push_back({L"Startup", KnownFolder(FOLDERID_Startup)});
    }
    for (auto& r : g_roots) { double s = Score(r.first, q); if (s >= 85) list.push_back({RKind::Folder, r.first, Shorten(r.second), r.second, s * 0.75}); }
    return list;
}

// ======================================================= programs (classic installs without a Start entry)
struct Prog { ws name, target; bool folder; };
static std::vector<Prog> g_progs; static std::mutex g_pmu;
void ProgramsLoadAsync() {
    std::thread([] {
        std::vector<Prog> list;
        for (auto [hive, view] : {std::pair<HKEY, REGSAM>{HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, 0}}) {
            HKEY root; if (RegOpenKeyExW(hive, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &root)) continue;
            wchar_t name[256]; for (DWORD i = 0;; i++) {
                DWORD nl = 256; if (RegEnumKeyExW(root, i, name, &nl, nullptr, nullptr, nullptr, nullptr)) break;
                HKEY k; if (RegOpenKeyExW(root, name, 0, KEY_READ | view, &k)) continue;
                auto str = [&](const wchar_t* v) { wchar_t b[1024]; DWORD sz = sizeof b; return RegGetValueW(k, nullptr, v, RRF_RT_REG_SZ, nullptr, b, &sz) == 0 ? ws(b) : ws(); };
                DWORD sc = 0, sz = 4; RegGetValueW(k, nullptr, L"SystemComponent", RRF_RT_DWORD, nullptr, &sc, &sz);
                ws disp = str(L"DisplayName"); bool parent = !str(L"ParentKeyName").empty();
                if (!Trim(disp).empty() && !sc && !parent && !StartsI(disp, L"Update for") && !StartsI(disp, L"Security Update") && !StartsI(disp, L"Microsoft Visual C++")) {
                    ws icon = Trim(str(L"DisplayIcon")); if (!icon.empty() && icon[0] == L'"') icon = icon.substr(1, icon.find(L'"', 1) - 1); size_t c = icon.rfind(L','); if (c != ws::npos && c > 3) icon = icon.substr(0, c);
                    ws loc = Trim(str(L"InstallLocation")); if (!loc.empty() && loc[0] == L'"') loc = loc.substr(1, loc.find(L'"', 1) - 1);
                    if (ExtOf(icon) == L".exe" && FileExists(icon) && !ContainsI(icon, L"unins")) list.push_back({disp, icon, false});
                    else if (!loc.empty() && DirExists(loc)) list.push_back({disp, loc, true});
                }
                RegCloseKey(k);
            }
            RegCloseKey(root);
        }
        std::vector<Prog> uniq; for (auto& p : list) if (std::none_of(uniq.begin(), uniq.end(), [&](const Prog& u) { return EqI(u.name, p.name); })) uniq.push_back(p);
        { std::lock_guard<std::mutex> l(g_pmu); g_progs = uniq; } Log(L"installed programs: %d", (int)uniq.size());
    }).detach();
}

// ======================================================= settings pages
static const struct { const wchar_t *name, *uri, *kw; } kSettings[] = {
    {L"Device Manager", L"devmgmt.msc", L"hardware drivers devices"}, {L"Disk Management", L"diskmgmt.msc", L"partition volume drive format"}, {L"Services", L"services.msc", L"windows services background"},
    {L"Event Viewer", L"eventvwr.msc", L"logs errors"}, {L"Registry Editor", L"regedit", L"registry regedit"}, {L"Task Scheduler", L"taskschd.msc", L"scheduled tasks"}, {L"Computer Management", L"compmgmt.msc", L"admin tools"},
    {L"System Configuration", L"msconfig", L"startup boot msconfig"}, {L"Resource Monitor", L"resmon", L"cpu disk network memory performance"}, {L"Performance Monitor", L"perfmon", L"counters"}, {L"Task Manager", L"taskmgr", L"processes end task"},
    {L"Control Panel", L"control", L"classic"}, {L"Programs and Features", L"appwiz.cpl", L"uninstall remove programs"}, {L"Network Connections", L"ncpa.cpl", L"adapters ethernet wifi"}, {L"Power Options", L"powercfg.cpl", L"sleep battery plan"},
    {L"System Properties", L"sysdm.cpl", L"advanced computer name"}, {L"Environment Variables", L"rundll32 sysdm.cpl,EditEnvironmentVariables", L"path variables env"}, {L"Windows Features", L"optionalfeatures", L"turn windows features on off hyper-v wsl"},
    {L"Sound control panel", L"mmsys.cpl", L"playback recording devices"}, {L"Date and Time", L"timedate.cpl", L"clock timezone"}, {L"Mouse properties", L"main.cpl", L"pointer buttons"}, {L"Firewall", L"firewall.cpl", L"windows defender firewall"},
    {L"Disk Cleanup", L"cleanmgr", L"free space temporary files"}, {L"Command Prompt", L"cmd", L"terminal cmd"}, {L"Windows PowerShell", L"powershell", L"terminal shell"}, {L"Remote Desktop Connection", L"mstsc", L"rdp"},
    {L"System Information", L"msinfo32", L"specs hardware"}, {L"Character Map", L"charmap", L"symbols"}, {L"Magnifier", L"magnify", L"zoom screen"}, {L"On-Screen Keyboard", L"osk", L"touch keyboard"},
    {L"Settings", L"ms-settings:", L"control panel preferences"}, {L"Display", L"ms-settings:display", L"screen resolution scale brightness monitor hdr night"}, {L"Night light", L"ms-settings:nightlight", L"blue light warm"},
    {L"Sound", L"ms-settings:sound", L"audio volume speakers microphone output input"}, {L"Volume mixer", L"ms-settings:apps-volume", L"app volume audio"}, {L"Notifications", L"ms-settings:notifications", L"alerts do not disturb focus"},
    {L"Focus", L"ms-settings:quiethours", L"do not disturb quiet"}, {L"Power & battery", L"ms-settings:powersleep", L"battery sleep energy saver power mode"}, {L"Energy saver", L"ms-settings:batterysaver", L"battery saver"},
    {L"Storage", L"ms-settings:storagesense", L"disk space cleanup"}, {L"Multitasking", L"ms-settings:multitasking", L"snap windows alt tab desktops"}, {L"Clipboard", L"ms-settings:clipboard", L"copy paste history"},
    {L"About this PC", L"ms-settings:about", L"system info specs rename pc device name"}, {L"Bluetooth & devices", L"ms-settings:bluetooth", L"pair headphones mouse keyboard"}, {L"Printers & scanners", L"ms-settings:printers", L"print"},
    {L"Mouse", L"ms-settings:mousetouchpad", L"pointer speed scroll"}, {L"Touchpad", L"ms-settings:devices-touchpad", L"gestures trackpad"}, {L"Pen & Windows Ink", L"ms-settings:pen", L"stylus surface pen"}, {L"Cameras", L"ms-settings:camera", L"webcam studio effects"},
    {L"Wi-Fi", L"ms-settings:network-wifi", L"wireless internet network"}, {L"Network & internet", L"ms-settings:network", L"ethernet internet proxy"}, {L"VPN", L"ms-settings:network-vpn", L"vpn"},
    {L"Mobile hotspot", L"ms-settings:network-mobilehotspot", L"tethering share internet"}, {L"Airplane mode", L"ms-settings:network-airplanemode", L"flight"}, {L"Personalization", L"ms-settings:personalization", L"theme"},
    {L"Background", L"ms-settings:personalization-background", L"wallpaper"}, {L"Colors", L"ms-settings:colors", L"accent dark mode light mode transparency"}, {L"Themes", L"ms-settings:themes", L"theme"}, {L"Lock screen", L"ms-settings:lockscreen", L"lock"},
    {L"Fonts", L"ms-settings:fonts", L"font"}, {L"Taskbar", L"ms-settings:taskbar", L"tray"}, {L"Installed apps", L"ms-settings:appsfeatures", L"uninstall programs remove"}, {L"Default apps", L"ms-settings:defaultapps", L"open with browser"},
    {L"Startup apps", L"ms-settings:startupapps", L"startup boot"}, {L"Your info", L"ms-settings:yourinfo", L"account profile picture"}, {L"Sign-in options", L"ms-settings:signinoptions", L"password pin windows hello fingerprint face"},
    {L"Date & time", L"ms-settings:dateandtime", L"clock timezone time zone"}, {L"Language & region", L"ms-settings:regionlanguage", L"keyboard input greek english"}, {L"Typing", L"ms-settings:typing", L"autocorrect touch keyboard"},
    {L"Gaming", L"ms-settings:gaming-gamebar", L"game bar xbox"}, {L"Game Mode", L"ms-settings:gaming-gamemode", L"performance"}, {L"Accessibility", L"ms-settings:easeofaccess", L"narrator magnifier"}, {L"Text size", L"ms-settings:easeofaccess-display", L"bigger font"},
    {L"Privacy & security", L"ms-settings:privacy", L"permissions"}, {L"Location", L"ms-settings:privacy-location", L"gps"}, {L"Windows Security", L"windowsdefender:", L"antivirus defender firewall virus"},
    {L"Windows Update", L"ms-settings:windowsupdate", L"updates upgrade"}, {L"Recovery", L"ms-settings:recovery", L"reset restore"}, {L"Graphics", L"ms-settings:display-advancedgraphics", L"gpu"}, {L"Advanced display", L"ms-settings:display-advanced", L"refresh rate hz"},
};

// ======================================================= catalog of installed apps (shell:AppsFolder)
static std::vector<CatApp> g_apps; static std::mutex g_amu;
const std::vector<CatApp>& CatalogApps() { return g_apps; }
const CatApp* CatalogFindByName(const ws& n) { for (auto& a : g_apps) if (EqI(a.name, n)) return &a; return nullptr; }
const CatApp* CatalogFindById(const ws& id) { for (auto& a : g_apps) if (EqI(a.id, id)) return &a; return nullptr; }
static bool IsJunk(const ws& name) {
    ws n = Trim(name);
    for (auto j : {L"readme", L"release notes", L"documentation", L"website", L"license"}) if (ContainsI(n, j)) return true;
    if (StartsI(n, L"Uninstall ") || (n.size() > 10 && EqI(n.substr(n.size() - 10), L" Uninstall")) || EqI(n, L"Uninstall")) return true;
    if (EqI(n, L"Help") || (n.size() > 5 && EqI(n.substr(n.size() - 5), L" Help")) || StartsI(n, L"Help ")) return true;
    return false;
}
void CatalogLoad() {
    for (ws f : {DataPath(L"apps.json"), KnownFolder(FOLDERID_LocalAppData) + L"\\DeskBar2\\apps.json", KnownFolder(FOLDERID_LocalAppData) + L"\\DeskBar\\apps.json"}) {
        std::string raw; if (!ReadAll(f, raw)) continue; Json j = Json::Parse(raw); if (j.t != Json::Arr) continue;
        std::vector<CatApp> v; for (auto& e : j.a) { auto* n = e.Find(L"Name"); auto* i = e.Find(L"Id"); if (!n) n = e.Find(L"n"); if (!i) i = e.Find(L"i"); if (n && i) v.push_back({n->s, i->s}); }
        if (!v.empty()) { g_apps = v; return; }
    }
}
void CatalogRefresh(std::function<void()> onUpdated) {
    std::thread([onUpdated] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::vector<CatApp> v;
        ComPtr<IShellItem> root;
        if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&root)))) {
            ComPtr<IEnumShellItems> en;
            if (SUCCEEDED(root->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) {
                for (;;) {
                    ComPtr<IShellItem> it; ULONG got = 0; if (en->Next(1, &it, &got) != S_OK || !got) break;
                    PWSTR a = nullptr, b = nullptr; ws name, id;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &a)) && a) name = a;
                    if (SUCCEEDED(it->GetDisplayName((SIGDN)0x80018001, &b)) && b) id = b;   // PARENTRELATIVEPARSING: the id Start pins and launching use
                    CoTaskMemFree(a); CoTaskMemFree(b);
                    if (name.empty() || id.empty() || IsJunk(name) || StartsI(id, L"http")) continue;
                    if (std::any_of(v.begin(), v.end(), [&](const CatApp& x) { return EqI(x.id, id); })) continue;
                    v.push_back({name, id});
                }
            }
        }
        if (v.empty()) { CoUninitialize(); return; }
        std::sort(v.begin(), v.end(), [](const CatApp& a, const CatApp& b) { return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE, a.name.c_str(), -1, b.name.c_str(), -1, nullptr, nullptr, 0) == CSTR_LESS_THAN; });
        Json arr = Json::A(); for (auto& a : v) { Json o = Json::O(); o.Set(L"Name", Json::S(a.name)); o.Set(L"Id", Json::S(a.id)); arr.a.push_back(o); }
        WriteAtomic(DataPath(L"apps.json"), arr.Dump(0)); Log(L"app catalog: %d apps", (int)v.size());
        UiPost([v, onUpdated] { g_apps = v; if (onUpdated) onUpdated(); });
        CoUninitialize();
    }).detach();
}

// ======================================================= recent files
ws FriendlyAppFor(const ws& ext) {
    static std::map<ws, ws> cache; static std::mutex mu; std::lock_guard<std::mutex> l(mu);
    auto it = cache.find(ext); if (it != cache.end()) return it->second;
    wchar_t b[520]; DWORD n = 520; ws r = SUCCEEDED(AssocQueryStringW(0, ASSOCSTR_FRIENDLYAPPNAME, ext.c_str(), nullptr, b, &n)) ? ws(b) : (ext.size() > 1 ? ext.substr(1) : ext) + L" file";
    if (StartsI(r, L"Microsoft ") && r.size() > 10) r = r.substr(10);   // keep group names short: "Microsoft Word" -> "Word"
    return cache[ext] = r;
}
ws ExecutableFor(const ws& ext) {
    static std::map<ws, ws> cache; static std::mutex mu; { std::lock_guard<std::mutex> l(mu); auto it = cache.find(ext); if (it != cache.end()) return it->second; }
    wchar_t b[520]; DWORD n = 520; ws r = SUCCEEDED(AssocQueryStringW(0, ASSOCSTR_EXECUTABLE, ext.c_str(), nullptr, b, &n)) ? ws(b) : ws(); std::lock_guard<std::mutex> l(mu); return cache[ext] = r;
}
std::vector<RecentItem> RecentLoad(int max) {
    std::vector<RecentItem> out; ws dir = KnownFolder(FOLDERID_Recent); struct L { ws path; FILETIME t; }; std::vector<L> links;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + L"\\*.lnk").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) { do links.push_back({dir + L"\\" + fd.cFileName, fd.ftLastWriteTime}); while (FindNextFileW(h, &fd)); FindClose(h); }
    std::sort(links.begin(), links.end(), [](const L& a, const L& b) { return CompareFileTime(&a.t, &b.t) > 0; });
    ComPtr<IShellLinkW> sl; CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)); ComPtr<IPersistFile> pf; if (sl) sl.As(&pf);
    std::set<ws> seen; int tried = 0;
    for (auto& l : links) {
        if (++tried > max * 3 || (int)out.size() >= max || !pf) break;
        if (FAILED(pf->Load(l.path.c_str(), STGM_READ))) continue;
        wchar_t t[1024] = {}; sl->GetPath(t, 1024, nullptr, SLGP_RAWPATH); ws target = ExpandEnv(t);
        if (target.empty() || !seen.insert(Lower(target)).second || target.compare(0, 2, L"::") == 0 || !FileExists(target)) continue;
        ws ext = ExtOf(target); if (ext == L".lnk" || ext == L".exe") continue;
        out.push_back({target, FileNameOf(target), FriendlyAppFor(ext), ext, l.t});
    }
    return out;
}

std::vector<RecentItem> RecentCached() {
    static std::mutex mu; static std::vector<RecentItem> cache; static bool started = false; std::lock_guard<std::mutex> l(mu);
    if (!started) { started = true; std::thread([] { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); for (;;) { auto v = RecentLoad(80); { std::lock_guard<std::mutex> l2(mu); cache = v; } Sleep(30000); } }).detach(); }
    return cache;
}

// ======================================================= SearchLocal
std::vector<Result> SearchLocal(const ws& query) {
    ws q = Trim(query); std::vector<Result> r; if (q.empty()) return r;
    if (ws c = TryCalc(q); !c.empty()) r.push_back({RKind::Calc, L"= " + c, q, c, 1000});
    if (ws c = TryConvert(q); !c.empty()) r.push_back({RKind::Calc, L"= " + c, q, c.substr(0, c.find(L' ')), 1000});
    for (auto& x : RunAndPaths(q)) r.push_back(x);
    for (auto& x : TopLevelFolders(q)) r.push_back(x);
    for (auto& a : g_apps) {
        double s = Score(a.name, q); if (s <= 0) continue;
        auto it = S.launchCounts.find(a.id); if (it != S.launchCounts.end()) s += std::min(40, it->second * 4);
        r.push_back({RKind::App, a.name, L"App", L"shell:AppsFolder\\" + a.id, s, a.id});
    }
    { std::lock_guard<std::mutex> l(g_pmu);
      for (auto& pr : g_progs) {
          double s = Score(pr.name, q); if (s <= 0) continue;
          bool dup = false; for (auto& a : g_apps) if (EqI(a.name, pr.name) || (a.name.size() > 4 && StartsI(pr.name, a.name))) { dup = true; break; }
          if (!dup) r.push_back({RKind::Program, pr.name, pr.folder ? L"Program folder" : L"Program", pr.target, s * 0.9});
      } }
    for (auto& st : kSettings) {
        double s = Score(st.name, q); for (auto& k : Words(st.kw)) s = std::max(s, Score(k, q) * 0.8);
        if (s > 0) r.push_back({RKind::Setting, st.name, L"Settings", st.uri, s * 0.85});
    }
    for (auto& x : r) if (x.kind != RKind::Calc) x.score += HabitBoost(q, x.kind, x.target);
    std::stable_sort(r.begin(), r.end(), [](const Result& a, const Result& b) { return a.score > b.score; });
    if (r.size() > 30) r.resize(30); return r;
}
