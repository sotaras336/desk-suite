#include "pch.h"
#include "search.h"
#include <condition_variable>
#include <functional>

// An index of EVERY file and folder name on the fixed and removable drives (hidden, system, Windows, Program Files, AppData: all of it; only the
// Recycle Bin and System Volume Information are left out), searched in tens of milliseconds. A scan runs in its own background-priority process
// (DeskBar.exe --buildindex) and writes fileindex.<time>.bin; the bar maps that file read-only and never copies it, so a few million names cost
// almost no private memory and "loading" is instant. Two name lists live in the file: a FOLDED one (lower case, accents and final sigma removed:
// "Εργασία" matches "εργασια") that searches run over, and the ORIGINAL one that full paths are rebuilt from for the results shown.
namespace {
constexpr uint32_t kRoot = 0x80000000u;

// ---------------------------------------------------------------- folding
// What both the names in the index and what you type go through, so they meet in the middle.
std::string FoldName(const wchar_t* s, size_t n) {
    bool ascii = true; for (size_t i = 0; i < n; i++) if (s[i] >= 0x80) { ascii = false; break; }
    if (ascii) { std::string o(n, 0); for (size_t i = 0; i < n; i++) { char c = (char)s[i]; o[i] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; } return o; }
    ws w(s, n); int m = NormalizeString(NormalizationKD, w.c_str(), (int)w.size(), nullptr, 0); ws d(m > 0 ? m : 0, 0);
    if (m > 0) m = NormalizeString(NormalizationKD, w.c_str(), (int)w.size(), d.data(), m); if (m <= 0) d = w; else d.resize(m);
    ws r; r.reserve(d.size());
    for (wchar_t c : d) { WORD t = 0; GetStringTypeW(CT_CTYPE3, &c, 1, &t); if (!(t & C3_NONSPACING)) r += c; }   // accents, Greek tonos
    if (!r.empty()) CharLowerBuffW(r.data(), (DWORD)r.size());
    for (auto& c : r) if (c == 0x3C2) c = 0x3C3;   // final sigma
    return Narrow(r);
}

// ---------------------------------------------------------------- the file
#pragma pack(push, 1)
struct Hdr { char magic[8]; uint32_t version, count, rootCount, pad; uint64_t lowBytes, origBytes, rootBytes, fileBytes; };
#pragma pack(pop)
constexpr char kMagic[8] = {'D', 'B', '3', 'I', 'D', 'X', '2', 0};

// What a scan collects. Entry i has a folded name (arena `low`, offsets noff), an original name (arena `orig`, offsets ooff), a parent (an entry
// index, or kRoot | n for the n-th scan root) and a folder flag. A folder is always added before what is inside it. Names end with '\n'.
struct Builder {
    std::string low, orig; std::vector<uint32_t> noff, ooff, parent; std::vector<uint8_t> dir; std::vector<std::string> roots;
    size_t Count() const { return dir.size(); }
    uint32_t AddRoot(ws path) { while (path.size() > 3 && path.back() == L'\\') path.pop_back(); if (path.size() == 3 && path[1] == L':') path.pop_back(); roots.push_back(Narrow(path)); return kRoot | (uint32_t)(roots.size() - 1); }   // "C:\" -> "C:"
    uint32_t Add(uint32_t par, const wchar_t* name, bool d) {
        uint32_t me = (uint32_t)dir.size(); size_t n = wcslen(name);
        noff.push_back((uint32_t)low.size()); ooff.push_back((uint32_t)orig.size());
        low += FoldName(name, n); low += '\n'; orig += Narrow(ws(name, n)); orig += '\n'; parent.push_back(par); dir.push_back(d ? 1 : 0); return me;
    }
    bool Full() const { return low.size() > 0xE0000000u || orig.size() > 0xE0000000u || dir.size() > 0x7FFFFFF0u; }   // offsets are 32-bit
};

bool WriteIndex(const ws& path, const Builder& b) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr); if (h == INVALID_HANDLE_VALUE) return false;
    uint32_t c = (uint32_t)b.Count(); std::string rootBlob; for (auto& r : b.roots) { rootBlob += r; rootBlob += '\0'; }
    Hdr hd{}; memcpy(hd.magic, kMagic, 8); hd.version = 2; hd.count = c; hd.rootCount = (uint32_t)b.roots.size(); hd.lowBytes = b.low.size(); hd.origBytes = b.orig.size(); hd.rootBytes = rootBlob.size();
    hd.fileBytes = sizeof(Hdr) + 4ull * (c + 1) * 2 + 4ull * c + c + hd.lowBytes + hd.origBytes + hd.rootBytes;
    bool ok = true; auto put = [&](const void* p, size_t n) { const char* q = (const char*)p; while (ok && n) { DWORD w = 0, k = (DWORD)std::min<size_t>(n, 1u << 24); ok = WriteFile(h, q, k, &w, nullptr) && w == k; q += k; n -= k; } };
    uint32_t endLow = (uint32_t)b.low.size(), endOrig = (uint32_t)b.orig.size();
    put(&hd, sizeof hd); put(b.noff.data(), 4ull * c); put(&endLow, 4); put(b.ooff.data(), 4ull * c); put(&endOrig, 4);
    put(b.parent.data(), 4ull * c); put(b.dir.data(), c); put(b.low.data(), b.low.size()); put(b.orig.data(), b.orig.size()); put(rootBlob.data(), rootBlob.size());
    ok = CloseHandle(h) && ok; return ok;
}

