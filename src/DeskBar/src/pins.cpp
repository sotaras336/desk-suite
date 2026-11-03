#include "pch.h"
#include "app.h"

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
PinInfo Build(const ws& e) {
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
PinInfo ResolvePin(const ws& e) {
    Load();
    uint64_t st = ExtOf(e) == L".lnk" ? Stamp(e) : 0;
    auto it = g_pc.find(e); if (it != g_pc.end() && it->second.first == st) return it->second.second;
    PinInfo p = Build(e); g_pc[e] = {st, p}; return p;
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
