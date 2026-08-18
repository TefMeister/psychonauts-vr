# Session 36 — Full Shader Audit: "Skinned Uncorrected" REFUTED; UI-Depth Feature

Date: 2026-08-18 (third session today, continuing the pre-headset-test window). Goal was the
skinned-geometry correction gap; the audit that was supposed to scope it instead **refuted it**,
and surfaced (plus fixed) the real remaining gap: UI at infinity.

## 0. Summary

1. **Every one of the game's 455 vertex shaders was dumped and audited** (they are ALL created
   at startup — gameplay adds none). Exactly two position paths exist:
   - **445 shaders** (42 rigid + 403 skinned permutations) end with `m4x4 …, c6` — the register
     the stereo correction has patched since notes/14 and head tracking since notes/34. Bone
     palettes (c64–c91, no relative addressing — per-bone-count shader permutations) blend
     BEFORE that final transform, and the correction math is proven for any affine prefix.
     **Skinned geometry has been fully stereo- and tracking-corrected all along.** The
     "skinned/animated geometry does not get a per-eye correction" known-limitation (carried in
     USAGE.md and notes since ~notes/14) was an assumption, never a measurement — remove it.
   - **10 shaders** are pure screen-space UI: `oPos = input + c50`, never reading c6..c9. They
     bypass the correction entirely ⇒ the HUD/menus render with ZERO parallax ⇒ in a headset
     the UI sits at infinity behind everything — the classic VR HUD comfort problem.
2. **UI-depth feature implemented and verified** (default ON at 200 world units ≈ 2m;
   `PSYVR_UI_DEPTH=<world units>` to tune, `0` to disable): UI shaders are identified at
   `CreateVertexShader` by bytecode signature (no c6..c9 reads), binds tracked via a
   `SetVertexShader` hook, and their per-draw `c50.x` screen offset is shifted per eye by
   `-d·xScale/depth` — the exact shift a 3D point at that depth would receive.
3. **Verification was quantitative, not eyeballed**: cross-correlating the eye-surface BMP dumps,
   the text band shifts 16px between eyes (15.5px predicted); the 3D bands keep their
   pre-existing scene disparity (−11..−16px, opposite sign — no leakage); a control run with
   `PSYVR_UI_DEPTH=0` restores the text band to exactly 0px with 3D bands unchanged.

## 1. Recon tooling added (all reusable)

- `PSYVR_REG_HISTO=1`: per-register SetVertexShaderConstantF histogram during eye phases
  (~5s windows) + per-draw register-combination counts (which registers arrive together per
  draw) + one-time bytecode dump of every created vertex shader to `%TEMP%\psyvr_vs_NN.bin`.
- `tools/proxy-d3d9/vs_analyze.py`: D3D9 vertex-shader bytecode analyzer (token walker —
  version/comment/def/dcl aware, vs_2_0 operand-count fields, const-register extraction,
  relative-addressing detection, oPos chain reporting).
- Register map established (gameplay + title): c6=WVP (corrected), c10/c13=4x3 matrices
  (world/texgen — feed lighting, not position), c16=another 4x4 (only 19 shaders read it, none
  for position), c50=screen-space offset (UI position / shared constant), c64–c191=bone
  palettes, c94/c96=misc material params.

## 2. Key correction: what the combo data + audit mean together

Per-draw combos showed skinned draws sometimes arrive without a fresh c6 upload — irrelevant:
whenever c6 IS uploaded during an eye phase it passes through the correction, and the shader
consumes the corrected value regardless of upload timing. Since no shader computes position any
other way (445/445 of the 3D ones use c6), there is no uncorrected 3D geometry in this game.
Full stop. What head rotation will expose tonight is therefore NOT skinned-vs-rigid mismatch
(it cannot happen) — watch instead for the UI-depth feel and any FOV/scale issues.

## 3. UI-depth mechanics (for the next reader)

- Identification: `VSBytecodeIsUIShader()` — vs_2_0 token walk, TRUE iff no source operand reads
  c6..c9 (def/defi/defb/dcl skipped). All 10 UI shaders match; all 445 3D shaders don't.
- Application: c50 is shared between UI and 3D shaders (3D adds it post-transform as a
  half-pixel-style offset), so the shift must exist ONLY while a UI shader is bound during an
  eye phase. `UIShift_Reconcile()` adjusts the device's live c50.x on every SetVertexShader and
  phase transition (BeforeEye1/BeforeEye2/AfterBoth), tracking the applied delta; direct c50
  uploads while active are patched in-line in the SVSCF hook instead.
- 200 world units = 2m (WORLD_UNITS_PER_METER=100), the standard comfortable HUD distance.
  In-headset tuning knob for tonight: `PSYVR_UI_DEPTH=150` (closer) / `300` (farther) / `0` (off).

## 4. State / next

- NOT yet in any release: this + notes/35's work would be v0.1.3-alpha (pending go-ahead) or
  fold into tonight's testing directly from the dev build.
- USAGE.md still carries the false skinned-geometry limitation — fix with the next release push.
- Remaining known gaps after this session: UI depth untested on real hardware (like head
  tracking); FOV/scale question (game FOV vs headset FOV) deliberately deferred until tonight's
  in-headset feedback; eye buffers still below recommended resolution (scale 3 viable to try).
