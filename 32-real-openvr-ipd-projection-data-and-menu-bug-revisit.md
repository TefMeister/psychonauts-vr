# Session 32 — Real OpenVR-Queried IPD/Projection Data Replaces Hardcoded Stereo Constants

Date: 2026-08-17. Follows directly from notes/30/31 (VR bridge proven live, `WaitGetPoses` cost
investigated to its honest conclusion) — this session deliberately pivoted off the performance
investigation (paused pending real headset access, per the user's own call) onto other
headset-free work. **The user's game was NOT running at session start** (confirmed via
`Get-Process`), so this session had normal freedom for its own isolated test launches.

## 0. Summary (read this first)

1. **Task 1 (primary): the hardcoded `STEREO_HALF_IPD=3.25` constant and the estimated
   focus-distance-derived shear term are now driven by REAL OpenVR-queried data whenever it's
   available**, with a fully transparent fallback to the pre-existing hardcoded behavior
   otherwise. Confirmed via a standalone POC (`tools/vr-bridge/poc_ipd_query`, run against the
   live null-driver runtime BEFORE any `proxy_d3d9.c` changes) and then re-confirmed live inside
   the game via a full isolated self-test with `PSYVR_ENABLE_SUBMIT=1`.
2. **Real numbers, not placeholders** (from the live null-driver runtime, confirmed twice —
   standalone POC and in-game):
   - `IVRSystem::GetFloatTrackedDeviceProperty(Hmd, Prop_UserIpdMeters_Float)` = **0.063000 m
     (63.00 mm) exactly** — the standard OpenVR default IPD.
   - `IVRSystem::GetEyeToHeadTransform` = **exactly ±0.0315 m (±31.50 mm)**, perfectly symmetric,
     identity rotation — i.e. this driver's two eyes are exactly symmetric about the head origin.
   - `IVRSystem::GetProjectionRaw` = **exactly `l=-1, r=1, t=-1, b=1` for BOTH eyes** — a
     perfectly symmetric 90°-per-side default frustum, i.e. **this driver reports NO real
     off-axis asymmetry data** (confirmed by measurement, not assumed).
   - `IVRSystem::GetRecommendedRenderTargetSize` = **1656 × 1840 per eye** (matches notes/27's
     earlier finding via a different POC — consistent).
3. **Comparison to the previous hardcoded estimates**: real half-IPD (3.15 world units) is only
   **~3% below** the notes/15-derived hardcoded guess (3.25 units) — a genuine, if modest,
   cross-validation of that earlier estimate. The real per-eye offset is exactly symmetric under
   this driver, so it doesn't (yet) exercise the "may not be symmetric" case the task flagged —
   but the code now handles that case correctly should a real headset ever report otherwise.
4. **Submit re-verified working after the change**, same evidence method as every prior session:
   live proxy log shows `VRBridge: Submit(eye=N) OK (frame=N)` climbing continuously for both
   eyes (frame 3 through 700+ in a ~25s window), and the `SVSCF stereo-correct` log line
   confirms the real values are actually being consumed (`d=-3.150`/`d=3.150`, `dSrc=openvr`).
5. **Task 1 item 4 (higher internal render resolution)**: investigated, found to be a genuinely
   separate, larger undertaking — documented (§4), not attempted, per the task's own explicit
   permission to do so rather than risk a rushed mid-pipeline change.
6. **Task 2 (menu black-left-eye bug)**: bounded-effort hypothesis update only, not re-diagnosed
   from scratch — see §5. The real `GetProjectionRaw` data doesn't change the existing hypothesis
   because this driver reports no real asymmetry to test against; the real IPD being slightly
   *smaller* than the old hardcoded value (3.15 vs 3.25) very slightly reduces rather than
   increases the frustum-boundary risk the notes/23 hypothesis describes, so no new lead here.

## 1. Standalone confirmation first, per this project's established practice

Before touching the live-tested `proxy_d3d9.c` stereo-correction code, a new standalone POC
(`tools/vr-bridge/poc_ipd_query/ipd_query_poc.c`) queried all four target APIs against the
already-running, already-proven null-driver SteamVR stack (confirmed still up via `Get-Process`:
`vrserver`/`vrcompositor`/`vrdashboard`/`vrmonitor`/`vrwebhelper`, running continuously since
notes/27). Build clean (same one pre-existing harmless `EXTERN_C` redefinition warning every
OpenVR-SDK-including file in this project has). Full raw output:

