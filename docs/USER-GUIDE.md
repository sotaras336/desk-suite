# Desk Suite 4.0 User Guide

Written for Desk 4.0, 24 Nov 2026.

## Welcome

Desk is three small, free Windows 10 and 11 apps (64-bit) that work alone or together. DeskMode lives inside DeskBar. There is no account, no ads and no tracking.

| App | What it does | Where its settings live |
| --- | --- | --- |
| **DeskBar** | Replaces the taskbar, Start and Quick Settings | `%LOCALAPPDATA%\DeskBar3` |
| **DeskMode** (inside DeskBar) | Refresh rate, power mode and brightness for every screen | Same as DeskBar |
| **DeskDrawer** | Replaces File Explorer, with tabs and fast search | `%LOCALAPPDATA%\DeskDrawer` |
| **DeskTune** | Switches off what you don't use, and can undo every change | `%LOCALAPPDATA%\DeskTune` |

### Install

1. Download `Desk-Setup.exe` for the whole suite, or `DeskBar-Setup.exe`, `DeskDrawer-Setup.exe` or `DeskTune-Setup.exe` for one app, from the [latest release](https://github.com/sotaras336/desk-suite/releases/latest).
2. Run it. If Windows says "Windows protected your PC", choose **More info**, then **Run anyway**. The apps are not code-signed because certificates cost money every year.
3. It installs for your account only, with no administrator rights, adds a Start menu entry and opens.

DeskDrawer and DeskTune need Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0). Windows offers to install it the first time if it is missing. DeskBar needs nothing extra.

### One settings window for the suite

Every gear button in every Desk app opens the same **Desk settings** window, on that app's page. Changes apply the moment you make them; there is no OK button. You can also open it by right-clicking an empty part of the bar and choosing **Desk settings**.

The pages are: Taskbar, Screens and windows, DeskMode, Colours, DeskDrawer, DeskTune, Desk apps, and Start-up and updates. A **Windows Settings** link at the bottom of the rail opens Windows' own Settings.

### Colours

Pick a colour theme in Quick Settings > Colour (on the bar), or in DeskDrawer's View menu. There are ten themes, including black, white and glass, plus a full custom colour picker with Saturation, Softness and Fade, and **Match my wallpaper**.

In Desk settings > **Colours**, **Same colour for every app** is on by default. Turn it off to give DeskBar, DeskMode, DeskDrawer and DeskTune each their own colour. DeskBar and DeskMode take a second to restart into a new colour.

### Updates

Each app looks once a day for a newer version. It only installs one when you press **Update now**, and it only offers a version built later than the one you have. Turn this off with **Look for updates once a day** in each app's settings. The **Desk apps** page installs, updates and opens DeskDrawer and DeskTune for you.

### Move to a new PC

Desk settings > Start-up and updates > **Your settings**: **Save** copies everything you set in DeskBar, DeskDrawer and DeskTune to `Documents\Desk settings`. On the new PC, press **Bring back**. DeskBar restarts to load them.

### Uninstall

Open Start and choose **Uninstall DeskBar**, **Uninstall DeskDrawer** or **Uninstall DeskTune**. DeskBar gives the taskbar back to Windows. DeskDrawer gives folders and Win+E back to File Explorer. DeskTune leaves your changes in place, so press **Put everything back** in DeskTune first if you want Windows exactly as it was.

## DeskBar

DeskBar hides Windows' taskbar while it runs and puts its own bar on every screen. Quit it and Windows' taskbar comes straight back.

### The bar, left to right

| Part | Click | Right-click or long-press |
| --- | --- | --- |
| **Start button** | Opens DeskBar's Start | Opens Windows' power-user menu (the same as Win+X) |
| **Search box** (optional) | Type and press Enter to search Google | |
| **App buttons** | Brings the app to the front, or minimises it if it is already in front. With several windows, shows their previews. | App menu (see below) |
| **Media player** | Opens the app that is playing. Previous, play/pause and next buttons sit beside it. | |
| **Tray icons** and the **^** button | Your background apps' icons, and the hidden ones behind ^ | That app's own menu |
| **Touch keyboard** | Opens the on-screen keyboard. Only shown on touch screens at 150% scale or more. | |
| **DeskMode button** | Opens the DeskMode panel | |
| **Keyboard language** | Switches language (the same as Win+Space) | |
| **Wi-Fi, sound and battery** | Opens Quick Settings | |
| **Clock** | Opens the calendar | |
| **Far-right corner strip** | Show desktop: minimises everything, click again to bring it back | |

