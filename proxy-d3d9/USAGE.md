# Proxy `d3d9.dll` — usage

**v0.1.1-alpha.** This is the mod's core component: a `d3d9.dll` that loads in place of the
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

- **Head tracking is untested on real hardware.** It's on by default with the VR bridge; if
  anything about the motion feels wrong (inverted axis, wrong scale, judder), set
  `PSYVR_DISABLE_TRACKING=1` to get v0.1.0 behavior back, and please note WHICH axis felt wrong.
- **Performance (corrected from v0.1.0's known-issues list)**: the previously-reported
  ~28-31fps ceiling was an artifact of SteamVR's headset-free null driver, not the mod — with a
  real HMD, the bridge sustains the headset's native refresh (72Hz measured) with large
  headroom at the current render resolution. See
  [modding-notes/33 §3](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/33-first-physical-headset-test-confirmed-and-shutdown-hang.md).
- **VR-bridge eye buffers render at the game's own resolution** (e.g. 800×600), far below the
  headset's recommended per-eye size (2496×2688 queried on a Quest 3) — expect a soft,
  upscaled image. Native-resolution VR rendering is a separate, not-yet-attempted undertaking.
- **Only one shader-constant register is corrected.** Skinned/animated geometry (bone matrices)
  doesn't yet get per-eye/tracking correction — head rotation may make this more visible than
  static stereo did.
- **Main pause/menu UI screen: left eye reported completely black** (title screen and gameplay
  are fine). Diagnosed as far as passive evidence allows, not yet fixed.
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

Real-headset verification of head tracking — does the view follow the head correctly on all
axes, does positional lean feel 1:1, and how does the readback-based 72Hz path feel under real
head motion. After that: rendering at the headset's native per-eye resolution.
