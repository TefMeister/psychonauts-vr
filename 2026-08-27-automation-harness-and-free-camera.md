# 2026-08-27 — external automation harness + FREE CAMERA, verified live in a real level

Psychonauts can now be driven from outside the process. Commands go in a text
file next to `Psychonauts.exe`; the proxy runs them. **Verified live, first
try**: jumped from the title screen straight into Sasha's Lab, took the camera
off the engine, and flew it — with the rendered image changing to match.

The goal this serves, in the user's words: *"you can freely move around the game
world, ideally i launch the game and you go through the menus… as you please."*

## The finding that unlocked it: the camera needs no Lua

`ENGINE-DOSSIER.md` §9 framed the camera bindings as callable *"once in-process
Lua execution exists"*, which had been gating this work behind the unverified
Lua exec at `0x6B0C00`. **That framing was wrong**, and static disassembly of
the shipped exe (file only — game never launched, no debugger attached) shows
why. Every Lua binding is two layers:

- **shim** (`SetCameraPosition` @ `0x00568FA0`) — `int f(lua_State* L)`, does a
  stack switch via `0x0078CBCC`/`0x0078CBD0`, calls the impl.
- **impl** (`0x00569000`) — pulls args off the Lua stack, then does **plain
  engine work**.

Strip the marshalling half and what remains is:

```c
void *mgr = *(void **)0x0078BC20;      /* same singleton SetPendingLevel uses */
if (!Guard(mgr)) return;               /* __thiscall BOOL  @ 0x00504220 */
void *cam = GetCamera(mgr);            /* __thiscall void* @ 0x004FA5A0 */
*(float *)((char *)cam + 0x08) = x;
*(float *)((char *)cam + 0x0C) = y;
*(float *)((char *)cam + 0x10) = z;
*((unsigned char *)cam + 0x530) |= 1;  /* dirty flag */
```

Three float stores and a bit. **No interpreter, no `0x6B0C00`.** Full
derivation in dev-archive `recon/2026-08-27-camera-control-without-lua/`.

**This generalises**: all 1129 entries in `tools/lua-bindings.def` have the same
two-layer shape. Any binding whose real work is field writes or a `__thiscall`
on the singleton is reachable this way. `GetBoneWorldPosition`
(`0x005B1630`/`0x005B1690`) is the obvious next one — §11 records the FP and
orientation investigations as blocked waiting for exactly that.

## The harness (notes/67)

`PSYVR_AUTOMATION=1` (default off). Commands are appended to
`psyvr_automation_cmds.txt` next to the exe; the proxy reads, runs, truncates.
Results and camera telemetry go to the normal proxy log.

| command | effect |
| --- | --- |
| `status` | log current camera position |
| `level <CODE>` | `SetPendingLevel` — jump straight to a level |
| `campos <x> <y> <z>` | set camera position absolutely |
| `cammove <dx> <dy> <dz>` | move relative to current |
| `camhold <0\|1>` | re-apply the target every frame |
| `flag <id> <0\|1>` | poke a debug-menu flag byte |

Helper: `tools/psyvr-auto.ps1 -Send 'level CAJA' -Tail 20`. Launcher:
`Launch-Psychonauts-Automation.bat` (sets `PSYVR_AUTOMATION=1` +
`PSYVR_SUPPRESS_AUTOPAUSE=1`; without the latter the game pauses when unfocused
and the poll stalls).

## Both lessons from the XIII crash, carried over deliberately

Same day, the XIII harness GPF'd the first time a command arrived, because its
queue drained from a camera hook that turns out to be called from inside the
engine's `Draw`. Psychonauts' only per-frame hook (`CandB_AfterBoth`) is **also**
on the render path, so:

1. **The harness only does work already proven safe from that exact site**:
   `SetPendingLevel`'s async staging (F12 has done this since notes/59),
   debug-flag byte pokes (notes/62, 64), and plain camera field writes. It
   deliberately does **not** call the Lua interpreter or any command dispatcher.
   Doing that needs a game-logic tick site, which nobody has found in this engine
   yet — that is the prerequisite for any future `lua <chunk>` command, not an
   optional extra.
2. **Log BEGIN before the call, END after.** XIII logged only on completion, so
   its crash left no record of which command died and it had to be inferred from
   telemetry stopping and CPU hitting zero. `LogLine` already opens/appends/closes
   per call, so every line is on disk before the next runs.

## What the live test proved

1. `status` → `campos 2549.95 856.33 277.44` — guard passes, `GetCamera` returns
   a valid object, offsets correct. (~2549 is **menu space**; real level coords
   are tens of thousands, per the `enter_gameplay.ps1` arrival check.)
2. `cammove 0 0 300` → 277.44 → **577.44**, held across 4 telemetry samples.
3. `level CAJA` → **jumped title screen → Sasha's Lab directly**, no menu walk.
   Coordinates went world-space and were moving under engine control
   (1562 → 1258 → 954 → 650 in X over three seconds).
4. `camhold 1` → camera **froze** at `679.00 1369.00 -1684.00`, pinned across 7
   samples while the engine tried to keep moving it.
5. Three-leg flight, each landing exactly: `+400 Y` → 1769 ✓, `+500 X` → 1179 ✓,
   `+600 Z` → -1084 ✓.
6. **Screen captures before/after a `+1500 Y` move show the rendered image
   change** — so this is real camera control, not just memory that happens to
   read back.

The capture also confirmed the jump landed correctly: Sasha's dialogue
("What are you doing here at Whispering Rock?") is on screen, in the mod's
side-by-side stereo view.

## This directly retires a known blocker

`enter_gameplay.ps1` (the 13-keystroke title→menu→CONTINUE walk) is recorded in
§10 as **deterministically broken** — notes/54 found 3 consecutive runs landing
at the *exact same wrong coordinates*, so retries can never fix it. The dossier
names the robust fix as "finding an engine-level level/save-jump call instead of
walking". `level <CODE>` **is** that, now driveable externally, and it worked on
the first attempt.

## Open / honest caveats

- **Orientation is untested.** Only position was exercised. `camera+0x20` holds
  a 3×3 built by a converter at `0x0069F400`; matrix order, euler order and
  units are all still unknown. Free *look* is not done — only free *movement*.
- **`camhold 0` did not hand the camera back** — it stayed where it was put.
  That does not prove the engine yielded, because the test ran during a dialogue
  whose camera is static anyway. Whether a held camera fights normal gameplay
  movement is untested.
- The level jump landed **mid-dialogue** — jumping into a level does not
  necessarily produce a clean, controllable gameplay state.
- Menu *navigation* still relies on `SendInput` (§10), untouched by this work.
  The harness sidesteps menus rather than driving them.

## Next

1. Decode `SetCameraOrientation`'s converter for free look — completes the free
   camera and unblocks §11's orientation-source problem.
2. `GetBoneWorldPosition` via the same two-layer trick, for the FP work.
3. A game-logic tick site, if a general `lua <chunk>` command is ever wanted.
