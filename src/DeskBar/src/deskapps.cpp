#include "pch.h"
#include "app.h"

// Desk 4.0: the suite in one place.
// - Colours, in %LOCALAPPDATA%\Desk: theme.txt is the theme every app wears; theme-<app>.txt (deskbar, deskmode, deskdrawer, desktune), when present, is that
//   app's own colour ("Same colour for every app" off in Settings > Colours). DeskDrawer switches live; DeskBar (and DeskMode, inside it) restarts to wear a
//   new colour, as it always has, and the settings window comes back on the page it was on.
// - One installer: Settings > Desk apps installs and updates DeskDrawer and DeskTune from the same GitHub release DeskBar updates from, each file
//   checked against the release's SHA256SUMS before it runs (the same download as DeskBar's own update).

static ws DeskDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\Desk"; }
static ws ThemePath(const ws& app) { return DeskDir() + (app.empty() ? ws(L"\\theme.txt") : L"\\theme-" + app + L".txt"); }
static ws ReadId(const ws& path) { std::string d; if (!ReadAll(path, d)) return {}; ws t = Trim(Widen(d)); return t.size() < 200 ? t : ws(); }
static void WriteId(const ws& path, const ws& id) { if (id.empty() || ReadId(path) == id) return; CreateDirectoryW(DeskDir().c_str(), nullptr); WriteAtomic(path, Narrow(id)); }
const wchar_t* const kColourApps[] = {L"deskbar", L"deskmode", L"deskdrawer", L"desktune"};
bool ThemesSeparate() { for (auto* a : kColourApps) if (FileExists(ThemePath(a))) return true; return false; }
ws ThemeOf(const ws& app) { ws own = app.empty() ? ws() : ReadId(ThemePath(app)); return own.empty() ? ReadId(ThemePath(L"")) : own; }   // "" = the suite's
void ThemeSet(const ws& app, const ws& id) { WriteId(ThemePath(ThemesSeparate() ? app : ws()), id); }
void ThemesSetSeparate(bool on) {   // on: every app starts from the suite's colour, then each can change; off: one colour again (the bar's)
    ws all = ThemeOf(L""); if (all.empty()) all = S.theme.empty() ? ThemeIdNow() : S.theme;
    if (on) { for (auto* a : kColourApps) WriteId(ThemePath(a), all); }
    else { ws bar = ThemeOf(L"deskbar"); WriteId(ThemePath(L""), bar.empty() ? all : bar); for (auto* a : kColourApps) DeleteFileW(ThemePath(a).c_str()); }
}
ws SharedThemeRead() { return ThemeOf(L"deskbar"); }
void SharedThemeWrite(const ws& id) { ThemeSet(L"deskbar", id); }   // a colour picked in Quick settings
void SharedThemeTick() {   // UI thread, every 2 s: DeskBar's or DeskMode's colour changed elsewhere (Settings > Colours, DeskDrawer) -> restart to wear it
    static bool started = false;
    if (!started) {
        started = true; if (!FileExists(ThemePath(L""))) WriteId(ThemePath(L""), S.theme.empty() ? ThemeIdNow() : S.theme);   // no suite theme yet: DeskBar's becomes it
        SetDeskModeTheme(ThemeOf(L"deskmode")); return;
    }
    ws bar = ThemeOf(L"deskbar"), dm = ThemeOf(L"deskmode");
    static ws dmSeen = dm; bool barChanged = !bar.empty() && bar != S.theme, dmChanged = dm != dmSeen;
    if (!barChanged && !dmChanged) return;
    Log(L"theme: DeskBar %s, DeskMode %s: restarting to wear them", bar.c_str(), dm.c_str());
    if (!bar.empty()) S.theme = bar; SaveSettingsNow(); SettingsReopenAfterRestart(); AppQuit(true);
}

// ---------- DeskDrawer's settings, from the Desk settings window ----------
// Read from DeskDrawer's own settings file; changed through DeskDrawer itself (its pipe when it runs, "DeskDrawer.exe --set" when it doesn't), so each
// switch does exactly what DeskDrawer's own switch does (folders handed to it, sign-in start...).
Json DeskDrawerSettings() { std::string d; Json j = Json::O(); if (ReadAll(KnownFolder(FOLDERID_LocalAppData) + L"\\DeskDrawer\\settings.json", d)) j = Json::Parse(d); return j; }
bool DeskDrawerIsDefault() {
    wchar_t v[64] = L""; DWORD n = sizeof v; return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell", nullptr, RRF_RT_REG_SZ, nullptr, v, &n) == ERROR_SUCCESS && ContainsI(v, L"DeskDrawer");
}
void DeskDrawerSet(const ws& key, const ws& value) {
    ws line = L"--set " + key + L"=" + value;
    DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid); wchar_t user[256] = L""; DWORD un = 256; GetUserNameW(user, &un);
    ws pipe = Fmt(L"\\\\.\\pipe\\DeskDrawer-%s-%lu", user, sid);
    HANDLE h = CreateFileW(pipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) { std::string u = Narrow(line); DWORD w; WriteFile(h, u.data(), (DWORD)u.size(), &w, nullptr); CloseHandle(h); return; }
    ws exe = DeskAppExe(L"DeskDrawer"); if (FileExists(exe)) Launch(exe, L"--set " + key + L"=" + value);
}

