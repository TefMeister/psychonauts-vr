# Proxy `d3d9.dll` — usage

**v0.1.0-alpha.** This is the mod's core component: a `d3d9.dll` that loads in place of the
system one (standard Windows DLL search order, the same mechanism dxwrapper and most D3D9 mods
use). It does two things now:

1. **Side-by-side stereo render of real gameplay** — confirmed working by direct play-testing,
   on by default, no setup needed.
2. **An experimental bridge that submits real frames to a VR compositor (SteamVR/OpenVR)** —
   proven working end-to-end, but off by default and requiring extra setup (see below), since it
   has not yet been tested with a physical headset.

This is still early, experimental software — see "Known limitations" before expecting more than
what's described here.

## What it does now

1. **Loads and forwards** `Direct3DCreate9` to the real system `d3d9.dll` unmodified.
2. **Vtable-hooks** `IDirect3D9::CreateDevice`, `IDirect3DDevice9::Present`, and
   `IDirect3DDevice9::SetVertexShaderConstantF` (slots 16, 17, 94 — all independently verified).
3. **Inline (byte-patch/trampoline) hooks** directly into `Psychonauts.exe`'s own code on
   `BuildViewMatrix`, `BuildProjectionMatrix`, and `CandB` (the game's own per-frame
   render-dispatch function, confirmed safe to invoke twice per frame). `CandB` runs twice per
   real frame, once per eye, into two separate offscreen render targets.
4. **Injects a per-eye camera offset** via a hook on `SetVertexShaderConstantF` that patches the
   per-draw matrix upload register (`StartRegister=6`, `Vector4fCount=4`) with a closed-form
   correction. As of this release the correction uses **proper off-axis (asymmetric-frustum)
   projection** — parallel camera axes with a sheared frustum per eye, matching how real VR SDKs
   render, rather than a simpler "toe-in"/converged approach. See
   [modding-notes/24](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/24-off-axis-projection-upgrade-and-vr-runtime-scoping.md).
5. **Fixed a shared depth-stencil bug** that caused a frozen-looking left eye and a dark/corrupted
   right eye in real gameplay — both eyes now get their own private depth-stencil surface. See
   [modding-notes/22](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/22-eye-parity-refutes-culling-shared-depth-stencil-fix.md).
6. **Composites** both eyes' offscreen surfaces into the left/right halves of the real backbuffer
   via `StretchRect`, before the one real hardware `Present`. This monitor-view path is always on.
7. **(New, opt-in) Submits real frames to a VR compositor.** When enabled, each eye's render
   target is bridged (via a D3D9Ex-owned shared surface, or a CPU round-trip fallback when the
   game's own device can't directly share) into a separate D3D11 device and submitted to
   `IVRCompositor::Submit` — proven working live, with `Submit` returning success every frame for
   both eyes, cross-confirmed via SteamVR's own compositor logs independently. Real per-eye IPD,
   eye transforms, and frustum data are pulled from OpenVR itself (`GetFloatTrackedDeviceProperty`,
   `GetEyeToHeadTransform`, `GetProjectionRaw`) rather than hardcoded, with a safe fallback to the
   old constants if OpenVR isn't available. See
   [modding-notes/27](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/27-null-driver-openvr-init-and-compositor-submit.md),
   [28](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/28-nonblocking-sync-and-vr-submit-integration.md),
   [29](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/29-submit-never-fired-frame-counter-deadlock-fix.md),
   [30](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/30-submit-live-confirmed-and-performance-cost-measured.md),
   [32](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/32-real-openvr-ipd-projection-data-and-menu-bug-revisit.md).

## Confirmed evidence (be precise about what this is)

**Real gameplay stereo, user-confirmed (2026-08-17):** after the depth-stencil fix, the person
playing the game directly confirmed real, player-controlled gameplay renders correctly in stereo
on both eyes ("the game is running absolutely fine on both sides"). Full write-up:
[modding-notes/23](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/23-gameplay-stereo-working-milestone.md).

