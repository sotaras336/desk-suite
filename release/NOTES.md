## Desk 1.3

**DeskTune and DeskDrawer now fit small screens.** On a 1080p screen at 125% or 150% scaling, Windows leaves a window only about 1280 × 670 units, and both apps opened larger than that.

- **DeskTune:** the title, the three stat cards and the preset and section buttons used to stay fixed above the list, so only two or three switches fit. They now scroll away with the list. On a short window the stat cards fold into one line under the title, and the Apply bar always stays visible. The window also opens no larger than the screen.
- **DeskDrawer:** opens no larger than the screen, so its status bar is no longer cut off.

**DeskBar** is unchanged from 1.2.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. Run the `-Setup.exe` over your old copy: settings and DeskTune's undo log are kept. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### The three apps

- **DeskBar:** taskbar, Start with search over apps, settings and files, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, ten colour themes. On first run it copies your own taskbar and Start pins.
- **DeskDrawer:** tabs, breadcrumb path bar, search through subfolders, details/list/tiles/icon views with thumbnails, grouping, folder sizes, Home, Quick access and This PC pages, undo, ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
