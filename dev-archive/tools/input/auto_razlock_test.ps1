# notes/53: real-gameplay Raz-lock reliability capture. Needs actual gameplay (Raz genuinely
# rendered/animated in a level), not the title screen - so this one DOES drive enter_gameplay.ps1
# (focus-steal, ~15-20s). Silence -> launch off-screen -> enter_gameplay -> wait, collecting
# RAZLOCK: stat lines -> kill -> restore, all in try/finally.
$ErrorActionPreference = "Stop"
$root = "C:\Users\Tefa\Documents\PsychonautsVR"
$gameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts"
$log = Join-Path $env:TEMP "psychonautsvr_proxy.log"

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\silence-intro-videos.ps1")
Write-Output "intro videos silenced"

try {
    if (Test-Path $log) { Remove-Item $log -Force }
    $env:PSYVR_FIRST_PERSON = "1"
    $env:PSYVR_FP_FORCE_ACTIVE = "1"
    $env:PSYVR_RAZLOCK_STATS = "1"
    $proc = Start-Process -FilePath (Join-Path $gameDir "Psychonauts.exe") -WorkingDirectory $gameDir -PassThru
    Remove-Item Env:\PSYVR_FIRST_PERSON, Env:\PSYVR_FP_FORCE_ACTIVE, Env:\PSYVR_RAZLOCK_STATS

    # notes/53 root cause: the two prior failures were NOT a timing/off-screen issue - they were
    # Windows' foreground-lock security policy. A freshly-launched process gets a brief, one-shot
    # allowance to call SetForegroundWindow that expires quickly (a longer pre-input delay makes
    # this WORSE, not better - it just lets the allowance expire before the first send). The correct
    # fix is AllowSetForegroundWindow(pid), which explicitly and durably grants that process
    # permission, bypassing the lock's timeout entirely.
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class FgLock { [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int dwProcessId); }
"@
    Start-Sleep -Seconds 3   # let the process spin up enough to have a window
    [FgLock]::AllowSetForegroundWindow($proc.Id) | Out-Null
    Write-Output "AllowSetForegroundWindow granted to pid $($proc.Id)"

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\input\enter_gameplay.ps1")

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\move-window-offscreen.ps1") -TimeoutSeconds 10

    Write-Output "waiting 18s in gameplay to accumulate RAZLOCK samples..."
    Start-Sleep -Seconds 18

    if ((Test-Path $log) -and (Select-String -Path $log -Pattern "RAZLOCK:" -Quiet)) {
        Write-Output "CAPTURED: RAZLOCK lines found."
        Get-Content $log | Select-String "RAZLOCK:"
    } else {
        Write-Output "NOT CAPTURED - likely enter_gameplay drifted and never reached real gameplay. Log tail:"
        if (Test-Path $log) { Get-Content $log -Tail 30 }
    }
}
finally {
    Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Stop-Process -Force
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\restore-intro-videos.ps1")
    Write-Output "game closed, intro videos restored - ALL CLEAR, keyboard/mouse free"
}
