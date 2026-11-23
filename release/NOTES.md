## Desk 3.9

**A cleaner Save As and Open**

- DeskDrawer: the Save As / Open window it shows for other apps is redesigned. It has a title saying which app asked ("Save As for Google Chrome") instead of tabs, a short toolbar (New folder, Sort, View, Filter), and a line saying where the file will go. The file type is a proper drop-down.
- DeskDrawer: it opens at a size where names, dates and types fit. If you resize it, it keeps your size from then on, on every screen.

**Touch no longer resizes maximized windows**

- DeskBar: tapping or dragging near the bottom of a maximized window with a finger could still start a resize, mostly in Chrome and other Chromium apps. Those apps claim a finger near their edge before any other window is asked. The strip above the bar now claims every finger that lands on it first.

**Less memory**

- DeskDrawer: big photo folders in icon view no longer fill memory with a thumbnail of every file (Camera Roll went past 235 MB and kept climbing; now it stays near 150 MB). Only what is on screen, and a little around it, gets a thumbnail.
- DeskBar: icons of files shown once in search are let go after 10 minutes unused, instead of staying in memory for good.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
