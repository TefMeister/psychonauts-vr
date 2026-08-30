# Session 27 — Null-Driver Enabled, OpenVR Init Succeeds, IVRCompositor::Submit PASSES End-to-End

Date: 2026-08-17. Follows directly from notes/25 (VR runtime bridge scaffolding — SteamVR not yet
installed, D3D9Ex->D3D11 shared surface interop proven standalone) and notes/26 (unrelated, no code
changes). **Trigger: the user installed SteamVR** (`D:\Program Files (x86)\Steam\steamapps\common\
SteamVR`), unblocking everything notes/25 §6 flagged as gated on it.

**The user's own game (PID 7052, StartTime 12:41:57) was running for the entire session** and was
never touched (no attach, no kill, no write to the game directory) — this task never needed the
game at all; everything below is standalone POC work in `tools/vr-bridge/`.

## 0. Summary (read this first) — full success, all three task milestones hit

1. **Null driver enabled** — SteamVR's full stack (`vrserver`, `vrcompositor`, `vrdashboard`,
   `vrmonitor`, `vrwebhelper`) runs headlessly against the `null` HMD driver, no physical hardware.
2. **OpenVR init succeeds cleanly**: `VR_InitInternal2` returns `error=0` (`VRInitError_None`) —
   previously `VRInitError_Init_InstallationNotFound` (100) with SteamVR absent (notes/25 §4).
3. **`IVRCompositor::Submit` returns `VRCompositorError_None` for both eyes**, submitting a real
   D3D9Ex-rendered surface bridged through a shared D3D11 texture — proving the full intended bridge
   mechanism (D3D9Ex render → shared D3D11 surface → OpenVR compositor) end-to-end, with **zero
   physical VR hardware**, 4/4 clean runs.
4. **A second real, distinct SteamVR-install bug was found and fixed along the way** (a stale
   `openvrpaths.vrpath` config path pointing at a nonexistent `C:` Steam install — separate from the
   stale *runtime* path notes/25 already found and expected the installer to fix; the installer did
   NOT fix the config/log paths, so this needed a manual `vrpathreg.exe` fix — see §2).
5. **A real, load-bearing ABI finding, not assumed**: this installed SteamVR build's
   `VR_GetGenericInterface()` does **not** return the "flat FnTable" struct-of-function-pointers that
   `openvr_capi.h`'s vendored header documentation assumes — it returns a genuine C++ object pointer
   (a `this`-pointer into `vrclient.dll`). Calling through it as a flat table crashes
   (`STATUS_ACCESS_VIOLATION`, execute-fault, landing on a `PAGE_READONLY` `.rdata` page — confirmed
   via `VirtualQuery`, not guessed). The fix — dereference the real vtable and dispatch via
   `__thiscall` — is confirmed working for both `IVRSystem` and the load-bearing `IVRCompositor`. See
   §4 for the full diagnostic trail; this is the single most important technical finding of this
   session and needs to carry forward into the real `proxy_d3d9.c` integration.

## 1. Null driver configuration — executed per notes/25 §5, with SteamVR now actually present

Confirmed SteamVR install exists at `D:\Program Files (x86)\Steam\steamapps\common\SteamVR` (bin,
drivers, resources, content, etc. all present). Applied the three-file plan notes/25 researched:

1. `drivers\null\resources\settings\default.vrsettings` — `"driver_null": { "enable": true, ... }`
   (was `false`). **One correction to notes/25's plan**: the JSON section key is `driver_null`, not
   `null` (notes/25's §5 point 1 didn't specify the key name explicitly — worth noting exactly for
   next time).
2. `resources\settings\default.vrsettings` (top-level) — `steamvr.requireHmd: false`,
   `steamvr.forcedDriver: "null"`, `steamvr.activateMultipleDrivers: true`,
   `power.overrideWindowsPowerScheme: true` (the last one for the "don't let the null headset idle
   out" durability point notes/25 §5.4 flagged).
3. **Durable per-user override**: `D:\Program Files (x86)\Steam\config\steamvr.vrsettings` did not
   exist yet (SteamVR had never been run) — created it directly with the same `driver_null`/`steamvr`
   keys, per notes/25's own finding that this file survives SteamVR updates while the two
   `default.vrsettings` files above do not.

## 2. A second real config bug found and fixed: stale `openvrpaths.vrpath` config/log paths

notes/25 §1 flagged the *runtime* path in `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` as stale
(pointing at a since-removed `C:` Steam install). **What actually happened once SteamVR was
installed**: the installer added the correct `D:` runtime path (now first/primary in the `runtime`
list) but **left the `config` and `log` paths still pointing at the nonexistent
`C:\Program Files (x86)\Steam\config`/`logs`** — confirmed via `Test-Path` (both `False`) and
`vrpathreg.exe`'s own `show` command. This is a distinct bug from the one notes/25 found (runtime vs.
config/log), and the installer did not self-heal it.

