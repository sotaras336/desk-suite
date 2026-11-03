#include "pch.h"
#include "flyout.h"
#include "widgets.h"

static int IsoWeek(int y, int m, int d) {
    SYSTEMTIME st{}; st.wYear = (WORD)y; st.wMonth = (WORD)m; st.wDay = (WORD)d;
    FILETIME ft; SystemTimeToFileTime(&st, &ft); FileTimeToSystemTime(&ft, &st);
    int wd = (st.wDayOfWeek + 6) % 7 + 1;                      // Mon=1..Sun=7
    // day of year
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; int doy = cum[m - 1] + d + (leap && m > 2 ? 1 : 0);
    int w = (doy - wd + 10) / 7;
    auto weeks = [](int yy) { auto p = [](int a) { return (a + a / 4 - a / 100 + a / 400) % 7; }; return 52 + ((p(yy) == 4 || p(yy - 1) == 3) ? 1 : 0); };
    if (w < 1) return weeks(y - 1); if (w > weeks(y)) return 1; return w;
}
static ws Fmt1(const wchar_t* f, const SYSTEMTIME& st) { wchar_t b[96]; GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, f, b, 96, nullptr); return b; }

struct ClockPanel : Flyout {
    Label *time, *secs, *ampm, *date, *month; View* grid; int vy, vm; UINT_PTR tick = 0; int lastDay = 0; bool h12 = false;
    ClockPanel() : Flyout(Panel::Clock, 360, true) {
        DWORD it = 0; GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_ITIME | LOCALE_RETURN_NUMBER, (LPWSTR)&it, 2); h12 = it == 0;
        auto* top = MkBox(root, Lay::H); top->margin = Thick(22, 16, 12, 14);
        auto* left = MkBox(top, Lay::V); left->star = 1;
        auto* row = MkBox(left, Lay::H); row->ha = Al::Start;
        time = MkLabel(row, L"", FDisp(40, 600)); secs = MkLabel(row, L"", FDisp(22), Col(K::Accent)); secs->margin = Thick(4, 0, 0, 7); secs->va = Al::End;
        ampm = MkLabel(row, L"", FText(15), Col(K::White, K::Dim)); ampm->margin = Thick(8, 0, 0, 9); ampm->va = Al::End;
        date = MkLabel(left, L"", FText(13), Col(K::White, K::Dim));
        auto* bell = IconBtn(top, 0xEA8F, 36, 15, [] { Launch(L"ms-actioncenter:"); }); bell->va = Al::Start; bell->ha = Al::End;
        Divider(root);
        auto* nav = MkBox(root, Lay::H); nav->margin = Thick(20, 10, 12, 0); nav->gap = 2;
        month = MkLabel(nav, L"", FText(13, 600)); month->star = 1; month->va = Al::Center; month->hit = true;
        month->onClick = [this] { SYSTEMTIME n; GetLocalTime(&n); vy = n.wYear; vm = n.wMonth; Rebuild(); };
        auto step = [this](int d) { vm += d; if (vm < 1) { vm = 12; vy--; } if (vm > 12) { vm = 1; vy++; } Rebuild(); };
        IconBtn(nav, 0xE70E, 32, 12, [step] { step(-1); })->bg = Brush{}; IconBtn(nav, 0xE70D, 32, 12, [step] { step(1); })->bg = Brush{};
        grid = MkBox(root, Lay::Uniform); grid->cols = 7; grid->margin = Thick(14, 6, 14, 14); grid->cellH = 40;
        SYSTEMTIME n; GetLocalTime(&n); vy = n.wYear; vm = n.wMonth; Rebuild(); Update();
    }
    void Update() {
        SYSTEMTIME st; GetLocalTime(&st);
        int h = st.wHour; ws ap; if (h12) { ap = h >= 12 ? L"PM" : L"AM"; h = h % 12; if (!h) h = 12; }
        time->Set(h12 ? Fmt(L"%d:%02d", h, st.wMinute) : Fmt(L"%02d:%02d", h, st.wMinute)); secs->Set(Fmt(L":%02d", st.wSecond)); ampm->Set(ap); ampm->visible = h12;
        date->Set(Fmt1(L"dddd, d MMMM yyyy", st) + L"  ·  Week " + std::to_wstring(IsoWeek(st.wYear, st.wMonth, st.wDay)));
        if (st.wDay != lastDay) { lastDay = st.wDay; Rebuild(); }
    }
    void Rebuild() {
        grid->Clear(); SYSTEMTIME first{}; first.wYear = (WORD)vy; first.wMonth = (WORD)vm; first.wDay = 1;
        month->Set(Fmt1(L"MMMM yyyy", first));
        DWORD fdw = 0; GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IFIRSTDAYOFWEEK | LOCALE_RETURN_NUMBER, (LPWSTR)&fdw, 2);   // 0 = Monday
        for (int c = 0; c < 7; c++) {
            wchar_t b[16] = {}; GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTESTDAYNAME1 + (fdw + c) % 7, b, 16);
            auto* l = MkLabel(grid, b, FText(12), Col(K::White, K::Dim)); l->ta = TA::Center; l->margin = Thick(4, 0, 4, 0); l->tva = Al::Center;
        }
        FILETIME ft; SystemTimeToFileTime(&first, &ft); SYSTEMTIME fs; FileTimeToSystemTime(&ft, &fs);
        int off = ((fs.wDayOfWeek + 6) % 7 - (int)fdw + 7) % 7;
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; u.QuadPart -= (ULONGLONG)off * 864000000000ULL;
        SYSTEMTIME now; GetLocalTime(&now);
        for (int i = 0; i < 42; i++) {
            FILETIME f; f.dwLowDateTime = u.LowPart; f.dwHighDateTime = u.HighPart; SYSTEMTIME d; FileTimeToSystemTime(&f, &d); u.QuadPart += 864000000000ULL;
            bool today = d.wYear == now.wYear && d.wMonth == now.wMonth && d.wDay == now.wDay, inMonth = d.wMonth == vm;
            auto* cell = MkBox(grid); auto* c = MkBox(cell); c->fw = c->fh = 38; c->radius = 19; c->margin = Thick(1); c->va = c->ha = Al::Center; c->hit = true;
            if (today) c->bg = SolidB(K::Accent); c->bgHover = today ? SolidB(K::Accent) : SolidB(K::White, 0x26);
            auto* l = MkLabel(c, std::to_wstring(d.wDay), FText(12.5f, today ? 600 : 400), today ? Col(K::Black) : Col(K::White, inMonth ? 255 : K::Faint)); l->ta = TA::Center; l->tva = Al::Center;
        }
        Relayout();
    }
    void OnOpen() override { Update(); tick = UiTimer(200, [this] { Update(); }); }
    void OnClose() override { if (tick) { UiKillTimer(tick); tick = 0; } }
};
Flyout* MakeClockPanel() { return new ClockPanel(); }
