## Pre-release 4

An early look at Desk 4.0. Each app offers it as its next update; it may still have rough edges.

**One settings window for the whole suite**

- DeskBar's settings window is now the Desk settings window. Settings in DeskDrawer and DeskTune, the gear in Quick Settings, the gear in Start and the gear in DeskMode all open it, on the right page.
- New pages: Colours, DeskDrawer, DeskTune and Desk apps. DeskDrawer's switches change DeskDrawer straight away. DeskTune's page opens it, puts every change back, or opens it as administrator.
- Desk apps installs, updates and opens DeskDrawer and DeskTune, each download checked against this release's checksums.
- Windows' own Settings is a click away at the bottom of the window.

**One look, or a colour for each app**

- The glass theme is lighter in every app: mostly your blurred wallpaper, with a faint tint. DeskTune gets real glass too, and DeskMode no longer stays solid blue in Glass and Black.
- Settings > Colours: one colour for every app, or a different one for DeskBar, DeskMode, DeskDrawer and DeskTune.
- Each app has the full colour picker (any colour, how light, the fade): DeskBar in Quick Settings, DeskMode in its panel, DeskDrawer under View, DeskTune at the bottom of its window. With one colour for every app, a colour picked in any of them goes to all of them.

**DeskDrawer**

- Press Space on a file for a big look at it without opening it: pictures sharp, text files as text, everything else as Windows' preview. The arrow keys step to the next file, Enter opens it, Space or Esc closes.

**DeskBar**

- Optional: app icons swell when you point at them (Settings > Taskbar > Effects).
- Hovering a pinned app that isn't open shows its name without a box.
- Window previews and the full-size peek no longer cut a strip off the left of the window.

**Known in this pre-release**

- Tapping right above the bar next to a maximized app can still start a resize from the bottom edge. Being worked on.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
