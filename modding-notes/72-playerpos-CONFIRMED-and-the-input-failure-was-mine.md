# 72 — `playerpos` CONFIRMED both ways; and the input failure was my own error

Supersedes: 71-playerpos-is-camera-independent-and-no-synthetic-input-reached-the-game.md §2, §3

**2026-09-02, dev PC, FLAT (monitor), game launched by the user, live-supervised.**
Same session as notes/71, after the user confirmed Raz was in normal gameplay in the parking lot.

---

## 1. THE TEST IS COMPLETE AND IT PASSES

`playerpos` is Raz's position, isolated in **both** directions.
`[verified-live 2026-09-02, n=2]`

**Direction 1 — camera moves, player must not** (notes/71 §1, unchanged):
`cammove 1000 0 0` with `camhold 1` moved `campos` exactly +1000 in X;
`playerpos` did not change by 0.01 in any axis.

**Direction 2 — player moves, camera frozen** (new, and the half notes/71 could not run):

| | `campos` (held) | `playerpos` |
| --- | --- | --- |
| before | `-20452.23 442.69 17770.71` | `-20911.18 139.35 17217.61` |
| after LEFT arrow, 1.5 s | `-20452.23 442.69 17770.71` — **byte-identical** | `-21115.10 84.72 18090.04` |
| delta | **0, 0, 0** | **-203.92, -54.63, +872.43** |

The camera did not move at all and `playerpos` moved ~900 units. Nothing else can account for
that.

**Corroborating, from the free-camera run just before it:** with `camhold 0`, an UP arrow held
2 s moved `playerpos` by `(-830.13, +26.31, -1000.06)` while `campos` moved by
`(-833.29, -86.94, -1004.41)` — **the X and Z deltas agree to within 0.4% over a ~1300-unit
walk**, i.e. the game's own camera followed Raz by the same world displacement our chain
reported. `[measured 2026-09-02]`

**Consequence:** the notes/69 → notes/70 chain
`[[[0x0078BC20]+0x818C]+0x10]+0x40` delivers a usable, world-tracking player position. The FP
work can build on it.

**Still not fully settled:** whether the value is strictly world-space or local to a parent that
happens to be static. A static non-identity parent would look identical to this test. It matters
only where the parent moves — Raz on a moving platform, a lift, a vehicle — so the `+0xB8`
question stays open but is **no longer blocking**, and its framing changes from "is the chain
right" to "does the parent ever move".

## 2. ⚠️ CORRECTION — notes/71 §2 and §3 were wrong, and the cause was my own error

notes/71 recorded that **no synthetic input reached the game by any route**, and offered two
explanations: (a) the session was not in walkable gameplay, or (b) our own notes/65
`GetForegroundWindow` IAT patch was suppressing DirectInput re-acquisition.

**Both are wrong. Input works fine.** `[disproved 2026-09-02]` The failure was mine, in two
compounding ways:

1. **I pressed the wrong keys.** I sent `W`. **Psychonauts moves on the ARROW KEYS** — notes/38's
   own live proof was the RIGHT arrow walking Raz. `W` is very likely unbound, so a perfectly
   delivered keystroke did nothing and looked exactly like a delivery failure.
2. **I used the wrong scancode encoding for the arrow.** I sent `0x48` + extended. The repo's
   proven helper passes **DIK codes directly** — `UP = 0xC8`, `LEFT = 0xCB`, `RIGHT = 0xCD`, each
   with the extended flag. Sending `0xC8` worked first time.

The mouse sweep producing no rotation is consistent with the game not mapping raw mouse look the
way I assumed, and is **not** evidence of an input blocker either.

**The self-inflicted part worth remembering:** notes/71 §3 named our own bug-fix as a suspect and
wrote a plausible mechanism for it. Had nobody checked, that would have sent a future session
hunting a defect in working code — and the auto-pause fix is exactly the kind of load-bearing
thing someone might have "fixed" back into a bug. **The estate's own rule caught this**: the user
said "you have definitely moved Raz before", which is a positive control I should have looked for
in the repo before theorising. `tools/input/send_key.ps1` and notes/38 were sitting there the
whole time.

**This is also a textbook case of the standing input rule** (*build several input routes and
measure which one the game obeys against a no-input control*): I tried three variations inside a
single API family — `SendInput` scancode, `SendInput` extended scancode, `SendInput` mouse — and
mistook "one API, three parameter sets" for "several routes". They were never independent.

## 3. The working input recipe, for the next session

Use the repo's own helper — it already encodes every lesson:

```
powershell -File dev-archive/tools/input/send_key.ps1 -Scan 0xC8 -Extended -HoldMs 2000
```

- **DIK scan codes, passed directly**: `UP 0xC8 · DOWN 0xD0 · LEFT 0xCB · RIGHT 0xCD`
  (all `-Extended`), `SPACE 0x39`, `ENTER 0x1C`, `ESC 0x01`.
- Movement is the **arrow keys**.
- The helper's `AllowSetForegroundWindow` + 5-attempt retry + foreground verification matters —
  the tool harness's own console contends for foreground (notes/53).
- Warn the user hands-off during a send.

## 4. Unchanged from notes/71

§4 stands: the queued "free" eye-height measurement does not work as written — camera-minus-player
is a **third-person camera height**, not an eye height. `GetBoneWorldPosition(headJA_1)` minus
`playerpos` is the real route and is already decoded. This run reinforces it: Raz's Y moved from
113.04 → 139.35 → 84.72 across the walk, so the parking lot is **not** level ground and any
height taken there would have been wrong twice over.

## 5. Housekeeping

- `camhold` restored to **0**; the camera follows normally. Game left running per standing
  preference.
- Nothing rebuilt, nothing deployed, nothing written to the game folder.
