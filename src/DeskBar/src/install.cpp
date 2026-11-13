#include "pch.h"
#include "app.h"

// Install / uninstall. DeskBar.exe is one self-contained file (static CRT, no framework), so the "installer" is the same exe: run it as
// "DeskBar.exe --install" (or rename it to anything containing "Setup" and double-click). Nothing needs admin rights.
namespace {
#ifdef PINK
const wchar_t* kDirName = L"DeskBar"; const wchar_t* kEdition = L" (pink edition)";
#else
const wchar_t* kDirName = L"DeskBar3"; const wchar_t* kEdition = L"";
#endif
// ...\Programs\DeskBar3\bin: the exact path Programs\DeskBar3\DeskBar.exe was refused ("Access is denied") on the first machine for a reason we never found, other paths run fine
ws InstallDir() { return KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\" + kDirName + L"\\bin"; }
ws StartMenuDir() { return KnownFolder(FOLDERID_StartMenu) + L"\\Programs\\DeskBar"; }
ws g_runOut;   // what the last waited-for command printed (says why it failed)
ws Ps(const ws& script) { return L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + script + L"\""; }   // script must not contain double quotes
DWORD Run(const ws& cmd, bool wait = true) {
    ws c = cmd; STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; PROCESS_INFORMATION pi{}; DWORD code = (DWORD)-1; g_runOut.clear();
    HANDLE rd = nullptr, wr = nullptr; SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    if (wait && CreatePipe(&rd, &wr, &sa, 0)) { SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0); si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); }
    // started without a console, in its own process group and outside any job: closing the terminal that ran the installer must not kill DeskBar
    DWORD fl = wait ? CREATE_NO_WINDOW : DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(nullptr, c.data(), nullptr, nullptr, wr != nullptr, fl, nullptr, nullptr, &si, &pi);
    if (!ok && !wait) ok = CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (wr) CloseHandle(wr);
    if (ok) {
        if (wait) { std::string out; char b[512]; DWORD n; while (rd && ReadFile(rd, b, sizeof b, &n, nullptr) && n) out.append(b, n); WaitForSingleObject(pi.hProcess, 20000); GetExitCodeProcess(pi.hProcess, &code); g_runOut = Widen(out); }
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    } else g_runOut = Fmt(L"could not start (%lu)", GetLastError());
    if (ok && !wait) code = 0;
    if (rd) CloseHandle(rd); return code;
}
void IL(const wchar_t* f, ...) {   // install.log next to the installer exe (the user's real folder, readable from anywhere)
    static ws path; if (path.empty()) { wchar_t m[MAX_PATH]; GetModuleFileNameW(nullptr, m, MAX_PATH); path = DirOf(m) + L"\\install.log"; }
    va_list va; va_start(va, f); wchar_t b[1500]; _vsnwprintf_s(b, _TRUNCATE, f, va); va_end(va);
    SYSTEMTIME t; GetLocalTime(&t); std::string line = Narrow(Fmt(L"%02d:%02d:%02d %s\r\n", t.wHour, t.wMinute, t.wSecond, b));
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, line.data(), (DWORD)line.size(), &w, nullptr); CloseHandle(h); }
}
int CountAt(const ws& dir) {
    int n = 0; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return -1; PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) if (EqI(pe.szExeFile, L"DeskBar.exe") && StartsI(ExePathOfPid(pe.th32ProcessID), dir + L"\\")) n++;
    CloseHandle(s); return n;
}
void MakeShortcut(const ws& lnk, const ws& target, const ws& args, const ws& desc) {
    ComPtr<IShellLinkW> sl; if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return;
    sl->SetPath(target.c_str()); sl->SetArguments(args.c_str()); sl->SetDescription(desc.c_str()); sl->SetIconLocation(target.c_str(), 0); sl->SetWorkingDirectory(DirOf(target).c_str());
    ComPtr<IPersistFile> pf; if (SUCCEEDED(sl.As(&pf))) pf->Save(lnk.c_str(), TRUE);
}
void KillOthersAt(const ws& dir) {   // by install path, never by image name: that would also kill other copies (DeskBar2, a dev build)
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (s == INVALID_HANDLE_VALUE) return; PROCESSENTRY32W pe{sizeof pe};
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) {
        if (pe.th32ProcessID == GetCurrentProcessId()) continue; ws p = ExePathOfPid(pe.th32ProcessID); if (p.empty() || !StartsI(p, dir + L"\\")) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID); if (h) { TerminateProcess(h, 0); CloseHandle(h); }
    }
    CloseHandle(s);
}
}

