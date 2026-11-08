## Desk 2.2.1

Same files as the corrected Desk 2.2 download, under their own version number so the source tag matches the files. If you installed Desk 2.2 before this, you may be offered one more update: press *Check for updates now*, then *Update now*.

A DeskBar update, plus one fix in the update check of all four apps.

**All four apps: Update now no longer refuses a good download.** It used the checksum remembered from an earlier check, which could belong to an older release than the one it then downloaded, so it said "The download didn't match its checksum" and did nothing. Update now now reads the release list again first, and says "You already have the latest version" when that is true. A remembered "update available" is also re-checked at start-up instead of after a day.

**DeskBar**

- **Fixed: the bar flashed and sank behind apps when two screens were connected and the main screen was not the left-most one** (for example a laptop to the left of the main monitor). DeskBar treated the left-most screen as the main one, so the real main screen got its usable area re-set every 3 seconds, and that made Windows' own taskbar pop up over DeskBar every few seconds. The main screen is now the one Windows calls primary, and no other screen can re-set its area more than once every 15 seconds.
- **New: a search box on the bar** (off by default). Turn it on under *Widgets on the bar* in DeskBar settings (right-click the bar). It sits right after the Windows key and you type straight into it. Enter opens a Google search in your browser, or the address if you typed one; Esc clears it and gives the keyboard back to the window you were in.
- **New: a program's dialogs and popups open on the same screen as the window that opened them** (on by default, one switch in settings). Some programs (Quartus II, for one) opened their popups on the other screen.
- **Fixed: a pinned app lost its icon and would not start after it updated itself** (Discord, Slack and other apps that keep their program in a numbered folder like `app-1.0.9266`). The pin now follows the app to its newest folder.
- **Fixed: DeskBar settings opened on the left-most screen and did not fit.** It now opens on the screen you clicked on, is as tall as that screen allows, and scrolls for the rest.

**Updating:** with 2.1 installed, DeskBar offers this itself: right-click the bar, *Update DeskBar*. Or run the new `DeskBar-Setup.exe` over your old copy; settings are kept.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