// A saved index, mapped. Open() proves every offset is inside the file before anything reads it (the first compressed index read past its
// end and crashed the bar after every scan): after that, no lookup can leave the mapping.
struct Index {
    HANDLE file = INVALID_HANDLE_VALUE, map = nullptr; const uint8_t* base = nullptr; uint64_t bytes = 0;
    uint32_t count = 0; const uint32_t *noff = nullptr, *ooff = nullptr, *parent = nullptr; const uint8_t* dir = nullptr; const char *low = nullptr, *orig = nullptr; uint64_t lowBytes = 0;
    std::vector<std::string_view> roots;
    Index() = default; Index(const Index&) = delete; Index& operator=(const Index&) = delete;
    ~Index() { if (base) UnmapViewOfFile(base); if (map) CloseHandle(map); if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    bool Open(const ws& path) {
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr); if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz; if (!GetFileSizeEx(file, &sz) || sz.QuadPart < (LONGLONG)sizeof(Hdr)) return false; bytes = (uint64_t)sz.QuadPart;
        map = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr); if (!map) return false;
        base = (const uint8_t*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0); if (!base) return false;
        const Hdr* h = (const Hdr*)base; if (memcmp(h->magic, kMagic, 8) != 0 || h->version != 2 || h->count == 0 || h->count > 0x7FFFFFF0u || h->lowBytes == 0 || h->origBytes == 0) return false;
        uint64_t c = h->count, oN = sizeof(Hdr), oO = oN + 4 * (c + 1), oP = oO + 4 * (c + 1), oD = oP + 4 * c, oL = oD + c, oR = oL + h->lowBytes + h->origBytes, end = oR + h->rootBytes;
        if (end != bytes || h->fileBytes != bytes || h->lowBytes > 0xFFFFFFFFull || h->origBytes > 0xFFFFFFFFull) return false;
        count = h->count; noff = (const uint32_t*)(base + oN); ooff = (const uint32_t*)(base + oO); parent = (const uint32_t*)(base + oP); dir = base + oD;
        low = (const char*)(base + oL); orig = low + h->lowBytes; lowBytes = h->lowBytes;
        if (noff[0] != 0 || noff[count] != h->lowBytes || ooff[0] != 0 || ooff[count] != h->origBytes || low[h->lowBytes - 1] != '\n' || orig[h->origBytes - 1] != '\n') return false;
        for (uint32_t i = 0; i < count; i++) {   // names grow, parents come first
            if (noff[i + 1] <= noff[i] || noff[i + 1] > h->lowBytes || ooff[i + 1] <= ooff[i] || ooff[i + 1] > h->origBytes) return false;
            uint32_t p = parent[i]; if (p & kRoot) { if ((p & ~kRoot) >= h->rootCount) return false; } else if (p >= i) return false;
        }
        const char* r = (const char*)(base + oR); const char* re = r + h->rootBytes;
        for (uint32_t i = 0; i < h->rootCount; i++) { const char* z = (const char*)memchr(r, 0, re - r); if (!z) return false; roots.emplace_back(r, z - r); r = z + 1; }
        return r == re;
    }
    std::string_view Folded(size_t i) const { return {low + noff[i], noff[i + 1] - noff[i] - 1}; }
    std::string_view Name(size_t i) const { return {orig + ooff[i], ooff[i + 1] - ooff[i] - 1}; }
    std::string Path(size_t i) const {
        std::vector<uint32_t> chain; uint32_t p = (uint32_t)i;
        for (;;) { chain.push_back(p); uint32_t q = parent[p]; if (q & kRoot) { std::string out(roots[q & ~kRoot]); for (auto it = chain.rbegin(); it != chain.rend(); ++it) { out += '\\'; out.append(Name(*it)); } return out; } p = q; }
    }
    int Depth(size_t i) const { int n = 1; uint32_t p = parent[i]; while (!(p & kRoot)) { n++; p = parent[p]; } for (char c : roots[p & ~kRoot]) n += c == '\\'; return n; }
};

