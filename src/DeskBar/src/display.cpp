#include "pch.h"
#include "app.h"
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <winioctl.h>
#include <roapi.h>
#include <winstring.h>
#include <condition_variable>

// ---------- which screen is the built-in panel, and its friendly name ----------
DispInfo DisplayInfoFor(const ws& gdi) {
    DispInfo r; UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != ERROR_SUCCESS) return r;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(np); std::vector<DISPLAYCONFIG_MODE_INFO> modes(nm);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, paths.data(), &nm, modes.data(), nullptr) != ERROR_SUCCESS) return r;
    for (UINT32 i = 0; i < np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{}; sn.header = {DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof sn, paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id};
        if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS || gdi != sn.viewGdiDeviceName) continue;
        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{}; tn.header = {DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof tn, paths[i].targetInfo.adapterId, paths[i].targetInfo.id};
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) r.friendly = tn.monitorFriendlyDeviceName;
        auto t = paths[i].targetInfo.outputTechnology;
        r.internal = t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED || t == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        break;
    }
    return r;
}

// ---------- brightness ----------
// The UI calls Set as often as it likes (every slider tick); one worker sends only the newest value, spaced out so slow DDC/CI monitors
// are never flooded, and re-sends the final value once things go quiet.
namespace {
struct Chan : std::enable_shared_from_this<Chan> {   // the worker thread holds a reference: replacing a channel can no longer free it under a running thread
    std::mutex mu; std::condition_variable cv; int target = -1, written = -1, value = -1; bool avail = false, dead = false; int gapMs = 15, settleMs = 600;
    virtual ~Chan() {}
    virtual bool Write(int pct) = 0; virtual int Read() = 0; virtual void Reacquire() {}
    void Start() { std::thread([self = shared_from_this()] { self->Loop(); }).detach(); }
    void Stop() { { std::lock_guard<std::mutex> l(mu); dead = true; } cv.notify_all(); }
    void Set(int pct) { pct = std::clamp(pct, 0, 100); { std::lock_guard<std::mutex> l(mu); target = pct; value = pct; } cv.notify_one(); }
    bool WriteRetry(int v) { for (int i = 0; i < 3; i++) { if (Write(v)) return true; Sleep(40 + i * 40); if (i == 1) Reacquire(); } return false; }
    void Loop() {
        while (!dead) {
            { std::unique_lock<std::mutex> l(mu); cv.wait(l, [this] { return dead || target >= 0 && target != written; }); }
            bool wrote = false;
            while (!dead) {
                int t; { std::lock_guard<std::mutex> l(mu); t = target; }
                if (t < 0 || t == written) {
                    std::unique_lock<std::mutex> l(mu);
                    if (cv.wait_for(l, std::chrono::milliseconds(settleMs), [this] { return target != written; })) continue;
                    l.unlock(); if (wrote && t >= 0) WriteRetry(t); break;
                }
                if (WriteRetry(t)) wrote = true;
                { std::lock_guard<std::mutex> l(mu); written = t; }
                Sleep(gapMs);
            }
        }
    }
};
struct Internal : Chan {
    HANDLE h = INVALID_HANDLE_VALUE; std::vector<BYTE> levels;
    Internal() { gapMs = 15; settleMs = 600; Reacquire(); value = Read(); avail = value >= 0; }
    void Reacquire() override {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = CreateFileW(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, 3, nullptr, OPEN_EXISTING, 0, nullptr); levels.clear();
        if (h == INVALID_HANDLE_VALUE) return;
        BYTE buf[256]; DWORD ret = 0; if (DeviceIoControl(h, 0x230496, nullptr, 0, buf, 256, &ret, nullptr)) levels.assign(buf, buf + ret);
    }
    int Read() override {
        if (h == INVALID_HANDLE_VALUE) return -1; BYTE b[3]; DWORD ret;
        if (!DeviceIoControl(h, 0x230498, nullptr, 0, b, 3, &ret, nullptr)) return -1;
        SYSTEM_POWER_STATUS s{}; GetSystemPowerStatus(&s); int level = s.ACLineStatus == 1 ? b[1] : b[2];   // [policy, AC level, battery level]
        if (levels.size() < 2) return level;
        auto it = std::find(levels.begin(), levels.end(), (BYTE)level); if (it == levels.end()) return level;
        return (int)lroundf(100.f * (it - levels.begin()) / (levels.size() - 1));
    }
    bool Write(int pct) override {
        if (h == INVALID_HANDLE_VALUE) return false;
        BYTE level = levels.size() > 1 ? levels[(size_t)lroundf(pct / 100.f * (levels.size() - 1))] : (BYTE)pct; BYTE b[3] = {3, level, level}; DWORD ret;
        return DeviceIoControl(h, 0x23049C, b, 3, nullptr, 0, &ret, nullptr) != 0;
    }
};
struct Ddc : Chan {   // external monitor, VCP 0x10; tuned for slow/finicky LG firmware
    ws device; HANDLE h = nullptr; DWORD mx = 100; std::mutex hm;
    Ddc(const ws& dev) : device(dev) {
        gapMs = 70; settleMs = 450; Reacquire();
        for (int i = 0; i < 6 && value < 0; i++) { value = Read(); if (value < 0) { Sleep(250); if (i == 2) Reacquire(); } }   // LG often doesn't answer right after hot-plug
        avail = h != nullptr;
    }
    void Reacquire() override {
        std::lock_guard<std::mutex> l(hm); if (h) { DestroyPhysicalMonitor(h); h = nullptr; }
        HMONITOR mon = nullptr; for (auto& m : Monitors()) if (m.device == device) mon = m.h; if (!mon) return;
        DWORD n = 0; if (!GetNumberOfPhysicalMonitorsFromHMONITOR(mon, &n) || !n) return;
        std::vector<PHYSICAL_MONITOR> a(n); if (!GetPhysicalMonitorsFromHMONITOR(mon, n, a.data())) return;
        h = a[0].hPhysicalMonitor; for (DWORD i = 1; i < n; i++) DestroyPhysicalMonitor(a[i].hPhysicalMonitor);
    }
    bool Write(int pct) override { std::lock_guard<std::mutex> l(hm); return h && SetVCPFeature(h, 0x10, (DWORD)lroundf(pct * mx / 100.f)); }
    int Read() override { std::lock_guard<std::mutex> l(hm); if (!h) return -1; DWORD cur = 0, max = 0; if (!GetVCPFeatureAndVCPFeatureReply(h, 0x10, nullptr, &cur, &max) || !max) return -1; mx = max; return (int)lroundf(cur * 100.f / max); }
};
std::map<ws, std::shared_ptr<Chan>> g_chan; std::mutex g_chanMu;
}
void BrightOpen(const Mon& m, std::function<void(bool, int)> cb) {
    ws dev = m.device;
    std::thread([dev, cb] {
        { std::lock_guard<std::mutex> l(g_chanMu); auto it = g_chan.find(dev); if (it != g_chan.end() && it->second->avail) { int v = it->second->value; UiPost([cb, v] { cb(true, v); }); return; } }
        bool internal = DisplayInfoFor(dev).internal; std::shared_ptr<Chan> c; if (internal) c = std::make_shared<Internal>(); else c = std::make_shared<Ddc>(dev);
        c->Start(); bool ok = c->avail; int v = c->value;
        { std::lock_guard<std::mutex> l(g_chanMu); auto& slot = g_chan[dev]; if (slot) slot->Stop(); slot = std::move(c); }
        UiPost([cb, ok, v] { cb(ok, v); });
    }).detach();
}
void BrightSet(const ws& dev, int pct) { std::lock_guard<std::mutex> l(g_chanMu); auto it = g_chan.find(dev); if (it != g_chan.end()) it->second->Set(pct); }
int BrightRead(const ws& dev) { std::lock_guard<std::mutex> l(g_chanMu); auto it = g_chan.find(dev); return it == g_chan.end() ? -1 : it->second->value; }

