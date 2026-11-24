#include "pch.h"
#include "app.h"
#include <winhttp.h>
#include <bcrypt.h>

// Self-update, the same idea as Updater.cs in the other Desk apps: about once a day (and when asked) read SHA256SUMS.txt from the newest release of the Desk suite on GitHub.
// If the checksum listed for DeskBar-Setup.exe differs from the checksum of the running exe, a different build is out. "Update now" downloads that setup file, checks it against
// the listed checksum and runs it (DeskBar-Setup.exe --yes: it stops the running copy, replaces it and starts the new one). Only a plain download request leaves the PC.
namespace {
const wchar_t* kAsset = L"DeskBar-Setup.exe";
const wchar_t* kLatest = L"https://github.com/sotaras336/desk-suite/releases/latest/download/";

ws Base() {   // tests point DESK_UPDATE_BASE at a local server; anything but loopback is ignored
    wchar_t b[300]; DWORD n = GetEnvironmentVariableW(L"DESK_UPDATE_BASE", b, 300);
    if (n && n < 300) { ws v = b; if (StartsI(v, L"http://127.0.0.1") || StartsI(v, L"http://localhost")) return v; }
    return kLatest;
}
bool Testing() { return Base() != kLatest; }

std::mutex g_mu; bool g_newer = false; ws g_status, g_known; std::atomic<bool> g_busy{false}, g_recheck{false}; std::function<void()> g_listener;   // g_recheck: look again soon (the remembered build differs from this one, or the last look failed)
void SetStatus(const ws& s, int newer = -1) { std::lock_guard<std::mutex> l(g_mu); g_status = s; if (newer >= 0) g_newer = newer != 0; }
void Changed() { UiPost([] { if (g_listener) g_listener(); }); }

// one GET with WinHTTP; follows GitHub's redirect to the release CDN. Capped, so a wrong answer can't fill the memory.
bool Get(const ws& url, std::string& out, size_t cap) {
    wchar_t host[256], path[2048]; URL_COMPONENTS uc{sizeof uc}; uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    HINTERNET s = WinHttpOpen(L"Desk-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0); if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 20000, 30000);
    bool ok = false; HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0), r = nullptr;
    if (c) r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, sz = sizeof code; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
        if (code == 200) {
            ok = true; char b[16384]; DWORD n;
            while (WinHttpReadData(r, b, sizeof b, &n) && n) { if (out.size() + n > cap) { ok = false; break; } out.append(b, n); }
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s); return ok;
}

ws SelfHash() { wchar_t m[MAX_PATH]; GetModuleFileNameW(nullptr, m, MAX_PATH); std::string d; return ReadAll(m, d) ? Sha256HexOf(d) : ws(); }

// "Different" is not "newer": a build made on the PC after the release (a fix not published yet) was offered the older release as "a new version",
// and Update now put the old build back. The release list now says when each file was built ("# built DeskBar-Setup.exe 20261125T101500Z"), and this
// exe knows when it was linked (the PE header's time stamp, UTC). A release only counts as newer when it was built later. No "built" line: as before.
ws SelfBuilt() {
    auto* dos = (const IMAGE_DOS_HEADER*)GetModuleHandleW(nullptr); if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return {};
    auto* nt = (const IMAGE_NT_HEADERS*)((const BYTE*)dos + dos->e_lfanew); if (nt->Signature != IMAGE_NT_SIGNATURE) return {};
    return UpdateStamp((int64_t)nt->FileHeader.TimeDateStamp);
}

// the whole check, on the calling thread: true = the newest release holds a different build of DeskBar
bool DoCheck(ws& status) {
    std::string sums;
    if (!Get(Base() + L"SHA256SUMS.txt", sums, 65536)) { status = L"Couldn't reach GitHub. Try again later."; return false; }
    ws want = UpdateHashFor(sums, kAsset); if (want.empty()) { status = L"Couldn't read the release list."; return false; }
    { std::lock_guard<std::mutex> l(g_mu); g_known = want; }
    ws me = SelfHash(); bool newer = !me.empty() && want != me;
    ws built = UpdateBuiltFor(sums, kAsset), mine = SelfBuilt();
    if (newer && UpdateReleaseNotNewer(built, mine)) { status = L"You have a newer build than the latest release."; return false; }
    status = newer ? L"A new version is available." : L"You have the latest version."; return newer;
}
// download + verify, on the calling thread; true = the checked setup file is at `path`
bool DoDownload(ws& status, ws& path) {
    ws want; { std::lock_guard<std::mutex> l(g_mu); want = g_known; }
    std::string data; if (want.empty() || !Get(Base() + kAsset, data, 16u << 20)) { status = L"The download failed. Try again later."; return false; }
    if (Sha256HexOf(data) != want) { status = L"The download didn't match its checksum, so it was not run. Try again later."; return false; }
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); ws dir = ws(t) + L"DeskUpdate-DeskBar"; CreateDirectoryW(dir.c_str(), nullptr);
    path = dir + L"\\" + kAsset;   // "setup" in the name makes the exe install itself
    if (!WriteAtomic(path, data)) { status = L"Couldn't save the download."; return false; }
    return true;
}
void StartSetup(const ws& path) {   // detached and outside any job: the setup stops this very process, which must not take it along
    ws cmd = L"\"" + path + L"\" --yes"; STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
    if (!ok) ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (ok) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } else Log(L"update: couldn't start %s (%lu)", path.c_str(), GetLastError());
}
void Persist(const ws& known) { UiPost([known] { S.updateKnown = known; S.updateLast = (double)(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); SaveSettingsSoon(); }); }
void Tick() {
    double now = (double)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (S.updateCheck && (Testing() || g_recheck || now - S.updateLast >= 20 * 3600)) UpdateCheck();
    if (!Testing()) UiOnce(10u * 60 * 1000, Tick);   // every 10 minutes: a failed look is retried soon, a good one waits its 20 hours
}
}