std::shared_ptr<const Index> g_ix; std::mutex g_mu;
ws IndexDir() { return DirOf(DataPath(L"fileindex.x")); }
std::vector<ws> IndexFiles() {   // newest first (the name carries the time, fixed width)
    std::vector<ws> v; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((IndexDir() + L"\\fileindex.*.bin").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) { do v.push_back(fd.cFileName); while (FindNextFileW(h, &fd)); FindClose(h); }
    std::sort(v.begin(), v.end(), [](const ws& a, const ws& b) { return a > b; }); return v;
}
void TidyOld(const ws& keep) {   // older generations, the temp files of an interrupted scan, the text index of the old format
    for (auto& f : IndexFiles()) if (f != keep) DeleteFileW((IndexDir() + L"\\" + f).c_str());
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((IndexDir() + L"\\fileindex.*.tmp").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h != INVALID_HANDLE_VALUE) { do DeleteFileW((IndexDir() + L"\\" + fd.cFileName).c_str()); while (FindNextFileW(h, &fd)); FindClose(h); }
    DeleteFileW(DataPath(L"fileindex.txt").c_str());
}
void LoadNewest() {
    for (auto& f : IndexFiles()) {
        auto ix = std::make_shared<Index>(); uint64_t t = Now();
        if (!ix->Open(IndexDir() + L"\\" + f)) { Log(L"file index: %s is not usable, trying the next", f.c_str()); continue; }
        size_t n = ix->count; uint64_t mb = ix->bytes >> 20; { std::lock_guard<std::mutex> l(g_mu); g_ix = ix; }
        Log(L"file index: %d entries mapped from %s (%d MB, %d ms)", (int)n, f.c_str(), (int)mb, (int)(Now() - t)); TidyOld(f); return;
    }
}

