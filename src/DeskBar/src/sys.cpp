#include "pch.h"
#include "app.h"

// Battery + shared "something changed" plumbing. Audio/network/media live in their own files.
static std::mutex g_mu; static BattState g_batt; static std::function<void()> g_onChange; static std::atomic<bool> g_pending{false};
static HANDLE g_pokeEv;

static void Notify() { if (!g_pending.exchange(true)) UiPost([] { g_pending = false; if (g_onChange) g_onChange(); }); }
void SysNotify() { Notify(); }

BattState Battery() { std::lock_guard<std::mutex> l(g_mu); return g_batt; }
void SysOnChange(std::function<void()> f) { g_onChange = std::move(f); }
void SysPoke() { if (g_pokeEv) SetEvent(g_pokeEv); }

static void BatteryLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        SYSTEM_POWER_STATUS s{}; BattState b;
        if (GetSystemPowerStatus(&s)) {
            b.present = s.BatteryFlag != 128 && s.BatteryFlag != 255 && s.BatteryLifePercent <= 100; b.ac = s.ACLineStatus == 1;
            b.pct = s.BatteryLifePercent <= 100 ? s.BatteryLifePercent : 100; b.charging = b.ac && b.present && b.pct < 100;
            b.secsLeft = s.BatteryLifeTime == (DWORD)-1 ? -1 : (int)s.BatteryLifeTime;
        }
        // Energy Saver can be flipped by DeskMode, Settings or a key: watch it so the icon follows within ~2 s
        int sv = EnergySaverIsOn(); b.saver = sv >= 0 ? sv == 1 : g_batt.saver;
        bool ch;
        { std::lock_guard<std::mutex> l(g_mu); ch = memcmp(&b, &g_batt, sizeof b) != 0; g_batt = b; }
        if (ch) Notify();
        WaitForSingleObject(g_pokeEv, 2000);
    }
}
void SysStart() {
    g_pokeEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(BatteryLoop).detach(); AudioStart(); NetStart(); MediaStart();
}

ws KeyboardLabel(HWND fg) {
    if (GetKeyboardLayoutList(0, nullptr) < 2) return {};
    DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    LANGID lid = LOWORD((UINT_PTR)GetKeyboardLayout(tid));
    switch (PRIMARYLANGID(lid)) {
    case LANG_GREEK: return L"ΕΛ";
    case LANG_ENGLISH: return L"ENG";
    }
    wchar_t b[16] = {}; GetLocaleInfoW(MAKELCID(lid, SORT_DEFAULT), LOCALE_SABBREVLANGNAME, b, 16); return b;
}
