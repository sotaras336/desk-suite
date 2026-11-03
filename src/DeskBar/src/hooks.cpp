#include "pch.h"
#include "app.h"

// The input helper ("--hooks <supervisorPid>"): a separate small process that owns the system-wide keyboard and mouse hooks. Those hooks
// sit in the path of EVERY key press and mouse move on the PC, so they must never live in a program that can stall. This one does
// nothing else, so it cannot: if the taskbar freezes or crashes, the keyboard and mouse carry on as normal. It tells the taskbar about
// the Windows key through a named event.
void TypeAheadBegin(); bool TypeAheadActive(); bool TypeAheadPress(UINT vk, UINT scan); bool TypeAheadSwallowed(UINT vk);
namespace {
const ULONG_PTR kTag = 0xDB3DB3;       // our own injected keys: the hook lets them through
HANDLE g_winKeyEv = nullptr; HHOOK g_kb = nullptr, g_ms = nullptr;
bool g_winDown = false, g_comboUsed = false; uint64_t g_lastWinDown = 0; bool g_testInject = false;

void SendTagged(std::initializer_list<std::pair<WORD, bool>> keys) {   // (vk, isUp)
    std::vector<INPUT> in; for (auto& k : keys) { INPUT i{}; i.type = INPUT_KEYBOARD; i.ki.wVk = k.first; i.ki.dwFlags = k.second ? KEYEVENTF_KEYUP : 0; i.ki.dwExtraInfo = kTag; in.push_back(i); }
    SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}
bool Down(int vk) { return GetAsyncKeyState(vk) & 0x8000; }

// ---- Windows key ----
LRESULT CALLBACK KbProc(int code, WPARAM w, LPARAM l) {
    if (code < 0) return CallNextHookEx(g_kb, code, w, l);
    auto* k = (KBDLLHOOKSTRUCT*)l;
    if (k->dwExtraInfo == kTag || !S.winKeyOpensStart) return CallNextHookEx(g_kb, code, w, l);
    bool down = w == WM_KEYDOWN || w == WM_SYSKEYDOWN, up = w == WM_KEYUP || w == WM_SYSKEYUP; UINT vk = k->vkCode;
    if (vk == VK_LWIN || vk == VK_RWIN) {
        if (down) {   // held keys repeat every ~30 ms; a gap means a fresh press (even if we missed the last key-up)
            uint64_t now = GetTickCount64(); if (!g_winDown || now - g_lastWinDown > 700) { g_winDown = true; g_comboUsed = false; } g_lastWinDown = now;
            return CallNextHookEx(g_kb, code, w, l);
        }
        if (up && g_winDown) {
            g_winDown = false; bool injectedByOthers = !g_testInject && (k->flags & LLKHF_INJECTED);
            if (!g_comboUsed && !injectedByOthers && !Down(VK_LBUTTON) && !Down(VK_RBUTTON) && !Down(VK_MBUTTON)) {
                // Swallow this Win-up and replay it after a dummy key, so Windows sees "Win+something" and doesn't open its own Start. Then open ours.
                // Right here, not later: the replay must reach Windows before the next key press does.
                SendTagged({{0xE8, false}, {0xE8, true}, {(WORD)vk, true}});
                AllowSetForegroundWindow(ASFW_ANY); TypeAheadBegin(); SetEvent(g_winKeyEv);
                return 1;
            }
        }
        return CallNextHookEx(g_kb, code, w, l);
    }
    if (TypeAheadActive() && !g_winDown && !Down(VK_CONTROL) && !Down(VK_MENU)) {   // typing straight after the Windows key: keep the letters for Start's search box
        if (down && TypeAheadPress(vk, k->scanCode)) return 1;
        if (up && TypeAheadSwallowed(vk)) return 1;
    }
    if (g_winDown && down) g_comboUsed = true;
    if (vk == VK_ESCAPE && down && Down(VK_CONTROL) && !Down(VK_MENU) && !Down(VK_SHIFT) && !g_winDown) { SetEvent(g_winKeyEv); return 1; }   // Ctrl+Esc, the classic Start shortcut
    return CallNextHookEx(g_kb, code, w, l);
}

// ---- mouse bridge ----
// Moves the cursor between screens of different sizes at the SAME relative place: leave the Surface's right edge 30% down and arrive 30% down
// the LG's left edge. The cursor is kept clipped to its current screen; raw mouse input tells us when you keep pushing against an edge; once the
// push passes the threshold we jump to the proportional spot on the neighbour. Edge resistance stops aiming at scrollbars from throwing you over.
enum class Edge { None, Left, Right, Top, Bottom };
std::vector<RECT> g_mons; std::mutex g_mmu; bool g_haveClip = false; RECT g_ourClip{}; double g_pressure = 0; uint64_t g_pushStart = 0, g_lastPush = 0, g_lastRaw = 0, g_cooldown = 0; Edge g_pushEdge = Edge::None;
int g_lastHX = 0, g_lastHY = 0; DWORD g_mbTid = 0; const UINT WM_APPLY = WM_APP + 1;
bool Same(const RECT& a, const RECT& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
bool BridgeActive() { return S.mouseBridge && g_mons.size() > 1; }
void ReleaseClipLocked() { if (g_haveClip) { RECT c; GetClipCursor(&c); if (Same(c, g_ourClip)) ClipCursor(nullptr); } g_haveClip = false; }
void ReleaseClip() { std::lock_guard<std::mutex> l(g_mmu); ReleaseClipLocked(); }
void RefreshLayout() {
    std::vector<RECT> v; EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL { MONITORINFO mi{sizeof mi}; if (GetMonitorInfoW(h, &mi)) ((std::vector<RECT>*)p)->push_back(mi.rcMonitor); return TRUE; }, (LPARAM)&v);
    { std::lock_guard<std::mutex> l(g_mmu); g_mons = v; ReleaseClipLocked(); }   // layout changed: drop our clip, it is re-applied on the next move
    if (g_mbTid) PostThreadMessageW(g_mbTid, WM_APPLY, 0, 0);
}
int FindMon(POINT p) {
    for (size_t i = 0; i < g_mons.size(); i++) if (PtInRect(&g_mons[i], p)) return (int)i;
    int best = -1; long long bd = LLONG_MAX;   // outside every screen (unclipped hook points): nearest
    for (size_t i = 0; i < g_mons.size(); i++) { long long cx = std::clamp(p.x, g_mons[i].left, g_mons[i].right - 1) - p.x, cy = std::clamp(p.y, g_mons[i].top, g_mons[i].bottom - 1) - p.y, d = cx * cx + cy * cy; if (d < bd) { bd = d; best = (int)i; } }
    return best;
}
int Neighbour(int ci, Edge e) {
    const RECT& c = g_mons[ci]; int best = -1; long long bg = LLONG_MAX;
    for (size_t i = 0; i < g_mons.size(); i++) {
        if ((int)i == ci) continue; const RECT& m = g_mons[i]; long long gap = LLONG_MAX;
        switch (e) { case Edge::Right: if (m.left >= c.right - 4) gap = m.left - c.right; break; case Edge::Left: if (m.right <= c.left + 4) gap = c.left - m.right; break;
                     case Edge::Bottom: if (m.top >= c.bottom - 4) gap = m.top - c.bottom; break; case Edge::Top: if (m.bottom <= c.top + 4) gap = c.top - m.bottom; break; default: break; }
        if (gap == LLONG_MAX) continue;
        bool overlaps = (e == Edge::Left || e == Edge::Right) ? m.top < c.bottom && m.bottom > c.top : m.left < c.right && m.right > c.left;   // prefer screens that sit beside this one
        long long sc = std::llabs(gap) + (overlaps ? 0 : 100000); if (sc < bg) { bg = sc; best = (int)i; }
    }
    return best;
}
bool EnsureClip(const RECT& cur) {   // keeps the cursor clipped to the current screen, unless another app (a game) has its own clip
    RECT c; GetClipCursor(&c); RECT full{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    bool unclipped = Same(c, full), ours = g_haveClip && Same(c, g_ourClip); if (!unclipped && !ours) { g_haveClip = false; return false; }
    if (!Same(c, cur)) { RECT r = cur; ClipCursor(&r); g_ourClip = r; g_haveClip = true; } return true;
}
void Jump(const RECT& from, const RECT& to, Edge e, POINT p) {
    int nx, ny;
    if (e == Edge::Left || e == Edge::Right) { double f = (p.y - from.top) / (double)std::max(1L, from.bottom - from.top - 1); ny = to.top + (int)lround(std::clamp(f, 0.0, 1.0) * (to.bottom - to.top - 1)); nx = e == Edge::Right ? to.left + 1 : to.right - 2; }
    else { double f = (p.x - from.left) / (double)std::max(1L, from.right - from.left - 1); nx = to.left + (int)lround(std::clamp(f, 0.0, 1.0) * (to.right - to.left - 1)); ny = e == Edge::Bottom ? to.top + 1 : to.bottom - 2; }
    std::lock_guard<std::mutex> l(g_mmu); RECT clip = to; ClipCursor(&clip); g_ourClip = clip; g_haveClip = true; SetCursorPos(nx, ny); g_lastHX = nx; g_lastHY = ny;
}
// The bottom row of a screen, where the bar sits: pushing sideways there means aiming at a corner button, not leaving the screen.
bool InBarRow(const RECT& mon, POINT p) {
    UINT dx = 96, dy = 96; GetDpiForMonitor(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
    return p.y >= mon.bottom - MulDiv(56, (int)dy, 96);
}
void Process(POINT p, int dx, int dy, bool fromRaw) {
    if (!BridgeActive()) { if (g_haveClip) ReleaseClip(); return; }
    int ci = FindMon(p); if (ci < 0) return; RECT cur = g_mons[ci];
    { std::lock_guard<std::mutex> l(g_mmu); if (!EnsureClip(cur)) { g_pressure = 0; return; } }
    uint64_t now = GetTickCount64(); if (now < g_cooldown) return;
    Edge edge = Edge::None; double amount = 0;
    if (fromRaw) {
        if (p.x >= cur.right - 1 && dx > 0) { edge = Edge::Right; amount = dx; } else if (p.x <= cur.left && dx < 0) { edge = Edge::Left; amount = -dx; }
        else if (p.y >= cur.bottom - 1 && dy > 0) { edge = Edge::Bottom; amount = dy; } else if (p.y <= cur.top && dy < 0) { edge = Edge::Top; amount = -dy; }
    } else {   // hook points may be clamped at the edge; count steady events at the edge as pushing
        if (p.x >= cur.right - 1 && abs(dy) <= 3) { edge = Edge::Right; amount = std::max<double>(3, p.x - (cur.right - 1)); } else if (p.x <= cur.left && abs(dy) <= 3) { edge = Edge::Left; amount = std::max<double>(3, cur.left - p.x); }
        else if (p.y >= cur.bottom - 1 && abs(dx) <= 3) { edge = Edge::Bottom; amount = std::max<double>(3, p.y - (cur.bottom - 1)); } else if (p.y <= cur.top && abs(dx) <= 3) { edge = Edge::Top; amount = std::max<double>(3, cur.top - p.y); }
    }
    if ((edge == Edge::Left || edge == Edge::Right) && InBarRow(cur, p)) edge = Edge::None;   // the bar's corners (Show desktop, Start) must stay clickable
    if (edge == Edge::None) { if (now - g_lastPush > 200) g_pressure = 0; return; }
    int ni = Neighbour(ci, edge); if (ni < 0) { g_pressure = 0; return; }
    if (edge != g_pushEdge || now - g_lastPush > 250) { g_pressure = 0; g_pushStart = now; }
    g_pushEdge = edge; g_lastPush = now; g_pressure += amount;
    double counts; uint64_t ms; switch (S.edgeResistance) { case 0: counts = 0; ms = 0; break; case 1: counts = 14; ms = 60; break; case 3: counts = 90; ms = 320; break; default: counts = 38; ms = 150; }
    bool cross = g_pressure >= counts || (now - g_pushStart >= ms && g_pressure >= counts * 0.35); if (!cross) return;
    Jump(cur, g_mons[ni], edge, p); g_pressure = 0; g_pushEdge = Edge::None; g_cooldown = now + 90;
}
LRESULT CALLBACK MsProc(int code, WPARAM w, LPARAM l) {   // fallback for devices that don't produce raw input (some touchpads)
    if (code >= 0 && w == WM_MOUSEMOVE) {
        auto* m = (MSLLHOOKSTRUCT*)l;
        if (!(m->flags & LLMHF_INJECTED) && GetTickCount64() - g_lastRaw > 120) { int dx = m->pt.x - g_lastHX, dy = m->pt.y - g_lastHY; g_lastHX = m->pt.x; g_lastHY = m->pt.y; Process(m->pt, dx, dy, false); }
    }
    return CallNextHookEx(g_ms, code, w, l);
}
void ApplyHook() {   // the mouse hook only exists while two or more screens are connected: with one there is nothing to bridge
    bool want = g_mons.size() > 1;
    if (want && !g_ms) g_ms = SetWindowsHookExW(WH_MOUSE_LL, MsProc, g_inst, 0); else if (!want && g_ms) { UnhookWindowsHookEx(g_ms); g_ms = nullptr; ReleaseClip(); }
}
LRESULT CALLBACK MbWnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INPUT) {
        RAWINPUT raw; UINT sz = sizeof raw;
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &raw, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {   // relative only; pen/touch are absolute
            if (raw.data.mouse.lLastX || raw.data.mouse.lLastY) { g_lastRaw = GetTickCount64(); POINT p; GetCursorPos(&p); Process(p, raw.data.mouse.lLastX, raw.data.mouse.lLastY, true); }
        }
    }
    return DefWindowProcW(h, m, w, l);
}
void MouseThread() {
    g_mbTid = GetCurrentThreadId(); SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); RefreshLayout();
    WNDCLASSW wc{}; wc.lpfnWndProc = MbWnd; wc.hInstance = g_inst; wc.lpszClassName = L"DB3.MouseBridge"; RegisterClassW(&wc);
    HWND h = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_inst, nullptr);
    RAWINPUTDEVICE rid{1, 2, RIDEV_INPUTSINK, h}; if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) Log(L"MouseBridge: raw input registration failed %lu", GetLastError());
    ApplyHook(); MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { if (m.message == WM_APPLY) { ApplyHook(); continue; } TranslateMessage(&m); DispatchMessageW(&m); }
}
void KeyThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    g_kb = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, g_inst, 0); if (!g_kb) { Log(L"WinKeyHook: SetWindowsHookEx failed %lu", GetLastError()); return; }
    MSG m; while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
}
}

