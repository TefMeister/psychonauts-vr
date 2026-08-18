# Session 35 — Black-Left-Eye Bug ROOT-CAUSED AND FIXED; 2x Eye Resolution; F11 Recenter

Date: 2026-08-18 (same day as notes/34, continuing while the user waits ~9h for the
real-headset test window). Dev machine. Two headline items, one of them the oldest open
rendering bug in the project.

## 0. Summary (read this first)

1. **The notes/23 "black left eye" bug is FIXED and user-verified** — and it turned out to be
   far more important than "minor cosmetic menu issue": the affected screen is the **boot/title
   brain screen**, it reproduced in the user's headset last night ("left image completely black
   ... it was also black in VR yesterday, but gameplay was ok"), and it reproduced on the dev
   machine on demand. Root cause (§2, live-traced, not hypothesized): on this screen the engine
   re-binds the REAL backbuffer mid-eye-pass and draws the whole scene there, leaving eye 1's
   surface at its cleared black. The old notes/23 frustum/parallax hypothesis was wrong.
   Fix: three redirects during eye passes. Both halves user-confirmed rendering; gameplay
   user-regression-checked clean.
2. **VR eye buffers now render at 2x the game's resolution by default** (`PSYVR_RENDER_SCALE`,
   1x–4x, default 2x with submit enabled, 1x monitor-only): 1280×960/eye verified submitting on
   the dev machine (readback ~1.4ms/eye vs 0.31ms at 1x — ample 72Hz headroom); on the gaming
   PC this means 1600×1200/eye vs the recommended 2496×2688 — a large step toward native.
3. **F11 recenters head tracking** (re-captures the position+yaw reference).
4. New reusable diagnostics that made §1 possible: `PSYVR_DUMP_EYES=1` (periodic BMP dumps of
   both eye surfaces at composite time) and `PSYVR_TRACE_FRAME=1` (one full frame's exact
   SetRenderTarget/Clear/StretchRect/GetRenderTarget/DS/draw sequence with stereo phase, every
   ~5s).

## 1. How the bug was cornered

- `PSYVR_DUMP_EYES=1` gave the project's first ground truth: **eye1's surface is genuinely
  black at composite time; eye2's holds the full screen.** Not a composite bug, not a frustum
  bug — the content never lands in eye1.
- Isolation run WITHOUT the VR bridge (monitor-only, scale 1x): still black ⇒ VR bridge and all
  notes/34/35 features exonerated; long-standing base-stereo bug.
- `PSYVR_TRACE_FRAME=1` produced the smoking gun. Eye 1's pass, mid-pass, after the game's own
  render-to-texture work:
  ```
  TRACE: SetRenderTarget(0, 00BED5C0=BACKBUF) phase=1   <- the REAL backbuffer, during eye 1!
  TRACE: Clear(flags=0x6 ...) on RT=BACKBUF phase=1
  TRACE:   ... 73 draw calls (phase=1)                  <- the whole scene
  ```
  Eye 2's pass at the identical point binds `EYE2` and draws the same 73+13+11 calls — which is
  exactly why eye2 always had content.
- **Mechanism**: the engine records "the screen" RT pointer once per frame while the real
  backbuffer is still bound (post-present phase 0), restores that recorded pointer mid-pass
  after its RTT work, then re-records after its own (suppressed) internal Present — by which
  time BeforeEye2 has bound EYE2. So pass 1 draws to the real backbuffer (later overwritten by
  the composite: black EYE1 half + correct EYE2 half), pass 2 draws to EYE2 correctly.
- Gameplay's render path never does the mid-pass "restore screen" bind — hence gameplay was
  always fine, in 2D and in the headset.

## 2. The fix (three redirects, each found necessary by iterating on the eye dumps)

During an eye pass (`g_stereoPhase` = EYE1/EYE2), "the screen" MUST mean "that eye's surfaces":

1. `SetRenderTarget(0, realBackbuffer)` → active eye's render target. (First redirect alone:
   eye1 went from black to "dark blurred overlay + text" — progress but wrong.)
2. `StretchRect(src=realBackbuffer, …)` → src becomes the active eye's RT, src rects scaled by
   `g_eyeScale`. (The engine's bloom/post-process chain reads "the screen" back; it was reading
   the stale real backbuffer. After this: fresher but still dark — one asymmetry left.)
3. `SetDepthStencilSurface(realDS)` → active eye's private DS. (The engine also restores its
   recorded depth-stencil mid-pass; drawing eye1's late segments against the wrong depth buffer
   rejected the big center geometry. After this: **eye brightness parity, 48.5 vs 46** — and
   the dump shows eye1 fully rendered.)

All three are pointer-compares against our own known surfaces, active only during eye phases —
byte-inert for render paths that never touch the backbuffer mid-pass (gameplay) and for the
idle phase (AfterBoth's legitimate restore).

Verified at BOTH 1x and 2x scale, monitor-only AND with the full VR bridge + head tracking
running (final combined run: 1280×960/eye submitting, parity 47.8/47.1, clean exit).

## 3. 2x eye resolution details

- `SetupStereoSurfaces` creates eye RTs + private DS at `backbuffer * g_eyeScale` (same aspect —
  projection math untouched); monitor composite StretchRects down to the backbuffer halves; VR
  bridge buffers sized to the eye surfaces (`VRBridge_OnStereoSurfacesReady(eyeW, eyeH)`).
  Allocation failure at scale falls back to 1x rather than losing stereo.
- New `SetViewport` hook scales any game-set viewport during eye passes (D3D9's automatic
  full-RT viewport on SetRenderTarget covers the common case; this covers explicit ones).
- Cost at 2x measured: ReadbackChain ~1.4ms/eye (was ~0.31ms at 1x) — linear in pixels, well
  inside the 13.9ms 72Hz budget. GPU-side render cost on the RTX 5080 will be trivial.
- On the gaming PC (800×600 backbuffer): 1600×1200/eye. `PSYVR_RENDER_SCALE=3` (2400×1800)
  approaches the Quest 3's recommended 2496×2688 — worth trying if 2x looks good and performance
  holds; 32-bit address space is the main constraint to watch (~100MB extra at 2x, ~230MB at 3x).

## 4. What tonight's gaming-PC session should check (adds to notes/34 §4)

1. The brain/title screen should now render in BOTH eyes in the headset (last night's black
   left eye there is this same bug).
2. Sharpness: 1600×1200/eye vs last night's 800×600 — should be obviously better.
3. F11 to recenter if the world's origin feels wrong after putting the headset on.
4. Everything from notes/34 §4 (head tracking axes/scale, clean exit, quit handling).

## 5. Repo state

- Commit `a96353e` (proxy_d3d9.c + d3d9.dll): resolution + fix + recenter + diagnostics.
- Synced to dev-archive + modding-notes. Release repo: v0.1.1-alpha shipped EARLIER today
  (before this session's fix/resolution work); a v0.1.2-alpha carrying these is proposed,
  pending the user's go-ahead per standing rule.
