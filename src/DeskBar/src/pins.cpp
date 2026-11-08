#include "pch.h"
#include "app.h"
#include <regex>

// Taskbar pins: entry is a .lnk path, "app:<AUMID>" or an exe path. Resolved once, cached in pins.cache (tab separated), re-read only when the .lnk changes.
namespace {
std::map<ws, std::pair<uint64_t, PinInfo>> g_pc; bool g_loaded = false;      // entry -> (lnk last-write, info)

uint64_t Stamp(const ws& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{}; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}
void Load() {
    if (g_loaded) return; g_loaded = true;
    std::string raw; if (!ReadAll(DataPath(L"pins.cache"), raw)) return;
    for (auto& line : Split(Widen(raw), L'\n')) {
        auto f = Split(line, L'\t'); if (f.size() < 8) continue;
        PinInfo p; p.entry = f[0]; p.name = f[2]; p.exe = f[3]; p.aumid = f[4]; p.family = f[5]; p.launch = f[6]; p.iconKey = Trim(f[7]); p.ok = true;
        g_pc[p.entry] = {_wcstoui64(f[1].c_str(), nullptr, 10), p};
    }
}
ws FamilyOfAumid(const ws& a) { size_t i = a.find(L'!'); return i == ws::npos ? ws() : a.substr(0, i); }
PinInfo Build0(const ws& e);
PinInfo Build(const ws& e) {
    PinInfo p0 = Build0(e); if (p0.ok && !p0.exe.empty()) { ws x = FollowAppUpdate(p0.exe); if (x != p0.exe) { p0.exe = x; p0.launch = x; p0.iconKey = L"exe:" + x; } }
    return p0;
}
PinInfo Build0(const ws& e) {
    PinInfo p; p.entry = e;
    if (StartsI(e, L"app:")) {
        p.aumid = e.substr(4); p.family = FamilyOfAumid(p.aumid); p.launch = L"shell:AppsFolder\\" + p.aumid; p.iconKey = L"aum:" + p.aumid;
        p.name = ShellNameOf(p.iconKey); if (p.name.empty()) p.name = p.aumid; p.ok = true; return p;
    }
    if (ExtOf(e) == L".lnk") {
        p.launch = e; p.name = StemOf(e);
        ComPtr<IShellLinkW> sl;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) {
            ComPtr<IPersistFile> pf;
            if (SUCCEEDED(sl.As(&pf)) && SUCCEEDED(pf->Load(e.c_str(), STGM_READ))) {
                wchar_t t[1024] = {}; sl->GetPath(t, 1024, nullptr, SLGP_RAWPATH); p.exe = ExpandEnv(t);
                p.aumid = AumidOfShortcut(e);
            }
        }
        ws exeName = Lower(FileNameOf(p.exe));
        bool shellHost = p.exe.empty() || exeName == L"explorer.exe" || exeName == L"applicationframehost.exe";
        if (!p.aumid.empty() && shellHost) { p.iconKey = L"aum:" + p.aumid; p.exe.clear(); }
        else if (!p.exe.empty()) p.iconKey = L"exe:" + p.exe;
        else p.iconKey = L"file:" + e;
        p.family = !p.exe.empty() ? PackageFamilyFromPath(p.exe) : ws();
        if (p.family.empty()) p.family = FamilyOfAumid(p.aumid);
        p.ok = true; return p;
    }
    p.exe = e; p.launch = e; p.name = StemOf(e); p.iconKey = L"exe:" + e; p.family = PackageFamilyFromPath(e); p.ok = true; return p;
}
}
// Squirrel apps (Discord, Slack...) keep their exe in a versioned folder, "...\app-1.0.9266\Discord.exe", that every update replaces: a pin to the old path points at
// nothing (no icon, "launch failed"). When the file is gone, use the same file in the newest app-* folder next to it.
ws FollowAppUpdate(const ws& p) {
    if (FileExists(p)) return p;
    size_t i = Lower(p).rfind(L"\\app-"); if (i == ws::npos) return p;
    size_t j = p.find(L'\\', i + 1); if (j == ws::npos) return p;
    ws root = p.substr(0, i), rest = p.substr(j), best; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((root + L"\\app-*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && FileExists(root + L"\\" + fd.cFileName + rest) && (best.empty() || StrCmpLogicalW(fd.cFileName, best.c_str()) > 0)) best = fd.cFileName; } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return best.empty() ? p : root + L"\\" + best + rest;
}
PinInfo ResolvePin(const ws& e) {
    Load();
    uint64_t st = ExtOf(e) == L".lnk" ? Stamp(e) : 0;
    auto it = g_pc.find(e); if (it != g_pc.end() && it->second.first == st && (it->second.second.exe.empty() || FileExists(it->second.second.exe))) return it->second.second;   // a cached exe that is gone (an update moved it) is looked up again
    PinInfo p = Build(e); g_pc[e] = {st, p}; return p;
}
// Windows keeps desktop-app pins as shortcuts in "User Pinned\TaskBar"; Store apps exist only as app ids inside the undocumented Taskband\Favorites
// blob. The blob is used just for ORDER: each pin goes where its name first shows up in it (UTF-16 text at either byte alignment, plain text, or
// the 8.3 alias like AUTODE~1.LNK); pins it doesn't mention keep the folder's alphabetical order at the end.
std::vector<ws> WindowsTaskbarPins() {
    std::string blob; DWORD n = 0; const wchar_t* key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Taskband";
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, nullptr, &n) == 0 && n) { blob.resize(n); if (RegGetValueW(HKEY_CURRENT_USER, key, L"Favorites", RRF_RT_REG_BINARY, nullptr, blob.data(), &n) != 0) blob.clear(); }
    ws u[2]; for (int a = 0; a < 2; a++) for (size_t i = a; i + 1 < blob.size(); i += 2) u[a] += (wchar_t)((uint8_t)blob[i] | ((uint8_t)blob[i + 1] << 8));
    std::string up = blob; for (auto& c : up) c = (char)toupper((uint8_t)c);
    auto where = [&](const ws& file) {
        size_t best = SIZE_MAX;
        for (int a = 0; a < 2; a++) if (size_t i = u[a].find(file); i != ws::npos) best = std::min(best, i * 2 + a);
        if (size_t i = blob.find(Narrow(file)); i != std::string::npos) best = std::min(best, i);
        std::string s; for (char c : Narrow(StemOf(file))) if (isalnum((uint8_t)c)) s += (char)toupper((uint8_t)c);
        if (s.size() > 6) if (size_t i = up.find(s.substr(0, 6) + "~"); i != std::string::npos) best = std::min(best, i);
        return best;
    };
    std::vector<std::pair<size_t, ws>> pins; std::vector<ws> lnkIds;
    ws dir = KnownFolder(FOLDERID_RoamingAppData) + L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { ws p = dir + L"\\" + fd.cFileName; pins.push_back({where(fd.cFileName), p}); lnkIds.push_back(AumidOfShortcut(p)); } while (FindNextFileW(h, &fd)); FindClose(h); }
    std::wregex appId(LR"([A-Za-z0-9.\-]+_[a-z0-9]{13}![A-Za-z0-9.\-_]+)");   // PackageName_publisherid!App
    for (int a = 0; a < 2; a++) for (std::wsregex_iterator m(u[a].begin(), u[a].end(), appId), end; m != end; ++m) {
        ws id = m->str(), e = L"app:" + id; size_t at = (size_t)m->position() * 2 + a;
        if (std::any_of(lnkIds.begin(), lnkIds.end(), [&](const ws& x) { return EqI(x, id); })) continue;   // already there as its shortcut
        auto it = std::find_if(pins.begin(), pins.end(), [&](auto& p) { return EqI(p.second, e); });
        if (it != pins.end()) it->first = std::min(it->first, at);
        else if (!ShellNameOf(L"aum:" + id).empty()) pins.push_back({at, e});   // skip apps that were uninstalled since
    }
    std::stable_sort(pins.begin(), pins.end(), [](auto& x, auto& y) { return x.first < y.first; });
    std::vector<ws> out; for (auto& p : pins) out.push_back(p.second); return out;
}
void SavePinCache(const std::vector<PinInfo>& pins) {
    Load(); std::string out;
    for (auto& p : pins) {
        uint64_t st = ExtOf(p.entry) == L".lnk" ? Stamp(p.entry) : 0;
        out += Narrow(Fmt(L"%s\t%llu\t%s\t%s\t%s\t%s\t%s\t%s\n", p.entry.c_str(), st, p.name.c_str(), p.exe.c_str(), p.aumid.c_str(), p.family.c_str(), p.launch.c_str(), p.iconKey.c_str()));
    }
    WriteAtomic(DataPath(L"pins.cache"), out);
}
std::vector<PinInfo> LoadPinCache() {   // order follows settings, used by the stand-in bar before anything else is ready
    Load(); std::vector<PinInfo> v;
    for (auto& e : S.taskbarPins) { auto it = g_pc.find(e); if (it != g_pc.end()) v.push_back(it->second.second); }
    return v;
}
bool PinMatchesWindow(const PinInfo& p, const WinInfo& w) {
    if (!p.aumid.empty() && !w.aumid.empty() && EqI(p.aumid, w.aumid)) return true;
    if (w.aumid.empty() && !p.family.empty() && EqI(p.family, w.family)) return true;
    if (!p.exe.empty() && !w.exe.empty() && EqI(p.exe, w.exe)) {
        // two different Store apps in one host exe must not collapse into one button
        if (!p.aumid.empty() && !w.aumid.empty() && !EqI(p.aumid, w.aumid) && EqI(FileNameOf(p.exe), L"ApplicationFrameHost.exe")) return false;
        return true;
    }
    return false;
}
ws WinGroupKey(const WinInfo& w) { return !w.aumid.empty() ? L"a:" + Lower(w.aumid) : !w.exe.empty() ? L"e:" + Lower(w.exe) : L"t:" + w.title; }
ws WinGroupName(const WinInfo& w) {
    ws n = StemOf(w.exe);
    if (n.empty() || EqI(n, L"ApplicationFrameHost")) return w.title;
    return n;
}
ws WinIconKey(const WinInfo& w) {
    if (!w.aumid.empty() && w.aumid.find(L'!') != ws::npos) return L"aum:" + w.aumid;
    return w.exe.empty() ? ws() : L"exe:" + w.exe;
}
