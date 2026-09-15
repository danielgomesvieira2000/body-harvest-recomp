<#
    Run the port with a dump and grab its window every -Interval seconds.

    powershell -ExecutionPolicy Bypass -File tools/shoot_run.ps1 [-Count 10] [-Interval 4]
        [-OutDir shots\run] [-Exe build\body-harvest-recomp.exe] [-Rom rom.z64]

    Environment variables set in the calling shell are inherited (BH_INPUT_SCRIPT,
    BH_DEBUG_LOADS, ...). The log goes to <OutDir>\run.log(.err). The window is
    grabbed by process id (tools/grab_window.ps1), so it must be on screen and not
    covered. The process is killed at the end.
#>
param(
    [int]$Count = 10,
    [double]$Interval = 4,
    [double]$Delay = 2,
    [string]$OutDir = "shots\run",
    [string]$Exe = "build\body-harvest-recomp.exe",
    [string]$Rom = "rom.z64"
)

New-Item -ItemType Directory -Force $OutDir | Out-Null
Get-ChildItem $OutDir -Filter *.png -ErrorAction SilentlyContinue | Remove-Item -Force
$log = Join-Path $OutDir "run.log"
$p = Start-Process -FilePath $Exe -ArgumentList $Rom -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
Start-Sleep -Seconds $Delay
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
for ($i = 1; $i -le $Count; $i++) {
    if ($p.HasExited) { "process exited with $($p.ExitCode) before shot $i"; break }
    $t = [Math]::Round($Delay + ($i - 1) * $Interval, 1)
    $png = Join-Path $OutDir ("shot{0:D2}_t{1}s.png" -f $i, $t)
    & powershell -ExecutionPolicy Bypass -File (Join-Path $here "grab_window.ps1") -Out $png -ProcId $p.Id | Out-Null
    Start-Sleep -Seconds $Interval
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Get-ChildItem $OutDir -Filter *.png | Select-Object Name, Length
