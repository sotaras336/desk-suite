#include "pch.h"
#include "app.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <roapi.h>
#include <winstring.h>
#include <condition_variable>

// Undocumented IPolicyConfig (same one Windows' own sound settings use) to change the default output.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8") IPolicyConfigX : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static const CLSID CLSID_PolicyConfigX = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

namespace {
std::mutex g_mu; VolState g_vol; std::deque<std::function<void()>> g_q; std::condition_variable g_cv; std::mutex g_qmu;
ComPtr<IMMDeviceEnumerator> g_en; ComPtr<IAudioEndpointVolume> g_ev; ws g_defId; std::function<void()> g_onDefault;

ws IdOf(IMMDevice* d) { LPWSTR p = nullptr; ws r; if (SUCCEEDED(d->GetId(&p)) && p) r = p; CoTaskMemFree(p); return r; }
ws NameOf(IMMDevice* d) {
    ComPtr<IPropertyStore> ps; ws r; if (FAILED(d->OpenPropertyStore(STGM_READ, &ps))) return r;
    PROPVARIANT v; PropVariantInit(&v); if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) r = v.pwszVal; PropVariantClear(&v); return r;
}
ComPtr<IAudioEndpointVolume> EvOf(IMMDevice* d) { ComPtr<IAudioEndpointVolume> e; d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)&e); return e; }

void Poll() {
    ComPtr<IMMDevice> dev; VolState s;
    if (!g_en || FAILED(g_en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { g_ev.Reset(); g_defId.clear(); s.ok = false; s.muted = true; s.device = L"No output device"; }
    else {
        ws id = IdOf(dev.Get());
        if (id != g_defId || !g_ev) { bool first = g_defId.empty(); g_defId = id; g_ev = EvOf(dev.Get()); s.device = NameOf(dev.Get()); if (!first && g_onDefault) g_onDefault(); }
        else { std::lock_guard<std::mutex> l(g_mu); s.device = g_vol.device; }
        float f = 0; BOOL m = FALSE;
        if (g_ev && SUCCEEDED(g_ev->GetMasterVolumeLevelScalar(&f)) && SUCCEEDED(g_ev->GetMute(&m))) { s.ok = true; s.vol = f; s.muted = m != 0; }
    }
    bool ch; { std::lock_guard<std::mutex> l(g_mu); ch = s.ok != g_vol.ok || fabsf(s.vol - g_vol.vol) > 0.0005f || s.muted != g_vol.muted || s.device != g_vol.device; g_vol = s; }
    if (ch) SysNotify();
}
void Loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&g_en));
    for (;;) {
        std::function<void()> job;
        { std::unique_lock<std::mutex> l(g_qmu); g_cv.wait_for(l, std::chrono::milliseconds(400), [] { return !g_q.empty(); }); if (!g_q.empty()) { job = std::move(g_q.front()); g_q.pop_front(); } }
        try { if (job) job(); Poll(); } catch (...) { Log(L"audio loop exception"); }
    }
}
template <class T> void Ask(std::function<T()> f, std::function<void(T)> cb) {
    AudioPost([f, cb] { T r = f(); UiPost([cb, r] { cb(r); }); });
}
// Walk every audio session on every active output.
void ForEachSession(const std::function<void(IAudioSessionControl2*, ISimpleAudioVolume*, DWORD)>& visit) {
    ComPtr<IMMDeviceCollection> coll; if (!g_en || FAILED(g_en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll))) return;
    UINT n = 0; coll->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> d; if (FAILED(coll->Item(i, &d))) continue;
        ComPtr<IAudioSessionManager2> mgr; if (FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr))) continue;
        ComPtr<IAudioSessionEnumerator> se; if (FAILED(mgr->GetSessionEnumerator(&se))) continue;
        int ns = 0; se->GetCount(&ns);
        for (int k = 0; k < ns; k++) {
            ComPtr<IAudioSessionControl> c; if (FAILED(se->GetSession(k, &c))) continue;
            AudioSessionState st; c->GetState(&st); if (st == AudioSessionStateExpired) continue;
            ComPtr<IAudioSessionControl2> c2; if (FAILED(c.As(&c2)) || c2->IsSystemSoundsSession() == S_OK) continue;
            DWORD pid = 0; c2->GetProcessId(&pid); if (pid == GetCurrentProcessId()) continue;
            ComPtr<ISimpleAudioVolume> sv; if (FAILED(c.As(&sv))) continue;
            visit(c2.Get(), sv.Get(), pid);
        }
    }
}
}

