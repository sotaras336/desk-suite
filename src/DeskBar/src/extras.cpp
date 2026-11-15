#include "pch.h"
#include "app.h"
#include "bar.h"

// Small 3.0 additions that belong to no other file: the colour that suits the wallpaper, taking all Desk settings to another PC, giving idle memory back.

// The colour theme nearest to the wallpaper's strongest colour ("" when the picture is grey or cannot be read). It picks one of the existing themes,
// so DeskDrawer and DeskTune, which follow DeskBar's theme by its name, follow this too.
static HBITMAP DecodeImage(const ws& path) {   // any picture WIC reads (jpg, png, bmp, heic...) as a 32-bit bitmap
    ComPtr<IWICImagingFactory> f; if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return nullptr;
    ComPtr<IWICBitmapDecoder> d; if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> fr; if (FAILED(d->GetFrame(0, &fr))) return nullptr;
    ComPtr<IWICFormatConverter> c; f->CreateFormatConverter(&c); if (FAILED(c->Initialize(fr.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;
    UINT w, h; c->GetSize(&w, &h); BITMAPINFO bi{}; bi.bmiHeader = {sizeof bi.bmiHeader, (LONG)w, -(LONG)h, 1, 32}; void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); if (!hb) return nullptr; c->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits); return hb;
}
ws WallpaperThemeId() {
    wchar_t path[MAX_PATH] = L""; if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) || !*path) return {};
    HBITMAP bmp = DecodeImage(path); if (!bmp) return {};
    BITMAP b{}; GetObjectW(bmp, sizeof b, &b); const BYTE* px = (const BYTE*)b.bmBits; double bins[36] = {}; double total = 0;
    int step = std::max(1, (int)std::max(b.bmWidth, b.bmHeight) / 160);   // about 160 samples across: plenty for a dominant colour
    if (px) for (int y = 0; y < b.bmHeight; y += step) for (int x = 0; x < b.bmWidth; x += step) {
        const BYTE* p = px + (size_t)y * b.bmWidthBytes + (size_t)x * 4; float bl = p[0] / 255.f, g = p[1] / 255.f, r = p[2] / 255.f;
        float mx = std::max({r, g, bl}), mn = std::min({r, g, bl}), l = (mx + mn) / 2, d = mx - mn; if (d < 0.08f || l < 0.12f || l > 0.9f) continue;   // greys, near black, near white: no colour to speak of
        float s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn), h = mx == r ? (g - bl) / d + (g < bl ? 6 : 0) : mx == g ? (bl - r) / d + 2 : (r - g) / d + 4;
        bins[(int)(h * 6) % 36] += s; total += s;
    }
    int w = b.bmWidth, hgt = b.bmHeight; DeleteObject(bmp);
    if (total < (double)(w / step) * (hgt / step) * 0.03) return {};   // hardly any colour in it
    int best = 0; double bv = -1; for (int i = 0; i < 36; i++) { double v = bins[i] + (bins[(i + 1) % 36] + bins[(i + 35) % 36]) * 0.5; if (v > bv) { bv = v; best = i; } }
    float hue = best * 10 + 5.f; ws id; float bd = 1e9f;
    for (int i = 0; i < kThemeCount; i++) {
        const ThemeDef& t = kThemes[i]; if (t.kind != 0) continue; float th = t.hue >= 0 ? (float)t.hue : 205.f;   // Blue is the unshifted original, around 205 degrees
        float dist = fabsf(hue - th); if (dist > 180) dist = 360 - dist; if (dist < bd) { bd = dist; id = t.id; }
    }
    Log(L"wallpaper colour: hue %d -> %s", (int)hue, id.c_str()); return id;
}

// All Desk settings in one folder (Documents\Desk settings), to carry to another PC or keep before a reinstall. Returns what to tell the user.
static ws SettingsBag() { return KnownFolder(FOLDERID_Documents) + L"\Desk settings"; }
static std::vector<std::pair<ws, ws>> SettingsFiles() {   // (file in the bag, where it lives)
    ws la = KnownFolder(FOLDERID_LocalAppData);
    return {{L"DeskBar.json", DataPath(L"settings.json")}, {L"DeskBar-search-habits.txt", DataPath(L"searchhabits.txt")}, {L"DeskDrawer.json", la + L"\DeskDrawer\settings.json"}, {L"DeskTune.json", la + L"\DeskTune\settings.json"}};
}
ws DeskSettingsExport() {
    SaveSettingsNow(); ws bag = SettingsBag(); SHCreateDirectoryExW(nullptr, bag.c_str(), nullptr); int n = 0;
    for (auto& [name, src] : SettingsFiles()) if (CopyFileW(src.c_str(), (bag + L"\\" + name).c_str(), FALSE)) n++;
    return n ? Fmt(L"Saved %d file(s) to Documents\Desk settings.", n) : ws(L"Couldn't write to Documents\Desk settings.");
}
ws DeskSettingsImport() {   // DeskBar restarts to load its own; DeskDrawer and DeskTune read theirs the next time they start
    ws bag = SettingsBag(); int n = 0;
    for (auto& [name, dst] : SettingsFiles()) { ws src = bag + L"\\" + name; if (!FileExists(src)) continue; SHCreateDirectoryExW(nullptr, DirOf(dst).c_str(), nullptr); if (CopyFileW(src.c_str(), dst.c_str(), FALSE)) n++; }
    if (!n) return L"Nothing to bring back: Documents\Desk settings is empty.";
    UiOnce(900, [] { AppQuit(true); }); return Fmt(L"Brought back %d file(s). DeskBar restarts now.", n);
}

// Windows keeps every page DeskBar ever touched in its working set (start-up, a panel opened once, decoded icons). Handing the idle ones back costs
// nothing while they stay unused and a soft page-in (no disk) when they are needed again. Done once, shortly after start (app.cpp). Not while a panel is open or the pointer is on the bar.
void TrimMemoryIfIdle() {
    if (OpenPanel() != Panel::None) return; POINT p; GetCursorPos(&p); for (auto* b : AllBars()) { RECT r; GetWindowRect(b->hwnd, &r); if (PtInRect(&r, p)) return; }
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}
