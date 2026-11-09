## Desk 2.5

DeskDrawer can answer the Open and Save windows of other apps, and DeskMode loses its animations. DeskBar and DeskTune are the same as in 2.4.

**DeskDrawer: Open and Save windows of other apps (off by default)**

- Turn it on in DeskDrawer's ⋯ menu: **Use DeskDrawer for other apps' Open and Save windows**. When Word, Chrome or any other app opens a Save As or Open window, DeskDrawer's own picker opens instead, in DeskDrawer's look: your open folders and the usual places on the left, the files in the middle, the file name at the bottom.
- The file-type list of the original window is a drop-down in the picker (also when saving), and the file name follows the type you choose (report.docx becomes report.pdf).
- It works by putting Windows' own window out of sight, then typing your answer into it and pressing Open or Save, so the app gets exactly what Windows' window would have given it. Several files in one folder work too. If anything looks unusual (no file-name box, a folder picker, an error), the normal window stays or comes back.
- It does not replace Windows' file dialogs system-wide and changes nothing in the other app. Windows Store apps use pickers Windows runs itself; those stay as they are.
- If DeskDrawer is closed or the switch is off, nothing changes at all.

**DeskMode: no animations**

- The panel opens at once: the rising-in animation, the drifting glows and the pop of a changed number are gone, and Windows' own fade when it opens and closes is switched off for it.
- The screen fade around a refresh-rate change is removed, together with its switch in Settings. Windows still blanks the screen for a moment when the rate changes; that part is Windows'.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer, DeskTune and DeskMode; DeskBar needs nothing extra. With 2.2 or later installed, DeskDrawer and DeskMode offer this update themselves. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