ws Sha256HexOf(const std::string& d) {
    BCRYPT_ALG_HANDLE alg = nullptr; uint8_t h[32] = {}; ws out;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptHash(alg, nullptr, 0, (PUCHAR)d.data(), (ULONG)d.size(), h, 32) == 0) for (int i = 0; i < 32; i++) out += Fmt(L"%02x", h[i]);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}
ws UpdateHashFor(const std::string& sums, const ws& file) {   // "hash *name" per line
    ws all = Widen(sums), line; size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find(L'\n', p); if (e == ws::npos) e = all.size(); line = all.substr(p, e - p); p = e + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) line.pop_back();
        size_t sp = line.find(L' '); if (sp != 64) continue;
        ws name = line.substr(sp + 1); while (!name.empty() && (name[0] == L' ' || name[0] == L'*')) name.erase(0, 1);
        if (EqI(name, file)) return Lower(line.substr(0, 64));
    }
    return {};
}
// "# built <file> <yyyyMMddTHHmmssZ>" in the release list: when that file was built (empty = the list doesn't say)
ws UpdateBuiltFor(const std::string& sums, const ws& file) {
    ws all = Widen(sums), line; size_t p = 0;
    while (p < all.size()) {
        size_t e = all.find(L'\n', p); if (e == ws::npos) e = all.size(); line = Trim(all.substr(p, e - p)); p = e + 1;
        if (!StartsI(line, L"# built ")) continue;
        ws rest = Trim(line.substr(8)); size_t sp = rest.rfind(L' '); if (sp == ws::npos) continue;
        ws stamp = rest.substr(sp + 1); if (stamp.size() != 16 || stamp[8] != L'T' || stamp[15] != L'Z') continue;
        if (EqI(Trim(rest.substr(0, sp)), file)) return stamp;
    }
    return {};
}
// The one rule for "is the release newer than me", the same in all three apps. A release is only newer when it says it was built later than this copy.
// A release list with no "built" line comes from before 4.0, when builds started carrying their time: every copy that knows its own build time is newer than
// that, so it is never offered (the test builds on the PC were offered the older public release over and over, and Update put the old one back).
// Only a copy that doesn't know its own build time (older than 4.0) still goes by "the file is different".
bool UpdateReleaseNotNewer(const ws& releaseBuilt, const ws& mine) {
    if (mine.empty()) return false;              // no time of our own: different = newer, as before
    if (releaseBuilt.empty()) return true;       // the release doesn't say: it is older than any copy that does
    return releaseBuilt <= mine;                 // same-length stamps: text order is time order
}
ws UpdateStamp(int64_t unixSeconds) {   // seconds since 1970 (UTC) -> "yyyyMMddTHHmmssZ"
    if (unixSeconds <= 0) return {};
    ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)(unixSeconds + 11644473600LL) * 10000000ULL; FILETIME ft{u.LowPart, u.HighPart}; SYSTEMTIME t;
    if (!FileTimeToSystemTime(&ft, &t)) return {};
    return Fmt(L"%04d%02d%02dT%02d%02d%02dZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}
// On the calling thread: when the newest release holds a different DeskBar than this exe, download and verify it. True = the checked setup is at `path`.
bool UpdateGet(const ws& url, std::string& out, size_t cap) { return Get(url, out, cap); }
bool UpdateFetchLatest(ws& path) { ws status; return DoCheck(status) && DoDownload(status, path); }
bool UpdateNewer() { std::lock_guard<std::mutex> l(g_mu); return g_newer; }
bool UpdateBusy() { return g_busy; }
ws UpdateStatus() { std::lock_guard<std::mutex> l(g_mu); return g_status; }
void UpdateListen(std::function<void()> f) { g_listener = std::move(f); }

void UpdateCheck() {
    if (g_busy.exchange(true)) return; SetStatus(L"Checking…"); Changed();
    std::thread([] {
        ws status; bool newer = DoCheck(status); bool ok = !ContainsI(status, L"Couldn't");
        if (ok) { ws k; { std::lock_guard<std::mutex> l(g_mu); k = g_known; } Persist(k); }
        SetStatus(status, ok ? newer : -1); g_recheck = !ok; g_busy = false; Changed();
    }).detach();
}
void UpdateNow() {
    if (g_busy.exchange(true)) return; SetStatus(L"Checking…"); Changed();
    std::thread([] {
        ws status, path; bool newer = DoCheck(status), failed = ContainsI(status, L"Couldn't");   // look again first: the checksum kept from an earlier check can belong to an older release than "latest" serves now
        if (!failed) { ws k; { std::lock_guard<std::mutex> l(g_mu); k = g_known; } Persist(k); }
        if (newer) { SetStatus(L"Downloading…"); Changed(); bool ok = DoDownload(status, path); if (ok && !Testing()) StartSetup(path); if (ok) status = L"Updating…"; }
        else if (!failed) status = L"You already have the latest version.";
        SetStatus(status, failed ? -1 : newer ? 1 : 0); g_busy = false; Changed();
    }).detach();
}
void UpdateStart() {   // UI thread, from the deferred startup block: remember what the last check found, then check quietly about once a day
    ws known = S.updateKnown;
    // The last check saw a different build than this one: a newer release, or this IS the newer build (installed since that check). Only a fresh look can tell,
    // so look again soon instead of saying "a new version is available" (it said so wrongly right after every update).
    if (!known.empty()) { { std::lock_guard<std::mutex> l(g_mu); g_known = known; } std::thread([known] { ws me = SelfHash(); if (!me.empty() && me != known) g_recheck = true; }).detach(); }
    UiOnce(Testing() ? 500 : 30000, Tick);
}
// --updatetest <report>: check (and download + verify, never run) against DESK_UPDATE_BASE, and write what happened
int UpdateTest(const ws& report) {
    ws st, path, out; bool newer = DoCheck(st); out += Fmt(L"newer=%d status=%s\r\n", newer ? 1 : 0, st.c_str());
    if (newer) { ws st2; bool ok = DoDownload(st2, path); out += Fmt(L"download=%d status=%s path=%s\r\n", ok ? 1 : 0, st2.c_str(), path.c_str()); }
    WriteAtomic(report, Narrow(out)); return 0;
}
