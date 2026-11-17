## Desk 3.2.1

**DeskDrawer** (Save and Open windows)

- **The file name is kept.** Apps such as Chrome and Office fill in the file name a moment after their Save window appears, and DeskDrawer's picker opened before that, with an empty name box. It now picks up the name when it arrives (never over something you have typed), and selects it without the extension, as Windows does, so typing replaces "report" and keeps ".docx".
- **A sensible size.** The picker was about three quarters of a Surface screen and sized for the main screen even on another one. It is now sized for the screen the Save window is on, at most 60% of its width and two thirds of its height.
- **Opens faster.** When the Save window opens in the same folder as last time (Downloads, usually), the folder list already built is reused, and DeskDrawer no longer hands its memory back to Windows when you close it, which made the next picker slow to wake.

DeskBar and DeskTune are the same as in 3.2.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
