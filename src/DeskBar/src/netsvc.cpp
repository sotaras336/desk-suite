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
std::mutex g_adMu, g_scanMu;   // g_adapter is shared by the worker, the scan threads and the connect thread
std::atomic<bool> g_probing{false}; std::atomic<uint64_t> g_probeAt{0}; int g_portal = -1; ws g_portalSsid;   // the last sign-in page check and the network it was for (g_mu)
WiFiAdapter Adapter() { std::lock_guard<std::mutex> l(g_adMu); if (!g_adapter) { auto all = WiFiAdapter::FindAllAdaptersAsync().get(); if (all.Size()) g_adapter = all.GetAt(0); } return g_adapter; }
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
void QueueRefresh();
void ProbeSoon(const ws& ssid) {   // on its own thread: without an answer it waits seconds
    if (g_probing.exchange(true)) return;
    std::thread([ssid] {
        bool first; { std::lock_guard<std::mutex> l(g_mu); first = g_portalSsid != ssid; }   // a network with no internet at all is asked once per look, not four times
        int r = -1; for (int i = 0; i < (first ? 4 : 1) && r < 0; i++) { if (i) Sleep(1500); r = PortalProbe(); }   // right after joining there is no address yet: a few tries
        bool ch; { std::lock_guard<std::mutex> l(g_mu); ch = g_portalSsid != ssid || g_portal != r; g_portalSsid = ssid; g_portal = r; }
        g_probeAt = Now(); g_probing = false; if (ch) { Log(L"wifi %s: sign-in page check says %s", ssid.c_str(), r == 1 ? L"sign-in needed" : r == 0 ? L"online" : L"no answer"); QueueRefresh(); }
    }).detach();
}
void RefreshNow() {
    NetState s; { std::lock_guard<std::mutex> l(g_mu); s.btSubtitle = g_net.btSubtitle; }
    try {
        auto p = NetworkInformation::GetInternetConnectionProfile();
        // at sign-in a virtual adapter without internet can be the only profile: that's "not connected"
        if (p && !p.IsWlanConnectionProfile() && p.GetNetworkConnectivityLevel() != NetworkConnectivityLevel::InternetAccess) p = nullptr;
        // Wi-Fi that has no internet (yet): a router with its line down, or a sign-in page in the way. Windows names no "internet profile" then, and the bar said "not connected"
        if (!p) for (auto c : NetworkInformation::GetConnectionProfiles()) if (c && c.IsWlanConnectionProfile() && c.GetNetworkConnectivityLevel() != NetworkConnectivityLevel::None) { p = c; break; }
        if (p) {
            auto lv = p.GetNetworkConnectivityLevel();
            s.internet = lv == NetworkConnectivityLevel::InternetAccess; s.needsSignIn = lv == NetworkConnectivityLevel::ConstrainedInternetAccess;
            s.status = lv == NetworkConnectivityLevel::InternetAccess ? L"Connected" : lv == NetworkConnectivityLevel::ConstrainedInternetAccess ? L"Sign-in required" : lv == NetworkConnectivityLevel::LocalAccess ? L"Connected, no internet" : L"No connectivity";
            try {
                auto na = p.NetworkAdapter(); if (!na) throw hresult_error(E_POINTER); auto id = na.NetworkAdapterId();   // a profile can come without its adapter: calling into nothing is a crash try/catch does not stop
                for (auto h : NetworkInformation::GetHostNames()) {
                    if (h.Type() != winrt::Windows::Networking::HostNameType::Ipv4 || !h.IPInformation() || !h.IPInformation().NetworkAdapter()) continue;
                    if (h.IPInformation().NetworkAdapter().NetworkAdapterId() == id) { s.ip = h.CanonicalName().c_str(); break; }
                }
            } catch (...) {}
            if (p.IsWlanConnectionProfile()) {
                s.kind = NetState::WiFi; auto b = p.GetSignalBars(); s.bars = b ? (int)b.Value() : 4;
                try { s.name = p.WlanConnectionProfileDetails().GetConnectedSsid().c_str(); } catch (...) {}
                if (s.name.empty()) s.name = p.ProfileName().c_str();
                int portal; bool mine; { std::lock_guard<std::mutex> l(g_mu); portal = g_portal; mine = g_portalSsid == s.name; }
                if (!s.internet && (!mine || Now() - g_probeAt > 5000)) ProbeSoon(s.name);   // our own check is quicker than Windows' and runs right after joining
                if (mine && portal == 1 && !s.internet) { s.needsSignIn = true; s.status = L"Sign-in required"; }
                else if (mine && portal == 0 && !s.internet) { s.internet = true; s.needsSignIn = false; s.status = L"Connected"; }   // signed in a moment ago: Windows has not looked again yet
            } else {
                s.kind = p.NetworkAdapter() && p.NetworkAdapter().IanaInterfaceType() == 6 ? NetState::Ether : NetState::Other; s.name = p.ProfileName().c_str();
                try {
                    if (auto na = p.NetworkAdapter()) { ws listed = NetworkNameForAdapter(na.NetworkAdapterId()); if (!listed.empty()) s.name = listed; }
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
    auto batt = BtBatteries();   // what Windows knows of each connected device's charge
    for (auto& d : list) { ws addr = BtAddressFromId(d.id); if (d.connected && !addr.empty()) for (auto& b : batt) if (ContainsI(b.first, addr)) d.battery = b.second; }
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
    try {   // a paired device connected, dropped, was added or forgotten: Windows tells us, and the tile's "2 devices" and the page follow at once (they used to go stale until the page was opened)
        using namespace winrt::Windows::Devices::Enumeration; static std::vector<DeviceWatcher> keep; static std::atomic<bool> queued{false};
        auto poke = [] { if (!queued.exchange(true)) Post([] { Sleep(300); queued = false; BtSubtitleRefresh(); SysNotify(); }); };   // a burst (every device at start, both halves of earbuds) is one look
        auto props = single_threaded_vector<hstring>({hstring{L"System.Devices.Aep.IsConnected"}});
        for (auto& sel : {winrt::Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)}) {
            auto w = DeviceInformation::CreateWatcher(sel, props, DeviceInformationKind::AssociationEndpoint);
            w.Added([poke](auto&&, auto&&) { poke(); }); w.Updated([poke](auto&&, auto&&) { poke(); }); w.Removed([poke](auto&&, auto&&) { poke(); });
            w.Start(); keep.push_back(w);
        }
    } catch (hresult_error const& e) { Log(L"bt watch: %s", e.message().c_str()); }
    WlanOnChange([] { g_probeAt = 0; QueueRefresh(); });   // joined or dropped: the bar shows it at once (it used to wait for Windows' slower notice or the 10 s look)
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
                if (!m) r = -1;
                else if (query) r = m.TetheringOperationalState() == NetOps::TetheringOperationalState::On ? 1 : 0;
                else { auto res = on ? m.StartTetheringAsync().get() : m.StopTetheringAsync().get(); r = res && res.Status() == NetOps::TetheringOperationStatus::Success ? (on ? 1 : 0) : -1; }
            }
        } catch (hresult_error const& e) { Log(L"hotspot: %s", e.message().c_str()); }
        if (cb) UiPost([cb, r] { cb(r); });
    });
}
std::vector<WifiNet> WifiLast() { std::lock_guard<std::mutex> l(g_mu); return g_wifiLast; }
// The network this adapter is joined to ("" = none). Windows can hand back a connection profile whose Wi-Fi details are not there yet (right after connecting): calling
// into that missing object is a null dereference, which try/catch does not catch, so it is checked first.
static ws ConnectedSsid() {
    { ws cur; if (WlanCurrentSsid(cur)) return cur; }   // Windows' own engine answers at once
    try { auto ad = Adapter(); if (!ad) return {}; auto na = ad.NetworkAdapter(); if (!na) return {}; auto pr = na.GetConnectedProfileAsync().get(); if (!pr) return {}; auto d = pr.WlanConnectionProfileDetails(); if (!d) return {}; return ws(d.GetConnectedSsid().c_str()); } catch (...) { return {}; }
}
static std::atomic<int> g_connecting{0};   // a connect is under way: no fresh scan may start under it (a scan aborts a connection that is starting)
void WifiScan(bool fresh, std::function<void(bool, std::vector<WifiNet>)> cb) {
    // On its own thread: a scan takes seconds, and on the shared worker it held up the Wi-Fi switch, Bluetooth and the bar's status behind it.
    std::thread([=] {
        winrt::init_apartment(); std::vector<WifiNet> out; bool access = true;
        try {
            if (WiFiAdapter::RequestAccessAsync().get() != WiFiAccessStatus::Allowed) access = false;
            else if (auto ad = Adapter()) {
                if (fresh && !g_connecting) {
                    std::lock_guard<std::mutex> one(g_scanMu);   // one scan at a time
                    for (int attempt = 0; attempt < 2 && !g_connecting; attempt++) {   // "Operation aborted" = a connection was starting under the scan: once more, then use what the adapter already knows (its report is still right about who is connected)
                        try { ad.ScanAsync().get(); break; } catch (hresult_error const& e) { Log(L"wifi scan: %s", e.message().c_str()); if (!attempt) Sleep(1200); }
                    }
                }
                ws connected = ConnectedSsid(); std::set<ws> saved; WlanSaved(saved);
                std::map<ws, WiFiAvailableNetwork> best;
                for (auto n : ad.NetworkReport().AvailableNetworks()) {
                    ws ssid = n.Ssid().c_str(); if (ssid.empty()) continue;
                    auto it = best.find(ssid); if (it == best.end() || n.NetworkRssiInDecibelMilliwatts() > it->second.NetworkRssiInDecibelMilliwatts()) best.insert_or_assign(ssid, n);
                }
                for (auto& kv : best) { out.push_back(ToNet(kv.second, connected)); out.back().saved = saved.count(out.back().ssid) > 0; }
                std::sort(out.begin(), out.end(), WifiBefore);
                if (!out.empty() || fresh) { std::lock_guard<std::mutex> l(g_mu); g_wifiLast = out; }
            }
        } catch (hresult_error const& e) { Log(L"wifi scan: %s", e.message().c_str()); std::lock_guard<std::mutex> l(g_mu); out = g_wifiLast; }
        UiPost([cb, access, out] { cb(access, out); });
    }).detach();
}
// The order of the list: the network we are on, then the ones this PC already knows, then the rest; strongest first within each
bool WifiBefore(const WifiNet& a, const WifiNet& b) { return a.connected != b.connected ? a.connected : a.saved != b.saved ? a.saved : a.pct > b.pct; }
// Is the adapter joined to this network right now? (what Windows itself says, whatever the connect call reported)
static bool ConnectedTo(const ws& ssid) { return ConnectedSsid() == ssid; }
static const wchar_t* StatusName(WiFiConnectionStatus s) {
    switch (s) { case WiFiConnectionStatus::Success: return L"Success"; case WiFiConnectionStatus::AccessRevoked: return L"AccessRevoked"; case WiFiConnectionStatus::InvalidCredential: return L"InvalidCredential";
    case WiFiConnectionStatus::NetworkNotAvailable: return L"NetworkNotAvailable"; case WiFiConnectionStatus::Timeout: return L"Timeout"; case WiFiConnectionStatus::UnsupportedAuthenticationProtocol: return L"UnsupportedAuthenticationProtocol"; default: return L"UnspecifiedFailure"; }
}
// One try. "" = connected, "password", or a message. The network is looked up in the adapter's CURRENT report (the copy kept from the last scan can be minutes
// old, with an access point that has since changed: the first try failed instantly and a later one, after a rescan, worked), and the strongest signal wins.
static ws TryConnect(const ws& ssid, const ws& pass, const ws& user, bool usePass, bool scanFirst) {
    auto g_adapter = Adapter(); if (!g_adapter) return L"No Wi-Fi adapter found";
    if (scanFirst) { try { g_adapter.ScanAsync().get(); } catch (...) {} }
    WiFiAvailableNetwork best{nullptr}; auto rep = g_adapter.NetworkReport(); if (!rep) return L"Network not available";
    for (auto n : rep.AvailableNetworks()) if (ws(n.Ssid().c_str()) == ssid && (!best || n.NetworkRssiInDecibelMilliwatts() > best.NetworkRssiInDecibelMilliwatts())) best = n;
    if (!best) { Log(L"wifi connect %s: not in the adapter's report%s", ssid.c_str(), scanFirst ? L" even after a scan" : L""); return L"Network not available"; }
    uint64_t t0 = Now(); winrt::Windows::Devices::WiFi::WiFiConnectionResult res{nullptr};
    if (!usePass) res = g_adapter.ConnectAsync(best, WiFiReconnectionKind::Automatic).get();
    else {
        winrt::Windows::Security::Credentials::PasswordCredential cred; cred.Password(pass); if (!user.empty()) cred.UserName(user);
        res = g_adapter.ConnectAsync(best, WiFiReconnectionKind::Automatic, cred).get();
    }
    auto st = res.ConnectionStatus(); Log(L"wifi connect %s: %s after %d ms%s", ssid.c_str(), StatusName(st), (int)(Now() - t0), scanFirst ? L" (second try)" : L"");
    switch (st) {
    case WiFiConnectionStatus::Success: return L"";
    case WiFiConnectionStatus::InvalidCredential: return L"password";
    case WiFiConnectionStatus::Timeout: return L"Timed out";
    case WiFiConnectionStatus::NetworkNotAvailable: return L"Network not available";
    case WiFiConnectionStatus::AccessRevoked: return L"Access revoked";
    default: return L"Couldn't connect";
    }
}
// The whole connect, blocking (runs on the worker thread): "" = connected, "password", or a message.
static ws ConnectBlocking(const ws& ssid, const ws& pass, const ws& user, bool usePass) {
    ws err;
    try {
        // A network this PC already knows: Windows' own engine joins it by its saved profile, as Windows' network list does. It finds the access point itself
        // (no scan report that can be stale) and it is the quick way to change from one network to another.
        ws native = usePass ? ws(L"none") : WlanConnectSaved(ssid, 20000);
        if (native.empty() || native == L"password") return native;
        {
            err = native == L"none" ? TryConnect(ssid, pass, user, usePass, false) : native;
            if (!err.empty() && err != L"password") err = TryConnect(ssid, pass, user, usePass, true);   // a stale or aborted first try: scan, look again, try once more
            if (!err.empty()) {   // the call can report a failure for a connection Windows then completes (enterprise sign-in especially): trust what the adapter says
                int waitMs = err == L"password" ? 3000 : 8000, t = 0; while (t < waitMs && !ConnectedTo(ssid)) { Sleep(500); t += 500; }
                if (ConnectedTo(ssid)) { Log(L"wifi connect %s: reported \"%s\" but Windows is connected (after %d more ms)", ssid.c_str(), err.c_str(), t); err.clear(); }
            }
        }
    } catch (hresult_error const& e) { Log(L"wifi connect %s: %s (0x%08x)", ssid.c_str(), e.message().c_str(), (unsigned)e.code().value); err = L"Couldn't connect"; if (ConnectedTo(ssid)) err.clear(); }
    return err;
}
void WifiConnect(const ws& ssid, const ws& pass, const ws& user, bool usePass, std::function<void(ws)> cb) {
    // On its own thread, at once: it used to wait in the worker's queue behind the scan the Wi-Fi page had just started (seconds), and a scan started by reopening the
    // panel could abort it. Closing the panel never stops it: only the page's "done" callback is skipped when the page is gone.
    std::thread([=] {
        winrt::init_apartment(); g_connecting++;
        ws err = ConnectBlocking(ssid, pass, user, usePass); g_connecting--;
        if (err.empty()) { std::lock_guard<std::mutex> l(g_mu); for (auto& n : g_wifiLast) { n.connected = n.ssid == ssid; if (n.connected) n.saved = true; } std::sort(g_wifiLast.begin(), g_wifiLast.end(), WifiBefore); }   // the saved list still says the OLD network is the connected one until a scan succeeds: the page showed that one right after the green check
        Post([] { RefreshNow(); }); UiPost([cb, err] { cb(err); });
    }).detach();
}
// "DeskBar.exe --wifitest <ssid>": drops the connection, connects again exactly as the Wi-Fi page does (a saved profile, no password), and logs how it went.
int WifiTest(const ws& ssid, bool keep) {
    init_apartment(apartment_type::multi_threaded);
    try {
        if (WiFiAdapter::RequestAccessAsync().get() != WiFiAccessStatus::Allowed) { Log(L"wifitest: Windows did not allow network access"); return 2; }
        auto ad = Adapter(); if (!ad) { Log(L"wifitest: no Wi-Fi adapter"); return 2; }
        Log(L"wifitest: connected to %s before: %d", ssid.c_str(), (int)ConnectedTo(ssid));
        if (!keep) { ad.Disconnect(); Sleep(1500); Log(L"wifitest: dropped, connected now: %d", (int)ConnectedTo(ssid)); }   // --keep: no drop, only the connect call and what follows it
        uint64_t t0 = Now(); ws err = ConnectBlocking(ssid, L"", L"", false);
        Log(L"wifitest: result \"%s\" after %d ms, connected now: %d", err.c_str(), (int)(Now() - t0), (int)ConnectedTo(ssid)); return err.empty() ? 0 : 1;
    } catch (hresult_error const& e) { Log(L"wifitest: %s", e.message().c_str()); return 3; }
}
// "DeskBar.exe --wifilist": what the Wi-Fi page would show, into the log (changes nothing)
int WifiDump() {
    init_apartment(apartment_type::multi_threaded); bool done = false; std::mutex m; std::condition_variable cv; uint64_t t0 = Now();
    ws cur; bool native = WlanCurrentSsid(cur); std::set<ws> saved; WlanSaved(saved);
    Log(L"wifilist: native engine %s, on \"%s\", %d saved network(s), sign-in page check %d", native ? L"ok" : L"not available", cur.c_str(), (int)saved.size(), PortalProbe());
    std::thread([&] {
        try { auto ad = Adapter(); if (!ad) { Log(L"wifilist: no adapter"); } else for (auto n : ad.NetworkReport().AvailableNetworks()) { WifiNet w = ToNet(n, cur); if (!w.ssid.empty()) Log(L"wifilist: %s%s%s · %s · %s · %d%%", w.ssid.c_str(), w.connected ? L" [connected]" : L"", saved.count(w.ssid) ? L" [saved]" : L"", w.band.c_str(), w.security.c_str(), w.pct); } }
        catch (hresult_error const& e) { Log(L"wifilist: %s", e.message().c_str()); }
        { std::lock_guard<std::mutex> l(m); done = true; } cv.notify_all();
    }).join();
    Log(L"wifilist: done in %d ms", (int)(Now() - t0)); Sleep(500); return 0;
}
void WifiDisconnect() { Post([] { try { if (auto ad = Adapter()) ad.Disconnect(); } catch (...) {} RefreshNow(); }); }
void WifiForget(const ws& ssid, std::function<void()> cb) {   // drops the saved password; if we are on that network, Windows leaves it
    std::thread([=] { WlanForget(ssid); { std::lock_guard<std::mutex> l(g_mu); for (auto& n : g_wifiLast) if (n.ssid == ssid) { n.saved = false; n.connected = false; } std::sort(g_wifiLast.begin(), g_wifiLast.end(), WifiBefore); } QueueRefresh(); UiPost(cb); }).detach();
}
std::vector<BtDev> BtCached() { std::lock_guard<std::mutex> l(g_mu); return g_btLast; }
void BtDevices(std::function<void(std::vector<BtDev>)> cb) { Post([cb] { BtRefreshList(); std::vector<BtDev> v; { std::lock_guard<std::mutex> l(g_mu); v = g_btLast; } UiPost([cb, v] { cb(v); }); }); }

