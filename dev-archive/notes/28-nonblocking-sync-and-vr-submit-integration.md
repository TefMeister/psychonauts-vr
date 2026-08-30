# Session 28 — Non-Blocking Sync Mechanism Fixed, Real proxy_d3d9.c VR Submission Integration Built

Date: 2026-08-17. Follows directly from notes/27 (null-driver enabled, OpenVR init succeeds,
`IVRCompositor::Submit` PASSES end-to-end via standalone POCs, but explicitly flags the sync
mechanism as a synchronous GPU-flush stall unfit for a real per-frame hot path). **The user's own
game (PID 7052, StartTime 12:41:57) ran the entire session, untouched** (no attach, no kill, no
write to the game directory) — everything below happened in `tools/vr-bridge/` standalone POCs and
`tools/proxy-d3d9/proxy_d3d9.c`'s source (not yet deployed/tested against the real game).

## 0. Summary (read this first)

1. **Sync mechanism fixed and verified**, in `tools/vr-bridge/poc_submit_test/`: replaced the
   synchronous GPU-flush-and-wait helper with a proper non-blocking, double-buffered technique.
   Measured, real, repeated evidence: the OLD blocking helper stalls **~15-16ms per call**
   (dominated by `Sleep(1)`'s Windows timer-granularity rounding — a real, damning finding in its
   own right, not just "it's slow"); the NEW non-blocking poll costs **1.2-16.7 microseconds** —
   roughly a **1000-6000x** improvement — with zero pixel mismatches and zero dropped frames across
   4/4 clean runs (90 simulated frames each).
2. **A real, load-bearing architectural finding that changes the whole integration design**,
   discovered empirically via a second standalone POC (`tools/vr-bridge/poc_dual_device_shared/`):
   Psychonauts' own D3D9 device is a **plain (non-Ex)** device — confirmed a plain device **cannot**
   open a shared handle a separate D3D9Ex device originated (`hr=0x8876086C`/`D3DERR_INVALIDCALL`,
   tried two ways). This rules out any zero-copy design; the real integration needs a CPU-visible
   round trip (`GetRenderTargetData` + `UpdateSurface`) between the game's device and a private
   bridge D3D9Ex device.
3. **That round trip itself needed a second non-blocking fence**, also discovered empirically: an
   initial one-fence implementation produced a real, reproducible pixel mismatch (stale/torn data
   reaching D3D11); root-caused with a diagnostic blocking-flush test (confirmed the hypothesis
   before writing the real fix) to Device A's `UpdateSurface` being itself an async GPU upload that
   needs its own completion fence. The fully-fixed two-hop, double-buffered, fully-non-blocking
   pipeline passed 3/3 clean runs (150 simulated frames each), zero mismatches, zero waits anywhere.
