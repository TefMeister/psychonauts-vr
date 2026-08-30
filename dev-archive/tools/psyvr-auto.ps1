# tools/psyvr-auto.ps1
#
# Drives the notes/67 automation harness from outside the game: appends
# commands to the drop-file the proxy polls, and tails the proxy log.
# Requires the game to have been launched with PSYVR_AUTOMATION=1
# (Launch-Psychonauts-VR-Automation.bat does that).
#
#   .\psyvr-auto.ps1 -Send 'status'
#   .\psyvr-auto.ps1 -Send 'level CAJA'
#   .\psyvr-auto.ps1 -Send 'cammove 0 0 200' -Tail 10
#   .\psyvr-auto.ps1 -Tail 30
#
# Commands the harness understands:
#   status                    - log the current camera position
#   level <CODE>              - SetPendingLevel (e.g. CAJA = Sasha's Lab)
#   campos <x> <y> <z>        - set camera position absolutely
#   cammove <dx> <dy> <dz>    - move camera relative to where it is now
#   camhold <0|1>             - re-apply the last target every frame
#   flag <id> <0|1>           - poke a debug-menu flag byte

param(
    [string]$GameDir = "D:\Program Files (x86)\Steam\steamapps\common\Psychonauts",
    [string[]]$Send,
    [int]$Tail = 0
)

$cmdFile = Join-Path $GameDir "psyvr_automation_cmds.txt"

# GetLogPath() writes to %TEMP%\psychonautsvr_proxy.log, falling back to the
# DLL's own directory when GetTempPath fails.
$logCandidates = @(
    (Join-Path $env:TEMP "psychonautsvr_proxy.log"),
    (Join-Path $GameDir "psychonautsvr_proxy.log")
)
$log = $logCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if ($Send) {
    if (-not (Test-Path $cmdFile)) {
        "WARNING: $cmdFile does not exist yet - is the game running with PSYVR_AUTOMATION=1?"
    }
    Add-Content -Path $cmdFile -Value $Send -Encoding ASCII
    "sent $($Send.Count) command(s):"
    foreach ($c in $Send) { "  $c" }
}

if ($Tail -gt 0) {
    if ($log) {
        "--- last $Tail line(s) of $log ---"
        Get-Content $log -Tail $Tail
    } else {
        "no proxy log found yet (looked in the game dir and %TEMP%)"
    }
}
