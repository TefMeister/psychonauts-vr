# Proxy `d3d9.dll` — usage

This is the mod's core component so far: a `d3d9.dll` that loads in place of the system one
(standard Windows DLL search order, the same mechanism dxwrapper and most D3D9 mods use), and now
implements a **first working side-by-side stereo render** — two independent renders of the title
screen, composited into the left/right halves of the window, with a per-eye camera offset that
demonstrably reaches the GPU. This is early, experimental, title-screen-only work, not a finished
VR mod — see "Known limitations" below before expecting more than that.

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

At `STEREO_HALF_IPD=0` (a zero-offset control), both eyes render the same content at the same
screen position (differing only in brightness) — proof the pipeline introduces no spurious effect.
At the shipped `STEREO_HALF_IPD=3.25` (a realistic half-IPD scaled to the title screen's camera
distance) and a diagnostic `60` (18×), the two eyes' background content visibly and reproducibly
diverges in a way that scales with the offset magnitude, while unrelated screen-space UI stays
fixed in both halves as expected. This is real, causal, reproducible evidence the per-eye
correction reaches the GPU. **It is not yet a single obviously-legible "the same object is at a
different X position in each eye" screenshot** — the specific geometry the identified register
drives on the title screen is a detailed background texture, not a discrete object, so the visible
effect reads as "the pattern's character changes with the correction" rather than "the shape moved
sideways." Read [modding-notes/14](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/14-shader-constant-stereo-hook.md)
before assuming more than this.

## Known limitations

- **Title screen only.** Real gameplay has not been reached yet (simulated input doesn't dismiss
  the title screen in this dev environment — see
  [modding-notes/08](https://github.com/TefMeister/psychonauts-vr-modding-notes/blob/main/08-live-camera-data-gameplay.md)).
  Nothing about gameplay rendering/camera behavior has been validated.
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
- No head tracking, no VR runtime (SteamVR/OpenXR) integration, no asymmetric per-eye frustums yet
  (this uses a rigid parallel-axis offset, not proper toe-in/converged stereo).

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

Trace the untraced transform-composition path (`exe+0x433E50`/`0x42E2A0` in the current build) to
understand why the corrected register's matrix doesn't decompose against the tracked camera state,
extend the correction to the other per-draw matrix registers (skinning/bone matrices) so animated
geometry also gets correct per-eye parallax, and work toward reaching real gameplay (past the
title screen) to validate all of this against content with clear discrete objects rather than a
detailed background texture.
