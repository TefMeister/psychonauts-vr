# 63 — Live A/B test completed: disabling Visibility Tree Culling does NOT remove the void

**Date:** 2026-08-24, dev machine. Direct follow-up to notes/62 (toggle built and verified but the
live disappears-vs-persists test wasn't reached). This session reached it.

## What changed to get there

1. **Switched test level from `CAJA` (Sasha's Lab) to `CABH`** (`PSYVR_LEVEL_JUMP_CODE=CABH`) — an
   outdoor Campgrounds beach area, per notes/55's table. No immediate NPC dialogue trigger at spawn,
   unlike `CAJA`. Confirmed real: exe string `_Campgrounds\CA_MAP_CABHoverlay.plb` (notes/55), and the
   visual content (sand, rope bridge, huts, "Boathouse and Beach" save name) matches.

2. **Found and fixed why head-tracking sway wasn't actually rendering.** `VRBridge_UpdateHeadTracking`
   (the function that applies `g_fakePose`'s synthesized yaw/pitch sway) is only invoked on the
   monitor-preview path when **`g_firstPerson` is true** (`proxy_d3d9.c` ~line 3593:
   `if (g_firstPerson && !(g_vrSubmitEnabled && g_vrBridgeReady))`). Neither this session nor notes/62
   ever set `PSYVR_FIRST_PERSON=1` — meaning **every prior "does the view rotate" observation this
   whole investigation was never actually testing head-tracking sway at all**; the flat monitor path
   silently no-oped the entire function. All those "frozen camera" and "no visible rotation" readings
   from notes/62 were real observations of a real gap, just not the one originally suspected (scene
   navigation) — the gap was this env var. Fixed by adding `PSYVR_FIRST_PERSON=1` to the launch (with
   `PSYVR_FP_FORCE_ACTIVE` left **unset**, so the camera stays normal third-person rather than
   switching to the FP prototype, which currently doesn't hide Raz's own model and self-occludes the
   view — confirmed by testing FP mode first and seeing exactly that).

   **The `BVM cache SET` log line's `right` vector is not a valid way to check whether head-tracking
   is rotating the render** — it logs the game's own base view matrix, which the head-tracking sway is
   applied on top of at the per-draw WVP level, invisible to that log point. It stayed frozen at
   `(-1,0,0)` in every capture this session (both with and without the fix), including ones that
   *did* visibly rotate on screen. Use the actual rendered frame to judge rotation, not this log line
   — the notes/62 conclusion that stemmed from trusting it was a false signal.

3. **The auto-pause dialog turned out to require genuine window focus to dismiss, which the user's
   own foreground activity (browser, actively being used to watch this session) was legitimately
   denying** — `SetForegroundWindow` and even `send_key.ps1`'s `AllowSetForegroundWindow`-based retry
   both failed outright (`GetForegroundWindow()` stayed pointed at the user's Chrome throughout).
   This is normal Windows focus-stealing prevention working as intended, not a bug. Asked the user to
   click the game window once; they did, focus transferred cleanly, the pause dismissed on the very
   next raw `SendInput` ENTER. No further `SetForegroundWindow`-triggering calls were made afterward,
   per the standing lesson from notes/62 (raw `SendInput` only for F12/NUMPAD9 once real focus exists).

## The test

With `PSYVR_FIRST_PERSON=1`, `PSYVR_FAKE_POSE=1`, `PSYVR_FAKE_POSE_YAW_DEG=170`, real third-person
gameplay in `CABH`, no dialogue/pause blocking: captured frames across the ~12.6s sway period
(`sin(t*0.5)`, amplitude 170°) both **before** toggling (culling ON, default) and **after** pressing
NUMPAD9 (culling OFF, confirmed in the log: `flag @ 05EA9201 now 0`).

**Result: a large, screen-filling black region appears at the same sway phases in both states, with
closely matching size, shape, and position.** Two matched pairs, ~3 minutes apart in wall-clock time
but at corresponding points in the (independently, continuously running) sway cycle:

| Phase | Culling ON | Culling OFF |
|---|---|---|
| "behind," near-full black | `tp5.png`: black fills ~85% of frame, only a sand-ridge line visible at the bottom | `off1.png`: near-identical — same ridge line, same proportions, same colors |
| "behind," rounded black mass | `tp4.png`: large rounded black shape, greenish sky visible around it, fence-post objects lower-right | `off4.png`: near-identical — same rounded shape, same greenish sky swirl, same fence posts |

**Direct answer: the void does not disappear, shrink, or visibly change when Visibility Tree Culling
is disabled.** This is a clean negative result, not an inconclusive one — both matched-phase pairs are
close to pixel-identical by eye, and non-void phases (`tp3`, `off2`/`off3`/`off5`/`off6`) show normal,
fully-populated scenery in both states too, so the toggle is doing *something* (confirmed already via
the log in notes/62) even if it isn't affecting whatever produces this black region.

## Important caveat — this may not even be "the void bug"

Looking at the non-void frames from the same session (`off3.png`, `off6.png`), there's a large grey
rock/cliff formation in this exact area of `CABH`, unlit on its shadowed face, silhouetted against a
bright sky. Its outline is a plausible match for the smooth, rounded black shape in `tp4`/`off4`. A
third-person orbit camera swung by ±170° yaw sway is very likely to point directly at/through nearby
terrain the game's normal camera never aims at — an unlit rock face rendering near-black in silhouette
against bright sky is a mundane, expected result of that, not evidence of missing geometry. The
flatter, more edge-to-edge black region in `tp5`/`off1` is less obviously rock-shaped and remains a
better candidate for an actual gap, but wasn't distinguishable from "very close backlit terrain" with
certainty either.

**This matters for where to look next**: the octree/Visibility-Tree-Culling hypothesis this whole
investigation (notes/59-62) has been chasing may be the wrong mechanism — or at least, this
specific flag isn't gating whatever produces the black regions seen here. The clean, non-destructive
way to tell "unlit terrain silhouette" apart from "actual missing geometry" going forward: compare
against the SAME phase with Psychonauts' own in-game **lighting/wireframe debug toggles** (several
exist in the same debug menu discovered in notes/61, unexplored) rather than only camera angle, since
a real gap stays black regardless of light while backlit terrain would show up as flat-shaded gray/
colored geometry under wireframe or unlit-flat rendering modes.

## Cleanup

Game process killed, intro videos restored (none left `.silenced`), Visibility Tree Culling flag
toggled back to its default `1` (ON) before killing the process. No save files were created, modified,
or needed deleting this session — the successful run never touched the Journal/save menu, only F12,
NUMPAD9, and one raw ENTER to dismiss an auto-pause dialog. No new DLL was built or deployed this
session (reused notes/62's build as-is, no code changes were needed to reach this result — only launch
env vars and test-level choice changed).

🤖 Live testing via raw `SendInput` (no `SetForegroundWindow` after initial focus), one user-assisted
window-focus click, `x64dbg-skills` not used this session (no further decompiling needed — notes/61-62
already covered the relevant code).