**Fixed with the official tool**, not by hand-editing the JSON (safer — the file has a specific
schema `vrpathreg` itself manages):
```
vrpathreg.exe setconfig "D:\Program Files (x86)\Steam\config"
vrpathreg.exe setlog    "D:\Program Files (x86)\Steam\logs"
vrpathreg.exe setruntime "D:\Program Files (x86)\Steam\steamapps\common\SteamVR"
```
Confirmed via `vrpathreg.exe show` afterward — all three now correctly point at the real `D:`
install. This mattered concretely: SteamVR's user-settings persistence (the durable
`steamvr.vrsettings` override file from §1.3) lives under the `config` path, so a wrong path here
would have silently broken settings durability even though the runtime itself could still launch.
**Concrete lesson for next time / for anyone reusing this project's notes**: after installing SteamVR
onto a non-default drive/location where a stale `openvrpaths.vrpath` already exists, run
`vrpathreg.exe show` and manually correct config/log paths too, not just verify the runtime path.

## 3. `poc_openvr_init.exe` rerun — full init success, then a real crash, fully diagnosed and fixed

Reran the exact unmodified `tools/vr-bridge/poc_openvr_init/openvr_init_poc.exe` from notes/25.
`VR_InitInternal2` now returns `error=0` (previously 100) — the headline expected result. But the
POC then **crashed** (`STATUS_ACCESS_VIOLATION`, exit code `-1073741819` / `0xC0000005`) on the very
next step, `pSystem->GetRecommendedRenderTargetSize(&w, &h)` (called through
`struct VR_IVRSystem_FnTable *`, exactly as `openvr_capi.h`'s own documented usage pattern
prescribes). This was investigated with real evidence, not patched blind:

1. **stdout buffering hid all output on crash** — piped/redirected stdout is fully buffered by the
   C runtime, so a crash before an explicit flush loses everything printed so far. Fixed by adding
   `setvbuf(stdout, NULL, _IONBF, 0)` at the top of `main()` — a reusable fix worth keeping in any
   future POC that might crash.
2. **Added a vectored exception handler** (`AddVectoredExceptionHandler`) to print the exact
   exception code/address/access-type before the process dies. Result: `code=0xC0000005` (access
   violation), **access type=8 (execute fault)**, faulting at the exact address read from the
   FnTable's slot 0.
3. **`VirtualQuery` on that address**: `Protect=0x2` (`PAGE_READONLY`) — genuinely **not
   executable**, confirming the crash is a real DEP-style violation, not a wild/corrupted pointer
   from stack smashing. `AllocationProtect=0x80` (`PAGE_EXECUTE_WRITECOPY`) shows the *region* was
   originally mapped executable (i.e., this is a real PE image mapping, specifically its `.rdata`
   section, not garbage/unmapped memory).
4. **Dumped the raw bytes at that address**: decoded as four more in-range-looking pointers
   (`0x6D2E5000`, `0x6D2E4B70`, `0x6D2E4C20`, `0x6D2E2A70`), i.e. **another array of pointers**, not
   x86 instruction bytes. This is the classic signature of a C++ vtable stored in `.rdata`.
