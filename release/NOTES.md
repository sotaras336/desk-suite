## Desk 2.9.4

DeskDrawer fixes, and update notices you can trust.

**DeskDrawer**

- A shortcut to a folder (like the ones on the Desktop) opens inside DeskDrawer, from the list, from Home and in the Open/Save window. It used to open in File Explorer.
- Cut items are drawn dimmed until they are pasted, like in File Explorer (also when the clipboard changes in another app).
- Many open tabs share the tab bar like a browser's: they get narrower instead of running under the new-tab and window buttons.

**All three apps**

- No more "a new version is available" right after updating. An app remembered what its last check found, and if that check came just before a release (or failed), it announced an update it already had. Now a mismatch only makes it look again, and a failed look is retried after 10 minutes instead of 6 hours.
- An **Update now** button shows at the bottom of the settings window's page list whenever an update is out, so you see it on every page, and the button on the Updates page appears as soon as a check finds one.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