Right-click an empty part of the bar for: **DeskDrawer**, **Task Manager**, **Desk settings**, **Update DeskBar** (when a new version is out) and **Report a bug** (when switched on).

### App buttons

- **Shift+click** or **middle-click** opens a new window of the app.
- **Scroll the wheel** over an app with several windows to step through them.
- **Point** at an app to see its window previews. Click a preview to go to it; middle-click a preview to close that window.
- **Drag** a pinned app sideways to reorder it.
- The **app menu** has: the app's name (opens it), **Run as administrator**, up to six recent files it opened, **Pin to taskbar** or **Unpin from taskbar**, **Send to other screen** (with two or more screens), **End task**, and **Close window** or **Close all windows**.
- Turn on Settings > Taskbar > Effects > **Magnify apps under the mouse** to make icons swell as you point at them.

### Start

Press the Windows key, Ctrl+Esc or the Start button. Start shows a greeting, your pinned apps, folders, recent files and **All apps**. You can start typing straight away, even before Start has fully opened; nothing you type is lost.

Search finds apps, Settings pages, files, folders and classic programs as you type, best match first. It also does:

- **Sums**: type `12*7+3`, `50%`, `2^10`, `sqrt 81` or `sin 30`. **Copy result** copies the answer.
- **Conversions**: type `5 km to mi`, `200 lb in kg`, `2 GB to MB`, `90 min to h` or `100 F to C`.
- **Commands and paths**: type `cmd`, `regedit`, `%appdata%`, `~\Downloads` or `C:\Users` and press Enter.

In search, Up and Down move through the results, Enter opens one, and **Ctrl+Shift+Enter** opens an app or command as administrator. Esc clears the search; Esc again goes back to the start page.

Right-click any app for **Pin to Start**, **Pin to taskbar**, **Open file location**, **Run as administrator** and **Uninstall**. Right-click a pinned app for **Pin to top**, **Unpin**, **Move to front**. Use **Add a folder to Start** to pin folders; right-click a recent file to pin it or remove it from the list.

The power button at the bottom gives **Lock**, **Sign out**, **Sleep**, **Hibernate**, **Shut down** and **Restart**.

### Quick Settings

Click the Wi-Fi, sound and battery icons. It has:

- Sliders for **brightness** and **volume**. Tap the speaker to mute; the wheel works on both sliders.
- Tiles for **Wi-Fi**, **Bluetooth**, **Airplane mode**, **Energy saver**, **Night light**, **Mobile hotspot**, **Nearby sharing**, **Rotation lock**, **Focus**, **Cast**, **Project**, **Accessibility**, **Live captions** and **Studio effects** (only those your PC supports).
- The arrow on a tile opens its page:
  - **Wi-Fi**: nearby networks, password entry, **Forget**, and **Open sign-in page** when a café or hotel network wants you to sign in.
  - **Bluetooth**: paired devices, **Connect**, **Disconnect**, **Forget**, and **Add device** to pair a new one.
  - **Sound**: the main output, **App volumes**, and **Also play**, which plays the same sound on a second device. If you hear an echo between a Bluetooth speaker and a wired one, move the **Bluetooth delay** slider until it is gone.
  - **Colour**: the ten themes, the custom colour picker, **Match my wallpaper**, and DeskMode's own colour.
- The media player, battery state and memory in use.

### DeskBar settings

| Page | Setting | What it does | Default |
| --- | --- | --- | --- |
| Taskbar | Windows key opens DeskBar's Start | Off: the Windows key opens Windows' own Start | On |
| Taskbar | Battery percentage | Number next to the battery icon | On |
| Taskbar | Media player | Song, artist and controls while something plays | On |
| Taskbar | DeskMode button | The DeskMode button on the bar | On |
| Taskbar | Search box | A Google search box next to the Start button | Off |
| Taskbar | Magnify apps under the mouse | Icons swell when you point at them | Off |
| Screens and windows | Same place on the next screen | Crossing to a screen of another size, the mouse lands at the same relative height | On |
| Screens and windows | Edge resistance | How hard you push past an edge before the mouse crosses: None, Light, Medium, Strong | Medium |
| Screens and windows | Snap onto one screen | A window let go across two screens moves fully onto the one holding most of it. Hold **Alt** while letting go to leave it where it is. | On |
| Screens and windows | Keep the size in proportion | A window dragged to a bigger or smaller screen grows or shrinks with it | On |
| Screens and windows | Rescue windows | When a screen is unplugged, its windows come back to one you can see, and go back when you plug it in again | On |
| Screens and windows | Popups follow their window | A program's dialogs open on the same screen as the program | On |
| Start-up and updates | Start DeskBar when I sign in | Starts before the desktop shows | On |
| Start-up and updates | Look for updates once a day | Only looks; nothing installs until you press Update now | On |
| Start-up and updates | Report a bug | Adds **Report a bug to Claude** to the bar's right-click menu | Off |

