## Desk 1.5

**DeskBar**

- **Start search finds any file.** DeskBar's file list now covers everything on your drives: Windows, Program Files, AppData, hidden files, USB sticks. It is read straight from a file on disk, so it is ready the moment DeskBar starts and takes almost no memory (2.5 million files: about 0.15 s per search). Accents and capitals don't matter (`εργασια` finds `Εργασία`), several words must all match (`report 2024` finds `report.docx` inside a folder called `2024`), and your own folders rank above Windows and AppData. The first scan after updating takes about five minutes in the background; until it finishes, file search stays empty.
- **Wi-Fi: connecting looks right.** Pressing Connect turns the page into one card: the Wi-Fi mark with rings spreading out, the network's name and a running light. It turns green with a check when you're on, goes straight back to the form with the password box open (and what you typed still there) if the password was wrong, and turns red with *Try again* / *Back* for any other failure.
- **Wi-Fi: fewer false failures.** Connecting looks the network up fresh, retries once, and asks Windows before it reports a failure, so "it aborted, then connected a few seconds later" no longer shows up as an error. Every attempt is logged in `deskbar3.log`.
- **Touch works.** DeskBar only ever recognised a pen as touch input, so a finger behaved like a mouse. Now you can swipe to scroll, long-press for menus, tap for previews and swipe up on the bar for Start and Quick Settings.

**DeskDrawer**

- **Delete works** (it silently did nothing before). It now selects the next item, says "Moved to the Recycle Bin", and **Ctrl+Z puts it back**. When a copy, move or delete fails, the status bar says so.
- **Drag-select box.** Drag on empty space, or on the blank part of a row, and everything the box touches is selected. Ctrl or Shift adds to the selection, and the list scrolls on its own near the edges.
- **Click a selected name to rename it**, like File Explorer.
- **Search rebuilt.** Results stream in while it looks, best match first, under a banner that says what is being searched and how far it has got (with Stop). Several words, no matter the case or accents, hidden files included. From Home or This PC it searches the whole PC, instantly when DeskBar's file list is there; *Search live* walks the disks instead.
- Small things: the mouse's back and forward buttons, F4 / Alt+D for the address bar, Ctrl+E / F3 for search, Ctrl+Shift+C to copy the path, Esc leaves search results, "Open file location" on a result, and "Copied / Moved N items" messages.

**DeskTune** is the same as in 1.4.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. Run the `-Setup.exe` over your old copy: settings and DeskTune's undo log are kept. If DeskDrawer is open when you update and the old window is still there afterwards, run the setup once more. The files aren't code-signed, so Windows SmartScreen asks first: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.

### The three apps

- **DeskBar:** taskbar, Start with search over apps, settings and every file on the PC, Quick Settings (Wi-Fi, Bluetooth with one-tap headphone connect, sound, brightness, night light, energy saver), window previews, show desktop, touch support, ten colour themes. On first run it copies your own taskbar and Start pins.
- **DeskDrawer:** tabs, breadcrumb path bar, search that streams results and can look through the whole PC, details/list/tiles/icon views with thumbnails, grouping, folder sizes, a drag-select box, Home, Quick access and This PC pages, undo (including Delete), ZIP, a "folders" button inside other apps' Save/Open dialogs, optional "open folders and Win+E here", and it stays ready in the background so folders open in about half a second (switch off in the ⋯ menu).
- **DeskTune:** startup apps, Windows extras, ads and suggestions, background services and preinstalled apps, each with live memory use and what you'd lose. Tick, then apply; every change can be undone.
