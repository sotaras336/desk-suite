# Runs "<app>.exe --selftest <report>" (the apps are GUI programs, so wait for them explicitly),
# prints the report, and fails if the app exits non-zero, times out, or the report has a FAIL line.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$Report, [int]$TimeoutSec = 600)
New-Item -ItemType Directory -Force (Split-Path $Report) | Out-Null
Remove-Item $Report -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList '--selftest', "`"$Report`"" -PassThru
if (-not $p.WaitForExit($TimeoutSec * 1000)) { $p.Kill($true); Write-Host "::error::$(Split-Path $Exe -Leaf) self-test timed out after $TimeoutSec s"; exit 1 }
$code = $p.ExitCode
if (-not (Test-Path $Report)) { Write-Host "::error::no report written (exit code $code)"; exit 1 }
$lines = Get-Content $Report
$lines | ForEach-Object { Write-Host $_ }
$fails = @($lines | Where-Object { $_ -match '^FAIL' })
$fails | ForEach-Object { Write-Host "::error::$_" }
Write-Host ("{0} passed, {1} failed, exit code {2}" -f @($lines | Where-Object { $_ -match '^PASS' }).Count, $fails.Count, $code)
if ($code -ne 0 -or $fails.Count) { exit 1 }
