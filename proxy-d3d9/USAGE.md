# Proxy `d3d9.dll` — usage

**v0.1.7-alpha** *(the zoomed-in picture is fixed at the root: the mod now submits each eye
with tangent-matched texture bounds, so the compositor's angular mapping is exactly 1:1 on any
headset with no per-headset tuning. `PSYVR_FOV_SCALE` changes meaning — see its entry below —
and the v0.1.6 "HUD invisible at FOV scale >1.2" issue is gone with the mechanism that caused
it. Also restores the suggested-FOV log line that v0.1.4's notes promised.)*
This is the mod's core component: a `d3d9.dll` that loads in place of the
system one (standard Windows DLL search order, the same mechanism dxwrapper and most D3D9 mods
use). It does three things now:

1. **Side-by-side stereo render of real gameplay** — confirmed working by direct play-testing,
   on by default, no setup needed.
2. **A bridge that submits real frames to a VR compositor (SteamVR/OpenVR)** — now confirmed
   working with a real physical headset (Quest 3 via Virtual Desktop), off by default and
   requiring extra setup (see below).
3. **(New in v0.1.1) Experimental 6DOF head tracking** — the HMD's real per-frame pose
   (rotation AND position) drives the rendered view. On by default whenever the VR bridge is
   active; verified via monitor testing with a synthesized moving pose, **not yet tested with a
   real headset** — see Known issues.

This is still early, experimental software — see "Known limitations" before expecting more than
what's described here.

## What it does now

1. **Loads and forwards** `Direct3DCreate9` to the real system `d3d9.dll` unmodified.
2. **Vtable-hooks** `IDirect3D9::CreateDevice`, `IDirect3DDevice9::Present`, `::Reset`, and
   `IDirect3DDevice9::SetVertexShaderConstantF` (slots 16, 17, 94 — all independently verified).
3. **Inline (byte-patch/trampoline) hooks** directly into `Psychonauts.exe`'s own code on
   `BuildViewMatrix`, `BuildProjectionMatrix`, and `CandB` (the game's own per-frame
   render-dispatch function, confirmed safe to invoke twice per frame). `CandB` runs twice per
   real frame, once per eye, into two separate offscreen render targets.
4. **Injects a per-eye camera offset** via a hook on `SetVertexShaderConstantF` that patches the
   per-draw matrix upload register with a closed-form correction using **proper off-axis
   (asymmetric-frustum) projection**. Real per-eye IPD, eye transforms, and frustum data are
   pulled from OpenVR itself when available, with a safe fallback to calibrated constants. The
   OpenVR-sourced path passed its first real-hardware inspection (user-confirmed correct and
   comfortable in-headset). See
   [modding-notes/24](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/24-off-axis-projection-upgrade-and-vr-runtime-scoping.md),
   [32](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/32-real-openvr-ipd-projection-data-and-menu-bug-revisit.md),
   [33](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/33-first-physical-headset-test-confirmed-and-shutdown-hang.md).
5. **Per-eye private depth-stencil surfaces** (fixed a shared depth-stencil bug that corrupted
   both eyes — see
   [modding-notes/22](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/22-eye-parity-refutes-culling-shared-depth-stencil-fix.md)).
6. **Composites** both eyes into the left/right halves of the real backbuffer before the one
   real hardware Present. This monitor-view path is always on.
7. **(Opt-in) Submits real frames to a VR compositor** via a D3D9Ex/D3D11 bridge into
   `IVRCompositor::Submit` — confirmed working end-to-end with a real headset at the HMD's
   native refresh rate. See
   [modding-notes/33](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/33-first-physical-headset-test-confirmed-and-shutdown-hang.md).
8. **(New) Head tracking**: the HMD pose returned by `WaitGetPoses` every frame is converted
   into a per-frame view correction applied at the same proven per-draw patch point as the
   stereo correction — full rotation plus positional lean, with the tracking reference
   (position + yaw) captured at startup so the game's horizon stays level. Math validated
   numerically before build; live-verified on-monitor via a synthesized moving pose. See
   [modding-notes/34](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/34-shutdown-zombie-fix-quit-events-and-head-tracking.md).
9. **(New) Clean lifecycle**: SteamVR quitting mid-game is now handled (the game drops back to
   flat monitor rendering and keeps running instead of being killed), and the v0.1.0 bug where
   the game could hang on exit as an unkillable process with the VR bridge active is fixed.

## Confirmed evidence (be precise about what this is)

**Real gameplay stereo, user-confirmed (2026-08-17)**: real, player-controlled gameplay renders
correctly in stereo on both eyes
([modding-notes/23](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/23-gameplay-stereo-working-milestone.md)).

**First physical-headset test, user-confirmed (2026-08-17)**: with `PSYVR_ENABLE_SUBMIT=1` and
SteamVR running, the game displayed inside a real Quest 3 (via Virtual Desktop), sustaining the
HMD's native 72Hz for the whole run; the user later confirmed the stereo looked correct and
comfortable. Head tracking did not exist yet in that build. Full write-up:
[modding-notes/33](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/33-first-physical-headset-test-confirmed-and-shutdown-hang.md).

**Head tracking (2026-08-18)**: implemented and verified on-monitor via a synthesized moving
pose (smooth, solid camera motion; static-pose regression clean). **Not yet verified with a
real headset** — that is exactly what this release exists to test.
[modding-notes/34](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/34-shutdown-zombie-fix-quit-events-and-head-tracking.md).

## Known issues / limitations

- **Fixed in v0.1.7: HUD invisible at `PSYVR_FOV_SCALE` above ~1.2.** This was collateral
  damage from the old full-texture submit stretching the frame onto the lens frustum; with
  tangent-matched submit bounds the HUD sits at its natural lens-relative angles at any FOV
  scale. At very high scales the outermost HUD corners can still clip a few degrees at the
  lens edge (dev-archive notes/43 has the numbers).
- **HUD elements render at their original angular size** — a per-element rescale exists as an
  experimental opt-in (`PSYVR_UI_SCALE`, see below) but is off by default because the game
  draws fullscreen fades/backdrops through the same shaders (dev-archive notes/43).
- **Head tracking: now confirmed working on real hardware** (Quest 3 playtests 2026-08-18
  and 2026-08-19, dev-archive notes/40 and 42) — motion tracks correctly and comfortably.
  Two real limitations remain: the engine's frustum culling doesn't know about head rotation,
  so looking far off the game camera's axis (e.g. over your shoulder) shows unrendered black
  void until the game camera turns (fix queued, notes/40); and distant LOD billboard sprites
  (trees/bushes) look doubled/cross-eyed in stereo (fix queued, notes/40).
  `PSYVR_DISABLE_TRACKING=1` still restores the fixed-view behavior if wanted.
- **Performance (corrected from v0.1.0's known-issues list)**: the previously-reported
  ~28-31fps ceiling was an artifact of SteamVR's headset-free null driver, not the mod — with a
  real HMD, the bridge sustains the headset's native refresh (72Hz measured) with large
  headroom at the current render resolution. See
  [modding-notes/33 §3](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/33-first-physical-headset-test-confirmed-and-shutdown-hang.md).
- **VR-bridge eye buffers now render at 2× the game's resolution by default** (e.g. 1600×1200
  from an 800×600 game setting; `PSYVR_RENDER_SCALE=1`..`4` to override) — still below the
  headset's recommended per-eye size (2496×2688 on a Quest 3), but a large sharpness step over
  v0.1.1's native-resolution upscale. Higher scales cost VRAM/readback time; 3 may be viable.
- ~~Skinned geometry uncorrected~~ **Refuted in v0.1.3 by a full audit of all 455 of the
  game's vertex shaders**: every 3D shader — skinned included — transforms through the corrected
  matrix register, so characters have been stereo- and tracking-correct all along.
- **UI depth (new, experimental)**: the 10 screen-space UI shaders bypass the stereo correction,
  which would pin HUD/menus at infinity in a headset — v0.1.3 places them at a virtual ~2m by
  default. `PSYVR_UI_DEPTH=<world units>` tunes it (100 = 1m, 300 = 3m), `0` disables.
  Verified on-monitor by pixel measurement; untested on real hardware.
- **Fixed in v0.1.2**: the long-standing "left eye completely black" bug on the brain
  title/menu screen (root cause: the engine re-binds the real backbuffer mid-eye-pass on that
  screen; fixed by redirecting screen binds/reads to the active eye during eye passes).
- **Fixed in this release**: v0.1.0's exit hang (unkillable zombie process when closing the
  game with the VR bridge active) and the unhandled SteamVR-quit sequence.

