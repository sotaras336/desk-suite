; Desk-Setup.exe: one installer for DeskBar, DeskDrawer and DeskTune.
;
; Each app already installs itself (copy to %LOCALAPPDATA%\Programs\<app>, Start menu shortcuts, its own "Uninstall"), and each updates itself from
; the same GitHub release. So this installer only carries the three setup files and runs each one's own install, the same way the website downloads do.
; Nothing needs administrator rights. Build: ISCC.exe /DAppVersion=4.0 /DFiles=<folder with the three *-Setup.exe> Desk.iss
;
; Every app keeps its own uninstall (Start > <app> > Uninstall); this installer adds no second one that could fall out of step with them.

#ifndef AppVersion
  #define AppVersion "4.0"
#endif
#ifndef Files
  #define Files "..\out"
#endif

[Setup]
AppId={{7E0C4C7B-3D5E-4D0A-9B8E-DE5C0D5E0400}
AppName=Desk
AppVersion={#AppVersion}
AppPublisher=sotaras336
AppPublisherURL=https://github.com/sotaras336/desk-suite
DefaultDirName={localappdata}\Programs\Desk
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
Uninstallable=no
CreateAppDir=no
OutputDir={#Files}
OutputBaseFilename=Desk-Setup
SetupIconFile=..\src\DeskBar\src\DeskBar.ico
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
MinVersion=10.0
CloseApplications=no

[Messages]
WelcomeLabel2=This installs the Desk apps you pick for your account: DeskBar (taskbar, Start and Quick Settings), DeskDrawer (files) and DeskTune (makes Windows lighter).%n%nNo administrator rights are needed. Each app updates itself, and each can be removed on its own from the Start menu.

[Types]
Name: "all"; Description: "All three apps"
Name: "custom"; Description: "Pick the apps"; Flags: iscustom

[Components]
Name: "deskbar"; Description: "DeskBar: taskbar, Start menu and Quick Settings (DeskMode built in)"; Types: all
Name: "deskdrawer"; Description: "DeskDrawer: a file manager with tabs, preview and quick look"; Types: all
Name: "desktune"; Description: "DeskTune: makes Windows lighter, every change can be undone"; Types: all

[Files]
Source: "{#Files}\DeskBar-Setup.exe"; DestDir: "{tmp}"; Components: deskbar; Flags: deleteafterinstall
Source: "{#Files}\DeskDrawer-Setup.exe"; DestDir: "{tmp}"; Components: deskdrawer; Flags: deleteafterinstall
Source: "{#Files}\DeskTune-Setup.exe"; DestDir: "{tmp}"; Components: desktune; Flags: deleteafterinstall

[Run]
; Each file keeps "Setup" in its name, which is what makes it install itself. DeskBar: --yes = no questions, it stops any running DeskBar, installs and starts.
; DeskDrawer / DeskTune: --nostart = install only; the last page offers to open them.
Filename: "{tmp}\DeskBar-Setup.exe"; Parameters: "--yes"; StatusMsg: "Installing DeskBar..."; Components: deskbar; Flags: waituntilterminated
Filename: "{tmp}\DeskDrawer-Setup.exe"; Parameters: "--nostart"; StatusMsg: "Installing DeskDrawer..."; Components: deskdrawer; Flags: waituntilterminated
Filename: "{tmp}\DeskTune-Setup.exe"; Parameters: "--nostart"; StatusMsg: "Installing DeskTune..."; Components: desktune; Flags: waituntilterminated
Filename: "{localappdata}\Programs\DeskDrawer\DeskDrawer.exe"; Description: "Open DeskDrawer"; Components: deskdrawer; Flags: postinstall nowait skipifsilent skipifdoesntexist; Check: not NeedsDotNet
Filename: "{localappdata}\Programs\DeskTune\DeskTune.exe"; Description: "Open DeskTune"; Components: desktune; Flags: postinstall nowait skipifsilent skipifdoesntexist unchecked; Check: not NeedsDotNet
Filename: "https://dotnet.microsoft.com/download/dotnet/10.0"; Description: "Get Microsoft's free .NET 10 Desktop Runtime (DeskDrawer and DeskTune need it)"; Flags: postinstall shellexec nowait skipifsilent; Check: NeedsDotNet

[Code]
function FindFirstRuntime(const Pattern: String): Boolean;
var F: TFindRec;
begin
  Result := FindFirst(Pattern, F);
  if Result then FindClose(F);
end;

// DeskDrawer and DeskTune need the .NET 10 Desktop Runtime (x64). DeskBar needs nothing. Installed runtimes are listed here, one value per version.
function HasDesktopRuntime10: Boolean;
var Names: TArrayOfString; I: Integer;
begin
  Result := False;
  if RegGetValueNames(HKLM64, 'SOFTWARE\dotnet\Setup\InstalledVersions\x64\sharedfx\Microsoft.WindowsDesktop.App', Names) then
    for I := 0 to GetArrayLength(Names) - 1 do
      if Copy(Names[I], 1, 3) = '10.' then Result := True;
  // a runtime installed without the setup entries (zip, or a per-user install) still shows up as a folder
  if not Result then Result := DirExists(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.WindowsDesktop.App')) and
    (FindFirstRuntime(ExpandConstant('{commonpf64}\dotnet\shared\Microsoft.WindowsDesktop.App\10.*')));
end;

function NeedsDotNet: Boolean;
begin
  Result := (WizardIsComponentSelected('deskdrawer') or WizardIsComponentSelected('desktune')) and not HasDesktopRuntime10;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and NeedsDotNet then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'DeskDrawer and DeskTune need Microsoft''s free .NET 10 Desktop Runtime (x64), which this PC doesn''t have yet. Leave the box below ticked to open the download page.';
end;