```
[1] GetRecommendedRenderTargetSize -> 1656 x 1840 per eye

[2] GetFloatTrackedDeviceProperty(Hmd, Prop_UserIpdMeters_Float) -> 0.063000 meters (err=0)
    = 6.3000 cm, half-IPD = 3.1500 cm

[3] GetEyeToHeadTransform(Left):
    [  1.00000  0.00000  0.00000 -0.03150 ]
    [  0.00000  1.00000  0.00000  0.00000 ]
    [  0.00000  0.00000  1.00000  0.00000 ]
    translation (x,y,z) = (-0.031500, 0.000000, 0.000000) meters -> x = -3.1500 cm

[3] GetEyeToHeadTransform(Right):
    translation (x,y,z) = (0.031500, 0.000000, 0.000000) meters -> x = 3.1500 cm

[4] GetProjectionRaw(Left)  -> left=-1.000000 right=1.000000 top=-1.000000 bottom=1.000000
[4] GetProjectionRaw(Right) -> left=-1.000000 right=1.000000 top=-1.000000 bottom=1.000000
    center-offset (l+r)/2 = 0.000000  (both eyes)
```

**One real ABI wrinkle this POC hit that no prior session's OpenVR calls did**:
`GetEyeToHeadTransform` returns `HmdMatrix34_t` (48 bytes) BY VALUE. The MSVC x86 ABI this
installed SteamVR build's `vrclient.dll` was compiled with passes a hidden pointer to
caller-allocated return storage as the FIRST explicit parameter (right after `this`) for any
large-struct-returning method. Rather than trust a cross-compiler (clang/mingw calling
MSVC-compiled code) struct-return code-generation guess, the function pointer type was declared
explicitly as a void-returning `__thiscall` taking that pointer as an explicit argument —
the same "write out the ABI contract by hand, don't assume it" discipline notes/27 already
established for this file's vtable-dispatch fix. Worked correctly first try, confirmed by the
POC's real, plausible output above. `GetProjectionRaw` and `GetFloatTrackedDeviceProperty` both
avoid this issue entirely (out-params / plain `float` return) — part of why the task specifically
called out `GetProjectionRaw` over `GetProjectionMatrix` (which also returns a struct by value).

## 2. The `GetProjectionRaw` -> shear-term derivation

The task asked for `GetProjectionRaw` to "directly replace/inform" notes/24's off-axis math. The
existing correction (notes/24) already works by inserting a shear `X' = X + k·Z` into eye space
before the game's own **symmetric**-width projection is applied — this is mathematically the
standard "shift-lens" stereo-camera technique, and it is *exactly* equivalent to a genuine
asymmetric frustum with the *same* angular width (`r-l` unchanged) but a shifted center. This
equivalence is what makes a clean substitution possible without needing to re-derive the whole
correction: the frustum-center ray (in eye space, RH convention, Z negative in front) satisfies
`X/(-Z) = (l+r)/2`; the correction's own "zero disparity" condition is `X + k·Z == 0` at that same
ray, i.e. `X = -k·Z`; equating the two gives:

```
k = (l + r) / 2
```