5. **Confirmed hypothesis directly**: `sysPtr` (the `VR_GetGenericInterface` return value) is a real
   C++ object `this`-pointer, not a flat FnTable base. `*(void***)sysPtr` (dereferencing the object's
   first 4 bytes, the standard MSVC single-inheritance vtable-pointer location) gives the *real*
   vtable — its slot 0 (`0x6D2E5000`) is on a `PAGE_EXECUTE_READ` page (confirmed via a second
   `VirtualQuery`), genuinely executable. Dispatching through it with the correct calling convention
   (`__thiscall` — `openvr.h`'s C++ interface methods are unattributed virtuals, so the default MSVC
   x86 ABI applies: `this` in `ECX`, args on the stack, callee cleans up) **worked cleanly**:
   `GetRecommendedRenderTargetSize` returned `1656 x 1840` per eye (a real, plausible value — close
   to the null driver's configured `1512x1680` base scaled by the live GPU-benchmark render-target
   multiplier SteamVR computed for this machine's GTX 1660 SUPER), and `VR_ShutdownInternal()`
   returned cleanly afterward (exit code 0).

**Why this matters beyond just this one POC**: `openvr_capi.h`'s own file header says "auto-
generated" flat-C-API structs, and this exact call pattern (`VR_GetGenericInterface` +
struct-of-function-pointers) is the standard, widely-documented way third-party/non-C++ bindings use
OpenVR from C. That this specific installed SteamVR build's `vrclient.dll` does not honor it (at
least not for `IVRSystem_026`/`IVRCompositor_029`) is a genuine, concrete finding about *this*
environment, not a mistake in the vendored SDK files themselves — worth carrying forward as a known
fact, not re-discovering it from scratch in a future session. `tools/vr-bridge/poc_openvr_init/
openvr_init_poc.c` now contains the full working fix (vtable dereference + `__thiscall` dispatch)
plus all the diagnostic instrumentation described above, left in place and commented for reuse.

## 4. New POC: `poc_submit_test` — the full end-to-end bridge, PASS 4/4

Built `tools/vr-bridge/poc_submit_test/submit_test_poc.c`, combining the two previously-separate,
independently-proven pieces into one program:

