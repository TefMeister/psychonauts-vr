# Head-follow camera wired to the void fix — monitor-validated, ready for a headset test

**Date:** 2026-08-28 · **Session:** modding (dev PC) · **Status:** ✅ works on the monitor, **NOT yet worn**

## What this is

The void fix (`ENGINE-DOSSIER.md` §9b) made the game's own camera transform writable.
This wires it to **live head yaw**, so turning your head turns the engine's camera
**before it culls** — the void fix as a player actually experiences it, rather than a
command I type.

Commands: `camfollow 0|1`, `camfollowscale <-4..4>` (sign/gain, tunable live).

## Monitor validation (synthetic head sway, no headset)

`PSYVR_FAKE_POSE=1` with `PSYVR_FAKE_POSE_YAW_DEG=60` supplies a +/-60 deg sway, so the
whole thing is testable at a desk. `Launch-Psychonauts-FollowTest.bat` sets it up.

| | near-black |
| --- | --- |
| follow **off** (void present) | **6.72%** |
| follow **on**, sampled across the sweep | **1.85 – 2.73%** |

Frames render fully at every sway position and differ from one another, so the view is
genuinely tracking. Head yaw confirmed live (read 9.9 deg, then 83.4 deg mid-sweep) —
**well past the 87.4 deg free-look clamp** that caps the gamepad route.

## Three defects caught before anyone wore it

1. **Double rotation — the dangerous one.** The register-6 path *already* rotates the
   rendered image by head yaw. Adding a camera rotation on top turns the view **twice**:
   the world swinging at double head speed. That is a motion-sickness generator, not a
   cosmetic bug. **Fix:** split the work — `camfollow` owns YAW (the half that fixes
   culling, and the half with no clamp); the register-6 path keeps pitch, roll and
   positional head motion. Yaw is cancelled out of the pose before that correction is built.
2. **Head tracking never ran on a monitor.** `VRBridge_UpdateHeadTracking` is only called
   from the SteamVR pose pump (inert with no headset) and an FP-preview path gated on
   `PSYVR_FIRST_PERSON`. So head yaw would have sat at 0 forever and the test would have
   measured nothing *while appearing to pass*. Gate widened to include follow mode —
   deliberately WITHOUT setting `g_fpPreviewMode`, so the first-person build is not dragged
   into a test that is not about first person.
3. **Log spam of my own making.** The snapshot logged every frame; in follow mode the
   engine updates the camera every frame, so that wrote ~60 lines/sec — 13.6 MB in one
   short test, burying the lines that matter. Now throttled to fire only on real change.

## Honest caveats

- **Not worn yet.** Comfort is the one thing that cannot be measured from the dev PC.
  Nothing is uploaded until it has been tested at home (user's call, and the right one).
- **At large yaw the camera looks into hillsides.** Expected for a low chase-cam rotating
  in place — turn your head 60 deg in a valley and you see the valley wall. Whether it
  *feels* right is a headset question.
- **Sign unverified.** If the view turns the wrong way, `camfollowscale -1` flips it live,
  no rebuild. The correct sign is a convention question best settled by looking, not
  guessing (XIII needed its HMD yaw negated for the same reason).

## Why first person was deliberately NOT wired in the same build

The user asked whether to do both at once. Reasons for one at a time, and the
double-rotation bug above is the strongest one — with two untested changes stacked, an
uncomfortable headset session could not have been attributed to the camera, the FP anchor,
the sign, or that bug.

Also, on reading the existing FP code: it composes a translation into the **render**
transform (`X1` into `T`, the register-6 path). That is the **post-culling** layer this
session proved cannot fix the void — which very likely explains the notes/51-53 mystery
(a computed translation producing zero visible eye movement). It was never moving the
camera, only the picture. FP should be **rebuilt** on `+0x08` (position) + `+0x150`
(orientation) so the engine culls and renders from the head. That is a rewrite, not a
wire-up, and it belongs in its own build.

## Next

1. **Headset test at home** (user): does it feel right, is the sign correct, is it comfortable.
2. If good — release as v0.1.8-alpha with the standard disclaimer and motion-sickness
   caution. **Not before.** The repo's last release is v0.1.7-alpha (2026-08-19).
3. Then first person, rebuilt at the engine level. The project's real goal is FP gameplay
   with third-person dialogue and cutscenes.
