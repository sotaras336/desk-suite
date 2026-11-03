#include "pch.h"
#include "app.h"
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <bluetoothapis.h>

// Connects / disconnects paired Bluetooth audio devices the way the Windows flyout does: every Bluetooth audio endpoint sits on a
// kernel-streaming filter that understands the one-shot "reconnect / disconnect" property. Mice, keyboards and pens connect by themselves.
static const GUID kKsControl = {0x28F54685, 0x06FD, 0x11D2, {0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
static const GUID kBtAudioSet = {0x7FA06C40, 0xB8F6, 0x4C7E, {0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D}};
struct KSPROP { GUID Set; ULONG Id, Flags; };

static ws AddressFromId(const ws& id) {   // AEP ids end in "...-aa:bb:cc:dd:ee:ff"
    if (id.size() < 17) return {};
    ws hex; for (wchar_t c : id.substr(id.size() - 17)) if (c != L':') hex += (wchar_t)towupper(c);
    if (hex.size() != 12) return {}; for (wchar_t c : hex) if (!iswxdigit(c)) return {};
    return hex;
}
static int Send(const ws& addr, ULONG prop) {
    ComPtr<IMMDeviceEnumerator> en; if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return 2;
    ComPtr<IMMDeviceCollection> coll; if (FAILED(en->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &coll))) return 2;
    UINT n = 0; coll->GetCount(&n); std::set<ws> done; bool any = false, ok = false;
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> ep; if (FAILED(coll->Item(i, &ep))) continue;
        ComPtr<IDeviceTopology> topo; if (FAILED(ep->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, (void**)&topo))) continue;
        ComPtr<IConnector> conn; if (FAILED(topo->GetConnector(0, &conn))) continue;
        LPWSTR idp = nullptr; if (FAILED(conn->GetDeviceIdConnectedTo(&idp)) || !idp) continue;
        ws fid = idp; CoTaskMemFree(idp);
        if (!ContainsI(fid, addr) || !done.insert(Lower(fid)).second) continue;
        any = true;
        ComPtr<IMMDevice> filter; if (FAILED(en->GetDevice(fid.c_str(), &filter))) continue;
        void* ks = nullptr; if (FAILED(filter->Activate(kKsControl, CLSCTX_ALL, nullptr, &ks)) || !ks) continue;
        KSPROP p{kBtAudioSet, prop, 1 /*GET*/}; ULONG ret = 0; using F = HRESULT(STDMETHODCALLTYPE*)(void*, KSPROP*, ULONG, void*, ULONG, ULONG*);
        HRESULT hr = ((F)(*(void***)ks)[3])(ks, &p, sizeof p, nullptr, 0, &ret);
        if (hr >= 0) ok = true; else Log(L"BtAudio KsProperty 0x%08X", hr);
        ((IUnknown*)ks)->Release();
    }
    return !any ? 1 : ok ? 0 : 2;
}
// Second try when the audio endpoint isn't there (earbuds that are off or not yet "seen" by Windows' audio): ask the Bluetooth stack itself to
// switch the device's audio profiles (A2DP sink + hands-free) on or off, which makes Windows connect to it. Blocks while it tries.
static int ServiceToggle(const ws& hexAddr, bool connect) {
    ULONGLONG a = wcstoull(hexAddr.c_str(), nullptr, 16);
    BLUETOOTH_FIND_RADIO_PARAMS fp{sizeof fp}; HANDLE radio = nullptr; HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&fp, &radio);
    if (!find) return 2;
    static const GUID kSink = {0x0000110B, 0, 0x1000, {0x80, 0, 0, 0x80, 0x5F, 0x9B, 0x34, 0xFB}}, kHandsFree = {0x0000111E, 0, 0x1000, {0x80, 0, 0, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
    int result = 2;
    do {
        BLUETOOTH_DEVICE_INFO di{sizeof di}; di.Address.ullLong = a;
        if (BluetoothGetDeviceInfo(radio, &di) == ERROR_SUCCESS) {
            if (connect) { BluetoothSetServiceState(radio, &di, &kSink, BLUETOOTH_SERVICE_DISABLE); BluetoothSetServiceState(radio, &di, &kHandsFree, BLUETOOTH_SERVICE_DISABLE); }   // off then on: Windows opens a fresh link
            DWORD flag = connect ? BLUETOOTH_SERVICE_ENABLE : BLUETOOTH_SERVICE_DISABLE;
            DWORD r1 = BluetoothSetServiceState(radio, &di, &kSink, flag), r2 = BluetoothSetServiceState(radio, &di, &kHandsFree, flag);
            if (r1 == ERROR_SUCCESS || r2 == ERROR_SUCCESS) result = 0; else Log(L"BtAudio service toggle %lu / %lu", r1, r2);
        }
        CloseHandle(radio);
    } while (BluetoothFindNextRadio(find, &radio));
    BluetoothFindRadioClose(find); return result;
}
void BtConnectAudio(const ws& id, bool connect, std::function<void(int)> cb) {
    ws addr = AddressFromId(id);
    if (addr.empty()) { UiPost([cb] { cb(1); }); return; }
    std::thread([addr, connect, cb] { CoInitializeEx(nullptr, COINIT_MULTITHREADED); int r = Send(addr, connect ? 0 : 1); if (r != 0) r = ServiceToggle(addr, connect); UiPost([cb, r] { cb(r); }); }).detach();
}
