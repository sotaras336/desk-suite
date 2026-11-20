## Desk 3.8.1

**Maximized windows stay maximized**

- DeskBar: grabbing the bottom edge of a maximized window right above the bar (a mouse or a finger on the bar's edge) no longer un-maximizes it. Apps that draw their own frame, like Chrome, restored themselves as the drag started; DeskBar now puts them back.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
