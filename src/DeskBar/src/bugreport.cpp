#include "pch.h"
#include "app.h"
#include <winhttp.h>

// "Report a bug": a small notepad that sends what you type (plus DeskBar's build, Windows version, screens and recent log) to the owner's Google Form,
// where Claude reads it later. Off unless S.bugReports is on: only the copy installed from DeskBar-Setup-Bugs.exe turns it on (install.cpp).
// The form takes anonymous answers, so nothing secret lives in this exe.
namespace {
// shortcut: the form is fixed at build time; to move to another form, change these two and publish.
const wchar_t* kFormPost = L"https://docs.google.com/forms/d/e/1FAIpQLSerXcJ-ztyA7ffOJmqfYZWivIRIr5ByGf7IG-ByocP2SqNf3A/formResponse";   // https://docs.google.com/forms/d/e/<form id>/formResponse
const char* kFormEntry = "entry.563252164";      // entry.<number> of the form's paragraph question

HWND g_wnd = nullptr, g_edit = nullptr, g_send = nullptr, g_status = nullptr; HFONT g_font = nullptr;
const UINT WM_SENT = WM_APP + 0x61;

std::string UrlEncode(const std::string& s) {
    std::string o; char b[4];
    for (unsigned char c : s) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c; else { sprintf_s(b, "%%%02X", c); o += b; } }
    return o;
}
ws WindowsVersion() {
    HKEY k; wchar_t name[128] = L"", build[32] = L""; DWORD ubr = 0, n;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) == 0) {
        n = sizeof name; RegQueryValueExW(k, L"ProductName", nullptr, nullptr, (LPBYTE)name, &n);
        n = sizeof build; RegQueryValueExW(k, L"CurrentBuild", nullptr, nullptr, (LPBYTE)build, &n);
        n = sizeof ubr; RegQueryValueExW(k, L"UBR", nullptr, nullptr, (LPBYTE)&ubr, &n); RegCloseKey(k);
    }
    return Fmt(L"%s, build %s.%lu", name, build, ubr);
}
std::string LogTail(size_t maxBytes) {   // the last part of DeskBar's log, whole lines only
    std::string d; if (!ReadAll(DataPath(L"deskbar3.log"), d)) return "(no log)";
    if (d.size() > maxBytes) { d = d.substr(d.size() - maxBytes); auto nl = d.find('\n'); if (nl != std::string::npos) d = d.substr(nl + 1); }
    return d;
}
bool Post(const std::string& body) {
    wchar_t host[256], path[2048]; URL_COMPONENTS uc{sizeof uc}; uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(kFormPost, 0, 0, &uc)) return false;
    HINTERNET s = WinHttpOpen(L"DeskBar-bugreport", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0); if (!s) return false;
    WinHttpSetTimeouts(s, 10000, 10000, 20000, 30000);
    bool ok = false; HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0), r = nullptr;
    if (c) r = WinHttpOpenRequest(c, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    const wchar_t* hdr = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (r && WinHttpSendRequest(r, hdr, (DWORD)-1L, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) && WinHttpReceiveResponse(r, nullptr)) {
        DWORD code = 0, sz = sizeof code; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
        ok = code == 200;   // Google answers 200 with its "your response has been recorded" page
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s); return ok;
}
void Layout(HWND h) {
    RECT rc; GetClientRect(h, &rc); UINT dpi = GetDpiForWindow(h); auto S_ = [dpi](int v) { return MulDiv(v, (int)dpi, 96); };
    int pad = S_(12), bw = S_(96), bh = S_(32);
    MoveWindow(g_edit, pad, pad, rc.right - 2 * pad, rc.bottom - 3 * pad - bh, TRUE);
    MoveWindow(g_status, pad, rc.bottom - pad - bh, rc.right - 3 * pad - bw, bh, TRUE);
    MoveWindow(g_send, rc.right - pad - bw, rc.bottom - pad - bh, bw, bh, TRUE);
}
void Send() {
    int n = GetWindowTextLengthW(g_edit); if (n == 0) { SetWindowTextW(g_status, L"Write what went wrong first."); return; }
    ws text(n + 1, L'\0'); GetWindowTextW(g_edit, text.data(), n + 1); text.resize(n);
    EnableWindow(g_send, FALSE); SetWindowTextW(g_status, L"Sending…");
    ws info = Fmt(L"\r\n\r\n---\r\nDeskBar build: %s\r\nWindows: %s\r\nScreens: %s\r\nTheme: %s\r\n--- recent log ---\r\n", [] { wchar_t m[MAX_PATH]; GetModuleFileNameW(nullptr, m, MAX_PATH); std::string d; return ReadAll(m, d) ? Sha256HexOf(d).substr(0, 12) : ws(L"?"); }().c_str(), WindowsVersion().c_str(), MonSignature().c_str(), CurrentTheme().id);
    std::string body = std::string(kFormEntry) + "=" + UrlEncode(Narrow(text + info) + LogTail(12000));
    HWND w = g_wnd;
    std::thread([body, w] { bool ok = Post(body); Log(L"bug report: %s", ok ? L"sent" : L"FAILED"); PostMessageW(w, WM_SENT, ok, 0); }).detach();
}
LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        UINT dpi = GetDpiForWindow(h);
        g_font = CreateFontW(-MulDiv(10, (int)dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 10, 10, h, (HMENU)1, g_inst, nullptr);
        g_status = CreateWindowExW(0, L"STATIC", L"What went wrong, and what were you doing? DeskBar adds its recent log, so Claude can see what happened.", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, h, (HMENU)2, g_inst, nullptr);
        g_send = CreateWindowExW(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 10, 10, h, (HMENU)3, g_inst, nullptr);
        for (HWND c : {g_edit, g_status, g_send}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
        SendMessageW(g_edit, EM_SETLIMITTEXT, 20000, 0);
        Layout(h); SetFocus(g_edit); return 0; }
    case WM_SIZE: Layout(h); return 0;
    case WM_COMMAND: if (LOWORD(w) == 3) Send(); return 0;
    case WM_SENT:
        EnableWindow(g_send, TRUE);
        if (w) { SetWindowTextW(g_edit, L""); SetWindowTextW(g_status, L"Sent. Thank you! Write another, or close this window."); }
        else SetWindowTextW(g_status, L"Couldn't send (no internet?). Your text is still here: try again in a moment.");
        return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)w, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: g_wnd = nullptr; if (g_font) { DeleteObject(g_font); g_font = nullptr; } return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}

int BugSendTest(const ws& text) { return Post(std::string(kFormEntry) + "=" + UrlEncode(Narrow(text))) ? 0 : 1; }

bool BugReportsOn() { return S.bugReports && *kFormPost && *kFormEntry; }

void OpenBugReport() {
    if (g_wnd) { ShowWindow(g_wnd, SW_RESTORE); SetForegroundWindow(g_wnd); return; }
    static bool reg = false;
    if (!reg) { WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = g_inst; wc.lpszClassName = L"DeskBarBugReport"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1)); RegisterClassW(&wc); reg = true; }
    POINT p; GetCursorPos(&p); HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST); MONITORINFO mi{sizeof mi}; GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96; GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy); int w = MulDiv(560, dx, 96), hgt = MulDiv(420, dx, 96);
    g_wnd = CreateWindowExW(0, L"DeskBarBugReport", L"Report a bug to Claude", WS_OVERLAPPEDWINDOW, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2, mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - hgt) / 2, w, hgt, nullptr, nullptr, g_inst, nullptr);
    if (!g_wnd) return;
    ShowWindow(g_wnd, SW_SHOW); SetForegroundWindow(g_wnd);
}
