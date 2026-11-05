#pragma once
// Cross-module declarations for DeskBar3. Everything here runs in the taskbar process unless noted.
#include "ui.h"

// ---------- process-wide names (distinct from DeskBar2's "DeskBar3.*" so both can coexist while testing) ----------
#define N_INSTANCE L"DB3.Instance"
#define N_HOOKS L"DB3.Hooks"
#define N_WINKEY L"DB3.WinKey"
#define N_READY L"DB3.Ready"
#define N_TYPEAHEAD L"DB3.TypeAhead"
#define N_TYPEAHEAD_LOCK L"DB3.TypeAheadLock"
#ifdef PINK
#define N_TASK L"DeskBar3Pink"
#else
#define N_TASK L"DeskBar3"
#endif

// Calls into undocumented Windows interfaces (hardcoded vtable slots) go through this: if a Windows update moves a slot the call fails with
// E_FAIL instead of an access violation that takes the whole taskbar down. (No objects with destructors allowed in here: __try.)
template <class F> HRESULT SehCall(F&& f) { __try { return f(); } __except (EXCEPTION_EXECUTE_HANDLER) { return E_FAIL; } }

extern bool g_dev;                       // dev copy: bars at the top, nothing hidden, no input hooks
extern HINSTANCE g_inst;

// ---------- monitors / shell ----------
struct Mon { HMONITOR h; RECT bounds, work; float scale; ws device; bool primary; };
std::vector<Mon> Monitors();             // ordered left to right
ws MonSignature();
bool IsFullscreenOn(const RECT& monBounds, ws* who = nullptr, bool barHidden = false);   // who: class, title and exe of the window that covers the screen
HWND MainTray();                         // Windows' own Shell_TrayWnd (never ours)
namespace Explorer { void Hide(); void KeepHidden(); void Restore(); }
namespace Dock { void Register(HWND h, const RECT& mon, int hpx); void Reserve(HWND h, const RECT& mon, int hpx); void Remove(HWND h); }
void Launch(const ws& target, const ws& args = L"", bool admin = false, const ws& dir = L"");   // async ShellExecute
void OpenProps(const ws& path);
void ToggleDesktop();                                                                               // Show desktop / bring the windows back, like Win+D
void SendKeys(std::initializer_list<WORD> vks);                                                    // press in order, release in reverse
void ForceForeground(HWND h);            // SetForegroundWindow with the F24 trick; never AttachThreadInput
ws ClassOf(HWND h);
ws ExePathOfPid(DWORD pid);

// ---------- top-level windows (scanned on a helper thread) ----------
struct WinInfo { HWND h = nullptr; DWORD pid = 0; ws title, exe, aumid, family; bool minimized = false, flashing = false; };
struct WinSnap { std::vector<WinInfo> list; HWND fg = nullptr; };       // list is top of z-order first
void WinTrackerStart();
std::shared_ptr<const WinSnap> WinSnapshot();
void WinTrackerOnChange(std::function<void()> f);                       // called on the UI thread
void WinTrackerPoke();                                                  // scan soon
void SetFlashing(HWND h, bool on);
bool IsFlashing(HWND h);
ws PackageFamilyFromPath(const ws& exe);
ws AumidOfShortcut(const ws& lnk);

// ---------- system state (each service runs on its own thread; the UI only reads snapshots) ----------
struct BattState { bool present = false, charging = false, ac = false, saver = false; int pct = 100; int secsLeft = -1; };
BattState Battery();
struct NetState {
    enum Kind { None, WiFi, Ether, Other } kind = None; int bars = 0; bool internet = false, airplane = false, wifiOn = false, btOn = false, btPresent = false, wifiPresent = false, needsSignIn = false;
    ws name, adapter, status = L"Not connected", ip, btSubtitle;
};
NetState NetSnap();
struct WifiNet { ws ssid, band, security; int bars = 0, pct = 0; bool secured = false, connected = false, enterprise = false; };
struct BtDev { ws name, id; bool connected = false; };
void NetStart();
void NetRefresh();
void NetSetWifi(bool on); void NetSetBt(bool on); void NetSetAirplane(bool on);
void NetHotspot(bool query, bool on, std::function<void(int state)> cb);          // state: -1 unknown, 0 off, 1 on
void WifiScan(bool fresh, std::function<void(bool access, std::vector<WifiNet>)> cb);
std::vector<WifiNet> WifiLast();
int WifiTest(const ws& ssid, bool keep);   // --wifitest <ssid> [--keep]: a drop and reconnect (or just the connect call), logged
void WifiConnect(const ws& ssid, const ws& pass, const ws& user, bool usePass, std::function<void(ws err)> cb);   // err: "" ok, "password", or a message
void WifiDisconnect();
void BtDevices(std::function<void(std::vector<BtDev>)> cb);
std::vector<BtDev> BtCached();
void BtConnectAudio(const ws& deviceId, bool connect, std::function<void(int)> cb);   // result 0 done, 1 not an audio device, 2 failed
void MediaStart();
void MediaOpenSource();
void MediaCmd(int cmd);                                                           // 0 play/pause, 1 next, 2 previous

