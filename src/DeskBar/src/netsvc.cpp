#include "pch.h"
#include "app.h"
#include <unknwn.h>
#include <netlistmgr.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Devices.WiFi.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Networking.NetworkOperators.h>
#include <winrt/Windows.Security.Credentials.h>
#include <condition_variable>

using namespace winrt;
using namespace winrt::Windows::Devices::Radios;
using namespace winrt::Windows::Devices::WiFi;
using namespace winrt::Windows::Networking::Connectivity;
namespace NetOps = winrt::Windows::Networking::NetworkOperators;

// Wi-Fi, Bluetooth, airplane mode, hotspot. Everything runs on ONE worker thread with blocking WinRT calls; the UI reads snapshots.
namespace {
std::mutex g_mu; NetState g_net; std::vector<WifiNet> g_wifiLast; std::vector<BtDev> g_btLast;
std::deque<std::function<void()>> g_q; std::mutex g_qmu; std::condition_variable g_cv;
Radio g_wifiRadio{nullptr}, g_btRadio{nullptr}; WiFiAdapter g_adapter{nullptr};
std::map<ws, WiFiAvailableNetwork> g_raw;
std::atomic<bool> g_refreshQueued{false};

void Post(std::function<void()> f) { { std::lock_guard<std::mutex> l(g_qmu); g_q.push_back(std::move(f)); } g_cv.notify_one(); }

// --- airplane mode via the shell's radio manager ---
const GUID kRmClsid = {0x581333F6, 0x28DB, 0x41BE, {0xBC, 0x7A, 0xFF, 0x20, 0x1F, 0x12, 0xF3, 0xF6}}, kRmIid = {0xDB3AFBFB, 0x08E6, 0x46C6, {0xAA, 0x70, 0xBF, 0x9A, 0x34, 0xC3, 0x0A, 0xB7}};
bool AirplaneIsOn() {
    void* rm = nullptr; if (FAILED(CoCreateInstance(kRmClsid, nullptr, CLSCTX_ALL, kRmIid, &rm)) || !rm) return false;
    int enabled = 1, unk = 0, state = 0; using F = HRESULT(STDMETHODCALLTYPE*)(void*, int*, int*, int*);
    bool on = SUCCEEDED(SehCall([&] { return ((F)(*(void***)rm)[5])(rm, &enabled, &unk, &state); })) && enabled == 0;
    ((IUnknown*)rm)->Release(); return on;
}
void AirplaneSet(bool on) {
    void* rm = nullptr; if (FAILED(CoCreateInstance(kRmClsid, nullptr, CLSCTX_ALL, kRmIid, &rm)) || !rm) return;
    using F = HRESULT(STDMETHODCALLTYPE*)(void*, int); SehCall([&] { return ((F)(*(void***)rm)[6])(rm, on ? 0 : 1); }); ((IUnknown*)rm)->Release();
}
// --- the network names Windows shows in Settings ---
ws NetworkNameForAdapter(const GUID& adapter) {
    ComPtr<INetworkListManager> mgr; if (FAILED(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&mgr)))) return {};
    ComPtr<IEnumNetworks> nets; if (FAILED(mgr->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &nets))) return {};
    for (;;) {
        ComPtr<INetwork> n; ULONG got = 0; if (nets->Next(1, &n, &got) != S_OK || !got) break;
        ComPtr<IEnumNetworkConnections> cs; if (FAILED(n->GetNetworkConnections(&cs))) continue;
        for (;;) {
            ComPtr<INetworkConnection> c; ULONG g2 = 0; if (cs->Next(1, &c, &g2) != S_OK || !g2) break;
            GUID id; if (SUCCEEDED(c->GetAdapterId(&id)) && id == adapter) { BSTR b = nullptr; ws r; if (SUCCEEDED(n->GetName(&b)) && b) r = b; SysFreeString(b); return r; }
        }
    }
    return {};
}

