#include "pch.h"
#include "search.h"
#include <condition_variable>

// An in-memory list of every file and folder name on the fixed drives, searched in a few milliseconds. Built on a background-priority
// thread (CPU and disk) well after start-up, saved to fileindex.txt so the next start has it, and loaded 15 s after start so it never
// competes with the bar for disk and memory (that made the old cold start take 28 s). Names live in two UTF-8 arenas, not 800k strings.
namespace {
struct Snap {
    // Each entry is its own name plus the index of its parent folder (or of a root path): full paths are rebuilt only for the results shown.
    // Storing every full path took ~110 MB for 800k entries; names + parent links take about a third of that.
    std::string names, lower; std::vector<uint32_t> noff, loff, parent; std::vector<uint8_t> dir; std::vector<std::string> roots;
    std::unordered_map<std::string, uint32_t> folders;   // while building only: folder path -> its entry (or root) id; dropped by Seal()
    static constexpr uint32_t kRoot = 0x80000000u;
    size_t Count() const { return dir.size(); }
    void Add(const ws& full, const ws& name, bool d) {
        std::string f = Narrow(full); size_t cut = f.rfind('\\'); std::string up = cut == std::string::npos ? std::string() : f.substr(0, cut);
        uint32_t par; auto it = folders.find(up);
        if (it != folders.end()) par = it->second; else { par = kRoot | (uint32_t)roots.size(); roots.push_back(up); folders.emplace(up, par); }
        uint32_t me = (uint32_t)dir.size();
        noff.push_back((uint32_t)names.size()); names += Narrow(name); loff.push_back((uint32_t)lower.size());
        ws l = name; CharLowerBuffW(l.data(), (DWORD)l.size()); lower += Narrow(l); dir.push_back(d); parent.push_back(par);
        if (d) folders.emplace(std::move(f), me);
    }
    void Seal() { noff.push_back((uint32_t)names.size()); loff.push_back((uint32_t)lower.size()); std::unordered_map<std::string, uint32_t>().swap(folders); for (auto* v : {&noff, &loff, &parent}) v->shrink_to_fit(); names.shrink_to_fit(); lower.shrink_to_fit(); dir.shrink_to_fit(); HeapCompact(GetProcessHeap(), 0); }   // sentinel end offsets
    // An entry ends where the next one starts; the last one ends at the end of the text. Seal() adds that end as a sentinel, but a snapshot that is
    // still being built has none yet: reading noff[i + 1] there ran past the vector and crashed the bar after every rescan.
    std::string_view Name(size_t i) const { size_t e = i + 1 < noff.size() ? noff[i + 1] : names.size(); return {names.data() + noff[i], e - noff[i]}; }
    std::string_view Low(size_t i) const { size_t e = i + 1 < loff.size() ? loff[i + 1] : lower.size(); return {lower.data() + loff[i], e - loff[i]}; }
    void PathInto(size_t i, std::string& out) const {
        uint32_t p = parent[i];
        if (p & kRoot) out = roots[p & ~kRoot]; else PathInto(p, out);
        out += '\\'; out.append(Name(i));
    }
    std::string Path(size_t i) const { std::string out; PathInto(i, out); return out; }
    int Depth(size_t i) const { int n = 1; uint32_t p = parent[i]; while (!(p & kRoot)) { n++; p = parent[p]; } for (char c : roots[p & ~kRoot]) n += c == '\\'; return n; }
};
std::shared_ptr<const Snap> g_snap; std::mutex g_mu;
const std::set<ws>& SkipDirs() { static const std::set<ws> s = [] { std::set<ws> r; for (auto n : {L"node_modules", L".git", L"__pycache__", L"$recycle.bin", L"winsxs", L".gradle", L".nuget", L".vs", L".cache", L"system volume information", L"windows", L"$windows.~bt", L"recovery", L"perflogs", L"driverstore", L".venv", L"venv", L".idea", L".m2", L"site-packages"}) r.insert(n); return r; }(); return s; }
const std::set<ws>& Junctions() { static const std::set<ws> s = [] { std::set<ws> r; for (auto n : {L"application data", L"local settings", L"my documents", L"cookies", L"nethood", L"printhood", L"recent", L"sendto", L"start menu", L"templates", L"all users", L"default user", L"default"}) r.insert(n); return r; }(); return s; }
const std::set<ws>& AppNoise() { static const std::set<ws> s = [] { std::set<ws> r; for (auto n : {L"temp", L"cache", L"caches", L"code cache", L"gpucache", L"packages", L"inetcache", L"crashdumps", L"d3dscache", L"microsoft", L"history", L"logs", L"crashpad", L"service worker", L"indexeddb", L"local storage", L"session storage", L"blob_storage"}) r.insert(n); return r; }(); return s; }
const std::set<ws>& SkipExt() { static const std::set<ws> s = [] { std::set<ws> r; for (auto n : {L".dll", L".pyc", L".tmp", L".mui", L".mum", L".cat", L".etl", L".sys", L".pdb", L".lib", L".obj", L".class", L".map"}) r.insert(n); return r; }(); return s; }

ws CachePath() { return DataPath(L"fileindex.txt"); }
std::string CacheText(const Snap& s) {   // one line per entry: 'd' or 'f' and the full path
    std::string out, p; out.reserve(s.names.size() * 4 + s.Count() * 2);
    for (size_t i = 0; i < s.Count(); i++) { out += s.dir[i] ? 'd' : 'f'; s.PathInto(i, p); out += p; out += '\n'; }
    return out;
}
void LoadCache() {   // streamed in 1 MB chunks: reading the whole 90 MB file into one string doubled the peak memory
    HANDLE h = CreateFileW(CachePath().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr); if (h == INVALID_HANDLE_VALUE) return;
    auto s = std::make_shared<Snap>(); std::string buf, carry; buf.resize(1 << 20); DWORD got = 0;
    auto line = [&](const std::string& l) { if (l.size() < 2) return; ws full = Widen(l.substr(1)); s->Add(full, FileNameOf(full), l[0] == 'd'); };
    while (ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr) && got) {
        size_t i = 0; while (i < got) { size_t e = buf.find('\n', i); if (e == std::string::npos || e >= got) { carry.append(buf, i, got - i); break; } carry.append(buf, i, e - i); if (!carry.empty() && carry.back() == '\r') carry.pop_back(); line(carry); carry.clear(); i = e + 1; }
    }
    if (!carry.empty()) line(carry);
    CloseHandle(h); size_t n = s->Count(); s->Seal(); { std::lock_guard<std::mutex> l(g_mu); g_snap = s; } Log(L"file index: %d entries from cache", (int)n);
}
bool IsCloudRoot(const ws& root) {
    wchar_t r[4] = {root[0], L':', L'\\', 0}; wchar_t label[128] = {}; if (!GetVolumeInformationW(r, label, 128, nullptr, nullptr, nullptr, nullptr, 0)) return false;
    return ContainsI(label, L"Google Drive") || ContainsI(label, L"OneDrive");
}
std::vector<ws> Roots() {
    std::vector<ws> v; v.push_back(KnownFolder(FOLDERID_Profile)); v.push_back(KnownFolder(FOLDERID_ProgramData));
    v.push_back(KnownFolder(FOLDERID_ProgramFiles)); v.push_back(KnownFolder(FOLDERID_ProgramFilesX86));
    ws sys = KnownFolder(FOLDERID_Windows).substr(0, 3);
    wchar_t drives[256]; GetLogicalDriveStringsW(256, drives);
    for (wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        if (GetDriveTypeW(d) != DRIVE_FIXED) continue; ws dr = d;
        if (EqI(dr, sys)) {   // the system drive: your own folders at the top (Dev, Games...), not Windows or Users
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dr + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
            if (h != INVALID_HANDLE_VALUE) { do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                ws n = Lower(fd.cFileName); if (SkipDirs().count(n) || n == L"users" || StartsI(n, L"program files")) continue; v.push_back(dr + fd.cFileName); } while (FindNextFileW(h, &fd)); FindClose(h); }
        } else v.push_back(dr);
    }
    return v;
}
void Walk(const ws& root, uint64_t budgetMs, Snap& out, size_t cap) {
    std::vector<ws> stack{root}; uint64_t t0 = Now(); unsigned tick = 0;
    while (!stack.empty()) {
        ws dir = stack.back(); stack.pop_back();
        int depth = 0; for (wchar_t c : dir) if (c == L'\\') depth++;
        ws ld = Lower(dir); bool inApp = ld.find(L"\\appdata\\") != ws::npos || ld.find(L"\\programdata") != ws::npos; int rel = 0;
        if (inApp) { size_t a = ld.find(L"\\appdata\\"); if (a == ws::npos) a = ld.find(L"\\programdata"); for (size_t k = a + 1; k < ld.size(); k++) if (ld[k] == L'\\') rel++; }
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileExW((dir + (dir.back() == L'\\' ? L"*" : L"\\*")).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.cFileName[0] == L'.' && (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) continue;
            ws name = fd.cFileName, ln = Lower(name); ws full = dir + (dir.back() == L'\\' ? L"" : L"\\") + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (SkipDirs().count(ln)) continue;
                out.Add(full, name, true);
                bool follow = true;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { DWORD tag = fd.dwReserved0; if (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT) follow = false; }   // links loop; cloud folders are fine
                if (Junctions().count(ln) || depth >= 18) follow = false;
                if (inApp && (AppNoise().count(ln) || rel >= 3)) follow = false;       // AppData\Local\Programs\Sudachi yes, caches and logs never
                if (follow) stack.push_back(full);
            } else {
                if (SkipExt().count(ExtOf(name))) continue; out.Add(full, name, false);
            }
            if (out.Count() > cap || ((++tick & 0x3F) == 0 && Now() - t0 > budgetMs)) { FindClose(h); return; }     // never let one huge folder hold everything up
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}
// The scan: walks the fixed drives and saves fileindex.txt. It runs in its OWN process ("DeskBar.exe --buildindex", started by SpawnScan) at background
// priority for CPU, disk and memory. Inside the bar it competed with the window thread for the heap lock, and a crash in it took the bar down with it.
int FileIndexBuildMain() {
    SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
    uint64_t t0 = Now(); Snap s; std::vector<ws> cloud, normal; for (auto& r : Roots()) (IsCloudRoot(r) ? cloud : normal).push_back(r);
    auto run = [&](const ws& r, bool c) { size_t before = s.Count(); uint64_t t = Now(); Walk(r, c ? 60000 : 40000, s, 2500000); Log(L"file index root %s: %d entries, %d ms", r.c_str(), (int)(s.Count() - before), (int)(Now() - t)); };
    for (auto& r : normal) run(r, false);
    for (auto& r : cloud) run(r, true);
    Log(L"file index: %d entries in %d ms", (int)s.Count(), (int)(Now() - t0));
    s.Seal(); std::string out = CacheText(s); WriteAtomic(CachePath(), out);
    return 0;
}
void SpawnScan() {
    static std::atomic<bool> running{false}; if (running.exchange(true)) return;
    std::thread([] {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); ws cmd = L"\"" + ws(exe) + L"\" --buildindex";
        STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; DWORD code = (DWORD)-1;
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
            if (WaitForSingleObject(pi.hProcess, 30 * 60 * 1000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code); else TerminateProcess(pi.hProcess, 1);   // our own child, by handle
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        } else Log(L"file index: could not start the scan (%lu)", GetLastError());
        if (code == 0) LoadCache(); else Log(L"file index: the scan ended with %lu, keeping the old list", code);
        running = false;
    }).detach();
}

