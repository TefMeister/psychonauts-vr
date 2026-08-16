# CreateDevice / Present Vtable Hooks — Build & Validation

Date: 2026-08-16. Goal: extend the validated proxy `d3d9.dll` (`notes/05-proxy-dll-validation.md`)
to vtable-hook `IDirect3D9::CreateDevice` and `IDirect3DDevice9::Present`, still purely
observational — log data, call straight through to the real implementation, change nothing about
rendering behavior. This builds the actual infrastructure (a live device handle + a hook that
fires every frame) the eventual stereo-rendering work will need.

## 1. Vtable indices used, and how they were verified (not guessed)

| Interface method | Slot | Verified via |
|---|---|---|
| `IDirect3D9::CreateDevice` | **16** | (a) counting `STDMETHOD` entries in mingw-w64's `d3d9.h` `IDirect3D9Vtbl` (3 `IUnknown` slots + 13 more methods before `CreateDevice`); (b) independently, the prior live x64dbg session already read this exact slot out of live process memory and breakpointed it successfully (`notes/04-live-debug-findings.md`: "vtable slot 16 read live + breakpoint hit") |
| `IDirect3DDevice9::Present` | **17** | same two-source cross-check: header field order (3 `IUnknown` + 14 more methods before `Present`), and the live-debug session's "vtable slot 17 read live + ... breakpoint hit confirmed" |

Rather than hardcode raw pointer offsets (`((void**)vtbl)[16] = ...`), the code patches the named
struct fields of the real `d3d9.h` vtbl types directly — `This->lpVtbl->CreateDevice = Hook_...`
and `This->lpVtbl->Present = Hook_...`. This makes the compiler responsible for computing the
correct slot from the struct layout instead of manual arithmetic, which removes an entire class of
off-by-one-slot bugs (the exact bug the live-debug session hit and fixed in its own scripting, per
`04-live-debug-findings.md` §"CreateDevice call arguments"). mingw-w64's `d3d9.h` ships with the
LLVM-MinGW toolchain already installed (`.../llvm-mingw-20260616-ucrt-x86_64/include/d3d9.h`), so
no extra download was needed.

Both vtables are patched with a `VirtualProtect(PAGE_EXECUTE_READWRITE)` / restore-old-protection
pattern, since the real d3d9.dll's vtables normally live in read-only `.rdata`. Each hook install
is guarded by a one-shot boolean (`g_d3d9Hooked`, `g_deviceHooked`) since these vtables are shared,
static structures — re-patching on a hypothetical second `CreateDevice` call would be harmless but
pointless, so it's skipped.

## 2. Design

Source: `tools/proxy-d3d9/proxy_d3d9.c` (extends the existing file, not a new one).

- `Direct3DCreate9` forwards to the real DLL exactly as before, then calls
  `InstallCreateDeviceHook()` on the returned `IDirect3D9*`.
- `Hook_CreateDevice` logs `Adapter`, `DeviceType`, `hFocusWindow`, `BehaviorFlags`, and every field
  of `*pPresentationParameters`, then calls the real `CreateDevice` (saved as `g_pRealCreateDevice`)
  and logs the returned `HRESULT` + `IDirect3DDevice9*`. On success it calls
  `InstallPresentHook()` on the newly created device.
- `Hook_Present` increments an atomic frame counter (`InterlockedIncrement`) every call, but only
  writes a log line when at least 1000ms (via `GetTickCount`) have passed since the last log line —
  wall-clock throttling was chosen over "every Nth frame" so the log stays readable regardless of
  the game's actual framerate. It then calls the real `Present` (`g_pRealPresent`) and returns its
  result unmodified.
- All hook functions are declared `HRESULT STDMETHODCALLTYPE` (i.e. `__stdcall`) with an explicit
  `This` first parameter, matching COM's calling convention and avoiding the "forgot `this` is an
  explicit stack arg" mistake flagged in the prior live-debug session's notes.

## 3. Build

Same toolchain and command as `05-proxy-dll-validation.md` (`build.ps1`, unchanged), now also
`#include <d3d9.h>`. One new, harmless warning:

```
warning: redeclaration of 'Direct3DCreate9' should not add 'dllexport' attribute
```

— expected: `d3d9.h` itself forward-declares `Direct3DCreate9` (its normal import declaration),
and our definition re-adds `__declspec(dllexport)` with a matching signature. No functional issue;
`llvm-objdump -p` still shows the correct plain export name and its stdcall-decorated alias:

```
Export Table:
       1   0x1310  Direct3DCreate9
       2   0x1310  Direct3DCreate9@4
```

## 4. Live validation

Script: `tools/proxy-d3d9/validate.ps1`, extended from the prior version to poll for at least 5
`"Present() hit"` log lines (up to 30 extra seconds) before printing the log and cleaning up,
instead of a fixed ~2-second sleep — the original fixed sleep was too short to observe repeated
`Present` calls (first run only captured through device creation). Same safety pattern as before:
aborts if a `d3d9.dll` already exists in the game directory, copies the proxy in, launches
`Psychonauts.exe` directly (no debugger), unconditionally kills the process and removes the copied
DLL in a `finally` block.

**Result: full success.** Actual log from the validated run:

```
[2026-08-16 12:44:16.651] ==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_ATTACH (pid=12588) ====
[2026-08-16 12:44:18.066] Direct3DCreate9(SDKVersion=0x20) called - forwarding to real d3d9.dll
[2026-08-16 12:44:18.068] Loaded real d3d9.dll from "C:\Windows\system32\d3d9.dll" (hModule=0x73310000)
[2026-08-16 12:44:18.069] Resolved real Direct3DCreate9 at 0x73374B20
[2026-08-16 12:44:18.133] Real Direct3DCreate9 returned IDirect3D9* = 0x008BBFE0
[2026-08-16 12:44:18.133] Hooked IDirect3D9::CreateDevice (vtable slot 16), original=0x7337F750
[2026-08-16 12:44:18.136] CreateDevice() called: Adapter=0 DeviceType=1 hFocusWindow=0x00780842 BehaviorFlags=0x46
[2026-08-16 12:44:18.137]   D3DPRESENT_PARAMETERS: Windowed=1 BackBufferWidth=640 BackBufferHeight=480 BackBufferFormat=21 BackBufferCount=0 hDeviceWindow=0x00000000 SwapEffect=1 EnableAutoDepthStencil=1 AutoDepthStencilFormat=75 FullScreen_RefreshRateInHz=0 PresentationInterval=0x0 Flags=0x0
[2026-08-16 12:44:18.257] Real CreateDevice returned hr=0x00000000, IDirect3DDevice9*=0x073938C0
[2026-08-16 12:44:18.257] Hooked IDirect3DDevice9::Present (vtable slot 17), original=0x733F6120
[2026-08-16 12:44:22.420] Present() hit - total frame #1 (throttled ~1 log/sec)
[2026-08-16 12:44:23.424] Present() hit - total frame #29 (throttled ~1 log/sec)
[2026-08-16 12:44:24.425] Present() hit - total frame #59 (throttled ~1 log/sec)
[2026-08-16 12:44:25.427] Present() hit - total frame #89 (throttled ~1 log/sec)
[2026-08-16 12:44:26.427] Present() hit - total frame #119 (throttled ~1 log/sec)
[2026-08-16 12:44:27.429] Present() hit - total frame #149 (throttled ~1 log/sec)
```

### Decoded `D3DPRESENT_PARAMETERS` (first real run, main menu launch defaults)

