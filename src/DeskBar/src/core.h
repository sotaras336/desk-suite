#pragma once
#include "pch.h"

// ---------- strings / paths ----------
ws Widen(const std::string& u8);
std::string Narrow(const ws& w);
ws Fmt(const wchar_t* f, ...);
ws Lower(const ws& s);
bool EqI(const ws& a, const ws& b);
bool StartsI(const ws& s, const ws& p);
bool ContainsI(const ws& s, const ws& p);
ws Trim(const ws& s);
std::vector<ws> Split(const ws& s, wchar_t sep);
ws ExpandEnv(const ws& s);
ws FileNameOf(const ws& p);
ws DirOf(const ws& p);
ws StemOf(const ws& p);
ws ExtOf(const ws& p);                 // lower-case, with dot
ws DataDir();                          // %LOCALAPPDATA%\DeskBar3 (env DB3_DATA overrides)
ws DataPath(const wchar_t* leaf);
ws KnownFolder(REFKNOWNFOLDERID id);
bool FileExists(const ws& p);
bool DirExists(const ws& p);
bool ReadAll(const ws& path, std::string& out);
bool WriteAtomic(const ws& path, const std::string& data);
uint64_t Now();                        // ms, monotonic
uint64_t Sha1Prefix(const ws& s);      // first 8 bytes of SHA1 of UTF-16 lower-case text
ws Hex16(uint64_t v);

// ---------- log ----------
void LogInit(bool fresh);
void Log(const wchar_t* f, ...);
void Boot(const wchar_t* step);        // "boot <step> +N ms"

// ---------- UI thread ----------
void UiInit();
void UiPost(std::function<void()> f);  // run on the UI thread (any thread may call)
bool OnUiThread();
HWND UiHwnd();
UINT_PTR UiTimer(UINT ms, std::function<void()> f);   // repeating; returns id
void UiKillTimer(UINT_PTR id);
void UiOnce(UINT ms, std::function<void()> f);        // one-shot
extern const UINT WM_UIPOST;

// ---------- JSON (tiny) ----------
struct Json {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; ws s;
    std::vector<Json> a; std::vector<std::pair<ws, Json>> o;
    static Json Parse(const std::string& u8, bool* ok = nullptr);   // ok: false when the text was cut short or not JSON (what could be read is still returned)
    std::string Dump(int indent = 0) const;
    const Json* Find(const wchar_t* k) const;
    Json& Set(const wchar_t* k, Json v);
    static Json S(const ws& v) { Json j; j.t = Str; j.s = v; return j; }
    static Json N(double v) { Json j; j.t = Num; j.n = v; return j; }
    static Json B(bool v) { Json j; j.t = Bool; j.b = v; return j; }
    static Json A() { Json j; j.t = Arr; return j; }
    static Json O() { Json j; j.t = Obj; return j; }
};

// ---------- settings ----------
struct Settings {
    std::vector<ws> taskbarPins; bool pinsImported = false;
    bool winKeyOpensStart = true, mouseBridge = true; int edgeResistance = 2;
    bool snapOnRelease = true, keepSizeRatio = true, rescueWindows = true, popupsFollowOwner = true;
    bool dmAutoMax = true, dmRestore = true, dmGameLock = true, dmButton = true;
    int btLagMs = 200;         // "Also play": how far a Bluetooth output runs behind a wired one. Windows does not report it, so it is set by ear on the Sound page (mirror.cpp)
    bool bugReports = false;   // "Report a bug" notepad (bugreport.cpp): only turned on by DeskBar-Setup-Bugs.exe   // DeskMode, now inside DeskBar
    int dmMaxLaptopHz = 0, dmMaxMonitorHz = 0, dmMaxMode = 2, dmEasyHz = 60, dmEasyMode = 0;   // 0 Hz = the highest the screen offers; modes 0 recommended, 1 better, 2 best
    bool dmMaxSaverOff = true, dmEasySaverOn = true;
    bool showMedia = true, showBatteryPercent = true;
    bool hoverMagnify = false;   // optional, off by default: app icons grow under the mouse with a liquid bounce
    std::vector<ws> mirrorDevices;
    std::vector<ws> quickFolders; bool foldersInit = false;
    std::vector<ws> startPins; bool startPinsInit = false, startPinsImported = false;   // imported: this PC's own Start pins were read once
    std::vector<ws> pinnedRecents, hiddenRecents;
    std::vector<ws> promotedTrayIcons; bool trayImported = false;
    std::map<ws, int> launchCounts;
    ws theme;                              // colour theme id (see kThemes); empty = this edition's default
    std::vector<ws> widgets;               // ids of the bar widgets that are switched on (webwidget.cpp)
    bool updateCheck = true; ws updateKnown; double updateLast = 0;   // once-a-day look for a newer release (update.cpp): on/off, the newest build's checksum, when (unix seconds)
};
extern Settings S;
void LoadSettings();                   // also imports DeskBar2's file on first run; an unreadable file is set aside and settings.bak.json used
bool SettingsRecovered();              // this run's settings came from the spare copy (or the defaults) because settings.json was unreadable
void SaveSettingsSoon();               // debounced 400 ms, atomic
void SaveSettingsNow();
bool SettingsFileChanged();            // for the input helper: re-read when the file changes

