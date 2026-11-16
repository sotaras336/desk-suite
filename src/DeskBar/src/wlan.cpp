#include "pch.h"
#include "app.h"
#include <wlanapi.h>
#include <winhttp.h>
#include <condition_variable>

// Windows' own Wi-Fi engine (the one its network list uses), for what the WinRT Wi-Fi classes do slowly or not at all:
//  - which networks are saved, and forgetting one
//  - joining a SAVED network by its profile: Windows finds the access point itself (no scan report that can be stale) and says why it failed
//  - who we are connected to, at once, and a call-back the moment that changes
// Joining a network for the first time (a new password) stays with WinRT in netsvc.cpp, which writes the profile.
// Also here: the check for a sign-in page ("captive portal").
namespace {
std::mutex g_mu; HANDLE g_h = nullptr; GUID g_if{}; bool g_tried = false; std::function<void()> g_onChange;
struct { std::mutex m; std::condition_variable cv; bool active = false, done = false; DWORD reason = 0, lastFail = 0; ws profile; } g_wait;

void WINAPI OnNote(PWLAN_NOTIFICATION_DATA d, PVOID) {
    if (!d || d->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) return;
    bool change = false;
    switch (d->NotificationCode) {
    case wlan_notification_acm_connection_attempt_fail: case wlan_notification_acm_connection_complete: {
        if (d->dwDataSize < sizeof(WLAN_CONNECTION_NOTIFICATION_DATA) || !d->pData) break;
        auto* c = (WLAN_CONNECTION_NOTIFICATION_DATA*)d->pData; bool complete = d->NotificationCode == wlan_notification_acm_connection_complete;
        { std::lock_guard<std::mutex> l(g_wait.m);
          if (g_wait.active && g_wait.profile == c->strProfileName) {
              if (!complete) g_wait.lastFail = c->wlanReasonCode;   // one access point said no; the final word comes with "complete"
              else { g_wait.done = true; g_wait.reason = c->wlanReasonCode; }
          } }
        if (complete) { g_wait.cv.notify_all(); change = true; }
        break; }
    case wlan_notification_acm_disconnected: case wlan_notification_acm_profile_change: case wlan_notification_acm_interface_arrival: case wlan_notification_acm_interface_removal: change = true; break;
    }
    if (change) { std::function<void()> f; { std::lock_guard<std::mutex> l(g_mu); f = g_onChange; } if (f) f(); }
}
bool Ensure() {   // g_mu held
    if (g_h) return true; if (g_tried) return false; g_tried = true;
    DWORD ver = 0; HANDLE h = nullptr; if (WlanOpenHandle(2, nullptr, &ver, &h) != ERROR_SUCCESS) return false;
    PWLAN_INTERFACE_INFO_LIST list = nullptr; bool ok = false;
    if (WlanEnumInterfaces(h, nullptr, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems && !ok; i++) if (list->InterfaceInfo[i].isState == wlan_interface_state_connected) { g_if = list->InterfaceInfo[i].InterfaceGuid; ok = true; }
        if (!ok && list->dwNumberOfItems) { g_if = list->InterfaceInfo[0].InterfaceGuid; ok = true; }
        WlanFreeMemory(list);
    }
    if (!ok) { WlanCloseHandle(h, nullptr); return false; }
    WlanRegisterNotification(h, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, OnNote, nullptr, nullptr, nullptr);
    g_h = h; return true;
}
bool Handle(HANDLE& h, GUID& id) { std::lock_guard<std::mutex> l(g_mu); if (!Ensure()) return false; h = g_h; id = g_if; return true; }
}

void WlanOnChange(std::function<void()> f) { std::lock_guard<std::mutex> l(g_mu); g_onChange = std::move(f); Ensure(); }
bool WlanSaved(std::set<ws>& names) {
    HANDLE h; GUID id; if (!Handle(h, id)) return false;
    PWLAN_PROFILE_INFO_LIST list = nullptr; if (WlanGetProfileList(h, &id, nullptr, &list) != ERROR_SUCCESS || !list) return false;
    for (DWORD i = 0; i < list->dwNumberOfItems; i++) names.insert(list->ProfileInfo[i].strProfileName);
    WlanFreeMemory(list); return true;
}
bool WlanCurrentSsid(ws& ssid) {   // false = can't tell (no native Wi-Fi); true with "" = not connected
    HANDLE h; GUID id; ssid.clear(); if (!Handle(h, id)) return false;
    DWORD sz = 0; PWLAN_CONNECTION_ATTRIBUTES a = nullptr;
    DWORD r = WlanQueryInterface(h, &id, wlan_intf_opcode_current_connection, nullptr, &sz, (PVOID*)&a, nullptr);
    if (r != ERROR_SUCCESS || !a) return r == ERROR_INVALID_STATE || r == ERROR_NOT_FOUND;   // "not connected" is an answer
    if (a->isState == wlan_interface_state_connected) { auto& s = a->wlanAssociationAttributes.dot11Ssid; ssid = Widen(std::string((const char*)s.ucSSID, std::min<ULONG>(s.uSSIDLength, 32))); }
    WlanFreeMemory(a); return true;
}
bool WlanForget(const ws& ssid) { HANDLE h; GUID id; if (!Handle(h, id)) return false; DWORD r = WlanDeleteProfile(h, &id, ssid.c_str(), nullptr); Log(L"wifi forget %s: %lu", ssid.c_str(), r); return r == ERROR_SUCCESS; }

