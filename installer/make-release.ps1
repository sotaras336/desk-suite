# Makes the files of a Desk release in one folder:
#   DeskBar-Setup.exe, DeskDrawer-Setup.exe, DeskTune-Setup.exe   (each app installs itself when its file name has "Setup" in it)
#   Desk-Setup.exe                                                (one installer for all three; needs Inno Setup 6)
#   SHA256SUMS.txt                                                (what every app's update check reads)
#
# SHA256SUMS.txt also says when each app file was built ("# built DeskBar-Setup.exe 20261125T101500Z"). The apps only offer an update when the release
# was built later than the copy they are, so a newer build on this PC is never "updated" back to an older release. Older apps skip these lines.
#
# Usage (from anywhere):
#   .\make-release.ps1 -Version 4.0                       builds all three from this repo's src folder into .\out
#   .\make-release.ps1 -DeskBarSrc C:\...\DeskBar3 -DeskDrawerSrc C:\...\DeskDrawer -DeskTuneSrc C:\...\DeskTune -Out C:\...\release-4.0
param(
    [string]$Version = "4.0",
    [string]$DeskBarSrc = "$PSScriptRoot\..\src\DeskBar",       # the folder with build.ps1
    [string]$DeskDrawerSrc = "$PSScriptRoot\..\src\DeskDrawer",
    [string]$DeskTuneSrc = "$PSScriptRoot\..\src\DeskTune",
    [string]$DeskBarExe = "",                                    # an already built DeskBar exe to use instead of building one
    [string]$Out = "$PSScriptRoot\out",
    [switch]$NoInstaller                                         # skip Desk-Setup.exe (no Inno Setup here)
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')

# --- DeskBar: native, so its build time is the link time in its own header (what DeskBar reads about itself) ---
if (-not $DeskBarExe) {
    & "$DeskBarSrc\build.ps1" -Out "$Out\deskbar-build"
    if ($LASTEXITCODE -ne 0) { throw "DeskBar build failed" }
    $DeskBarExe = "$Out\deskbar-build\DeskBar_blue_BUILD.exe"
}
Copy-Item $DeskBarExe "$Out\DeskBar-Setup.exe" -Force
function PeStamp([string]$path) {
    $b = [IO.File]::ReadAllBytes($path); $pe = [BitConverter]::ToInt32($b, 0x3C)
    $secs = [BitConverter]::ToUInt32($b, $pe + 8)
    [DateTimeOffset]::FromUnixTimeSeconds($secs).UtcDateTime.ToString('yyyyMMddTHHmmssZ')
}
$built = [ordered]@{ 'DeskBar-Setup.exe' = (PeStamp "$Out\DeskBar-Setup.exe") }

# --- DeskDrawer and DeskTune: one exe each, needs the .NET 10 Desktop Runtime; the build time is stamped in (-p:DeskBuilt) ---
foreach ($app in @(@{ Name = 'DeskDrawer'; Src = $DeskDrawerSrc }, @{ Name = 'DeskTune'; Src = $DeskTuneSrc })) {
    $pub = "$Out\$($app.Name)-publish"
    dotnet publish "$($app.Src)\$($app.Name).csproj" -c Release -r win-x64 --self-contained false -p:PublishSingleFile=true -p:DeskBuilt=$stamp -o $pub --nologo -v quiet
    if ($LASTEXITCODE -ne 0) { throw "$($app.Name) publish failed" }
    Copy-Item "$pub\$($app.Name).exe" "$Out\$($app.Name)-Setup.exe" -Force
    $built["$($app.Name)-Setup.exe"] = $stamp
}

# --- Desk-Setup.exe ---
if (-not $NoInstaller) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { $c = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($c) { $iscc = $c.Source } }
    if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup), or run with -NoInstaller" }
    & $iscc /Q "/DAppVersion=$Version" "/DFiles=$Out" "$PSScriptRoot\Desk.iss"
    if ($LASTEXITCODE -ne 0) { throw "Desk-Setup.exe build failed" }
}

# --- SHA256SUMS.txt: "hash *name" lines (as before), then "# built" lines ---
$names = @('DeskBar-Setup.exe', 'DeskDrawer-Setup.exe', 'DeskTune-Setup.exe', 'Desk-Setup.exe') | Where-Object { Test-Path "$Out\$_" }
$lines = foreach ($n in $names) { "{0} *{1}" -f (Get-FileHash "$Out\$n" -Algorithm SHA256).Hash.ToLowerInvariant(), $n }
$lines += foreach ($k in $built.Keys) { "# built $k $($built[$k])" }
[IO.File]::WriteAllText("$Out\SHA256SUMS.txt", ($lines -join "`n") + "`n")   # plain LF, no BOM: the apps read it byte for byte
Get-Content "$Out\SHA256SUMS.txt" | ForEach-Object { Write-Host $_ }
Write-Host "OK -> $Out" -ForegroundColor Green