directly from `GetProjectionRaw`'s raw tangent bounds — no unit conversion needed (both sides are
already dimensionless tangent ratios in the same space this file's `k` already lives in), and no
decomposition of an unknown World*View matrix required (same "derive only from what's known"
principle notes/24's own Y=Proj⁻¹·X·Proj derivation used). This is implemented in
`VRBridge_QueryRealGeometry()` and only overrides the old focus-distance-estimated `k` when
`|k_real| > 1e-4` (i.e. the driver actually reports asymmetry) — this null driver reports exactly
`0.0` for both eyes, so the fallback path is the one actually exercised this session; it will be
automatically superseded the moment a real headset (or any driver reporting genuine per-eye
asymmetry) is connected, with no further code changes needed.

## 3. Implementation in `proxy_d3d9.c` — additive, fully backward-compatible

- New IVRSystem dispatch (same vtable-deref + `__thiscall` fix as the existing IVRCompositor
  dispatch), fetched in `VRBridge_Init` right after IVRCompositor — but **not load-bearing** for
  Submit itself: a failure here logs and continues (the geometry queries are a bonus, not
  required for the already-proven Submit path).
- `VRBridge_QueryRealGeometry()` (new function): queries IPD, both eyes' `GetEyeToHeadTransform`
  and `GetProjectionRaw`, and `GetRecommendedRenderTargetSize`; converts meters to world units via
  a new `WORLD_UNITS_PER_METER = 100.0f` constant (the notes/15/18-established "1 world unit ≈
  1cm" calibration — documented inline with its provenance); caches results in
  `g_realHalfIPD[2]`/`g_realShearK[2]`/`g_realShearValid[2]`; sets `g_vrGeomValid = TRUE` only on
  full success.
- `Hook_SetVertexShaderConstantF` (the actual per-draw stereo correction — runs unconditionally,
  VR-submit on or off) now checks `g_vrGeomValid`: when TRUE, `d` comes from
  `g_realHalfIPD[eyeIdx]` (a real, potentially-asymmetric per-eye value, no longer
  `STEREO_HALF_IPD*sign`) and `k` comes from `g_realShearK[eyeIdx]` whenever
  `g_realShearValid[eyeIdx]` (real off-axis data), else the pre-existing focus-distance estimate.
  When `g_vrGeomValid` is FALSE (the default for anyone without `PSYVR_ENABLE_SUBMIT=1` set and
  SteamVR installed — i.e. most users of this mod, who play the monitor side-by-side stereo path
  with no VR runtime at all), behavior is **byte-for-byte identical to before this session**.
- The throttled `SVSCF stereo-correct` log line now also prints `dSrc=`/`kSrc=` (`openvr` or
  `hardcoded`/`focus-est`) so a live log unambiguously shows which source is active.
- Build clean (`build.ps1`) — same two pre-existing, unrelated warnings as every prior session
  touching this file (`EXTERN_C` redefinition; `Direct3DCreate9` dllexport-on-redeclaration).

## 4. Task 1 item 4: higher internal VR render resolution — investigated, not attempted

Real `GetRecommendedRenderTargetSize` = 1656×1840 per eye, vs. the VR-submit path's current eye
buffers at 640×480 (matching the game's own native backbuffer size, per notes/28's design). Why
this wasn't attempted this session:

- The VR-submit eye buffers (`VRBridgeEyeState`'s `sysmemB`/`texA`/`tex11`) are explicitly sized
  from `g_bbWidth`/`g_bbHeight` (`VRBridge_CreateEyeBuffers(&g_vrEye1, ..., w, h)` in
  `VRBridge_OnStereoSurfacesReady`), which come directly from the game's own `CreateDevice`/
  `Reset` back-buffer dimensions — the same values the monitor-composite path's
  `g_pEye1Surf`/`g_pEye2Surf` render targets use.
- Rendering the VR-submitted path at a genuinely higher resolution than the monitor composite
  would require the game to actually **render** its scene at that higher resolution for at least
  the eye-surface passes (`CandB`'s twice-invoked body) — but `CandB`'s output render target is
  set via `SetEyeAndTarget`, which points at the SAME `g_pEye1Surf`/`g_pEye2Surf` the monitor
  composite's `StretchRect` reads from. Decoupling them would mean either (a) rendering the scene
  a *third* time at a different resolution purely for the VR path (real added GPU cost, and a
  nontrivial amount of new plumbing to give `CandB` a second target-resolution-aware code path), or
  (b) upscaling the existing 640×480 content in the CPU round-trip before `UpdateSurface`
  (cheap, but a fake resolution boost — no more real detail than 640×480 has, likely a visible
  quality *downgrade* on a real headset's near-eye optics compared to true native rendering, since
  it would still look blurry/upscaled rather than sharp).
- The genuinely correct fix (real per-VR-path higher-resolution rendering) is a render-target and
  viewport-dimension change threaded through `SetupStereoSurfaces`, `CandB`'s hook, and the
  existing `BuildProjectionMatrix`-derived aspect-ratio math — a real, substantial change to code
  this project has spent many sessions hardening (notes/13 through notes/31), with real risk of
  destabilizing the just-reconfirmed-working Submit path for a benefit (sharper image on
  currently-nonexistent physical hardware) that cannot even be visually verified this session.
- **Recommendation for a future session, once real headset access exists**: prototype the
  higher-resolution VR-only render target as a fully separate, additively-gated path (mirroring
  how the whole Milestone 8 VR bridge itself was built and tested standalone first via
  `tools/vr-bridge/` POCs before touching `proxy_d3d9.c`) — verify a scaled-up `CandB` render
  target + matching viewport/projection-aspect math works correctly in isolation before wiring it
  into the live per-frame hook. Not attempted here, exactly per the task's own explicit permission
  to document rather than risk this session.

## 5. Task 2: menu black-left-eye bug — bounded-effort hypothesis update

Per the task's own explicit "don't spend excessive time here" scoping, this was not re-diagnosed
from scratch. The notes/23 hypothesis ("the fixed-world-unit parallax correction may push the
menu's likely close-up camera framing outside one eye's frustum") was checked against this
session's new real data:

- **`GetProjectionRaw`'s real values don't change the picture**: they're exactly the symmetric
  default (`l=-1,r=1,t=-1,b=1`, §1) under this null-driver runtime — there's no real per-eye
  asymmetry data available to test the hypothesis against without physical headset hardware. The
  off-axis frustum math itself is unchanged in shape (still the notes/24 shear-based technique);
  only its two input numbers (`d`, and now potentially `k`) changed source.
- **The real half-IPD is *smaller*, not larger, than the old hardcoded value** (3.15 vs. 3.25
  world units, §0.3) — if the notes/23 hypothesis is correct (excessive eye separation pushing a
  close-up menu camera's content outside one eye's view frustum), the real value very slightly
  *reduces* that risk margin rather than increasing it. This is a small, not dramatic, change
  (~3%) and not something this session could usefully test without reproducing the actual menu
  screen (which, per notes/23, requires being at that specific UI state with the log capturing the
  moment — not reachable via this session's own isolated title-screen-only launches, since this
  project's synthetic-input mechanism for navigating past the title screen remains unsolved per
  notes/15-18).
- **Disposition**: hypothesis unchanged, not strengthened or refuted by this session's new data.
  No new lead identified. A future session with either working synthetic input past the title
  screen, or the user reproducing the bug live with the (now real-IPD-driven) build deployed,
  remains the concrete next step — unchanged from notes/23's own recommendation.

## 6. Self-test evidence

Isolated launch (game was closed at session start; intro videos silenced, window moved offscreen,
`PSYVR_ENABLE_SUBMIT=1`), same methodology as notes/27-31. Full evidence trail:

```
VRBridge_Init: IVRCompositor ready (vtable=6909F5A8)
VRBridge_Init: IVRSystem ready (vtable=69094F3C)
VRBridge_QueryRealGeometry: real IPD = 0.063000 m (63.00 mm), err=0
VRBridge_QueryRealGeometry: eye=0 eyeToHead.x=-0.031500m (-3.150 world units) projRaw l=-1.0000 r=1.0000 t=-1.0000 b=1.0000 centerOffset=0.000000 (symmetric - no real off-axis data, keeping focus-distance k fallback)
VRBridge_QueryRealGeometry: eye=1 eyeToHead.x=0.031500m (3.150 world units) projRaw l=-1.0000 r=1.0000 t=-1.0000 b=1.0000 centerOffset=0.000000 (symmetric - no real off-axis data, keeping focus-distance k fallback)
VRBridge_QueryRealGeometry: GetRecommendedRenderTargetSize = 1656 x 1840 per eye (current VR-submit eye buffers are 640x480, matching the game's own backbuffer - see notes/32 Sec4 for why this session did not change that)
VRBridge_QueryRealGeometry: g_vrGeomValid = TRUE - stereo correction now uses real OpenVR-sourced IPD/eye-offset values instead of the hardcoded STEREO_HALF_IPD constant
VRBridge_Init: SUCCESS - g_vrBridgeReady = TRUE (640x480 per eye)
VRBridge: Submit(eye=0) OK (frame=3)          <- and climbing continuously, both eyes, through frame 712+ over ~25s
SVSCF stereo-correct: reg=6 phase=1 xScale=1.5377 d=-3.150 focus=193.92 k=0.016244 Y20=-0.484287 Y30=0.4594 dSrc=openvr kSrc=focus-est
SVSCF stereo-correct: reg=6 phase=2 xScale=1.5377 d=3.150  focus=190.55 k=-0.016531 Y20=0.484287 Y30=-0.4590 dSrc=openvr kSrc=focus-est
```

`d=-3.150`/`d=3.150` (exactly the real IPD-derived value, per eye, correctly signed) and
`dSrc=openvr` confirm the real values are actually flowing into the live per-draw correction —
not just logged at init and unused. `focus` still tracks the live camera eye→at distance (~190-233
units, matching the title screen's known range) exactly as before — the focus-distance mechanism
itself is unchanged, only feeding into `k` when no real off-axis data is available (`kSrc=focus-est`
here, as expected given §1's symmetric `GetProjectionRaw` result).

## 7. Deployment and cleanup

- **New DLL deployed into the game directory**, replacing the notes/31 build (which was backed up
  first, to this session's scratchpad, matching notes/31's own precedent for exactly this
  situation — additive/off-by-default, so the user's next normal launch without
  `PSYVR_ENABLE_SUBMIT=1` set is completely unaffected).
- Intro videos silenced then fully restored (verified: all four `.bik` files present under their
  real names, no `.silenced` remnants). Test process killed cleanly. No stray debugger/python
  processes (none were used this session — pure standalone-POC + isolated-game-launch work, no
  x64dbg). The user's own game was not running at any point this session, so no interference with
  a live session was possible or attempted.
- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; `tools/proxy-d3d9/proxy_d3d9.c`
  updated (Milestone 9); new `tools/vr-bridge/poc_ipd_query/` (source + build script + built .exe).
- **modding-notes / dev-archive**: synced this session (this note + updated source + new POC — no
  game assets, only original code/build scripts).
- **Mod repo** (`psychonauts-vr`): **not touched this session**, per the task's own explicit
  instruction (modding-notes/dev-archive only this session).
