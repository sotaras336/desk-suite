## Desk 3.0.2

**DeskBar**

- Fixed: after a restart, some PCs went back to a much older DeskBar. An older copy (an old install folder, the old pink edition, a leftover start-up entry) could start first, and the new one then quietly stepped aside. Now the installed DeskBar stops any older copy at start and switches the old editions' start-up off (disabled, not deleted), so the version you installed is the one that runs.

DeskDrawer and DeskTune are the same as in 3.0.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
