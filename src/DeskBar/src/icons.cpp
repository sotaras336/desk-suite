#include "pch.h"
#include "app.h"

// Icons come from the shell (IShellItemImageFactory) and are cached on disk (icons\<hash>_<px>.png) so the next start has them instantly.
// Two workers, newest request first: the FAST one only reads that disk cache and never waits for the shell; what it can't find goes to the SLOW one,
// which asks the shell. With a single worker, one slow shell answer right after boot held up every icon already saved on disk (a half-empty bar).
namespace {
struct Job { ws key; int px; };
std::mutex g_mu; std::map<std::pair<ws, int>, ImgP> g_cache; std::set<std::pair<ws, int>> g_pending; std::set<std::pair<ws, int>> g_failed; std::vector<Job> g_queue, g_slow;
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
ImgP FromShell(const ws& key, int px) {
    ComPtr<IShellItemImageFactory> fac;
    if (FAILED(SHCreateItemFromParsingName(ParsingName(key).c_str(), nullptr, IID_PPV_ARGS(&fac)))) return nullptr;
    HBITMAP hb = nullptr;
    if (FAILED(fac->GetImage({px, px}, SIIGBF_ICONONLY, &hb)) || !hb) return nullptr;
    ImgP im = ImgFromHBitmap(hb); DeleteObject(hb);
    if (im) ImgSavePng(*im, CacheFile(key, px));
    return im;
}
void Finish(const Job& j, ImgP im) {
    { std::lock_guard<std::mutex> l(g_mu); g_pending.erase({j.key, j.px}); if (im) g_cache[{j.key, j.px}] = im; else g_failed.insert({j.key, j.px}); }
    if (im) UiPost([] { InvalidateAllWnds(); });
}
bool Take(std::vector<Job>& q, Job& j) { std::lock_guard<std::mutex> l(g_mu); if (q.empty()) return false; j = q.back(); q.pop_back(); return true; }
void FastWorker() {   // the disk cache only
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Job j; if (!Take(g_queue, j)) { WaitForSingleObject(g_ev, INFINITE); continue; }
        if (ImgP im = ImgFromFile(CacheFile(j.key, j.px))) Finish(j, im);
        else { { std::lock_guard<std::mutex> l(g_mu); g_slow.push_back(j); } SetEvent(g_evSlow); }
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
void IconInit() {
    g_dir = DataPath(L"icons"); SHCreateDirectoryExW(nullptr, g_dir.c_str(), nullptr);
    g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr); g_evSlow = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(FastWorker).detach(); std::thread(SlowWorker).detach();
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
