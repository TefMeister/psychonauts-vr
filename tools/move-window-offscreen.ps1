# Moves the Psychonauts window off any visible monitor so it never appears on screen,
# even though it keeps running/rendering/receiving input normally.
# Call this right after launching Psychonauts.exe (polls briefly for the window to exist).
param(
    [int]$TimeoutSeconds = 15
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class OffscreenWin {
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);
}
"@

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$moved = $false
while ((Get-Date) -lt $deadline -and -not $moved) {
    $proc = Get-Process -Name "Psychonauts" -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($proc -and $proc.MainWindowHandle -ne [IntPtr]::Zero) {
        # SWP_NOSIZE=0x1, SWP_NOZORDER=0x4, SWP_NOACTIVATE=0x10 -- move only, don't resize/reorder/steal focus
        [OffscreenWin]::SetWindowPos($proc.MainWindowHandle, [IntPtr]::Zero, -32000, 0, 0, 0, 0x15) | Out-Null
        $moved = $true
        Write-Output "MOVED: Psychonauts window relocated off-screen (handle $($proc.MainWindowHandle))."
    } else {
        Start-Sleep -Milliseconds 300
    }
}
if (-not $moved) { Write-Output "TIMEOUT: no Psychonauts window handle appeared within $TimeoutSeconds s." }
