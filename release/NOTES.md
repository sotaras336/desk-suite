## Desk 3.8.5

**Window previews stay out of the way**

- DeskBar: right-clicking an app on the bar right after pointing at it could open its window preview on top of the menu. Clicking an app quickly, or starting to drag a pinned app, could also pop the preview up a moment later. A click, right-click or drag now cancels a preview that was about to open.
- DeskBar: while a bar menu is open, pointing at another app no longer opens its preview over the menu.
- DeskBar: after closing a window from its preview and moving the mouse away, the preview no longer comes back on its own.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