1. **Part A** (from `poc_shared_surface`, unchanged mechanism): create a D3D9Ex device, a 64×64
   `D3DFMT_A8R8G8B8` shared render target, clear it to a distinctive test color
   (R=40,G=160,B=220 — not game content, per this session's explicit scope), flush.
2. **Part B** (from `poc_shared_surface`): create a separate `ID3D11Device` matched to the same
   adapter LUID, `OpenSharedResource` to get an `ID3D11Texture2D*` view of the same live surface.
3. **Part C** (new this session): `VR_InitInternal2(VRApplication_Scene)`, get `IVRCompositor` via
   `VR_GetGenericInterface(IVRCompositor_029, ...)`, apply the §3 vtable/`__thiscall` fix, call
   `WaitGetPoses` (standard once-per-frame OpenVR usage pattern, establishes compositor frame timing
   before Submit), then build a `Texture_t{ handle=pTex11, eType=TextureType_DirectX,
   eColorSpace=ColorSpace_Auto }` and call `Submit(Eye_Left/Eye_Right, &tex, NULL, Submit_Default)`.

### Result: PASS, 4/4 clean runs

```
=== VR bridge end-to-end POC: D3D9Ex -> shared D3D11 -> IVRCompositor::Submit ===

[A1] D3D9Ex device created
[A2] D3D9Ex shared surface cleared to test color (R=40,G=160,B=220) and flushed

[B1] Matched D3D11 adapter to D3D9Ex's LUID: NVIDIA GeForce GTX 1660 SUPER
[B2] D3D11CreateDevice OK, feature level=0xB000
[B3] OpenSharedResource OK - D3D11 texture ready to hand to OpenVR

VR_IsRuntimeInstalled() = true
VR_IsHmdPresent()       = true

[C1] VR_InitInternal2(VRApplication_Scene) -> token=1, error=0 (No Error (0))
[C2] VR_GetGenericInterface(IVRCompositor_029) -> ptr=69F2E0D8, error=0
[C3] IVRCompositor real vtable = 69EBF5A8 (dispatching via __thiscall, this-ptr fix)

[C4] WaitGetPoses -> error=0
[C5] Submit(Eye_Left)  -> error=0 (VRCompositorError_None)
[C6] Submit(Eye_Right) -> error=0 (VRCompositorError_None)

=== RESULT: PASS - IVRCompositor::Submit accepted the D3D9Ex-bridged D3D11 texture for both eyes ===
VR_ShutdownInternal() called.
```

Run 4 times consecutively (matching this project's established robustness-check practice — notes/12
15/15, notes/25 4/4) — **all 4 runs: exit code 0, `error=0` on every `WaitGetPoses`/`Submit` call, no
crashes.** No leftover processes after each run (confirmed via `Get-Process`); the user's
`Psychonauts` process (PID 7052) was unaffected throughout, confirmed unchanged before and after.

### Why this is the real milestone

This is the literal success criterion the task set: `IVRCompositor::Submit` returning
`VRCompositorError_None`, proving the full bridge mechanism (D3D9Ex render → shared D3D11 surface →
OpenVR compositor) end-to-end, **without any physical VR hardware** — the null driver stands in for
a real headset completely transparently from the submitting application's point of view. Combined
with notes/25's already-proven shared-surface interop, this closes out the single riskiest open
technical question the project identified back in notes/24 §2: **yes, Psychonauts' D3D9 rendering
can reach OpenVR's D3D11-only compositor, on this machine, with real evidence, not just a plan.**

## 5. What's proven vs. what's still open

**Proven this session, with real evidence, on this machine:**
- SteamVR runs headlessly against the null driver (no physical HMD) — full process stack up
  (`vrserver`/`vrcompositor`/`vrdashboard`/`vrmonitor`/`vrwebhelper`), confirmed via `Get-Process`
  and live log lines (`Using existing HMD null.Null Serial Number`).
- `VR_InitInternal2` succeeds cleanly (`error=0`) against the null driver.
- The real, load-bearing calling-convention fix for this SteamVR build's `VR_GetGenericInterface`
  return values (vtable-deref + `__thiscall`, not the header's documented flat FnTable) — confirmed
  working for both `IVRSystem` and `IVRCompositor`.
- `IVRCompositor::Submit` accepts a D3D9Ex-bridged, live GPU-shared D3D11 texture and returns
  `VRCompositorError_None` for both eyes, 4/4 clean runs.

**Still open, concretely scoped, not attempted this session:**
- No visual confirmation that the null driver actually *displays* anything (it has no physical
  output by design) — `VRCompositorError_None` is the correct, sufficient success signal for a
  hardware-free test, but a real headset would still be the only way to visually confirm final
  image correctness. Out of scope for this project until real hardware is available.
- The submitted texture this session is a static solid-color clear, not a live per-frame pipeline —
  notes/25 §3c's "still needs a proper per-frame pipeline" caveat still applies; `Submit` itself is
  now proven, but calling it every frame at 60-90fps from inside `proxy_d3d9.c`'s existing per-frame
  hook, with non-stalling synchronization (the current `flush_d3d9` helper is a synchronous stall,
  fine for a one-shot POC, wrong for a hot path) is real remaining engineering work.
- Per this session's explicit scope, **no Psychonauts integration was attempted** — this stays a
  standalone POC in `tools/vr-bridge/poc_submit_test/`. The concrete next step is wiring this same
  vtable-dispatch + shared-surface pattern into `tools/proxy-d3d9/proxy_d3d9.c`'s existing per-eye
  render targets, once a per-frame (non-stalling) synchronization strategy is designed.
- `WaitGetPoses`'s returned pose arrays were zeroed and never inspected (not needed for this
  session's Submit-focused goal) — a real integration would want to read back at least the HMD pose
  for the render loop, even against the null driver's presumably-identity/fixed pose.

## 6. Repo sync and cleanup

No game files touched. No debugger used (the vectored-exception-handler technique in §3 replaced any
need for x64dbg this session — a lighter-weight, in-process technique worth remembering for future
POC-level crash diagnosis, distinct from this project's usual x64dbg-automate live-game debugging).
SteamVR's own background services remain running after this session ends (normal — they're meant to
run as a persistent OS-level service once started, not something this session should kill). The
user's own Psychonauts process (PID 7052) was confirmed running, unaffected, before and after all
work.

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; `tools/vr-bridge/
  poc_openvr_init/openvr_init_poc.c` updated in place (unbuffered stdout, vectored exception handler,
  the vtable/`__thiscall` fix — all kept as reusable diagnostic + working code, not reverted); new
  `tools/vr-bridge/poc_submit_test/` (source + build script + built .exe).
- **System-level changes** (outside any repo, on this machine only — not committed anywhere, correct
  per this project's scope): SteamVR's `default.vrsettings` (x2), the new per-user
  `steamvr.vrsettings` override, and `openvrpaths.vrpath`'s config/log paths (via `vrpathreg.exe`).
- **modding-notes / dev-archive**: to be synced this session (this note + updated `poc_openvr_init`
  source + new `poc_submit_test/` — no game assets, only original code/vendored-SDK/build scripts).
- **Mod repo** (`psychonauts-vr`): not touched, per the task's own instruction (standalone bridge
  work, not yet game integration).