// ---------------------------------------------------------------- the scan
bool IsCloudRoot(const ws& root) {
    wchar_t r[4] = {root[0], L':', L'\\', 0}; wchar_t label[128] = {}; if (!GetVolumeInformationW(r, label, 128, nullptr, nullptr, nullptr, nullptr, 0)) return false;
    return ContainsI(label, L"Google Drive") || ContainsI(label, L"OneDrive");
}
std::vector<ws> Roots() {   // whole drives: fixed disks and USB sticks that are plugged in (not network shares, not empty card readers)
    std::vector<ws> v; wchar_t drives[256]; GetLogicalDriveStringsW(256, drives);
    for (wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        UINT t = GetDriveTypeW(d); if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) continue;
        if (!GetVolumeInformationW(d, nullptr, 0, nullptr, nullptr, nullptr, nullptr, 0)) continue;   // no disk in it
        v.push_back(d);
    }
    return v;
}
bool NeverIndexed(const wchar_t* n) { return !_wcsicmp(n, L"$Recycle.Bin") || !_wcsicmp(n, L"System Volume Information"); }   // Windows' own bookkeeping: nobody looks for files there, and it is locked anyway
void Walk(const ws& root, uint64_t budgetMs, Builder& out, size_t cap) {
    struct Dir { ws path; uint32_t id; int depth; };
    std::vector<Dir> stack; stack.push_back({root, out.AddRoot(root), 0}); uint64_t t0 = Now(); unsigned tick = 0;
    while (!stack.empty()) {
        Dir d = std::move(stack.back()); stack.pop_back(); bool bs = d.path.back() == L'\\';
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((d.path + (bs ? L"*" : L"\\*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isDir && NeverIndexed(fd.cFileName)) continue;
            uint32_t id = out.Add(d.id, fd.cFileName, isDir);
            if (isDir) {
                bool follow = d.depth < 120;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { DWORD tag = fd.dwReserved0; if (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT) follow = false; }   // links loop and list things twice; cloud folders are fine
                if (follow) stack.push_back({d.path + (bs ? L"" : L"\\") + fd.cFileName, id, d.depth + 1});
            }
            if (out.Count() > cap || out.Full() || ((++tick & 0x3F) == 0 && Now() - t0 > budgetMs)) { FindClose(h); return; }   // never let one huge place hold everything up
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
ws NewGenerationName() { FILETIME f; GetSystemTimeAsFileTime(&f); return Fmt(L"fileindex.%08lx%08lx.bin", f.dwHighDateTime, f.dwLowDateTime); }
// The scan: "DeskBar.exe --buildindex", started by SpawnScan, in its own process at background priority for CPU, disk and memory. Inside the bar it
// competed with the window thread for the heap lock, and a crash in it took the bar down with it.
int FileIndexBuildMain() {
    SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
    uint64_t t0 = Now(); Builder b; std::vector<ws> cloud, normal; for (auto& r : Roots()) (IsCloudRoot(r) ? cloud : normal).push_back(r);
    auto run = [&](const ws& r, bool c) { size_t before = b.Count(); uint64_t t = Now(); Walk(r, c ? 5 * 60000 : 20 * 60000, b, 40000000); Log(L"file index root %s: %d entries, %d ms", r.c_str(), (int)(b.Count() - before), (int)(Now() - t)); };
    for (auto& r : normal) run(r, false);
    for (auto& r : cloud) run(r, true);
    Log(L"file index: %d entries in %d ms", (int)b.Count(), (int)(Now() - t0));
    if (b.Count() == 0) return 2;
    ws name = NewGenerationName(), tmp = IndexDir() + L"\\" + name + L".tmp", fin = IndexDir() + L"\\" + name;
    if (!WriteIndex(tmp, b)) { DeleteFileW(tmp.c_str()); Log(L"file index: could not write %s (%lu)", tmp.c_str(), GetLastError()); return 3; }
    if (!MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); return 4; }
    return 0;
}
void SpawnScan() {
    static std::atomic<bool> running{false}; if (running.exchange(true)) return;
    std::thread([] {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); ws cmd = L"\"" + ws(exe) + L"\" --buildindex";
        STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; DWORD code = (DWORD)-1;
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
            if (WaitForSingleObject(pi.hProcess, 40 * 60 * 1000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code); else TerminateProcess(pi.hProcess, 1);   // our own child, by handle
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        } else Log(L"file index: could not start the scan (%lu)", GetLastError());
        if (code == 0) LoadNewest(); else Log(L"file index: the scan ended with %lu, keeping the old list", code);
        running = false;
    }).detach();
}

// ---------------------------------------------------------------- searching
struct Hit { uint32_t i; double s; };
std::vector<std::string> Tokens(const std::string& folded) {   // words; a path separator or a wildcard also separates ("docs\report", "*.pdf")
    std::vector<std::string> t; std::string cur;
    for (char c : folded) { if (c == ' ' || c == '\\' || c == '/' || c == '*' || c == '?' || c == '\t') { if (!cur.empty()) t.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) t.push_back(cur); return t;
}
bool WordStart(std::string_view n, const std::string& t) {
    size_t i = 0; while ((i = n.find(t, i)) != std::string_view::npos) { if (i == 0 || !(isalnum((unsigned char)n[i - 1]) || (unsigned char)n[i - 1] >= 0x80)) return true; i++; } return false;
}
// The best matches for a query, best first (at most `keep`). Names must hold every word, or have it in one of the folders above them
// ("report 2024" finds report.docx inside a folder called 2024). Returns empty when `cancelled` says a newer query has arrived.
std::vector<Hit> Rank(const Index& ix, const std::string& folded, size_t keep, const std::function<bool()>& cancelled) {
    std::vector<Hit> out; auto toks = Tokens(folded); size_t total = 0; for (auto& t : toks) total += t.size(); if (toks.empty() || total < 2) return out;
    std::string qs; for (auto& t : toks) qs += (qs.empty() ? "" : " ") + t;
    std::vector<size_t> order(toks.size()); for (size_t k = 0; k < order.size(); k++) order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](size_t x, size_t y) { return toks[x].size() > toks[y].size(); });   // the longest word is the rarest: scan for that one first
    std::vector<Hit> hits; const char* b = ix.low; const char* e = b + ix.lowBytes; constexpr size_t kChunk = 4u << 20;
    for (size_t a : order) {   // one pass is enough unless a word only exists in a folder name above the file: then each word takes a turn
    const std::string& anchor = toks[a]; if (anchor.size() < 2) continue;
    std::boyer_moore_horspool_searcher<const char*> bmh(anchor.data(), anchor.data() + anchor.size()); const char* p = b; size_t alen = anchor.size();
    while (p < e) {
        if (cancelled()) return {};
        const char* lim = (size_t)(e - p) > kChunk ? p + kChunk : e;
        const char* f = std::search(p, lim, bmh);
        if (f == lim) { if (lim == e) break; p = lim - (alen - 1); continue; }   // overlap, so a match across the chunk edge is not lost
        uint32_t off = (uint32_t)(f - b), ei = (uint32_t)(std::upper_bound(ix.noff, ix.noff + ix.count + 1, off) - ix.noff) - 1;
        p = b + ix.noff[ei + 1];   // one hit per name
        std::string_view name = ix.Folded(ei); bool inName = true, ok = true;
        for (size_t k = 0; k < toks.size() && ok; k++) {
            if (k == a || name.find(toks[k]) != std::string_view::npos) continue;
            inName = false; ok = false;
            for (uint32_t q = ix.parent[ei]; !(q & kRoot); q = ix.parent[q]) if (ix.Folded(q).find(toks[k]) != std::string_view::npos) { ok = true; break; }
        }
        if (!ok) continue;
        bool d = ix.dir[ei]; std::string_view stem = name; if (!d) { size_t dot = stem.rfind('.'); if (dot != std::string_view::npos && dot > 0) stem = stem.substr(0, dot); }
        double v = stem == qs ? 120 : name.compare(0, toks[0].size(), toks[0]) == 0 ? 100 : WordStart(name, toks[0]) ? 85 : name.find(toks[0]) != std::string_view::npos ? 55 : 45;
        if (!inName) v -= 8;
        v += d ? 3 : 0; v -= std::min(10.0, name.size() * 0.08); hits.push_back({ei, v});
        if (hits.size() >= 3000000) break;
    }
    if (toks.size() == 1 || hits.size() >= 30) break;
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.i < y.i; }); hits.erase(std::unique(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.i == y.i; }), hits.end());
    size_t top = std::min<size_t>(hits.size(), 20000);   // the best by name; then where each one lives decides, so a common name (readme) doesn't fill the list with Program Files
    std::partial_sort(hits.begin(), hits.begin() + top, hits.end(), [](const Hit& x, const Hit& y) { return x.s > y.s; }); hits.resize(top);
    static const std::string user = [] { ws h = FileNameOf(KnownFolder(FOLDERID_Profile)); return FoldName(h.c_str(), h.size()); }();
    std::vector<std::string_view> chain;   // the folders above one hit, top-level one first: ("users", "me", "documents", ...)
    for (auto& h : hits) {
        chain.clear(); uint32_t q = ix.parent[h.i]; while (!(q & kRoot)) { chain.push_back(ix.Folded(q)); q = ix.parent[q]; }
        std::reverse(chain.begin(), chain.end()); int depth = (int)chain.size() + 1; for (char c : ix.roots[q & ~kRoot]) depth += c == '\\';
        h.s -= std::min(12.0, depth * 0.8);
        bool mine = chain.size() >= 2 && chain[0] == "users" && chain[1] == user, app = mine && chain.size() >= 3 && chain[2] == "appdata";
        bool sys = !chain.empty() && (chain[0] == "windows" || chain[0] == "programdata" || chain[0] == "program files" || chain[0] == "program files (x86)");
        bool noise = false; for (auto n : chain) if (n == "node_modules" || n == ".git" || n == "site-packages" || n == "winsxs" || n == ".cache" || n == "__pycache__") { noise = true; break; }
        if (mine && !app) h.s += 5; else if (app || sys) h.s -= 6;
        if (noise) h.s -= 10;
    }
    size_t keepN = std::min(hits.size(), keep); std::partial_sort(hits.begin(), hits.begin() + keepN, hits.end(), [](const Hit& x, const Hit& y) { return x.s > y.s; }); hits.resize(keepN); return hits;
}

