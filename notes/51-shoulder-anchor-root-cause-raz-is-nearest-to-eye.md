# 51 — Shoulder-anchor root cause: Raz is the c96 entity NEAREST the eye (notes/49 filter was inverted)

**Date:** 2026-08-20, dev machine, monitor diagnostics (no SteamVR) + static Lua recon earlier
(notes/50). Goal: get the first-person camera truly anchored to Raz's shoulders and stable, per the
user's top priority ("get it stable on the monitor first, then the headset"). **Result: root cause
of the notes/48/49 FP bounce is FOUND and FIXED at the render level — no Lua, no bone extraction
needed for the anchor. The fix is validated in principle by the diagnostic data; visual FP
confirmation needs a SteamVR pass (see the architecture note below).**

## How we got here (route change, notes/50)

The session started aiming to finish the Lua control plane (DumpSkeletonInfo → head-bone index).
A pure-static disassembly (notes/50) showed the Lua string-exec primitive is a genuine
multi-session lift (luaB_dostring is an inlined compile+run, not a callable `lua_dobuffer`; needs
lua_pushstring + g_L capture + a game-thread pump). With the user, we pivoted to getting the
head/shoulder anchor **empirically from the render stream** — faster, no VM risk.

## Diagnostic: PSYVR_BONE_DUMP=1 (new, notes/51)

Added a monitor-path probe (runs in Hook_SetVertexShaderConstantF, no poses/SteamVR needed): once
per second, dump a burst of the next ~16 c96 (32-bone) draws — each draw's recovered entity World
origin (`WVP_row3 · P⁻¹V⁻¹`) + its eye-distance — plus all 32 bone model-space translations for
the burst's first draw. Two monitor captures (stand still, then walk straight).

## What the data proved (decisive)

1. **Origins are bimodal and rock-stable, NOT scattered.** Standing, exactly two 32-bone entities
   were drawn: one at `(-497,390,7)` with **eyeDist ≈ 11.6**, one at `(86,-62,121)` with eyeDist ≈
   759. Each origin repeated near-identically across the whole burst. This **refutes notes/49's
   "the centroid wobbles because the draw set varies"** as the root cause — every entity's origin
   is individually stable.
2. **Raz = the entity ~11.6 wu from the eye.** Walking (world coords ~52500,-33016,-4388), only ONE
   32-bone entity remained in frame and its origin stayed **exactly eyeDist ≈ 11.6** while tracking
   every camera move perfectly. That rigid, constant chase-cam offset is Raz. The far `(86,-62,121)`
   entity in the standing burst was a separate NPC in the start area.
3. **notes/49's filter was inverted.** It rejected origins within `0.35·focus ≈ 67 wu` of the eye as
   "screen-space / camera-attached", then centroid-averaged the rest within a lock radius. Raz sits
   at **11.6 wu < 67**, so the filter **rejected Raz himself**; when it didn't, it **averaged Raz
   with distant NPCs** → the jitter and the "anchor at his center, not his eyes" both trace to this.

## The fix (render-level, shipped in the DLL)

In `Hook_SetVertexShaderConstantF`, for each c96 (n≥96) draw during an eye pass, recover the origin
and **track the single NEAREST-to-eye one this frame** (= Raz). No centroid, no near-eye rejection.
Cap the candidate at `0.75·focus` (floored at 60 wu) so that when only distant NPCs are on-screen
(Raz occluded/cutscene) we report "no candidate" and hold the last lock instead of snapping to an
NPC. The FP promotion (`VRBridge_UpdateHeadTracking`) now consumes this single origin with EMA
smoothing + a >500 wu snap for level loads/camera cuts. Globals `g_razNearOrigin/Dist2/Valid`
replace the `g_razSum*/g_razCount` centroid. Vertical offset for shoulder/eye height stays the
tunable `PSYVR_FP_HEIGHT` (F9/F10). New `ANCHOR:` log line (on PSYVR_FP_PROBE or BONE_DUMP) reports
the chosen origin from the monitor path for SteamVR-free stability verification.

## Architecture note (important for "stable on the monitor first")

**First-person cannot render without SteamVR.** The FP eye-repositioning lives in
`VRBridge_UpdateHeadTracking`, called only from `VRBridge_PumpPoses`, which hard-returns unless
`g_vrSubmitEnabled && g_vrBridgeReady` (OpenVR/SteamVR up). `PSYVR_FAKE_POSE` synthesizes a pose
VALUE but still needs the bridge initialized. So: the **anchor point** is verifiable on the monitor
(the `ANCHOR:`/`BONEDUMP:` lines run in the vertex hook), but **seeing the FP view** needs SteamVR
running (headset can sit on the desk; watch the desktop mirror). Options for true monitor-only FP
preview (decouple the FP X1 onto the monitor path with an identity head pose) are a possible future
change — flagged, not yet done.

## VALIDATED on the monitor (same session)

Re-ran with the fix + `ANCHOR:` probe. Standing: raz held ~`(-497.7, 390.8, 7.6)` — XY steady
within ~1.5 wu, ~3 wu Z shimmer from idle animation; eyeDist locked 11.6. Walking: raz glided
smoothly `(52511,-33017,-4382)` → `(52096,-32457,-4124)`, eyeDist steady 11.6–11.8, zero jumps.
The old inter-entity centroid jitter (100s of wu) is gone. Anchor identification is correct and
stable. (Note: eye Y ≈ 397.7 sits only ~7.5 wu above raz origin Y ≈ 390.2 — the origin is ~his
neck/upper chest, so the shoulder/eye offset PSYVR_FP_HEIGHT should start small & POSITIVE, ~+5..+10,
not the old -20 which was tuned to the broken anchor.)

## Next

- [DONE] Verify `ANCHOR:` lines on the monitor: stable standing, smooth tracking + eyeDist ~11.6 walking.
- SteamVR pass to confirm the eye sits at Raz's shoulders and the position bounce is gone (tune
  PSYVR_FP_HEIGHT for exact shoulder/eye height). Residual *rotational* sway (chase-cam right/up
  swing through V) is the separate orientation-decouple problem (HMD-only orientation), tackled
  next once position is confirmed solid.
- Bone-translation dump (all 32) is banked for later: hiding Raz's head/goggles mesh + hand IK.

🤖 Session via Claude Code: static Lua recon (notes/50) → monitor bone-dump diagnostics → render-level
root cause + fix. No game files modified; splash videos silenced during launches and restored after.
