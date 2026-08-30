# Session 25 — VR Runtime Bridge: SteamVR/Null-Driver Status, OpenVR SDK Vendored, Shared-Surface Proof-of-Concept

Date: 2026-08-17. Follows directly from notes/24 §2 (VR runtime integration scoping). This session
moves from "scoped, not implemented" to genuinely building and testing the riskiest single piece of
the bridge — with **no dependency on the game or SteamVR being installed** for any of it, per this
session's own safety scoping. **The user's own game was running for the entire session** (PID 16672
at session start, later PID 21588 — the user evidently relaunched it themselves at some point; it was
never attached to, killed, or written to either time, exactly as the task required).

## 0. Summary (read this first)

- **SteamVR is NOT installed on this machine** — confirmed via process check, registry, and a
  filesystem search across all local drives (C:/D:/E:/F:), not assumed. This is a real blocker for
  §2/§3 of notes/24's plan (actual `IVRCompositor::Submit` testing against the null driver) and is
  flagged per this session's task instructions as something requiring **the user's explicit
  go-ahead** before installing — not installed this session. See §1 for detail and exactly what
  would need to happen.
- **The OpenVR SDK (headers + win32 import lib + DLL only, ~13MB) was vendored** into
  `tools/vr-bridge/openvr-sdk/` via a sparse shallow git clone with the nested `.git` stripped
  afterward (plain vendored files, not a submodule) — judged in-scope to do without asking, per the
  task's own "lightweight header/lib dependency, not a heavy install" framing. See §2.
- **The single riskiest open technical question from notes/24 — can a D3D9Ex shared surface
  actually be opened and read correctly from a separate D3D11 device — is now answered YES, with
  real, repeated, live evidence on this exact machine's actual GPU (NVIDIA GTX 1660 SUPER).** A
  standalone proof-of-concept program (`tools/vr-bridge/poc_shared_surface/`) creates a D3D9Ex
  render target with a shared handle, clears it to two different known colors in sequence, and
  reads back byte-exact matching pixel data from a completely separate `ID3D11Device` both times —
  4/4 clean runs, zero mismatches. This is a genuinely big, concrete result: the specific mechanism
  notes/24 identified as "the realistic path" for bridging D3D9→OpenVR is now proven to actually
  work in this environment, not just plausible on paper. See §3.
- **A second, smaller proof-of-concept confirms the vendored OpenVR SDK itself links and calls
  correctly** (`tools/vr-bridge/poc_openvr_init/`) — `VR_InitInternal2` reaches the real
  `openvr_api.dll` and returns a clean, correct `VRInitError_Init_InstallationNotFound` (error 100),
  exactly the expected result with SteamVR absent. This de-risks the OpenVR side of the vendoring
  independently of the D3D interop question. See §4.
- Nothing in this session touched the game, launched a debugger, or required SteamVR to be present.
  Both POCs are plain standalone executables with zero dependency on Psychonauts.

## 1. SteamVR installation status — confirmed NOT installed, blocker for the user

Checked three independent ways, not assumed:

1. **Running processes**: `Get-Process -Name vrserver,vrmonitor,vrcompositor` — no matches.
2. **Steam library manifests**: the only Steam library (`D:\Program Files (x86)\Steam`, per
   `libraryfolders.vdf`) lists exactly two app IDs installed (`228980` = Steamworks Common
   Redistributables, `883710` = Psychonauts itself) — no SteamVR (`250820`). A second, unrelated
   Steam-adjacent library on `F:\SteamLibrary` exists but contains no `steamapps` folder at all
   (just regular non-Steam program files).
