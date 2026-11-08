## Desk 2.3.1

A DeskDrawer fix. DeskBar, DeskTune and DeskMode are the same as in 2.3.

**DeskDrawer: a plain click on the path bar opens it for typing.** A click anywhere on the bar except a folder name or its little arrow (the blank end, the padding, the space beside the folders) now turns it into the text box. The bar takes the click before anything inside it can, and touch panning inside the bar is switched off, so a tap works too. The right-click shortcut added in 2.3 is gone. Ctrl+L, F4 and Alt+D still open it from the keyboard.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. With 2.2 or later installed, DeskDrawer offers this itself: ⋯ menu, *Update DeskDrawer now*, or the link in its status bar. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
