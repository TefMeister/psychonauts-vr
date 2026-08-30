# 49 — Render-level head-lock: world-position extraction working; anchor needs the head bone

**Date:** 2026-08-19, dev machine, many live gameplay iterations with the user tuning on the
monitor. Goal (from notes/47/48): lock the FP eye to Raz's real body instead of the springy chase
camera. **Result: the render-level world-position extraction WORKS and is validated live — the eye
locks to Raz, tracks him, and no longer clips through the floor. But the anchor (body centroid) is
not yet playable: it sits at his center rather than his eyes, and the varying draw set leaves
residual jitter. The proper fix is anchoring to Raz's HEAD BONE specifically — the same bone-map
work hand IK needs. Strong prototype; not shippable yet.**

## What works (validated live)

- **World-matrix recovery from the render stream.** Every skinned draw's `World = WVP * (P^-1 *
  V^-1)` is recovered from data already in our hook: WVP = the register-6 upload (transposed), V
  from the BVM eye/at/up cache, P from the BPM cache. `g_pinvVinv = P^-1 * V^-1` is rebuilt once per
  frame; each skinned draw's world origin = `WVP_row3 * g_pinvVinv`. Confirmed: when standing, the
  recovered Raz origin is rock-stable (probe `candDist` 0.3-2.6 wu).
- **Raz identification.** Two filters make it robust: (1) reject origins near the camera eye (those
  are screen-space / camera-attached draws, or skinned draws reusing a stale non-Raz register-6 —
  they recover to ~eye); (2) keep only origins near the lock. This correctly picks Raz out of the
  scene.
- **Floor-clip fixed.** Lift the eye along TRUE world up (+Y in this engine) not the camera up
  (which came back as (0,0.888,-0.46) — already pitched, so the lift collapsed and the eye sank
  through the floor as Raz walked). With +Y lift the eye holds its height.

## Bugs found and fixed along the way (all live, user-driven)

1. **Latched onto the wrong draw** → recovered origin ≈ camera eye. Cause: screen-space / stale-r6
   pairings. Fix: the eye-distance reject filter.
2. **Anchor jitter** (~100-200 wu/frame) → "nearest to look-at" switched between body parts and
   nearby entities each frame. Fix attempt 1: temporal coherence (track nearest to last frame's
   Raz).
3. **Frozen lock / eye flew 60k units off** → the temporal lock latched at the MENU, then rejected
   every gameplay candidate (60k away > the 300-wu accept gate), freezing at a menu-space position
   while the eye translated wildly (buried "blurry green" view). Fix: a miss-frame counter that
   drops the lock after ~8 candidate-less frames so it re-seeds from the look-at point on scene
   changes.
4. **Sank through the floor when walking** → height lifted along camera-up (pitched). Fix: +Y world
   up (above).
5. **Foot-drift** → single-draw tracking ratcheted down onto Raz's planted feet as he walked. Fix:
   switched the anchor from nearest-single-draw to the CENTROID of Raz's body-part draws.

## Current state (user verdict: "more locked on him, getting better, but not playable")

- Locked to Raz's body centroid, tracks him, height tunable live and no longer clips the floor.
- **Remaining, all pointing at the same root cause — a body-centroid is the wrong anchor:**
  - Eye sits at Raz's CENTER, not his eyes/face (no forward-at-the-head placement).
  - Residual jitter: the set of Raz draws passing the filters varies frame to frame, so the
    centroid wobbles even with smoothing.
  - Occasional 3rd-person flashes: brief occlusion makes `n=0`, the miss-counter drops the lock,
    and the fallback shows the chase camera for a moment.

## The proper next step: anchor to the head BONE, not the body centroid

Instead of the World-translation (entity origin) averaged over draws, read the HEAD bone's world
position: `headWorld = World * headBoneMatrix_origin`. This gives a single, stable, correct anchor
at Raz's head — fixes the "at his center not his eyes" placement, kills the centroid wobble (one
bone, not a varying average), and provides FACING (the head bone's orientation) for the future
HMD-orientation decoupling. Requires identifying Raz's head bone index in the c96 palette, via
`DumpSkeletonInfo` (Lua) or empirically (perturb one bone, see what moves; or the topmost bone).
This is the SAME bone-map groundwork hand IK needs — not throwaway.

Orientation decoupling (view from the HMD, not the chase camera's sway) is the other half of
"comfortable" and also wants the head bone's facing. Both land together once the head bone is
mapped.

## Env / tuning surface built (all live-tunable, default off)

`PSYVR_FIRST_PERSON=1`; `PSYVR_FP_HEIGHT` (world-up lift), `PSYVR_FP_FORWARD` (small facing nudge),
`PSYVR_FP_SMOOTH` (anchor low-pass), `PSYVR_FP_PROBE=1` (logs recovered origin vs at/eye/baseUp).
Live keys: F5/F6 smooth, F7/F8 forward, F9/F10 height, F11 recenter.

## Assessment

The render-level extraction pipeline is the hard part and it WORKS — we can put a stable,
Raz-tracking anchor in world space from the render stream alone, no Lua, no VM risk. The gap to
playable is entirely the ANCHOR CHOICE (body centroid → head bone) plus orientation decoupling,
both gated on mapping Raz's head bone. That's the focused next session, and it doubles as the
hand-IK foundation. WIP build lives in the private `psychonauts-vr-mod` staging repo.

🤖 Session driven via Claude Code with live user gameplay tuning; no game files modified; extensive
iteration captured above so the next session starts from the exact failure modes already ruled out.
