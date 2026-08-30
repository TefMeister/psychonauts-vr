# notes/52: fully autonomous capture for the shader-identity diagnostic (playbook Phase 3.3).
# Silences intro audio, launches the game off-screen with PSYVR_SHADER_DUMP=1, drives it to real
# gameplay via the notes/38-39 verified enter_gameplay.ps1 sequence, polls the proxy log for the
# SHADERDUMP line (fires once a skinned/c96 draw is seen = definitely in gameplay), then kills the
# process and restores the intro videos - all in a try/finally so cleanup happens even on error.
$ErrorActionPreference = "Stop"
$root = "C:\Users\Tefa\Documents\PsychonautsVR"
$gameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts"
$log = Join-Path $env:TEMP "psychonautsvr_proxy.log"

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\silence-intro-videos.ps1")
Write-Output "intro videos silenced"

try {
    if (Test-Path $log) { Remove-Item $log -Force }
    $env:PSYVR_SHADER_DUMP = "1"
    Start-Process -FilePath (Join-Path $gameDir "Psychonauts.exe") -WorkingDirectory $gameDir
    Remove-Item Env:\PSYVR_SHADER_DUMP

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\move-window-offscreen.ps1") -TimeoutSeconds 20
    Start-Sleep -Seconds 3   # let the title screen finish loading before driving input

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\input\enter_gameplay.ps1")

    $deadline = (Get-Date).AddSeconds(30)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern "SHADERDUMP:" -Quiet)) { $found = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if ($found) {
        Write-Output "CAPTURED: SHADERDUMP line found in the log."
    } else {
        Write-Output "NOT CAPTURED within 30s - likely the enter_gameplay door-entry drifted (known issue, notes/43). Log tail follows for diagnosis:"
        if (Test-Path $log) { Get-Content $log -Tail 20 }
    }
}
finally {
    Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Stop-Process -Force
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\restore-intro-videos.ps1")
    Write-Output "game closed, intro videos restored - ALL CLEAR, keyboard/mouse free"
}