void AudioPost(std::function<void()> job) { { std::lock_guard<std::mutex> l(g_qmu); g_q.push_back(std::move(job)); } g_cv.notify_one(); }
void AudioOnDefaultChanged(std::function<void()> f) { g_onDefault = std::move(f); }
void AudioStart() { std::thread(Loop).detach(); }
VolState Volume() { std::lock_guard<std::mutex> l(g_mu); return g_vol; }

void AudioSetVolume(float v) {
    AudioPost([v] {
        if (!g_ev) return; float vv = std::clamp(v, 0.f, 1.f);
        g_ev->SetMasterVolumeLevelScalar(vv, nullptr); if (vv > 0) g_ev->SetMute(FALSE, nullptr);
        { std::lock_guard<std::mutex> l(g_mu); g_vol.vol = vv; if (vv > 0) g_vol.muted = false; } SysNotify();
    });
}
void AudioToggleMute() {
    AudioPost([] { if (!g_ev) return; BOOL m = FALSE; g_ev->GetMute(&m); g_ev->SetMute(!m, nullptr); { std::lock_guard<std::mutex> l(g_mu); g_vol.muted = !m; } SysNotify(); });
}
void AudioDevices(std::function<void(std::vector<AudioDev>)> cb) {
    Ask<std::vector<AudioDev>>([] {
        std::vector<AudioDev> v; ComPtr<IMMDeviceCollection> coll; if (!g_en || FAILED(g_en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll))) return v;
        UINT n = 0; coll->GetCount(&n);
        for (UINT i = 0; i < n; i++) {
            ComPtr<IMMDevice> d; if (FAILED(coll->Item(i, &d))) continue;
            AudioDev a; a.id = IdOf(d.Get()); a.name = NameOf(d.Get()); a.def = a.id == g_defId;
            if (auto ev = EvOf(d.Get())) { float f; if (SUCCEEDED(ev->GetMasterVolumeLevelScalar(&f))) a.vol = f; }
            v.push_back(a);
        }
        return v;
    }, std::move(cb));
}
void AudioSetDefault(const ws& id, std::function<void()> done) {
    AudioPost([id, done] {
        ComPtr<IPolicyConfigX> cfg;
        if (SUCCEEDED(CoCreateInstance(CLSID_PolicyConfigX, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&cfg)))) for (int r = 0; r < 3; r++) cfg->SetDefaultEndpoint(id.c_str(), (ERole)r);
        Poll(); if (done) UiPost(done);
    });
}
void AudioSetDeviceVolume(const ws& id, float v) {
    AudioPost([id, v] {
        if (id == g_defId) { if (g_ev) g_ev->SetMasterVolumeLevelScalar(std::clamp(v, 0.f, 1.f), nullptr); return; }
        ComPtr<IMMDevice> d; if (!g_en || FAILED(g_en->GetDevice(id.c_str(), &d))) return;
        if (auto ev = EvOf(d.Get())) ev->SetMasterVolumeLevelScalar(std::clamp(v, 0.f, 1.f), nullptr);
    });
}

