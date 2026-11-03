## Desk 1.2

**DeskBar** now starts with the pins you already have, and Start fits on every screen.

- **Your own pins.** On first run DeskBar copies the apps pinned to your Windows taskbar, Store apps included, in the same order. Start's pinned apps come from your Windows Start menu too (Windows 11; on Windows 10 you get a standard set). If you already used 1.0 or 1.1 and never changed Start's pins, they are replaced with yours; pins you did change stay as they are, and a DeskBar that already has taskbar pins keeps them.
- **Start fits.** On a 1080p screen at 125% or 150% scaling the bottom bar with your profile, Settings and the power button was pushed off the screen. The scrolling middle of Start now shrinks to fit, so the bar is always there.
- A taskbar button for an administrator window, such as a waiting permission prompt, brings it up.

**DeskDrawer** and **DeskTune** are the same as in 1.1 (built for .NET 10).

**The website** shows the apps one at a time as you scroll, and its taskbar picture matches the real one: pinned apps on the left, now playing, tray and clock on the right.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. Run the `-Setup.exe` over your old copy: settings and DeskTune's undo log are kept. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### The three apps

- **DeskBar:** taskbar, Start with search over apps, settings and files, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, ten colour themes.
- **DeskDrawer:** tabs, breadcrumb path bar, search through subfolders, details/list/tiles/icon views with thumbnails, grouping, folder sizes, Home, Quick access and This PC pages, undo, ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
