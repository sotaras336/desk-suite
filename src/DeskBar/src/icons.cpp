#include "pch.h"
#include "app.h"

// Icons come from the shell (IShellItemImageFactory) and are cached on disk (icons\<hash>_<px>.png) so the next start has them instantly.
// Two workers, newest request first: the FAST one only reads that disk cache and never waits for the shell; what it can't find goes to the SLOW one,
// which asks the shell. With a single worker, one slow shell answer right after boot held up every icon already saved on disk (a half-empty bar).
namespace {
struct Job { ws key; int px; };
std::mutex g_mu; std::map<std::pair<ws, int>, ImgP> g_cache; std::set<std::pair<ws, int>> g_pending; std::set<std::pair<ws, int>> g_failed; std::vector<Job> g_queue, g_slow;
std::map<std::pair<ws, int>, uint64_t> g_used;   // last time each cached icon was asked for
HANDLE g_ev, g_evSlow; ws g_dir;

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
ws CacheFile(const ws& key, int px) { return g_dir + L"\\" + Hex16(Sha1Prefix(CacheName(key))) + L"_" + std::to_wstring(px) + L".png"; }
// The same icon under the key alone, without the program's write time: the last picture the shell gave for it. After a reboot that updated an app
// (Windows Update, Chrome, Teams, Office all do this at restart) the exact file above is missing and the shell, busy with sign-in, took seconds per icon:
// the bar waited (up to 8 s) with empty buttons. Now the last picture shows at once, and the fresh one replaces it when the shell answers.
ws LastFile(const ws& key, int px) { return g_dir + L"\\" + Hex16(Sha1Prefix(key)) + L"_" + std::to_wstring(px) + L".png"; }
ImgP FromShell(const ws& key, int px) {
    ComPtr<IShellItemImageFactory> fac;
    if (FAILED(SHCreateItemFromParsingName(ParsingName(key).c_str(), nullptr, IID_PPV_ARGS(&fac)))) return nullptr;
    HBITMAP hb = nullptr;
    if (FAILED(fac->GetImage({px, px}, SIIGBF_ICONONLY, &hb)) || !hb) return nullptr;
    ImgP im = ImgFromHBitmap(hb); DeleteObject(hb);
    if (im) { ws exact = CacheFile(key, px), last = LastFile(key, px); ImgSavePng(*im, exact); if (!EqI(exact, last)) CopyFileW(exact.c_str(), last.c_str(), FALSE); }
    return im;
}
void Finish(const Job& j, ImgP im) {
    { std::lock_guard<std::mutex> l(g_mu); g_pending.erase({j.key, j.px}); if (im) g_cache[{j.key, j.px}] = im; else if (!g_cache.count({j.key, j.px})) g_failed.insert({j.key, j.px}); }   // the shell failing keeps the last picture, if there was one
    if (im) UiPost([] { InvalidateAllWnds(); });
}
bool Take(std::vector<Job>& q, Job& j) { std::lock_guard<std::mutex> l(g_mu); if (q.empty()) return false; j = q.back(); q.pop_back(); return true; }
void FastWorker() {   // the disk cache only
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Job j; if (!Take(g_queue, j)) { WaitForSingleObject(g_ev, INFINITE); continue; }
        ws exact = CacheFile(j.key, j.px), last = LastFile(j.key, j.px);
        if (ImgP im = ImgFromFile(exact)) { Finish(j, im); if (!EqI(exact, last) && !FileExists(last)) CopyFileW(exact.c_str(), last.c_str(), TRUE); continue; }   // a cache from before the "last picture" existed gets one too
        if (ImgP old = ImgFromFile(last)) { { std::lock_guard<std::mutex> l(g_mu); g_cache[{j.key, j.px}] = old; } UiPost([] { InvalidateAllWnds(); }); }   // shown now, kept pending: the shell's answer replaces it
        { std::lock_guard<std::mutex> l(g_mu); g_slow.push_back(j); } SetEvent(g_evSlow);
    }
}
void SlowWorker() {   // the shell
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        Job j; if (!Take(g_slow, j)) { WaitForSingleObject(g_evSlow, INFINITE); continue; }
        Finish(j, FromShell(j.key, j.px));
    }
}
}
// For the supervisor's stand-in bar (no icon workers there): the exact cached picture, else the last one.
ws IconCachedFile(const ws& key, int px) {
    if (g_dir.empty()) g_dir = DataPath(L"icons");
    ws f = CacheFile(key, px); if (FileExists(f)) return f; f = LastFile(key, px); return FileExists(f) ? f : ws();
}
void IconInit() {
    g_dir = DataPath(L"icons"); SHCreateDirectoryExW(nullptr, g_dir.c_str(), nullptr);
    g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); g_evSlow = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(FastWorker).detach(); std::thread(SlowWorker).detach();
}
ImgP IconGet(const ws& key, int px) {
    if (key.empty() || px <= 0) return nullptr;
    std::lock_guard<std::mutex> l(g_mu);
    auto k = std::make_pair(key, px); auto it = g_cache.find(k); if (it != g_cache.end()) { g_used[k] = GetTickCount64(); return it->second; }
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

// Every file shown in search kept its icon in memory for good. Above `keep` icons, the ones nothing holds and nobody asked for in 10 minutes go
// (oldest first); the disk cache brings one back in a few ms. The bar's and Start's icons are asked for on every paint, so they are never the old ones.
size_t IconTrim(size_t keep, uint64_t idleMs) {
    std::lock_guard<std::mutex> l(g_mu); if (g_cache.size() <= keep) return 0;
    uint64_t now = GetTickCount64(); std::vector<std::pair<uint64_t, std::pair<ws, int>>> old;
    for (auto& [k, im] : g_cache) { uint64_t u = g_used.count(k) ? g_used[k] : 0; if (im.use_count() == 1 && now - u >= idleMs) old.push_back({u, k}); }
    std::sort(old.begin(), old.end()); size_t freed = 0, drop = std::min(old.size(), g_cache.size() - keep);
    for (size_t i = 0; i < drop; i++) { auto& k = old[i].second; freed += g_cache[k]->px.size() * 4; g_cache.erase(k); g_used.erase(k); }
    return freed;
}

bool IconTrimSelfTest(ws& detail) {   // 3 fake icons, one still held: trimming to 1 drops only the two nobody holds (real icons set aside meanwhile)
    decltype(g_cache) real; { std::lock_guard<std::mutex> l(g_mu); real.swap(g_cache); }
    ImgP held = ImgMake(4, 4);
    { std::lock_guard<std::mutex> l(g_mu); g_cache[{L"t:a", 4}] = ImgMake(4, 4); g_cache[{L"t:b", 4}] = ImgMake(4, 4); g_cache[{L"t:c", 4}] = held; }
    size_t freed = IconTrim(1, 0);
    bool ok; { std::lock_guard<std::mutex> l(g_mu); ok = freed == 2 * 64 && g_cache.size() == 1 && g_cache.count({L"t:c", 4}); g_cache.swap(real); }
    detail = Fmt(L"freed %zu bytes", freed); return ok;
}
