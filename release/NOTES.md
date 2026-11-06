## Desk 2.0

**DeskMode joins the suite.** Desk is now four apps.

**DeskMode (new)**

- **One panel for your screens and power:** refresh rate per screen, Windows power mode (Recommended / Better / Best, or Efficiency / Balanced / Best on PCs that use those names), Energy saver, and brightness per screen, the external monitor too (over DDC/CI), with a "both" slider that keeps the gap between them.
- **Go max / Go easy.** Two presets you define yourself. Plug in an external monitor and the charger and DeskMode switches to your "max" settings by itself; unplug and it puts everything back (switch either off).
- **Settings are a page of the panel itself** (gear, or Esc to go back): which monitor counts (any external one, or one you pick), what each preset sets, what happens on battery, hotkeys (Ctrl+Alt+D / M / B / F), notifications, start with Windows. Nothing needs editing in files.
- **For games:** leaves screen modes alone while a full-screen game is up, and can make the games on your list borderless so alt-tab never blanks the screen.
- Works with any monitor and any laptop or desktop; whatever your PC doesn't have (Energy saver, some power modes, a built-in screen) is hidden. Lives in the tray; settings in `%LOCALAPPDATA%\DeskMode`.

**DeskBar**

- **Scrolling is calmer.** A wheel notch now moves what Windows' "lines per notch" says (48 px at the default 3 lines; it was a fixed 80), and a touchpad swipe over the Start menu's pinned grid moves one page instead of flying through several. The same goes for cycling a multi-window taskbar button with the wheel.
- **Wi-Fi shows the network you really joined.** After a successful connect the page sometimes kept showing the previous network, which looked like Windows had switched back. It now marks the new network as connected right away, and a scan that Windows aborts is retried once.

**DeskTune**

- A flick on a touch screen no longer coasts across the whole list.

**DeskDrawer** is the same as in 1.5.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. Run the `-Setup.exe` over your old copy: settings and DeskTune's undo log are kept. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### The four apps

- **DeskBar:** taskbar, Start with search over apps, settings and every file on the PC, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, touch support, ten colour themes. On first run it copies your own taskbar and Start pins.
- **DeskDrawer:** tabs, breadcrumb path bar, search that streams results and can look through the whole PC, details/list/tiles/icon views with thumbnails, grouping, folder sizes, a drag-select box, Home, Quick access and This PC pages, undo (including Delete), ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
- **DeskMode:** refresh rate, power mode and brightness for your screens in one panel, and a docked setup that switches itself when you plug in.
