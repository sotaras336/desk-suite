#include "pch.h"
#include "bar.h"
#include "quick.h"
#include "search.h"

int RunSupervisor();                      // supervisor.cpp
int RunHooks(DWORD parentPid);            // hooks.cpp
int RunSelfTest(const ws& outFile);       // selftest.cpp
int RunStackOf(DWORD pid, const ws& outPath);   // watchdog.cpp
void WatchdogStart(bool kill); void WatchdogBeat(); void StallForTest(int secs);

static bool g_withTray = false, g_openPreview = false, g_sup = false, g_stall = false; static int g_snapMs = 0; static ws g_snapDir; static Panel g_openPanel = Panel::None; static ws g_page;
static int g_clickX = -1, g_clickY = 0; static int g_exitCode = 0; static bool g_restoreOnExit = true; static uint64_t g_start = 0; static HANDLE g_readyEv = nullptr; static bool g_readySent = false;

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* e) {
    void* at = e->ExceptionRecord->ExceptionAddress; HMODULE mod = nullptr; wchar_t name[MAX_PATH] = L"?";   // which file and where in it: maps straight onto DeskBar.pdb
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)at, &mod)) GetModuleFileNameW(mod, name, MAX_PATH);
    Log(L"CRASH code=%08lx addr=%p in %s +0x%llx", e->ExceptionRecord->ExceptionCode, at, FileNameOf(name).c_str(), (unsigned long long)((uintptr_t)at - (uintptr_t)mod));
    WriteCrashReport(e);                   // crash.txt: the stack with function names; the next start offers to send it (Report a bug)
    if (!g_dev) Explorer::Restore();       // never leave the user without a taskbar if we can't continue (the supervisor will start us again)
    Sleep(300); return EXCEPTION_EXECUTE_HANDLER;
}
void AppQuit(bool restart) { g_exitCode = restart ? 3 : 0; g_restoreOnExit = !restart; PostQuitMessage(0); }

// "Ready" = the real bar has drawn 3 frames AND its pinned-app icons are decoded (max 8 s): the supervisor's stand-in bar then goes away.
static void CheckReady() {
    if (g_readySent || !g_readyEv) return; bool ok = true;
    ws late; int frames = 0;
    for (auto* b : AllBars()) { if (b->frames < 3) ok = false; frames = std::max(frames, b->frames); for (auto* t : b->btns) if (!IconSettled(t->e.iconKey, (int)lroundf(24 * b->scale))) { ok = false; if (late.size() < 300) late += t->e.iconKey + L"; "; } }
    if (ok || Now() - g_start > 8000) {
        g_readySent = true; SetEvent(g_readyEv); Boot(ok ? L"ready signalled" : L"ready signalled (gave up waiting after 8 s)");
        if (!ok) Log(L"startup: still waiting at 8 s for %s", frames < 3 ? L"the bar to draw" : (L"icons: " + late).c_str());   // what made a slow start slow, for the next bug report
    }
}
static void StartSafetyNet() {   // Windows' own Start / Search host must never end up on top of ours
    if (OpenPanel() != Panel::Start) return; HWND fg = GetForegroundWindow(); if (!fg) return; DWORD pid; GetWindowThreadProcessId(fg, &pid); if (pid == GetCurrentProcessId()) return;
    ws exe = Lower(FileNameOf(ExePathOfPid(pid)));
    if (exe == L"startmenuexperiencehost.exe" || exe == L"searchhost.exe" || exe == L"shellhost.exe") { SendKeys({VK_ESCAPE}); if (auto* f = Panel_(Panel::Start)) ForceForeground(f->hwnd); }
}
// Explorer restarting (it crashed, or Task Manager > Restart) used to cost the bar its reserved strip (maximized windows ran under it for a few
// seconds), brought Windows' taskbar back for a moment and moved every tray icon to Windows' hidden tray, where they stayed. Watched by its
// taskbar window: a new one means a new Explorer.
static void ExplorerRestartCheck() {
    static bool init = false; static HWND last = nullptr; HWND t = MainTray();
    if (!init) { init = true; last = t; return; }
    if (t == last) return; HWND was = last; last = t;
    if (!t) { Log(L"Windows Explorer has gone (its taskbar window closed)"); return; }
    Log(L"Windows Explorer restarted (taskbar %p, was %p): hiding its taskbar again, docking the bars again, asking apps for their tray icons", (void*)t, (void*)was);
    UiOnce(1500, [] { Explorer::Hide(); BarsRedock(); TrayAnnounce(); });   // not at once: a just-started Explorer answers slowly, and these wait for it
    UiOnce(6000, [] { Explorer::Hide(); TrayAnnounce(); });   // apps (and Explorer itself) that come back slower
}
// The friendly crash report: once things have settled, and not over a game or a video.
static void CrashOfferLater(int tries) {
    for (auto* b : AllBars()) if (b->hiddenFs) { if (tries < 30) UiOnce(60000, [tries] { CrashOfferLater(tries + 1); }); return; }
    CrashReportCheck();
}
static void AppTick() {
    static int n = 0; n++; WatchdogBeat();
    BarsRebuildIfMonitorsChanged();
    if (!g_dev) Explorer::KeepHidden();
    BarsTick(); CheckReady();
    if (!g_dev && n == 120) TrimMemoryIfIdle();   // once, 30 s after start: the pages only start-up touched. Not again: under memory pressure a trimmed page comes back from disk, and the next Start would stutter
    if (n % 8 == 4) SharedThemeTick();   // another Desk app picked the suite's theme (every 2 s)
    if (n % 2400 == 1200) if (size_t f = IconTrim(400)) Log(L"icons: let go of %zu KB of icons nobody looked at in 10 min", f / 1024);   // every 10 min (250 ms ticks)
    if (n % 4 == 0) { ws req = DataPath(L"snap.req"); if (FileExists(req)) {   // debug: drop a file named snap.req into the data dir to get PNGs of the bars and the open panel
        DeleteFileW(req.c_str()); int i = 0; for (auto* b : AllBars()) b->Snapshot(DataPath(Fmt(L"live_bar%d.png", i++).c_str()));
        if (Panel p = OpenPanel(); p != Panel::None) Panel_(p)->Snapshot(DataPath(L"live_panel.png")); } }
    if (!g_dev && n % 4 == 2) ExplorerRestartCheck();   // once a second
    if (!g_dev) { WinMoveTick(n); StartSafetyNet(); if (n % 12 == 0) BarsWorkAreaUpkeep(); if (n % 8 == 0) DeskModeTick(); }
}