The same page has **Restart DeskBar** and **Quit**. Quitting gives Windows its own taskbar back.

### Clipboard history

Press **Win+V** to see the last 25 texts you copied. Click one, or press Enter, to paste it; Ctrl+P pins it and Ctrl+Del removes it. Pins are saved on your PC, locked to your Windows account. Copies from password managers are never kept, and the history is only kept until you sign out. Turn it off, or give Win+V back to Windows, in Desk settings > Taskbar > Clipboard.

### Touch and touchpad

- **Long-press** anything on the bar or in Start to get its right-click menu.
- Tapping just above the bar next to a maximised app no longer grabs that app's bottom edge. A thin invisible strip takes the tap and passes it to the bar; the mouse goes straight through it.
- **Three-finger swipe down** on a precision touchpad minimises everything, as with Windows' own taskbar.

### Keyboard and screen readers

Tab and Shift+Tab, or the arrow keys, walk through everything you can click on the bar and in its panels. Enter or Space clicks; the menu key or Shift+F10 right-clicks. Menus take Up, Down, Enter and Esc; sliders take the arrows, Page Up, Page Down, Home and End.

Screen readers can read every button, switch and slider. With Windows' high contrast on, DeskBar uses its colours. Tooltips and menus follow Settings > Accessibility > Text size.

## DeskMode

DeskMode is one panel for refresh rate, power mode and brightness on every screen. Open it with the DeskMode button on the bar (next to the keyboard language) or **Ctrl+Alt+D**.

### In the panel

- **Go max**: every screen at its top refresh rate, power mode on Best performance, Energy saver off.
- **Go easy**: screens at 60 Hz, power mode on Recommended, Energy saver on.
- **Screens**: each screen's refresh rate and brightness. External monitors work too, over DDC/CI. Windows blanks a screen for a moment each time its rate changes.
- **Power mode**: Recommended, Better or Best, and Energy saver on or off.
- **By itself**: the three automatic switches below.
- Everything applies as soon as you change it.

### By itself

| Switch | What it does | Default |
| --- | --- | --- |
| Go max when docked | With a monitor and the charger connected, goes to Go max | On |
| Undo when undocked | When the monitor or charger is unplugged, puts the screens and power mode back how they were | On |
| Hands off during games | Nothing changes by itself while a full-screen game is up | On |

To choose exactly what Go max and Go easy do (the rate for each screen, the power mode and Energy saver), open Desk settings > DeskMode > **Choose what Go max and Go easy do**.

### Shortcuts

| Keys | Does |
| --- | --- |
| Ctrl+Alt+D | Opens the DeskMode panel |
| Ctrl+Alt+M | Go max |
| Ctrl+Alt+B | Go easy |
| Ctrl+Alt+F | Makes the window in front borderless full screen |

DeskMode gets its own colour in Quick Settings > Colour, or in Desk settings > Colours. With no colour of its own, it wears the bar's.

## DeskDrawer

DeskDrawer is a file manager with tabs that opens folders as fast as you click them. It opens on **Home**: a greeting, your pinned folders (the same ones pinned in File Explorer), recent files (newest first) and your drives.

### Moving around

- **Path bar**: click any part of the path to jump there, or click the empty part (or press Ctrl+L) and type a path, then Enter.
- **Back, Forward, Up**: the buttons, or Alt+Left, Alt+Right, Alt+Up. Backspace also goes up.
- **Sidebar**: Quick access, This PC and your drives. Clicking the folder you are already in takes you back to it.
- **Pin to Home**: right-click a folder to pin it to Home; right-click it on Home to unpin it.

### Tabs

Ctrl+T opens a new tab, Ctrl+W closes one, Ctrl+Tab and Ctrl+Shift+Tab move between them, and Ctrl+Shift+T reopens the last one you closed. Drag a tab sideways to reorder it.

Right-click a tab for **New tab**, **Duplicate tab**, **Open in new window**, **Side by side**, **Close tab**, **Close other tabs**, **Close tabs to the right** and **Reopen closed tab**.

