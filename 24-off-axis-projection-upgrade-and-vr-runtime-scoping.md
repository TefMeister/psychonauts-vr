# Session 24 — Off-Axis (Asymmetric Frustum) Projection Upgrade, and VR Runtime Integration Scoping

Date: 2026-08-17. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install). **The user's own game (PID 9188, per notes/23) was running for this entire
session** — per this project's standing safety rule, it was never touched (no kill, no debugger attach,
no write to the game directory's `d3d9.dll`). This session's work is therefore: (1) a real, carefully
derived and rigorously self-verified code upgrade to the stereo projection math, built and math-checked
but **not live-tested in the actual game this session**; and (2) headset-free research/scoping for real
VR runtime integration, written up but not implemented, per the task's own instructions.

## 1. Task 1: off-axis (asymmetric frustum) projection upgrade

### 1a. What was already true, and what was actually missing

Re-reading the shipped code (notes/14/18) before changing anything found the premise needed a small
correction: the currently-shipped GPU-side correction (`Hook_SetVertexShaderConstantF`, register 6) is
**already a parallel-axis (non-toe-in) offset** — it inserts a rigid view-space translation between View
and Proj and never re-aims either eye's look direction. (There IS a disabled CPU-side rewrite in
`SetEyeAndTarget` that would have re-aimed toward the same look-at point — genuine toe-in — but it's been
dead code since notes/20, explicitly because it isn't load-bearing for the actual GPU-visible correction.)

What was actually missing, and what real VR SDKs do differently: the shipped correction reuses the
**same symmetric projection frustum for both eyes**. A parallel eye offset *with* a symmetric frustum is
only correct for a stereo pair converged at **infinite** distance — every finite-distance point shows
disparity, and that disparity only shrinks asymptotically as distance grows, never reaching zero. Real
VR SDKs instead use an **asymmetric ("off-axis") frustum per eye**, shifted so that a *chosen finite*
convergence/focal distance gets exactly zero disparity, with natural "crossed" parallax nearer than that
and "uncrossed" parallax farther — this is the actual technique this session implements.

### 1b. The formula (researched, not guessed)

Per the task's explicit instruction, the standard formula was looked up rather than derived blind. Web
search confirmed the well-known asymmetric-frustum/parallel-camera-axes technique (horizontal shift
`s = (e/2)·(n/d)` for eye separation `e`, near plane `n`, screen/focal distance `d`) and the documented
`D3DXMatrixPerspectiveOffCenterRH` matrix form:

```
Row0: [2·zn/(r-l), 0, 0, 0]
Row1: [0, 2·zn/(t-b), 0, 0]
Row2: [(l+r)/(l-r), (t+b)/(b-t), zf/(zn-zf), -1]
Row3: [0, 0, zn·zf/(zn-zf), 0]
```

which matches this codebase's own already-validated `D3DXMatrixPerspectiveFovRH`-convention Proj matrix
in the l=-r, t=-b (symmetric) limit — a useful independent cross-check of the whole approach before any
new code was written.

### 1c. A real bug this session's own verification script caught before it reached the game

The first implementation attempt did the "obvious" thing: patch the received WVP matrix's row2/col0
entry, mirroring exactly how the existing code already patches row3/col0 for the translation. **A
standalone Python verification script (not the game — see §1e) caught this as wrong** before any code
was built against it.

The reason: the existing translation patch is safe as a *single matrix entry* only because of a specific
structural fact — inserting a translation between the (unknown, untraced per notes/14 §1b) `World*View`
matrix `M` and `Proj` can only ever perturb `M`'s own row 3, and row 3 is the *only* row an **affine**
`M` is guaranteed to touch in a way that lands on exactly one entry of the final `WVP = M*Proj` product
(worked out by hand: `(M*T)[r][0] = M[r][0] - d·M[r][3]`, and `M[r][3] = 0` for `r<3` for any affine `M`
— so only row 3 changes). A **shear** (needed for the off-axis frustum) lives in `M`'s row 2, which has
no such affine guarantee — `M[r][2]` is genuinely unknown and varies with camera orientation. Patching
`WVP[2][0]` alone silently assumes the camera happens to be exactly axis-aligned at the world origin; the
verification script's first "robustness check" (an arbitrary tilted, off-origin test camera) caught this
immediately — disparity was *not* zero at the intended focus distance, off by an amount that scaled with
how far the test camera was tilted from the trivial case.

### 1d. The fix: a matrix-similarity patch that never needs to know `World*View`

Model the per-eye adjustment as one combined matrix `X` (translate by `-d`, then shear x by `k·z`)
inserted between `M` and `Proj`:

```
WVP_new = M · X · Proj = (M·Proj) · (Proj⁻¹ · X · Proj) = WVP_received · Y
```

