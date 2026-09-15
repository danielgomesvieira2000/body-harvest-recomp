<#
    Boot the port N times with a dump and tabulate what each run reached.

    Phase 03's gate is observable only from the log: the runtime reaching
    recomp_entrypoint and creating game threads, with no lookup miss or crash.
    Each run is killed after -Seconds; logs are kept in -OutDir.

    powershell -ExecutionPolicy Bypass -File tools/boot_runs.ps1 [-Runs 10] [-Seconds 12]
        [-Exe build\body-harvest-recomp.exe] [-Rom rom.z64] [-OutDir <dir>] [-Env "BH_DEBUG_LOADS=1"]
#>
param(
    [int]$Runs = 10,
    [int]$Seconds = 12,
    [string]$Exe = "build\body-harvest-recomp.exe",
    [string]$Rom = "rom.z64",
    [string]$OutDir = "$env:TEMP\bh_boot_runs",
    [string[]]$Env = @()
)

New-Item -ItemType Directory -Force $OutDir | Out-Null
$saved = @{}
foreach ($pair in $Env) {
    $name, $value = $pair -split '=', 2
    $saved[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, $value)
}

$rows = @()
try {
    for ($i = 1; $i -le $Runs; $i++) {
        $out = Join-Path $OutDir "run$i.log"
        $err = "$out.err"
        $p = Start-Process -FilePath $Exe -ArgumentList $Rom -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
        Start-Sleep -Seconds $Seconds
        $alive = -not $p.HasExited
        if ($alive) { Stop-Process -Id $p.Id -Force; Start-Sleep -Milliseconds 800 }
        $lines = @(Get-Content $out -ErrorAction SilentlyContinue) + @(Get-Content $err -ErrorAction SilentlyContinue)
        $text = $lines -join "`n"
        $screens = [regex]::Matches($text, 'update_screen #(\d+)') | Select-Object -Last 1
        $rows += [pscustomobject]@{
            run     = $i
            alive   = $alive
            exit    = if ($alive) { '' } else { $p.ExitCode }
            init    = ([regex]::Matches($text, 'entering recomp_entrypoint')).Count
            threads = ([regex]::Matches($text, 'game thread \d+ created')).Count
            loads   = ([regex]::Matches($text, '\[bh-load\]')).Count
            dls     = ([regex]::Matches($text, 'send_dl')).Count
            misses  = ([regex]::Matches($text, 'LOOKUP MISS|==== CRASH')).Count
            screens = if ($screens) { $screens.Groups[1].Value } else { '' }
        }
    }
}
finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}
$rows | Format-Table -AutoSize
"logs: $OutDir"