### Split view

Ctrl+\ splits the window into two folders side by side. F6 switches sides, and **Copy** and **Move** send the selected files to the other side. The split is remembered. Ctrl+Shift+S opens two separate windows instead.

### Views

Use the View menu, or Ctrl+Shift and a number: 1 Extra large icons, 3 Medium icons, 4 Small icons, 5 List, 6 Details, 7 Tiles. Large icons is in the View menu. Ctrl+Plus and Ctrl+Minus zoom, Ctrl+0 resets.

The View menu also has **Sort by** and **Group by**. DeskDrawer remembers the view for each folder. Downloads opens sorted and grouped by date, newest first. **Alt+P** shows or hides the preview pane.

### Quick Look

Select one file and press **Space** for a big look at it without opening it. In Quick Look, the arrow keys step to the next or previous file, Enter opens it, and Space or Esc closes it. It opens on the screen DeskDrawer is on.

### Search

Type in the search box (Ctrl+F, Ctrl+E or F3). Results show best matches first. It searches the folder you are in; press **Search this PC** to look everywhere. DeskDrawer uses DeskBar's file list when it can, so whole-PC search is fast. **Search live** looks through the disk right now instead, and the stop button keeps what has been found so far. Esc leaves the results and goes back to the folder.

### Working with files

- **Copy, cut, paste**: Ctrl+C, Ctrl+X, Ctrl+V. **Ctrl+Shift+C** copies the full paths of what is selected.
- **Delete** sends to the Recycle Bin; **Shift+Delete** deletes for good.
- **Rename**: F2. Enter keeps the new name; Esc cancels.
- **New folder**: Ctrl+Shift+N. Right-click > **New** also makes text documents, shortcuts and anything your apps add.
- **Undo**: Ctrl+Z takes back the last rename, move, copy, new item or delete (a deleted file comes back from the Recycle Bin).
- **ZIP**: right-click > **Compress to ZIP file**, or **Extract all** on a ZIP. Esc stops a ZIP in progress.
- **Select**: drag a box around files; hold Ctrl or Shift to add to the selection. The menu also has **Select all**, **Select none** and **Invert selection**.
- **Right-click** also has **Open with**, **Open in new tab**, **Open in new window**, **Open in Terminal**, **Copy as path**, **Create shortcut**, **Properties** (Alt+Enter) and **Show more options** for Windows' full menu.

### DeskDrawer settings

Open them with ⋯ > **Settings**, **Ctrl+comma**, or Desk settings > DeskDrawer.

| Setting | What it does | Default |
| --- | --- | --- |
| Open folders with DeskDrawer | Folders, drives and Win+E open in DeskDrawer. Off gives them back to File Explorer. | Off |
| Answer other apps' Open and Save | When Word, Chrome or any app asks where to open or save, DeskDrawer answers instead of Windows' window | Off |
| Show hidden items | Hidden and system files are listed, dimmed. Ctrl+H flips it from any folder. | Off |
| Keep ready in the background | Closing the window keeps DeskDrawer out of sight (about 20–30 MB), so the next folder opens in about half a second | On |
| Scrolling speed | Slower, Slow, Normal or Fast, for the wheel and touchpad | Normal |
| Use the graphics card | Smoother on very large screens, about 85 MB more memory. Applies from the next start. | Off |
| Look for updates once a day | Only looks; nothing installs until you press Update now | On |

### Open and Save windows

With **Answer other apps' Open and Save** on, DeskDrawer's picker appears when an app asks for a file. It starts in the folder the app asked for. Type a name and press Enter (or the button) to save; double-click a file to open it; Esc cancels. It has no tabs.

## DeskTune

DeskTune makes Windows lighter. It shows what each item costs in memory and what you would lose, you tick what you want off, and you apply once. Every change is written down and can be put back. It never touches Windows Update, Defender, the page file, SysMain or system files.

### How to use it

1. Open DeskTune. It looks at the PC and lists everything you can switch off, with the memory in use at the top.
2. Tick what you want off, or press **Select Safe** or **Select Recommended**. The bar at the bottom shows what your selection would free.
3. Press **Apply**. DeskTune reports memory before and after. Startup and service changes fully take effect after a restart.

To put one thing back, flip its switch back and press Apply. To put everything back, press **Undo all** at the bottom, or Desk settings > DeskTune > **Put everything back**. **Rescan** looks at the PC again and re-measures memory.

### What it can switch off

