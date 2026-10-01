Supersedes: modding-notes/76-the-menu-void-is-the-camera-guard-saying-no.md (its conclusion, §"Answer" point 2-3 and §"What would fix it")

# 77 — correction: the menu void is NOT explained by the camera guard

**2026-10-01, dev PC, `/pd`, same session as 76. The game was not launched.**

Notes/76 concluded that the brain-menu void comes from the void fix's camera-turning part (`camfollow`) switching
itself off on menus via `AutoGetCamera()`. **That cannot be the cause.** The board's record of the 2026-09-10 wear
shows Tefa saw the menu void, and then "void was gone" in gameplay, **before** `camfollow 1` was sent ("after i wrote
IN, the whole world went…" is the moment it went on). During both observations the camera-turning fix was OFF; only
`PSYVR_FOV_SCALE=3.0` (set at launch) was active `[reported 2026-09-10]`. What 76 says about `AutoGetCamera()`
returning NULL on menus is still true of the code, but it is not what made the difference that day.

## What is left `[hypothesis]` each, and what separates them

1. **The menu's projection is not widened.** `PSYVR_FOV_SCALE` acts only at `BuildProjectionMatrix` entry
   (`0x6924D0`), which runs when the projection changes, not every frame (notes/07). If the brain menu's projection
   is built by another path (another caller of `D3DXMatrixPerspectiveFovRH`, or a matrix the menu keeps from
   before), the widening never reaches it, while gameplay rebuilds through `0x6924D0` on level load.
2. **The menu scene genuinely has nothing there.** The brain screen may be a brain in an empty background by
   design; widening shows more emptiness, which reads as void.
3. **Culling on the menu ignores the widened projection** (the menu scene culls with its own camera data).

**Separating observation (one flat run, no headset needed):** start with `PSYVR_FOV_SCALE=3.0` and look at the
brain menu on the monitor. Brain much smaller than with 1.0 = the widening reaches the menu (rules out 1); then a
log line from `Hook_BuildProjectionMatrix` with a counter shows whether it fired at all on the menu. If the brain is
the same size at 3.0 and 1.0, it is 1.

The `[PD]` fallback row that 76 queued is withdrawn: it would have fixed a cause that was not active.

## Narrowed the same session `[inferred-static 2026-10-01]`

`D3DXMatrixPerspectiveFovRH` (import stub `0x6ECD00`) has exactly **one** caller in the exe, `0x692520`, which is
inside the hooked `BuildProjectionMatrix` (`0x6924D0`). So every D3DX perspective projection the game builds passes
through the FOV widening, the menu's included. Possibility 1 now survives only as "the menu builds its projection by
hand, without D3DX" (less likely). Possibilities 2 (the scene is empty there by design) and 3 (its culling) lead.
The monitor check above still decides it.
