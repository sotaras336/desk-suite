# Desk

A lighter Windows desktop: three small, free apps for Windows 10 and 11.

| App | Replaces | Size |
|---|---|---|
| **DeskBar** | The taskbar, Start and Quick Settings | 1.7 MB |
| **DeskDrawer** | File Explorer | 0.8 MB |
| **DeskTune** | Nothing: it switches off what you don't use, and can undo every change | 0.5 MB |

Website: the `docs/` folder, published with GitHub Pages.

## Download and install

1. Download `DeskBar-Setup.exe`, `DeskDrawer-Setup.exe` or `DeskTune-Setup.exe` from the latest release.
2. Run it. Windows may say "Windows protected your PC" because the apps aren't code-signed; choose **More info**, then **Run anyway**.
3. It installs for your account only (no administrator rights), adds a Start menu entry and opens.

DeskDrawer and DeskTune need Microsoft's free [.NET 10 Desktop Runtime (x64)](https://dotnet.microsoft.com/download/dotnet/10.0). DeskBar needs nothing extra.

Check a download against `SHA256SUMS.txt` in the release:

```powershell
Get-FileHash .\DeskTune-Setup.exe -Algorithm SHA256
```

## Uninstall

Start menu, then **Uninstall DeskBar**, **Uninstall DeskDrawer** or **Uninstall DeskTune**.

- DeskBar gives the taskbar back to Windows.
- DeskDrawer gives folders and Win+E back to File Explorer first.
- DeskTune leaves the changes you applied in place. Use **Undo all** in DeskTune before uninstalling if you want Windows exactly as it was.

## License

MIT: see `LICENSE`.

## Privacy

None of the apps send data anywhere: no accounts, no telemetry, no ads. The only web pages they open are ones you click, such as a Wi-Fi sign-in page or a Microsoft Store page to reinstall an app.

## Build it yourself

The code is in `src/`.

- **DeskBar** (C++, Win32 + Direct2D): needs Visual Studio 2022 or its Build Tools with "Desktop development with C++". Run `src\DeskBar\build.ps1`; the exe lands in `src\DeskBar\out`.
- **DeskDrawer** and **DeskTune** (C#, WPF): need the .NET 10 SDK. In `src\DeskDrawer` or `src\DeskTune`, run `dotnet publish -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DebugType=none -o app`.

Each app checks itself with `--selftest report.txt`.

An exe whose file name contains "setup" installs itself to `%LOCALAPPDATA%\Programs\<App>` when run; that's how the release files work.

## What each app changes

- **DeskBar** hides Windows' taskbar while it runs and restores it when it quits. Settings live in `%LOCALAPPDATA%\DeskBar3`.
- **DeskDrawer** only opens your folders and Win+E if you turn on "Open folders with DeskDrawer"; that writes per-user entries under `HKEY_CURRENT_USER\Software\Classes` and removes exactly those when switched off. Closing its window keeps it ready in the background (about 20–30 MB of memory), and it starts hidden when you sign in (a `DeskDrawer` value under `HKEY_CURRENT_USER\...\CurrentVersion\Run`), so folders open in about half a second instead of two or three; switch this off with ⋯ > "Keep ready in the background". Settings live in `%LOCALAPPDATA%\DeskDrawer`.
- **DeskTune** changes only what you tick. Each change, and what was there before it, is recorded in `%LOCALAPPDATA%\DeskTune\changes.json`, so it can be undone even after a restart. It makes a Windows restore point before the first change that needs administrator rights. It never touches Windows Update, Defender, the page file, SysMain or system files.
