## Desk 2.9.2

Two DeskDrawer fixes.

**DeskDrawer**

- OneDrive shows in the sidebar on every PC. It used to depend on two Windows variables that some PCs don't set, and that a DeskDrawer already running in the background never sees when OneDrive is set up later. DeskDrawer now reads where OneDrive keeps your folders from OneDrive itself.
- The Open and Save window for other apps shows the folders you pinned, including ones pinned after DeskDrawer started. It is prepared ahead of time so it opens fast, and it used to keep the pins from that moment.

DeskBar and DeskTune are the same as in 2.9.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