// "" = joined, "password" = Windows could not sign in with what is saved, "none" = no saved profile (or no native Wi-Fi): use the other way, else a message.
ws WlanReasonKind(DWORD reason) {
    if (reason == WLAN_REASON_CODE_SUCCESS) return L"";
    if (reason >= WLAN_REASON_CODE_MSMSEC_BASE && reason <= WLAN_REASON_CODE_MSMSEC_END) return L"password";   // every security failure: wrong key, 802.1X sign-in refused, key exchange timed out
    if (reason == WLAN_REASON_CODE_NETWORK_NOT_AVAILABLE || reason == WLAN_REASON_CODE_NO_VISIBLE_AP) return L"Network not available";
    return L"Couldn't connect";
}
ws WlanConnectSaved(const ws& ssid, int timeoutMs) {
    HANDLE h; GUID id; if (!Handle(h, id)) return L"none";
    { std::set<ws> saved; if (!WlanSaved(saved) || !saved.count(ssid)) return L"none"; }
    { std::lock_guard<std::mutex> l(g_wait.m); g_wait.active = true; g_wait.done = false; g_wait.reason = g_wait.lastFail = 0; g_wait.profile = ssid; }
    WLAN_CONNECTION_PARAMETERS cp{}; cp.wlanConnectionMode = wlan_connection_mode_profile; cp.strProfile = ssid.c_str(); cp.dot11BssType = dot11_BSS_type_any;
    uint64_t t0 = Now(); DWORD r = WlanConnect(h, &id, &cp, nullptr); ws out;
    if (r != ERROR_SUCCESS) { Log(L"wifi connect %s (saved): WlanConnect error %lu", ssid.c_str(), r); out = r == ERROR_NOT_FOUND ? L"none" : L"Couldn't connect"; }
    else {
        bool done = false; DWORD reason = 0, lastFail = 0;
        {   std::unique_lock<std::mutex> l(g_wait.m);
            // Joining the network we are already on can finish without any notice: look at the adapter now and then while waiting
            for (int waited = 0; waited < timeoutMs && !g_wait.done; waited += 400) {
                if (g_wait.cv.wait_for(l, std::chrono::milliseconds(400), [] { return g_wait.done; })) break;
                if (waited >= 1200) { l.unlock(); ws cur; bool on = WlanCurrentSsid(cur) && cur == ssid; l.lock(); if (on && !g_wait.lastFail) { g_wait.done = true; g_wait.reason = WLAN_REASON_CODE_SUCCESS; } }
            }
            done = g_wait.done; reason = g_wait.reason; lastFail = g_wait.lastFail; }
        if (!done) { ws cur; out = WlanCurrentSsid(cur) && cur == ssid ? L"" : L"Timed out"; }
        else { out = WlanReasonKind(reason); if (out == L"Couldn't connect" && lastFail) out = WlanReasonKind(lastFail); }
        wchar_t why[256] = L""; if (reason) WlanReasonCodeToString(reason, 256, why, nullptr);
        Log(L"wifi connect %s (saved): %s after %d ms%s%s", ssid.c_str(), out.empty() ? L"Success" : out.c_str(), (int)(Now() - t0), why[0] ? L" · " : L"", why);
    }
    { std::lock_guard<std::mutex> l(g_wait.m); g_wait.active = false; }
    return out;
}

// Is there a sign-in page between us and the internet? Asks for the small test file Windows itself uses, without following redirects:
// the real file = online (0); a redirect or any other answer = a sign-in page is in the way (1); no answer = can't tell (-1).
int PortalProbe() {
    int out = -1;
    HINTERNET s = WinHttpOpen(L"Microsoft NCSI", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0); if (!s) return -1;
    WinHttpSetTimeouts(s, 3000, 3000, 3000, 3000);
    HINTERNET c = WinHttpConnect(s, L"www.msftconnecttest.com", 80, 0), r = nullptr;
    if (c) r = WinHttpOpenRequest(c, L"GET", L"/connecttest.txt", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (r) {
        DWORD off = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES; WinHttpSetOption(r, WINHTTP_OPTION_DISABLE_FEATURE, &off, sizeof off);
        if (WinHttpSendRequest(r, L"Cache-Control: no-cache\r\n", (DWORD)-1L, nullptr, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
            DWORD code = 0, sz = sizeof code; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
            if (code == 200) { char b[64] = {}; DWORD n = 0; WinHttpReadData(r, b, 22, &n); out = PortalVerdict(code, std::string(b, n)); }
            else out = PortalVerdict(code, {});
        }
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s); return out;
}
int PortalVerdict(int httpStatus, const std::string& bodyStart) { return httpStatus == 200 && bodyStart.rfind("Microsoft Connect Test", 0) == 0 ? 0 : 1; }
