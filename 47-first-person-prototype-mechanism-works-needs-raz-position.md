# 47 — First-person prototype: WORKS (reached inside Raz's head); residual bounce = chase-cam rotation

**Date:** 2026-08-19, dev machine, real gameplay via null-driver VR bridge, live user-driven
tuning on the monitor. Implements notes/44/46 route B (render-level first person, no Lua).
**Result: TRUE first person achieved — the user confirmed the view sits inside Raz's head (head
mesh clips away). A real bug was found and fixed mid-session. Position smoothing added; the
remaining discomfort is the chase camera's ROTATIONAL sway, which pins the proper next step.**

## What was built (PSYVR_FIRST_PERSON=1, default off — byte-identical when unset)

First person is a view-space forward translation `X1` composed into the SAME `Y = P^-1 * X * P`
premultiply the head-tracking path already applies to register 6 (notes/34). The eye is slid
forward from the chase-camera position along its view axis onto Raz, then head tracking rotates
the view about the new origin. Rides the existing c6 correction, so it inherits stereo, the
per-eye offset, and head rotation for free. Live tuning keys (no relaunch): **F5/F6** smoothing,
**F7/F8** forward -/+, **F9/F10** height -/+, logged as `FP tune:` lines.

## Bug found and fixed mid-session (this is why the first attempt "did nothing visually")

The FP block was gated on `g_frameCamCached`, but `Hook_Present` resets that flag to FALSE
*before* it calls `VRBridge_PumpPoses()` (which runs the head-tracking/FP update). So the guard
was always false and FP never reached the render — the user reported "pressing the keys did
nothing visually" while the tune log still changed (the key handler runs earlier). Fixed by
dropping the guard: `g_focusDistance` persists across frames, so it's valid regardless. **Lesson:
verify the feature actually affects pixels, not just that the knob logs — the user's "nothing
visually" caught a bug my eye dumps (plain 3rd person from a stale flag) had masked.**

## Result after the fix (user-driven, real save, Whispering Rock)

- **TRUE FIRST PERSON reached.** At **PSYVR_FP_FORWARD ~2.2, PSYVR_FP_HEIGHT -20**, the user:
  *"this is the closest to being inside Raz's head — I can see the goggles still but the head mesh
  disappears."* The head mesh clipping away is the near-plane doing us a favour (we're inside it);
  the goggles are a separate mesh that still needs hiding.
- **Why forward ~2.2 (not <1):** the chase camera's `at` (look-at) is an aim point SHORT of Raz
  (above/ahead of him), at ~192wu from the eye. Reaching his head needs ~423wu forward = ~2.2x the
  eye->at distance. Confirmed by the sweep: 1.0 overshoots into the air, the user settled ~2.2.
  (Env + key clamps raised 1.2 -> 4.0 to allow this.)

## Camera-position smoothing (added; helps, not sufficient alone)

The chase camera bobs/springs as Raz walks and our forward offset multiplies the springy eye->at
distance, so both rode into the VR view as bounce. Added an EMA low-pass on the eye position AND
focus distance (`PSYVR_FP_SMOOTH`, default 0.15; F5/F6 live), with a hard snap on >500wu jumps so
teleports/level-loads/camera-cuts don't glide across the map. Head ROTATION stays crisp (it's from
the HMD, not this), so smoothing the game-camera-driven POSITION is comfortable, not laggy. Math:
a camera world-move `d` becomes the view-space translation `(-d.right, -d.up, +d.fwd)` — same sign
framework the forward/height terms use (self-consistent, verified: standing still is now
rock-steady, `eye` frame-to-frame 52510.88 -> 52510.81).

**User verdict: "better but still bounces."** Log diagnosis: while stationary the eye is steady,
but the camera BASIS swings during movement (`right` 0.539 -> 0.607 -> 0.611 as Raz walks) — the
chase camera YAWS/PITCHES to follow Raz, and that rotational sway passes through the view matrix
uncorrected. **Position smoothing fixed the translational bob; the rotational swing remains.**

## The real fix (why this pins the next step)

The residual discomfort is architectural: in true VR first person the view ORIENTATION should come
from the HMD alone, NOT from the chase camera's Raz-following swing. Riding the chase camera's
orientation means every time it re-aims, your head appears to turn — nauseating and not "first
person." Two coupled fixes, both needing **Raz's actual head transform** (position + facing):

1. **Lock the eye to Raz's head bone** (smooth, moves rigidly with Raz) instead of the springy
   chase camera — removes translational bob AND the aim-point-overshoot guesswork in one move.
2. **Take orientation from the HMD + Raz's facing**, discarding the chase camera's yaw/pitch swing
   — removes the rotational sway.

Both need Raz's head world transform, available two ways (notes/44/45/46):
- **Lua route (cleanest):** `GetBoneWorldPosition(raz, headBoneId)` once the in-process Lua exec
  primitive exists (notes/46's remaining ~15-min debugger dig for `lua_dobuffer` + L-capture).
- **Render-level bone extraction:** Raz's head bone is already in the c96 palette (BONEPROBE);
  recover his world matrix from the cached c6 WVP and known V,P. Needs draw identification + the
  bone-index map — the same groundwork hand IK needs.

## Still outstanding for a shippable first-person mode

- Hide Raz's goggles (and body/head at other angles) — per-draw entity identification, shared with
  the bone route.
- Orientation decoupling from the chase camera (above) — the main comfort item.
- Comfort fallbacks for cutscenes/ledges/levitation ball (scriptable camera API, route A).

## Value delivered

- **First person is real and reachable at render level** — proven in-headset-equivalent on the
  monitor, tunable live. The `X1` machinery is done; when Raz's head transform is available it's a
  one-line target swap plus orientation source change.
- Position smoothing is a reusable comfort primitive.
- The exercise converges hard on the same conclusion as notes/46: **the Lua/bone route unblocks the
  whole goal set** (true first person, hiding Raz, hand IK, body, shadows). That's the throughline.

## Recommended next step

Finish the Lua exec primitive (notes/46), get Raz's head transform, then rebuild FP as
head-bone-locked + HMD-oriented — that removes both the position bob AND the rotational sway and
gives real, comfortable first person. Render-level bone extraction is the no-Lua fallback and
doubles as hand-IK groundwork.

🤖 Session driven autonomously via Claude Code (build + live user-driven gameplay tuning; a real
bug caught by the user's "nothing visually" report; no game files modified, no releases).
