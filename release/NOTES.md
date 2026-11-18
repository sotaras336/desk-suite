## Desk 3.4.2

**DeskBar: touching the bar under a maximized window works**

- A fingertip is wider than the bar's edge, so a touch could land a few pixels above the bar, on the maximized window's bottom edge, and start resizing that window instead of pressing the bar. DeskBar now treats that strip as part of the bar when the window above is maximized, so the tap goes through.

DeskDrawer is the same as in 3.2.3, DeskTune as in 3.2.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