int RunHooks(DWORD parentPid) {
    HANDLE single = CreateMutexW(nullptr, TRUE, N_HOOKS); if (GetLastError() == ERROR_ALREADY_EXISTS) return 0; (void)single;
    LogInit(false); LoadSettings(); Boot(Fmt(L"helper main (parent %lu)", parentPid).c_str());
    wchar_t e[8]; g_testInject = GetEnvironmentVariableW(L"DB3_TEST_INJECT", e, 8) && e[0] == L'1';   // test injection is ignored unless this is set
    g_winKeyEv = CreateEventW(nullptr, FALSE, FALSE, N_WINKEY);
    std::thread(KeyThread).detach(); std::thread(MouseThread).detach();
    HANDLE parent = parentPid ? OpenProcess(SYNCHRONIZE, FALSE, parentPid) : nullptr; ws sig = MonSignature(); int n = 0;
    for (;;) {
        Sleep(1000);
        if (parentPid && (!parent || WaitForSingleObject(parent, 0) == WAIT_OBJECT_0)) { Log(L"input helper: parent gone, exiting"); ReleaseClip(); return 0; }
        if (++n % 3 == 0) SettingsFileChanged();                   // picks up changes made in the settings window
        ws now = MonSignature(); if (now != sig) { sig = now; RefreshLayout(); }
    }
}

// UI side: turn the helper's Windows-key event into "toggle Start"; start the helper if nobody has (the supervisor normally does)
void HooksEnsure(DWORD parentPid) {
    HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, N_HOOKS); if (m) { CloseHandle(m); return; }
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); ws cmd = Fmt(L"\"%s\" --hooks %lu", exe, parentPid);
    STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
}
void HooksListen() {
    std::thread([] { HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, N_WINKEY); for (;;) { WaitForSingleObject(ev, INFINITE); UiPost([] { TogglePanel(Panel::Start, nullptr); }); } }).detach();
}