struct Req { ws q; uint64_t id; std::function<void(std::vector<Result>)> cb; };
std::mutex g_rmu; std::condition_variable g_rcv; Req g_req; std::atomic<uint64_t> g_latest{0}; bool g_haveReq = false;
void Worker() {
    for (;;) {
        Req r; { std::unique_lock<std::mutex> l(g_rmu); g_rcv.wait(l, [] { return g_haveReq; }); r = std::move(g_req); g_haveReq = false; }
        std::shared_ptr<const Index> ix; { std::lock_guard<std::mutex> l(g_mu); ix = g_ix; }
        std::vector<Result> out; if (!ix) { UiPost([cb = r.cb] { cb({}); }); continue; }
        ws q = Trim(r.q); auto hits = Rank(*ix, FoldName(q.c_str(), q.size()), 400, [&] { return r.id != g_latest; });
        if (r.id != g_latest) continue;
        for (auto& h : hits) {   // the ones that are still there, with their real names
            if (out.size() >= 40) break;
            ws path = Widen(ix->Path(h.i)); bool d = ix->dir[h.i]; WIN32_FILE_ATTRIBUTE_DATA fa{};
            if (!GetFileAttributesExW((path.size() >= 248 ? L"\\\\?\\" + path : path).c_str(), GetFileExInfoStandard, &fa)) continue;   // deleted since the scan
            ws sub = Shorten(DirOf(path)); if (!d) sub += L"  ·  " + Ago(fa.ftLastWriteTime);
            out.push_back({d ? RKind::Folder : RKind::File, FileNameOf(path), sub, path, h.s * 0.7});
        }
        if (r.id == g_latest) UiPost([cb = r.cb, out = std::move(out)]() mutable { cb(std::move(out)); });
    }
}
}

