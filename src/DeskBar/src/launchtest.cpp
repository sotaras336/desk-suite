#include "pch.h"
#include "app.h"

// Self-test of the ways the Desk apps start and reach each other (part of "DeskBar.exe --selftest"). Each rule is checked on its own; the detail names the ones that failed.
// The other half lives in DeskDrawer's and DeskTune's own self-tests (their side of "DeskOpenSettings"), in .github/launch-contract.py (names and paths the three
// apps must agree on) and in .github/launch-test.ps1, which installs the suite on the CI machine and clicks through the real menus.
namespace {
std::vector<int> g_gotCodes;   // what the settings hook was asked for, in order
void Pump(int ms) { uint64_t end = Now() + ms; MSG m; while (Now() < end) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(5); } }
// Sends "DeskOpenSettings" the way DeskDrawer and DeskTune do (Theme.cs DeskSettings.Open: SendMessageTimeout from another process, so from another thread here)
// while this thread keeps answering, and returns each answer.
std::vector<LRESULT> AskForSettings(HWND to, const std::vector<int>& codes) {
    UINT msg = RegisterWindowMessageW(L"DeskOpenSettings"); std::vector<LRESULT> got(codes.size(), -1); std::atomic<bool> done{false};
    std::thread t([&] { for (size_t i = 0; i < codes.size(); i++) { DWORD_PTR r = 0; if (SendMessageTimeoutW(to, msg, (WPARAM)codes[i], 0, SMTO_ABORTIFHUNG, 2000, &r)) got[i] = (LRESULT)r; } done = true; });
    for (int i = 0; i < 800 && !done; i++) Pump(5);
    t.join(); Pump(60);   // the hook runs from UiPost: let it through
    return got;
}
}