| Group | Examples | Preset |
| --- | --- | --- |
| Starts with Windows | Any app that opens when you sign in | By hand |
| Windows extras | Widgets, Copilot, Game Bar background recording, Edge and Chrome running after you close them | Recommended |
| Windows extras | Phone Link, Store apps running in the background | By hand |
| Ads and suggestions | Suggestions in Start and Settings, lock screen tips and ads, apps Windows installs by itself, the advertising ID, OneDrive and Microsoft 365 ads in File Explorer, tailored experiences | Safe |
| Background services | Usage data to Microsoft, Windows Error Reporting, Fax, Retail Demo, offline maps updates, printing (when no printer is set up) | Recommended |
| Background services | Xbox services, Gaming Services, the Windows Search index, Surface Machine Learning, location | By hand |
| Preinstalled apps | Microsoft News, Weather, Solitaire, Tips, Get Help, Feedback Hub, Clipchamp, Candy Crush, Maps, To Do, Dev Home and others | By hand |

**Safe** picks only ads, suggestions and nags; nothing you use stops working. **Recommended** adds the Recommended items too. Each row says in plain words what you lose.

Turning off the Windows Search index does not affect DeskBar or DeskDrawer, which keep their own file list.

### Administrator rights

Services and machine-wide switches need administrator rights. Until you unlock them they are greyed out. Press **Unlock services**; DeskTune restarts as administrator. Before the first such change, it makes a restore point.

### DeskTune settings

- **Updates**: Look for updates once a day (on), Check now, Update now.
- **Administrator**: Unlock services.
- **Safety net**: Put everything back, and **Open the change log**. The log is `%LOCALAPPDATA%\DeskTune\changes.json` and lets DeskTune undo changes even after a restart.

If something was already off because it was changed outside DeskTune, DeskTune says so and cannot undo it.

## Hotkeys and gestures

Every shortcut in the suite in one place.

| App | Keys or gesture | Does |
| --- | --- | --- |
| DeskBar | Windows key, Ctrl+Esc | Opens Start (type straight away to search) |
| DeskBar | Win+X, or right-click Start | Windows' power-user menu |
| DeskBar | Up, Down, Enter, Esc in Start | Move through results, open, clear search, go back |
| DeskBar | Ctrl+Shift+Enter in Start | Open the app or command as administrator |
| DeskBar | Shift+click or middle-click an app | New window of that app |
| DeskBar | Wheel over an app | Step through its windows |
| DeskBar | Middle-click a preview | Close that window |
| DeskBar | Hold Alt while letting go of a window | Leave it exactly where you dropped it |
| DeskBar | Long-press (touch) | Right-click menu |
| DeskBar | Three-finger swipe down (touchpad) | Minimise everything |
| DeskBar | Tab, arrows, Enter, Shift+F10 | Use the bar and panels from the keyboard |
| DeskMode | Ctrl+Alt+D | Open DeskMode |
| DeskMode | Ctrl+Alt+M | Go max |
| DeskMode | Ctrl+Alt+B | Go easy |
| DeskMode | Ctrl+Alt+F | Borderless full screen for the window in front |
| DeskDrawer | Alt+Left, Alt+Right, Alt+Up, Backspace | Back, forward, up |
| DeskDrawer | Ctrl+L, F4, Alt+D | Type in the path bar |
| DeskDrawer | Ctrl+F, Ctrl+E, F3 | Search |
| DeskDrawer | F5 | Refresh |
| DeskDrawer | Ctrl+T, Ctrl+W, Ctrl+Shift+T | New tab, close tab, reopen closed tab |
| DeskDrawer | Ctrl+Tab, Ctrl+Shift+Tab | Next tab, previous tab |
| DeskDrawer | Ctrl+N | New window |
| DeskDrawer | Ctrl+Shift+S | Side by side (two windows) |
| DeskDrawer | Ctrl+\ | Split view in one window; F6 switches sides |
| DeskDrawer | Ctrl+1 to 9, Ctrl+Page Up and Page Down | Go to a tab |
| DeskBar | Win+V | Clipboard history |
| DeskDrawer | Ctrl+Shift+1, 3 to 7 | Extra large, medium, small icons, list, details, tiles |
| DeskDrawer | Ctrl+Plus, Ctrl+Minus, Ctrl+0 | Zoom in, out, reset |
| DeskDrawer | Alt+P | Preview pane |
| DeskDrawer | Space | Quick Look (arrows step, Enter opens, Esc closes) |
| DeskDrawer | Enter | Open |
| DeskDrawer | Ctrl+C, Ctrl+X, Ctrl+V | Copy, cut, paste |
| DeskDrawer | Ctrl+Shift+C | Copy paths |
| DeskDrawer | Ctrl+A | Select all |
| DeskDrawer | Del, Shift+Del | Recycle Bin, delete for good |
| DeskDrawer | F2 | Rename |
| DeskDrawer | Ctrl+Shift+N | New folder |
| DeskDrawer | Ctrl+Z | Undo |
| DeskDrawer | Alt+Enter | Properties |
| DeskDrawer | Ctrl+H | Show or hide hidden items |
| DeskDrawer | Ctrl+comma | Settings |
| DeskDrawer | Esc | Leave search results, or stop a ZIP in progress |