3. **Registry / OpenVR path registration**: `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` DOES exist
   and references a SteamVR runtime path — but at `C:\Program Files (x86)\Steam\steamapps\common\
   SteamVR`, on a `C:` drive Steam install that **no longer exists** (`C:\Program Files (x86)\Steam`
   itself doesn't exist; the current, only Steam install is on `D:`). This is stale registration
   left over from some prior system state (a reimage, drive change, or Steam reinstall-to-a-new-
   drive), not evidence of a currently-usable SteamVR — confirmed by the `poc_openvr_init` test in
   §4 actually trying to init through this exact stale path and getting
   `VRInitError_Init_InstallationNotFound`.

**What would need to happen, if/when the user gives the go-ahead**: install SteamVR from Steam
(free, ~1-2GB, app ID 250820) to the existing `D:\Program Files (x86)\Steam` library. This is the
one piece of this session's scope that was flagged per the task's explicit "ask before installing"
rule rather than done unilaterally.

## 2. OpenVR SDK vendored (headers + win32 lib/dll only)

Per the task's own framing — a header/import-lib dependency is much lighter-weight than an
application install, closer to adding a library to the project than "installing new software" —
this was done without stopping to ask, judged in-scope.

**Method**: sparse shallow clone of `https://github.com/ValveSoftware/openvr` (`git clone
--filter=blob:none --no-checkout --depth 1`, then `git sparse-checkout set headers lib/win32
bin/win32`), narrowing from the repo's full ~80MB multi-platform tree (linux/osx/android libs,
win64, samples, docs) down to **~13.6MB total**. The nested `.git` was then deliberately stripped
(this workspace — `C:\Users\Tefa\Documents\PsychonautsVR` — is itself a git repo; leaving a nested
`.git` in place would create a submodule-like gitlink when synced into `dev-archive`, which is
messier than just vendoring plain files for this purpose). Provenance (source URL, commit hash at
vendoring time via `git ls-remote`, date, license) recorded in
`tools/vr-bridge/openvr-sdk/VENDORED.md`.

**Contents kept**: `headers/openvr.h` (C++ wrapper), `headers/openvr_capi.h` (flat C API),
`headers/openvr_driver.h`, `lib/win32/openvr_api.lib` (32-bit import lib, matching Psychonauts.exe's
own architecture), `bin/win32/openvr_api.dll` (32-bit runtime DLL), `LICENSE` (BSD-3-Clause),
`README.md`. No build step needed to use any of this — plain headers + a standard import lib + a
DLL, exactly like the project's existing d3d9/d3d11/dxgi headers that already ship inside the
LLVM-MinGW toolchain.

**A real header quirk found and worked around** (documented in code, not just here): `openvr_capi.h`
wraps its own prototypes for the global entry points (`VR_InitInternal`, `VR_ShutdownInternal`,
etc.) in `#if 0 ... #endif` — dead code in the shipped header, for any language. Worse, those
particular commented-out prototypes are also **outdated** relative to what the current
`openvr_api.dll` actually exports — modern OpenVR uses `VR_InitInternal2` (extra `pStartupInfo`
argument, returns a plain `uint32_t` token) confirmed via `openvr.h`'s live (non-dead) C++ wrapper
code, not the dead C API block. `tools/vr-bridge/poc_openvr_init/openvr_init_poc.c` declares the
correct, current entry points itself (still using `openvr_capi.h`'s enum/struct definitions
unmodified) — a small, documented, and now load-bearing finding for anyone using this vendored SDK
from plain C in the future (the eventual `proxy_d3d9.c` integration will need the same fix).

A second, smaller quirk: `openvr_capi.h` typedefs its own `bool` as `char` on Windows (guarded by
`#if defined(__WIN32)` — note the missing leading underscore vs. the standard `_WIN32`, which
mingw-w64 happens to also define) — this collides with `<stdbool.h>`'s `#define bool _Bool` if both
are included in the same translation unit. Fixed by simply not including `<stdbool.h>` and using
`openvr_capi.h`'s own `bool`/0/1 throughout.

## 3. THE crux result: D3D9Ex shared surface IS correctly readable from a separate D3D11 device

This is the single most important finding of this session. notes/24 §2b identified this as "the
realistic bridging approach" but flagged it as unverified — a real synchronization/correctness risk
the project hadn't tested. This session built a minimal, standalone, game-free proof-of-concept to
answer exactly that question.

### 3a. What the POC does (`tools/vr-bridge/poc_shared_surface/shared_surface_poc.c`)

1. Creates a real `IDirect3DDevice9Ex` device (offscreen, hidden window, never `Present()`s —
   the game process is never involved).
