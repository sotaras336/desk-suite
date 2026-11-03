## Desk 1.1

DeskDrawer and DeskTune now run on **.NET 10**, because Microsoft stops supporting .NET 8 on 10 November 2026. Nothing else about them changed, and they're the same size (0.86 MB and 0.47 MB). DeskBar is unchanged from 1.0 and needs no extra runtime.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune. If it's missing, Windows offers to install it the first time you open them. If you already use 1.0, install the runtime first, then run the new setup file over your old copy: your settings and DeskTune's undo log are kept.

**Install:** run the `-Setup.exe` file. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### Also in this line (from 1.0)

- **DeskBar:** taskbar, Start with search over apps, settings and files, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, ten colour themes. Clicking a taskbar button for an administrator window, such as a waiting permission prompt, now brings it up.
- **DeskDrawer:** tabs, breadcrumb path bar, search through subfolders, details/list/tiles/icon views with thumbnails, grouping, folder sizes, Home, Quick access and This PC pages, undo, ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
