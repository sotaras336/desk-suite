## Desk 1.0

First public release of the three apps.

**DeskBar**: taskbar, Start with search over apps, settings and files, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, ten colour themes. The file index for Start search now uses about half the memory it did.

**DeskDrawer**: tabs, breadcrumb path bar, search through subfolders, details/list/tiles/icon views with thumbnails, grouping, folder sizes, Home, Quick access and This PC pages, undo, ZIP, a "folders" button inside other apps' Save/Open dialogs, and optional "open folders and Win+E here". It stays ready in the background like File Explorer (starts hidden at sign-in, about 20–30 MB while waiting), so folders open in about half a second; switch off in the ⋯ menu.

**DeskTune**: startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.

Install: run the `-Setup.exe` file. DeskDrawer and DeskTune need the .NET 8 Desktop Runtime (x64). The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