`Y = Proj⁻¹·X·Proj` depends only on the (fully known) `Proj` and `X` — never on the unknown `M` — and
works out to differ from the identity matrix only in column 0. So the fix only ever touches 4 of the 16
received floats (the received matrix's own column 0), each computed from the **received matrix's own
row-2/row-3 values** plus known projection constants:

```
A = zf/(zn-zf),  B = zn·zf/(zn-zf)
Y20 = (-d)·xScale / B
Y30 = (-k - A·d/B)·xScale
WVP_new[r][0] = WVP_received[r][0] + WVP_received[r][2]·Y20 + WVP_received[r][3]·Y30   (r = 0..3)
WVP_new[r][c] = WVP_received[r][c]                                          for c != 0
```

`k` (the eye-space shear coefficient) is chosen so a point on the *original, un-offset* camera axis at
the convergence distance `focus` gets exactly zero disparity: solving `x_clip == 0` at eye-space
`X=0, Z=-focus` (RH eye space has negative Z in front of the camera) gives **`k = -d/focus`**. With
`k=0` (i.e. `focus → ∞`) this reduces *exactly* to the old translation-only patch — verified both
algebraically and numerically (see §1e) — so the new code is a strict superset of the old, not a
parallel code path.

In the **transposed** buffer the hook actually receives (`upload[c][r] = WVP[r][c]`, same convention
notes/18 already established), `WVP`'s column 0 maps to `upload`'s entire row 0 (flat indices 0-3), and
`WVP[r][2]`/`WVP[r][3]` map to `upload` flat indices `8+r`/`12+r`. The implemented C loop:

```c
for (r = 0; r < 4; r++) {
    patched[r] = pConstantData[r] + pConstantData[8 + r] * Y20 + pConstantData[12 + r] * Y30;
}
```

### 1e. Verification (headset-free, game-free — the game was running and could not be used)

Because the user's own game session was live for the entire task, the usual isolated-launch
screenshot-comparison self-test (notes/14/18-style) could **not** be performed this session. In its
place, three independent, increasingly rigorous standalone checks were run (Python, no game, no
debugger — scripts kept in the session scratchpad):

1. **Basic case** (`eye=(0,0,0)`, camera looking straight down -Z): confirmed the OLD (symmetric)
   behaviour never reaches zero disparity at any finite depth (asymptotic only), while the NEW (off-axis)
   behaviour hits **exactly 0.000000** disparity at the chosen focus depth (200 units) and shows
   correctly-signed "crossed" (negative) disparity nearer than focus, "uncrossed" (positive) farther —
   the textbook off-axis signature.
2. **Robustness check** (arbitrary tilted, off-origin test camera, three different lateral offsets from
   center-frame): this is what caught the row2/col0-only bug in §1c. After the fix, disparity is
   **exactly 0.000000** at the focus distance for *all three* lateral offsets and the tilted/off-origin
   camera — confirming the fix is genuinely camera-pose-agnostic, not just correct for the trivial case.
3. **Byte-for-byte C-code mirror**: a third script reimplements the *exact* flat-index formula from the
   real C code (same indices `[r]`, `[8+r]`, `[12+r]`, same `Y20`/`Y30` expressions) against a properly
   transposed upload buffer, and reprojects through it exactly as the game's own shader would consume
   it. Result: identical to check 2 — exact zero disparity at focus, for the tilted camera, at all three
   lateral offsets. This directly validates the *actual code path*, not just the abstract algebra.

All three checks' outputs are reproducible; the scripts are in
`%TEMP%\claude\...\scratchpad\verify_offaxis.py`, `debug_Y.py`, `verify_flat_index_c_mirror.py` (session
scratchpad, not part of any repo).

### 1f. What's implemented in `tools/proxy-d3d9/proxy_d3d9.c`

- `BuildProjectionMatrix`'s inline hook now also captures `zn`/`zf` from entry arguments (previously
  only `rawFov`/`aspect` were read) — needed for the off-axis `A`/`B` terms. Same "read from entry
  arguments, not the output buffer" pattern notes/14 already established as the only safe option for
  this function.
- A new per-frame `g_focusDistance`, computed in `BVM_OnEntry_asm` from the **live eye→at distance**
  (reusing data already cached for the disabled CPU-side rewrite), clamped away from a near-zero floor.
  This is a deliberate choice over a fixed guessed constant: since the game's own camera already follows
  a look-at target, the convergence point naturally tracks wherever the camera is actually aiming each
  frame.
