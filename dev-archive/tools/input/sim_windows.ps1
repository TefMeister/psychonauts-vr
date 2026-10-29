# 2026-10-29: with PSYVR_OPENXR=1 and the OpenXR Simulator, the simulator's preview window lives in the
# game's own process. While it is up it takes keyboard focus back, so keys sent to the game are lost.
# This puts the game window in front and minimises the preview without activating it.
# The simulator's screenshot tool still works with the preview minimised.
#   .\sim_windows.ps1            game in front, preview minimised
#   .\sim_windows.ps1 -Restore   both shown again
param([switch]$Restore)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class PsySimWin {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
    public static IntPtr FindPrefix(string prefix) { return Find(prefix, false); }
    public static IntPtr FindExact(string title) { return Find(title, true); }
    static IntPtr Find(string text, bool exact) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            var sb = new System.Text.StringBuilder(256);
            GetWindowTextW(h, sb, 256);
            string t = sb.ToString();
            if (exact ? t == text : t.StartsWith(text)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

$game = [PsySimWin]::FindExact("Psychonauts")
$sim = [PsySimWin]::FindPrefix("OpenXR Simulator")
if ($Restore) {
    if ($sim -ne [IntPtr]::Zero) { [PsySimWin]::ShowWindow($sim, 9) | Out-Null }
    if ($game -ne [IntPtr]::Zero) { [PsySimWin]::ShowWindow($game, 9) | Out-Null }
    "restored"
    return
}
if ($game -ne [IntPtr]::Zero) { [PsySimWin]::ShowWindow($game, 9) | Out-Null }   # SW_RESTORE
if ($sim -ne [IntPtr]::Zero) { [PsySimWin]::ShowWindow($sim, 7) | Out-Null }     # SW_SHOWMINNOACTIVE
if ($game -ne [IntPtr]::Zero) { [PsySimWin]::SetForegroundWindow($game) | Out-Null }
"game window {0}, simulator preview {1}" -f ($(if ($game -ne [IntPtr]::Zero) { "in front" } else { "NOT FOUND" })), ($(if ($sim -ne [IntPtr]::Zero) { "minimised" } else { "not open" }))
