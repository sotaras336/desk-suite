## Desk 2.9.3

An important DeskBar fix: it no longer resizes maximised windows every few seconds. Smoother drags out of DeskDrawer.

**DeskBar**

- DeskBar checks every few seconds that no maximised window has slid under the bar (that can happen after the screen rotates). It counted the invisible resize border that every maximised window hangs past the screen edge as "under the bar", resized the window, and Windows put it straight back. So maximised windows that draw their own title bar (DeskDrawer, Chrome, depending on the display scale) were resized every 3 seconds, all day. That made Chrome draw only part of its page, made drag and drop glitchy and brought Windows' own taskbar up. Now only a window that really covers the bar is moved, and never the same window twice within a minute.
- The same care for windows that aren't maximised: a few pixels of border over the bar no longer counts, and windows Windows snapped are left alone.

**DeskDrawer**

- Dragging files to other apps works the way File Explorer does: Windows' own file data, so apps get every format they look for, and the mouse is fully let go after the drop.

DeskTune is the same as in 2.9.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
