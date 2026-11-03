#include "pch.h"
#include "app.h"

// Icons come from the shell (IShellItemImageFactory) on one low-priority worker, newest request first, and are cached on disk
// (icons\<hash>_<px>.png) so the next start has them instantly.
namespace {
struct Job { ws key; int px; };
std::mutex g_mu; std::map<std::pair<ws, int>, ImgP> g_cache; std::set<std::pair<ws, int>> g_pending; std::set<std::pair<ws, int>> g_failed; std::vector<Job> g_queue;
HANDLE g_ev; ws g_dir;

ws ParsingName(const ws& key) {
    if (StartsI(key, L"aum:")) return L"shell:AppsFolder\\" + key.substr(4);
    return key.substr(key.find(L':') + 1);
}
ws CacheName(const ws& key) {   // a program's icon changes when the file is replaced (a new build): its write time is part of the cache name
    ws k = key; WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!StartsI(key, L"aum:") && GetFileAttributesExW(ParsingName(key).c_str(), GetFileExInfoStandard, &d))
        k += L"|" + std::to_wstring(((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime);
    return k;
}
ImgP Fetch(const ws& key, int px) {
    ws f = g_dir + L"\\" + Hex16(Sha1Prefix(CacheName(key))) + L"_" + std::to_wstring(px) + L".png";
    if (auto im = ImgFromFile(f)) return im;
    ComPtr<IShellItemImageFactory> fac;
    if (FAILED(SHCreateItemFromParsingName(ParsingName(key).c_str(), nullptr, IID_PPV_ARGS(&fac)))) return nullptr;
    HBITMAP hb = nullptr;
    if (FAILED(fac->GetImage({px, px}, SIIGBF_ICONONLY, &hb)) || !hb) return nullptr;
    ImgP im = ImgFromHBitmap(hb); DeleteObject(hb);
    if (im) ImgSavePng(*im, f);
    return im;
}
void Worker() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        Job j;
        { std::lock_guard<std::mutex> l(g_mu); if (g_queue.empty()) j.px = 0; else { j = g_queue.back(); g_queue.pop_back(); } }
        if (!j.px) { WaitForSingleObject(g_ev, INFINITE); continue; }
        ImgP im = Fetch(j.key, j.px);
        { std::lock_guard<std::mutex> l(g_mu); g_pending.erase({j.key, j.px}); if (im) g_cache[{j.key, j.px}] = im; else g_failed.insert({j.key, j.px}); }
        if (im) UiPost([] { InvalidateAllWnds(); });
    }
}
}
void IconInit() {
    g_dir = DataPath(L"icons"); SHCreateDirectoryExW(nullptr, g_dir.c_str(), nullptr);
    g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); std::thread(Worker).detach();
}
ImgP IconGet(const ws& key, int px) {
    if (key.empty() || px <= 0) return nullptr;
    std::lock_guard<std::mutex> l(g_mu);
    auto k = std::make_pair(key, px); auto it = g_cache.find(k); if (it != g_cache.end()) return it->second;
    if (!g_failed.count(k) && g_pending.insert(k).second) { g_queue.push_back({key, px}); SetEvent(g_ev); }
    return nullptr;
}
ws ShellNameOf(const ws& key) {
    ComPtr<IShellItem> it; if (FAILED(SHCreateItemFromParsingName(ParsingName(key).c_str(), nullptr, IID_PPV_ARGS(&it)))) return {};
    PWSTR n = nullptr; ws r; if (SUCCEEDED(it->GetDisplayName(SIGDN_NORMALDISPLAY, &n))) r = n; CoTaskMemFree(n); return r;
}

bool IconSettled(const ws& key, int px) {   // arrived or definitively failed: the bar needn't wait for it any longer
    if (key.empty() || px <= 0) return true; { std::lock_guard<std::mutex> l(g_mu); auto k = std::make_pair(key, px); if (g_cache.count(k) || g_failed.count(k)) return true; }
    IconGet(key, px); return false;
}