static bool g_openSettings = false; void SettingsSnapshot(const ws& file); void SettingsShowPage(const ws& name);
// shortcut: the 2026-11-23 test builds' liquid animation switched Windows' own minimise animation off for the session (not saved); this puts back
// the saved value at start, so it doesn't stay off until sign-out. Delete once no such build can still be running.
static void RestoreMinAnimate() {
    wchar_t v[8] = L""; DWORD n = sizeof v; ANIMATIONINFO ai{sizeof ai};
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop\\WindowMetrics", L"MinAnimate", RRF_RT_REG_SZ, nullptr, v, &n) != ERROR_SUCCESS || !SystemParametersInfoW(SPI_GETANIMATION, sizeof ai, &ai, 0)) return;
    int want = _wtoi(v); if (ai.iMinAnimate == want) return;
    ai.iMinAnimate = want; SystemParametersInfoW(SPI_SETANIMATION, sizeof ai, &ai, 0); Log(L"minimise animation put back to the saved %d", want);
}
int RunTaskbar(bool real) {
    g_dev = !real; g_start = Now();
    HANDLE mtx = CreateMutexW(nullptr, FALSE, real ? N_INSTANCE : N_INSTANCE L".Dev");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0; (void)mtx;
    SetUnhandledExceptionFilter(CrashFilter);
    OleInitialize(nullptr);
    LogInit(true); Boot(L"ui main"); UiInit(); UiGfxInit(); LoadSettings(); IconInit();
    WatchdogStart(real); Boot(L"settings + gfx ready");
    if (real) { Explorer::Hide(); g_readyEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, N_READY); }
    WinTrackerStart(); WinTrackerOnChange([] { BarsSync(); });
    BarsStart(); Boot(L"bars up");
    if (real || g_withTray) TrayStart();     // a dev copy leaves the tray alone unless asked (it would steal the icons from the running taskbar)
    SysOnChange([] { for (auto* b : AllBars()) b->UpdateStatus(); PanelsSysChanged(); });
    UiTimer(250, AppTick);
    // everything below is deferred: the bar is what you are waiting for
    UiOnce(100, [real] {
        SysStart(); MirrorStart();
        if (real) { if (!g_sup) HooksEnsure(GetCurrentProcessId()); HooksListen(); ClipStart(); WinMoveStart(); RestoreMinAnimate(); DeskModeHotkeysStart(); UiOnce(1500, [] { SettingsReopenCheck(); }); UiOnce(20000, [] { CrashOfferLater(0); }); g_onOpenSettings = SettingsOpenFromApp; GesturesStart(); }
        PrewarmPanels();
        CatalogLoad(); UiOnce(CatalogApps().empty() ? 500 : 4000, [] { CatalogRefresh([] { InvalidateAllWnds(); }); }); UiOnce(10000, ProgramsLoadAsync); FileIndexStart();
        if (!S.pinsImported) {   // first run: start with the pins Windows' taskbar had; a DeskBar that already has pins keeps its own
            if (!S.taskbarPins.empty()) { S.pinsImported = true; SaveSettingsSoon(); }
            else std::thread([] { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); auto v = WindowsTaskbarPins(); CoUninitialize(); UiPost([v] { if (S.taskbarPins.empty()) S.taskbarPins = v; S.pinsImported = true; SaveSettingsSoon(); BarsSync(); }); }).detach();
        }
        if (real || GetEnvironmentVariableW(L"DESK_UPDATE_BASE", nullptr, 0)) UpdateStart();   // a dev copy never asks GitHub (unless a test points it at a local server)
        if (real) std::thread([] { Sleep(60000); EnsureSignInTask(); }).detach();   // sign-in is crowded: look after the first minute
        Boot(L"services started");
    });
    if (g_clickX >= 0) UiOnce(1000, [] { if (AllBars().empty()) return; HWND h = AllBars()[0]->hwnd; LPARAM lp = MAKELPARAM(g_clickX, g_clickY); SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, lp); Sleep(80); SendMessageW(h, WM_LBUTTONUP, 0, lp); });   // debug: --click x,y (device px) on the first bar
    if (g_stall) UiOnce(2000, [] { StallForTest(9); });
    if (g_openPreview) UiOnce(1200, [] { for (auto* b : AllBars()) for (auto* t : b->btns) if (t->e.wins.size()) { PreviewOpen(t->Req(false)); return; } });
    if (g_openPanel != Panel::None) UiOnce(1200, [] {
        if (AllBars().empty()) return; TogglePanel(g_openPanel, AllBars()[0]);
        if (g_openPanel == Panel::Quick && !g_page.empty()) UiOnce(400, [] {
            auto* q = (QuickPanel*)Panel_(Panel::Quick);
            q->ShowDetail(g_page == L"wifi" ? MakeWifiPage(q) : g_page == L"bt" ? MakeBtPage(q) : g_page == L"theme" ? MakeThemePage(q) : g_page == L"dmtheme" ? MakeThemePage(q, true) : g_page == L"display" ? MakeDisplayPage(q) : g_page == L"dmsettings" ? MakeDeskModeSettings(q) : MakeSoundPage(q), g_page == L"display" || g_page == L"dmsettings");
        });
    });
    if (g_openSettings) UiOnce(1200, [] { OpenSettingsWindow(); if (!g_page.empty()) SettingsShowPage(g_page); });   // dev: --open settings --page colours|drawer|tune|apps
    if (g_snapMs) UiOnce(g_snapMs, [] {
        if (g_openSettings) SettingsSnapshot(g_snapDir + L"\\snap_settings.png");
        int i = 0; for (auto* b : AllBars()) b->Snapshot(g_snapDir + Fmt(L"\\snap_bar%d.png", i++));
        if (g_openPanel != Panel::None) Panel_(g_openPanel)->Snapshot(g_snapDir + L"\\snap_panel.png");
        else if (Panel o = OpenPanel(); o != Panel::None) Panel_(o)->Snapshot(g_snapDir + L"\\snap_panel.png");
        if (g_openPreview) PreviewSnapshot(g_snapDir + L"\\snap_preview.png");
        PostQuitMessage(0);
    });
    MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    Log(L"loop done"); TrayStop(); BarsStop(); Log(L"bars stopped"); if (real && g_restoreOnExit) Explorer::Restore();
    Sleep(150); TerminateProcess(GetCurrentProcess(), g_exitCode);   // skip static destructors (DirectWrite/D2D objects outlive the CRT teardown order)
    return g_exitCode;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_inst = inst;
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    auto has = [&](const wchar_t* a) { for (int i = 1; i < argc; i++) if (EqI(argv[i], a)) return true; return false; };
    auto val = [&](const wchar_t* a) -> ws { for (int i = 1; i + 1 < argc; i++) if (EqI(argv[i], a)) return argv[i + 1]; return {}; };
    if (!val(L"--screen").empty()) g_devScreen = val(L"--screen");
    if (!val(L"--snap").empty()) { g_snapMs = _wtoi(val(L"--snap").c_str()); g_snapDir = val(L"--snapdir"); if (g_snapDir.empty()) g_snapDir = DataDir(); }
    { ws o = val(L"--open"); g_openPanel = o == L"start" ? Panel::Start : o == L"quick" ? Panel::Quick : o == L"clock" ? Panel::Clock : o == L"clip" ? Panel::Clip : Panel::None; g_openPreview = o == L"preview"; g_openSettings = o == L"settings"; g_stall = o == L"stall"; }
    { ws c = val(L"--click"); if (c.find(L',') != ws::npos) { g_clickX = _wtoi(c.c_str()); g_clickY = _wtoi(c.c_str() + c.find(L',') + 1); } }
    g_wifiPreview = val(L"--wifipreview"); g_page = val(L"--page"); g_withTray = has(L"--tray"); g_sup = has(L"--sup");
    if (has(L"--restore")) { LogInit(false); LoadSettings(); Explorer::Restore(); Sleep(400); return 0; }
    if (has(L"--supervise")) return RunSupervisor();
    if (has(L"--hooks")) return RunHooks((DWORD)_wtoi(val(L"--hooks").c_str()));
    if (!val(L"--stackof").empty() && argc > 3) { LogInit(false); return RunStackOf((DWORD)_wtoi(val(L"--stackof").c_str()), argv[argc - 1]); }
    if (has(L"--btsearch")) { LogInit(false); UiInit(); BtFindNew([](std::vector<BtDev> v, bool done) { if (!done) return; for (auto& d : v) Log(L"bt search found: %s", d.name.c_str()); PostQuitMessage(0); }); MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m); Sleep(500); return 0; }   // dev: what Add device finds (in the log)
    if (has(L"--wallcolor")) { LogInit(false); CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); return WallpaperThemeId().empty() ? 1 : 0; }   // dev: which theme the wallpaper picks (in the log)
    if (has(L"--buildindex")) { LogInit(false); return FileIndexBuild(); }
    if (!val(L"--bugsend").empty()) { LogInit(false); return BugSendTest(val(L"--bugsend")); }   // dev: send one report through the real form code; exit 0 = Google took it
    if (!val(L"--updatetest").empty()) { LogInit(false); return UpdateTest(val(L"--updatetest")); }   // dev: check + download + verify against DESK_UPDATE_BASE, never installs
    if (has(L"--wifilist")) { LogInit(false); return WifiDump(); }
    if (!val(L"--wifitest").empty()) { LogInit(false); return WifiTest(val(L"--wifitest"), has(L"--keep")); }
    if (!val(L"--indexquery").empty() && argc > 3) { LogInit(false); FileIndexQuery(val(L"--indexquery"), argv[argc - 1]); return 0; }
    if (!val(L"--indexmem").empty()) { LogInit(false); FileIndexMemReport(val(L"--indexmem")); return 0; }   // loads the saved file index and writes entries + memory to the file
    if (!val(L"--selftest").empty()) {   // never touch the real settings: the test saves and reloads them
        wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); ws d = ws(t) + L"db3-selftest"; CreateDirectoryW(d.c_str(), nullptr); SetEnvironmentVariableW(L"DB3_DATA", d.c_str()); SetEnvironmentVariableW(L"DESK_DIR", (d + L"\\desk").c_str());   // the suite's colour files too
        LogInit(false); return RunSelfTest(val(L"--selftest"));
    }
    if (!val(L"--signintest").empty()) { LogInit(false); return SignInTest(val(L"--signintest")); }
    if (has(L"--uninstall")) return RunUninstall();
    if (has(L"--install") || [] { wchar_t m[MAX_PATH]; GetModuleFileNameW(nullptr, m, MAX_PATH); return ContainsI(FileNameOf(m), L"setup"); }()) return RunInstall();
    return RunTaskbar(has(L"--run"));
}
