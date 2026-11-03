#include "pch.h"
#include "app.h"
#include <unknwn.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <condition_variable>

using namespace winrt;
using namespace winrt::Windows::Media::Control;

// Now playing from any app that reports to Windows' media controls. One MTA thread, blocking calls.
namespace {
using Mgr = GlobalSystemMediaTransportControlsSessionManager; using Sess = GlobalSystemMediaTransportControlsSession;
std::mutex g_mu; MediaState g_media; std::atomic<bool> g_dirty{true}; std::condition_variable g_cv; std::mutex g_wm;
Mgr g_mgr{nullptr}; Sess g_session{nullptr};
std::vector<std::pair<Sess, std::pair<event_token, event_token>>> g_hooked;

bool Playing(Sess const& s) { try { return s.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing; } catch (...) { return false; } }
void Dirty() { g_dirty = true; g_cv.notify_one(); }

void Hook() {
    for (auto& h : g_hooked) { try { h.first.PlaybackInfoChanged(h.second.first); h.first.MediaPropertiesChanged(h.second.second); } catch (...) {} }
    g_hooked.clear();
    std::vector<Sess> all; for (auto s : g_mgr.GetSessions()) all.push_back(s);
    for (auto& s : all) { try { g_hooked.push_back({s, {s.PlaybackInfoChanged([](auto&&, auto&&) { Dirty(); }), s.MediaPropertiesChanged([](auto&&, auto&&) { Dirty(); })}}); } catch (...) {} }
    // show the one that's actually playing (Windows' "current" can be a paused one)
    Sess pick{nullptr}; for (auto& s : all) if (Playing(s)) { pick = s; break; }
    if (!pick) pick = g_mgr.GetCurrentSession();
    std::lock_guard<std::mutex> l(g_mu); g_session = pick;   // MediaCmd reads it from another thread
}
ImgP Decode(winrt::Windows::Storage::Streams::IRandomAccessStreamReference const& ref) {
    using namespace winrt::Windows::Graphics::Imaging;
    auto stream = ref.OpenReadAsync().get(); auto dec = BitmapDecoder::CreateAsync(stream).get();
    BitmapTransform tf; tf.ScaledWidth(96); tf.ScaledHeight(96);
    auto pd = dec.GetPixelDataAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied, tf, ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage).get(); auto data = pd.DetachPixelData();
    auto im = ImgMake(96, 96); if (data.size() >= 96 * 96 * 4) memcpy(im->px.data(), data.data(), 96 * 96 * 4); return im;
}
void Update() {
    MediaState m;
    try {
        Hook(); auto s = g_session;
        if (s) {
            auto info = s.GetPlaybackInfo(); auto st = info.PlaybackStatus();
            m.playing = st == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing; m.source = s.SourceAppUserModelId().c_str();
            auto props = s.TryGetMediaPropertiesAsync().get();
            m.title = props.Title().c_str(); m.artist = props.Artist().c_str();
            m.active = !m.title.empty() && st != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed;
            static ws lastKey; static ImgP lastArt; ws key = m.source + L"|" + m.title + L"|" + m.artist;   // the same track keeps its decoded art (the bar re-lays-out when the image changes)
            if (key == lastKey && lastArt) m.art = lastArt;
            else if (m.active && props.Thumbnail()) { try { m.art = Decode(props.Thumbnail()); } catch (...) {} }
            lastKey = key; lastArt = m.art;
        }
    } catch (hresult_error const& e) { Log(L"media: %s", e.message().c_str()); }
    { std::lock_guard<std::mutex> l(g_mu); g_media = m; } SysNotify();
}
void Loop() {
    init_apartment(apartment_type::multi_threaded);
    try { g_mgr = Mgr::RequestAsync().get(); g_mgr.CurrentSessionChanged([](auto&&, auto&&) { Dirty(); }); g_mgr.SessionsChanged([](auto&&, auto&&) { Dirty(); }); }
    catch (hresult_error const& e) { Log(L"media start: %s", e.message().c_str()); return; }
    for (;;) {
        { std::unique_lock<std::mutex> l(g_wm); g_cv.wait_for(l, std::chrono::seconds(5), [] { return g_dirty.load(); }); }
        g_dirty = false; Update();
    }
}
}
MediaState Media() { std::lock_guard<std::mutex> l(g_mu); return g_media; }
void MediaStart() { std::thread(Loop).detach(); }
void MediaCmd(int cmd) {
    std::thread([cmd] {
        init_apartment(apartment_type::multi_threaded);
        try { Sess s{nullptr}; { std::lock_guard<std::mutex> l(g_mu); s = g_session; } if (!s) return; if (cmd == 0) s.TryTogglePlayPauseAsync().get(); else if (cmd == 1) s.TrySkipNextAsync().get(); else s.TrySkipPreviousAsync().get(); } catch (...) {}
    }).detach();
}
void MediaOpenSource() {
    MediaState m = Media(); if (m.source.empty()) return;
    auto snap = WinSnapshot();
    for (auto& w : snap->list) if (EqI(w.aumid, m.source) || EqI(FileNameOf(w.exe), m.source) || EqI(StemOf(w.exe) + L".exe", m.source)) { ForceForeground(w.h); return; }
    Launch(m.source.find(L'!') != ws::npos ? L"shell:AppsFolder\\" + m.source : m.source);
}
