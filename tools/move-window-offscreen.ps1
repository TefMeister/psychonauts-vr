# Moves the Psychonauts window (and, if present, x32dbg/x64dbg's own GUI window) off any
# visible monitor so nothing ever appears on screen, even though everything keeps
# running/rendering/receiving input/debugging normally.
# Call this right after launching Psychonauts.exe (polls briefly for the window to exist).
# Also safe to call standalone any time to sweep up a debugger window that appeared later
# (e.g. after attaching) -- pass -TargetNames to control what it looks for.
param(
    [int]$TimeoutSeconds = 15,
    [string[]]$TargetNames = @("Psychonauts","x32dbg","x64dbg")
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OffscreenWin {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
}
"@

function Move-OneOffscreen($name) {
    $proc = Get-Process -Name $name -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
        # SWP_NOSIZE=0x1, SWP_NOZORDER=0x4, SWP_NOACTIVATE=0x10 -- move only, don't resize/reorder/steal focus
        [OffscreenWin]::SetWindowPos($proc.MainWindowHandle, [IntPtr]::Zero, -32000, 0, 0, 0, 0x15) | Out-Null
        Write-Output "MOVED: $name window relocated off-screen (handle $($proc.MainWindowHandle))."
        return $true
    }
    return $false
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$pending = [System.Collections.Generic.HashSet[string]]::new([string[]]$TargetNames)
while ((Get-Date) -lt $deadline -and $pending.Count -gt 0) {
    foreach ($name in @($pending)) {
        if (Move-OneOffscreen $name) { $pending.Remove($name) | Out-Null }
    }
    if ($pending.Count -gt 0) { Start-Sleep -Milliseconds 300 }
}
if ($pending.Count -gt 0) { Write-Output "NOTE: no window found for: $($pending -join ', ') within $TimeoutSeconds s (fine if that process isn't running/hasn't opened a window yet -- e.g. call again after attaching a debugger)." }
