## Desk 3.2

A reliability release: the parts of DeskBar and DeskDrawer that were built weakly have been rebuilt, and a round of bugs is fixed.

**DeskBar**

- **Wi-Fi.** Networks this PC already knows connect through Windows' own Wi-Fi engine, usually in about a second, and on one tap. New secured networks show the password box straight away. Saved networks carry a badge and can be forgotten from the list. The list keeps itself current while open.
- **Sign-in networks** (hotel, guest, café): DeskBar notices a sign-in page right after connecting and shows an **Open sign-in page** button. Wi-Fi with no internet no longer shows as "not connected".
- **Bluetooth.** The tile and the page update the moment a device connects or drops. New devices appear as they are found. Connected devices show their charge where Windows knows it (classic devices such as earbuds and speakers). Devices can be forgotten from the page.
- **Sound.** Volume and device changes show at once, and the Sound page updates itself when headphones are plugged in or an app starts playing. "Also play" has a **Bluetooth delay** slider to tune out an echo by ear.
- **Brightness** slider follows the brightness keys and automatic brightness.
- **Quick Settings tiles** say so when a switch didn't take. Tiles that open a Windows panel or a Settings page are drawn with an arrow.
- **Touchpad:** a three-finger swipe down minimises everything again.
- **Tray:** the overflow pop-up follows icons that come and go while it is open.
- Fixed: settings **Save / Bring back** used the wrong folder; a possible crash in the input helper when a screen is plugged or unplugged; windows mis-snapping after another window closed mid-drag; blank preview cards for windows closed elsewhere; needless background work reading recent files.

**DeskDrawer**

- **Dragging from behind another window:** pressing on files that are already selected no longer brings DeskDrawer in front of the window you are dragging to. A plain click still brings it forward.
- **The open folder updates in place** when files change: your scroll position, selection and folder sizes stay where they were, even during a download.
- **Copy, move and delete** use the same engine as File Explorer: long paths work, and errors are said in words.
- **Folder sizes** show at once on a folder you have visited, and correct themselves in the background.
- **Unreachable drives** (a network share that is off, a failing USB stick) no longer freeze the window.
- **Compress / Extract** show progress, stop with Esc, and Extract also opens .7z, .rar and .tar archives.
- The "DeskDrawer folders" button on other apps' Save and Open windows waits for the window to really reach the folder.

DeskTune is the same as in 3.0.

**Privacy note:** to detect a Wi-Fi sign-in page, DeskBar asks Microsoft's connection-test address for a small test file, as Windows itself does. Nothing about you is sent.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
