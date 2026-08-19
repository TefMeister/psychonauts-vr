# 43 — Tangent-matched submit bounds kill the zoom for real; UI viewport shrink post-mortem; suggested-FOV log fixed

**Date:** 2026-08-19, dev machine session (v0.1.7-alpha built and dev-verified; awaiting Quest 3 flight).
**Queue context:** notes/42 ranked "UI x/y rescale for FOV scale" as priority 1 (HUD invisible at FOV 1.5 on Quest 3). This session first built exactly that fix, watched it fail live in an instructive way, and ended up replacing the whole FOV-zoom workaround stack with the correct mechanism instead.

## Part 1 — The notes/42 UI shrink: built, live-tested, and demoted to experimental

The proposed fix (shrink screen-space UI back to its native angular footprint when FOV scale > 1)
can't be done through shader constants — the 10 UI shaders are purely additive (`oPos = input +
c50`), so there is no register that scales positions. Implemented instead as a **viewport shrink**:
while a UI-signature shader is bound during an eye phase, render through a center-shrunk viewport
(auto factor = tan(fovyBase/2)/tan(fovyScaled/2), 0.602 at FOV 1.5), restoring on non-UI binds and
phase changes. The depth-shift constant was compensated by 1/shrink so perceived UI depth stayed put.

**Live result: correct for actual HUD elements, catastrophic for everything else.** The user
entered gameplay and reported "everything is dark and the brain intro part was all wrong". Eye
dumps nailed the mechanism: the game draws its **fullscreen overlays through the same UI shader
signature** — pause/menu backdrops, fades, the works. Those must span the full widened frame; the
shrink crushed them into the center ~60% and left black borders (the paused-game dump shows the
pause art shrunk to center with pure black around it, while the "your game was automatically
paused" dialog itself shrank *correctly*).

Classification recon (one-frame TRACE-UI logging, now permanently available under
`PSYVR_TRACE_FRAME=1`):
- `c50` is NOT a per-element position — it's the classic **D3D9 half-pixel offset** (exactly
  (-1/640, 1/480) at 640x480) plus occasional 1–2px nudges (drop shadows). Element positions live
  in the vertex buffers. So no cheap constant-based fullscreen-vs-element discriminator exists.
- Shader identity doesn't separate them either: UI shader **#0 draws everything rectangular** —
  fullscreen backdrops, HUD quads, and glyph-batch text (prims=2 / 26 / 84 / 144 / 588 triangle
  lists); #2/#3/#4 draw single-triangle-strip particles/sparkles. A usable classifier would need
  per-draw vertex extents (VB reads) or texture identity — real work, deferred.

**Disposition:** machinery kept but inert. `PSYVR_UI_SCALE` (0.25..1.0) now sets an absolute
opt-in shrink for experiments; default 1.0 = off. The TRACE-UI diagnostics and `g_lastC50`
tracking stay (trace-gated, zero hot-path cost).

## Part 2 — The actual fix: tangent-matched VRTextureBounds at Submit (the zoom was never a FOV problem)

Stepping back from the failed patch exposed the real architecture error. The compositor maps the
submitted texture onto the headset's per-eye lens frustum **linearly in tangent space, regardless
of what FOV the frame was rendered with**. We always submitted the full texture (`pBounds=NULL`),
so the game's narrow frame got stretched onto the wide lens frustum = the zoom. PSYVR_FOV_SCALE
(notes/37/40) was a per-headset hand-tuned workaround for that stretch — and the HUD invisibility
at scale > 1.2 was collateral damage from the same stretch pushing frame edges past the lens crop.

`VRBridge_SubmitBounds(eye)` now computes, per eye, the sub-rectangle of our frame whose tangent
extents equal the headset's real frustum (`GetProjectionRaw`, cached in the new
`g_realProjRaw[2][4]`), and passes it to `IVRCompositor::Submit`:

```
frame x-tangents: [k - 1/xScale, k + 1/xScale]   (k = the per-eye shear the WVP patch applies)
frame y-tangents: [-1/yScale, +1/yScale]         (no vertical shear yet)
u(tan) = (tan - (k - xHalf)) / 2·xHalf ;  v(tanUp) = (yHalf - tanUp) / 2·yHalf   (v=0 = top)
```

Consequences:
- **Angular mapping is exactly 1:1 at ANY FOV scale.** No more per-headset zoom derivation; the
  1.5-from-tangent-math of notes/40 is obsolete.
- **PSYVR_FOV_SCALE changes meaning:** it now only controls how much frame exists *outside* the
  visible window — culling margin (notes/40 §3) and lens coverage — not the zoom.
- **Bounds are clamped to [0,1]** where the frame is smaller than the lens frustum (the compositor
  then mildly stretches that axis). The log prints the exact scale for full coverage:
  **Quest 3 needs PSYVR_FOV_SCALE ≥ 1.77** (vertical-driven: the Quest 3 frustum reaches tan 1.0355
  up vs 0.6745 down, and we render vertically symmetric). At 1.0 everything clamps to the full
  texture = old behavior, so monitor-only and default configs are behaviorally unchanged.
- **The ~10° vertical frustum asymmetry the old path silently ignored is now handled** on the
  horizontal axis by the existing shear and vertically by the crop (when coverage allows). A
  proper vertical render-shear (symmetric-frame waste removal) is a future upgrade.
- **HUD at any FOV scale now lands at its lens-relative angles** (like the FOV 1.0 sessions the
  user found fine), instead of being compressed into invisibility. Extreme HUD corners can still
  clip a few degrees at high scales — acceptable until a per-draw classifier revives the shrink.

## Part 3 — notes/40 Issue 1 (missing suggested-FOV log): root cause + fix, live-verified

The v0.1.4 line lived in `VRBridge_QueryRealGeometry` guarded by `g_projYScale > 0` — but that
function runs at bridge init, **before the game has ever built a projection matrix**, so the guard
was always false and the line could never print. Now: QueryRealGeometry stashes the HMD vertical
FOV (`g_hmdFovyRad`), and `BPM_OnEntry` one-shot-logs the suggestion at the first projection cache
(suggested value computed against the *default* fovy — current/scale — so it stays right even when
a scale is already active). Live-verified on the null driver this session:
`BPM: HMD vertical FOV=90.0 deg, game default fovy=52.0 deg -> suggested PSYVR_FOV_SCALE=1.73`.

## Dev-machine verification (null driver, FOV 1.5)

- Bounds computed and accepted: `submit bounds eye=0 u=[0.030,0.956] v=[0.000,1.000] (CLAMPED…)`
  with continuous `Submit OK` both eyes, no compositor errors.
- Offline math validation against the real Quest 3 tangents from the notes/40 log:
  FOV 1.0 → all-clamped (= old behavior); FOV 1.5 → u=[0.030,0.970] exact, v top clamped 14%;
  FOV 1.8 → u=[0.142,0.858] v=[0.014,0.817], zero clamps. Matches the in-DLL suggestion (1.77).
- Title screen/menu render normal again after the shrink demotion (user-confirmed on monitor +
  full-brightness eye dump), clean exit, saves intact.
- Gameplay-entry side note: `enter_gameplay.ps1`'s blind walk missed the CONTINUE door twice today
  (position drift); a manual 10-second assist was faster. Needs the eye-dump-servo upgrade the
  notes/39 header already suggested before it counts as reliable.

## v0.1.7-alpha (built, ready to release)

- Tangent-matched submit bounds, ON by default with real HMD geometry (`PSYVR_SUBMIT_BOUNDS=0` reverts).
- Suggested-FOV log line restored (deferred to first BPM cache) + full-coverage suggestion in the bounds log.
- `PSYVR_UI_SCALE` demoted to experimental absolute shrink (default off); fullscreen-overlay
  regression from this morning's intermediate build eliminated.
- Quest 3 launcher: FOV 1.8 + render scale 3 (full lens coverage); generic launcher docs updated.

## Quest 3 flight checklist (next headset session)

- [ ] Zoom gone at 1:1 mapping? (Should be *exact* now, not "better".)
- [ ] HUD visible at FOV 1.8?
- [ ] Culling void: reduced at 1.8 vs the 1.5 session? (Still expected behind the shoulder — real fix still queued.)
- [ ] 72Hz holds at FOV 1.8 + render scale 3 on the 5080?
- [ ] Log: `submit bounds eye=…` lines present with zero clamps at 1.8.

## Next dev queue (carried + updated)

1. Head-yaw feedback into the game camera / cull-frustum widening (notes/40 §3 — comfort issue).
2. Center-eye LOD/billboard decisions (notes/40 §4).
3. Vertical render-shear (stop wasting frame on the symmetric-vs-asymmetric vertical mismatch).
4. Per-draw fullscreen-vs-element classifier to revive the UI shrink (texture identity or VB extents).

🤖 Session driven autonomously via Claude Code on the dev machine.
