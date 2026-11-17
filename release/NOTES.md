## Desk 3.2.4

**DeskBar** (Start search)

- **New files show up at once.** DeskBar's list of every file is rebuilt every few hours, so a file you had just saved could not be found until then. Now Windows tells DeskBar about new and renamed files in your folders and on your other drives, and they are searchable within a second.
- **Open file location** for apps: in a search result and in the right-click menu, it shows the program's file in its folder.
- **Uninstall goes straight to that app**, instead of opening the list of every app:
  - Store apps open their own page in Settings, which has the Uninstall button.
  - Other programs: DeskBar finds the program in Windows' list of installed programs and starts its own uninstaller (what Settings' Uninstall button does), after asking you first.
  - Anything it can't place opens the Installed apps page in Settings, as before.

DeskDrawer is the same as in 3.2.3, DeskTune as in 3.2.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
