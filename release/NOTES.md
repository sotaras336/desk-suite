## Desk 2.3

A DeskDrawer update. DeskBar, DeskTune and DeskMode are the same as in 2.2.1.

**DeskDrawer: the path bar can be typed into, like File Explorer's**

- **Click the path bar to type a path.** A click on the blank end of the bar (there is always one now, even when the path fills the bar), a right-click anywhere on it, Ctrl+L, F4 or Alt+D turns it into a text box with the whole path selected.
- **Suggestions under the box.** Open it and it lists the folders you were in lately; type and it lists the folders that complete what you typed. Up and Down choose, Tab completes the name and goes on into that folder, Enter or a click goes there, Esc closes the list (a second Esc leaves the box).
- **Fixed: what you were typing was wiped when the folder changed.** Whenever files appeared or disappeared in the folder (a download, a program writing its output), the address box was put back to the current path in the middle of your typing. It now leaves the box alone while you are in it.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. With 2.2 or later installed, DeskDrawer offers this itself: ⋯ menu, *Update DeskDrawer now*, or the link in its status bar. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
