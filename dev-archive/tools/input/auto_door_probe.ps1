# notes/53: visual probe for tuning enter_gameplay.ps1's door-entry walk. Launches, drives the
# same sequence, then dumps an eye frame immediately after so we can SEE where the character
# landed relative to the door, instead of guessing at timing adjustments blindly.
$ErrorActionPreference = "Stop"
$root = "C:\Users\Tefa\Documents\PsychonautsVR"
$gameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts"
$log = Join-Path $env:TEMP "psychonautsvr_proxy.log"

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\silence-intro-videos.ps1")
Write-Output "intro videos silenced"

try {
    if (Test-Path $log) { Remove-Item $log -Force }
    Remove-Item (Join-Path $env:TEMP "psyvr_eye1.bmp") -ErrorAction SilentlyContinue
    $env:PSYVR_DUMP_EYES = "1"
    $proc = Start-Process -FilePath (Join-Path $gameDir "Psychonauts.exe") -WorkingDirectory $gameDir -PassThru
    Remove-Item Env:\PSYVR_DUMP_EYES

    Start-Sleep -Seconds 4   # let the process actually create its window before driving input

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\input\enter_gameplay.ps1")

    Write-Output "waiting for an eye dump to land..."
    $deadline = (Get-Date).AddSeconds(10)
    $eyePath = Join-Path $env:TEMP "psyvr_eye1.bmp"
    while ((Get-Date) -lt $deadline -and -not (Test-Path $eyePath)) { Start-Sleep -Milliseconds 500 }
    if (Test-Path $eyePath) { Write-Output "CAPTURED: $eyePath" } else { Write-Output "NO EYE DUMP LANDED" }
}
finally {
    Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Stop-Process -Force
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\restore-intro-videos.ps1")
    Write-Output "game closed, intro videos restored - ALL CLEAR, keyboard/mouse free"
}