namespace {
struct DeskApp { const wchar_t* name; const wchar_t* setup; const wchar_t* exe; const wchar_t* what; wchar_t glyph; };
const DeskApp kApps[] = {
    {L"DeskDrawer", L"DeskDrawer-Setup.exe", L"DeskDrawer.exe", L"Files: tabs, a preview pane, Space for a quick look, fast search.", 0xEC50},
    {L"DeskTune", L"DeskTune-Setup.exe", L"DeskTune.exe", L"Makes Windows lighter. Every change can be undone.", 0xE9F5},
};
const ws kLatest = L"https://github.com/sotaras336/desk-suite/releases/latest/download/";
std::mutex g_mu; std::map<ws, ws> g_state;   // app name -> what the page says about it ("" = not looked yet)
std::atomic<bool> g_busy{false};
}
ws DeskAppExe(const ws& name) { for (auto& a : kApps) if (name == a.name) return KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\" + a.name + L"\\" + a.exe; return {}; }
ws DeskAppState(const ws& name) { std::lock_guard<std::mutex> l(g_mu); auto it = g_state.find(name); return it == g_state.end() ? ws() : it->second; }
static void SetState(const ws& name, const ws& s) { { std::lock_guard<std::mutex> l(g_mu); g_state[name] = s; } UiPost([] { DeskAppsChanged(); }); }

// When the installed DeskDrawer / DeskTune was built, as it wrote it into %LOCALAPPDATA%\<app>\update.json ("Built"); empty for builds older than this
static ws InstalledBuilt(const ws& name) {
    std::string d; if (!ReadAll(KnownFolder(FOLDERID_LocalAppData) + L"\\" + name + L"\\update.json", d)) return {};
    Json j = Json::Parse(d); const Json* b = j.Find(L"Built"); return b && b->t == Json::Str ? b->s : ws();
}
// Looks at the newest release (worker thread): for each app, installed or not, and whether the release holds a different build.
void DeskAppsCheck() {
    if (g_busy.exchange(true)) return;
    for (auto& a : kApps) SetState(a.name, FileExists(DeskAppExe(a.name)) ? L"Installed · checking for updates…" : L"Not installed");
    std::thread([] {
        std::string sums; bool got = UpdateGet(kLatest + L"SHA256SUMS.txt", sums, 65536);
        for (auto& a : kApps) {
            ws exe = DeskAppExe(a.name); bool have = FileExists(exe);
            if (!have) { SetState(a.name, L"Not installed"); continue; }
            if (!got) { SetState(a.name, L"Installed · couldn't reach GitHub to look for updates"); continue; }
            std::string d; ws want = UpdateHashFor(sums, a.setup), mine = ReadAll(exe, d) ? Sha256HexOf(d) : ws();
            // a different file is only "new" when the release built it later than the installed one (the app writes its own build time into its update.json)
            ws built = UpdateBuiltFor(sums, a.setup), have = InstalledBuilt(a.name);
            bool ahead = !want.empty() && want != mine && !built.empty() && !have.empty() && built <= have;
            SetState(a.name, want.empty() ? L"Installed" : want == mine ? L"Installed · up to date" : ahead ? L"Installed · newer than the latest release" : L"Installed · a new version is available");
        }
        g_busy = false;
    }).detach();
}
// Downloads the app's setup from the newest release, checks it against SHA256SUMS and runs it (it installs itself and starts). Worker thread.
void DeskAppInstall(const ws& name) {
    const DeskApp* app = nullptr; for (auto& a : kApps) if (name == a.name) app = &a; if (!app || g_busy.exchange(true)) return;
    SetState(name, L"Downloading…");
    ws setup = app->setup;
    std::thread([name, setup] {
        std::string sums, data; ws st;
        if (!UpdateGet(kLatest + L"SHA256SUMS.txt", sums, 65536)) st = L"Couldn't reach GitHub. Try again later.";
        else if (ws want = UpdateHashFor(sums, setup); want.empty()) st = L"This release has no " + setup + L".";
        else if (!UpdateGet(kLatest + setup, data, 400u << 20)) st = L"The download failed. Try again later.";
        else if (Sha256HexOf(data) != want) st = L"The download didn't match its checksum, so it was not run.";
        else {
            wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); ws dir = ws(t) + L"DeskUpdate-" + name; CreateDirectoryW(dir.c_str(), nullptr); ws path = dir + L"\\" + setup;
            if (!WriteAtomic(path, data)) st = L"Couldn't save the download.";
            else { Log(L"desk apps: installing %s from the newest release", name.c_str()); Launch(path); st = L"Installing… it opens when it's done."; }
        }
        g_busy = false; SetState(name, st);
        if (ContainsI(st, L"Installing")) UiOnce(8000, [] { DeskAppsCheck(); });   // look again once the setup has had time to finish
    }).detach();
}
int DeskAppCount() { return (int)(sizeof kApps / sizeof kApps[0]); }
ws DeskAppName(int i) { return kApps[i].name; }
ws DeskAppWhat(int i) { return kApps[i].what; }
wchar_t DeskAppGlyph(int i) { return kApps[i].glyph; }