- `Hook_SetVertexShaderConstantF`'s correction is now the general 4-entry column-0 patch from §1d,
  replacing the old single-line `patched[3] += (-d)*xScale`. `STEREO_HALF_IPD` (the fixed 3.25-unit
  half-IPD) is unchanged, now with an explicit `TODO(real headset)` comment on both it and
  `g_focusDistance` documenting exactly what a real OpenVR/OpenXR runtime would replace them with
  (real per-eye IPD, and a directly-supplied per-eye asymmetric projection matrix that wouldn't need
  this whole focal-distance-estimation step at all).
- Build: clean (`build.ps1`), only the pre-existing harmless `Direct3DCreate9` redeclaration warning.
  `tools/proxy-d3d9/d3d9.dll` rebuilt.

### 1g. Honest disposition — NOT live-tested this session

**The math is now verified as rigorously as is possible without the actual game** — three independent
checks, one of which caught and drove the fix of a real bug before it ever reached compiled code. What
is **not** verified: that the live asm stack-offset changes to `Hook_BuildProjectionMatrix` are correct
in the actual running process (the offsets were extended by the same established pattern used for
`Hook_BuildViewMatrix`'s existing 4-argument stub, but only a live run confirms it), and that the
in-game visual result looks like genuine parallel/off-axis stereo rather than something subtly broken.
**Not pushed to the mod repo this session** — per this project's own established practice (e.g.
notes/22), an unverified-in-game fix isn't shipped to the public repo, no matter how solid the offline
math check is. **Concrete next step**: once the user closes the current game session, a follow-up
session should copy the new DLL in, relaunch, and run the same 0/3.25/60-unit controlled screenshot
comparison notes/14/18 used, **plus** one off-axis-specific check: log-compare the `SVSCF stereo-correct`
line's `Y20`/`Y30`/`focus` values against `g_baseEye`/`g_baseAt` at the same moment (should show `focus`
tracking the live eye→at distance), and ideally frame a shot where a discrete object sits near the
camera's own look-at point — under the new off-axis correction that object's two-eye images should look
nearly coincident, unlike under the old code where any finite-distance object should always show some
disparity.

## 2. Task 2: what real VR runtime integration would take (research/scoping only, not implemented)

No SDK was installed this session, per the task's explicit instruction. This is a plan for a future
session, most likely once the user has headset/SteamVR access.

### 2a. The core technical obstacle: OpenVR has no D3D9 support

Confirmed via web search (not assumed): modern OpenVR's `IVRCompositor::Submit()` **does not support
D3D9 textures at all**. `ETextureType` only lists D3D11 (`TextureType_DirectX`), D3D12, Vulkan, OpenGL,
and Metal — there is no D3D9 entry, and there never has been a supported one (some historical confusion
exists because early D3D9/D3D9Ex attempts against the compositor produced specific error codes —
`VRCompositorError_TextureUsesUnsupportedFormat` for plain D3D9, `VRCompositorError_InvalidTexture` for
D3D9Ex — rather than silently working). This means the render target surfaces this DLL already creates
(`g_pEye1Surf`/`g_pEye2Surf`, plain D3D9 `IDirect3DSurface9`) **cannot be hand ed directly to
`IVRCompositor::Submit`** under any current OpenVR SDK version.

### 2b. The realistic bridging approach: D3D9Ex shared surface → D3D11 texture

The standard, documented workaround (confirmed via search of GameDev.net/MSDN discussion of this exact
problem) is:

1. Create the game's D3D9 device as **D3D9Ex** (`IDirect3D9Ex`/`IDirect3DDevice9Ex`, not plain D3D9) —
   D3D9Ex adds proper cross-API shared-surface support that plain D3D9 lacks. This is itself a nontrivial
   change: the game creates its own device via `IDirect3D9::CreateDevice` (already hooked in this file),
   and Psychonauts' import table only references plain `Direct3DCreate9` (confirmed in early recon,
   notes/03) — getting a D3D9Ex device would require either intercepting `CreateDevice` and internally
   creating a *second*, separate D3D9Ex device purely for the shared-surface bridge (leaving the game's
   own device untouched), or a full "Direct3DCreate9Ex-only" rewrite (higher risk, more invasive).
2. Create each eye's render target with the `pSharedHandle` parameter of `CreateTexture` (D3D9Ex
   surfaces created this way can be opened from a D3D11 device via `ID3D11Device::OpenSharedResource`
   against that same handle).
3. Stand up a **separate, minimal D3D11 device** purely for the OpenVR bridge (SteamVR's compositor
   expects D3D11 textures) — open each eye's shared D3D9Ex surface as a D3D11 texture there, then call
   `IVRCompositor::Submit(EVREye_Left/Right, &d3d11Texture, ...)` once per eye per frame instead of (or
   in addition to) the current `StretchRect`-into-backbuffer composite.
