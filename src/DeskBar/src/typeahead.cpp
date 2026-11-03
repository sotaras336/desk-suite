#include "pch.h"
#include "app.h"

// Keys typed right after the Windows key. Start takes a moment to come up and take focus; until it has, whatever you type would land in
// the app you were just using. So for that moment the keyboard hook (in the separate input-helper process) holds those keys back,
// and Start collects them from shared memory the instant it is ready.
// Shared block (both processes): [0] int64 held-until tick, [8] int flags (1 = Enter, 2 = Esc), [12] int length, [16..] UTF-16 text.
namespace {
const int kSize = 4096, kMax = 1000; BYTE* g_view = nullptr; HANDLE g_lock = nullptr; bool g_failed = false; std::set<UINT> g_down;   // g_down: helper only
bool Open() {
    if (g_view) return true; if (g_failed) return false;
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kSize, N_TYPEAHEAD);
    if (m) g_view = (BYTE*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, kSize);
    g_lock = CreateMutexW(nullptr, FALSE, N_TYPEAHEAD_LOCK);
    if (!g_view || !g_lock) { g_failed = true; return false; } return true;
}
bool Enter() { DWORD r = WaitForSingleObject(g_lock, 30); return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED; }
void Leave() { ReleaseMutex(g_lock); }
int64_t& T() { return *(int64_t*)g_view; } int& Flags() { return *(int*)(g_view + 8); } int& Len() { return *(int*)(g_view + 12); } wchar_t* Text() { return (wchar_t*)(g_view + 16); }
wchar_t ToChar(UINT vk, UINT scan) {
    BYTE st[256] = {}; if (GetKeyState(VK_SHIFT) & 0x8000) st[VK_SHIFT] = 0x80; if (GetKeyState(VK_CAPITAL) & 1) st[VK_CAPITAL] = 1;
    HWND fg = GetForegroundWindow(); DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0; wchar_t b[4] = {};
    int n = ToUnicodeEx(vk, scan, st, b, 4, 4 /* do not disturb dead-key state */, GetKeyboardLayout(tid)); return n == 1 ? b[0] : 0;
}
}
void TypeAheadBegin() { if (!Open() || !Enter()) return; T() = (int64_t)GetTickCount64() + 2500; Flags() = 0; Len() = 0; g_down.clear(); Leave(); }
void TypeAheadEnd() { if (!Open() || !Enter()) return; T() = 0; Leave(); }
bool TypeAheadActive() { return Open() && (int64_t)GetTickCount64() < T(); }
ws TypeAheadTake(bool& enter, bool& esc) {
    enter = esc = false; if (!Open() || !Enter()) return {};
    int fl = Flags(), len = std::clamp(Len(), 0, kMax); ws r(Text(), len); Flags() = 0; Len() = 0; Leave(); enter = fl & 1; esc = fl & 2; return r;
}
bool TypeAheadPress(UINT vk, UINT scan) {   // helper side: a key went down while Start is opening: true = kept (the app you were in never sees it)
    if (!Open()) return false; wchar_t c = 0; bool special = vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_BACK;
    if (!special) { c = ToChar(vk, scan); if (c == 0 || iswcntrl(c)) return false; }
    if (!Enter()) return false;
    int fl = Flags(), len = std::clamp(Len(), 0, kMax);
    if (vk == VK_RETURN) fl |= 1; else if (vk == VK_ESCAPE) fl |= 2; else if (vk == VK_BACK) { if (len > 0) len--; } else if (len < kMax) Text()[len++] = c;
    Flags() = fl; Len() = len; g_down.insert(vk); Leave(); return true;
}
bool TypeAheadSwallowed(UINT vk) { return g_down.erase(vk) > 0; }