## Troubleshooting

| Problem | What to do |
| --- | --- |
| Windows says "Windows protected your PC" | Choose **More info**, then **Run anyway**. The apps are safe but not code-signed. To check a file, compare `Get-FileHash .\DeskBar-Setup.exe` with `SHA256SUMS.txt` in the release. |
| DeskDrawer or DeskTune won't open | Install the [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0). |
| The bar froze or vanished | Wait a few seconds: DeskBar notices a freeze and restarts itself within about 20 seconds. A simple stand-in bar shows while it does. |
| DeskBar keeps crashing | After more than 5 crashes in 3 minutes, DeskBar stops and gives Windows its taskbar back. Restart it from Start, and report the bug. |
| I want Windows' taskbar back now | Right-click the bar > Desk settings > **Quit**. If the bar won't answer, run `%LOCALAPPDATA%\Programs\DeskBar3\bin\DeskBar.exe --restore`. |
| The Windows key opens Windows' Start | Turn on Desk settings > Taskbar > **Windows key opens DeskBar's Start**. |
| A window landed in the wrong place on another screen | Hold **Alt** while letting go to leave it where it is, or turn off **Snap onto one screen** and **Keep the size in proportion**. |
| The mouse jumps between screens too easily, or not at all | Change Desk settings > Screens and windows > **Edge resistance**, or turn off **Same place on the next screen**. |
| Refresh rate or power mode changed by itself | That is DeskMode's **Go max when docked** and **Undo when undocked**. Turn them off on the DeskMode page. |
| The screen blinks when changing refresh rate | Normal: Windows blanks a screen for a moment on every rate change. |
| Brightness slider does nothing on a monitor | The monitor needs DDC/CI. Turn it on in the monitor's own menu. Some monitors don't allow it at all. |
| Wi-Fi connects but there is no internet | Quick Settings > Wi-Fi shows **Open sign-in page** when the network wants you to sign in. |
| Echo between a Bluetooth speaker and wired speakers | Quick Settings > Sound > **Bluetooth delay**: slide until the echo is gone. |
| Folders still open in File Explorer | Turn on Desk settings > DeskDrawer > **Open folders with DeskDrawer**. |
| DeskDrawer uses memory when closed | That is **Keep ready in the background** (about 20–30 MB). Turn it off in DeskDrawer's settings. |
| A DeskTune change broke something | Flip that item back and press Apply, or press **Undo all**. A restore point was made before the first administrator change. |
| DeskTune items are greyed out | They need administrator rights: press **Unlock services**. |
| Update now is missing | The apps only offer a version built later than the one you run. **Check now** in settings looks again. |

### Where the files are

| What | Where |
| --- | --- |
| DeskBar settings and log | `%LOCALAPPDATA%\DeskBar3` (`settings.json`, `deskbar3.log`, and `stuck.txt` after a freeze) |
| DeskDrawer settings | `%LOCALAPPDATA%\DeskDrawer\settings.json` |
| DeskTune change log | `%LOCALAPPDATA%\DeskTune\changes.json` |
| The suite's shared colour | `%LOCALAPPDATA%\Desk\theme.txt` |
| Saved settings for a new PC | `Documents\Desk settings` |
| Installed apps | `%LOCALAPPDATA%\Programs` |

### Reporting a bug

Turn on Desk settings > Start-up and updates > **Report a bug**. Then right-click the bar > **Report a bug to Claude**, type what happened and press **Send**. It adds DeskBar's version, your Windows version, your screen setup and the recent log. Nothing is sent until you press Send.

Please say which app and version, what you did, what you expected, and what happened instead.

### Self-test

Each app can check itself. Run it with `--selftest report.txt` (for example `DeskBar.exe --selftest report.txt`) and read the report. Every line should pass.
