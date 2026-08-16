# Proxy `d3d9.dll` — Build & Validation

Date: 2026-08-16. Goal: prove the standard dxwrapper/Helix-Mod-style D3D9 proxy-DLL injection
technique works for this game *before* writing any real stereo-rendering logic. No hooking of
the returned `IDirect3D9`/`IDirect3DDevice9` interfaces yet — just load-and-forward with logging.

## 1. Toolchain

No C/C++ compiler (`cl.exe`, `gcc`, `clang`) was present on this machine at the start of this
session. Installed **LLVM-MinGW (UCRT variant)** via winget:

```
winget install --source winget --id MartinStorsjo.LLVM-MinGW.UCRT --accept-package-agreements --accept-source-agreements
```

Chosen over "Build Tools for Visual Studio" because it's a single self-contained multi-target
(i686/x86_64/armv7/aarch64) clang distribution with no separate workload-selection step, and
over the 64-bit-only WinLibs winget packages because it explicitly ships an
`i686-w64-mingw32-clang.exe` target compiler, which is what's needed — Psychonauts.exe is 32-bit
(confirmed in the prior live-debug session), so the proxy DLL must be built 32-bit or the game
simply won't load it.

Installed to:
```
C:\Users\Tefa\AppData\Local\Microsoft\WinGet\Packages\MartinStorsjo.LLVM-MinGW.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\llvm-mingw-20260616-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe
```

**Gotcha**: the download+extract was very slow (~35-40 minutes total for a ~190MB zip containing
several thousand small files — almost certainly real-time antivirus scanning each extracted file
individually, not a network issue; the zip itself downloaded in a couple of minutes). Didn't
touch AV settings to speed this up since that wasn't asked for. Worth remembering for future
sessions: this install is now cached locally, no need to repeat it.

## 2. Proxy DLL

Source: `tools/proxy-d3d9/proxy_d3d9.c`, module-def file `tools/proxy-d3d9/proxy_d3d9.def`,
build script `tools/proxy-d3d9/build.ps1`.

Design:
- Exports exactly one function, `Direct3DCreate9(UINT SDKVersion)`, `WINAPI`/`__stdcall`,
  matching the game's only d3d9.dll import (confirmed via static recon in
  `notes/03-static-recon.md`).
- On first call, resolves the **real** system DLL via `GetSystemDirectoryA` +
  `"\\d3d9.dll"` (i.e. `C:\Windows\system32\d3d9.dll`), then `LoadLibraryA` on that full path —
  never a bare `LoadLibraryA("d3d9.dll")`, which could re-resolve back to this same proxy DLL
  once it's sitting in the game's own directory (the game directory is searched before
  `System32` under standard DLL search order).
- Forwards the call unmodified and returns the real `IDirect3D9*` to the game. No vtable
  patching, no interface wrapping.
- Logs every step (DLL attach/detach, real-DLL load, proc resolution, the call itself with its
  `SDKVersion` argument, and the returned pointer) with millisecond timestamps to
  `%TEMP%\psychonautsvr_proxy.log`, using a critical section for thread-safety (`Present` etc.
  are per-frame, so future logging on hotter paths needs to stay cheap/thread-safe from the
  start).
