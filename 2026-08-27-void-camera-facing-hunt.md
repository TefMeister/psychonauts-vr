# 2026-08-27 — void work: what controls the camera, measured live (three results, two of them negatives)

The user's reason for prioritising this, in their own words: *"i'm scared to put
on the headset until the no-rendering black void situation is dealt with, not
joking."* A mod that is frightening to wear is not finished, so the void
outranks the dialogue/UI work started earlier the same day (parked, committed).

All of this was measurable for the first time because **automated gameplay entry
was solved earlier today** — `00-status.md` lists that as blocker #1 for the void
work, and the whole investigation had been stuck partly behind it.

Test rig: `camhold 1` freezes the camera so two captures differ **only** by the
variable under test. That removes the phase-matching problem that made notes/63's
void A/B ambiguous.

## Result 1 (negative, useful): widening the FOV does not expose the void

Swept `PSYVR_FOV_SCALE` 1.0 → 1.8 → 2.5 → 4.0 with the camera frozen, in the
Milkman Conspiracy (an open level, chosen by the user because it reproduces the
conditions).

| FOV scale | near-black pixels |
| --- | --- |
| 1.0 | 1.36 % |
| 1.8 | 1.73 % |
| 2.5 | 2.59 % |
| 4.0 | 2.87 % |

**The frame stays full of world at every setting** — increasingly stretched, but
not black. Cause: `PSYVR_FOV_SCALE` multiplies the game's *own* FOV argument in
place, so the game culls and submits geometry for the widened frustum too. It is
a real mitigation, and it can never be the cure — consistent with the standing
architectural note that a symmetric widen cannot cover 180° behind the player.

## Result 2 (positive): the engine culls relative to a camera position WE write

Pulled the camera 4000 units backwards along its own facing. The world still
rendered correctly from the new viewpoint (underside of the street, foliage,
coherent geometry) — no void.

So the engine is **selecting what to draw based on the camera object we are
editing**, not merely moving a view matrix across a pre-chosen set of geometry.
That is the property Candidate 1 ("feed HMD yaw into the game camera") depends
on, and it is now demonstrated rather than assumed.

## Result 3 (negative): `camera+0x20` is NOT the renderer's view direction

`SetCameraOrientation`'s impl writes three floats at `camera+0x20`. Live:

- Reading it returns a unit-length vector that looks exactly like a facing
  direction. (This also **corrected an earlier static mis-read** of the same
  field as a 3×3 matrix — `m[3..8]` turned out to be unrelated neighbouring
  fields, including a stray `104.0`.)
- Writing the **reversed** direction succeeded, and reading back confirmed our
  value was still there, untouched by the game.
- **The rendered view did not move at all.**

So the game neither overwrites that field nor renders from it. It is what the
Lua binding sets, but not what the renderer consumes.

## Also ruled out: the camera is not a look-at rig

The obvious explanation for "position matters, direction doesn't" was a look-at
camera deriving facing from `target − position`. Tested by sliding the camera
2500 units sideways: **Raz left the frame entirely and the view translated
without rotating.** A look-at rig would have kept him centred. So facing is a
stored value — just not the one at `+0x20`.

## Where that leaves Candidate 1

Still alive, and better supported than before — Result 2 shows the engine culls
to our camera. The missing piece is narrower than it was: **find the field the
renderer takes facing from.** Added a `dump <hexOffset> <count>` command (logs
the camera object as float and hex side by side) to go looking for another
unit-length vec3 or a rotation matrix near the known fields.

Known camera-object layout so far:

| offset | meaning | evidence |
| --- | --- | --- |
| `+0x08` | position (3 floats) | **used** — writes move the view |
| `+0x20` | a facing vector | **not used by the renderer** — write persists, view unchanged |
| `+0x530` | dirty flag, bit 0 | set after position writes |
| `+0x24`-ish | a `104.0` constant | unidentified; FOV or a distance? |

## Bonus: full menu navigation, unassisted

