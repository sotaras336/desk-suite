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
        { ws d; bool ok = IconTrimSelfTest(d); check(L"icons: unused icons are let go, held ones stay", ok, d); }
        {   // search that learns: what you pick for a query rises for that query (and longer ones that start the same), and nothing else moves
            HabitRecord(L"dxtest qq", RKind::File, L"C:\\dxtest\\picked.txt"); HabitRecord(L"dxtest qq", RKind::File, L"C:\\dxtest\\picked.txt");
            double same = HabitBoost(L"dxtest qq", RKind::File, L"C:\\dxtest\\picked.txt"), longer = HabitBoost(L"dxtest qqx", RKind::File, L"C:\\dxtest\\picked.txt");
            double other = HabitBoost(L"dxtest qq", RKind::File, L"C:\\dxtest\\other.txt"), unrelated = HabitBoost(L"zzz", RKind::File, L"C:\\dxtest\\picked.txt");
            check(L"search learns: a result picked twice rises for that search", same >= 45 && longer > 0 && longer < same && other == 0 && unrelated == 0, Fmt(L"same %.0f longer %.0f other %.0f unrelated %.0f", same, longer, other, unrelated));
        }
        { ws d; bool ok = WinMoveSelfTest(d); check(L"windows: maximized-size and hidden-under-bar rules", ok, d); }
        { ws d; bool ok = FreshSelfTest(d); check(L"search: a file made just now is found before the next scan", ok, d); }
        if (GetEnvironmentVariableW(L"DB3_UNINSTALL_LOOKUP", nullptr, 0)) {   // read-only: which installed-programs entry each app in Start maps to (nothing is run)
            CatalogLoad(); int shown = 0; std::string rep; check(L"lookup: apps in the catalog", !CatalogApps().empty(), Fmt(L"%d", (int)CatalogApps().size()));
            for (auto& a : CatalogApps()) {
                ws exe = AppTargetPath(a.id), cmd, listed; bool store = a.id.find(L'!') != ws::npos, f = !store && FindUninstall(a.name, exe, cmd, listed);
                rep += Narrow(Fmt(L"lookup %s: %s\n", a.name.c_str(), store ? L"Store app, its Settings page" : f ? (L"\"" + listed + L"\" -> " + cmd).c_str() : (L"not found (exe " + exe + L")").c_str()));
                if (++shown >= 400) break;
            } ws lp = DataPath(L"lookup.txt"); check(L"lookup: report written", WriteAtomic(lp, rep), Fmt(L"%d apps, %d bytes to %s", shown, (int)rep.size(), lp.c_str()));
        }
        {   // uninstall command lines as Windows keeps them
            ws e1, a1, e2, a2, e3, a3; bool s1 = SplitCommand(L"\"C:\\Program Files\\App\\unins000.exe\" /SILENT", e1, a1), s2 = SplitCommand(L"MsiExec.exe /X{1234-ABCD}", e2, a2), s3 = SplitCommand(L"C:\\Users\\x\\AppData\\Local\\Discord\\Update.exe --uninstall", e3, a3);
            check(L"uninstall: commands split into program and arguments", s1 && s2 && s3 && e1 == L"C:\\Program Files\\App\\unins000.exe" && a1 == L"/SILENT" && e2 == L"MsiExec.exe" && a2 == L"/X{1234-ABCD}" && e3 == L"C:\\Users\\x\\AppData\\Local\\Discord\\Update.exe" && a3 == L"--uninstall", e3 + L" | " + a3);
        }
        DeleteFileW((base + L"\\inner\\a.txt").c_str()); RemoveDirectoryW((base + L"\\inner").c_str()); RemoveDirectoryW(base.c_str());
    }
    check(L"package family", PackageFamilyFromPath(L"C:\\Program Files\\WindowsApps\\Claude_2.3.1.0_x64__pzs8sxrjxfjjc\\app\\claude.exe") == L"Claude_pzs8sxrjxfjjc");
    { PinInfo p; p.exe = L"C:\\x\\a.exe"; WinInfo w; w.exe = L"c:\\X\\A.EXE"; check(L"pin match exe", PinMatchesWindow(p, w)); PinInfo q; q.aumid = L"Foo!App"; WinInfo w2; w2.aumid = L"foo!app"; check(L"pin match aumid", PinMatchesWindow(q, w2)); }
    check(L"short name", AudioShortName(L"Headphones (2- Sam's Buds Pro)") == L"Sam's Buds Pro", AudioShortName(L"Headphones (2- Sam's Buds Pro)"));
    { Settings keep = S; S.taskbarPins = {L"a", L"b"}; S.edgeResistance = 3; S.launchCounts[L"x"] = 4; SaveSettingsNow(); S = Settings(); LoadSettings(); check(L"settings roundtrip", S.taskbarPins.size() == 2 && S.edgeResistance == 3 && S.launchCounts[L"x"] == 4); S = keep; SaveSettingsNow(); }   // write the real settings back: run against the real data folder, the test pins "a", "b" stayed in the file and replaced the user's pins at the next start
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
    {   // touchpad: three fingers sliding down then lifting is a swipe; a short slide, a sideways one or four fingers is not
        auto run = [](std::initializer_list<std::tuple<int, float, float>> frames) { SwipeTracker t; int got = 0; for (auto& [n, x, y] : frames) got += t.Feed(n, x, y); return got; };
        int down = run({{1, .5f, .3f}, {3, .5f, .3f}, {3, .5f, .45f}, {3, .51f, .6f}, {2, .5f, .62f}, {0, 0, 0}});
        int shortSlide = run({{3, .5f, .3f}, {3, .5f, .38f}, {0, 0, 0}}), side = run({{3, .2f, .3f}, {3, .6f, .5f}, {0, 0, 0}}), up = run({{3, .5f, .7f}, {3, .5f, .3f}, {0, 0, 0}});
        int four = run({{3, .5f, .3f}, {4, .5f, .4f}, {4, .5f, .7f}, {3, .5f, .7f}, {0, 0, 0}});
        check(L"touchpad: three-finger swipe down", down == 1 && !shortSlide && !side && !up && !four, Fmt(L"%d %d %d %d %d", down, shortSlide, side, up, four));
    }
    {   // Wi-Fi: the list order, what a failed join means, and the sign-in page check's verdict
        WifiNet on, known, strong, weak; on.connected = true; on.pct = 10; known.saved = true; known.pct = 20; strong.pct = 90; weak.pct = 30;
        std::vector<WifiNet> v{weak, strong, known, on}; std::sort(v.begin(), v.end(), WifiBefore);
        bool order = v[0].connected && v[1].saved && v[2].pct == 90 && v[3].pct == 30;
        bool why = WlanReasonKind(0).empty() && WlanReasonKind(0x00048005 /*a security failure*/) == L"password" && WlanReasonKind(0x00010011) != L"password";
        bool portal = PortalVerdict(200, "Microsoft Connect Test") == 0 && PortalVerdict(302, "") == 1 && PortalVerdict(200, "<html>Welcome to Hotel") == 1 && PortalVerdict(511, "") == 1;
        check(L"wifi: list order, failure kinds, sign-in check", order && why && portal, Fmt(L"%d %d %d", (int)order, (int)why, (int)portal));
    }
    check(L"bluetooth: address from a device id", BtAddressFromId(L"Bluetooth#Bluetooth00:11:22:33:44:55-aa:bb:cc:0d:ee:ff") == L"AABBCC0DEEFF" && BtAddressFromId(L"short").empty() && BtAddressFromId(L"BluetoothLE#xx-zz:bb:cc:dd:ee:ff").empty(), L"");
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
    {   // screens and scaling, on pretend layouts: the test machine has one screen at 100%, so the mixed set-ups are described here and the same code that places the bar is asked about them
        auto M = [](int l, int t, int r, int b, float sc, const wchar_t* dev, bool prim) { Mon m{}; m.bounds = {l, t, r, b}; m.work = m.bounds; m.scale = sc; m.device = dev; m.primary = prim; return m; };
        auto R = [](const RECT& r) { return Fmt(L"%d,%d,%d,%d", (int)r.left, (int)r.top, (int)r.right, (int)r.bottom); };
        auto inside = [](const RECT& in, const RECT& out) { return in.left >= out.left && in.top >= out.top && in.right <= out.right && in.bottom <= out.bottom && in.right > in.left && in.bottom > in.top; };
        // docked: the Surface (2736 x 1824 at 200%) LEFT of the main LG (2560 x 1440 at 100%), so the second screen has negative coordinates and the leftmost screen is not the main one
        std::vector<Mon> docked{M(-2736, 0, 0, 1824, 2.f, L"\\\\.\\DISPLAY2", false), M(0, 0, 2560, 1440, 1.f, L"\\\\.\\DISPLAY1", true)};
        check(L"screens: the main screen is the primary one, not the leftmost", MainMonIndex(docked) == 1 && MainMonIndex({docked[1], docked[0]}) == 0 && MainMonIndex({M(0, 0, 10, 10, 1, L"a", false)}) == 0);
        {   // the bar's own height on each screen, at every scaling Windows offers, flush with that screen's bottom edge and as wide as it
            bool ok = true; ws bad;
            for (float sc : {1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.25f, 2.5f, 3.f, 3.5f}) for (auto& m : docked) {
                RECT b = BarRectOn(m.bounds, sc, false), w = BarWorkAreaOn(m.bounds, sc); int h = BarHeightFor(sc);
                bool good = h == (int)lroundf(48 * sc) && b.left == m.bounds.left && b.right == m.bounds.right && b.bottom == m.bounds.bottom && b.top == m.bounds.bottom - h
                    && w.top == m.bounds.top && w.left == m.bounds.left && w.right == m.bounds.right && w.bottom == b.top;
                if (!good) { ok = false; bad += Fmt(L"%.2f: bar %s work %s | ", sc, R(b).c_str(), R(w).c_str()); }
            }
            check(L"bar: own height per scaling, on the bottom edge of its own screen (second screen left of the main one)", ok && BarHeightFor(1.25f) == 60 && BarHeightFor(1.75f) == 84, bad);
            RECT above = BarRectOn({0, -1080, 1920, 0}, 1.5f, false), devTop = BarRectOn(docked[0].bounds, 2.f, true);
            check(L"bar: a screen above the main one, and the developer bar on top", above.top == -72 && above.bottom == 0 && devTop.top == 0 && devTop.bottom == 96 && devTop.left == -2736, R(above) + L" | " + R(devTop));
        }
        {   // any change to the screens must be seen as one (that is what rebuilds the bars); the order Windows lists them in is not a change
            ws a = MonSignatureOf(docked);
            auto changed = [&](std::vector<Mon> v) { return MonSignatureOf(v) != a; };
            std::vector<Mon> dpi = docked; dpi[0].scale = 1.5f;
            std::vector<Mon> moved = docked; moved[0].bounds = {0, -1824, 2736, 0};
            std::vector<Mon> added = docked; added.push_back(M(2560, 0, 4480, 1080, 1.25f, L"\\\\.\\DISPLAY3", false));
            std::vector<Mon> mainSwap = docked; mainSwap[0].primary = true; mainSwap[1].primary = false;
            check(L"screens: scaling changed, screen moved, added, removed, main screen switched are all seen",
                changed(dpi) && changed(moved) && changed(added) && changed({docked[1]}) && changed({}) && changed(mainSwap) && !changed({docked[1], docked[0]}), a);
        }
        {   // Start and Quick Settings on the second screen at 200%, and on a small 1280 x 800 tablet at 150%: inside that screen's work area, 12 units from its edges
            RECT wk = BarWorkAreaOn(docked[0].bounds, 2.f), st = FlyoutRectOn(wk, docked[0].bounds, 2.f, 640, 720, false), qs = FlyoutRectOn(wk, docked[0].bounds, 2.f, 380, 5000, true);
            check(L"panels: Start on the second screen at 200% sits in its corner", inside(st, wk) && st.left == wk.left + 24 && st.bottom == wk.bottom - 24 && st.right - st.left == 1280 && st.bottom - st.top == 1440, R(st) + L" in " + R(wk));
            check(L"panels: a too-tall Quick Settings is cut to the screen, right-aligned", inside(qs, wk) && qs.right == wk.right - 24 && qs.top == wk.top + 24 && qs.bottom == wk.bottom - 24, R(qs) + L" in " + R(wk));
            RECT tw = BarWorkAreaOn({0, 0, 1280, 800}, 1.5f), ts = FlyoutRectOn(tw, {0, 0, 1280, 800}, 1.5f, 640, 2000, false);
            check(L"panels: a 1280 x 800 tablet at 150% keeps Start on screen", inside(ts, tw), R(ts) + L" in " + R(tw));
        }
        {   // the settings window's first size and place: centred, and never bigger than the screen (860 units at 150% is 1290 px, wider than a 1280 px tablet)
            struct { RECT work; float sc; } cases[] = {{{0, 0, 1280, 752}, 1.5f}, {{0, 0, 1920, 1032}, 2.25f}, {{-2736, 0, 0, 1728}, 2.f}, {{0, 0, 2560, 1392}, 1.f}, {{0, -1080, 1920, -48}, 1.25f}};
            bool ok = true; ws d; for (auto& c : cases) { RECT r = SettingsRectOn(c.work, c.sc); int cx = (r.left + r.right) / 2 - (c.work.left + c.work.right) / 2; if (!inside(r, c.work) || std::abs(cx) > 1) { ok = false; d += R(r) + L" in " + R(c.work) + L" | "; } }
            RECT big = SettingsRectOn({0, 0, 2560, 1392}, 1.f);
            check(L"settings window: fits and is centred on every screen", ok && big.right - big.left == 860 && big.bottom - big.top == 680, d.empty() ? R(big) : d);
        }
    }
    {   // a scaling change (WM_DPICHANGED, sent here by hand as Windows would when the window crosses to a 200% screen): the settings window takes the suggested size, the bar-like windows do not
        Wnd follow, self; follow.followDpi = true;
        follow.Create(WS_EX_TOOLWINDOW, WS_POPUP, -32000, 100, 300, 200); self.Create(WS_EX_TOOLWINDOW, WS_POPUP, -32000, 400, 300, 200);
        RECT sug{-31990, 120, -31990 + 600, 120 + 400}, a{}, b{};
        if (follow.hwnd && self.hwnd) { SendMessageW(follow.hwnd, WM_DPICHANGED, MAKEWPARAM(192, 192), (LPARAM)&sug); SendMessageW(self.hwnd, WM_DPICHANGED, MAKEWPARAM(192, 192), (LPARAM)&sug); GetWindowRect(follow.hwnd, &a); GetWindowRect(self.hwnd, &b); }
        check(L"scaling change: a window you move takes the size Windows suggests, one that places itself stays", EqualRect(&a, &sug) && b.right - b.left == 300 && b.bottom - b.top == 200 && b.top == 400,
            Fmt(L"%d,%d,%d,%d | %d,%d,%d,%d", (int)a.left, (int)a.top, (int)a.right, (int)a.bottom, (int)b.left, (int)b.top, (int)b.right, (int)b.bottom));
    }
    out += Narrow(Fmt(L"%d failure(s)\r\n", fails)); WriteAtomic(outFile, out); return fails;
}
