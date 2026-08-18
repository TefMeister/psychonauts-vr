# notes/38: synthetic keyboard input into Psychonauts - the notes/15-18 input blocker, solved.
# The game's DirectInput polling reads the REAL OS input stack, so plain SendInput with
# SCANCODE events works - the only requirements are (1) the game window genuinely foreground
# and (2) nobody touching the physical keyboard/mouse during the send (focus-warning protocol).
#
# Usage: powershell -File send_key.ps1 -Scan 0x39            # SPACE
#        powershell -File send_key.ps1 -Scan 0xCD -Extended  # RIGHT arrow
#        powershell -File send_key.ps1 -Scan 0x1C            # ENTER
# Common DIK scan codes: SPACE=0x39 ENTER=0x1C ESC=0x01 UP=0xC8 DOWN=0xD0 LEFT=0xCB RIGHT=0xCD
# (arrows are EXTENDED keys - pass -Extended or DirectInput sees the numpad variants)
param(
    [Parameter(Mandatory=$true)] [int]$Scan,
    [switch]$Extended,
    [int]$HoldMs = 120,
    [string]$ProcessName = "Psychonauts"
)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class PsySendKey {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KEYBDINPUT ki; public ulong pad; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
    public static uint Key(ushort scan, uint extended, bool up) {
        INPUT[] inp = new INPUT[1];
        inp[0].type = 1; // INPUT_KEYBOARD
        inp[0].ki.wScan = scan;
        inp[0].ki.dwFlags = (uint)(8 | extended | (up ? 2u : 0u)); // SCANCODE | EXTENDEDKEY? | KEYUP?
        return SendInput(1, inp, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@

$p = Get-Process -Name $ProcessName -ErrorAction Stop
if ([PsySendKey]::GetForegroundWindow() -ne $p.MainWindowHandle) {
    [PsySendKey]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 500
}
if ([PsySendKey]::GetForegroundWindow() -ne $p.MainWindowHandle) {
    throw "could not bring $ProcessName to foreground - aborting (key would land elsewhere)"
}
$ext = 0; if ($Extended) { $ext = 1 }
[PsySendKey]::Key($Scan, $ext, $false) | Out-Null
Start-Sleep -Milliseconds $HoldMs
[PsySendKey]::Key($Scan, $ext, $true) | Out-Null
"sent scan=0x{0:X2} extended={1} hold={2}ms to {3}" -f $Scan, [bool]$Extended, $HoldMs, $ProcessName