At the user's request, navigated the brain-vault main menu from scratch with
synthetic input only: walked Raz to the green door, `Ⓐ Load a Saved Game`,
selected the Raz bunk, moved to **slot 1 (The Milkman Conspiracy)** and loaded
it. Two corrections from the user during it, both worth recording: the doors are
easy to misidentify from a stereo capture (I read blue as green), and the action
key is **SPACE**, not ENTER — ENTER does nothing on the door prompt.

## FOUND: the real camera matrices — and why writing them still does not work (yet)

Dumped the camera object and diffed it across a camera turn. Two 4×4 matrices,
both of which moved while everything around them stayed put:

| offset | rows | translation | identification |
| --- | --- | --- | --- |
| `+0x50` | 0x50/0x60/0x70 | `+0x80` = (−4173, 21382, 24637) | **view matrix** (world→camera) |
| `+0x90` | 0x90/0xA0/0xB0 | `+0xC0` = (17047, 8817, −26707) | **camera world matrix** |

Three things confirm it rather than one suggestive number: the rotations are
**exact transposes** of each other (the view/world relationship); `+0xC0` sits
near the camera position while `+0x80` is in the `−R·p` form a view matrix
takes; and both changed on a camera turn. Rows are 4 floats (16-byte stride).

**World up is Z** — row 0 of the world matrix (the camera's *right* vector)
reads `Z = 0.0000` exactly, which only holds for a right vector kept
horizontal. Worth having on its own: session 51 lost time to a hardcoded +Y up
that was wrong for some levels.

Row identification, from the same dump: row0 = right, row1 = up
(`0.1856 0.1552 0.9703`, mostly +Z), row2 = **forward**
(`−0.7443 −0.6225 0.2419`).

### ❌ `camyaw` does not work — overwritten, not mis-aimed

Built `camyaw <deg>` (rotate the world matrix about Z) and applied it from
`CandB_BeforeEye1`, i.e. before the engine renders and culls.

- **Controlled A/B/A with the camera frozen:** yaw 0 → 9.95 % black, yaw 90 →
  10.12 %, yaw 0 again → 10.00 %, with matching column profiles. No visible
  change at all.
- **Diagnostic:** dumped `+0x90` with yaw 0 and with yaw 90 applied. The rows
  came back **bit-for-bit identical** (`3F2438D3 BF44629C …` both times).

So the write is being **completely overwritten inside the same frame**. This is
a *timing* failure, not a wrong-field failure — which is the opposite of the
`+0x20` case, where the write survived untouched and was simply never read.
Taken together those two results bracket the problem nicely.

**Most likely cause:** `CandB` is the engine's own camera-update tick (notes/59
established that, including its reentrancy guard). `BeforeEye1` runs *before*
`CandB`, so the camera update inside `CandB` recomputes the matrix over our
write. To land, the write has to happen **after** the camera update but
**before** the draw traversal — i.e. inside `CandB`, not around it.

### Live-scouting is limited by the same bug

Tried to find the exit from Boyd's house by flying the free camera. It moves
freely and passes through walls (position writes have no collision), but with
rotation not working the camera can only *translate* — it always looks the same
way. Scouting an unfamiliar interior while unable to turn is slow, and that
limitation is exactly the unfixed bug. Walking Raz normally works better for
now. **Did not reach the outdoor neighbourhood this session.**

Incidental: the figment tutorial popup blocks input until dismissed, and
`BACKSPACE` does not do it. One of `ESC`/`X`/`C` cleared it (not isolated
which — ESC appears to open the Journal, so likely `X` or `C` closed that).
Worth pinning down, since these popups will stall any unattended run.

## Next

1. `dump` the camera object around the known fields, hunting a second
   unit-length vec3 or a 3×3. That is now a single command, not a rebuild.
2. If facing is found and writable, Candidate 1 becomes testable directly:
   point the camera behind the player and see whether geometry appears.
3. If facing turns out to be derived downstream (e.g. recomputed into the MVP
   from a source further up), the fallback is Candidate 2 — widen only the cull
   test — which still needs the cull test itself, unfound after four sessions.
