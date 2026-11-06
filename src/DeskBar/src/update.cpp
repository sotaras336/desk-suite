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

std::mutex g_mu; bool g_newer = false; ws g_status, g_known; std::atomic<bool> g_busy{false}; std::function<void()> g_listener;
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

// the whole check, on the calling thread: true = the newest release holds a different build of DeskBar
bool DoCheck(ws& status) {
    std::string sums;
    if (!Get(Base() + L"SHA256SUMS.txt", sums, 65536)) { status = L"Couldn't reach GitHub. Try again later."; return false; }
    ws want = UpdateHashFor(sums, kAsset); if (want.empty()) { status = L"Couldn't read the release list."; return false; }
    { std::lock_guard<std::mutex> l(g_mu); g_known = want; }
    ws me = SelfHash(); bool newer = !me.empty() && want != me;
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
    if (S.updateCheck && (Testing() || now - S.updateLast >= 20 * 3600)) UpdateCheck();
    if (!Testing()) UiOnce(6u * 3600 * 1000, Tick);
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
bool UpdateNewer() { std::lock_guard<std::mutex> l(g_mu); return g_newer; }
bool UpdateBusy() { return g_busy; }
ws UpdateStatus() { std::lock_guard<std::mutex> l(g_mu); return g_status; }
void UpdateListen(std::function<void()> f) { g_listener = std::move(f); }

void UpdateCheck() {
    if (g_busy.exchange(true)) return; SetStatus(L"Checking…"); Changed();
    std::thread([] {
        ws status; bool newer = DoCheck(status); bool ok = !ContainsI(status, L"Couldn't");
        if (ok) { ws k; { std::lock_guard<std::mutex> l(g_mu); k = g_known; } Persist(k); }
        SetStatus(status, ok ? newer : -1); g_busy = false; Changed();
    }).detach();
}
void UpdateNow() {
    if (!UpdateNewer() || g_busy.exchange(true)) return; SetStatus(L"Downloading…"); Changed();
    std::thread([] {
        ws status = L"Updating…", path; bool ok = DoDownload(status, path);
        if (ok && !Testing()) StartSetup(path);
        SetStatus(status); g_busy = false; Changed();
    }).detach();
}
void UpdateStart() {   // UI thread, from the deferred startup block: remember what the last check found, then check quietly about once a day
    ws known = S.updateKnown;
    if (!known.empty()) { { std::lock_guard<std::mutex> l(g_mu); g_known = known; } std::thread([known] { ws me = SelfHash(); if (!me.empty() && me != known) { SetStatus(L"A new version is available.", 1); Changed(); } }).detach(); }
    UiOnce(Testing() ? 500 : 30000, Tick);
}
// --updatetest <report>: check (and download + verify, never run) against DESK_UPDATE_BASE, and write what happened
int UpdateTest(const ws& report) {
    ws st, path, out; bool newer = DoCheck(st); out += Fmt(L"newer=%d status=%s\r\n", newer ? 1 : 0, st.c_str());
    if (newer) { ws st2; bool ok = DoDownload(st2, path); out += Fmt(L"download=%d status=%s path=%s\r\n", ok ? 1 : 0, st2.c_str(), path.c_str()); }
    WriteAtomic(report, Narrow(out)); return 0;
}