4. **This validated design is now wired into `tools/proxy-d3d9/proxy_d3d9.c`**, additively, gated
   behind a runtime flag (`PSYVR_ENABLE_SUBMIT=1`, default OFF) — the existing, working
   side-by-side monitor composite is completely untouched. Builds clean (32-bit, links the vendored
   OpenVR SDK + d3d11/dxgi). **Not yet live-tested against the real game** — the DLL's pre-existing
   inline-hook mechanism (unrelated to this session's changes) hardcodes absolute addresses that
   only exist in `Psychonauts.exe`'s own address space, so it can only be meaningfully tested by
   loading it into the real game process; a standalone loader-only sanity check confirmed the DLL's
   imports (d3d9/d3d11/dxgi/openvr_api) all resolve correctly and the failure mode is specifically
   "wrong process," not a linking/import problem.
5. **The user needs to close their current game session** for the orchestrating session to deploy
   this DLL and test it for real — see §5.

## 1. Sync mechanism fix (`tools/vr-bridge/poc_submit_test/submit_test_poc.c`)

### 1a. The problem, precisely

notes/27's `flush_d3d9()` helper issued a D3D9 `D3DQUERYTYPE_EVENT` query, then looped
`GetData(..., D3DGETDATA_FLUSH)` with `Sleep(1)` between calls **until it returned `S_OK`** — a
genuine synchronous CPU stall. The key clarification (not obvious going in): `IDirect3DQuery9::GetData`
itself never blocks, with or without `D3DGETDATA_FLUSH` — it always returns immediately with
`S_OK`/`S_FALSE`. The stall came entirely from the caller's own `while` + `Sleep(1)` loop, not from
D3D9 requiring it.

### 1b. The fix

Standard real-world technique: **double-buffer the shared surface** (N=2) and use a **single
non-blocking poll per frame**, never a wait loop:
- Each simulated "frame" renders into `buffer[i % 2]`, issues an `EVENT` query on it, and kicks it
  once with `D3DGETDATA_FLUSH` (a single non-blocking call, not a loop) so the driver submits the
  work promptly instead of batching indefinitely.
- The buffer submitted to `IVRCompositor::Submit` each frame is always `buffer[(i+1) % 2]` — one
  frame behind the buffer currently being written — polled with exactly **one** non-blocking
  `GetData(NULL, 0, 0)` call. By construction that buffer had a full frame's wall-clock time to
  finish on the GPU, so in practice it's always ready; if it somehow weren't, the code simply skips
  that frame's submission (a dropped/retried-next-frame frame) rather than waiting.

### 1c. Measured results (real hardware, GTX 1660 SUPER, 4/4 clean runs)

```
[C1] OLD blocking helper over 20 iters: min=14.99ms avg=15.87ms max=16.09ms   (run 1)
[D5] NEW non-blocking poll: min=1.40us avg=2.48us max=12.50us (89 samples), submitted=89 skipped=0 mismatches=0

Run 2: OLD min=3.53ms avg=15.03ms max=16.55ms | NEW min=1.20us avg=2.37us max=10.10us, 89/89 submitted, 0 mismatches
Run 3: OLD min=7.14ms avg=15.50ms max=16.19ms | NEW min=1.30us avg=2.34us max=16.70us, 89/89 submitted, 0 mismatches
Run 4: OLD min=5.45ms avg=15.37ms max=16.12ms | NEW min=1.30us avg=2.28us max=7.40us,  89/89 submitted, 0 mismatches
```

Correctness verified each run via a D3D11 readback comparing the actually-submitted pixel content
against the color that should have been written N-1 frames earlier — always an exact match, never
stale/torn. `FRAME_W/H` bumped to `1920x1080` (real backbuffer scale, not the earlier POCs' tiny
64x64) so the timing comparison is representative of real per-eye render-target sizes.

## 2. Real integration groundwork: cross-device sharing is more constrained than assumed

### 2a. The blocking discovery

`tools/proxy-d3d9/proxy_d3d9.c`'s own device is created via `Direct3DCreate9` →
`IDirect3D9::CreateDevice` (confirmed: this file only ever hooks `IDirect3D9::CreateDevice`, never
`CreateDeviceEx`) — a **plain (non-Ex)** `IDirect3DDevice9`. Only a D3D9Ex device's `CreateTexture`
can produce a shared handle `ID3D11Device::OpenSharedResource` can open (a documented D3D9Ex
requirement). The open question — untested until this session — was whether a plain device could at
least *open* a handle an Ex device originated (same process, same adapter), which would let the
game render straight into a D3D11-shareable surface with zero extra copies.

Built `tools/vr-bridge/poc_dual_device_shared/dual_device_poc.c` to test this directly: Device A
(D3D9Ex) originates a shared handle; Device B (plain, standing in for the real game device) attempts
to open it two ways — as a direct render-target destination, and as a `StretchRect` destination via
its own `CreateTexture(pSharedHandle=<Device A's handle>)` call. **Both failed identically**:
`hr=0x8876086C` (`D3DERR_INVALIDCALL`). Confirmed, not assumed: a plain device cannot touch an
Ex-originated shared surface *at all*, by any method tried, on this machine.

### 2b. The fallback design, and its own hidden sync bug

The only remaining path: a CPU-visible round trip. `GetRenderTargetData` and `UpdateSurface` both
require source+destination to belong to the *same* device (a real MSDN constraint), so the chain is:
game device renders → `GetRenderTargetData` into a game-device-owned `D3DPOOL_SYSTEMMEM` surface →
CPU `memcpy` into a bridge-device-owned `D3DPOOL_SYSTEMMEM` surface → bridge device's
`UpdateSurface` into its own `D3DPOOL_DEFAULT` shared texture → D3D11 opens that.

First attempt used only ONE fence (a `D3DLOCK_DONOTWAIT` check on the game-device sysmem surface,
gating the `GetRenderTargetData` hop) and got **`final-content-match=NO`** — a real, reproducible
bug. Root-caused (not guessed) with a diagnostic test: adding a single blocking flush on the bridge
device (Device A) right before the D3D11 readback made the mismatch disappear, confirming the
hypothesis that `UpdateSurface` — itself an asynchronous GPU upload — needs its **own** completion
fence before a downstream reader (D3D11/the compositor) touches the destination, independent of the
first hop's fence.

### 2c. The fully-fixed, fully non-blocking two-hop pipeline

Implemented a second fence: each of Device A's two shared-texture slots gets its own
`D3DQUERYTYPE_EVENT` query, issued+kicked (non-blocking) right after each `UpdateSurface`, and
checked non-blockingly before either (a) writing new content into that slot again or (b) treating
that slot as safe to hand downstream. Both hops are independently double-buffered.

**Result: PASS, 3/3 clean runs** (150 simulated frames each):
```
[P2-1] GetRenderTargetData over 150 iters: min=0.0057ms avg=0.0097ms max=0.0520ms   (NOT a stall)
[P2-2] Non-blocking poll cost (both hops): min=0.80us avg=221.69us max=1724.40us (149 samples)
[P2-3] hop1 (B-readback -> A-upload): promoted=149 skipped=0
[P2-4] hop2 (A-upload-complete -> verified-ready): verified=148 skipped=0 mismatches=0
```
`GetRenderTargetData` itself turned out NOT to be a stall on this hardware/driver (sub-0.1ms, a
real, measured answer to a well-known historical D3D9 performance question, not assumed either
way) — the earlier mismatch was purely a missing-fence correctness bug, not a hidden stall.

## 3. Real `proxy_d3d9.c` integration (additive, off by default)

Wired the now-fully-validated two-hop design directly into `tools/proxy-d3d9/proxy_d3d9.c` as
"Milestone 8." Every new symbol is prefixed `VRBridge_`/`g_vr`:

- **`VRBridge_ReadEnableFlag()`** (called from `DllMain`/`DLL_PROCESS_ATTACH`): reads
  `PSYVR_ENABLE_SUBMIT` once. Unset/not `"1"` → `g_vrSubmitEnabled=FALSE` (the default) → every
  other VRBridge function is a guarded no-op, and the file's behavior is byte-for-byte identical to
  before this session.
- **`VRBridge_Init()`** (called lazily from `SetupStereoSurfaces`, i.e. on `CreateDevice` and every
  `Reset`): creates the private D3D9Ex "Device A" (matched to the game's adapter via
  `IDirect3D9::GetAdapterIdentifier`/LUID comparison, same proven mechanism as the POCs), a D3D11
  device, and initializes OpenVR + `IVRCompositor` (the notes/27 vtable-deref + `__thiscall`
  dispatch fix, carried forward verbatim).
- **`VRBridge_CreateEyeBuffers()` / `VRBridge_ReleaseEyeBuffers()`**: per-eye double-buffered
  sysmem/shared-texture/query state (the `VRBridgeEyeState` struct), sized to
  `g_bbWidth`/`g_bbHeight` and recreated automatically if a `Reset` changes those dimensions.
- **`VRBridge_PumpEye()`**: the per-eye, per-real-frame two-hop pump (hop1: game-device readback →
  promote to Device A when ready; hop2: check Device A's oldest pending upload, `Submit` if ready) —
  a direct, line-for-line-equivalent port of the proven `poc_dual_device_shared` steady-state loop.
- **`VRBridge_OnFrameComposited()`**: called once per real frame from `CandB_AfterBoth_asm`, right
  after both eyes finish rendering into the **existing, unchanged** `g_pEye1Surf`/`g_pEye2Surf` —
  calls `WaitGetPoses` once, then pumps both eyes. This is the ONLY new call site inside the
  existing render path, and it runs *before* the existing code switches the render target back to
  the real backbuffer — it never touches `g_pRealBackBuffer` or anything the monitor composite
  (`Hook_Present`, unchanged) depends on.
- **`VRBridge_Shutdown()`** (called from `DllMain`/`DLL_PROCESS_DETACH`): releases everything,
  `VR_ShutdownInternal()`.

**Build**: `tools/proxy-d3d9/build.ps1` updated to link `-ld3d11 -ldxgi` (safe — no name collision
with this DLL) plus the vendored `openvr_api.lib`, and to copy `openvr_api.dll` alongside the built
`d3d9.dll` (both must ship together for the VR path to work — `Direct3DCreate9Ex` itself is resolved
dynamically via `GetProcAddress` on the already-`LoadLibrary`'d real system `d3d9.dll`, deliberately
NOT linked as an import, to avoid any risk of the loader resolving a `-ld3d9` import back to this
DLL itself when it's loaded into the game process under the name `d3d9.dll`). **Build is clean**
(two pre-existing, unrelated warnings only — `EXTERN_C` macro redefinition from the vendored OpenVR
header, and a harmless `dllexport`-on-redeclaration note for `Direct3DCreate9` — both present before
this session's changes too).

One real bug found and fixed during code review (not live-tested, just careful reading): an early
draft called `IDirect3DSurface9::UnlockRect` unconditionally after a `LockRect(..., D3DLOCK_DONOTWAIT)`
call regardless of whether that Lock actually succeeded — fixed to only `UnlockRect` inside the
`lockHr == S_OK` branch (calling `UnlockRect` without a matching successful `Lock` is undefined
D3D9 usage). Also relocated the small pre-existing globals block (`g_pDevice`, `g_pEye1Surf`,
`g_pEye2Surf`, `g_bbWidth`/`g_bbHeight`, `g_stereoReady`) earlier in the file — pure reordering, no
behavior change — so the new VR bridge code (which reads them) doesn't need forward declarations.

## 4. What validation was and wasn't possible this session

**Possible and done**: the underlying sync mechanism (§1) and cross-device architecture (§2) were
both fully proven standalone, with real repeated measurements and correctness checks, entirely
outside the game process. The integrated `proxy_d3d9.c` **compiles and links cleanly**.

**Not possible this session, and why**: this DLL's pre-existing inline-hook mechanism (from
notes/13 onward, untouched by this session) hardcodes absolute addresses (e.g. `0x004FEDA0` for
`CandB`) that are only valid inside `Psychonauts.exe`'s own address space — confirmed directly via
`VirtualQuery` returning failure (address completely unmapped) when queried from any other process.
A standalone loader-only test (`LoadLibraryA` + `GetProcAddress(Direct3DCreate9)` + calling it, from
a throwaway console EXE, deliberately never touching the real game) confirmed the DLL's imports all
resolve correctly (`d3d9`/`d3d11`/`dxgi`/`openvr_api` all link and load — a `DLL_INIT_FAILED`
error, not a missing-module/missing-export error) but then crashes during `DllMain`'s pre-existing
`InstallInlineHooks()` call, exactly as expected given the hardcoded-address read happens before my
new `VRBridge_ReadEnableFlag()` call — i.e. the crash is in unrelated, pre-existing code, in the
wrong (non-game) process, not evidence of a problem in this session's own changes. This DLL has
always required the real game process to test past this point (see `validate.ps1`, which launches
the actual game for exactly this reason) — not a new limitation introduced this session.
`validate.ps1` itself was deliberately NOT run this session because its cleanup step
(`Stop-Process -Force -ErrorAction SilentlyContinue` on any process named `Psychonauts`) would kill
the user's live game session (PID 7052) — exactly the safety rule this project has followed since
notes/20.

## 5. What's proven vs. what's still open, and what's needed to proceed

**Proven this session, with real evidence:**
- The non-blocking sync mechanism works and is dramatically (1000-6000x) faster than the blocking
  approach it replaces, with zero correctness regressions (§1).
- The only viable cross-device architecture for this specific game (CPU round-trip, two
  independently-fenced non-blocking hops) is now fully validated standalone (§2).
- That validated design is now real code in `proxy_d3d9.c`, additive, off by default, compiling
  and linking cleanly (§3).

**Still open, concretely scoped:**
- **Real in-game testing** — needs the user to close their current game session (see below).
- Once testable: confirm `PSYVR_ENABLE_SUBMIT=1` actually reaches `g_vrBridgeReady=TRUE` in the
  live proxy log, confirm `Submit` calls return `VRCompositorError_None` for both eyes every real
  frame, and confirm the existing monitor composite is completely unaffected whether the flag is
  set or not (the core "additive, doesn't destabilize the working path" requirement).
- Frame-rate/latency impact of the CPU round-trip at real game resolution (this session's timing
  numbers are from a synthetic `Clear()`-only workload, not the game's actual rendering) is not yet
  measured — the two-hop pipeline adds roughly 2 frames of latency (by design, to stay non-blocking)
  which is untested for comfort/correctness with real gameplay content.
- `WaitGetPoses`'s returned pose arrays are still zeroed/unused (same open item notes/27 left) —
  not needed for this session's "does Submit reach the compositor from the real game" goal.

**To proceed to real testing, the user needs to close the current Psychonauts session.** Once
closed, the orchestrating session can: copy `tools/proxy-d3d9/d3d9.dll` AND
`tools/proxy-d3d9/openvr_api.dll` into the game directory, set `PSYVR_ENABLE_SUBMIT=1` in the
environment the game launches with, relaunch, and read the live proxy log for `VRBridge_Init`/
`Submit` results — SteamVR's null driver is already confirmed enabled and running from notes/27,
so no additional SteamVR setup is needed.

## 6. Repo sync

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; `tools/vr-bridge/poc_submit_test/submit_test_poc.c`
  rewritten (non-blocking double-buffered sync, §1); new `tools/vr-bridge/poc_dual_device_shared/`
  (source + build script + built .exe, §2); `tools/proxy-d3d9/proxy_d3d9.c` and `build.ps1` updated
  (§3) — `d3d9.dll`/`openvr_api.dll` rebuilt in place but NOT deployed to the game directory.
- **modding-notes / dev-archive**: synced this session (this note + all changed/new source — no
  game assets, only original code/build scripts, consistent with this project's legal/scope
  boundaries).
- **Mod repo** (`psychonauts-vr`): NOT touched this session — unverified in real gameplay (per this
  project's own standing practice, see notes/22/23 for precedent).
