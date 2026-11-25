## Desk 4.0

Desk 4.0 is the finished version of Pre-release 4: one settings window for the whole suite, a colour for each app, clipboard history, split view in DeskDrawer, and a long list of fixes found while testing. Each app offers it as its next update, and there is now one installer (`Desk-Setup.exe`) for all three apps.

**One settings window, one look**

- One **Desk settings** window for DeskBar, DeskMode, DeskDrawer and DeskTune, opened from any of their gear buttons, on the right page.
- One colour for every app, or a different one for each. The full colour picker is in every app, with new Saturation, Softness and Fade controls.
- The lighter glass theme is in every app, DeskTune and DeskMode included.
- **Desk apps** installs, updates and opens DeskDrawer and DeskTune, each download checked against this release's checksums. A copy built on your own PC is never "updated" back to an older release.
- `Desk-Setup.exe` installs any of the three apps in one go. No administrator rights are needed.

**DeskBar**

- **Clipboard history:** press Win+V for a small panel with the last 25 texts you copied. Pin what you use often, search, and press Enter to paste. Copies from password managers are never kept, and the history stays in memory only. Turn it off in Desk settings > Taskbar > Clipboard.
- The bar's right-click menu is short: DeskDrawer, Task Manager and Desk settings.
- A tap just above the bar next to a maximized app no longer starts a resize of that app, and the bar's touch strip steps aside while a menu or panel is open.
- The bar no longer freezes when an open app stops responding.
- When Explorer restarts, the bar takes the screen back within a couple of seconds: Windows' taskbar hidden again, tray icons back.
- A faster start after a reboot, and the stand-in bar shows program icons.
- App icons swell more smoothly when you point at them (Settings > Taskbar > Effects).
- Uses less memory. A crash writes `crash.txt` with function names, which helps with bug reports.
- Screen reader and keyboard support across the bar and its panels.

**DeskDrawer**

- **Split view:** Ctrl+\ shows two folders side by side in one window. F6 switches sides, and Copy and Move send files to the other side. The split is remembered. (Side by side in two windows is now Ctrl+Shift+S.)
- Ctrl+1 to 9 and Ctrl+Page Up / Page Down move between tabs, and tabs work with the keyboard and screen readers.
- Press Space on a file for a big look at it without opening it. It opens on the screen DeskDrawer is on, and closing it no longer crashes.
- Save As and Open pickers start in the folder the dialog is showing, and a double-click in the Open picker opens the file.
- Clicking the selected sidebar folder again goes back to it.
- Updating while DeskDrawer is open works: setup waits for it and closes it.

**DeskTune**

- Opens only one window; starting it again brings the open one forward.
- Follows colour changes made in DeskBar straight away.
- Updating while DeskTune is open works.

**All apps**

- A broken settings file is recovered from a spare copy instead of being lost.
- In the testers' Bugs build, a filled-in bug report opens about 20 seconds after a crash.
- Every change is built and self-tested on Windows automatically.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
