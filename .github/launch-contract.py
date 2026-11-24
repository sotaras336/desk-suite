#!/usr/bin/env python3
"""The names, paths, numbers and arguments the three Desk apps must agree on to start and reach each other.

DeskBar (C++) and DeskDrawer / DeskTune (C#) are built separately, so nothing stops one side renaming a window class, a page code, an install
folder or a command-line switch while the other keeps the old one: everything still builds, every self-test still passes, and a button or menu
entry just does nothing on a real PC. This reads the sources and fails if the two sides drift apart. It runs anywhere (no Windows, no build):

    python .github/launch-contract.py            (from the repository root)

Each line is PASS or FAIL with what was found; the exit code is the number of failures.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
results = []


def src(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8-sig") as f:
        return f.read()


def check(name, ok, detail=""):
    results.append((bool(ok), name, detail))


def one(pattern, text, what, flags=0):
    m = re.search(pattern, text, flags)
    if not m:
        check(f"found {what}", False, f"pattern {pattern!r} not found")
        return None
    return m.group(1)


BAR = "src/DeskBar/src/"
ui_wnd, settingswin, deskapps, shellx = src(BAR + "ui_wnd.cpp"), src(BAR + "settingswin.cpp"), src(BAR + "deskapps.cpp"), src(BAR + "shellx.cpp")
install, appcpp, displaymode = src(BAR + "install.cpp"), src(BAR + "app.cpp"), src(BAR + "displaymode.cpp")
dd = {f: src("src/DeskDrawer/" + f) for f in ("Theme.cs", "Setup.cs", "Instance.cs", "Program.cs", "App.xaml.cs", "MainWindow.Undo.cs", "MainWindow.Settings.cs", "DefaultApp.cs", "Updater.cs", "Shortcuts.cs", "LaunchCheck.cs")}
dt = {f: src("src/DeskTune/" + f) for f in ("Theme.cs", "App.xaml.cs", "MainWindow.cs", "Updater.cs", "LaunchCheck.cs")}
iss, release, workflow = src("installer/Desk.iss"), src("installer/make-release.ps1"), src(".github/workflows/build.yml")
csharp = {"DeskDrawer": dd, "DeskTune": dt}

# ---- 1. "Settings" in DeskDrawer / DeskTune opens DeskBar's one settings window (Theme.cs DeskSettings.Open -> ui_wnd.cpp WProc) ----
cls = one(r'kWndClass\s*=\s*L"([^"]+)"', ui_wnd, "DeskBar's window class")
msg = one(r'RegisterWindowMessageW\(L"([^"]+)"\);\s*//\s*DeskDrawer', ui_wnd, "DeskBar's settings message")
ack = one(r'kOpenSettingsAck\s*=\s*(0x[0-9A-Fa-f]+)', ui_wnd, "DeskBar's settings answer")
for app, f in csharp.items():
    t = f["Theme.cs"]
    check(f"{app} looks for DeskBar's windows by its class", one(r'WndClass\s*=\s*"([^"]+)"', t, f"{app}'s DeskBar class") == cls, cls)
    check(f"{app} sends DeskBar's settings message", re.search(r'RegisterWindowMessageW\("' + re.escape(msg or "?") + r'"\)', t), msg)
    check(f"{app} expects DeskBar's answer", (one(r'const long Ack\s*=\s*(0x[0-9A-Fa-f]+)', t, f"{app}'s answer") or "").lower() == (ack or "").lower(), ack)
    check(f"{app}'s self-test answers like DeskBar", re.search(r'answer\s*=\s*' + re.escape(ack or "?"), f["LaunchCheck.cs"]) and re.search(r'"' + re.escape(msg or "?") + '"', f["LaunchCheck.cs"]), "LaunchCheck.cs")

# page codes: C# page name -> code; DeskBar code -> page
bar_codes = dict((int(c), p) for c, p in re.findall(r'code == (\d) \? (kPg\w+)', one(r'(int SettingsPageForCode\(int code\)[^\n]*)', settingswin, "SettingsPageForCode") or ""))
want = {"deskdrawer": "kPgDrawer", "desktune": "kPgTune", "colours": "kPgColours"}
maps = {}
for app, f in csharp.items():
    body = one(r'page switch \{([^}]*)\}', f["Theme.cs"], f"{app}'s page codes") or ""
    maps[app] = dict((n, int(c)) for n, c in re.findall(r'"(\w+)"\s*=>\s*(\d+)', body))
    for name, page in want.items():
        code = maps[app].get(name)
        check(f"{app}: \"{name}\" opens DeskBar's {page[3:]} page", code is not None and bar_codes.get(code) == page, f"code {code} -> {bar_codes.get(code)}")
    for name in re.findall(r'DeskSettings\.Open\("(\w+)"\)', "".join(f.values())):
        check(f"{app} asks for a page DeskBar knows (\"{name}\")", name in maps[app], ", ".join(maps[app]))
check("DeskDrawer and DeskTune use the same page codes", maps.get("DeskDrawer") == maps.get("DeskTune"), str(maps))
check("DeskDrawer's Settings asks for its own page", 'DeskSettings.Open("deskdrawer")' in dd["MainWindow.Settings.cs"])
check("DeskTune's Settings asks for its own page", 'DeskSettings.Open("desktune")' in dt["MainWindow.cs"])
check("each self-test checks its own page and code", 'LaunchCheck.Checks("deskdrawer", 1)' in src("src/DeskDrawer/SelfTest.cs") and 'LaunchCheck.Checks("desktune", 2)' in src("src/DeskTune/SelfTest.cs"))
check("DeskBar's settings hook is set once the bar is up", re.search(r'g_onOpenSettings\s*=\s*SettingsOpenFromApp', appcpp), "app.cpp")

# ---- 2. where each app installs = where the others look for it ----
apps = dict((n, (s, e)) for n, s, e in re.findall(r'\{L"(\w+)", L"([\w-]+\.exe)", L"([\w-]+\.exe)", L"', deskapps))
check("DeskBar knows DeskDrawer and DeskTune", set(apps) == {"DeskDrawer", "DeskTune"}, str(apps))
check("DeskBar looks in %LOCALAPPDATA%\\Programs\\<app>\\<app>.exe", 'KnownFolder(FOLDERID_LocalAppData) + L"\\\\Programs\\\\" + a.name + L"\\\\" + a.exe' in deskapps)
check("DeskDrawer installs to %LOCALAPPDATA%\\Programs\\DeskDrawer\\DeskDrawer.exe",
      'Name = "DeskDrawer"' in dd["Setup.cs"] and 'Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", Name)' in dd["Setup.cs"]
      and 'Path.Combine(Dir, Name + ".exe")' in dd["Setup.cs"] and apps.get("DeskDrawer", ("", ""))[1] == "DeskDrawer.exe")
check("DeskTune installs to %LOCALAPPDATA%\\Programs\\DeskTune\\DeskTune.exe",
      '"Programs", "DeskTune", "DeskTune.exe")' in dt["App.xaml.cs"] and apps.get("DeskTune", ("", ""))[1] == "DeskTune.exe")
check("Quick Settings' DeskTune tile uses the same path", 'DeskAppExe(L"DeskTune")' in src(BAR + "quickpanel.cpp") and "Programs\\\\DeskTune" not in src(BAR + "quickpanel.cpp"))
bar_dir = one(r'#else\s*\nconst wchar_t\* kDirName = L"(\w+)"', install, "DeskBar's install folder") or one(r'kDirName = L"(DeskBar\d*)"; const wchar_t\* kEdition = L"";', install, "DeskBar's install folder")
check("DeskBar installs to %LOCALAPPDATA%\\Programs\\<dir>\\bin\\DeskBar.exe", 'L"\\\\Programs\\\\" + kDirName + L"\\\\bin"' in install, bar_dir)
for exe in (f"{bar_dir}\\bin\\DeskBar.exe", "DeskDrawer\\DeskDrawer.exe", "DeskTune\\DeskTune.exe"):
    check(f"the CI test install looks for {exe}", f"'{exe}'" in workflow or exe.replace("\\", "\\\\") in workflow)
for app in ("DeskDrawer", "DeskTune"):
    check(f"Desk-Setup.exe's \"Open {app}\" box starts the installed {app}", f"{{localappdata}}\\Programs\\{app}\\{app}.exe" in iss)

# ---- 3. setup file names: the release, Desk-Setup.exe, DeskBar's installer page and each app's own updater ----
for app, f in csharp.items():
    asset = one(r'Asset = "([\w.-]+)"', f["Updater.cs"], f"{app}'s update file")
    check(f"{app} updates from the file DeskBar installs it from", asset == apps.get(app, ("?",))[0], f"{asset} / {apps.get(app)}")
    check(f"Desk-Setup.exe carries {asset}", f"{{#Files}}\\{asset}" in iss)
    check(f"{app}'s updater keeps \"setup\" in the downloaded file's name (that is what makes it install itself)", "setup" in (asset or "").lower() and "Path.Combine(dir, Asset)" in f["Updater.cs"])
check("the release has DeskBar-Setup.exe and <app>-Setup.exe", '"$Out\\DeskBar-Setup.exe"' in release and '-Setup.exe"' in release and "{#Files}\\DeskBar-Setup.exe" in iss)

# ---- 4. arguments one app gives another must be ones the other reads ----
check("Desk-Setup.exe -> DeskBar --yes", 'Filename: "{tmp}\\DeskBar-Setup.exe"; Parameters: "--yes"' in iss and 'L"--yes"' in install)
check("Desk-Setup.exe -> DeskDrawer --nostart", 'DeskDrawer-Setup.exe"; Parameters: "--nostart"' in iss and 'args.Contains("--nostart")' in dd["Program.cs"])
check("Desk-Setup.exe -> DeskTune --nostart", 'DeskTune-Setup.exe"; Parameters: "--nostart"' in iss and 'a.Contains("--nostart")' in dt["App.xaml.cs"])
check("Settings > DeskTune > Put everything back -> DeskTune --undoall", 'Launch(DeskAppExe(L"DeskTune"), L"--undoall")' in settingswin and 'a.Contains("--undoall")' in dt["App.xaml.cs"])
check("Settings > DeskDrawer switches -> DeskDrawer --set key=value (not running)", 'Launch(exe, L"--set " + key + L"=" + value)' in deskapps
      and 'e.Args[0] == "--set"' in dd["App.xaml.cs"] and 'or "--set")' in dd["Program.cs"])
check("Settings > DeskDrawer switches -> the running DeskDrawer's pipe takes \"--set \" lines", 'ws line = L"--set " + key' in deskapps and 'StartsWith("--set ")' in dd["MainWindow.Undo.cs"])
check("DeskBar writes to DeskDrawer's own pipe (DeskDrawer-<user>-<session>)",
      'pipe\\\\DeskDrawer-%s-%lu", user, sid)' in deskapps and '"DeskDrawer-" + Environment.UserName + "-" + System.Diagnostics.Process.GetCurrentProcess().SessionId' in dd["Instance.cs"])
check("DeskDrawer's setup closes the running DeskDrawer first (--quit on the pipe)", 'Instance.HandOff(new[] { "--quit" })' in dd["Setup.cs"] and 'paths is ["--quit"]' in dd["MainWindow.Undo.cs"])
check("DeskTune's setup closes the installed DeskTune first", "CloseInstalledCopies();" in dt["App.xaml.cs"])
for flag in sorted(set(re.findall(r'Launch\(DeskAppExe\(L"\w+"\), L"(--[\w-]+)', settingswin + deskapps))):
    check(f"every switch DeskBar passes is known to its app ({flag})", f'"{flag}"' in "".join(dd.values()) + "".join(dt.values()))
for lnk_args in set(re.findall(r'MakeShortcut\([^;]*?, exe, L"(--[\w-]+)"', install)):
    check(f"DeskBar's Start menu shortcut switch {lnk_args} is read", f'L"{lnk_args}"' in appcpp)
check("DeskDrawer's and DeskTune's Uninstall shortcuts are read", '"--uninstall"' in dd["Setup.cs"] and 'args.Contains("--uninstall")' in dd["Program.cs"]
      and '"--uninstall"' in dt["App.xaml.cs"] and 'a.Contains("--uninstall")' in dt["App.xaml.cs"])
check("Start menu shortcuts are named after the apps (Start search finds them by name)", 'Path.Combine(StartMenu, Name + ".lnk")' in dd["Setup.cs"] and 'Path.Combine(dir, "DeskDrawer.lnk")' in dd["Shortcuts.cs"]
      and '"DeskTune.lnk")' in dt["App.xaml.cs"] and 'StartMenuDir() + L"\\\\DeskBar.lnk"' in install)

# ---- 5. "Open folders with DeskDrawer": what DeskDrawer writes is what DeskBar reads ----
verb = one(r'Verb\s*=\s*"(\w+)"', dd["DefaultApp.cs"], "DeskDrawer's folder verb")
check("folder handler: DeskBar reads the verb DeskDrawer writes", f'get(L"Software\\\\Classes\\\\Directory\\\\shell") != L"{verb}"' in shellx
      and f'Software\\\\Classes\\\\Directory\\\\shell\\\\{verb}\\\\command' in shellx and 'ContainsI(v, L"DeskDrawer")' in deskapps, verb)
check("folder handler: the command is \"exe\" \"%1\", which DeskBar takes apart", '$"\\"{exe}\\" \\"%1\\""' in dd["DefaultApp.cs"] and "ExeOfOpenCommand(" in shellx)

# ---- 6. colours: each app's own colour file ----
bar_colour_apps = re.findall(r'L"(\w+)"', one(r'kColourApps\[\]\s*=\s*\{([^}]*)\}', deskapps, "DeskBar's colour apps") or "")
for app, f in csharp.items():
    name = one(r'public const string App = "(\w+)"', f["Theme.cs"], f"{app}'s colour name")
    check(f"{app}'s own colour file is one DeskBar writes (theme-{name}.txt)", name in bar_colour_apps and '"theme-" + App + ".txt"' in f["Theme.cs"] and 'L"\\\\theme-" + app + L".txt"' in deskapps, ", ".join(bar_colour_apps))

# ---- 7. DeskMode's shortcuts: the keys registered are the keys the settings page lists ----
keys = one(r'"([A-Z]{4})"\[i\]', displaymode, "DeskMode's hotkeys")
listed = "".join(re.findall(r'key\(L"[^"]+", L"([A-Z])"\)', settingswin))
check("Ctrl+Alt+D / M / B / F: registered keys match the settings page", keys == listed and keys == "DMBF", f"{keys} vs {listed}")
check("Ctrl+Alt+D opens DeskMode, M Go max, B Go easy, F borderless", "id == 0) UiPost([] { if (Bar* b = BarHere()) OpenDeskModePanel(b); })" in displaymode
      and "id == 3) ToggleBorderless" in displaymode and "DeskModeGo(id == 1)" in displaymode)
check("the hotkeys start with the real taskbar", "DeskModeHotkeysStart();" in appcpp)

fails = 0
for ok, name, detail in results:
    print(("PASS " if ok else "FAIL ") + name + (f"  [{detail}]" if detail and not ok else ""))
    fails += not ok
print(f"{len(results) - fails} passed, {fails} failed")
if os.environ.get("GITHUB_ACTIONS"):
    for ok, name, detail in results:
        if not ok:
            print(f"::error::launch contract: {name} {detail}")
sys.exit(min(fails, 100))
