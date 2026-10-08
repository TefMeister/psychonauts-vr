# 78. Head-follow gives the engine its camera back every frame, and counts which fault it was

`/pd`, dev PC, 2026-10-08. **The game was not launched, and nothing here has been run in the game.**

## The question

On 2026-09-10 Tefa wore the head-follow build (`camfollow 1`): *"it stretches the game world around and is quite
shaky. after i wrote IN, the whole world went all sorts of crazy glitchy"* `[verified-live 2026-09-10, n=1]`. The
2026-08-28 desk check had passed, but with a still camera, and the dossier already says the engine does not rewrite
`camera+0x150` while the camera is still. So the desk check never exercised the case the wear hit.

## Two candidate faults

`camfollow` writes the engine's basis, turned by head yaw, at BeforeEye1, and leaves it there.

- **FEEDBACK** `[hypothesis]`: the engine's next camera update starts from what is in `+0x150`, i.e. from our turned
  basis. `PsyBasisFollowHead` sees "the engine moved", snapshots that already-turned basis and turns it again. The
  head yaw is added once per frame and the view swings far past the head.
- **OVERWRITE** `[hypothesis]`: the engine rewrites `+0x150` inside the first CandB, after our write, from its own
  angles. The head turn is lost on every frame where the camera moves.

Which step runs last: our write is at BeforeEye1; the engine's update runs inside CandB and again before the next
BeforeEye1. Restoring the engine's basis at AfterBoth changes what the next update reads, so it cures FEEDBACK.
It cannot cure OVERWRITE.

⚠️ **Withdrawn as evidence:** the 09-10 log showed `CAMBASIS snapshot: origin solved` three times in the camfollow
window and never in the 30 s before. That proves nothing: the snapshot only runs while camfollow is on.

## What was built

Proxy `76630f3641c8` (`dev-archive/tools/proxy-d3d9`), installed on the dev PC. New file
`px_07b_camfollow_restore.c.inc`, hooked in at BeforeEye1 (measure), BeforeEye2 (check) and AfterBoth (restore):

- **`camfollowrestore 1`** (the default): at AfterBoth, if the basis is still ours, write the engine's own basis and
  translation row back. `camfollowrestore 0` = the old behaviour, for comparison.
- **A stats line every 120 frames** while camfollow is on:
  `CAMFOLLOW stats (restore R): ... engine rewrote the camera between frames N times; its own camera turned X deg
  TOWARD the head's side, Y away; rewritten during eye 1's pass L; restored K, engine had already replaced it M`.

A one-frame check cannot separate the faults: once FEEDBACK has settled, a frame looks exactly like an engine keeping
its own angle. The tell is the drift: with the head held turned, the engine's own camera keeps turning toward the
head's side. That is why the line sums turn in degrees.

## Tests

`tests/build_camfollow_test.sh` cuts the basis code out of `px_07` exactly as it ships, compiles it with `px_07b`,
and runs it against three pretend engines (easing from memory = FEEDBACK, rewriting from its own angle = OWN, and
rewriting after our write = OVERWRITE), with the head turned left and right and the up column pointing up and
down. `[verified-numerically 2026-10-08, n=16 cases]`:

- FEEDBACK, restore off: view drawn at 162.6 deg for a 20 deg head turn (the fault reproduces); drift toward the head
  1222 deg, away 0.
- FEEDBACK, restore on: view at exactly 20 deg; drift 0.
- OWN: 20 deg, drift 0, either way.
- OVERWRITE: head turn lost (0 deg); "rewritten during eye 1's pass" and "already replaced" both count every frame.

These are models. Which one Psychonauts is, is what the next run decides.

## The next run (FLAT, monitor, no headset)

`Launch-Psychonauts-FollowTest.bat` (synthetic ±60 deg head sway). In gameplay, write to the command file:
`camfollowrestore 0`, `camfollow 1`, walk Raz a few steps and stand still for 20 s, then `camfollowrestore 1` for
another 20 s. Read `%TEMP%\psychonautsvr_proxy.log`:

| stats lines show | means | next |
| --- | --- | --- |
| restore 0: large TOWARD, view spins; restore 1: TOWARD ~0, calm | FEEDBACK, and the restore cures it | re-wear (VR USER) |
| "rewritten during eye 1's pass" climbing in both | OVERWRITE | move the write later (BeforeEye2 can only fix eye 2) or find the angles the engine keeps |
| both ~0 and the picture is calm in both | neither fault on a monitor | the wear's trouble came from elsewhere (FOV 3.0, register-6 pitch/roll) |

## Also done

- The dev PC's installed `d3d9.dll` (`a7b6ad976e00`) was identified as the 2026-09-01 build (commit `43173a1`, same
  minute, has camfollow and fpcam but no "THUNK" forwarding table) `[inferred-static 2026-10-08]`. It and eight old
  backups were taken out of the game folder into `dev-archive/deployed-history/2026-10-08-dev-pc-game-folder/`
  (with a hash manifest). The duplicate `openvr_api.dll` backup (identical to the installed one) went to the local
  archive. The new build forwards all seventeen d3d9 exports.