int FileIndexBuild() { return FileIndexBuildMain(); }   // --buildindex

void SearchFiles(const ws& q, std::function<void(std::vector<Result>)> cb) {
    static std::once_flag once; std::call_once(once, [] { std::thread(Worker).detach(); });
    uint64_t id = ++g_latest; { std::lock_guard<std::mutex> l(g_rmu); g_req = {q, id, std::move(cb)}; g_haveReq = true; } g_rcv.notify_one();
}
void FileIndexStart() {
    std::thread([] { Sleep(10000); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); LoadNewest(); }).detach();   // mapping is cheap, but not during the boot rush
    bool fresh = false; auto files = IndexFiles();
    if (!files.empty()) { WIN32_FILE_ATTRIBUTE_DATA fa{}; if (GetFileAttributesExW((IndexDir() + L"\\" + files[0]).c_str(), GetFileExInfoStandard, &fa)) { FILETIME n; GetSystemTimeAsFileTime(&n); ULARGE_INTEGER a, b; a.LowPart = n.dwLowDateTime; a.HighPart = n.dwHighDateTime; b.LowPart = fa.ftLastWriteTime.dwLowDateTime; b.HighPart = fa.ftLastWriteTime.dwHighDateTime; fresh = (a.QuadPart - b.QuadPart) < 6ull * 3600 * 10000000; } }
    // The scan reads the whole disk: it runs as a low-priority process, only when the saved list is old (or there is none yet), and rarely.
    UiOnce(fresh ? 30 * 60000 : 3 * 60000, [] { SpawnScan(); UiTimer(3 * 3600 * 1000, [] { SpawnScan(); }); });
}

// ---------------------------------------------------------------- tests
namespace {
ws TempIndexPath() { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); return ws(t) + L"db3-selftest-index.bin"; }
std::vector<std::string> RankedPaths(const Index& ix, const ws& q, size_t keep = 10) {
    std::vector<std::string> v; ws qq = Trim(q); for (auto& h : Rank(ix, FoldName(qq.c_str(), qq.size()), keep, [] { return false; })) v.push_back(ix.Path(h.i)); return v;
}
}