// ---- search worker: latest request wins ----
struct Req { ws q; uint64_t id; std::function<void(std::vector<Result>)> cb; };
std::mutex g_rmu; std::condition_variable g_rcv; Req g_req; std::atomic<uint64_t> g_latest{0}; bool g_haveReq = false;
bool IsWordStart(const std::string& n, const std::string& t) { size_t i = 0; while ((i = n.find(t, i)) != std::string::npos) { if (i == 0 || !isalnum((unsigned char)n[i - 1])) return true; i++; } return false; }
void Worker() {
    for (;;) {
        Req r; { std::unique_lock<std::mutex> l(g_rmu); g_rcv.wait(l, [] { return g_haveReq; }); r = std::move(g_req); g_haveReq = false; }
        std::shared_ptr<const Snap> s; { std::lock_guard<std::mutex> l(g_mu); s = g_snap; }
        std::vector<Result> out; if (!s) { UiPost([cb = r.cb] { cb({}); }); continue; }
        ws ql = Trim(r.q); CharLowerBuffW(ql.data(), (DWORD)ql.size()); std::vector<std::string> toks; for (auto& t : Split(ql, L' ')) if (!t.empty()) toks.push_back(Narrow(t));
        if (toks.empty() || ql.size() < 2) { UiPost([cb = r.cb] { cb({}); }); continue; }
        std::string qs; for (auto& t : toks) qs += (qs.empty() ? "" : " ") + t;
        std::vector<size_t> hits;
        for (size_t i = 0; i < s->Count(); i++) {
            if ((i & 0x3FFF) == 0 && r.id != g_latest) break;
            auto n = s->Low(i); bool all = true; for (auto& t : toks) if (n.find(t) == std::string_view::npos) { all = false; break; }
            if (all) { hits.push_back(i); if (hits.size() >= 6000) break; }
        }
        if (r.id != g_latest) continue;
        struct Sc { size_t i; double s; }; std::vector<Sc> sc;
        for (size_t i : hits) {
            std::string name(s->Low(i)); bool d = s->dir[i]; std::string stem = name; if (!d) { size_t dot = stem.rfind('.'); if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot); }
            double v = stem == qs ? 120 : name.compare(0, toks[0].size(), toks[0]) == 0 ? 100 : IsWordStart(name, toks[0]) ? 85 : 55;
            int depth = s->Depth(i);
            v += d ? 3 : 0; v -= std::min(12.0, depth * 0.8) + std::min(10.0, name.size() * 0.08); sc.push_back({i, v});
        }
        std::sort(sc.begin(), sc.end(), [](const Sc& a, const Sc& b) { return a.s > b.s; }); if (sc.size() > 30) sc.resize(30);
        for (auto& x : sc) {
            ws path = Widen(s->Path(x.i)); bool d = s->dir[x.i]; ws sub = Shorten(DirOf(path));
            if (!d) { WIN32_FILE_ATTRIBUTE_DATA fa{}; if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) sub += L"  ·  " + Ago(fa.ftLastWriteTime); }
            out.push_back({d ? RKind::Folder : RKind::File, FileNameOf(path), sub, path, x.s * 0.7});
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
    std::thread([] { Sleep(30000); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); LoadCache(); }).detach();   // after the boot rush; not "background" mode, whose slow page-ins can hold up a thread that shares the heap with the window
    WIN32_FILE_ATTRIBUTE_DATA fa{}; bool fresh = false;
    if (GetFileAttributesExW(CachePath().c_str(), GetFileExInfoStandard, &fa)) { FILETIME n; GetSystemTimeAsFileTime(&n); ULARGE_INTEGER a, b; a.LowPart = n.dwLowDateTime; a.HighPart = n.dwHighDateTime; b.LowPart = fa.ftLastWriteTime.dwLowDateTime; b.HighPart = fa.ftLastWriteTime.dwHighDateTime; fresh = (a.QuadPart - b.QuadPart) < 6ull * 3600 * 10000000; }
    // The scan reads the whole disk: it runs as a low-priority process, only when the saved list is old, and rarely.
    UiOnce(fresh ? 30 * 60000 : 3 * 60000, [] { SpawnScan(); UiTimer(3 * 3600 * 1000, [] { SpawnScan(); }); });
}

