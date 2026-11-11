#pragma once
#include "app.h"

// Panel framework shared by Start, Quick Settings and the clock/calendar: a borderless always-on-top window with the blue gradient,
// anchored above the bar, with a short slide+fade, dismissed by clicking away / Esc / losing activation.
struct Flyout : Wnd {
    Panel id; float widthDip, fixedH = 0; bool anchorRight, shown = false, hiding = false; uint64_t closedAt = 0;
    UINT_PTR poll = 0; bool downPrev = false; HMONITOR mon = nullptr; float baseY = 0; int curX = 0, curY = 0, curW = 0, curH = 0;
    Flyout(Panel p, float w, bool right) : id(p), widthDip(w), anchorRight(right) { root->lay = Lay::V; }
    void Build();                                   // creates the window (hidden)
    void Open(HMONITOR m);
    void Close(bool instant = false);
    virtual void OnOpen() {}
    virtual void OnClose() {}
    virtual void OnSysChange() {}                   // battery / audio / network state changed while open
    virtual bool OnEsc() { return false; }          // true = consumed (e.g. cleared the search box)
    virtual bool OnKey(UINT vk, bool ctrl, bool shift) { return false; }   // keys the focused control declined
    virtual void OnShown() {}                       // window is up and being brought to the foreground
    bool keepOpen = false;                          // a modal dialog of ours is up: don't dismiss
    virtual float ContentHeight(float maxH);        // dip; default = measured
    void Place(bool animate);
    void Refit();   // the height follows the content again (a page that is taller or shorter), kept on the same spot at the bottom
};
Flyout* Panel_(Panel p);
void PanelsSysChanged();
Flyout* MakeClockPanel();
Flyout* MakeQuickPanel();
Flyout* MakeStartPanel();
Brush PanelBrush();
bool MenuContains(POINT p);