bool FileIndexSelfTest(ws& detail) {
    Builder b; uint32_t r1 = b.AddRoot(L"C:\\Users\\me"), r2 = b.AddRoot(L"D:\\");   // the order a walk produces: each folder before what is inside it
    uint32_t docs = b.Add(r1, L"Docs", true); b.Add(docs, L"a.txt", false); uint32_t sub = b.Add(docs, L"Sub", true); b.Add(sub, L"B.PDF", false);
    uint32_t games = b.Add(r2, L"Games", true); b.Add(games, L"x.exe", false); b.Add(r1, L"top.txt", false);
    uint32_t gr = b.Add(r1, L"Εργασία", true); b.Add(gr, L"Έκθεση ΤΕΛΙΚΗ.docx", false); b.Add(r1, L"Straße.txt", false);
    ws path = TempIndexPath(); if (!WriteIndex(path, b)) { detail = L"could not write the index"; return false; }
    auto pix = std::make_unique<Index>(); Index& ix = *pix; if (!ix.Open(path)) { detail = L"could not open what was written"; pix.reset(); DeleteFileW(path.c_str()); return false; }
    const wchar_t* want[] = {L"C:\\Users\\me\\Docs", L"C:\\Users\\me\\Docs\\a.txt", L"C:\\Users\\me\\Docs\\Sub", L"C:\\Users\\me\\Docs\\Sub\\B.PDF", L"D:\\Games", L"D:\\Games\\x.exe",
        L"C:\\Users\\me\\top.txt", L"C:\\Users\\me\\Εργασία", L"C:\\Users\\me\\Εργασία\\Έκθεση ΤΕΛΙΚΗ.docx", L"C:\\Users\\me\\Straße.txt"};
    bool ok = ix.count == std::size(want);
    for (size_t i = 0; ok && i < ix.count; i++) {   // a depth is the number of backslashes in the full path
        ws got = Widen(ix.Path(i)); int depth = 0; for (wchar_t c : ws(want[i])) depth += c == L'\\';
        if (got != want[i] || ix.Depth(i) != depth) { detail = got + L" (depth " + std::to_wstring(ix.Depth(i)) + L")"; ok = false; }
    }
    auto first = [&](const wchar_t* q) { auto v = RankedPaths(ix, q); return v.empty() ? ws() : Widen(v[0]); };
    auto has = [&](const wchar_t* q, const wchar_t* p) { for (auto& x : RankedPaths(ix, q)) if (Widen(x) == p) return true; return false; };
    if (ok && !has(L"b.pdf", L"C:\\Users\\me\\Docs\\Sub\\B.PDF")) { detail = L"b.pdf not found"; ok = false; }                                                  // case-insensitive
    if (ok && !has(L"εργασια", L"C:\\Users\\me\\Εργασία")) { detail = L"accent-free Greek not found"; ok = false; }                                      // accents folded
    if (ok && !has(L"ΕΚΘΕΣΗ τελικη", L"C:\\Users\\me\\Εργασία\\Έκθεση ΤΕΛΙΚΗ.docx")) { detail = L"two Greek words not found"; ok = false; }                // final sigma, all words
    if (ok && !has(L"εργασια εκθεση", L"C:\\Users\\me\\Εργασία\\Έκθεση ΤΕΛΙΚΗ.docx")) { detail = L"word from the folder above not found"; ok = false; }   // a word may sit in a parent folder
    if (ok && !has(L"*.pdf", L"C:\\Users\\me\\Docs\\Sub\\B.PDF")) { detail = L"wildcard not found"; ok = false; }
    if (ok && first(L"top") != L"C:\\Users\\me\\top.txt") { detail = L"ranking: " + first(L"top"); ok = false; }
    if (ok && !RankedPaths(ix, L"nothing-like-this").empty()) { detail = L"a miss found something"; ok = false; }
    if (ok) detail = std::to_wstring(ix.count) + L" entries, " + std::to_wstring(ix.roots.size()) + L" roots, " + std::to_wstring(ix.bytes) + L" bytes mapped";
    pix.reset(); DeleteFileW(path.c_str()); return ok;
}