- Built via the `.def` file to guarantee the plain undecorated export name `Direct3DCreate9`
  (in addition to clang's own `Direct3DCreate9@4` stdcall-decorated alias) — confirmed via
  `llvm-objdump -p`, the export table lists both names at the same address.
- Build command (`build.ps1`): `i686-w64-mingw32-clang.exe --target=i686-w64-mingw32 -shared -O2
  -municode -o d3d9.dll proxy_d3d9.c proxy_d3d9.def -lgdi32 -luser32 -lkernel32`.

Verified before touching the game directory:
```
llvm-objdump -f d3d9.dll   → file format coff-i386, architecture: i386   (32-bit, correct)
llvm-objdump -p d3d9.dll   → Export Table: DLL name d3d9.dll
                                1  Direct3DCreate9
                                2  Direct3DCreate9@4
```

## 3. Live validation

Script: `tools/proxy-d3d9/validate.ps1`. Safety checks it performs: aborts if a `d3d9.dll`
already exists in the game directory (none did — confirmed clean beforehand), copies the
freshly-built proxy in, launches `Psychonauts.exe` directly via `Start-Process` (no debugger —
the task note about the first-chance-AV loop applies to launching *under x64dbg specifically*;
launching without any debugger attached, as done here, hit no such issue), polls for the log
file, prints it, then unconditionally cleans up (kills any `Psychonauts` process, deletes the
copied DLL) in a `finally` block.

**Result: full success.** Log contents from the actual run:

```
[2026-08-16 12:15:03.255] ==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_ATTACH (pid=12636) ====
[2026-08-16 12:15:05.635] Direct3DCreate9(SDKVersion=0x20) called - forwarding to real d3d9.dll
[2026-08-16 12:15:05.638] Loaded real d3d9.dll from "C:\Windows\system32\d3d9.dll" (hModule=0x73310000)
[2026-08-16 12:15:05.638] Resolved real Direct3DCreate9 at 0x73374B20
[2026-08-16 12:15:05.725] Real Direct3DCreate9 returned IDirect3D9* = 0x00B90DA0
```

Notable cross-validation with the prior x64dbg session (`notes/04-live-debug-findings.md`): that
session found `d3d9.dll!Direct3DCreate9` at module offset `d3d9.dll+0x64B20` via live
breakpointing. This session's proxy resolved the real function at `0x73374B20`, and
`0x73310000` (this run's `d3d9.dll` base, logged above) `+ 0x64B20 = 0x73374B20` — an exact
match against an independently-obtained offset, from a completely different mechanism (proxy
DLL forwarding vs. debugger breakpoint). Good confirmation both findings are solid. (Base
address differs run-to-run per ASLR/loader address-space layout, as expected — the *offset* is
what's stable and what matters.)

`SDKVersion=0x20` = `D3D_SDK_VERSION` (32), the standard value apps pass — unremarkable, just
confirms the argument decodes as expected.

## 4. Cleanup

Confirmed after the run:
- `d3d9.dll` no longer present in the game directory (`Test-Path` → `False`).
- No `Psychonauts` process left running (`Get-Process` → nothing).
- The built DLL remains only in `tools/proxy-d3d9/d3d9.dll` inside the workspace (gitignored?
  no — see below, currently tracked so the built artifact is in git history for this checkpoint;
  can be removed from tracking later if repo size becomes a concern, easy to rebuild via
  `build.ps1` regardless).

## 5. What this proves / next milestone

This closes the loop the prior session proposed: a plain, unmodified DLL named `d3d9.dll`
dropped into the game folder *is* loaded by the game in preference to the system one (standard
Windows DLL search order — application directory before `System32`), and the game calls into it
exactly as the static/live recon predicted, with no anti-tamper/integrity check rejecting it.
The injection mechanism itself is no longer a risk for this project.

**Next concrete milestone**: hook `IDirect3D9::CreateDevice` and `IDirect3DDevice9::Present` via
vtable patching, from inside this same proxy DLL, and log per-frame `Present` calls plus the
`D3DPRESENT_PARAMETERS` passed to `CreateDevice` (resolution, windowed/fullscreen, backbuffer
format) — still pure observation, no rendering changes. That sets up the actual infrastructure
(a live `IDirect3DDevice9*` handle and a hook point that fires every frame) needed before
attempting the real work: creating a second render target for the other eye and injecting
per-eye view/projection matrices at the `D3DXMatrixPerspectiveFovRH`/`D3DXMatrixLookAtRH` call
sites already located at `exe+0x292525`/`exe+0x2924B1` (per
`notes/04-live-debug-findings.md`) — the actual callers of those two call sites still need to be
disassembled with x64dbg to find where the per-eye matrix would need to be swapped in.
