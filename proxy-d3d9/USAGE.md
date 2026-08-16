# Proxy `d3d9.dll` — usage

This is the first working component of the mod: a minimal logging proxy `d3d9.dll` that
confirms Psychonauts.exe can be hooked via the standard proxy-DLL injection technique
(the same mechanism dxwrapper and most D3D9 mods use). It loads the real system
`d3d9.dll`, forwards `Direct3DCreate9` unmodified, and logs every step. **It does not
change any game behavior** — no VR, no stereo rendering yet. This is purely a validated
foothold for the hooking work to come.

## What it proves

Dropping a DLL named `d3d9.dll` into the Psychonauts install directory causes the game
to load it in preference to the system one (standard Windows DLL search order), and the
game calls into its exported `Direct3DCreate9` exactly as expected, with no anti-tamper
pushback. See the [dev-archive](https://github.com/TefMeister/psychonauts-vr-dev-archive)
and [modding-notes](https://github.com/TefMeister/psychonauts-vr-modding-notes) repos for
the full validation log and cross-checks against independent debugger findings.

## How to use it (for testing only, not a VR fix)

1. Back up your existing `d3d9.dll` if one is already present in your Psychonauts install
   folder (there normally isn't one — the game uses the system copy).
2. Copy `d3d9.dll` from this folder into the Psychonauts install directory (next to
   `Psychonauts.exe`).
3. Launch the game normally.
4. Check `%TEMP%\psychonautsvr_proxy.log` for a line-by-line log proving the proxy loaded
   and intercepted `Direct3DCreate9`.
5. Remove the copied `d3d9.dll` when done — the game will fall back to the system DLL.

## Building from source

Requires a 32-bit-capable MinGW/clang toolchain (this project uses LLVM-MinGW,
`i686-w64-mingw32-clang`, since Psychonauts.exe is a 32-bit executable). Run
`build.ps1` in this folder; it searches common LLVM-MinGW winget install locations and
falls back to whatever `i686-w64-mingw32-clang.exe` is on `PATH`.

## Next milestone

Hook `IDirect3D9::CreateDevice` and `IDirect3DDevice9::Present` via vtable patching from
inside this same proxy, still observation-only, as the next step toward the real
per-eye stereo rendering work.
