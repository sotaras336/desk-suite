## Desk 2.1

**The apps can now update themselves.** No more downloading and running a setup file for every new version.

- About once a day each app looks at `SHA256SUMS.txt` in the newest release on GitHub. If the checksum listed for its own setup file differs from the exe that is running, a newer build is out, and the app says so:
  - **DeskBar:** an *Update DeskBar* entry at the top of the taskbar's right-click menu, and a button in DeskBar settings.
  - **DeskDrawer:** *A new version is available · Update now* in the status bar, and *Update DeskDrawer now* in the ⋯ menu.
  - **DeskTune:** the same line in the footer.
  - **DeskMode:** a banner at the top of the panel, and a button in Settings.
- **Nothing is installed until you press Update now.** The app then downloads the new setup file, checks it against the published checksum (a file that doesn't match is never run), and runs it. It closes the old copy, replaces it and opens the new one. Your settings and DeskTune's undo log are kept.
- Only a plain download request leaves your PC: nothing about you or your PC is sent. **Switch the daily look off** in each app: DeskBar settings, DeskDrawer ⋯ menu, DeskTune footer (*Daily check: on*), DeskMode Settings. *Check for updates* looks right now.
- An app that didn't change in a release is never offered an update, because the check compares the files, not version numbers.

**You need to update by hand once more** (run each new `-Setup.exe` over your old copy). Versions before 2.1 don't have the check; from here on they update themselves.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Updates the apps download themselves don't ask. Checksums are in `SHA256SUMS.txt`.

**Privacy text:** the site and README no longer say "nothing leaves your PC" without qualification. They now describe this daily check.

DeskBar, DeskDrawer, DeskTune and DeskMode are otherwise the same as in 2.0.1.
