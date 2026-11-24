#include "pch.h"
#include "app.h"
#include <dbghelp.h>

// Freeze recorder + watchdog. The UI thread stamps a heartbeat from a plain Win32 timer; this thread notices when the message loop stops.
// After 6 s of silence stuck.txt gets the time and the UI thread's stack WITH function names (dbghelp + the .pdb next to the exe), so the
// next freeze can be diagnosed instead of guessed at. After 20 s (40 s more while starting up) the process ends with exit code 7 and the
// supervisor restarts it.
namespace {
const char* FileNameOfA(const char* p) { const char* r = p; for (; *p; p++) if (*p == '\\' || *p == '/') r = p + 1; return r; }
std::atomic<uint64_t> g_beat{0}; std::atomic<bool> g_first{false}; DWORD g_uiTid = 0; bool g_kill = false;

void DumpStack(DWORD tid, const wchar_t* reason, HANDLE target = nullptr, const ws* outPath = nullptr) {
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid); if (!th) return;
    std::string out; SYSTEMTIME st; GetLocalTime(&st); char hdr[160]; sprintf_s(hdr, "%04d-%02d-%02d %02d:%02d:%02d  %S\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, reason); out += hdr;
    HANDLE proc = target ? target : GetCurrentProcess(); SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES); SymInitialize(proc, nullptr, TRUE);
    if (SuspendThread(th) != (DWORD)-1) {
        CONTEXT c{}; c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, &c)) {
            STACKFRAME64 f{}; f.AddrPC = {c.Rip, 0, AddrModeFlat}; f.AddrFrame = {c.Rbp, 0, AddrModeFlat}; f.AddrStack = {c.Rsp, 0, AddrModeFlat};
            for (int i = 0; i < 48; i++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &f, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset) break;
                char buf[sizeof(SYMBOL_INFO) + 256]; auto* sym = (SYMBOL_INFO*)buf; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255; DWORD64 disp = 0;
                IMAGEHLP_MODULE64 mod{sizeof mod}; SymGetModuleInfo64(proc, f.AddrPC.Offset, &mod);
                char line[400]; if (SymFromAddr(proc, f.AddrPC.Offset, &disp, sym)) sprintf_s(line, "  %s!%s+0x%llx\r\n", mod.ModuleName, sym->Name, disp); else sprintf_s(line, "  %s+0x%llx\r\n", mod.ModuleName[0] ? mod.ModuleName : "?", f.AddrPC.Offset - mod.BaseOfImage);
                out += line;
            }
        }
        ResumeThread(th);
    }
    SymCleanup(proc); CloseHandle(th); out += "\r\n";
    HANDLE f = CreateFileW((outPath ? *outPath : DataPath(L"stuck.txt")).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, out.data(), (DWORD)out.size(), &w, nullptr); CloseHandle(f); }
}
void Thread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); uint64_t t0 = Now(), last = Now(); bool wrote = false;
    for (;;) {
        Sleep(500); uint64_t tn = Now(); bool woke = tn - last > 5000; last = tn;
        if (woke) { g_beat = tn; wrote = false; continue; }   // this thread itself was frozen for seconds: the PC slept. The UI hasn't had a chance to beat yet, so that is not a freeze
        uint64_t age = Now() - g_beat; uint64_t grace = g_first ? 0 : 40000;
        if (ws rq = DataPath(L"stack.req"); FileExists(rq)) {   // debug: a file named stack.req in the data dir dumps every thread's stack to stuck.txt (for "why is it using CPU?")
            DeleteFileW(rq.c_str()); HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 te{sizeof te};
            if (s != INVALID_HANDLE_VALUE) { for (BOOL ok = Thread32First(s, &te); ok; ok = Thread32Next(s, &te)) if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != GetCurrentThreadId()) DumpStack(te.th32ThreadID, Fmt(L"on request, tid %lu%s", te.th32ThreadID, te.th32ThreadID == g_uiTid ? L" (UI)" : L"").c_str()); CloseHandle(s); }
        }
        if (age < 1000) { wrote = false; continue; }
        if (age > 6000 + grace && !wrote) { wrote = true; DumpStack(g_uiTid, Fmt(L"UI thread silent for %d s", (int)(age / 1000)).c_str()); Log(L"watchdog: UI thread silent for %d s (stack in stuck.txt)", (int)(age / 1000)); }
        if (g_kill && age > 20000 + grace) {
            SYSTEMTIME st; GetLocalTime(&st); std::string line = Narrow(Fmt(L"%04d-%02d-%02d %02d:%02d:%02d  DeskBar stopped answering for %d s and was restarted (its stack is in stuck.txt)\r\n\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (int)(age / 1000)));
            HANDLE h = CreateFileW(DataPath(L"crash.txt").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, line.data(), (DWORD)line.size(), &w, nullptr); CloseHandle(h); }   // the next start offers to report it
            Log(L"watchdog: UI thread dead for %d s, ending the process so the supervisor restarts it", (int)(age / 1000)); ExitProcess(7); }
        (void)t0;
    }
}
}
// Called from the crash filter: what crashed and the crashing thread's stack, with function names, appended to crash.txt (kept under 64 KB).
// The next start offers to send it with Report a bug (CrashReportCheck below). Plain Win32 + dbghelp only: the heap may be broken here.
void WriteCrashReport(EXCEPTION_POINTERS* e) {
    static std::atomic<bool> once{false}; if (once.exchange(true)) return;
    char buf[8192]; int n = 0; auto put = [&](const char* f, auto... a) { if (n < (int)sizeof buf - 1) { int w = _snprintf_s(buf + n, sizeof buf - n, _TRUNCATE, f, a...); n = w < 0 ? (int)sizeof buf - 1 : n + w; } };
    SYSTEMTIME st; GetLocalTime(&st); wchar_t exe[MAX_PATH] = L""; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    put("%04d-%02d-%02d %02d:%02d:%02d  DeskBar crashed: code %08lx, %S\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, e->ExceptionRecord->ExceptionCode, exe);
    HANDLE proc = GetCurrentProcess(); SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (SymInitialize(proc, nullptr, TRUE)) {
        CONTEXT c = *e->ContextRecord; STACKFRAME64 f{}; f.AddrPC = {c.Rip, 0, AddrModeFlat}; f.AddrFrame = {c.Rbp, 0, AddrModeFlat}; f.AddrStack = {c.Rsp, 0, AddrModeFlat};
        for (int i = 0; i < 40; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &f, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) || !f.AddrPC.Offset) break;
            char sb[sizeof(SYMBOL_INFO) + 256]; auto* sym = (SYMBOL_INFO*)sb; sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 255; DWORD64 disp = 0;
            IMAGEHLP_MODULE64 mod{sizeof mod}; SymGetModuleInfo64(proc, f.AddrPC.Offset, &mod);
            IMAGEHLP_LINE64 ln{sizeof ln}; DWORD ld = 0; bool hasLine = SymGetLineFromAddr64(proc, f.AddrPC.Offset, &ld, &ln) != FALSE;
            if (SymFromAddr(proc, f.AddrPC.Offset, &disp, sym)) { if (hasLine) put("  %s!%s+0x%llx  (%s:%lu)\r\n", mod.ModuleName, sym->Name, disp, FileNameOfA(ln.FileName), ln.LineNumber); else put("  %s!%s+0x%llx\r\n", mod.ModuleName, sym->Name, disp); }
            else put("  %s+0x%llx\r\n", mod.ModuleName[0] ? mod.ModuleName : "?", f.AddrPC.Offset - mod.BaseOfImage);
        }
        SymCleanup(proc);
    }
    put("\r\n");
    ws path = DataPath(L"crash.txt"); WIN32_FILE_ATTRIBUTE_DATA d{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) && d.nFileSizeLow > 64 * 1024) DeleteFileW(path.c_str());   // only the last few crashes matter
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, buf, (DWORD)n, &w, nullptr); FlushFileBuffers(h); CloseHandle(h); }
}
void WatchdogStart(bool kill) {
    g_uiTid = GetCurrentThreadId(); g_kill = kill; g_beat = Now(); std::thread(Thread).detach();
}
void WatchdogBeat() { g_beat = Now(); g_first = true; }
void StallForTest(int secs) { Sleep(secs * 1000); }   // --open stall: freezes the UI thread on purpose to check the recorder
// "DeskBar.exe --stackof <pid> <file>": stack of every thread of another process (a hung installer, a stuck supervisor), with function names.
int RunStackOf(DWORD pid, const ws& outPath) {
    HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid); if (!p) { WriteAtomic(outPath, Narrow(Fmt(L"OpenProcess failed %lu\r\n", GetLastError()))); return 1; }
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 te{sizeof te};
    if (s != INVALID_HANDLE_VALUE) { for (BOOL ok = Thread32First(s, &te); ok; ok = Thread32Next(s, &te)) if (te.th32OwnerProcessID == pid) DumpStack(te.th32ThreadID, Fmt(L"pid %lu tid %lu", pid, te.th32ThreadID).c_str(), p, &outPath); CloseHandle(s); }
    CloseHandle(p); return 0;
}