// ---------- night light (the CloudStore blob the Settings app writes; best effort) ----------
static const wchar_t* kNL = L"Software\\Microsoft\\Windows\\CurrentVersion\\CloudStore\\Store\\DefaultAccount\\Current\\default$windows.data.bluelightreduction.bluelightreductionstate\\windows.data.bluelightreduction.bluelightreductionstate";
static bool NlRead(std::vector<BYTE>& d) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, kNL, 0, KEY_READ, &k)) return false;
    DWORD n = 0; bool ok = RegQueryValueExW(k, L"Data", nullptr, nullptr, nullptr, &n) == 0 && n > 0; if (ok) { d.resize(n); ok = RegQueryValueExW(k, L"Data", nullptr, nullptr, d.data(), &n) == 0; }
    RegCloseKey(k); return ok;
}
int NightLightIsOn() { std::vector<BYTE> d; if (!NlRead(d) || d.size() < 20) return -1; return d[18] == 0x15; }
bool NightLightSet(bool on) {
    std::vector<BYTE> d; if (!NlRead(d) || d.size() < 25) return false;
    if ((d[18] == 0x15) == on) return true;
    std::vector<BYTE> n;
    if (on) { n.assign(d.begin(), d.begin() + 23); n.push_back(0x10); n.push_back(0x00); n.insert(n.end(), d.begin() + 23, d.end()); n[18] = 0x15; }
    else { if (d[23] != 0x10 || d[24] != 0x00) return false; n.assign(d.begin(), d.begin() + 23); n.insert(n.end(), d.begin() + 25, d.end()); n[18] = 0x13; }
    for (int i = 10; i < 15; i++) if (n[i] != 0xFF) { n[i]++; break; }   // bump the change timestamp
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, kNL, 0, KEY_WRITE, &k)) return false;
    bool ok = RegSetValueExW(k, L"Data", 0, REG_BINARY, n.data(), (DWORD)n.size()) == 0; RegCloseKey(k); return ok;
}