4. Synchronization matters: a shared D3D9Ex→D3D11 surface needs proper GPU-side sync (D3D9Ex's shared
   surfaces are more forgiving than plain D3D9's, but this is still a real correctness risk worth testing
   carefully, not assuming away).

This is a genuinely separate, sizeable piece of work from anything done so far in this project — it adds
a second graphics API (D3D11) and a second device into a process that has otherwise been pure D3D9, and
introduces real synchronization considerations the project hasn't had to deal with yet.

### 2c. Headset-free validation path: SteamVR's null driver

Confirmed via web search: SteamVR ships a **null driver** specifically for headset-free development/
testing (used by several public "run SteamVR without a headset" guides). To enable it:

1. Edit `<Steam>\steamapps\common\SteamVR\drivers\null\resources\settings\default.vrsettings` —
   set `"enable": true`.
2. Edit SteamVR's own `default.vrsettings` — set `"requireHmd": false`, `"forcedDriver": "null"`,
   `"activateMultipleDrivers": true`.
3. In the SteamVR settings UI: disable "Pause VR when headset is idle" (Video tab) and enable "Override
   Windows Power Scheme" (Startup/Shutdown tab) — otherwise the null-driver "headset" goes to standby
   after a few seconds, which would interrupt frame-submission testing.

This would let a future session validate the actual `IVRCompositor::Submit()` call path — confirming
frames are accepted, checking for compositor errors, watching frame timing/reprojection behavior — all
**without needing the physical headset to be present**, which directly matches this task's "validate
frame submission code without physical headset hardware" framing. It does not validate comfort/actual
visual correctness in a headset (that still needs the real hardware, later, at the user's home setup),
but it validates the entire plumbing (device creation, shared-surface bridging, Submit call, error
handling) ahead of time.

### 2d. Minimal OpenVR API surface needed

Scoped, not implemented:

- `vr::VR_Init()` / `vr::VR_Shutdown()` — application-type `VRApplication_Scene`.
- `IVRSystem::GetRecommendedRenderTargetSize()` — real per-eye render target dimensions (likely larger
  than this DLL's current fixed backbuffer-sized offscreen surfaces, and probably not exactly the
  desktop window's resolution).
- `IVRSystem::GetProjectionMatrix(EVREye, zNear, zFar)` / `GetEyeToHeadTransform(EVREye)` — **this is
  the single most valuable API for this project specifically**: it directly replaces this session's
  entire off-axis-frustum-estimation mechanism (§1) with the runtime's own already-correct per-eye
  asymmetric projection and eye offset, derived from the actual physical headset's lens/display geometry
  rather than an estimated focal distance. The `TODO(real headset)` comments added to `proxy_d3d9.c` this
  session point at exactly this.
- `IVRCompositor::WaitGetPoses()` — per-frame head pose (needed for the head-tracking feature this
  project has explicitly deferred throughout, per every prior session's "Known limitations" section).
- `IVRCompositor::Submit(EVREye, const Texture_t*, ...)` — the actual per-eye frame handoff, via the
  D3D9Ex→D3D11 bridge in §2b (a plain D3D9 `Texture_t` is not an accepted `ETextureType`, per §2a).

### 2e. Disposition

This is scoping only, as instructed — no SDK installed, nothing implemented. The single most important
finding to carry forward: **the D3D9/OpenVR incompatibility is real and confirmed**, not a hypothetical
risk, and the bridging fix (D3D9Ex shared surfaces → a second D3D11 device → `IVRCompositor::Submit`) is
a nontrivial, separate piece of engineering from anything this project has built so far. The null-driver
path is a genuinely useful, currently-actionable way to validate that bridge's plumbing before the user
has physical headset access.

## 3. Cleanup and repo sync

No game files were touched this session (the user's own live game process, PID 9188, was never
attached-to, killed, or written to — the game directory's own `d3d9.dll` was never touched). All build/
verification work happened in `tools/proxy-d3d9/` (rebuilds the tools-folder copy of `d3d9.dll`, not the
game-directory copy) and the session scratchpad (temp directory, not part of any repo). No new
processes were left running by this session (only the DLL build and standalone Python verification
scripts were run — no game launches, no debugger attaches).

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note, updated `proxy_d3d9.c`/`d3d9.dll`.
- **modding-notes / dev-archive**: synced as usual (this note + updated source).
- **Mod repo** (`psychonauts-vr`): **not updated this session** — the off-axis upgrade is real,
  math-verified work, but per §1g it has not been live-tested against the actual game, so per this
  project's own established practice it is not pushed to the public repo yet. The next session that can
  live-test (after the user closes their current game session) should push then, if the in-game
  verification confirms the upgrade behaves as intended.
