## Desk 3.0

**DeskBar**

- Fixed a crash that could happen at the moment a song or video ended.
- Fixed three more rare crashes: changing page quickly in Settings, dragging a file over the taskbar while that app closes, and switching monitors.
- Go max, Go easy and the automatic switch when you dock now take turns, so two display changes can no longer fight each other.
- New on the Color page: **Match my wallpaper** picks the colour nearest to your wallpaper.
- New in Settings > Start-up and updates: **Save** and **Bring back** keep your DeskBar and DeskDrawer settings in `Documents\Desk settings`, for a new PC or a reinstall.
- New, and off unless you switch it on: **Report a bug**. It adds a small notepad to the bar's right-click menu. When you press Send, what you typed goes to the developer with DeskBar's version, your Windows version, your screen setup and the apps' recent logs. Nothing is sent before you press Send.
- The bar's right-click menu is shorter: Task Manager and DeskBar settings.
- DeskBar gives back the memory it only needed while starting.

**DeskDrawer**

- Dragging selected files out now works from any part of a selected row. Before, pressing beside the name started the selection box instead.
- **Rename several files at once:** select them, press F2 (or right-click > Rename all), type one name. They are numbered and each keeps its own extension. Undo works.
- **Side by side:** right-click a tab > Side by side (Ctrl+Shift+2). Your window takes the left half of the screen and a second one opens on the right, ready to drag files across.
- Opening DeskDrawer after it sat closed for a while should no longer take a few seconds now and then.
- If something goes wrong, DeskDrawer writes it to `crash.log` and keeps running where it can, instead of closing without a trace.

**DeskTune**

- After Apply, DeskTune tells you how much memory was in use before and after.
- The same `crash.log` safety net as DeskDrawer.

**What you need:** Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0) for DeskDrawer and DeskTune; DeskBar needs nothing extra. With 2.2 or later installed, each app offers this update itself. The files aren't code-signed, so Windows SmartScreen asks the first time you run a downloaded setup: More info, then Run anyway. Checksums are in `SHA256SUMS.txt`.
