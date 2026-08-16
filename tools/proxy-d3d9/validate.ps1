# Validates the proxy d3d9.dll by copying it into the (read-only, not otherwise modified)
# Psychonauts game directory, launching the game, waiting briefly for the proxy to log
# Direct3DCreate9 being called, then cleaning up (removes the copied DLL, kills the game
# process). Does NOT modify or overwrite any existing game file.

$ErrorActionPreference = "Stop"

$gameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts"
$exePath = Join-Path $gameDir "Psychonauts.exe"
$proxyDll = Join-Path $PSScriptRoot "d3d9.dll"
$targetDll = Join-Path $gameDir "d3d9.dll"
$logPath = Join-Path $env:TEMP "psychonautsvr_proxy.log"

if (-not (Test-Path $proxyDll)) { throw "Proxy DLL not built yet: $proxyDll" }
if (Test-Path $targetDll) { throw "SAFETY ABORT: $targetDll already exists - refusing to overwrite." }

if (Test-Path $logPath) {
    Write-Host "Removing stale log file: $logPath"
    Remove-Item $logPath -Force
}

Write-Host "Copying proxy DLL: $proxyDll -> $targetDll"
Copy-Item $proxyDll $targetDll

try {
    Write-Host "Launching $exePath ..."
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $gameDir -PassThru

    $waited = 0
    $found = $false
    while ($waited -lt 20) {
        Start-Sleep -Seconds 2
        $waited += 2
        if (Test-Path $logPath) { $found = $true; break }
    }

    if ($found) {
        Start-Sleep -Seconds 2  # let a few more lines flush
        Write-Host "=== Log file found: $logPath ==="
        Get-Content $logPath
    } else {
        Write-Host "=== No log file appeared after $waited seconds ==="
    }
}
finally {
    Write-Host "Cleaning up: killing Psychonauts.exe if running, removing copied d3d9.dll"
    Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    if (Test-Path $targetDll) { Remove-Item $targetDll -Force }
    Write-Host "Done."
}
