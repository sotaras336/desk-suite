## Desk 3.9.1

**Touch on the bar stays on the bar**

- DeskBar: tapping the bar next to a maximized app (for example the bottom-right corner, to show the desktop) could start resizing that app's window instead, mostly with Chrome-based apps such as Chrome, Edge, Discord or Claude. Every DeskBar window (the bar, Start, Quick Settings, menus) now claims a finger that lands on it.

**DeskDrawer folders button**

- DeskDrawer: the "DeskDrawer folders" button under a Save / Open dialog showed empty boxes instead of its folder and arrow icons. Fixed.
- DeskDrawer: only the DeskDrawer running in the background watches Save / Open dialogs. An extra DeskDrawer window could put a second button under them.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