// A real scan of a small folder, saved and mapped exactly as a rescan does it: the path that crashed the bar after every rescan. Everything is indexed now
// (a .dll, a hidden system file); only the Recycle Bin / System Volume Information folders are left out.
bool FileIndexScanSelfTest(ws& detail) {
    ws root = KnownFolder(FOLDERID_Profile) + L"\\db3-index-scan";
    SHCreateDirectoryExW(nullptr, (root + L"\\a\\b").c_str(), nullptr); SHCreateDirectoryExW(nullptr, (root + L"\\$Recycle.Bin").c_str(), nullptr);
    { std::string z = "x"; for (auto n : {L"\\top.txt", L"\\a\\one.txt", L"\\a\\b\\deep.docx", L"\\a\\skip.dll", L"\\a\\hidden.sys", L"\\$Recycle.Bin\\bin.txt"}) WriteAtomic(root + n, z); }
    SetFileAttributesW((root + L"\\a\\hidden.sys").c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    Builder b; Walk(root, 5000, b, 1000);
    ws path = TempIndexPath(); bool wrote = WriteIndex(path, b); auto pix = std::make_unique<Index>(); Index& ix = *pix; bool opened = wrote && ix.Open(path);
    std::set<std::string> got, want; std::string r8 = Narrow(root);
    for (auto n : {"\\a", "\\a\\b", "\\top.txt", "\\a\\one.txt", "\\a\\b\\deep.docx", "\\a\\skip.dll", "\\a\\hidden.sys"}) want.insert(r8 + n);   // not the Recycle Bin folder or what is in it
    if (opened) for (size_t i = 0; i < ix.count; i++) got.insert(ix.Path(i));
    bool found = false; if (opened) { auto v = RankedPaths(ix, L"deep"); found = !v.empty() && v[0] == r8 + "\\a\\b\\deep.docx"; }
    pix.reset();
    SetFileAttributesW((root + L"\\a\\hidden.sys").c_str(), FILE_ATTRIBUTE_NORMAL);
    for (auto n : {L"\\a\\b\\deep.docx", L"\\a\\one.txt", L"\\a\\skip.dll", L"\\a\\hidden.sys", L"\\top.txt", L"\\$Recycle.Bin\\bin.txt"}) DeleteFileW((root + n).c_str());
    for (auto n : {L"\\a\\b", L"\\a", L"\\$Recycle.Bin", L""}) RemoveDirectoryW((root + n).c_str());
    DeleteFileW(path.c_str());
    detail = std::to_wstring(b.Count()) + L" entries saved, mapped and searched"; return opened && got == want && found;
}

// --indexquery "words" file: runs one search on the saved index and writes the best matches with their scores and the time taken (a developer check).
void FileIndexQuery(const ws& q, const ws& file) {
    LoadNewest(); std::shared_ptr<const Index> ix; { std::lock_guard<std::mutex> l(g_mu); ix = g_ix; }
    std::string out; if (!ix) { WriteAtomic(file, "no index"); return; }
    uint64_t t = Now(); ws qq = Trim(q); auto hits = Rank(*ix, FoldName(qq.c_str(), qq.size()), 400, [] { return false; }); uint64_t ms = Now() - t;
    out += Narrow(Fmt(L"%d hits kept, %d ms\n", (int)hits.size(), (int)ms));
    for (size_t i = 0; i < hits.size() && i < 25; i++) out += Narrow(Fmt(L"%6.1f  ", hits[i].s)) + ix->Path(hits[i].i) + "\n";
    WriteAtomic(file, out);
}

void FileIndexMemReport(const ws& file) {
    PROCESS_MEMORY_COUNTERS_EX a{sizeof a}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&a, sizeof a);
    uint64_t t = Now(); LoadNewest(); uint64_t ms = Now() - t;
    std::shared_ptr<const Index> ix; { std::lock_guard<std::mutex> l(g_mu); ix = g_ix; }
    uint64_t t2 = Now(); size_t hits = 0; if (ix) hits = Rank(*ix, "readme", 400, [] { return false; }).size(); uint64_t ms2 = Now() - t2;   // one search over everything, for the timing
    PROCESS_MEMORY_COUNTERS_EX b{sizeof b}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&b, sizeof b);
    WriteAtomic(file, Narrow(Fmt(L"entries %d, mapped in %d ms, file %d MB, one search %d ms (%d hits kept), private memory +%d MB, working set +%d MB", ix ? (int)ix->count : 0, (int)ms, ix ? (int)(ix->bytes >> 20) : 0, (int)ms2, (int)hits,
        (int)((b.PrivateUsage - a.PrivateUsage) >> 20), (int)((b.WorkingSetSize - a.WorkingSetSize) >> 20))));
}
