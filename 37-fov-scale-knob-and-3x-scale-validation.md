# Session 37 — PSYVR_FOV_SCALE Knob (verified) + 3x Render Scale Validation

Date: 2026-08-18 (fourth session today; the working block was cut short by usage limits — code
was committed and pushed at the time, this note written on resume). Everything here is in
commit "PSYVR_FOV_SCALE knob ... 3x scale validated" and was live-verified before the cutoff.

## 0. Summary

1. **`PSYVR_FOV_SCALE` implemented and numerically verified.** The compositor maps the
   submitted eye textures onto the HEADSET's frustum (~80°+ vertical on a Quest 3) regardless
   of the FOV the game rendered — so the game's narrower ~52° fovy shows up magnified/zoomed
   in-headset. The knob multiplies the game's `rawFov` argument IN PLACE on the stack at
   `Hook_BuildProjectionMatrix` entry (three x87 instructions before anything reads it), so the
   observer cache, the game's own projection build, its culling via that matrix, and the stereo
   correction all consume the same scaled value with zero further plumbing. Verified at 1.3:
   `rawFov` 104→135.2, fovy 52.1°→67.6°, xScale 1.5377→1.1203 — every value matching the
   analytic prediction, and the live `SVSCF stereo-correct` line consuming the new xScale.
   Default 1.0 is an exact no-op (IEEE `x*1.0 == x`). Range clamp 0.5–2.5.
2. **The log now suggests the right value**: `VRBridge_QueryRealGeometry` computes the HMD's
   real vertical FOV from its projection tangents and prints
   `suggested PSYVR_FOV_SCALE=...` next to the game's fovy. On the gaming PC, read that line
   from `%TEMP%\psychonautsvr_proxy.log` and try the suggested value if the world feels zoomed.
   Deliberately NOT auto-applied: the game's CPU-side logic wasn't audited for independent FOV
   assumptions, and `BuildProjectionMatrix` may not re-fire after VR geometry arrives.
3. **`PSYVR_RENDER_SCALE=3` validated with the live bridge**: 1920×1440/eye created, submit OK,
   readback ~3.7ms/eye on the dev machine's GTX 1660 (vs ~1.4ms at 2x — linear in pixels).
   On the RTX 5080 at 800×600 base (2400×1800/eye) this should still hold 72Hz comfortably;
   watch the `ReadbackChain` log lines there to confirm.
4. **Combined regression at release defaults + fake pose**: init, HMD identity, head-tracking
   reference, UI depth, FOV default, submit — all healthy in one run.

## 1. What tonight's headset session should do with this

- If the world looks **zoomed-in / too large / "inside a scope"**: read the
  `suggested PSYVR_FOV_SCALE` log line and relaunch with that value set. Note that a wider
  rendered FOV spreads the same eye-buffer pixels over more view — consider pairing with
  `PSYVR_RENDER_SCALE=3`.
- The FOV knob ships in **v0.1.4-alpha** (release created this session, after the note); the
  v0.1.3 zip does NOT contain it.

## 2. Honest caveats

- FOV scaling is uniform (vertical and horizontal together, aspect unchanged at 4:3). The
  HMD's per-eye aspect is taller than wide — a vertical-FOV match overcovers horizontally.
  Real per-axis/asymmetric projection override is a bigger, separate undertaking.
- Possible edge pop-in if any game system derives visibility from the FOV value independently
  of the projection matrix (not audited).