**VR compositor submission, live-confirmed (2026-08-17):** with the VR-bridge path enabled in the
actual running game (not just a standalone test), `Submit` returned success continuously for both
eyes across 1600+ frames, and SteamVR's own compositor-side log independently showed it creating
the internal textures it only creates upon genuinely receiving a submitted frame — timestamps
matching within 1ms of this project's own log. Tested entirely via SteamVR's null/desktop driver —
**no physical VR headset has been used in any test so far.** Full write-up:
[modding-notes/30](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/30-submit-live-confirmed-and-performance-cost-measured.md).

## Known issues / limitations

- **No physical VR headset has been used in any test.** Everything above was validated on a
  monitor (stereo composite) and via SteamVR's headset-free null driver (compositor submission).
  Real in-headset comfort, tracking, and correctness are unverified.
- **Performance with the VR-bridge path enabled is currently low** (~28-31fps measured on the dev
  machine, vs. ~60fps with it off) — dominated by `IVRCompositor::WaitGetPoses`, which is required
  (not a bug — removing it breaks `Submit`) but may behave very differently with a real headset
  actually connected, since that call is normally paced by the HMD's real display timing. This is
  unresolved and needs real hardware to evaluate further. See
  [modding-notes/31](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/31-waitgetposes-is-the-real-cost-present-double-pacing-fixed.md).
- **Main pause/menu UI screen: left eye reported completely black.** Distinct from both the title
  screen and real gameplay, which both work correctly. Diagnosed as far as passive evidence
  allows but not yet fixed or reproduced under a debugger.
- **No head tracking.** The per-eye offset is currently a fixed IPD-based translation (now sourced
  from OpenVR's real IPD property when available), not yet driven by live head pose from
  `WaitGetPoses`' returned tracking data.
- **Only one shader-constant register is corrected.** Skinned/animated geometry (a different
  register carrying bone matrices) does not yet get a per-eye correction.
- **VR-bridge eye buffers render at the game's native resolution (640×480)**, not OpenVR's
  recommended per-eye render target size (queried as 1656×1840 on the dev machine) — rendering at
  a VR-appropriate resolution is a separate, not-yet-attempted undertaking.

## How to use it

### Basic stereo (monitor only, always on, no extra setup)

1. Back up your existing `d3d9.dll` if one is already present in your Psychonauts install folder
   (there normally isn't one — the game uses the system copy).
2. Copy `d3d9.dll` from this folder into the Psychonauts install directory (next to
   `Psychonauts.exe`).
3. Launch the game normally. You'll see the screen split into left/right halves.
4. Check `%TEMP%\psychonautsvr_proxy.log` for a line-by-line log of every hook firing.
5. Remove the copied `d3d9.dll` when done — the game will fall back to the system DLL.

### VR-compositor submission (experimental, opt-in, needs SteamVR)

1. Do the above, and also copy `openvr_api.dll` from this folder into the game directory.
2. Have SteamVR installed (free, via Steam).
3. Set the environment variable `PSYVR_ENABLE_SUBMIT=1` before launching the game (e.g.
   `$env:PSYVR_ENABLE_SUBMIT="1"` in PowerShell, then launch `Psychonauts.exe` from that same
   shell).
4. This has only been tested via SteamVR's null/desktop driver so far — trying it with a real
   headset connected is genuinely untested territory. Performance is currently low (see above);
   don't expect a comfortable experience yet.

## Building from source

Requires a 32-bit-capable MinGW/clang toolchain (this project uses LLVM-MinGW,
`i686-w64-mingw32-clang`, since Psychonauts.exe is a 32-bit executable). Run `build.ps1` in this
folder. The VR-bridge code additionally needs the OpenVR SDK headers/lib, vendored in this
project's dev-archive repo under `tools/vr-bridge/openvr-sdk/`.

## Next milestone

The real next frontier is validation with an actual physical VR headset — performance,
comfort, tracking, and whether the compositor-submitted output is actually correct when a real
display is on the other end. Everything confirmed so far has been monitor-only or headset-free
null-driver testing.
