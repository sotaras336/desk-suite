## Desk 3.4.1

**DeskBar: taps on the bar work over full-screen windows**

- With a window filling the screen, touching the bar could grab that window's bottom edge instead of pressing the button under your finger. The window jumped back to full screen and the tap was lost. DeskBar now checks whether the touch is on the bar itself, so the tap goes through.
- The built-in self-test no longer leaves test pins in your settings.

DeskDrawer is the same as in 3.2.3, DeskTune as in 3.2.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