// Bluetooth devices nearby that are NOT paired yet: Windows' own discovery (classic and LE) for about ten seconds. The watchers keep their state in a
// shared block: an event that arrives just after Stop must not touch a finished call's locals.
// Forgets a paired device (both halves, for earbuds that pair twice under one name). ok = Windows unpaired at least one.
void BtForget(const ws& name, std::function<void(bool)> cb) {
    std::thread([name, cb] {
        winrt::init_apartment(); bool ok = false;
        try {
            using namespace winrt::Windows::Devices::Enumeration;
            for (auto& sel : {winrt::Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true), winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)})
                for (auto info : DeviceInformation::FindAllAsync(sel, nullptr, DeviceInformationKind::AssociationEndpoint).get()) {
                    if (ws(info.Name().c_str()) != name) continue; auto p = info.Pairing(); if (!p) continue;
                    auto s = p.UnpairAsync().get().Status(); Log(L"bt forget %s: status %d", name.c_str(), (int)s); if (s == DeviceUnpairingResultStatus::Unpaired || s == DeviceUnpairingResultStatus::AlreadyUnpaired) ok = true;
                }
        } catch (hresult_error const& e) { Log(L"bt forget: %s", e.message().c_str()); }
        UiPost([cb, ok] { cb(ok); });
    }).detach();
}
void BtFindNew(std::function<void(std::vector<BtDev>, bool)> cb) {
    std::thread([cb] {
        winrt::init_apartment();
        struct St { std::mutex m; std::vector<BtDev> found; int seen = 0; }; auto st = std::make_shared<St>();
        using namespace winrt::Windows::Devices::Enumeration;
        std::vector<DeviceWatcher> watchers;
        try {
            hstring sels[2] = {winrt::Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(false), winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(false)};
            for (auto& sel : sels) {
                auto w = DeviceInformation::CreateWatcher(sel, nullptr, DeviceInformationKind::AssociationEndpoint);
                w.Added([st, cb](auto&&, DeviceInformation const& i) {
                    { std::lock_guard<std::mutex> l(st->m); st->seen++; }
                    ws n = i.Name().c_str(); if (Trim(n).empty()) return;   // nameless LE beacons: nothing a person could pick
                    std::vector<BtDev> v; { std::lock_guard<std::mutex> l(st->m); if (std::any_of(st->found.begin(), st->found.end(), [&](const BtDev& d) { return d.name == n; })) return; st->found.push_back({n, i.Id().c_str(), false}); v = st->found; }
                    std::sort(v.begin(), v.end(), [](const BtDev& a, const BtDev& b) { return a.name < b.name; }); UiPost([cb, v] { cb(v, false); });   // each one shows as it is found, not after the ten seconds
                });
                w.Updated([](auto&&, auto&&) {});   // an association-endpoint watcher stalls unless Updated has a handler
                w.Start(); watchers.push_back(w);
            }
            Sleep(10000);
            for (auto& w : watchers) try { w.Stop(); } catch (...) {}
        } catch (hresult_error const& e) { Log(L"bt search: %s", e.message().c_str()); }
        std::vector<BtDev> v; { std::lock_guard<std::mutex> l(st->m); v = st->found; }
        std::sort(v.begin(), v.end(), [](const BtDev& a, const BtDev& b) { return a.name < b.name; });
        Log(L"bt search: %d new device(s) with a name, %d seen in all", (int)v.size(), st->seen); UiPost([cb, v] { cb(v, true); });
    }).detach();
}
// Pairs one of them. Windows shows its own confirmation or PIN prompt when the device asks for one. 0 = paired, 1 = cancelled or refused, 2 = failed.
void BtPair(const ws& id, std::function<void(int)> cb) {
    std::thread([id, cb] {
        winrt::init_apartment(); int r = 2;
        try {
            using namespace winrt::Windows::Devices::Enumeration;
            auto di = DeviceInformation::CreateFromIdAsync(id).get();
            auto res = di.Pairing().PairAsync().get(); auto s = res.Status();
            r = s == DevicePairingResultStatus::Paired || s == DevicePairingResultStatus::AlreadyPaired ? 0 : s == DevicePairingResultStatus::PairingCanceled || s == DevicePairingResultStatus::RejectedByHandler ? 1 : 2;
            Log(L"bt pair: status %d", (int)s);
        } catch (hresult_error const& e) { Log(L"bt pair: %s", e.message().c_str()); }
        UiPost([cb, r] { cb(r); });
    }).detach();
}
