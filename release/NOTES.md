## Desk 2.9.7

**DeskBar**

- A maximized window can no longer be resized from its left, right or bottom edge by accident. Dragging the title bar to move it still works. Apps that draw their own title bar (like Chrome) are put back to maximized, so you don't end up with a window that only looks maximized.
- The setup now stops every DeskBar that is running, from whatever folder it started, and removes an old start-up entry. Before, an older copy left running elsewhere kept the bar, and the freshly installed version quietly closed, so you still saw the old one.

DeskDrawer and DeskTune are the same as in 2.9.6.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