bool FileIndexSelfTest(ws& detail) {
    Snap s;   // the order a walk produces: each folder before what is inside it
    const wchar_t* items[][2] = {{L"C:\\Users\\me\\Docs", L"d"}, {L"C:\\Users\\me\\Docs\\a.txt", L"f"}, {L"C:\\Users\\me\\Docs\\Sub", L"d"},
                                 {L"C:\\Users\\me\\Docs\\Sub\\b.pdf", L"f"}, {L"D:\\Games", L"d"}, {L"D:\\Games\\x.exe", L"f"}, {L"C:\\Users\\me\\top.txt", L"f"}};
    for (auto& it : items) { ws full = it[0]; s.Add(full, FileNameOf(full), it[1][0] == L'd'); }
    s.Seal();
    for (size_t i = 0; i < s.Count(); i++) {
        ws got = Widen(s.Path(i)); int depth = 0; for (wchar_t c : ws(items[i][0])) depth += c == L'\\';
        if (got != items[i][0] || s.Depth(i) != depth) { detail = got + L" (depth " + std::to_wstring(s.Depth(i)) + L")"; return false; }
    }
    detail = std::to_wstring(s.Count()) + L" entries, " + std::to_wstring(s.roots.size()) + L" roots"; return s.folders.empty();
}

