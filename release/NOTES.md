## Desk 2.6

DeskMode moves into DeskBar, every settings window is redone, and a long list of fixes lands in all three apps.

**DeskMode is part of DeskBar now**

- A DeskMode button sits on the taskbar next to the keyboard language (DeskMode's own icon). It opens DeskMode's panel, in DeskMode's own dark look: each screen's refresh rate, Go max / *monitor* first / Go easy, the power mode, Energy saver, brightness per screen and for both together.
- Docking still works by itself: monitor and charger connected → Go max; either unplugged → back to how it was; hands off while a full-screen game is up. Switch any of it off in DeskBar's settings, DeskMode page.
- Shortcuts: Ctrl+Alt+D opens it, Ctrl+Alt+M Go max, Ctrl+Alt+B Go easy, Ctrl+Alt+F borderless full screen for the window in front.
- **If you have the separate DeskMode app:** install DeskBar 2.6, then Start menu → Uninstall DeskMode. While the old app runs, DeskBar leaves the screens to it. There is no DeskMode-Setup.exe in this release.

**New settings windows in DeskBar, DeskDrawer and DeskTune**

- One design for all three: pages on the left, short cards of switches on the right, one plain line under each switch saying what it does. A tap anywhere on a row flips it; changes apply at once.
- DeskBar: Taskbar, Screens and windows (edge resistance is now a None / Light / Medium / Strong strip), DeskMode, Start-up and updates.
- DeskDrawer (⋯ > Settings, Ctrl+,): Folders and files, Speed, Updates.
- DeskTune gets a settings window (Settings at the bottom, Ctrl+,): Safety net (put everything back, the change log, unlock services) and Updates.

**DeskBar fixes**

- Starts at sign-in again (a scheduled task, repaired by itself if it goes missing).
- File Explorer launched from DeskBar (Start, pins, "Open file location") opens in DeskDrawer when DeskDrawer handles your folders, instead of a second window.
- Wi-Fi: Connect starts at once and keeps going when you close the panel; the search-again button shows that it is searching.
- Sound outputs: switching is checked and retried; two outputs with the same name (USB and Bluetooth of one headset) show their full names.
- Popups follow the window that opened them in more cases; Quick Settings lost its page dots.

**DeskDrawer fixes**

- The Open/Save picker for other apps is a real DeskDrawer window now, with a search box (Ctrl+F), and it also answers folder pickers. It opens faster, Cancel no longer hides the app that asked, and the app that asked comes back to the front.
- Double-click opens files and folders again everywhere.
- Setup no longer fails with "the file is in use" when DeskDrawer is running in the background.

**DeskTune**

- New switches for Gaming Services, Windows Search indexing and Surface's machine-learning service (off by default, in no preset).

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
