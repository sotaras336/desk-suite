# Build DeskBar3 (native C++ / Direct2D). Usage: .\build.ps1 [-Pink] [-Debug] [-Out dir]
param([switch]$Pink, [switch]$Debug, [string]$Out = "$PSScriptRoot\out")
$env:Path += ";${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vc = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$obj = "$PSScriptRoot\obj" + $(if ($Pink) { '-pink' } else { '' })
New-Item -ItemType Directory -Force $obj, $Out | Out-Null
Remove-Item "$obj\*.obj" -ErrorAction SilentlyContinue
$def = '/DNDEBUG'; $opt = '/O2 /Gy /Gw'; $lnk = '/OPT:REF /OPT:ICF'
if ($Debug) { $def = '/D_DEBUG'; $opt = '/Od'; $lnk = '' }
if ($Pink) { $def += ' /DPINK' }
$flags = "/nologo /std:c++20 /EHsc /MT /utf-8 /W3 /Zi /MP $def $opt /permissive- /Zc:__cplusplus /Fo`"$obj\\`" /Fd`"$obj\\vc.pdb`""
$libs = 'user32.lib gdi32.lib d2d1.lib dwrite.lib dwmapi.lib windowscodecs.lib ole32.lib oleaut32.lib shell32.lib shlwapi.lib advapi32.lib comctl32.lib uuid.lib propsys.lib uxtheme.lib psapi.lib winmm.lib setupapi.lib dxva2.lib windowsapp.lib runtimeobject.lib crypt32.lib bcrypt.lib iphlpapi.lib wlanapi.lib netapi32.lib secur32.lib version.lib wtsapi32.lib msimg32.lib shcore.lib cfgmgr32.lib powrprof.lib dbghelp.lib bthprops.lib'
$src = Get-ChildItem "$PSScriptRoot\src\*.cpp" | Where-Object { $_.Name -ne 'pch.cpp' } | ForEach-Object { "`"$($_.FullName)`"" }
$exe = "$Out\" + $(if ($Pink) { 'DeskBar_pink_BUILD.exe' } else { 'DeskBar_blue_BUILD.exe' })   # not "DeskBar.exe": only the installed copy is called that, so a search shows which is which
$cmd = "call `"$vc`" >nul && cd /d `"$PSScriptRoot\src`" && cl $flags /Yc`"pch.h`" /Fp`"$obj\\pch.pch`" /c pch.cpp && cl $flags /Yu`"pch.h`" /Fp`"$obj\\pch.pch`" /c $($src -join ' ') && rc /nologo /fo`"$obj\\app.res`" app.rc && link /nologo /OUT:`"$exe`" /SUBSYSTEM:WINDOWS /DEBUG /PDB:`"$Out\DeskBar.pdb`" /PDBALTPATH:DeskBar.pdb $lnk /MANIFEST:EMBED /MANIFESTINPUT:app.manifest `"$obj\\*.obj`" `"$obj\\app.res`" $libs"
$ErrorActionPreference = 'Continue'; cmd /c $cmd 2>&1 | Where-Object { $_ -notmatch '^\s*$' -and $_ -notmatch '^(pch|[A-Za-z0-9_]+)\.cpp$' }
if ($LASTEXITCODE -ne 0) { Write-Host "BUILD FAILED" -ForegroundColor Red; exit 1 }
Write-Host "OK -> $exe" -ForegroundColor Green