// The sign-in task: at sign-in, normal rights, above-normal priority (the default "below normal" made sign-in much slower), restart 3x/1 min, no time limit.
DWORD RegisterSignInTask(const ws& exe) {
    ws user = []() { wchar_t u[256], d[256]; DWORD un = 256, dn = 256; GetUserNameW(u, &un); GetEnvironmentVariableW(L"USERDOMAIN", d, dn); return ws(d) + L"\\" + u; }();
    ws xml = L"<?xml version=\"1.0\" encoding=\"UTF-16\"?><Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + user + L"</UserId></LogonTrigger></Triggers>"
        L"<Principals><Principal id=\"A\"><UserId>" + user + L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
        L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>3</Priority>"
        L"<RestartOnFailure><Interval>PT1M</Interval><Count>3</Count></RestartOnFailure></Settings>"
        L"<Actions Context=\"A\"><Exec><Command>" + exe + L"</Command><Arguments>--supervise</Arguments></Exec></Actions></Task>";
    ws xf = DirOf(exe) + L"\\task.xml"; { std::string raw((const char*)xml.c_str(), xml.size() * 2); raw.insert(0, "\xFF\xFE", 2); WriteAtomic(xf, raw); }
    ws xq = xf; for (size_t p = 0; (p = xq.find(L'\'', p)) != ws::npos; p += 2) xq.insert(p, L"'");   // PowerShell single-quote escaping (a user name can hold an apostrophe)
    DWORD rc = Run(Ps(L"Register-ScheduledTask -TaskName '" N_TASK L"' -Xml (Get-Content -Raw -LiteralPath '" + xq + L"') -Force | Out-Null")); DeleteFileW(xf.c_str());   // schtasks.exe fails on some PCs; the ScheduledTasks cmdlets don't
    return rc;
}

// What the task is doing: "Ready", "Running", "Disabled"; empty when there is no such task; "?" when PowerShell could not tell (never treated as missing).
ws SignInTaskState() {
    DWORD rc = Run(Ps(L"$t = Get-ScheduledTask -TaskName '" N_TASK L"' -ErrorAction SilentlyContinue; if ($t) { [string]$t.State } else { '' }"));
    if (rc != 0) return L"?";
    ws s = g_runOut; while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back(); return s;
}

int RunInstall() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); LogInit(false);
    bool yes = wcsstr(GetCommandLineW(), L"--yes") != nullptr;   // already confirmed by the user: no dialogs
    if (!yes && MessageBoxW(nullptr, Fmt(L"Install DeskBar%s?\n\nIt replaces the Windows taskbar, Start menu and Quick Settings for your account.\nDeskBar2, if installed, is switched off (its sign-in task is disabled, not deleted).\nYou can remove DeskBar any time from Start > DeskBar > Uninstall DeskBar.", kEdition).c_str(), L"DeskBar", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) return 0;
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH); ws dir = InstallDir(), exe = dir + L"\\DeskBar.exe";
#ifndef PINK
    {   // the previous generation (DeskBar2) would fight us for the taskbar: switch its sign-in task off and stop it. Undo: schtasks /Change /TN DeskBar2 /ENABLE
        Run(L"schtasks.exe /Change /TN DeskBar2 /DISABLE"); KillOthersAt(KnownFolder(FOLDERID_LocalAppData) + L"\\Programs\\DeskBar2");
    }
