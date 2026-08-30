# 40 — First head-tracked playtest (v0.1.4-alpha, real Quest 3): FOV zoom, culling pop-out, LOD billboard stereo mismatch

**Date:** 2026-08-18, ~22:46–22:50 session on the test PC (RTX 5080, Meta Quest 3 via SteamVR/oculus runtime).
**Build:** v0.1.4-alpha release zip (d3d9.dll SHA256 `753D22BC…635BB0`), upgraded from v0.1.0-alpha on this machine today.
**Verdict:** First actual head-tracked play session. Big step up — head tracking works, exit is clean (shutdown-hang from notes/33 did NOT reproduce), submits solid both eyes. Three issues found, one of them a shipped-feature regression. Player quote: "oh it is so much better."

## Environment / session config (from proxy log)

```
VRBridge: PSYVR_ENABLE_SUBMIT=1 -> g_vrSubmitEnabled=1
VRBridge: UI depth = 200 world units (PSYVR_UI_DEPTH; 0 = UI stays at infinity)
VRBridge: FOV scale = 1.00 (PSYVR_FOV_SCALE, 0.5..2.5; 1.0 = game default)
VRBridge: eye render scale = 2x (PSYVR_RENDER_SCALE defaulted)
VRBridge_Init: game device adapter = "NVIDIA GeForce RTX 5080"
VRBridge_Init: HMD identity: trackingSystem="oculus" model="Meta Quest 3"
VRBridge_QueryRealGeometry: eye=0 eyeToHead.x=-0.031460m projRaw l=-1.1918 r=0.8391 t=-1.0355 b=0.6745 centerOffset=-0.176327
VRBridge_QueryRealGeometry: GetRecommendedRenderTargetSize = 2496 x 2688 per eye
VRBridge_Init: SUCCESS - g_vrBridgeReady = TRUE (1600x1200 per eye)
HeadTrack: reference captured - pos=(-0.100,1.107,0.074)m yaw=24.2deg
VRBridge: Submit(eye=0) OK ... Submit(eye=1) OK   (continuous, no errors for the whole session)
```

## Confirmed working (regressions from notes/33–37 all clear)

- **Head tracking**: live and playable (`HeadTrack: T fwd=... t=... src=openvr` throughout). First real head-tracked gameplay ever on this project.
- **Clean exit**: no zombie process, no shutdown hang. The notes/34 fix holds on real hardware.
- **Both eyes render**: no black-left-eye anywhere, including menus (notes/35 fix holds).
- **Stereo correction uses real OpenVR geometry** (`dSrc=openvr kSrc=openvr` in SVSCF lines).
- **UI depth 200** active; 10 UI-signature shaders registered for per-eye depth shift.

## Issue 1 — REGRESSION: "suggested PSYVR_FOV_SCALE" log line never prints

The v0.1.4 release notes say the proxy log "now prints a `suggested PSYVR_FOV_SCALE` computed from your headset's real projection geometry." **The shipped DLL does not print it.** Case-insensitive grep over the full log (both the 08-17 and 08-18 sessions, 4400+ lines): zero matches for "suggest"; no alternate wording found either ("recommend"/"zoom"/"hfov"). Either the feature missed the release build or its print path never executes after `VRBridge_QueryRealGeometry`. Since it's the discovery mechanism for the FOV knob, players on other headsets have no way to find their value.

## Issue 2 — Default FOV feels zoomed-in/magnified (expected; knob untested at >1.0 so far)

Player confirmed the zoom is clearly noticeable at the default `FOV scale = 1.00`. Manual computation from the logged Quest 3 geometry, for the dev-side suggested-value formula:

- Game projection: `rawFov=104.000 aspect=1.3333 fovy=0.9076rad` → **52° vertical, ~66° horizontal** (xScale 1.5377 = 1/tan(33°)).
- Quest 3 per-eye frustum from projRaw: vertical atan(1.0355)+atan(0.6745) ≈ **80°**; horizontal atan(1.1918)+atan(0.8391) ≈ **90°**.
- Vertical ratio 80/52 ≈ **1.54**; horizontal 90/66 ≈ 1.36. → **Suggested PSYVR_FOV_SCALE ≈ 1.5** for this headset (vertical-match; horizontal overshoot is fine, compositor crops).

Test PC launcher now sets `PSYVR_FOV_SCALE=1.5` + `PSYVR_RENDER_SCALE=3`; first flight of those values is the next session. Will report back.

## Issue 3 — Objects vanish when looking over the shoulder (frustum culling doesn't know about head rotation)

Repro (100% consistent per player): turn head well off the game camera's axis (e.g. look back over a shoulder). World geometry in that direction is simply **absent — black, meshes and textures gone** — and pops back in when the game camera (character/controller) is turned that way.

Analysis: the engine does CPU-side visibility culling against the *game camera's* frustum before issuing draw calls. The proxy applies head-pose rotation later (view-matrix stage), so anything the engine culled never reaches D3D and the proxy cannot resurrect it. With the game's narrow 66°×52° frustum vs the headset's ~90°×80°, even *forward-looking* edges are at risk; a shoulder-check is far outside it.

Mitigation observed/expected: raising PSYVR_FOV_SCALE widens the game projection the engine culls against, so 1.5 should shrink (not eliminate) the pop-out. Real fix candidates, dev side:
1. **Feed head yaw (at least) back into the game camera** each frame — also fixes aim/interaction alignment long-term.
2. **Pad the culling frustum**: hook whatever the engine culls with (likely derived from the same BuildProjectionMatrix we already cache in BPM) and widen only the *cull* test, not the render projection.
3. Cheapest stopgap: cull with FOV_SCALE-widened projection but max(scale, ~2.0) for the cull test only.

## Issue 4 — Distant LOD imposters (2D billboard trees/bushes) flicker and read "cross-eyed" in stereo

Repro: distant trees/bushes render as the game's flat billboard imposters. In the headset they (a) **flicker**, and (b) look **cross-eyed / double-vision** until the player gets close enough for the 3D mesh LOD, which then looks solid and correct.

Analysis: the game makes billboard-orientation and LOD-selection decisions per rendered view. Our two eye phases present two slightly different camera positions per frame, so:
- each eye gets a billboard rotated to face *that* eye → the two eyes see the card at slightly different orientations → fused image looks wrong ("cross-eyed"), and a zero-thickness card at object depth has no correct stereo answer anyway;
- near the LOD switch distance the two eyes can *disagree* (one draws the sprite, the other the mesh, or the choice alternates frame-to-frame) → binocular rivalry / flicker.

Fix direction, dev side: make LOD selection and billboard orientation **deterministic per frame from the center-eye (head) position** — decide once in phase 1 and reuse for phase 2. If the decisions happen in engine code before our hooks, the same trick as Issue 3 applies: present the engine a single center camera for its per-frame decisions and only diverge the eyes at the view/projection matrices we already control. Longer term, forcing the 3D-mesh LOD at larger distances (imposters are a 2005 perf optimization we don't need on this hardware) would sidestep billboards entirely.

## Next steps

- [ ] Dev: restore/implement the suggested-FOV log line (Issue 1) — formula above.
- [ ] Dev: culling-frustum fix (Issue 3) and center-eye LOD/billboard decisions (Issue 4).
- [ ] Test PC: playtest with FOV_SCALE=1.5 + RENDER_SCALE=3; verify zoom gone, measure how much the culling pop improves, confirm 72Hz pacing holds at 2400×1800/eye.

🤖 Written from the test PC via Claude Code (no git here; pushed with `gh api`).
