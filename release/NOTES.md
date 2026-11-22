## Desk 3.8.3

**No more resizing maximized windows with a finger, and a faster DeskDrawer**

- DeskBar: an invisible strip along the top edge of the bar now takes a finger touch there as a tap on the bar, so a maximized window's bottom edge can't be grabbed with touch (before, the grab started and was cancelled, and the window flickered). The mouse is unaffected. Windows that fill the screen without being maximized get the same protection, and a window left with only a sliver showing above the bar is lifted back even when it isn't in front.
- DeskDrawer: "Show more options" no longer freezes the window while Windows' shell extensions load; the first use is warmed up in the background.
- DeskDrawer: folders with hundreds of pictures load thumbnails on three threads, starting with the rows on screen, and again from where you scroll to.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