#endif
    IL(L"install start: self=%s target=%s elevated-user=%s", self, exe.c_str(), KnownFolder(FOLDERID_LocalAppData).c_str());
    KillOthersAt(dir); Sleep(500); SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (!EqI(self, exe) && !CopyFileW(self, exe.c_str(), FALSE)) { IL(L"copy failed %lu", GetLastError()); Explorer::Restore(); if (!yes) MessageBoxW(nullptr, L"Couldn't copy DeskBar into place (is it still running?).", L"DeskBar", MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND); return 1; }   // Restore: the old bar was just stopped, never leave the user without a taskbar
    if (ContainsI(FileNameOf(self), L"bugs")) {   // DeskBar-Setup-Bugs.exe: the tester's copy, with "Report a bug to Claude" switched on (kept across updates: it is a setting)
        LoadSettings(); S.bugReports = true; SaveSettingsNow(); IL(L"bug reports switched on");
    }
    DWORD rc = RegisterSignInTask(exe);
    if (rc != 0) { Log(L"install: Register-ScheduledTask failed (%lu): %s", rc, g_runOut.c_str()); IL(L"Register-ScheduledTask failed (%lu): %s", rc, g_runOut.c_str()); if (!yes) MessageBoxW(nullptr, (L"Couldn't create the sign-in task (DeskBar will still start through the Run key):\n\n" + g_runOut).c_str(), L"DeskBar", MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND); }
    if (rc != 0) {   // no scheduled-task rights: fall back to the HKCU Run key (sign-in start is delayed by Windows, but it works)
        HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == 0) { ws v = L"\"" + exe + L"\" --supervise"; RegSetValueExW(k, L"DeskBar", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)(v.size() + 1) * 2); RegCloseKey(k); }
    }
    SHCreateDirectoryExW(nullptr, StartMenuDir().c_str(), nullptr);
    MakeShortcut(StartMenuDir() + L"\\DeskBar.lnk", exe, L"--supervise", L"Start DeskBar"); MakeShortcut(StartMenuDir() + L"\\Uninstall DeskBar.lnk", exe, L"--uninstall", L"Remove DeskBar");
    IL(L"task create rc=%lu out=%s", rc, g_runOut.c_str());
    if (rc == 0) { DWORD r2 = Run(Ps(L"Start-ScheduledTask -TaskName '" N_TASK L"'")); IL(L"task run rc=%lu out=%s", r2, g_runOut.c_str()); if (r2 != 0) rc = r2; }
    if (rc != 0) { DWORD r3 = Run(L"\"" + exe + L"\" --supervise", false); IL(L"direct start rc=%lu err=%s", r3, g_runOut.c_str()); }
    Sleep(4000); IL(L"DeskBar processes running from the install dir: %d", CountAt(dir));
    if (!yes) MessageBoxW(nullptr, L"DeskBar is installed and starting.", L"DeskBar", MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND); return 0;
}
int RunUninstall() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); LogInit(false); LoadSettings();
    if (MessageBoxW(nullptr, L"Remove DeskBar and bring back the Windows taskbar?", L"DeskBar", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) return 0;
    ws dir = InstallDir(); Run(Ps(L"Stop-ScheduledTask -TaskName '" N_TASK L"'")); KillOthersAt(dir); Sleep(700);
    Explorer::Restore(); Sleep(400);
    Run(Ps(L"Unregister-ScheduledTask -TaskName '" N_TASK L"' -Confirm:$false")); { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &k) == 0) { RegDeleteValueW(k, L"DeskBar"); RegCloseKey(k); } }
    ws sm = StartMenuDir(); DeleteFileW((sm + L"\\DeskBar.lnk").c_str()); DeleteFileW((sm + L"\\Uninstall DeskBar.lnk").c_str()); RemoveDirectoryW(sm.c_str());
    Run(L"cmd.exe /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + DirOf(dir) + L"\"", false);   // the folder goes once this process has ended
    MessageBoxW(nullptr, L"DeskBar was removed.", L"DeskBar", MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND); return 0;
}

// Self-heal: DeskBar once stopped starting with Windows because its sign-in task was gone (nothing else started it, and nothing said so).
// The installed copy checks once, a minute after it starts, and puts the task back when it is missing. A dev or test copy never touches it.
void EnsureSignInTask() {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH); ws exe = InstallDir() + L"\\DeskBar.exe";
    if (!EqI(self, exe) || GetEnvironmentVariableW(L"DB3_DATA", nullptr, 0)) return;
    if (!SignInTaskState().empty()) return;   // there (whatever its state: a disabled task is the user's choice), or PowerShell could not tell ("?"): leave it alone
    DWORD rc = RegisterSignInTask(exe); Log(L"sign-in task was missing: re-created, rc=%lu %s", rc, g_runOut.c_str());
    if (rc != 0) {   // no scheduled-task rights: fall back to the HKCU Run key (a minute or two later at sign-in, but it works)
        HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == 0) { ws v = L"\"" + exe + L"\" --supervise"; RegSetValueExW(k, L"DeskBar", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)(v.size() + 1) * 2); RegCloseKey(k); }
    }
}

// Settings > "Start DeskBar when I sign in": on re-creates the task if it is gone, then enables it; off disables it.
void SetSignInTask(bool on) {
    if (on && SignInTaskState() == L"") { wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH); RegisterSignInTask(self); }
    Run(Ps(std::wstring(on ? L"Enable" : L"Disable") + L"-ScheduledTask -TaskName '" N_TASK L"' | Out-Null"));
}

// dev: --signintest <report>: the self-heal's view of the sign-in task; when it is missing, re-creates it for the INSTALLED exe exactly as the installer does. Touches the real task: run it on purpose.
int SignInTest(const ws& report) {
    ws exe = InstallDir() + L"\\DeskBar.exe", before = SignInTaskState(), out; DWORD rc = 0;
    if (before.empty()) { rc = RegisterSignInTask(exe); out = g_runOut; }
    ws after = SignInTaskState();
    WriteAtomic(report, Narrow(Fmt(L"before=[%s] register rc=%lu after=[%s] %s\r\n", before.c_str(), rc, after.c_str(), out.c_str())));
    return after.empty() || after == L"?" ? 1 : 0;
}
