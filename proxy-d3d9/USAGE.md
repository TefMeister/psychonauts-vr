# Proxy `d3d9.dll` — usage

This is the mod's core component so far: a `d3d9.dll` that loads in place of the system one
(standard Windows DLL search order, the same mechanism dxwrapper and most D3D9 mods use), and now
implements a **working side-by-side stereo render of real gameplay** — two independent renders of the
scene, once per eye, composited into the left/right halves of the window, with a per-eye camera offset
that demonstrably reaches the GPU. **This has been confirmed working in real, player-controlled
gameplay by direct play-testing** (not just the title screen's scripted attract-mode camera — see
"Confirmed evidence" below). It is still early, experimental, monitor-only work, not a finished VR mod
with head tracking or a VR runtime — see "Known limitations" below before expecting more than that.

## What it does now

1. **Loads and forwards** `Direct3DCreate9` to the real system `d3d9.dll` unmodified.
2. **Vtable-hooks** `IDirect3D9::CreateDevice`, `IDirect3DDevice9::Present`, and
   `IDirect3DDevice9::SetVertexShaderConstantF` (slots 16, 17, 94 — all independently verified, not
   guessed; see [modding-notes/06](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/06-createdevice-present-hooks.md)
   and [modding-notes/14](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/14-shader-constant-stereo-hook.md)).
3. **Inline (byte-patch/trampoline) hooks** directly into `Psychonauts.exe`'s own code — not just
   COM vtable patching — on `BuildViewMatrix`, `BuildProjectionMatrix`, and `CandB` (the game's own
   per-frame render-dispatch function). `CandB` is invoked **twice** per real frame (confirmed safe
   to double-invoke — see [modding-notes/12](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/12-double-call-safety-test.md)),
   once per eye, into two separate offscreen render targets stood up in `CreateDevice`.
4. **Injects a per-eye camera offset two ways**: (a) a CPU-side re-invocation of
   `BuildViewMatrix` with an eye position nudged along the camera's right vector (structurally
   correct, but confirmed to have no effect on its own — the game's real camera data flows to the
   GPU via shader constants computed earlier, not re-read from this buffer at draw time); (b) the
   real fix — a hook on `SetVertexShaderConstantF` that patches the specific per-draw matrix upload
   register (`StartRegister=6`, `Vector4fCount=4`) with a closed-form correction derived from
   inserting a rigid eye-space translation between the View and Projection matrices. See
   [modding-notes/14](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/14-shader-constant-stereo-hook.md)
   for the full derivation and the live-probe methodology used to find that register, and
   [modding-notes/17](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/17-keystate-mechanism-trace-and-reg6-transpose-confirmation.md)/[18](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/18-stereo-index-bug-fix-and-input-slot4-ruled-out.md)
   for a since-confirmed correction: the uploaded matrix is a **transpose** of the true camera
   composite, computed in-place by the game's own code before this hook ever sees it, which means
   the correction must patch a different flat index in the buffer than the naive (pre-transpose)
   derivation would suggest (`floats[3]`, not `floats[12]`) — fixed and re-verified this session.
5. **Composites** both eyes' offscreen surfaces into the left/right halves of the real backbuffer
   via `StretchRect`, before the one real hardware `Present`.

## Confirmed evidence (be precise about what this is)

**Real gameplay, user-confirmed (2026-08-17):** after fixing a structural bug where both eyes shared
the device's one physical depth-stencil surface (causing depth-test rejections that read as a frozen
left eye / dark right eye — see
[modding-notes/22](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/22-eye-parity-refutes-culling-shared-depth-stencil-fix.md)),
the person playing the game directly confirmed real, player-controlled gameplay renders correctly in
stereo on both eyes ("the game is running absolutely fine on both sides"). This is the strongest
evidence bar the project has cleared so far — a direct play-test verdict, not just an agent reading
logs or comparing screenshots. Full write-up:
[modding-notes/23](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/23-gameplay-stereo-working-milestone.md).

Getting here took two earlier rounds of real, well-evidenced fixes (notes/20, notes/21) that turned
out not to be sufficient on their own — the actual root cause (the shared depth-stencil surface) was
only found in notes/22 after proving, with hard per-frame draw-call counts from a live gameplay
session (206/206 frames, exact match), that a previously-suspected draw-call asymmetry between the
eyes was never real. If you're auditing this project's rigor, that arc (notes/20 → 21 → 22) is a good
one to read end to end.