bool LaunchSelfTest(ws& detail) {
    std::vector<std::pair<const wchar_t*, bool>> r;
    if (!UiHwnd()) UiInit();   // UiPost needs the UI thread's window; the self-test doesn't make one otherwise
    ws la = KnownFolder(FOLDERID_LocalAppData);

    // 1. DeskBar looks for DeskDrawer and DeskTune exactly where they install themselves (Setup.cs / App.xaml.cs InstalledExe): Settings > Desk apps, its
    //    "Open DeskDrawer" / "Open DeskTune" / "Put everything back" / "Unlock services" rows and the DeskTune tile in Quick Settings all use this path
    r.push_back({L"DeskDrawer is looked for where it installs", EqI(DeskAppExe(L"DeskDrawer"), la + L"\\Programs\\DeskDrawer\\DeskDrawer.exe")});
    r.push_back({L"DeskTune is looked for where it installs", EqI(DeskAppExe(L"DeskTune"), la + L"\\Programs\\DeskTune\\DeskTune.exe")});
    r.push_back({L"an unknown app has no path", DeskAppExe(L"Nope").empty()});
    {   // every app the Desk apps page lists has a path, a description and its own setup
        bool ok = DeskAppCount() == 2; for (int i = 0; i < DeskAppCount(); i++) ok = ok && !DeskAppExe(DeskAppName(i)).empty() && !DeskAppWhat(i).empty();
        r.push_back({L"Desk apps page: each app has a path and a description", ok});
    }

    // 2. the page each app's Settings button opens (code from Theme.cs DeskSettings.Open), and the --page names of the developer switch
    r.push_back({L"code 1 opens the DeskDrawer page", SettingsPageForCode(1) == PageNamed(L"drawer")});
    r.push_back({L"code 2 opens the DeskTune page", SettingsPageForCode(2) == PageNamed(L"tune")});
    r.push_back({L"code 3 opens Colours", SettingsPageForCode(3) == PageNamed(L"colours")});
    r.push_back({L"code 4 opens DeskMode", SettingsPageForCode(4) == PageNamed(L"deskmode")});
    r.push_back({L"code 0 and unknown codes open Taskbar", SettingsPageForCode(0) == PageNamed(L"") && SettingsPageForCode(99) == PageNamed(L"") && SettingsPageForCode(-1) == PageNamed(L"")});
    r.push_back({L"the five pages are all different", [] { std::set<int> s; for (int c = 0; c < 5; c++) s.insert(SettingsPageForCode(c)); return s.size() == 5; }()});

    // 3. "DeskOpenSettings" through a real DeskBar window, as DeskDrawer's and DeskTune's Settings buttons send it: answered with the ack, and the hook gets the code
    {
        auto keep = g_onOpenSettings; g_gotCodes.clear();
        Wnd w; w.Create(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WS_POPUP, -32000, -32000, 20, 20);
        bool made = w.hwnd != nullptr; std::vector<LRESULT> answers, old;
        if (made) {
            g_onOpenSettings = [](int code) { g_gotCodes.push_back(code); };
            answers = AskForSettings(w.hwnd, {1, 2, 3, 0, 4});
            g_onOpenSettings = nullptr; old = AskForSettings(w.hwnd, {1});   // no hook (DeskBar still starting, or the supervisor's stand-in bar): answers 0, so the app opens its own settings
        }
        g_onOpenSettings = keep;
        bool acked = made && std::all_of(answers.begin(), answers.end(), [](LRESULT a) { return a == kOpenSettingsAck; });
        r.push_back({L"settings message: a DeskBar window answers with the ack", acked});
        r.push_back({L"settings message: the code arrives unchanged and in order", g_gotCodes == std::vector<int>{1, 2, 3, 0, 4}});
        r.push_back({L"settings message: without the hook it answers 0", made && old.size() == 1 && old[0] == 0});
        r.push_back({L"settings message: the ack is \"DSK\" (DeskDrawer and DeskTune check this number)", kOpenSettingsAck == 0x44534B});
        if (!acked || g_gotCodes.size() != 5) detail += Fmt(L"(answers %d, codes %d) ", (int)answers.size(), (int)g_gotCodes.size());
    }

    // 4. File Explorer launches that go to DeskDrawer while it is the folder handler, and the folder handler's command as DeskDrawer writes it (DefaultApp.cs)
    {
        ws a1, a2, a3, a4;
        r.push_back({L"Explorer with no folder opens DeskDrawer's Home", ExplorerAsDeskDrawer(L"explorer.exe", L"", a1) && a1 == L"shell:Home"});
        r.push_back({L"Open file location goes to DeskDrawer with the file", ExplorerAsDeskDrawer(L"C:\\Windows\\explorer.exe", L"/select,\"C:\\a b\\f.txt\"", a2) && a2 == L"C:\\a b\\f.txt"});
        r.push_back({L"the Start button's File Explorer goes to DeskDrawer", ExplorerAsDeskDrawer(L"shell:AppsFolder\\Microsoft.Windows.Explorer", L"", a3) && a3 == L"shell:Home"});
        r.push_back({L"Explorer on a folder and other programs stay as they are", !ExplorerAsDeskDrawer(L"explorer.exe", L"C:\\", a4) && !ExplorerAsDeskDrawer(L"notepad.exe", L"", a4)});
        r.push_back({L"DeskDrawer's folder command is read back", ExeOfOpenCommand(L"\"C:\\Users\\x\\AppData\\Local\\Programs\\DeskDrawer\\DeskDrawer.exe\" \"%1\"") == L"C:\\Users\\x\\AppData\\Local\\Programs\\DeskDrawer\\DeskDrawer.exe"
                                                         && ExeOfOpenCommand(L"").empty() && ExeOfOpenCommand(L"C:\\x.exe %1").empty() && ExeOfOpenCommand(L"\"\" \"%1\"").empty()});
    }

    // 5. the pipe DeskBar hands DeskDrawer its settings on: DeskDrawer's own name (Instance.cs), for this user and session
    {
        DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid); wchar_t user[256] = L""; DWORD un = 256; GetUserNameW(user, &un);
        r.push_back({L"DeskDrawer's pipe: DeskDrawer-<user>-<session>", DeskDrawerPipeName() == Fmt(L"\\\\.\\pipe\\DeskDrawer-%s-%lu", user, sid) && un > 1});
    }

    // 6. a menu (the taskbar's right-click menu, a pin's menu): every row runs its own item, and nothing of ours lies over a row
    {
        int ran = -1; std::vector<MenuItem> items;
        const wchar_t* names[] = {L"Task Manager", L"DeskBar settings", L"Open DeskDrawer"};
        for (int i = 0; i < 3; i++) items.push_back({names[i], 0xE713, [&ran, i] { ran = i; }});
        items.insert(items.begin() + 1, MenuItem{L"", 0, {}, true});   // a separator must not shift which row runs what
        MONITORINFO mi{sizeof mi}; GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
        POINT at{(mi.rcWork.left + mi.rcWork.right) / 2, mi.rcWork.bottom - 20};   // where a right-click on the bar puts it: just above the bottom of the screen
        bool each = true, uncovered = true; int rows = 0; ws who;
        for (int pick = 0; pick < 3; pick++) {
            MenuShow(items, {at.x, at.y, at.x, at.y}, true, 1); Pump(30);
            Wnd* m = MenuWindow(); if (!m || !m->hwnd || !MenuOpen()) { each = false; break; }
            std::vector<View*> found; for (View* v : m->root->kids) if (v->onClick) found.push_back(v);
            rows = (int)found.size(); if (rows != 3) { each = false; MenuClose(); break; }
            if (pick == 0) for (View* v : found) {   // the centre of each row, on the screen: the menu is what a click there reaches
                RECT wr; GetWindowRect(m->hwnd, &wr); POINT c{wr.left + (LONG)lroundf((v->rc.l + v->rc.r) / 2 * m->scale), wr.top + (LONG)lroundf((v->rc.t + v->rc.b) / 2 * m->scale)};
                HWND h = WindowFromPoint(c); DWORD pid = 0; if (h) GetWindowThreadProcessId(h, &pid);
                if (h != m->hwnd && pid == GetCurrentProcessId()) { uncovered = false; who = ClassOf(h); }   // another program's window on top is not ours to judge here
            }
            ran = -1; found[pick]->onClick(); Pump(40);
            each = each && ran == pick && !MenuOpen();
        }
        MenuClose();
        r.push_back({L"menu: every row runs its own item, the menu closes", each});
        r.push_back({L"menu: nothing lies over a row (a click there reaches the menu)", uncovered});
        if (!uncovered) detail += L"(covered by " + who + L") ";
        if (rows != 3) detail += Fmt(L"(%d rows) ", rows);
    }

    for (auto& [name, ok] : r) if (!ok) detail += ws(name) + L"; ";
    int passed = 0; for (auto& p : r) passed += p.second; ws head = Fmt(L"%d of %d", passed, (int)r.size());
    detail = detail.empty() ? head : head + L", failed: " + detail;
    return passed == (int)r.size();
}