// ---------- rotation lock ----------
int RotationLocked() {
    typedef BOOL(WINAPI * F)(AR_STATE*); static F f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetAutoRotationState");
    AR_STATE s; if (!f || !f(&s)) return -1;
    if (s & (AR_NOSENSOR | AR_NOT_SUPPORTED | AR_DOCKED | AR_LAPTOP | AR_MULTIMON | AR_REMOTESESSION)) return -1;
    return (s & AR_DISABLED) ? 1 : 0;
}
void RotationSetLocked(bool locked) {
    typedef BOOL(WINAPI * F)(BOOL); static F f = (F)GetProcAddress(GetModuleHandleW(L"user32.dll"), MAKEINTRESOURCEA(2507));
    if (f) f(!locked);
}

// ---------- Energy Saver: the settings handler the Settings app uses (undocumented, every call fails soft) ----------
namespace {
const wchar_t* kSaverDll = L"C:\\Windows\\System32\\SettingsHandlers_OneCore_BatterySaver.dll";
const wchar_t* kSaverId = L"SystemSettings_BatterySaver_LandingPage_OverrideControl";
std::mutex g_saverMu; void* g_getSetting;
void* SaverItem() {
    RoInitialize(RO_INIT_MULTITHREADED);
    if (!g_getSetting) { HMODULE m = LoadLibraryW(kSaverDll); g_getSetting = m ? (void*)GetProcAddress(m, "GetSetting") : nullptr; if (!g_getSetting) return nullptr; }
    HSTRING id; WindowsCreateString(kSaverId, (UINT32)wcslen(kSaverId), &id); void* item = nullptr;
    using F = HRESULT(STDMETHODCALLTYPE*)(HSTRING, void**, void*); HRESULT hr = ((F)g_getSetting)(id, &item, nullptr); WindowsDeleteString(id);
    return hr == 0 ? item : nullptr;
}
void** Vt(void* o) { return *(void***)o; }
}
int EnergySaverIsOn() {
    std::lock_guard<std::mutex> lk(g_saverMu); void* item = SaverItem(); if (!item) return -1; int r = -1;
    HSTRING name; WindowsCreateString(L"Value", 5, &name); void* val = nullptr;
    using Get = HRESULT(STDMETHODCALLTYPE*)(void*, HSTRING, void**);
    HRESULT hr = SehCall([&] { return ((Get)Vt(item)[13])(item, name, &val); }); WindowsDeleteString(name);
    if (hr == 0 && val) {
        static const GUID iidPV = {0x4BD682DD, 0x7554, 0x40E9, {0x9A, 0x9B, 0x82, 0x65, 0x4E, 0xDE, 0x7E, 0x62}}; void* pv = nullptr;
        if (SUCCEEDED(((IUnknown*)val)->QueryInterface(iidPV, &pv)) && pv) { BYTE on = 0; using GB = HRESULT(STDMETHODCALLTYPE*)(void*, BYTE*); if (SehCall([&] { return ((GB)Vt(pv)[18])(pv, &on); }) == 0) r = on != 0; ((IUnknown*)pv)->Release(); }
        ((IUnknown*)val)->Release();
    }
    ((IUnknown*)item)->Release(); return r;
}
bool EnergySaverSet(bool on) {
    std::lock_guard<std::mutex> lk(g_saverMu); void* item = SaverItem(); if (!item) return false; bool ok = false;
    HSTRING cls; const wchar_t* cn = L"Windows.Foundation.PropertyValue"; WindowsCreateString(cn, (UINT32)wcslen(cn), &cls);
    static const GUID iid = {0x629BDBC8, 0xD932, 0x4FF4, {0x96, 0xB9, 0x8D, 0x96, 0xC5, 0xC1, 0xE8, 0x58}}; void* fac = nullptr;
    HRESULT hr = RoGetActivationFactory(cls, iid, &fac); WindowsDeleteString(cls);
    if (hr == 0 && fac) {
        void* boxed = nullptr; using CB = HRESULT(STDMETHODCALLTYPE*)(void*, BYTE, void**);
        if (SehCall([&] { return ((CB)Vt(fac)[17])(fac, on ? 1 : 0, &boxed); }) == 0 && boxed) {
            HSTRING name; WindowsCreateString(L"Value", 5, &name); using Set = HRESULT(STDMETHODCALLTYPE*)(void*, HSTRING, void*);
            ok = SehCall([&] { return ((Set)Vt(item)[14])(item, name, boxed); }) == 0; WindowsDeleteString(name); ((IUnknown*)boxed)->Release();
        }
        ((IUnknown*)fac)->Release();
    }
    ((IUnknown*)item)->Release(); return ok;
}

// ---------- touch keyboard ----------
void ToggleTouchKeyboard() {
    static const GUID clsid = {0x4ce576fa, 0x83dc, 0x4F88, {0x95, 0x1c, 0x9d, 0x07, 0x82, 0xb4, 0xe3, 0x76}}, iid = {0x37c994e7, 0x432b, 0x4834, {0xa2, 0xf7, 0xdc, 0xe1, 0xf1, 0x3b, 0x83, 0x4b}};
    void* tip = nullptr;
    if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, iid, &tip)) && tip) { using T = HRESULT(STDMETHODCALLTYPE*)(void*, HWND); ((T)Vt(tip)[3])(tip, GetDesktopWindow()); ((IUnknown*)tip)->Release(); return; }
    Launch(L"C:\\Program Files\\Common Files\\microsoft shared\\ink\\TabTip.exe");
}
