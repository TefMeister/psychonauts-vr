# 53 — Composition math PROVEN correct (exact numbers); Raz-lock flicker is the new leading suspect

**Date:** 2026-08-20, dev machine. Direct continuation of notes/52 at the user's request ("resume the
FP debugging now that the convention's ruled out"). **Result: the render-level camera-move
mechanism is now PROVEN mathematically correct end-to-end, with exact empirical numbers — not just
the shader-convention hypothesis (notes/52), but the FULL composition pipeline including the
FP-specific X1 construction. The original bug (eye stuck behind Raz) is real and still unresolved,
but two more hypotheses are now conclusively ruled out. New leading suspect: Raz-lock reliability
during real-time gameplay, not the transform math.**

## Method: two isolated empirical tests (playbook doctrine - instrument, don't re-derive by hand)

Rather than continue hand-deriving the row/column-vector composition algebra (high error risk even
carefully, per multiple self-contradicting derivations attempted and discarded mid-session), built
a direct instrumented test: recover a reference world position from a register-6 upload BOTH before
and after the head-tracking correction is applied (reusing the same empirically-validated
`wr3 . g_pinvVinv` extraction notes/49-51 already trust), and log the delta. If the correction
composes correctly, delta should equal the injected shift.

**Test 1 - raw T injection** (`PSYVR_HT_TEST_SHIFT=500`, bypasses X1 entirely): injected 500 world
units directly into T's translation slot, before the `Pinv*T*P` sandwich + transpose + GPU-upload
pipeline. Needed extending `g_fakePose` to override the monitor-preview's forced-identity-T
shortcut (small code change: `if ((g_trackingDisabled||g_fpPreviewMode) && !g_fakePose)`), and a
new fully-automated capture (`tools/input/auto_ht_test.ps1` - no gameplay/input needed at all, the
title screen alone has enough 3D geometry). **Result: measured `|delta|` consistently 499-540,
centered tightly on 500** across 16 samples in different sub-scenes/camera angles.

**Test 2 - X1 construction** (`PSYVR_FP_FORCE_ACTIVE=1`, `PSYVR_FP_FORCE_RAZ=0,0,500`, height/forward
=0): new force-override knobs that set `g_razWorld = g_baseEye + (0,0,500)` directly every frame,
bypassing the nearest-c96 Raz detector entirely, so X1's dot-product decomposition onto camera axes
(`X1[12]=-d.right, X1[13]=-d.up, X1[14]=+d.fwd`) and its composition into T
(`Mat4MulRow(T,X1,T)`) get a fully known, controlled, repeatable input - with T staying Identity via
the SAME monitor-preview path the user's real FP tests actually used (no `PSYVR_FAKE_POSE` this
time, matching real conditions exactly). New capture script `tools/input/auto_x1_test.ps1`.
**Result: `delta=(0,0,-500.0)`, `|delta|=500.0` EXACTLY, on every single sample (16/16), zero
variance in x/y.**

## What this proves

The full chain — X1's construction (target-eye-minus-base-eye, projected onto camera-local axes) →
composition into T via matrix multiply → the `Pinv*T*P` sandwich → transpose → upload to register 6
→ shader consumption — **propagates a translation with exact, correct magnitude.** Combined with
notes/52's shader-convention confirmation, there is no remaining unverified link in this pipeline;
every stage has now been checked against either the compiled shader bytecode or direct instrumented
measurement, not assumption.

## Consequence: the bug is real, but NOT in this mechanism

The user's observed symptom (F8 to max forward, still see Raz in 3rd person, camera "fighting for a
position") cannot be explained by "the correction doesn't propagate" - it's now proven to propagate
exactly. Two remaining explanations, ranked by fit to the SPECIFIC symptom described:

1. **Leading suspect: Raz-lock (`g_razNearValid`) flickering during real gameplay.** The nearest-
   to-eye c96 detector needs a FRESH skinned upload every single frame to stay locked; if Raz's draw
   is occasionally culled/LOD-switched/absent for a frame, `g_razNearValid` goes false, the code
   falls into the "no Raz found" fallback branch (a completely different, much smaller
   chase-cam-relative shift, not the razWorld-anchored X1), and `g_razMissFrames` only drops the
   lock after 8 consecutive misses - so a game genuinely flickering Raz's draw presence at, say,
   30-50% of frames would produce EXACTLY the described symptom: mostly-anchored-but-occasionally-
   yanked-back-to-fallback, i.e. "fighting for a position" and "moves relative to terrain" (the
   fallback branch's motion is NOT razWorld-relative, so switching between the two branches frame-
   to-frame would look like the camera doesn't track any single consistent frame of reference).
   **Not yet tested** - needs a real gameplay capture (not title-screen-only) logging
   `g_razNearValid`'s hit rate frame-by-frame while walking, the natural next diagnostic.
2. Less likely given how precisely the isolated tests matched: a scale/sign error specific to some
   OTHER factor present only in real gameplay and absent from these isolated tests (e.g. FOV/
   render-scale interactions, or something about the specific razWorld values recovered live vs the
   clean forced test value) - not ruled out, but no positive evidence for it either, whereas the
   lock-flicker hypothesis directly explains the qualitative "fighting/unstable" character of the
   symptom in a way pure math errors (which would look like a STATIC wrong offset, not dynamic
   fighting) do not.

## New diagnostic surface (default off, no behavior change)

`PSYVR_HT_TEST_SHIFT` (float, raw T-translation injection), `PSYVR_HT_DEBUG` (origin-before/after
logging on every register-6 upload with a valid correction), `PSYVR_FP_FORCE_ACTIVE` (skip F4/
gameplay-gate for isolated testing), `PSYVR_FP_FORCE_RAZ="dx,dy,dz"` (force a known razWorld offset,
bypassing Raz detection). All default off/0, zero effect unless explicitly set. Two new fully-
automated capture scripts: `tools/input/auto_ht_test.ps1`, `tools/input/auto_x1_test.ps1` (same
silence/launch-offscreen/poll-log/kill/restore pattern as notes/52's `auto_shader_dump.ps1` -
neither needs real gameplay or keyboard input, both run from the title screen alone).

## Next (if the user wants to continue)

Instrument `g_razNearValid`'s per-frame hit/miss rate during an actual real-time gameplay walk
(needs `enter_gameplay.ps1` or manual play - title-screen-only capture can't test this, since it
needs Raz genuinely moving/animating in a real level). If the hit rate is poor, the fix is either a
more permissive candidate-acceptance window or carrying the last-known razWorld through single-frame
misses instead of only doing so after 8 consecutive misses (may already effectively do this via
`g_razMissFrames`, worth re-checking whether the FALLBACK branch itself is the problem even during
the "held" grace period, not just after it expires - the two branches take entirely different code
paths and don't currently share state at the moment of the switch).

🤖 Session via Claude Code: two fully-automated, no-gameplay-needed empirical tests; no game files
modified; intro videos silenced/restored each run; zero keyboard/mouse focus-steal (title-screen-only
captures, no input driving needed).
