#pragma once
#include "flyout.h"
#include "widgets.h"

struct QuickPanel : Flyout {
    View *pages = nullptr, *main = nullptr; Scroll* detail = nullptr; View* page = nullptr;
    QuickPanel();
    void ShowDetail(View* p);
    void Back();
    bool OnEsc() override { if (page) { Back(); return true; } return false; }
    void OnClose() override { Back(); }
    void OnSysChange() override;
    void OnOpen() override;
    // main page
    void Refresh();
    void BuildBrightness();
    struct Impl; Impl* d = nullptr;
};
View* PageHeader(View* parent, const ws& title, std::function<void()> back, Toggle** toggle = nullptr);
View* MakeWifiPage(QuickPanel* q);
extern ws g_wifiPreview;                              // dev: --wifipreview connecting|done|failed opens the Wi-Fi page already showing that card
View* MakeBtPage(QuickPanel* q);
View* MakeSoundPage(QuickPanel* q);
View* MakeThemePage(QuickPanel* q);
void MirrorSet(const ws& deviceId, bool on);          // "Also play" (mirror.cpp)
bool MirrorIsOn(const ws& deviceId);
