#include "pch.h"
#include "app.h"
#include "search.h"
#include "flyout.h"

// "DeskBar.exe --selftest <file>": checks the services that need no UI and writes PASS/FAIL lines to <file>.
int RunSelfTest(const ws& outFile) {
    std::string out; int fails = 0;
    auto check = [&](const wchar_t* name, bool ok, const ws& detail = L"") { out += Narrow(Fmt(L"%s %s %s\r\n", ok ? L"PASS" : L"FAIL", name, detail.c_str())); if (!ok) fails++; };
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); UiGfxInit();
    { Json j = Json::O(); j.Set(L"a", Json::S(L"h\u00E9llo \"q\"\n")); Json arr = Json::A(); arr.a.push_back(Json::N(1.5)); arr.a.push_back(Json::B(true)); j.Set(L"b", arr);
      Json r = Json::Parse(j.Dump(2)); check(L"json roundtrip", r.Find(L"a") && r.Find(L"a")->s == L"h\u00E9llo \"q\"\n" && r.Find(L"b") && r.Find(L"b")->a.size() == 2 && r.Find(L"b")->a[0].n == 1.5); }
    check(L"score prefix", Score(L"Calculator", L"calc") > 90); check(L"score none", Score(L"Notepad", L"calc") == 0); check(L"score acronym", Score(L"Visual Studio Code", L"vsc") > 70);
    check(L"score greek accents", Score(L"\u039C\u03AC\u03C1\u03BA\u03BF\u03C2", L"\u03BC\u03B1\u03C1\u03BA") > 90); check(L"score typo", Score(L"Firefox", L"fierfox") > 0);
    auto first = [](const ws& q) { auto r = SearchLocal(q); return r.empty() ? ws() : r[0].title; };
    check(L"calc", first(L"2+2*3") == L"= 8", first(L"2+2*3")); check(L"calc percent", first(L"50%") == L"= 0.5", first(L"50%"));
    check(L"units length", first(L"5 ft in cm") == L"= 152.4 cm", first(L"5 ft in cm")); check(L"units temp", first(L"30 c to f") == L"= 86 \u00B0F", first(L"30 c to f")); check(L"units data", first(L"2 gb in mb") == L"= 2048 MB", first(L"2 gb in mb"));
    check(L"expand ~", ExpandPath(L"~") == KnownFolder(FOLDERID_Profile)); check(L"expand drive", ExpandPath(L"C:Users") == L"C:\\Users");
    check(L"run box shell:", !SearchLocal(L"shell:startup").empty() && SearchLocal(L"shell:startup")[0].kind == RKind::Run);
    check(L"resolve cmd", !ResolveCommand(L"notepad").empty());
    {   // a partial path typed from somewhere inside a usual folder: <profile>\dxtest-search\inner\a.txt found as "dxtest-search\inner\a.txt"
        ws base = KnownFolder(FOLDERID_Profile) + L"\\dxtest-search"; SHCreateDirectoryExW(nullptr, (base + L"\\inner").c_str(), nullptr);
        { std::string z; WriteAtomic(base + L"\\inner\\a.txt", z); }
        auto r = SearchLocal(L"dxtest-search\\inner\\a.txt"); bool found = !r.empty() && r[0].kind == RKind::File && EqI(r[0].target, base + L"\\inner\\a.txt");
        check(L"partial relative path", found, r.empty() ? L"(no result)" : r[0].target);
        { ws d; bool ok = FileIndexSelfTest(d); check(L"file index rebuilds full paths", ok, d); }
        { ws d; bool ok = FileIndexScanSelfTest(d); check(L"file index saves a scan (the crash)", ok, d); }
        DeleteFileW((base + L"\\inner\\a.txt").c_str()); RemoveDirectoryW((base + L"\\inner").c_str()); RemoveDirectoryW(base.c_str());
    }
    check(L"package family", PackageFamilyFromPath(L"C:\\Program Files\\WindowsApps\\Claude_2.3.1.0_x64__pzs8sxrjxfjjc\\app\\claude.exe") == L"Claude_pzs8sxrjxfjjc");
    { PinInfo p; p.exe = L"C:\\x\\a.exe"; WinInfo w; w.exe = L"c:\\X\\A.EXE"; check(L"pin match exe", PinMatchesWindow(p, w)); PinInfo q; q.aumid = L"Foo!App"; WinInfo w2; w2.aumid = L"foo!app"; check(L"pin match aumid", PinMatchesWindow(q, w2)); }
    check(L"short name", AudioShortName(L"Headphones (2- Sam's Buds Pro)") == L"Sam's Buds Pro", AudioShortName(L"Headphones (2- Sam's Buds Pro)"));
    { Settings keep = S; S.taskbarPins = {L"a", L"b"}; S.edgeResistance = 3; S.launchCounts[L"x"] = 4; SaveSettingsNow(); S = Settings(); LoadSettings(); check(L"settings roundtrip", S.taskbarPins.size() == 2 && S.edgeResistance == 3 && S.launchCounts[L"x"] == 4); S = keep; }
    check(L"measure text", MeasureText(L"Hello", FText(12)).w > 10);
    {   // panels on short screens: a 1080p screen leaves ~806 dip at 125% and ~664 at 150% (Start's footer with the power button used to be cut off)
        std::unique_ptr<Flyout> st(MakeStartPanel());
        for (float h : {806.f, 664.f}) { float got = st->ContentHeight(h); check(h > 700 ? L"start fits 125% 1080p" : L"start fits 150% 1080p", got <= h + 0.5f, Fmt(L"%.0f of %.0f dip", got, h)); }
        std::unique_ptr<Flyout> qp(MakeQuickPanel()), cp(MakeClockPanel());
        for (Flyout* f : {qp.get(), cp.get()}) { float got = f->root->Measure({f->widthDip, 1e5f}).h; check(f == qp.get() ? L"quick settings fits 150% 1080p" : L"calendar fits 150% 1080p", got <= 664.5f, Fmt(L"%.0f dip", got)); }
    }
    {   // this PC's own taskbar pins: every one must resolve to a shortcut on disk or an installed app
        auto pins = WindowsTaskbarPins(); bool ok = true; ws bad, order;
        for (auto& p : pins) { if (StartsI(p, L"app:") ? ShellNameOf(L"aum:" + p.substr(4)).empty() : !FileExists(p)) { ok = false; bad = p; } order += (order.empty() ? L"" : L", ") + (StartsI(p, L"app:") ? ShellNameOf(L"aum:" + p.substr(4)) : StemOf(p)); }
        check(L"windows taskbar pins", ok, Fmt(L"%d in order: %s %s", (int)pins.size(), order.c_str(), bad.c_str()));
    }
    {   // a touchpad swipe (many small deltas) is one step, and the inertia tail right after it adds none
        int early = 0; for (int i = 0; i < 5; i++) early += WheelStep(0.1f);
        int one = WheelStep(0.6f), tail = 0; for (int i = 0; i < 20; i++) tail += WheelStep(0.4f);
        check(L"wheel: small deltas make one step", early == 0 && one == 1 && tail == 0, Fmt(L"%d %d %d", early, one, tail));
    }
    {   // Discord-style update: the pinned app-1.0.9266 folder is gone, app-1.0.10000 has the exe: the pin follows it (and 10000 beats 9266 by number, not by letters)
        wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); ws root = ws(t) + L"db3-selftest-sq"; CreateDirectoryW(root.c_str(), nullptr);
        for (const wchar_t* v : {L"app-1.0.9267", L"app-1.0.10000"}) { CreateDirectoryW((root + L"\\" + v).c_str(), nullptr); HANDLE h = CreateFileW((root + L"\\" + v + L"\\Z.exe").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
        ws old = root + L"\\app-1.0.9266\\Z.exe", now = root + L"\\app-1.0.10000\\Z.exe";
        check(L"pins: an updated Squirrel app is followed to its new folder", FollowAppUpdate(old) == now && FollowAppUpdate(now) == now && FollowAppUpdate(root + L"\\other\\Z.exe") == root + L"\\other\\Z.exe", FollowAppUpdate(old));
        DeleteFileW((root + L"\\app-1.0.9267\\Z.exe").c_str()); DeleteFileW(now.c_str()); RemoveDirectoryW((root + L"\\app-1.0.9267").c_str()); RemoveDirectoryW((root + L"\\app-1.0.10000").c_str()); RemoveDirectoryW(root.c_str());
    }
    {   // a popup that opened on the other screen lands centred on its owner, inside the owner's screen
        RECT a = PopupSpot({-2000, 100, -1400, 500}, {200, 100, 1400, 900}, {0, 0, 1920, 1040}), b = PopupSpot({-2000, 100, -1400, 500}, {1500, 700, 1900, 1000}, {0, 0, 1920, 1040});
        check(L"popups: centred on the owner, kept on its screen", a.left == 500 && a.top == 300 && a.right == 1100 && a.bottom == 700 && b.right <= 1920 && b.bottom <= 1040 && b.left >= 0 && b.right - b.left == 600, Fmt(L"%d,%d,%d,%d | %d,%d,%d,%d", (int)a.left, (int)a.top, (int)a.right, (int)a.bottom, (int)b.left, (int)b.top, (int)b.right, (int)b.bottom));
    }
    {   // the search widget: addresses open as they are, everything else is a Google search with the text percent-encoded as UTF-8
        check(L"search widget: words become a Google search", WebSearchUrl(L"  hello world ") == L"https://www.google.com/search?q=hello%20world");
        check(L"search widget: a web address opens as it is", WebSearchUrl(L"example.com/a?b=1") == L"https://example.com/a?b=1" && WebSearchUrl(L"https://x.org") == L"https://x.org" && WebSearchUrl(L"3.14") == L"https://www.google.com/search?q=3.14");
        check(L"search widget: Greek is encoded, empty opens nothing", WebSearchUrl(L"\x03B5\x03C1").find(L"q=%CE%B5%CF%81") != ws::npos && WebSearchUrl(L"   ").empty());
    }
    {   // the bar's "main screen" is the primary monitor, whichever side of the others it is on (the work-area upkeep depends on it)
        int prim = 0; bool same = true; for (auto& m : Monitors()) if (m.primary) { prim++; same = m.h == MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY); }
        check(L"exactly one primary monitor, found the same way the bar upkeep finds it", prim == 1 && same, Fmt(L"%d primary", prim));
    }
    {   // the update check: the checksum of "abc" and the line the release list uses
        std::string sums = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad *DeskBar-Setup.exe\r\n" + std::string(64, 'c') + " *DeskTune-Setup.exe\n";
        check(L"update: sha-256 of abc", Sha256HexOf("abc") == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        check(L"update: reads the checksum for a file", UpdateHashFor(sums, L"DeskBar-Setup.exe") == Sha256HexOf("abc") && UpdateHashFor(sums, L"deskTune-setup.exe") == Widen(std::string(64, 'c')) && UpdateHashFor(sums, L"DeskMode-Setup.exe").empty());
    }
    {   // File Explorer requests that DeskDrawer takes over (when it is the folder handler), and the ones that stay with Explorer
        ws a;
        check(L"explorer: no arguments -> DeskDrawer's Home", ExplorerAsDeskDrawer(L"explorer.exe", L"", a) && a == L"shell:Home");
        check(L"explorer: /select, -> the file path itself", ExplorerAsDeskDrawer(L"C:\\Windows\\explorer.exe", L"/select,\"C:\\a b\\c.txt\"", a) && a == L"C:\\a b\\c.txt");
        check(L"explorer: the File Explorer app id counts too", ExplorerAsDeskDrawer(L"shell:AppsFolder\\Microsoft.Windows.Explorer", L"", a) && a == L"shell:Home");
        check(L"explorer: a folder path or another shell: place stays with Explorer", !ExplorerAsDeskDrawer(L"explorer.exe", L"C:\\Users", a) && !ExplorerAsDeskDrawer(L"explorer.exe", L"shell:RecycleBinFolder", a));
        check(L"explorer: other programs are never touched", !ExplorerAsDeskDrawer(L"notepad.exe", L"", a) && !ExplorerAsDeskDrawer(L"C:\\x\\myexplorer.exe", L"", a));
        ws dd = DeskDrawerExe(); check(L"explorer: DeskDrawer's exe is empty or a real file", dd.empty() || FileExists(dd), dd);
    }
    out += Narrow(Fmt(L"%d failure(s)\r\n", fails)); WriteAtomic(outFile, out); return fails;
}
