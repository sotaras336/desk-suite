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
    out += Narrow(Fmt(L"%d failure(s)\r\n", fails)); WriteAtomic(outFile, out); return fails;
}
