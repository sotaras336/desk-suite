## Desk 2.4

A DeskMode update: a new look, colour choices, and a smoother refresh-rate change. DeskBar, DeskDrawer and DeskTune are the same as in 2.3.2.

**DeskMode: a new look**

- **What each screen is doing, big:** a glowing mini display at the top shows each screen's refresh rate in large light numerals (with a small "highest" tag when it is at its top rate). It pops when the rate changes.
- **One card per screen:** each screen has its own card with its refresh-rate chips and brightness slider. Brightness for both screens and the "brightness keys also change the monitor" switch sit in a slim card of their own. The monitor card only appears when a monitor is connected.
- **The preset that is on is lit:** Go max, the monitor-first preset or Go easy glow with a gradient when the screens and power mode match it.
- **Gradient chips and tiles with a soft glow**, rounder, roomier cards, header status as small pills, two slow glows drifting behind the panel while it is open (they stop when it is hidden), and each block rising into place when the panel opens.
- **Easier on the eyes:** softer glows, and a soft off-white instead of pure white text in the dark theme. The selected chip's text is readable in every colour (it was white on pale blue before).

**DeskMode: colour options** (Settings > Appearance, the first card)

- Colour: *Windows accent* (the default, follows Windows), Indigo, Teal, Green, Orange, Red, Pink, Light pink and Violet. Look: Auto (follows Windows), Dark or Light. Both apply at once.

**DeskMode: a smoother refresh-rate change**

- Windows blanks a screen while its refresh rate changes. DeskMode now fades the screens that are about to change to black and back, so it reads as one smooth dip, not a flash. It can be turned off in Settings ("Fade the screen while the refresh rate changes"). The black windows are click-through and close by themselves after a few seconds if anything goes wrong.
- When both screens change (Go max, Go easy), they change together: one blink, not one after the other.
- Go easy straight after Go max sometimes stayed at the high rate, because Windows reshuffles the display while the power mode changes and the change was sent only once. DeskMode now checks the result and retries a few times.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. With 2.2 or later installed, DeskMode offers this itself: the banner at the top of its panel, or Settings. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
