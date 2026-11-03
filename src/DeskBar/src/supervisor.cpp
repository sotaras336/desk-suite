#include "pch.h"
#include "app.h"

// Supervisor ("--supervise"): shows the stand-in bar at once, starts the taskbar, starts the input helper, and restarts the taskbar if it
// exits or stops answering. After more than 5 crashes in 3 minutes it gives Windows its taskbar back ("--restore") instead of looping.
// Plain GDI + WIC only: no D2D / DirectWrite / WinRT is loaded here, so this process is up in a few milliseconds even at a cold sign-in.

// ======================================================= stand-in bar
// One topmost window per monitor, drawn with GDI: the user sees a bar (start logo, pinned icons, clock) while the real one loads. It is clickable.
namespace {
struct Slot { HBITMAP bmp = nullptr; int w = 0, h = 0; ws launch; };
struct Standin {
    HWND hwnd; HMONITOR mon; float scale; int barH; std::vector<Slot> slots; HFONT font = nullptr; ws lastClock; uint64_t lastLaunch = 0;
};
std::vector<Standin*> g_stand; std::atomic<bool> g_standStop{false}; HANDLE g_ready = nullptr;
std::vector<PinInfo> g_pinsForStandin;

HBITMAP LoadPng(const ws& path, int px) {
    ComPtr<IWICImagingFactory> f; if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return nullptr;
    ComPtr<IWICBitmapDecoder> d; if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(d->GetFrame(0, &fr))) return nullptr;
    ComPtr<IWICFormatConverter> c; f->CreateFormatConverter(&c); if (FAILED(c->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    UINT w, h; c->GetSize(&w, &h); BITMAPINFO bi{}; bi.bmiHeader = {sizeof bi.bmiHeader, (LONG)w, -(LONG)h, 1, 32}; void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); if (!hb) return nullptr; c->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits); (void)px; return hb;
}
void DrawStandin(Standin& s) {
    RECT cr; GetClientRect(s.hwnd, &cr); HDC wdc = GetDC(s.hwnd); HDC dc = CreateCompatibleDC(wdc); HBITMAP buf = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); HGDIOBJ old = SelectObject(dc, buf);
    auto rgb = [](uint32_t c) { auto f = Col(c); return RGB((int)lroundf(f.r * 255), (int)lroundf(f.g * 255), (int)lroundf(f.b * 255)); };   // the stand-in wears the chosen colour theme too
    HBRUSH bg = CreateSolidBrush(rgb(K::Bar)); FillRect(dc, &cr, bg); DeleteObject(bg);
    auto D = [&](float v) { return (int)lroundf(v * s.scale); }; int cy = cr.bottom / 2;
    HBRUSH logo = CreateSolidBrush(rgb(0x3FB6F5)); int sq = D(10.2f), gap = D(1.6f), cx = D(36);   // four squares, centred at x = 36 dip
    for (int i = 0; i < 4; i++) { int x = cx - sq - gap / 2 + (i & 1) * (sq + gap), y = cy - sq - gap / 2 + (i >> 1) * (sq + gap); RECT r{x, y, x + sq, y + sq}; FillRect(dc, &r, logo); } DeleteObject(logo);
    int isz = D(24); for (size_t i = 0; i < s.slots.size(); i++) {
        if (!s.slots[i].bmp) continue; int x = D(84 + 48.f * i) - isz / 2, y = cy - isz / 2; HDC m = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(m, s.slots[i].bmp);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA}; AlphaBlend(dc, x, y, isz, isz, m, 0, 0, s.slots[i].w, s.slots[i].h, bf); SelectObject(m, o); DeleteDC(m);
    }
    SYSTEMTIME st; GetLocalTime(&st); wchar_t t[64], d[64]; GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, t, 64); GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 64, nullptr);
    SelectObject(dc, s.font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    RECT tr{0, cy - D(18), cr.right - D(26), cy}, dr{0, cy, cr.right - D(26), cy + D(18)}; DrawTextW(dc, t, -1, &tr, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE); DrawTextW(dc, d, -1, &dr, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, old); DeleteObject(buf); DeleteDC(dc); ReleaseDC(s.hwnd, wdc);
}
LRESULT CALLBACK StandinProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Standin* s = (Standin*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (s) DrawStandin(*s); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONUP:
        if (s) { int slot = (int)floorf((GET_X_LPARAM(l) / s->scale - 60) / 48); uint64_t now = Now();
            if (slot >= 0 && slot < (int)s->slots.size() && !s->slots[slot].launch.empty() && now - s->lastLaunch > 1500) { s->lastLaunch = now; Launch(s->slots[slot].launch); } }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void StandinThread() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = StandinProc; wc.hInstance = g_inst; wc.lpszClassName = L"DB3.Standin"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    ws iconDir = DataPath(L"icons");
    for (auto& m : Monitors()) {
        auto* s = new Standin(); s->mon = m.h; s->scale = m.scale; s->barH = (int)lroundf(48 * m.scale);
        s->hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"DeskBar", WS_POPUP | WS_VISIBLE, m.bounds.left, m.bounds.bottom - s->barH, m.bounds.right - m.bounds.left, s->barH, nullptr, nullptr, g_inst, nullptr);
        SetWindowLongPtrW(s->hwnd, GWLP_USERDATA, (LONG_PTR)s);
        s->font = CreateFontW(-(int)lroundf(12 * m.scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Variable Text");
        int px = (int)lroundf(24 * m.scale);
        for (auto& p : g_pinsForStandin) {
            Slot sl; sl.launch = p.launch; sl.bmp = LoadPng(iconDir + L"\\" + Hex16(Sha1Prefix(p.iconKey)) + L"_" + std::to_wstring(px) + L".png", px);
            if (sl.bmp) { BITMAP b; GetObjectW(sl.bmp, sizeof b, &b); sl.w = b.bmWidth; sl.h = b.bmHeight; } s->slots.push_back(sl);
        }
        g_stand.push_back(s); InvalidateRect(s->hwnd, nullptr, FALSE);
    }
    uint64_t t0 = Now(), readyAt = 0; MSG msg; UINT_PTR tm = SetTimer(nullptr, 1, 200, nullptr); int tick = 0;
    while (!g_standStop) {
        DWORD r = MsgWaitForMultipleObjects(1, &g_ready, FALSE, 250, QS_ALLINPUT);
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_TIMER) { Explorer::KeepHidden(); for (auto* s : g_stand) { SetWindowPos(s->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); if (++tick % 5 == 0) InvalidateRect(s->hwnd, nullptr, FALSE); } continue; } TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (r == WAIT_OBJECT_0 && !readyAt) readyAt = Now();
        if ((readyAt && Now() - readyAt > 350) || Now() - t0 > 45000) break;   // the real bar has drawn: remove the stand-in
    }
    KillTimer(nullptr, tm); for (auto* s : g_stand) { DestroyWindow(s->hwnd); for (auto& sl : s->slots) if (sl.bmp) DeleteObject(sl.bmp); DeleteObject(s->font); delete s; } g_stand.clear();
}