2. Creates one 64×64 `D3DFMT_A8R8G8B8` render-target texture on it **with a shared `HANDLE`**
   (`IDirect3DDevice9Ex::CreateTexture`'s `pSharedHandle` out-parameter).
3. Clears that surface to a known color ("color A": R=200,G=64,B=32) and forces a full GPU flush
   (an `IDirect3DQuery9` `D3DQUERYTYPE_EVENT` query, polled with `D3DGETDATA_FLUSH` until `S_OK` —
   the standard D3D9 technique for "block until this GPU work is actually done," since D3D9 has no
   explicit `Flush()` method).
4. Creates a **completely separate** `ID3D11Device`, deliberately matched to the **same physical
   adapter** as the D3D9Ex device via DXGI adapter LUID comparison (`IDirect3D9Ex::GetAdapterLUID`
   vs. each `IDXGIAdapter1`'s `DXGI_ADAPTER_DESC1.AdapterLuid`) — required for the shared handle to
   be valid at all on a multi-GPU system, and good practice even on this single-GPU machine.
5. Opens the D3D9Ex surface's shared handle from the D3D11 device
   (`ID3D11Device::OpenSharedResource`), copies it into a CPU-readable staging texture, and reads
   back real pixel bytes at `(10,10)` — compares against color A.
6. **Re-clears the SAME D3D9Ex surface** to a second, different color ("color B": R=10,G=220,B=90),
   flushes again, and reads back through the **same already-open** D3D11 texture handle a second
   time. A match here proves genuine live/shared GPU memory — the same underlying surface, updated
   in place and visible to the other API without reopening anything — not a one-shot
   copy-on-open semantic that would only happen to look right once.

### 3b. Result: PASS, 4/4 clean runs

```
=== D3D9Ex -> D3D11 shared-surface interop proof-of-concept ===
[1] Direct3DCreate9Ex OK
[1] D3D9Ex adapter LUID = 00000000:00009A49
[1] CreateDeviceEx OK (D3D9Ex device created)
[2] CreateTexture (shared, 64x64, A8R8G8B8) OK, sharedHandle=80003382
[3] Matched D3D11 adapter to D3D9Ex's LUID: NVIDIA GeForce GTX 1660 SUPER
[3] D3D11CreateDevice OK, feature level=0xB000
[4] OpenSharedResource OK - D3D11 opened the D3D9Ex-created shared surface
    D3D11 texture desc: 64x64, DXGI format=87
[5] D3D9Ex side cleared the shared surface to color A (R=200,G=64,B=32), flushed
    D3D11 readback @ (10,10): B=32 G=64 R=200 A=255
    -> MATCH (color A)
[6] D3D9Ex side re-cleared the SAME shared surface to color B (R=10,G=220,B=90), flushed
    D3D11 readback @ (10,10): B=90 G=220 R=10 A=255
    -> MATCH (color B) -- proves LIVE sharing, not a one-shot snapshot
=== RESULT: PASS - D3D9Ex shared surface is correctly and LIVE readable from a separate D3D11 device ===
```

Run 4 times consecutively (once during initial development, three more as a deliberate robustness
check, matching this project's established practice — see notes/12's 15/15, notes/15's 4/4) — **all
4 runs: exit code 0, both color checks matched exactly, zero mismatches.** DXGI format 87 =
`DXGI_FORMAT_B8G8R8A8_UNORM`, the correct/expected mapping for a `D3DFMT_A8R8G8B8` D3D9 surface
opened from D3D11 (same memory layout, byte order B,G,R,A confirmed directly in the readback).
Feature level `0xB000` = `D3D_FEATURE_LEVEL_11_0`, i.e. the real GPU driver, not a software/WARP
fallback.

### 3c. Why this matters

This directly answers notes/24 §2b/2e's open question — "is the bridging fix... nontrivial, separate
piece of engineering... [that] hasn't tested" — for its single riskiest sub-part. **The crux
mechanism works, on this exact machine's real GPU/driver stack, not just in theory.** What's still
NOT tested (honestly, not glossed over): this POC only proves the mechanism with a solid-color
clear, not with the game's actual per-frame render-target *rendering* (shaders, geometry, real
frame rates); it doesn't yet integrate `IVRCompositor::Submit` at all (needs SteamVR installed, see
§1); and real per-frame synchronization at 60-90fps under load (rather than one clear + one flush)
still needs to be proven — the event-query flush technique used here is correct but is a
synchronous stall, which would need to become a proper per-frame pipeline (e.g. a keyed mutex or a
lighter-weight fence) before this could go into the actual hot path without hurting frame time.
Those are the concrete next steps, not new unknowns discovered this session.

## 4. OpenVR SDK link/init smoke test — confirms the vendored SDK works end-to-end

`tools/vr-bridge/poc_openvr_init/openvr_init_poc.c` — separate from and simpler than §3, this just
proves the vendored headers+lib (§2) actually compile, link, and make a real call into
`openvr_api.dll` at runtime:

```
=== OpenVR SDK header/link smoke test (VR_InitInternal2) ===
VR_IsRuntimeInstalled() = false
VR_IsHmdPresent()       = false
VR_InitInternal2(VRApplication_Scene) -> token=0, error=100 (Installation Not Found (100))
This is the EXPECTED result on this machine right now: SteamVR is not installed...
```

Exactly the expected result given §1's findings — a clean, correct `VRInitError_Init_
InstallationNotFound`, not a crash, not a linker error, not garbage. **This exact same .exe can be
rerun (no rebuild needed) once SteamVR is installed** and should then return `error=0`
(`VRInitError_None`) plus a real per-eye render target size from `GetRecommendedRenderTargetSize` —
this is now a ready-made smoke test for whenever that happens.

## 5. Null-driver configuration — researched and documented in full, still not executable (SteamVR absent)

Refines notes/24 §2c with exact, verified file paths/keys (re-confirmed via fresh web research this
session, cross-checked against two independent sources) and one durability refinement notes/24
didn't have:

1. **`<Steam>\steamapps\common\SteamVR\drivers\null\resources\settings\default.vrsettings`** — set
   `"enable": true` (this file's default ships as `false`).
2. **`<Steam>\steamapps\common\SteamVR\resources\settings\default.vrsettings`** — set
   `"requireHmd": false`, `"forcedDriver": "null"`, `"activateMultipleDrivers": true`.
3. **Important refinement not in notes/24**: both `default.vrsettings` files above get **overwritten
   on every SteamVR update** — not a durable place to keep these settings long-term. The durable
   mechanism is the **per-user override file**, `<Steam>\userdata\<SteamID>\config\
   steamvr.vrsettings`, which persists across updates and takes precedence over the shipped
   defaults; settings placed there under the correct JSON section (e.g. `"steamvr"` for
   `requireHmd`/`forcedDriver`/`activateMultipleDrivers`) survive SteamVR updates that would
   otherwise silently revert the `default.vrsettings` edits. A future session enabling the null
   driver for real should set it up here, not just in `default.vrsettings`.
4. In the SteamVR settings UI (once running): Video tab → disable "Pause VR when headset is idle";
   Startup/Shutdown tab → enable "Override Windows Power Scheme" — otherwise the null-driver
   "headset" goes to standby after a few seconds, interrupting frame-submission testing.

On this machine, once SteamVR is installed, `<Steam>` above resolves to
`D:\Program Files (x86)\Steam` (the only real Steam install — see §1's finding that the
stale `openvrpaths.vrpath` still points at a nonexistent `C:` path, which SteamVR's own installer
would correct on a fresh install to this library).

## 6. What's proven vs. what's still open

**Proven this session, with real evidence, on this machine:**
- SteamVR is not installed (checked three ways).
- The OpenVR SDK's headers/import-lib vendor cleanly into this project's existing mingw toolchain
  and successfully call the real `openvr_api.dll` (§4).
- A D3D9Ex shared surface is correctly and *live* (not just once) readable from a separate D3D11
  device, on this machine's real GPU — the specific mechanism notes/24 flagged as the realistic
  bridging approach (§3).

**Still open, concretely scoped, not attempted this session (needs SteamVR, or needs game
integration, or both):**
- Actually enabling the null driver and calling `VR_Init`/`IVRCompositor::Submit` against it —
  blocked purely on the SteamVR install (§1), which needs the user's go-ahead.
- Extending the shared-surface POC from "one solid-color clear" to a per-frame pipeline running at
  real frame rate with a non-stalling sync mechanism.
- Any actual integration into `tools/proxy-d3d9/proxy_d3d9.c` — this session deliberately kept the
  D3D9Ex/D3D11/OpenVR work as fully standalone, game-free POCs, per the task's own explicit
  instruction to prove the riskiest piece first rather than jump straight to game integration.
  Wiring this into the real DLL is a separate, later piece of work.

## 7. Repo sync and cleanup

No game files touched. No debugger used. No processes started beyond the two POC .exe files (both
exit cleanly on their own, confirmed no leftover processes after each run). The user's own
Psychonauts process (seen as PID 16672 at session start, PID 21588 later — evidently restarted by
the user themselves at some point mid-session) was never attached to, queried beyond a plain
`Get-Process` existence check, killed, or written to.

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; new
  `tools/vr-bridge/openvr-sdk/` (vendored SDK, ~13MB), `tools/vr-bridge/poc_shared_surface/`
  (source + build script + built .exe), `tools/vr-bridge/poc_openvr_init/` (source + build script +
  built .exe + copied `openvr_api.dll`).
- **modding-notes / dev-archive**: synced this session (this note + all new `tools/vr-bridge/`
  content — no game assets, only original code/vendored-SDK/build scripts, consistent with this
  project's legal/scope boundaries).
- **Mod repo** (`psychonauts-vr`): not touched this session, per the task's own instruction (not
  relevant for this exploratory/scaffolding work yet).