void Publish(const NetState& s) {
    bool ch; { std::lock_guard<std::mutex> l(g_mu); ch = s.kind != g_net.kind || s.bars != g_net.bars || s.internet != g_net.internet || s.airplane != g_net.airplane || s.wifiOn != g_net.wifiOn || s.btOn != g_net.btOn
        || s.name != g_net.name || s.status != g_net.status || s.btSubtitle != g_net.btSubtitle || s.ip != g_net.ip || s.adapter != g_net.adapter || s.needsSignIn != g_net.needsSignIn || s.btPresent != g_net.btPresent; g_net = s; }
    if (ch) SysNotify();
}
ws BtSubtitleNow();
void RefreshNow() {
    NetState s; { std::lock_guard<std::mutex> l(g_mu); s.btSubtitle = g_net.btSubtitle; }
    try {
        auto p = NetworkInformation::GetInternetConnectionProfile();
        // at sign-in a virtual adapter without internet can be the only profile: that's "not connected"
        if (p && !p.IsWlanConnectionProfile() && p.GetNetworkConnectivityLevel() != NetworkConnectivityLevel::InternetAccess) p = nullptr;
        if (p) {
            auto lv = p.GetNetworkConnectivityLevel();
            s.internet = lv == NetworkConnectivityLevel::InternetAccess; s.needsSignIn = lv == NetworkConnectivityLevel::ConstrainedInternetAccess;
            s.status = lv == NetworkConnectivityLevel::InternetAccess ? L"Connected" : lv == NetworkConnectivityLevel::ConstrainedInternetAccess ? L"Sign-in required" : lv == NetworkConnectivityLevel::LocalAccess ? L"Connected, no internet" : L"No connectivity";
            try {
                auto id = p.NetworkAdapter().NetworkAdapterId();
                for (auto h : NetworkInformation::GetHostNames()) {
                    if (h.Type() != winrt::Windows::Networking::HostNameType::Ipv4 || !h.IPInformation() || !h.IPInformation().NetworkAdapter()) continue;
                    if (h.IPInformation().NetworkAdapter().NetworkAdapterId() == id) { s.ip = h.CanonicalName().c_str(); break; }
                }
            } catch (...) {}
            if (p.IsWlanConnectionProfile()) {
                s.kind = NetState::WiFi; auto b = p.GetSignalBars(); s.bars = b ? (int)b.Value() : 4;
                try { s.name = p.WlanConnectionProfileDetails().GetConnectedSsid().c_str(); } catch (...) {}
                if (s.name.empty()) s.name = p.ProfileName().c_str();
            } else {
                s.kind = p.NetworkAdapter() && p.NetworkAdapter().IanaInterfaceType() == 6 ? NetState::Ether : NetState::Other; s.name = p.ProfileName().c_str();
                try {
                    ws listed = NetworkNameForAdapter(p.NetworkAdapter().NetworkAdapterId()); if (!listed.empty()) s.name = listed;
                } catch (...) {}
            }
        }
        s.wifiPresent = (bool)g_wifiRadio; s.wifiOn = g_wifiRadio && g_wifiRadio.State() == RadioState::On;
        s.btPresent = (bool)g_btRadio; s.btOn = g_btRadio && g_btRadio.State() == RadioState::On;
        s.airplane = AirplaneIsOn();
    } catch (hresult_error const& e) { Log(L"net refresh: %s", e.message().c_str()); }
    Publish(s);
}
void QueueRefresh() { if (!g_refreshQueued.exchange(true)) Post([] { g_refreshQueued = false; RefreshNow(); }); }

void BtRefreshList() {
    std::vector<BtDev> list;
    try {
        auto props = single_threaded_vector<hstring>({hstring{L"System.Devices.Aep.IsConnected"}});
        hstring sels[2] = {winrt::Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)};
        for (auto& sel : sels) {
            auto infos = winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(sel, props, winrt::Windows::Devices::Enumeration::DeviceInformationKind::AssociationEndpoint).get();
            for (auto info : infos) {
                ws name = info.Name().c_str(); if (Trim(name).empty()) continue;
                bool conn = false; auto v = info.Properties().TryLookup(L"System.Devices.Aep.IsConnected");
                if (v) { auto b = v.try_as<winrt::Windows::Foundation::IReference<bool>>(); if (b) conn = b.Value(); }
                // earbuds pair twice (classic + LE audio) under one name: one entry, connected if either is
                auto it = std::find_if(list.begin(), list.end(), [&](const BtDev& d) { return d.name == name; });
                if (it == list.end()) list.push_back({name, info.Id().c_str(), conn}); else if (conn && !it->connected) { it->connected = true; it->id = info.Id().c_str(); }
            }
        }
    } catch (hresult_error const& e) { Log(L"bt devices: %s", e.message().c_str()); }
    std::stable_sort(list.begin(), list.end(), [](const BtDev& a, const BtDev& b) { return a.connected != b.connected ? a.connected : a.name < b.name; });
    { std::lock_guard<std::mutex> l(g_mu); g_btLast = list; }
}
void BtSubtitleRefresh() {
    ws sub;
    if (g_btRadio && g_btRadio.State() == RadioState::On) {
        BtRefreshList(); std::vector<ws> c; { std::lock_guard<std::mutex> l(g_mu); for (auto& d : g_btLast) if (d.connected) c.push_back(d.name); }
        sub = c.empty() ? L"" : c.size() == 1 ? c[0] : Fmt(L"%d devices", (int)c.size());
    }
    NetState s; { std::lock_guard<std::mutex> l(g_mu); s = g_net; } if (s.btSubtitle != sub) { s.btSubtitle = sub; Publish(s); }
}

