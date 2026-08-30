# notes/52: isolated empirical test of the head-tracking composition order. No gameplay entry
# needed - the title/menu screen alone has 3D geometry + register-6 uploads, and g_fakePose runs a
# real T through the pipeline via the monitor-preview trigger. Silence -> launch off-screen -> wait
# -> poll log for HTDEBUG -> kill -> restore, all in try/finally.
$ErrorActionPreference = "Stop"
$root = "C:\Users\Tefa\Documents\PsychonautsVR"
$gameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts"
$log = Join-Path $env:TEMP "psychonautsvr_proxy.log"

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\silence-intro-videos.ps1")
Write-Output "intro videos silenced"

try {
    if (Test-Path $log) { Remove-Item $log -Force }
    $env:PSYVR_FIRST_PERSON = "1"
    $env:PSYVR_FAKE_POSE = "1"
    $env:PSYVR_HT_TEST_SHIFT = "500"
    $env:PSYVR_HT_DEBUG = "1"
    Start-Process -FilePath (Join-Path $gameDir "Psychonauts.exe") -WorkingDirectory $gameDir
    Remove-Item Env:\PSYVR_FIRST_PERSON, Env:\PSYVR_FAKE_POSE, Env:\PSYVR_HT_TEST_SHIFT, Env:\PSYVR_HT_DEBUG

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\move-window-offscreen.ps1") -TimeoutSeconds 20

    $deadline = (Get-Date).AddSeconds(20)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern "HTDEBUG:" -Quiet)) { $found = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if ($found) {
        Write-Output "CAPTURED: HTDEBUG lines found."
        Get-Content $log | Select-String "HTDEBUG:"
    } else {
        Write-Output "NOT CAPTURED within 20s. Log tail follows:"
        if (Test-Path $log) { Get-Content $log -Tail 25 }
    }
}
finally {
    Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Stop-Process -Force
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\restore-intro-videos.ps1")
    Write-Output "game closed, intro videos restored - ALL CLEAR, keyboard/mouse free"
}
