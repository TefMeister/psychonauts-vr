# The black void: SOLVED — the camera transform is at `+0x150`, and culling follows it

**Date:** 2026-08-28 · **Session:** modding (dev PC, monitor) · **Status:** ✅ solved, live-verified

## Result

Rotating the camera through **90°** renders the scene **fully** — measured
**2.58% near-black versus 3.80% at rest.** Turning the camera yields *less* black
than not turning it. Culling follows the camera completely, so there is no void.

| yaw | 0 | 5 | 30 | 60 | 90 | 180 |
| --- | --- | --- | --- | --- | --- | --- |
| near-black | 3.80% | 2.89% | 2.83% | 2.61% | **2.58%** | 2.44% |

Images visually confirmed clean at 5, 30 and 90. Reversible (3.80 → 2.58 → 3.88).
Bit-stable: three dumps 1.5 s apart identical. 180° points into terrain from a low
chase-cam and looks degenerate for that reason, not from a transform error.

Command: `cambasisyaw <deg>`. Durable detail in `ENGINE-DOSSIER.md` §9b.

## What it is

A full 4×4 at `camera+0x150`, four rows at stride `0x10`, **columns** significant:
c0 = right (scaled 1.538), c1 = up (scaled 2.052), c2 = forward (unit), c3 =
forward-prev (unit), row 3 = translation. `|c1|/|c0| = 1.334` = the 4:3 aspect,
so c0/c1 carry projection scaling. Row 3 = `dot(O, c_i)` with **O = −camera
position**, recovered by *solving* the system, not assumed.

## Three things all had to be right

1. **Snapshot and write absolute.** Rotating in place each frame compounds —
   while the camera is stationary the engine does not rewrite this matrix, so a
   15° hold became a spin (c0.z drifted 0.5160 → 0.1235 in 1.5 s).
2. **Rotate all four columns.** c2/c3 are a matched pair; divergence breaks it.
3. **Row 3 must follow the rotation.** Leaving the translation on the old axes
   produces an error that *grows with angle* — clean at 2–5°, sheared by 8–10°,
   wrecked at 15°+.

## The path here, including the wrong turns

Worth recording because three plausible theories died and each cost a rebuild:

- **"It's a timing problem, land the write inside `CandB`"** (the 08-27
  conclusion) — **wrong.** Probing at BeforeEye1/BeforeEye2/AfterBoth showed the
  write surviving the whole frame. The 08-27 dump was taken *before* the write in
  both the yaw-0 and yaw-90 runs, so it showed the engine's value either way.
  Wrong field, not wrong timing.
- **"`+0x50` (the view matrix) is the real target"** — **wrong.** A held `0.0`
  read back as `0.0` at end of frame with the picture unchanged. All three
  matrices (`+0x20`, `+0x50`, `+0x90`) are derived outputs nothing reads.
- **"The columns are scaled, so rotating them shears"** — **wrong.** Preserving
  each column's magnitude changed nothing at all.

Two of the failures were **my own bugs masquerading as findings**: the
accumulating spin, and letting c2/c3 diverge. Both produced broken images that
looked like evidence about the engine and were not.

**Method note.** What finally worked was arithmetic on the actual numbers rather
than another hypothesis: solving `M·O = t` for the origin, which returned
−campos to within the camera's own drift and identified the structure as a view
matrix immediately.

## Bonus findings

- **Menus need a gamepad.** Synthetic keyboard reaches gameplay but the
  title/credits screens do not consume it (matches the notes/15–18 dead end). An
  unattended run cannot get itself from launch into a save — a human must reach
  gameplay first.
- **The game has a built-in first-person camera on `Z`** (user, 2026-08-28),
  meant for looking around while standing still. Unmined, and a promising
  known-good stimulus: drive that mode and diff the camera object to find any
  remaining orientation state.

## Next

The project's real goal is **first-person gameplay with third-person dialogue and
cutscenes.** Position (`+0x08`) and now orientation (`+0x150`) are both writable,
which is both halves of the camera control FP needs. The remaining pieces are
anchoring the camera to Raz's head and switching back to the engine camera during
dialogue and cutscenes.