void Loop() {
    init_apartment(apartment_type::multi_threaded);
    try {
        winrt::Windows::Networking::Connectivity::NetworkInformation::NetworkStatusChanged([](auto&&) { QueueRefresh(); });
        if (Radio::RequestAccessAsync().get() == RadioAccessStatus::Allowed) {
            for (auto r : Radio::GetRadiosAsync().get()) {
                if (r.Kind() == RadioKind::WiFi && !g_wifiRadio) { g_wifiRadio = r; r.StateChanged([](auto&&, auto&&) { QueueRefresh(); }); }
                if (r.Kind() == RadioKind::Bluetooth && !g_btRadio) { g_btRadio = r; r.StateChanged([](auto&&, auto&&) { QueueRefresh(); Post(BtSubtitleRefresh); }); }
            }
        }
    } catch (hresult_error const& e) { Log(L"net start: %s", e.message().c_str()); }
    RefreshNow(); BtSubtitleRefresh();
    uint64_t lastPoll = Now();
    for (;;) {
        std::function<void()> job;
        { std::unique_lock<std::mutex> l(g_qmu); g_cv.wait_for(l, std::chrono::seconds(2), [] { return !g_q.empty(); }); if (!g_q.empty()) { job = std::move(g_q.front()); g_q.pop_front(); } }
        try { if (job) job(); } catch (hresult_error const& e) { Log(L"net job: %s", e.message().c_str()); } catch (...) {}
        if (Now() - lastPoll > 10000) { lastPoll = Now(); RefreshNow(); }
    }
}
WifiNet ToNet(const WiFiAvailableNetwork& n, const ws& connected) {
    WifiNet w; w.ssid = n.Ssid().c_str(); w.bars = n.SignalBars(); w.connected = w.ssid == connected;
    auto t = n.SecuritySettings().NetworkAuthenticationType(); using A = winrt::Windows::Networking::Connectivity::NetworkAuthenticationType;
    w.secured = !(t == A::Open80211 || t == A::None);
    w.security = (t == A::Wpa3Sae || t == A::Wpa3Enterprise) ? (t == A::Wpa3Sae ? L"WPA3" : L"WPA3 Enterprise") : t == A::Wpa3 ? L"WPA3 Enterprise" : t == A::RsnaPsk ? L"WPA2" : t == A::Rsna ? L"WPA2 Enterprise" : t == A::WpaPsk ? L"WPA" : t == A::Wpa ? L"WPA Enterprise"
        : (t == A::Open80211 || t == A::None) ? L"Open" : t == A::Owe ? L"Enhanced open" : L"Secured";
    w.enterprise = t == A::Rsna || t == A::Wpa || t == A::Wpa3 || t == A::Wpa3Enterprise;
    int k = n.ChannelCenterFrequencyInKilohertz(); w.band = k > 5925000 ? L"6 GHz" : k > 4900000 ? L"5 GHz" : k > 0 ? L"2.4 GHz" : L"";
    w.pct = std::clamp((int)lroundf((n.NetworkRssiInDecibelMilliwatts() + 100) * 2), 0, 100); return w;
}
}

