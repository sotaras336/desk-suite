## Desk 3.1

**DeskBar**

- **Any colour you like:** Quick Settings > Color now has a slider. Slide it, watch the preview, press Use. DeskDrawer and DeskTune follow the same colour.
- **Add a Bluetooth device right in the panel:** Bluetooth > Add device now searches for nearby devices itself (about ten seconds) and lists them with a Pair button. Before, it handed off to Windows' "Add a device" window, which did not always come up. Put the device in pairing mode first; Windows shows its own prompt when a device asks for a PIN or a confirmation.
- Fixed "Searching 26" appearing where "Searching…" was meant.
- The tester's bug-report setup now works whatever version it is: it switches Report a bug on, then installs the newest DeskBar from GitHub if that is newer than itself.

DeskDrawer and DeskTune are the same as in 3.0.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
