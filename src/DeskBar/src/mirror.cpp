#include "pch.h"
#include "app.h"
#include "quick.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <condition_variable>

// "Also play": captures the main output (WASAPI loopback) and replays it on extra devices. Bluetooth outputs run ~0.2 s behind wired ones,
// and Windows reports 0 ms latency for every device, so a connected Bluetooth device (matched by name) is ASSUMED to lag 200 ms
// (shortcut: a typical value, not measured per codec); the slowest device is made the main output and the faster copies are delayed.
// All audio work happens on one thread, so turning this on or off can never freeze the UI. Not verified with real hardware.
namespace {
struct Chunk { std::vector<BYTE> data; int64_t at; };
struct Leg { ws id; ComPtr<IAudioClient> client; ComPtr<IAudioRenderClient> render; UINT32 bufFrames = 0; std::deque<Chunk> pending; size_t pendingBytes = 0; int delayMs = 0; };
std::mutex g_qmu; std::deque<std::function<void()>> g_q; std::condition_variable g_cv;
std::vector<ws> g_wanted; std::set<ws> g_uiWanted;                       // g_wanted: audio thread's copy; g_uiWanted: UI thread view
ComPtr<IMMDeviceEnumerator> g_en; ComPtr<IAudioClient> g_capClient; ComPtr<IAudioCaptureClient> g_cap; WAVEFORMATEX* g_fmt = nullptr; int g_frameBytes = 0; ws g_srcId;
std::vector<Leg> g_legs; std::vector<std::vector<BYTE>> g_pool; int64_t g_freq = 1;
uint64_t g_btAt = 0;

int64_t Qpc() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
void Post(std::function<void()> f) { { std::lock_guard<std::mutex> l(g_qmu); g_q.push_back(std::move(f)); } g_cv.notify_one(); }
ws IdOf(IMMDevice* d) { LPWSTR p = nullptr; ws r; if (SUCCEEDED(d->GetId(&p)) && p) r = p; CoTaskMemFree(p); return r; }
ws NameOf(IMMDevice* d) {
    ComPtr<IPropertyStore> ps; ws r; if (FAILED(d->OpenPropertyStore(STGM_READ, &ps))) return r;
    PROPVARIANT v; PropVariantInit(&v); static const PROPERTYKEY fn = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    if (SUCCEEDED(ps->GetValue(fn, &v)) && v.vt == VT_LPWSTR) r = v.pwszVal; PropVariantClear(&v); return r;
}
bool IsBluetooth(const ws& id) {
    ComPtr<IMMDevice> d; if (!g_en || FAILED(g_en->GetDevice(id.c_str(), &d))) return false;
    if (Now() - g_btAt > 30000) { g_btAt = Now(); BtDevices([](std::vector<BtDev>) {}); }
    ws inner = AudioShortName(NameOf(d.Get()));
    for (auto& b : BtCached()) if (b.connected && (EqI(inner, b.name) || EqI(inner, b.name + L" Hands-Free"))) return true;
    return false;
}
int Latency(const ws& id) { return IsBluetooth(id) ? 200 : 0; }

void StopCapture() { if (g_capClient) g_capClient->Stop(); g_cap.Reset(); g_capClient.Reset(); if (g_fmt) CoTaskMemFree(g_fmt); g_fmt = nullptr; g_srcId.clear(); }
void StopAll() { for (auto& l : g_legs) { if (l.client) l.client->Stop(); Log(L"Mirror off: %s", l.id.c_str()); } g_legs.clear(); StopCapture(); }
bool EnsureCapture() {
    if (g_capClient) return true;
    ComPtr<IMMDevice> dev; if (FAILED(g_en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return false;
    ComPtr<IAudioClient> c; if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&c))) return false;
    WAVEFORMATEX* f = nullptr; if (FAILED(c->GetMixFormat(&f))) return false;
    HRESULT hr = c->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, f, nullptr);
    if (FAILED(hr)) { Log(L"Mirror capture init 0x%08X", hr); CoTaskMemFree(f); return false; }
    ComPtr<IAudioCaptureClient> cap; if (FAILED(c->GetService(IID_PPV_ARGS(&cap)))) { CoTaskMemFree(f); return false; }
    c->Start(); g_capClient = c; g_cap = cap; g_fmt = f; g_frameBytes = f->nBlockAlign; g_srcId = IdOf(dev.Get()); return true;
}
bool StartLeg(const ws& id) {
    ComPtr<IMMDevice> dev; if (FAILED(g_en->GetDevice(id.c_str(), &dev))) return false;
    DWORD st = 0; dev->GetState(&st); if (st != DEVICE_STATE_ACTIVE) return false;
    Leg l; l.id = id; if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&l.client))) return false;
    // AUTOCONVERTPCM + default sample-rate converter: this device may not like the main output's exact format
    HRESULT hr = l.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000, 0, g_fmt, nullptr);
    if (FAILED(hr)) { Log(L"Mirror leg init 0x%08X", hr); return false; }
    l.client->GetBufferSize(&l.bufFrames); if (FAILED(l.client->GetService(IID_PPV_ARGS(&l.render)))) return false;
    l.client->Start(); Log(L"Mirror on: %s", id.c_str()); g_legs.push_back(std::move(l)); return true;
}
void Rebuild() {
    std::vector<ws> wanted = g_wanted; ComPtr<IMMDevice> def; ws defId;
    if (g_en && SUCCEEDED(g_en->GetDefaultAudioEndpoint(eRender, eConsole, &def))) defId = IdOf(def.Get());
    std::erase(wanted, defId);
    int srcLat = !defId.empty() && !wanted.empty() ? Latency(defId) : 0;
    if (!wanted.empty()) {
        auto slowest = *std::max_element(wanted.begin(), wanted.end(), [](const ws& a, const ws& b) { return Latency(a) < Latency(b); });
        if (Latency(slowest) > srcLat) {   // only the extra copies can be delayed, so the slowest device has to be the main output
            Log(L"Mirror: making the slower device the main output so everything lines up");
            ws sl = slowest, di = defId;
            UiPost([sl, di] { std::erase(S.mirrorDevices, sl); if (!di.empty() && std::find(S.mirrorDevices.begin(), S.mirrorDevices.end(), di) == S.mirrorDevices.end()) S.mirrorDevices.push_back(di); SaveSettingsSoon(); });
            std::erase(g_wanted, slowest); if (!defId.empty()) g_wanted.push_back(defId);
            AudioSetDefault(slowest, nullptr);   // the default-changed event rebuilds with the new roles
            return;
        }
    }
    if (defId != g_srcId) StopAll();
    for (auto it = g_legs.begin(); it != g_legs.end();) if (std::find(wanted.begin(), wanted.end(), it->id) == wanted.end()) { if (it->client) it->client->Stop(); it = g_legs.erase(it); } else ++it;
    if (wanted.empty()) { StopCapture(); return; }
    if (!EnsureCapture()) return;
    for (auto& id : wanted) if (std::none_of(g_legs.begin(), g_legs.end(), [&](const Leg& l) { return l.id == id; })) StartLeg(id);
    for (auto& l : g_legs) l.delayMs = std::max(0, srcLat - Latency(l.id));
}
void Pump() {
    if (!g_cap) return;
    for (;;) {
        UINT32 next = 0; if (FAILED(g_cap->GetNextPacketSize(&next)) || !next) break;
        BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0; if (FAILED(g_cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
        int bytes = (int)frames * g_frameBytes; int64_t t = Qpc();
        for (auto& l : g_legs) {   // pooled buffers: no allocation per packet
            Chunk c; if (!g_pool.empty()) { c.data = std::move(g_pool.back()); g_pool.pop_back(); } c.data.resize(bytes); c.at = t;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) memcpy(c.data.data(), data, bytes); else memset(c.data.data(), 0, bytes);
            l.pendingBytes += bytes; l.pending.push_back(std::move(c));
        }
        g_cap->ReleaseBuffer(frames);
    }
    int64_t now = Qpc();
    for (auto& l : g_legs) {
        int64_t delay = l.delayMs * g_freq / 1000;
        while (l.pendingBytes > (size_t)g_frameBytes * 48 * (400 + l.delayMs) && !l.pending.empty()) { l.pendingBytes -= l.pending.front().data.size(); g_pool.push_back(std::move(l.pending.front().data)); l.pending.pop_front(); }   // never fall far behind
        UINT32 padding = 0; if (FAILED(l.client->GetCurrentPadding(&padding))) continue; UINT32 freeF = l.bufFrames - padding;
        while (freeF > 0 && !l.pending.empty()) {
            Chunk& c = l.pending.front(); if (now - c.at < delay) break;
            UINT32 cf = (UINT32)(c.data.size() / g_frameBytes); if (cf > freeF) break;
            BYTE* dst = nullptr; if (FAILED(l.render->GetBuffer(cf, &dst))) break;
            memcpy(dst, c.data.data(), c.data.size()); l.render->ReleaseBuffer(cf, 0);
            l.pendingBytes -= c.data.size(); freeF -= cf; g_pool.push_back(std::move(c.data)); l.pending.pop_front();
        }
    }
}
void Loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&g_en));
    for (;;) {
        std::function<void()> job;
        { std::unique_lock<std::mutex> l(g_qmu); g_cv.wait_for(l, std::chrono::milliseconds(g_legs.empty() ? 500 : 8), [] { return !g_q.empty(); }); if (!g_q.empty()) { job = std::move(g_q.front()); g_q.pop_front(); } }
        try { if (job) job(); if (!g_legs.empty()) Pump(); } catch (...) { Log(L"mirror exception"); Sleep(50); }
    }
}
void Push() { std::vector<ws> v = S.mirrorDevices; Post([v] { g_wanted = v; Rebuild(); }); }
}

void MirrorStart() { std::thread(Loop).detach(); Push(); AudioOnDefaultChanged([] { if (!g_wanted.empty()) Post([] { StopAll(); Rebuild(); }); }); }
bool MirrorIsOn(const ws& id) { return std::find(S.mirrorDevices.begin(), S.mirrorDevices.end(), id) != S.mirrorDevices.end(); }
void MirrorSet(const ws& id, bool on) {
    bool has = MirrorIsOn(id); if (on == has) return;
    if (on) S.mirrorDevices.push_back(id); else std::erase(S.mirrorDevices, id);
    SaveSettingsSoon(); Push();
}
void MirrorRemove(const ws& id) { MirrorSet(id, false); }