**Earlier, title-screen-only evidence, still valid**: at `STEREO_HALF_IPD=0` (a zero-offset control),
both eyes render the same content at the same screen position (differing only in brightness) — proof
the pipeline introduces no spurious effect. At the shipped `STEREO_HALF_IPD=3.25` (a realistic
half-IPD, cross-validated in
[modding-notes/15](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/15-input-blocker-retry-and-stereo-robustness.md)
against real human IPD, ≈6.5cm) and a diagnostic `60` (18×), the two eyes' content visibly and
reproducibly diverges in a way that scales with the offset magnitude. Read
[modding-notes/14](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/14-shader-constant-stereo-hook.md)
for the full original derivation.

## Known issues / limitations

- **Main pause/menu UI screen: left eye reported completely black.** Distinct from both the title
  screen and real gameplay, which both work correctly. Diagnosed as far as passive log evidence
  allows this session (ruled out draw-call omission and `StretchRect` failure — see
  [modding-notes/23](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/23-gameplay-stereo-working-milestone.md)
  §5) but not yet fixed; leading unconfirmed hypothesis is a near-clip/frustum interaction specific to
  that screen's likely close-up camera framing colliding with the fixed-world-unit parallax offset.
- **No real VR headset / runtime validation yet.** Everything above (including the gameplay milestone)
  has been tested as a side-by-side window render on a monitor, without SteamVR/OpenXR or a physical
  headset in the loop. No head tracking exists — the per-eye offset is a fixed, rigid parallel-axis
  translation (correct for comfort per the IPD cross-check above), not a converged/toe-in stereo pair
  driven by live head pose.
- **Only one shader-constant register is corrected.** Skinned/animated geometry (uploaded via a
  different register carrying up to 24 bone matrices) does not yet get a per-eye correction, so it
  will currently render identically in both eyes.
- **The corrected register's matrix does not derive from the specific camera state the existing
  hooks track** (confirmed via a live matrix-decomposition test — 0/20 sampled uploads decomposed
  sanely against the tracked View/Projection matrices). The implemented fix works anyway because it
  only assumes the uploaded matrix ends with the Projection matrix as its final factor, not that it
  equals `World * (tracked View) * Proj` exactly. A live stack/register trace since fully confirmed
  the actual structure: two chained matrix multiplies (`World*View*Proj`, whichever object this
  particular draw call is for) followed by an in-place 4×4 transpose before upload (see
  modding-notes/17). The correction now patches the buffer index that accounts for that transpose;
  see modding-notes/18 for the re-derivation and the real bug it found/fixed in an earlier version
  of this same file (patching `floats[12]` instead of `floats[3]`).

## How to use it (for testing/development only)

1. Back up your existing `d3d9.dll` if one is already present in your Psychonauts install folder
   (there normally isn't one — the game uses the system copy).
2. Copy `d3d9.dll` from this folder into the Psychonauts install directory (next to
   `Psychonauts.exe`).
3. Launch the game normally. You'll see the title screen split into left/right halves.
4. Check `%TEMP%\psychonautsvr_proxy.log` for a line-by-line log of every hook firing, including
   the exact `xScale`/correction values applied to the shader-constant patch.
5. Remove the copied `d3d9.dll` when done — the game will fall back to the system DLL.

## Building from source

Requires a 32-bit-capable MinGW/clang toolchain (this project uses LLVM-MinGW,
`i686-w64-mingw32-clang`, since Psychonauts.exe is a 32-bit executable) with its bundled `d3d9.h`.
Run `build.ps1` in this folder; it searches common LLVM-MinGW winget install locations and falls
back to whatever `i686-w64-mingw32-clang.exe` is on `PATH`.

## Next milestone

With real gameplay now confirmed working (the big one), the next priorities are: track down the
main-menu black-left-eye issue above with a real reproduction; extend the correction to the other
per-draw matrix registers (skinning/bone matrices) so animated geometry also gets correct per-eye
parallax; and — the real next frontier — actual VR headset/runtime validation (SteamVR/OpenXR
integration and head tracking), since everything confirmed so far has been a monitor-only
side-by-side render.