// ---------- display / power toggles ----------
struct DispInfo { ws friendly; bool internal = false; };
DispInfo DisplayInfoFor(const ws& gdiDevice);
void BrightOpen(const Mon& m, std::function<void(bool avail, int value)> cb);     // opens the channel on a worker (DDC can take seconds)
void BrightSet(const ws& device, int pct);
int BrightRead(const ws& device);                                                 // latest known value, -1 if none
int NightLightIsOn();                                                             // -1 unknown
bool NightLightSet(bool on);
int RotationLocked();                                                             // -1 not available, 0 free, 1 locked
void RotationSetLocked(bool locked);
int EnergySaverIsOn();                                                            // -1 unknown
bool EnergySaverSet(bool on);
void ToggleTouchKeyboard();
struct VolState { bool ok = false, muted = false; float vol = 0; ws device; };
VolState Volume();
struct MediaState { bool active = false, playing = false; ws title, artist, source; ImgP art; };
MediaState Media();
// audio: every COM call happens on one MTA thread; results come back on the UI thread
struct AudioDev { ws id, name; bool def = false; float vol = -1; };
struct AppAudio { ws name, exe; DWORD pid = 0; float vol = 0; bool muted = false; ws routed; };
void AudioSetVolume(float v);
void AudioToggleMute();
void AudioDevices(std::function<void(std::vector<AudioDev>)> cb);
void AudioSetDefault(const ws& id, std::function<void()> done);
void AudioSetDeviceVolume(const ws& id, float v);
void AudioApps(std::function<void(std::vector<AppAudio>)> cb);
void AudioSetAppVolume(const ws& exe, DWORD pid, float v);
void AudioSetAppMute(const ws& exe, DWORD pid, bool mute);
void AudioRouteApp(const ws& exe, DWORD pid, const ws& deviceId);   // empty = follow the default again
ws AudioShortName(const ws& n);
void AudioStart();
void AudioOnDefaultChanged(std::function<void()> f);                 // called on the audio thread
void MirrorStart();                                                   // restore "also play" devices
void MirrorRemove(const ws& deviceId);
void AudioPost(std::function<void()> job);                           // run on the audio thread
void SysNotify();
void SysStart();                                 // starts battery / audio / network / media threads
void SysOnChange(std::function<void()> f);       // UI thread, coalesced
void SysPoke();                                  // something changed on the UI side (e.g. user toggled airplane): refresh now
ws KeyboardLabel(HWND fg);                       // "ENG", "ΕΛ", ... ("" if only one layout is installed)

// ---------- panels (flyouts) ----------
enum class Panel { None, Start, Quick, Clock };
void TogglePanel(Panel p, Wnd* from, const char* page = nullptr);
void ClosePanels();
Panel OpenPanel();
void PrewarmPanels();
void OpenSettingsWindow();

// ---------- hover previews ----------
struct PreviewReq { Wnd* wnd; View* anchor; ws key, name, iconKey; std::vector<WinInfo> wins; bool touch; };
void PreviewOpen(const PreviewReq& r);
void PreviewHideSoon();
void PreviewCancelHide();
void PreviewClose();
bool PreviewShowingFor(const ws& key);
void PreviewSnapshot(const ws& path);   // debug

// ---------- taskbar pins by app id (Start menu uses these) ----------
bool TaskbarHasApp(const ws& aumid);
void TaskbarPinApp(const ws& aumid);
void TaskbarUnpinApp(const ws& aumid);
// ---------- Win key type-ahead (shared memory filled by the input helper process) ----------
ws TypeAheadTake(bool& enter, bool& esc);
void TypeAheadEnd();

// ---------- app lifecycle ----------
void AppQuit(bool restart);                       // false: quit and give Windows its taskbar back
int RunInstall(); int RunUninstall();
void BarsWorkAreaUpkeep();

// ---------- input helper process ----------
void HooksEnsure(DWORD parentPid);
void HooksListen();
void WinMoveStart();
void WinMoveTick(int n);

// ---------- window helpers ----------
void SendToNextScreen(HWND h);

// ---------- icons ----------
// key forms: "exe:<path>", "aum:<AUMID>", "file:<path>" (any shell path).  Returns null until the image has arrived; onReady runs on the UI thread.
ImgP IconGet(const ws& key, int px);
void IconInit();
bool IconSettled(const ws& key, int px);   // arrived or failed for good (starts the fetch if neither)
ws ShellNameOf(const ws& key);          // display name for "aum:..." / "exe:..." keys (blocking: call from workers)

// ---------- pinned taskbar apps ----------
struct PinInfo {
    ws entry, name, exe, aumid, family, launch, iconKey;
    bool ok = false;
};
PinInfo ResolvePin(const ws& entry);     // uses pins.cache
std::vector<ws> WindowsTaskbarPins();     // first run: the pins Windows' own taskbar had, in its order (blocking: call from a worker)
void SavePinCache(const std::vector<PinInfo>& pins);
std::vector<PinInfo> LoadPinCache();
bool PinMatchesWindow(const PinInfo& p, const WinInfo& w);
ws WinGroupKey(const WinInfo& w);        // key for unpinned grouping
ws WinGroupName(const WinInfo& w);
ws WinIconKey(const WinInfo& w);
