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
    { ws d; bool ok = WinMoveMoreSelfTest(d); check(L"windows: touch on the bar's edge, drops and moves between screens", ok, d); }
    {   // colours: what each theme id means and the colours it gives. DeskDrawer's and DeskTune's self-tests check the very same table, so the three apps look like one.
        const uint32_t ins[6] = {0x1167C4, 0x0A4096, 0x4CC2FF, 0xFFFFFF, 0x000000, 0x2BB673};   // the accent, a background, a light accent, white, black, a green (greens never move)
        struct Row { const wchar_t* id; uint32_t want[6]; };   // 0xAARRGGBB
        const Row rows[] = {
            {L"pink", {0xFFC67299, 0xFFC46890, 0xFFFA6AC2, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}}, {L"blue", {0xFF1167C4, 0xFF0A4096, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}},
            {L"teal", {0xFF11C4AE, 0xFF0A9688, 0xFF4CFFE1, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}}, {L"red:30", {0xFFC4112D, 0xFF960A1D, 0xFFFF4C70, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}},
            {L"black", {0xFF353535, 0xFF282828, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}}, {L"white", {0xFFDFDFDF, 0xFFE7E7E7, 0xFF009DEE, 0xFF1B1B1B, 0xFFFFFFFF, 0xFF2BB673}},
            {L"glass", {0x3D202020, 0x3D181818, 0xFF4CC2FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}}, {L"custom:200", {0xFF1193C4, 0xFF0A6C96, 0xFF4CD6FF, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}},
            {L"custom:120:40:50:60", {0xFF285B25, 0xFF1C451B, 0xFF82D279, 0xFFFFFFFF, 0xFF000000, 0xFF2BB673}},
        };
        ws bad;
        for (auto& r : rows) { float g; ThemeDef t = ThemeFromId(r.id, g);
            for (int i = 0; i < 6; i++) { auto c = ColFor(t, ins[i]); int got[4] = {(int)lroundf(c.a * 255), (int)lroundf(c.r * 255), (int)lroundf(c.g * 255), (int)lroundf(c.b * 255)}; bool same = true;
                for (int k = 0; k < 4; k++) same = same && abs(got[k] - (int)((r.want[i] >> (24 - 8 * k)) & 255)) <= 2;   // float here, double in the C# apps: a step or two apart at most
                if (!same) bad += Fmt(L"%s %06X gave %02X%02X%02X%02X, want %08X; ", r.id, ins[i], got[0], got[1], got[2], got[3], r.want[i]); } }
        check(L"colours: every theme gives the suite's colours (the table all three apps check)", bad.empty(), bad);
        float g1, g2, g3, g4; ThemeDef a = ThemeFromId(L"blue:40", g1), b = ThemeFromId(L"custom:-30", g2), c = ThemeFromId(L"custom:200:150:120:-5", g3), d = ThemeFromId(L"PURPLE", g4);
        check(L"colours: theme ids are read the same way as in DeskDrawer and DeskTune", EqI(a.id, L"blue") && g1 == 40 && b.hue == 330 && g2 == -1 && c.hue == 200 && g3 == 100 && c.sat == 1 && c.val == 0 && EqI(d.id, L"purple") && g4 == -1,
              Fmt(L"blue:40 -> %s %.0f; custom:-30 -> hue %.0f; clamped -> fade %.0f sat %.2f val %.2f", a.id, g1, b.hue, g3, c.sat, c.val));
        bool ends = ColourIdOf(0.05f, L"", true, 200, 30, 100) == L"white" && ColourIdOf(0.95f, L"blue", false, 200, 30, 100) == L"black";
        bool named = ColourIdOf(0.25f, L"blue", false, 205, 40, 100) == L"blue:40" && ColourIdOf(0.25f, L"blue", true, 200, 0, 100) == L"custom:200:0" && ColourIdOf(0.25f, L"", true, 200, 40, 80) == L"custom:200:40:80:100";
        check(L"colour picker: the ids it makes (black and white ends, a ready-made colour with a fade, a custom one)", ends && named, ColourIdOf(0.25f, L"", true, 200, 40, 80));
        ws back;   // open the picker on a colour and press OK without touching it: the same colour comes back
        for (const wchar_t* id : {L"custom:120:40:50:60", L"custom:200:0", L"custom:10:25:100:30", L"black", L"white"}) { float g; ThemeDef t = ThemeFromId(id, g); ws got = ColourIdOf(LightStart(t), L"", false, t.hue, g > 0 ? g : 0, t.sat * 100); if (got != id) back += ws(id) + L" -> " + got + L"; "; }
        check(L"colour picker: reopened on a colour, it gives the same colour back", back.empty(), back);
    }
    {   // one colour for every app, or each app its own: the files DeskDrawer and DeskTune read the same way (here in the test's own folder, never the real one)
        wchar_t dd[MAX_PATH] = L""; GetEnvironmentVariableW(L"DESK_DIR", dd, MAX_PATH); ws dir = dd;
        auto clean = [&] { DeleteFileW((dir + L"\\theme.txt").c_str()); for (const wchar_t* a : {L"deskbar", L"deskmode", L"deskdrawer", L"desktune"}) DeleteFileW((dir + L"\\theme-" + a + L".txt").c_str()); };
        if (dir.empty()) check(L"shared colours: the test has its own folder", false, L"DESK_DIR not set");
        else {
            clean(); std::vector<std::pair<const wchar_t*, bool>> r;
            ThemeSet(L"deskbar", L"teal");
            r.push_back({L"one colour: every app wears it", !ThemesSeparate() && ThemeOf(L"") == L"teal" && ThemeOf(L"deskdrawer") == L"teal" && ThemeOf(L"desktune") == L"teal" && FileExists(dir + L"\\theme.txt")});
            ThemesSetSeparate(true);
            r.push_back({L"separate: each app starts from the suite's colour", ThemesSeparate() && ThemeOf(L"deskdrawer") == L"teal" && ThemeOf(L"desktune") == L"teal" && FileExists(dir + L"\\theme-desktune.txt")});
            ThemeSet(L"deskdrawer", L"custom:120:40");
            r.push_back({L"separate: one app's colour leaves the others alone", ThemeOf(L"deskdrawer") == L"custom:120:40" && ThemeOf(L"deskbar") == L"teal" && ThemeOf(L"desktune") == L"teal" && ThemeOf(L"") == L"teal"});
            ThemesSetSeparate(false);
            r.push_back({L"one colour again: the bar's, and the apps' own files are gone", !ThemesSeparate() && ThemeOf(L"") == L"teal" && ThemeOf(L"deskdrawer") == L"teal" && !FileExists(dir + L"\\theme-deskdrawer.txt")});
            WriteAtomic(dir + L"\\theme.txt", "  pink\r\n"); WriteAtomic(dir + L"\\theme-desktune.txt", std::string(300, 'x'));
            r.push_back({L"a colour file with spaces and a line end is read; a broken one is ignored", ThemeOf(L"deskbar") == L"pink" && ThemeOf(L"desktune") == L"pink"});
            clean(); ws bad; for (auto& [n, ok] : r) if (!ok) bad += ws(n) + L"; ";
            check(L"shared colours: one for all, or each app its own", bad.empty(), bad);
        }
    }
    out += Narrow(Fmt(L"%d failure(s)\r\n", fails)); WriteAtomic(outFile, out); return fails;
}
