# Desk

A lighter Windows desktop: four small, free apps for Windows 10 and 11.

| App | Replaces | Size |
|---|---|---|
| **DeskBar** | The taskbar, Start and Quick Settings | 1.7 MB |
| **DeskDrawer** | File Explorer | 1.1 MB |
| **DeskTune** | Nothing: it switches off what you don't use, and can undo every change | 0.5 MB |
| **DeskMode** | Nothing: it sets refresh rate, power mode and brightness for your screens, and switches by itself when you dock | 1.3 MB |

Website: the `docs/` folder, published with GitHub Pages.

## Download and install

1. Download `DeskBar-Setup.exe`, `DeskDrawer-Setup.exe`, `DeskTune-Setup.exe` or `DeskMode-Setup.exe` from the latest release.
2. Run it. Windows may say "Windows protected your PC" because the apps aren't code-signed; choose **More info**, then **Run anyway**.
3. It installs for your account only (no administrator rights), adds a Start menu entry and opens.

DeskDrawer, DeskTune and DeskMode need Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0). DeskBar needs nothing extra.

Check a download against `SHA256SUMS.txt` in the release:

```powershell
Get-FileHash .\DeskTune-Setup.exe -Algorithm SHA256
```

## Uninstall

Start menu, then **Uninstall DeskBar**, **Uninstall DeskDrawer**, **Uninstall DeskTune** or **Uninstall DeskMode**.

- DeskBar gives the taskbar back to Windows.
- DeskDrawer gives folders and Win+E back to File Explorer first.
- DeskTune leaves the changes you applied in place. Use **Undo all** in DeskTune before uninstalling if you want Windows exactly as it was.

## License

MIT: see `LICENSE`.

## Privacy

None of the apps have accounts, telemetry or ads, and none sends anything about you or your PC. The one thing each app does by itself is look, about once a day, at `SHA256SUMS.txt` in this repository's newest release to see whether a newer version is out (GitHub sees an ordinary download request). A new version is only installed when you press **Update now**; it is downloaded, checked against that checksum and then run. Switch the daily look off in each app's settings. The only web pages the apps open are ones you click, such as a Wi-Fi sign-in page or a Microsoft Store page to reinstall an app.

## Build it yourself

The code is in `src/`.

- **DeskBar** (C++, Win32 + Direct2D): needs Visual Studio 2022 or its Build Tools with "Desktop development with C++". Run `src\DeskBar\build.ps1`; the exe lands in `src\DeskBar\out`.
- **DeskDrawer**, **DeskTune** and **DeskMode** (C#, WPF): need the .NET 10 SDK. In `src\DeskDrawer`, `src\DeskTune` or `src\DeskMode`, run `dotnet publish -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=none -o app`.

Each app checks itself with `--selftest report.txt`.

An exe whose file name contains "setup" installs itself to `%LOCALAPPDATA%\Programs\<App>` when run; that's how the release files work.

## What each app changes

- **DeskBar** hides Windows' taskbar while it runs and restores it when it quits. Settings live in `%LOCALAPPDATA%\DeskBar3`.
- **DeskDrawer** only answers other apps' Open and Save windows if you turn on "Use DeskDrawer for other apps' Open and Save windows": it then reads that window's folder, file name and file types, hides it, and types your answer into it. Nothing leaves your PC; switched off, it does nothing.
- **DeskDrawer** only opens your folders and Win+E if you turn on "Open folders with DeskDrawer"; that writes per-user entries under `HKEY_CURRENT_USER\Software\Classes` and removes exactly those when switched off. Closing its window keeps it ready in the background (about 20–30 MB of memory), and it starts hidden when you sign in (a `DeskDrawer` value under `HKEY_CURRENT_USER\...\CurrentVersion\Run`), so folders open in about half a second instead of two or three; switch this off with ⋯ > "Keep ready in the background". Settings live in `%LOCALAPPDATA%\DeskDrawer`.
- **DeskMode** changes refresh rates, the power mode and Energy saver only when you press a preset, or by itself when the monitor you chose is connected and the PC is plugged in (switch that off in Settings); it remembers what was set before and puts it back. Settings live in `%LOCALAPPDATA%\DeskMode`. It starts with Windows if you leave that on.
- **DeskTune** changes only what you tick. Each change, and what was there before it, is recorded in `%LOCALAPPDATA%\DeskTune\changes.json`, so it can be undone even after a restart. It makes a Windows restore point before the first change that needs administrator rights. It never touches Windows Update, Defender, the page file, SysMain or system files.
