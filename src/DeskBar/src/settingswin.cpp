#include "pch.h"
#include "bar.h"
#include "flyout.h"
#include "widgets.h"

// The DeskBar settings window (opened from the bar's right-click menu).
namespace {
struct Check : View {
    bool on = false; std::function<void(bool)> change; ws text;
    Check(const ws& t, bool v, std::function<void(bool)> f) : on(v), change(std::move(f)), text(t) {
        lay = Lay::H; gap = 12; hit = true; pad = Thick(6, 7); radius = 6; bgHover = SolidB(K::White, 0x12); margin = Thick(0, 0, 0, 2);
        auto* box = MkBox(this); box->fw = box->fh = 20; box->radius = 4; box->va = Al::Center; box->hit = false; boxV = box;
        auto* l = MkLabel(this, t, FText(13)); l->va = Al::Center; l->star = 1; l->wrap = true;
        onClick = [this] { on = !on; Paint(); if (change) change(on); };
        Paint();
    }
    void Paint() {
        boxV->bg = on ? SolidB(K::Accent) : Brush{}; boxV->borderW = on ? 0.f : 1.5f; boxV->border = Col(K::White, 0xB0); boxV->kids.clear();
        if (on) { auto* g = MkGlyph(boxV, 0xE73E, 12, Col(K::Black)); g->ha = g->va = Al::Center; } Invalidate();
    }
    View* boxV;
};
struct SettingsWnd : Wnd {
    SettingsWnd() { noActivate = false; }
};
std::unique_ptr<SettingsWnd> g_sw;
ws TaskState() {   // "Start DeskBar when I sign in": the scheduled task's state
    HANDLE r, w; SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE}; if (!CreatePipe(&r, &w, &sa, 0)) return {}; SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    ws cmd = L"schtasks.exe /Query /TN " N_TASK L" /FO LIST"; STARTUPINFOW si{sizeof si}; si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.hStdOutput = w; si.hStdError = w; PROCESS_INFORMATION pi{};
    std::string out; if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(w); char b[512]; DWORD n; while (ReadFile(r, b, sizeof b, &n, nullptr) && n) out.append(b, n); WaitForSingleObject(pi.hProcess, 3000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    } else CloseHandle(w);
    CloseHandle(r); return Widen(out);
}
void RunHidden(const ws& c) { ws cmd = c; STARTUPINFOW si{sizeof si}; PROCESS_INFORMATION pi{}; if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } }
}

