#pragma once
#include "app.h"

struct Entry {                           // one task button's worth of data
    ws key, name, iconKey; bool pinned = false; PinInfo pin; std::vector<WinInfo> wins;
};

struct TaskBtn : View {
    Entry e; bool active = false, flashing = false, dragging = false, wasP = false; float indW = 0, iconScale = 1, dragFrom = 0; int pinIndex = -1;
    UINT_PTR hoverTimer = 0; uint64_t launchedAt = 0;
    TaskBtn();
    ~TaskBtn() override;
    void Update(const Entry& n, HWND fg);
    void Click(bool shift);
    void Menu();
    void Launch_();
    void CycleWindow(float dir);
    void PaintContent(Gfx& g) override;
    bool OnDown(float x, float y, bool touch) override;
    bool OnPressMove(float x, float y) override;
    void OnDrag(float x, float y) override;
    void OnUp(float x, float y) override;
    bool OnWheel(float d, float x, float y) override;
    PreviewReq Req(bool touch);
};

struct Bar : Wnd {
    HMONITOR hmon = nullptr; int idx = 0; RECT mon{}; bool hiddenFs = false; uint64_t created = 0;
    View *left = nullptr, *right = nullptr, *startBtn = nullptr, *trayBox = nullptr, *overflowBtn = nullptr, *mediaBox = nullptr, *kbBtn = nullptr;
    Label *clockT = nullptr, *clockD = nullptr, *lang = nullptr, *battPct = nullptr; Glyph *gNet = nullptr, *gVol = nullptr, *gBattFill = nullptr, *gBattOutline = nullptr;
    View* statusBtn = nullptr, *clockBtn = nullptr, *langBtn = nullptr;
    Label *mTitle = nullptr, *mArtist = nullptr; Pic* mArt = nullptr; Glyph* mPlay = nullptr, *mNext = nullptr, *mPrev = nullptr;
    std::vector<TaskBtn*> btns; float swipeX = 0, swipeY = 0; bool swipeTouch = false;
    ws lastClock, lastMedia;
    Bar(HMONITOR m, int index);
    bool Init();
    RECT Target() const;
    void EnsurePlaced();
    void Sync(const std::vector<Entry>& es, HWND fg);
    void Tick();
    void UpdateStatus();
    void UpdateMedia();
    LRESULT Proc(UINT m, WPARAM w, LPARAM l) override;
    int HeightPx() const { return (int)lroundf(48 * scale); }
};

void BarsStart();                         // build bars for the current monitors
void BarsStop();
void BarsTick();
void BarsSync();
void BarsRebuildIfMonitorsChanged();
Bar* BarOnMonitor(HMONITOR m);
std::vector<Bar*> AllBars();
TaskBtn* FindBtn(const ws& key);
extern ws g_devScreen;                    // "surface" | "lg" | "all"

void TrayStart(); void TrayStop(); void TrayKeepOnTop(); void TrayPlace(const RECT& r); void TrayRefresh(); void TrayToggleOverflow(Bar* bar);