| Field | Raw value | Decoded |
|---|---|---|
| `Windowed` | 1 | `TRUE` — windowed mode by default |
| `BackBufferWidth` × `BackBufferHeight` | 640 × 480 | Default/menu resolution — much lower than the `800` glimpsed in one probe in the prior live-debug session; likely the very first device is created at a low default before any settings/config resolution is applied, or this simply is the configured default. Not fully explained yet — worth rechecking after reaching the in-game options menu. |
| `BackBufferFormat` | 21 | `D3DFMT_A8R8G8B8` |
| `BackBufferCount` | 0 | Driver default (effectively 1) |
| `hDeviceWindow` | `0x00000000` (NULL) | Falls back to `hFocusWindow` (`0x00780842`, a real HWND) per normal D3D9 semantics — not a bug, just worth noting for anyone tracing window-handle usage later |
| `SwapEffect` | 1 | `D3DSWAPEFFECT_DISCARD` |
| `EnableAutoDepthStencil` | 1 (`TRUE`) | Uses the driver-managed depth/stencil surface |
| `AutoDepthStencilFormat` | 75 | `D3DFMT_D24S8` |
| `FullScreen_RefreshRateInHz` | 0 | Irrelevant while windowed |
| `PresentationInterval` | `0x0` | `D3DPRESENT_INTERVAL_DEFAULT` (driver-controlled vsync) |
| `Flags` | `0x0` | None set |

`BehaviorFlags = 0x46` matches the live-debug session exactly: `D3DCREATE_FPU_PRESERVE (0x2) |
D3DCREATE_MULTITHREADED (0x4) | D3DCREATE_HARDWARE_VERTEXPROCESSING (0x40)`.

### Proof `Present` fires repeatedly

Frame counter went `1 → 29 → 59 → 89 → 119 → 149` across six ~1-second-apart log lines — a steady
~30 frames/second, consistent with a capped or vsynced menu/loading screen. Confirms the hook is a
real per-frame interception, not a one-shot artifact.

## 5. Gotchas

- **First validation attempt undershot the wait time.** The original `validate.ps1` treated "log
  file exists" as the finish line and only slept 2 more seconds — enough to catch
  `Direct3DCreate9`/`CreateDevice` but not enough for the game to reach a steady render loop that
  calls `Present` repeatedly. Fixed by polling for `"Present() hit"` line count (up to 5 hits / 30
  extra seconds) before declaring success.
- **`GetTickCount` wraparound / thread-safety**: `g_lastPresentLogTick` is read/written without a
  lock in `Hook_Present`. `Present` is effectively single-threaded on this game (one render
  thread), so this is fine in practice; flagged here in case a future hook (e.g. `Reset`, called
  from a different thread in some engines) needs a shared timestamp — use an interlocked compare
  or move it under the existing `g_logLock` if that ever becomes a concern.
- `BackBufferWidth`/`Height` = 640×480 here is smaller than the `800` value glimpsed once in the
  prior live-debug session's manual single-dword probe — not yet reconciled; likely just two
  different points in a resolution-negotiation sequence (e.g. `Reset()` may be called later with
  the user's actual configured resolution). Worth hooking `Reset` too if resolution changes need
  tracking, but out of scope for this pass.
- No anti-tamper/integrity pushback vtable-patching either interface — consistent with every prior
  session's findings for this game.

## 6. Cleanup

Confirmed after the run: no `Psychonauts` process left running (`Get-Process` → nothing), no
`d3d9.dll` present in the game directory (`Test-Path` → `False`). Only `tools/proxy-d3d9/d3d9.dll`
in the workspace retains a built copy.

## 7. Next milestone

Two viable directions, both queued from prior sessions:

1. **Second render target for the other eye**: now that a live `IDirect3DDevice9*` and a
   guaranteed-every-frame `Present` hook exist, the next real step is calling
   `CreateRenderTarget`/`CreateTexture` from inside `Hook_CreateDevice` (once the real device
   exists) to stand up a second color (and matching depth/stencil) surface sized to match the
   backbuffer, purely to prove render-target creation succeeds against this device/driver — still
   no compositing/stereo logic yet.
2. **Disassemble the camera-matrix callers**: use x64dbg to examine the functions *containing* the
   `D3DXMatrixPerspectiveFovRH` (`exe+0x292525`) / `D3DXMatrixLookAtRH` (`exe+0x2924B1`) call sites
   (already located, not yet disassembled) to find where FOV/aspect ratio and view matrices are
   computed — the actual injection point for per-eye matrices.

Leaning toward (2) next since it's the harder unknown and doesn't depend on (1); (1) is now
low-risk infrastructure that can happen anytime once the injection point is understood.