NetState NetSnap() { std::lock_guard<std::mutex> l(g_mu); return g_net; }
void NetStart() { std::thread(Loop).detach(); }
void NetRefresh() { QueueRefresh(); }
void NetSetWifi(bool on) { Post([on] { try { if (g_wifiRadio) g_wifiRadio.SetStateAsync(on ? RadioState::On : RadioState::Off).get(); } catch (...) {} RefreshNow(); }); }
void NetSetBt(bool on) { Post([on] { try { if (g_btRadio) g_btRadio.SetStateAsync(on ? RadioState::On : RadioState::Off).get(); } catch (...) {} RefreshNow(); BtSubtitleRefresh(); }); }
void NetSetAirplane(bool on) { Post([on] { AirplaneSet(on); Sleep(300); RefreshNow(); }); }
void NetHotspot(bool query, bool on, std::function<void(int)> cb) {
    Post([=] {
        int r = -1;
        try {
            auto p = NetworkInformation::GetInternetConnectionProfile();
            if (p) {
                auto m = NetOps::NetworkOperatorTetheringManager::CreateFromConnectionProfile(p);
                if (query) r = m.TetheringOperationalState() == NetOps::TetheringOperationalState::On ? 1 : 0;
                else { auto res = on ? m.StartTetheringAsync().get() : m.StopTetheringAsync().get(); r = res.Status() == NetOps::TetheringOperationStatus::Success ? (on ? 1 : 0) : -1; }
            }
        } catch (hresult_error const& e) { Log(L"hotspot: %s", e.message().c_str()); }
        if (cb) UiPost([cb, r] { cb(r); });
    });
}
std::vector<WifiNet> WifiLast() { std::lock_guard<std::mutex> l(g_mu); return g_wifiLast; }
void WifiScan(bool fresh, std::function<void(bool, std::vector<WifiNet>)> cb) {
    Post([=] {
        std::vector<WifiNet> out; bool access = true;
        try {
            if (WiFiAdapter::RequestAccessAsync().get() != WiFiAccessStatus::Allowed) access = false;
            else {
                if (!g_adapter) { auto all = WiFiAdapter::FindAllAdaptersAsync().get(); if (all.Size()) g_adapter = all.GetAt(0); }
                if (g_adapter) {
                    if (fresh) g_adapter.ScanAsync().get();
                    ws connected; try { auto pr = g_adapter.NetworkAdapter().GetConnectedProfileAsync().get(); if (pr) connected = pr.WlanConnectionProfileDetails().GetConnectedSsid().c_str(); } catch (...) {}
                    std::map<ws, WiFiAvailableNetwork> best;
                    for (auto n : g_adapter.NetworkReport().AvailableNetworks()) {
                        ws ssid = n.Ssid().c_str(); if (ssid.empty()) continue;
                        auto it = best.find(ssid); if (it == best.end() || n.NetworkRssiInDecibelMilliwatts() > it->second.NetworkRssiInDecibelMilliwatts()) best.insert_or_assign(ssid, n);
                    }
                    g_raw = best; for (auto& kv : best) out.push_back(ToNet(kv.second, connected));
                    std::sort(out.begin(), out.end(), [](const WifiNet& a, const WifiNet& b) { return a.connected != b.connected ? a.connected : a.bars > b.bars; });
                    if (!out.empty() || fresh) { std::lock_guard<std::mutex> l(g_mu); g_wifiLast = out; }
                }
            }
        } catch (hresult_error const& e) { Log(L"wifi scan: %s", e.message().c_str()); std::lock_guard<std::mutex> l(g_mu); out = g_wifiLast; }
        UiPost([cb, access, out] { cb(access, out); });
    });
}
void WifiConnect(const ws& ssid, const ws& pass, const ws& user, bool usePass, std::function<void(ws)> cb) {
    Post([=] {
        ws err;
        try {
            auto it = g_raw.find(ssid);
            if (!g_adapter || it == g_raw.end()) err = L"Network not available";
            else {
                winrt::Windows::Devices::WiFi::WiFiConnectionResult res{nullptr};
                if (!usePass) res = g_adapter.ConnectAsync(it->second, WiFiReconnectionKind::Automatic).get();
                else {
                    winrt::Windows::Security::Credentials::PasswordCredential cred; cred.Password(pass); if (!user.empty()) cred.UserName(user);
                    res = g_adapter.ConnectAsync(it->second, WiFiReconnectionKind::Automatic, cred).get();
                }
                switch (res.ConnectionStatus()) {
                case WiFiConnectionStatus::Success: break;
                case WiFiConnectionStatus::InvalidCredential: err = L"password"; break;
                case WiFiConnectionStatus::Timeout: err = L"Timed out"; break;
                case WiFiConnectionStatus::NetworkNotAvailable: err = L"Network not available"; break;
                case WiFiConnectionStatus::AccessRevoked: err = L"Access revoked"; break;
                default: err = L"Couldn't connect";
                }
            }
        } catch (hresult_error const& e) { Log(L"wifi connect: %s", e.message().c_str()); err = L"Couldn't connect"; }
        RefreshNow(); UiPost([cb, err] { cb(err); });
    });
}
void WifiDisconnect() { Post([] { try { if (g_adapter) g_adapter.Disconnect(); } catch (...) {} RefreshNow(); }); }
std::vector<BtDev> BtCached() { std::lock_guard<std::mutex> l(g_mu); return g_btLast; }
void BtDevices(std::function<void(std::vector<BtDev>)> cb) { Post([cb] { BtRefreshList(); std::vector<BtDev> v; { std::lock_guard<std::mutex> l(g_mu); v = g_btLast; } UiPost([cb, v] { cb(v); }); }); }