// ---------- palette ----------
// Every blue in the UI is written as the original blue and mapped through the current theme at use. hue < 0: leave the blues alone (the original look).
// soft: the calm light look of the pink edition; the other themes rotate the hue and keep each colour's saturation and lightness.
// kind: 0 = a hue theme (above), 1 = black (neutral dark), 2 = white (light: text and fills invert), 3 = glass (dark and translucent over a blurred backdrop).
struct ThemeDef { const wchar_t* id; const wchar_t* name; float hue; bool soft; int kind; float sat = 1, val = 1; };   // sat/val (0-1): how vivid a custom colour is, how bright its backgrounds are
extern const ThemeDef kThemes[]; extern const int kThemeCount;
const ThemeDef& CurrentTheme();
D2D1_COLOR_F ShiftColor(D2D1_COLOR_F c, float dh, float dl);
ws ThemeIdNow();
ws SharedThemeRead(); void SharedThemeWrite(const ws& id);   // Desk 4.0: the suite's one theme, %LOCALAPPDATA%\Desk\theme.txt (deskapps.cpp)
float ThemeGrad();                                  // >= 0: the custom colour's second hue (gradient), -1 = none
inline bool ThemeIsGlass(const ThemeDef& t) { return t.kind == 3; }
void SetTheme(const ws& id);
void SetDeskModeTheme(const ws& id); const ThemeDef& DeskModeTheme(); float DeskModeGrad();   // DeskMode's own colour (Desk settings > Colours), else the bar's                   // unknown or empty id = this edition's default (pink in the PINK build, blue otherwise)
ThemeDef ThemeFromId(const ws& id, float& grad);   // what a theme id means ("blue:40", "custom:200:40:80:90"), the fade in grad; nothing changes (core.cpp)
float LightStart(const ThemeDef& cur); ws ColourIdOf(float t, const ws& base, bool moved, float hue, float fade, float sat);   // the colour picker's light bar and the id it makes (quickpages.cpp)
D2D1_COLOR_F ColFor(const ThemeDef& t, uint32_t rgb, int a = 255);   // for swatches that show a theme other than the current one
D2D1_COLOR_F Col(uint32_t rgb, int a = 255);   // 0xRRGGBB, alpha 0..255; mapped through the current theme
bool HighContrastOn();                          // Windows' high contrast is on: Col() returns its system colours instead (ui_access.cpp)
D2D1_COLOR_F HighContrastCol(uint32_t rgb, int a);
bool DuoTone(uint32_t rgb);                     // true: the colour is a light accent, filled as a two-tone gradient (SolidB does it)
namespace K {
    constexpr uint32_t Bar = 0x003D91, Accent = 0x4CC2FF, Green = 0x6CE08A, Amber = 0xFFC83D, Charge = 0x6CCB5F, Flash = 0xE8A33A;
    constexpr uint32_t White = 0xFFFFFF, Black = 0x000000;
    constexpr int Dim = 0xB8, Faint = 0x80, Card = 0x16, CardHover = 0x26, CardLine = 0x18;
}