void OpenSettingsWindow() {
    if (g_sw && g_sw->hwnd) { ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd); return; }
    g_sw = std::make_unique<SettingsWnd>(); POINT at{}; GetCursorPos(&at);   /* opens on the screen you clicked on (the cursor is on that bar), not on the leftmost one */
    HMONITOR mon = MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi); UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); float sc = dx / 96.f;
    int w = (int)(560 * sc), h = (int)(std::clamp((mi.rcWork.bottom - mi.rcWork.top) / sc - 90, 420.f, 900.f) * sc);   /* as tall as the screen allows; the rest scrolls */ int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2, y = mi.rcWork.top + std::max(0, (int)(mi.rcWork.bottom - mi.rcWork.top - h) / 2);
    g_sw->Create(0, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, w, h); if (!g_sw->hwnd) return;
    SetWindowTextW(g_sw->hwnd, L"DeskBar settings"); int dark = 1; DwmSetWindowAttribute(g_sw->hwnd, 20, &dark, 4);
    RECT cr; GetClientRect(g_sw->hwnd, &cr); RECT wr; GetWindowRect(g_sw->hwnd, &wr); SetWindowPos(g_sw->hwnd, nullptr, x, y, w + (wr.right - wr.left) - cr.right, h + (wr.bottom - wr.top) - cr.bottom, SWP_NOZORDER);
    View* r = g_sw->root; r->bg = PanelBrush(); r->lay = Lay::V; r->pad = Thick(0);
    auto* sv = r->Add(new Scroll()); auto* col = MkBox(nullptr, Lay::V); col->margin = Thick(24, 20, 24, 24); sv->content = sv->Add(col);
    auto* t = MkLabel(col, L"DeskBar", FDisp(23, 600)); t->margin = Thick(0, 0, 0, 14);
    auto add = [&](const ws& text, bool v, std::function<void(bool)> f) { col->Add(new Check(text, v, [f](bool on) { f(on); SaveSettingsSoon(); })); };
    add(L"Open DeskBar's Start with the Windows key", S.winKeyOpensStart, [](bool v) { S.winKeyOpensStart = v; });
    add(L"Show battery percentage", S.showBatteryPercent, [](bool v) { S.showBatteryPercent = v; });
    add(L"Show the media player on the taskbar", S.showMedia, [](bool v) { S.showMedia = v; });
    add(L"Move the mouse between screens at the same relative place", S.mouseBridge, [](bool v) { S.mouseBridge = v; });
    auto* er = MkBox(col, Lay::H); er->margin = Thick(6, 4, 0, 8); er->gap = 12; auto* el = MkLabel(er, L"Edge resistance", FText(13)); el->va = Al::Center;
    static const wchar_t* names[] = {L"None", L"Light", L"Medium", L"Strong"};
    auto* eb = MkBox(er, Lay::H); eb->pad = Thick(14, 6); eb->radius = 6; eb->bg = SolidB(K::White, 0x18); eb->bgHover = SolidB(K::White, 0x28); eb->hit = true; eb->gap = 8; auto* ev = MkLabel(eb, names[std::clamp(S.edgeResistance, 0, 3)], FText(13)); auto* eg = MkGlyph(eb, 0xE70D, 10); eg->va = Al::Center;
    eb->onClick = [eb, ev] { std::vector<MenuItem> m; for (int i = 0; i < 4; i++) m.push_back({names[i], 0, [i, ev] { S.edgeResistance = i; ev->Set(names[i]); SaveSettingsSoon(); }, false, false, S.edgeResistance == i}); MenuShow(std::move(m), g_sw->ScreenRect(eb), false, g_sw->scale); };
    add(L"Snap a window fully onto one screen when you let go", S.snapOnRelease, [](bool v) { S.snapOnRelease = v; });
    add(L"Keep a window's size relative to the screen when moving it across", S.keepSizeRatio, [](bool v) { S.keepSizeRatio = v; });
    add(L"Bring windows back when a screen is unplugged", S.rescueWindows, [](bool v) { S.rescueWindows = v; });
    add(L"Open a program's dialogs and popups on the same screen as the window that opened them", S.popupsFollowOwner, [](bool v) { S.popupsFollowOwner = v; });
    auto* sign = new Check(L"Start DeskBar when I sign in", true, [](bool on) { RunHidden(L"schtasks.exe /Change /TN " N_TASK + ws(on ? L" /ENABLE" : L" /DISABLE")); }); col->Add(sign);
    std::thread([sign] { ws s = TaskState(); bool on = !ContainsI(s, L"Disabled"); UiPost([sign, on] { if (g_sw && g_sw->hwnd) { sign->on = on; sign->Paint(); } }); }).detach();
    SectionLabel(col, L"Widgets on the bar")->margin = Thick(4, 16, 2, 6);
    for (int i = 0; i < kWidgetCount; i++) { ws id = kWidgets[i].id; add(kWidgets[i].name, HasWidget(id), [id](bool v) { SetWidget(id, v); }); }
    SectionLabel(col, L"Updates")->margin = Thick(4, 16, 2, 6);
    add(L"Check for updates once a day (nothing is installed until you press Update now)", S.updateCheck, [](bool v) { S.updateCheck = v; });
    auto* us = MkLabel(col, UpdateStatus().empty() ? ws(L"Not checked yet.") : UpdateStatus(), FText(12), Col(K::White, K::Dim)); us->margin = Thick(6, 0, 0, 4);
    auto* btns = MkBox(col, Lay::V); btns->margin = Thick(0, 18, 0, 0); btns->gap = 8;
    UpdateListen([us] { if (g_sw && g_sw->hwnd) us->Set(UpdateStatus().empty() ? ws(L"Not checked yet.") : UpdateStatus()); });
    Pill(btns, L"Check for updates now", 0xE72C, false, [] { UpdateCheck(); });
    if (UpdateNewer()) Pill(btns, L"Update DeskBar now", 0xE896, true, [] { UpdateNow(); });
    Pill(btns, L"Restart DeskBar", 0xE777, false, [] { AppQuit(true); });
    Pill(btns, L"Quit and bring back the Windows taskbar", 0xE7E8, false, [] { AppQuit(false); });
    ShowWindow(g_sw->hwnd, SW_SHOW); SetForegroundWindow(g_sw->hwnd);
    g_sw->onDeactivate = nullptr;
}