## How to use it

### Basic stereo (monitor only, always on, no extra setup)

1. Back up any existing `d3d9.dll` in your Psychonauts install folder (there normally isn't
   one — the game uses the system copy).
2. Copy `d3d9.dll` from this folder next to `Psychonauts.exe`.
3. Launch the game normally. You'll see the screen split into left/right halves.
4. Check `%TEMP%\psychonautsvr_proxy.log` for a line-by-line log of every hook firing.
5. Remove the copied `d3d9.dll` when done — the game falls back to the system DLL.

### VR-compositor submission + head tracking (experimental, opt-in, needs SteamVR)

1. Do the above, and also copy `openvr_api.dll` from this folder into the game directory.
2. Have SteamVR installed and running (with your headset connected — e.g. Quest via
   Link/Air Link/Virtual Desktop).
3. Set `PSYVR_ENABLE_SUBMIT=1` before launching the game (e.g. a `.bat` with
   `set PSYVR_ENABLE_SUBMIT=1` then `start Psychonauts.exe`, or
   `$env:PSYVR_ENABLE_SUBMIT="1"` in PowerShell).
4. Head tracking is on automatically. Optional env vars:
   - `PSYVR_DISABLE_TRACKING=1` — fixed view (v0.1.0 behavior).
   - `PSYVR_FAKE_POSE=1` — synthesized swaying pose for monitor-only testing without moving a
     headset.
   - `PSYVR_RENDER_SCALE=1..4` — eye render-resolution multiplier (default 2 with the VR path).
   - `PSYVR_UI_DEPTH=<world units>` — virtual depth for HUD/menu UI (default 200 ≈ 2m; 0 = off).
   - `PSYVR_FOV_SCALE=0.5..2.5` — how much field of view the game renders (default 1.0 =
     the game's native 52° vertical). Since v0.1.7 this no longer affects the zoom (the
     submit-bounds crop keeps the angular mapping 1:1 regardless); it controls how much frame
     exists *around* the headset's visible window. Below the full-coverage value the compositor
     mildly stretches the uncovered axis and off-axis head turns hit unrendered black sooner;
     at or above it the image is geometrically exact everywhere. The log prints both a
     `suggested PSYVR_FOV_SCALE` line (restored in v0.1.7 — it was missing from v0.1.4-0.1.6)
     and the exact `full lens coverage needs PSYVR_FOV_SCALE>=…` value for your headset
     (≈1.8 on a Quest 3; that's the Quest 3 launcher's default). A wider FOV spreads pixels
     thinner — consider pairing with `PSYVR_RENDER_SCALE=3`.
   - `PSYVR_SUBMIT_BOUNDS=0` — disables the v0.1.7 tangent-matched submit crop and restores
     the old full-texture submit (which zooms; debug/comparison only).
   - `PSYVR_UI_SCALE=0.25..1.0` — EXPERIMENTAL: shrinks all screen-space UI draws about the
     screen center via a per-draw viewport. Known to also shrink fullscreen fades and
     pause/menu backdrops (they share the UI shader signature), which looks broken — off by
     default, only useful for experiments until a per-draw classifier lands.
   - **F11** in-game re-centers head tracking (re-captures the reference position/yaw).
5. The log's `VRBridge_Init: HMD identity:` line records which headset OpenVR reported, and
   `HeadTrack:` lines show the live tracking state.

## Building from source

Requires a 32-bit-capable MinGW/clang toolchain (this project uses LLVM-MinGW,
`i686-w64-mingw32-clang`, since Psychonauts.exe is a 32-bit executable). Run `build.ps1` in this
folder. The VR-bridge code additionally needs the OpenVR SDK headers/lib, vendored in this
project's dev-archive repo under `tools/vr-bridge/openvr-sdk/`. The head-tracking matrix math
has a standalone numerical validation script (`validate_headtrack.py`, numpy) in the
dev-archive's `tools/proxy-d3d9/`.

## Next milestone

Quest 3 flight of the v0.1.7 submit-bounds path (expect: zoom exactly gone, HUD visible at the
launcher's FOV 1.8). Then the two known head-tracking limitations: feeding head yaw back into
the game camera so frustum culling stops leaving an over-the-shoulder void, and center-eye
LOD/billboard decisions to stop distant sprite flicker.