// A real scan of a small folder, saved the way Build() saves it. The first compressed index read one entry past the end here (the end marker was only
// added after saving) and crashed the bar after every rescan; this runs that exact path, once with the snapshot still open and once sealed.
bool FileIndexScanSelfTest(ws& detail) {
    ws root = KnownFolder(FOLDERID_Profile) + L"\\db3-index-scan";   // not in %TEMP%: a scan deliberately doesn't go deep into AppData
    SHCreateDirectoryExW(nullptr, (root + L"\\a\\b").c_str(), nullptr);
    { std::string z = "x"; WriteAtomic(root + L"\\top.txt", z); WriteAtomic(root + L"\\a\\one.txt", z); WriteAtomic(root + L"\\a\\b\\deep.docx", z); WriteAtomic(root + L"\\a\\skip.dll", z); }
    Snap s; Walk(root, 5000, s, 1000);
    auto lines = [](const std::string& text) { std::set<std::string> r; size_t i = 0; while (i < text.size()) { size_t e = text.find('\n', i); r.insert(text.substr(i, e - i)); i = e + 1; } return r; };
    std::string r8 = Narrow(root), open = CacheText(s);
    std::set<std::string> want{"d" + r8 + "\\a", "d" + r8 + "\\a\\b", "f" + r8 + "\\top.txt", "f" + r8 + "\\a\\one.txt", "f" + r8 + "\\a\\b\\deep.docx"};   // .dll is never indexed
    s.Seal(); std::string sealed = CacheText(s);
    for (auto n : {L"\\a\\b\\deep.docx", L"\\a\\one.txt", L"\\a\\skip.dll", L"\\top.txt"}) DeleteFileW((root + n).c_str());
    RemoveDirectoryW((root + L"\\a\\b").c_str()); RemoveDirectoryW((root + L"\\a").c_str()); RemoveDirectoryW(root.c_str());
    detail = std::to_wstring(s.Count()) + L" entries saved before and after sealing";
    return lines(open) == want && lines(sealed) == want;
}

void FileIndexMemReport(const ws& file) {
    PROCESS_MEMORY_COUNTERS_EX a{sizeof a}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&a, sizeof a);
    uint64_t t = Now(); LoadCache(); uint64_t ms = Now() - t;
    PROCESS_MEMORY_COUNTERS_EX b{sizeof b}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&b, sizeof b);
    std::shared_ptr<const Snap> s; { std::lock_guard<std::mutex> l(g_mu); s = g_snap; }
    WriteAtomic(file, Narrow(Fmt(L"entries %d, loaded in %d ms, index memory %d MB (process %d MB)", s ? (int)s->Count() : 0, (int)ms, (int)((b.PrivateUsage - a.PrivateUsage) >> 20), (int)(b.PrivateUsage >> 20))));
}