bool Answers(DWORD pid) {   // does any window of that process answer within 1.5 s?
    struct C { DWORD pid; bool any, ok; } c{pid, false, false};
    EnumWindows([](HWND h, LPARAM p) -> BOOL { auto& c = *(C*)p; DWORD q; GetWindowThreadProcessId(h, &q); if (q != c.pid) return TRUE; c.any = true; DWORD_PTR r; if (SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1500, &r)) { c.ok = true; return FALSE; } return TRUE; }, (LPARAM)&c);
    return !c.any || c.ok;
}
}

int RunSupervisor() {
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"DB3.Supervisor"); if (GetLastError() == ERROR_ALREADY_EXISTS) return 0; (void)mtx;
    LogInit(false); Boot(L"supervisor main"); OleInitialize(nullptr); LoadSettings();
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); ws dir = DirOf(exe);
    std::vector<uint64_t> crashes; int tick = 0;
    for (;;) {
        // The stand-in bar first: at sign-in even starting a process can take seconds, and the stand-in is clickable
        LoadSettings();   // each round: the stand-in wears the colour theme that was just picked
        g_ready = CreateEventW(nullptr, TRUE, FALSE, N_READY); ResetEvent(g_ready); g_standStop = false; g_pinsForStandin = LoadPinCache();
        std::thread standin(StandinThread); Boot(L"supervisor: stand-in shown");
        ws cmd = L"\"" + ws(exe) + L"\" --run --sup"; STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) { Log(L"supervisor: cannot start taskbar (%lu)", GetLastError()); g_standStop = true; standin.join(); return 1; }
        // Sign-in is crowded (every startup app wants the CPU): run the taskbar ahead of them for the first minute
        uint64_t boostEnds = Now() + 60000; SetPriorityClass(pi.hProcess, HIGH_PRIORITY_CLASS); SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        Explorer::Hide(); HooksEnsure(GetCurrentProcessId()); Boot(L"supervisor: taskbar + helper started");
        int unresponsive = 0; bool killed = false;
        while (WaitForSingleObject(pi.hProcess, 1000) == WAIT_TIMEOUT) {
            if (boostEnds && Now() > boostEnds) { boostEnds = 0; SetPriorityClass(pi.hProcess, ABOVE_NORMAL_PRIORITY_CLASS); SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS); }
            if (++tick % 5 == 0) HooksEnsure(GetCurrentProcessId());          // restart the helper if it ever died
            unresponsive = Answers(pi.dwProcessId) ? 0 : unresponsive + 1;
            if (unresponsive >= 4) { Log(L"supervisor: DeskBar stopped answering for ~12 s, restarting it"); TerminateProcess(pi.hProcess, 9); killed = true; break; }
        }
        DWORD code = 0; WaitForSingleObject(pi.hProcess, 3000); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        g_standStop = true; standin.join(); CloseHandle(g_ready);
        if (code == 0 && !killed) return 0;       // quit on purpose
        if (!killed && (code == 0x40010004 || code == 0xC000026B)) return 0;   // Windows is signing out / shutting down (it ends us, and any process started then dies at DLL init): not a crash, don't restart or count it
        Log(L"supervisor: DeskBar exited with %lu, restarting", code);
        uint64_t now = Now(); crashes.push_back(now); std::erase_if(crashes, [&](uint64_t t) { return now - t > 180000; });
        if (crashes.size() > 5) {   // crashing over and over: give the user their Windows taskbar back instead of looping
            ws rc = L"\"" + ws(exe) + L"\" --restore"; STARTUPINFOW s2{sizeof s2}; PROCESS_INFORMATION p2{};
            if (CreateProcessW(nullptr, rc.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &s2, &p2)) { WaitForSingleObject(p2.hProcess, 5000); CloseHandle(p2.hProcess); CloseHandle(p2.hThread); }
            return 1;
        }
        Sleep(300);
    }
}