// ---------- per-app volume + routing ----------
static ws AppRouteGet(DWORD pid);
void AudioApps(std::function<void(std::vector<AppAudio>)> cb) {
    Ask<std::vector<AppAudio>>([] {
        std::vector<AppAudio> v; std::set<ws> seen;
        ForEachSession([&](IAudioSessionControl2*, ISimpleAudioVolume* sv, DWORD pid) {
            ws exe = ExePathOfPid(pid); ws key = exe.empty() ? std::to_wstring(pid) : Lower(exe); if (!seen.insert(key).second) return;
            AppAudio a; a.exe = exe; a.pid = pid; float f = 0; BOOL m = FALSE; sv->GetMasterVolume(&f); sv->GetMute(&m); a.vol = f; a.muted = m != 0;
            a.name = StemOf(exe);
            DWORD h; DWORD sz = GetFileVersionInfoSizeW(exe.c_str(), &h);
            if (sz) { std::vector<char> buf(sz); if (GetFileVersionInfoW(exe.c_str(), 0, sz, buf.data())) {
                struct LT { WORD l, c; } *tr = nullptr; UINT n = 0;
                if (VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &n) && n >= 4) {
                    wchar_t q[80]; swprintf_s(q, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr->l, tr->c); wchar_t* d = nullptr; UINT dn = 0;
                    if (VerQueryValueW(buf.data(), q, (void**)&d, &dn) && d && *d) a.name = d;
                } } }
            if (a.name.empty()) a.name = L"App";
            a.routed = AppRouteGet(pid); v.push_back(a);
        });
        return v;
    }, std::move(cb));
}
void AudioSetAppVolume(const ws& exe, DWORD pid, float v) {
    AudioPost([=] { ForEachSession([&](IAudioSessionControl2*, ISimpleAudioVolume* sv, DWORD p) { if (p == pid || (!exe.empty() && EqI(ExePathOfPid(p), exe))) sv->SetMasterVolume(std::clamp(v, 0.f, 1.f), nullptr); }); });
}
void AudioSetAppMute(const ws& exe, DWORD pid, bool mute) {
    AudioPost([=] { ForEachSession([&](IAudioSessionControl2*, ISimpleAudioVolume* sv, DWORD p) { if (p == pid || (!exe.empty() && EqI(ExePathOfPid(p), exe))) sv->SetMute(mute, nullptr); }); });
}
// AudioPolicyConfig (what Settings > Volume mixer and EarTrumpet use): undocumented, so every call fails soft.
static void* Factory() {
    static void* f = [] {
        RoInitialize(RO_INIT_MULTITHREADED); void* p = nullptr; HSTRING cls; const wchar_t* n = L"Windows.Media.Internal.AudioPolicyConfig";
        WindowsCreateString(n, (UINT32)wcslen(n), &cls);
        static const GUID iid = {0xab3d4648, 0xe242, 0x459f, {0xb0, 0x2f, 0x54, 0x1c, 0x70, 0x30, 0x63, 0x24}};
        HRESULT hr = RoGetActivationFactory(cls, iid, &p); WindowsDeleteString(cls);
        if (FAILED(hr)) { Log(L"AppRouting factory 0x%08X", hr); return (void*)nullptr; } return p; }();
    return f;
}
static ws AppRouteGet(DWORD pid) {
    void* f = Factory(); if (!f) return {};
    using Get = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, int, int, HSTRING*); HSTRING h = nullptr;
    if (FAILED(SehCall([&] { return ((Get)(*(void***)f)[26])(f, pid, 0, 1, &h); })) || !h) return {};
    ws s = WindowsGetStringRawBuffer(h, nullptr); WindowsDeleteString(h);
    size_t a = s.find(L"MMDEVAPI#"), b = s.rfind(L"#{"); return a != ws::npos && b != ws::npos && b > a ? s.substr(a + 9, b - a - 9) : ws();
}
void AudioRouteApp(const ws& exe, DWORD pid, const ws& dev) {
    AudioPost([=] {
        void* f = Factory(); if (!f) return;
        HSTRING h = nullptr; if (!dev.empty()) { ws full = L"\\\\?\\SWD#MMDEVAPI#" + dev + L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}"; WindowsCreateString(full.c_str(), (UINT32)full.size(), &h); }
        std::set<DWORD> pids{pid};
        if (!exe.empty()) { HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W pe{sizeof pe};
            if (s != INVALID_HANDLE_VALUE) { for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) if (EqI(pe.szExeFile, FileNameOf(exe))) pids.insert(pe.th32ProcessID); CloseHandle(s); } }
        using Set = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, int, int, HSTRING);
        for (DWORD id : pids) for (int role = 0; role < 2; role++) SehCall([&] { return ((Set)(*(void***)f)[25])(f, id, 0, role, h); });
        if (h) WindowsDeleteString(h);
    });
}
ws AudioShortName(const ws& name) {   // "Headphones (2- Sam's Buds Pro)" -> "Sam's Buds Pro"
    size_t open = name.find(L" ("); if (open == ws::npos || name.back() != L')') return name;
    ws outer = name.substr(0, open), inner = name.substr(open + 2, name.size() - open - 3);
    if (inner.size() > 3 && iswdigit(inner[0]) && inner[1] == L'-') inner = Trim(inner.substr(2));
    return (outer == L"Speakers" || outer == L"Headphones" || outer == L"Headset" || outer == L"Headset Earphone" || outer == L"Earphones") ? inner : outer;
}
