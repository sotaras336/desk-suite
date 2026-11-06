## Desk 2.0.1

A small update to 2.0: scrolling with a touchpad.

**DeskTune** and **DeskMode** (its Settings page) scrolled far too much with a precision touchpad. WPF moves a whole mouse notch for every wheel message, however small, and a touchpad sends dozens of tiny ones per swipe. Both now scroll by the real size of each message, so a swipe moves what Windows' "lines per notch" says, like any other app. A finger flick on a touch screen also coasts less. DeskMode's panel also hides the mouse cursor while you touch it and turns off Windows' press-and-hold ring.

DeskBar and DeskDrawer are the same as in 2.0.

If you already have 2.0, run the new `DeskTune-Setup.exe` and `DeskMode-Setup.exe` over it; settings and DeskTune's undo log are kept. Everything else about 2.0 (DeskMode joining the suite, calmer scrolling and the Wi-Fi fix in DeskBar) is in the [2.0 notes](https://github.com/sotaras336/desk-suite/releases/tag/v2.0).

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
