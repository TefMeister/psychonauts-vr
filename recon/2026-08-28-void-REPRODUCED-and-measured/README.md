# 2026-08-28 — THE VOID IS REPRODUCED, CHARACTERISED, AND MEASURED

The black-void-behind-player bug has been the project's top priority since
2026-08-23 and the reason the user will not wear the headset. It has now been
**reproduced on a monitor, proven to be a true rendering gap rather than dark
terrain, and measured against the one mitigation that exists.**

## How it was reproduced

Not with the game camera — with the mod's **own synthesized head sway**
(`PSYVR_FAKE_POSE=1` + `PSYVR_FIRST_PERSON=1`, amplitude
`PSYVR_FAKE_POSE_YAW_DEG=170`). This distinction is the key insight of the
session:

- Turning the **game camera** (via injected gamepad axes) brings the engine's
  **culling with it**, so no void appears — but it is clamped to ~100–150°.
- The **synthetic sway rotates the VIEW** past what the game ever rendered,
  which goes well beyond that clamp. **That is where the void lives.**

This explains why the same session found "no void" via the gamepad route and a
92% void via the sway, an hour apart. Both results are correct; they are
different sides of the same boundary.

The user supplied the crucial recollection — *"the camera was swinging from left
to right and the void was clearly visible"* — which identified the sway as the
mechanism.

## It is a TRUE VOID, not unlit terrain

`notes/63` raised a serious caveat: the black shape it captured closely matched
a real unlit rock face, so the void might have been dark scenery all along.
**That caveat is now resolved — it is a genuine rendering gap.**

Two images settle it:

- **`peak-void-fov1.0-92pct-black.png`** — at maximum sway, **91.8%** of the
  frame is uniform black with only the screen-space HUD floating in it. The
  world is entirely absent. Unlit terrain would still show variation; this shows
  none.
- **`void-BOUNDARY-hard-frustum-edge.png`** — the diagnostic image. Rendered
  world on one side, solid black on the other, separated by a **hard, straight
  edge**. Terrain does not produce razor-straight vertical boundaries. That edge
  is the frustum limit: exactly where the game stopped rendering.

Compare **`facing-forward-normal-1pct-black.png`** — the same scene facing
forward, 1.2% black. The void is not present at all until the view swings.

## Measured: how much the FOV widen actually helps

Sampled across a full sway cycle (14 frames, ~12.6 s) at each setting:

| `PSYVR_FOV_SCALE` | peak black | median black |
| --- | --- | --- |
| **1.0** | **91.76%** | 46.49% |
| 2.0 | 83.43% | 37.91% |
| **3.0** | **42.09%** | 30.90% |
| 4.0 | 44.56% | 37.52% |

**Findings:**

1. **Widening genuinely helps** — peak void more than halves, 92% → 42%.
2. **It plateaus and slightly reverses past 3.0.** 4.0 is no better than 3.0 and
   measurably worse on the median, so pushing the scale higher is not free.
   **~3.0 is the practical optimum**, against a shipped default of 1.8.
3. **It cannot eliminate the void.** Even at the optimum, 42% of the frame is
   still unrendered at peak swing — consistent with the standing architectural
   point that a symmetric frustum widen can never cover 180° behind the player.

## What this means for the fix

The two candidates are now both quantified, and they are complementary rather
than competing:

- **FOV widen** — cheap, already shipped, roughly halves the void, cannot close
  it. Raising the default from 1.8 toward 3.0 is a real improvement supported by
  data.
- **Candidate 1 (drive the game camera)** — closes the void completely *within*
  the game's free-look clamp, because the engine culls to wherever the camera
  points. Bounded at ~100–150°; beyond that the game camera stops and the void
  returns.

Neither alone is sufficient for a full 180° head turn. **Together they cover
much more than either does alone**, and the remaining exposure is the region
beyond the free-look clamp with the FOV widen as the only fallback there.

## Next

1. **Raise the shipped `PSYVR_FOV_SCALE` default toward 3.0** — supported by the
   table above, and it costs nothing to change.
2. **Combine both** — drive the game camera within the clamp *and* run a widened
   frustum, then re-measure peak void with the sway. That number is the honest
   answer to "how bad is it now".
3. **Lift the free-look clamp** if possible; four data-side attempts have failed,
   so the next honest attempt is static disassembly of the camera update.
4. **Then a headset check** — the monitor can measure void area, but only the
   user can judge whether the residual is comfortable.
