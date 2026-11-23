#pragma once
#include "ui.h"
// DeskMode's look (dark 0x202226, soft glows, gradient accent), shared by the DeskMode page and the settings window. Defined in quickpages.cpp.
namespace dm {
// While one is alive, DeskMode's colours come from DeskMode's own theme (Desk settings > Colours) instead of the bar's: the DeskMode pages build inside one.
struct Scope { Scope(); ~Scope(); };
D2D1_COLOR_F Raw(uint32_t rgb, int a = 255);
Brush Glow();
Brush Fill(int a);
View* Card(View* parent);
View* Title(View* parent, const ws& t);
void Strip(View* parent, const std::vector<ws>& items, int chosen, int cols, std::function<void(int)> pick);
}
