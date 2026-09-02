# 71 — `playerpos` is camera-independent; and no synthetic input reached the game

**2026-09-02, dev PC, FLAT (monitor), game launched by the user via
`Launch-Psychonauts-Automation.bat`. Async mode ("all yours").**
First live run of the notes/69 build. Nothing was rebuilt or deployed this session.

---

## 1. The result that IS established

**`playerpos` does not move with the camera.** `[verified-live 2026-09-02, n=1]`

| step | `campos` | `playerpos` |
| --- | --- | --- |
| baseline | `-20608.05 440.81 18698.02` | `-20081.05 113.04 18217.67` |
| `camhold 1` | `-20608.1 440.8 18698.0` | `-20081.05 113.04 18217.67` |
| `cammove 1000 0 0` | `-19608.05 440.81 18698.02` | `-20081.05 113.04 18217.67` |

The camera moved **exactly +1000 in X** and `playerpos` did not change **by 0.01 in any axis**.
That rules out the reading in which the chain is a camera-derived quantity — the possibility
notes/69 and notes/70 were most worried about.

Supporting, weaker: `playerpos` sits close to the render view's look-at point
(`BVM cache SET … at=(-20085.62, 109.35, 18207.59)`) — about 4 units in X and Y and 11 in Z —
which is where Raz should be. `[measured 2026-09-02, n=1]` Plausibility, not proof.

**Y is the up axis** — the view's `eye` and `at` differ only in Y. `[measured 2026-09-02]`

## 2. The result that is NOT established, and why it is not a negative

**Whether `playerpos` moves WITH Raz is still untested.** `playerpos` was byte-identical across
three minutes and every sample, but **Raz never moved**, so that constancy is not evidence.

No synthetic input reached the game, by any route tried:

| route | result |
| --- | --- |
| `SendInput` keyboard, **scancode** `W` (0x11), held 1.5 s and 2 s | no movement |
| `SendInput` keyboard, **extended scancode** Up arrow (0x48), held 2 s | no movement |
| `SendInput` **mouse** relative move, 20 × 60 px sweep | view `right` vector unchanged at exactly `(-1.0000, 0.0000, 0.0000)` |

The window was confirmed **foreground, visible, not minimized, responding**, with
`GetForegroundWindow() == MainWindowHandle` checked immediately before sending. The game was
confirmed **live**: `Present()` frame counter advancing ~120/s throughout, and the view showed a
small continuous idle sway (`eye` Y oscillating 261.7–266.0).

**This contradicts the board's standing claim that "synthetic keyboard reaches gameplay but menus
need a gamepad".** That claim should be treated as `[disproved 2026-09-02]` *for this
configuration* until re-established.

## 3. Two competing explanations — NOT resolved, do not act on either yet

**(a) The game was not in walkable gameplay.** `[hypothesis]` The render view has `eye` and `at`
sharing identical X and Z and differing only in Y — **the camera is directly overhead looking
straight down** — with `right` pinned at exactly `(-1, 0, 0)`. That is not how this game frames
normal third-person play, where the camera sits behind Raz. If the session was sitting in a menu,
a pause, or a scripted camera, then *nothing* would move and no input route would appear to work,
and there is no input bug at all.

**(b) Our own auto-pause fix suppresses input acquisition.** `[hypothesis]` notes/65 patches the
exe's IAT entry for `user32!GetForegroundWindow` to always return the game's own hwnd. If the
game's DirectInput layer consults that same call to decide when to acquire or re-acquire the
device, the hook would leave it never re-acquiring after a real focus change — and focus **did**
change repeatedly this session (terminal → game → terminal). This is the shape the claim-hygiene
rule warns about: a fix that removes one symptom and creates another that looks unrelated.

**The discriminator, one question to the user:** *what was on screen?* Normal gameplay with Raz
standing in a parking lot argues for (b). A menu, a pause dialog, or a cutscene argues for (a).

**If (b) survives that:** relaunch with `PSYVR_SUPPRESS_AUTOPAUSE` **unset** and repeat the mouse
sweep. View rotates ⇒ the auto-pause hook is the cause. View still frozen ⇒ neither hypothesis
holds and the input route needs its own investigation.

## 4. A queued test that does not work as written — correction

The board records, as free with this run:

> *"on flat ground the up-axis difference between them IS the engine's own eye height, in its own
> units — measured, not converted."*

**That premise does not hold.** The difference between `campos` and `playerpos` is
**327.8 units** in Y, and between the render `eye` and `playerpos` about **149** — but both are
the height of a **third-person** camera, which is not at Raz's eye. Neither number is an eye
height, and recording either as one would have baked a wrong constant into the FP work at exactly
the point where `fpheight`'s default of 60 is already flagged as a guess.

**The better route already exists and is decoded.** notes/69 records
`GetBoneWorldPosition` as fully decoded, returning **six** values (position *and* euler), with
Raz's real rig bone names found in `.rdata` — `headJA_1` at `0x00703F94`. **Head-bone world
position minus `playerpos` is the eye height**, measured in the engine's own units, with no
third-person camera in the path and no unit-scale conversion. That is a static-plus-one-call job,
not a guess.

## 5. What to do next, in order

1. **Ask the user what was on screen** — settles §3 in one sentence, costs nothing.
2. If normal gameplay: **relaunch without `PSYVR_SUPPRESS_AUTOPAUSE`**, repeat the mouse sweep.
3. Either way, the "moves with Raz" half needs **one human keypress**: walk Raz a few steps with
   the camera held (`camhold 1`), then `playerpos`. With the camera frozen, any change is Raz.
4. Replace the eye-height item with the `GetBoneWorldPosition(headJA_1)` route from §4.

## 6. Housekeeping

- `camhold` was left at **0** (released) at the end of the session; the camera follows normally.
- Nothing was written to the game folder. No rebuild, no redeploy. The command file
  `psyvr_automation_cmds.txt` is consumed and empty, as designed.
- Log for this session: `%TEMP%\psychonautsvr_proxy.log`.
