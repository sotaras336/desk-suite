## Desk 1.4

**DeskBar: please update if you use 1.0, 1.1, 1.2 or 1.3.** Those versions have a crash.

- **The crash.** After the file-search list finished rescanning, DeskBar crashed. It then restarted, rescanned and crashed again, about every 2.5 minutes. Each crash briefly brought Windows' own taskbar back, so the screen flashed. It could also leave the bar half-drawn after starting. This is fixed, and the rescan now runs in its own low-priority process, so it can no longer slow the bar down or take it with it.
- **Icons after startup.** Icons already saved on disk now load first, instead of waiting behind slow Windows lookups, so the bar fills in sooner after you sign in.
- **Apps with several windows** now look like they do in Windows: one soft plate behind the icon when you hover over it, a second card peeking out at its right edge, and a single mark under the icon. DeskBar used to draw a plate inside its own hover plate.
- If DeskBar ever crashes, its log (`%LOCALAPPDATA%\DeskBar3\deskbar3.log`) now says where.

**DeskDrawer** and **DeskTune** are the same as in 1.3.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. Run the `-Setup.exe` over your old copy: settings and DeskTune's undo log are kept. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### The three apps

- **DeskBar:** taskbar, Start with search over apps, settings and files, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, ten colour themes. On first run it copies your own taskbar and Start pins.
- **DeskDrawer:** tabs, breadcrumb path bar, search through subfolders, details/list/tiles/icon views with thumbnails, grouping, folder sizes, Home, Quick access and This PC pages, undo, ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
