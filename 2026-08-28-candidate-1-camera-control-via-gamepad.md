# 2026-08-28 — CANDIDATE 1 WORKS: the game camera can be turned on demand

The black void behind the player has been the project's top priority since
2026-08-23 (user: *"staring into that abyss gave me the heebiejeebies"*), and the
user will not wear the headset until it is dealt with. Four sessions of hunting
for the cull test failed. The other live candidate — **Candidate 1, "feed HMD yaw
into the game camera"** — was recorded as *unimplementable*.

**It is implementable, and it now works.**

## The result

Holding one gamepad axis swings the camera, repeatably and reversibly:

| axis 5 (`lRz`) | camera forward |
| --- | --- |
| rest | `+0.930, 0.082, −0.360` |
| **−1000** | `−0.354, 0.177, −0.918` |
| **+1000** | `+0.924, 0.085, −0.374` |
| **−1000** | `−0.355, 0.177, −0.918` |

Two full round trips returning to the same values. Causal, not drift.

**Why this matters more than a free camera:** the engine turns its *own* camera,
so **its culling follows for free**. That is the whole point of Candidate 1 — it
is a cure for the void rather than a mitigation, unlike widening the FOV, which
was measured on 2026-08-27 to be incapable of closing it.

## How it was found — measurement, after guessing failed repeatedly

Every wrong turn came from assuming; every step forward came from a probe.

**Dead ends, all now excluded with evidence:**

- `camera+0x20` — what `SetCameraOrientation` writes. A reversed write
  survived in memory *untouched* and the view never moved. Written, never read.
- `camera+0x90` (the real camera world matrix) — writes from `BeforeEye1`
  came back **bit-for-bit identical**. Overwritten inside the frame, because
  `CandB` *is* the engine's camera-update tick. Right field, wrong timing.
- **Mouse via `GetDeviceState`** — the game **never polls the mouse through it
  at all** (0 calls in 1198 frames). Injection there could not have worked at any
  magnitude.

**The probe that cracked it** counted every candidate input path at once:

```
GetDeviceData(buffered)=3598   WM_MOUSEMOVE=0   WM_INPUT(raw)=0
axes min  X=17 Y=33 Z=-1000 Rx=-1000 Ry=-1000 Rz=-1000
axes max  X=25 Y=41 Z=+1000 Rx=-1000 Ry=-1000 Rz=+1000
```

Two findings in one run:

1. **The mouse arrives via DirectInput's BUFFERED path** (`GetDeviceData`),
   not the immediate one — a path that had never been hooked. This fully
   explains months of "mouse injection does nothing".
2. **`lZ` (index 2) and `lRz` (index 5) are live stick axes**, swinging the
   full −1000…+1000 range, while `lRx`/`lRy` sit pinned at the minimum and
   are genuinely unmapped.

## The mechanism

`IDirectInputDevice8::GetDeviceState` is hooked; any `cb == 80` buffer is a
`DIJOYSTATE`, and named axes are overwritten with held values before the game
sees them. Absolute (a stick is a position, not a delta), off by default so real
input passes through, and matched by buffer size rather than a stored pointer so
it survives device re-creation.

Commands: `padaxis <0..5> <value|off>` and `padrelease`.

Resting values, for reference: `lX=57 lY=33 lZ=33 lRx=-1000 lRy=-1000 lRz=25`.

## ⚠️ Two process failures that cost most of the elapsed time

Worth more than the technical finding, because they were repeats:

1. **Running experiments without confirming the precondition.** Three camera
   tests were run with a pause menu open (the game ignores camera input there);
   one real-mouse test ran without verifying the foreground grab; and one pad
   test ran while the user was not moving the stick. **A screenshot was captured
   each time and not looked at** — only a derived brightness number, which
   happened to look plausible.
2. **Blocking the shell for the whole probe window.** The instruction "move the
   stick now" was printed *inside* a call that then blocked for 26 seconds, so it
   only reached the user after the window had closed. Arm the probe, return
   immediately, end the turn.

A wrong conclusion ("the pad path is dead") was published from failure 2 and had
to be formally withdrawn — see dev-archive
`recon/2026-08-28-correction-pad-probe-invalid.md`.

## Next

1. **Map the axes to yaw/pitch** — determine which of `lZ`/`lRz` is which,
   and the value-to-angle relationship, so a head pose can drive them.
2. **Then the actual void test**: turn the camera progressively behind the player
   and see whether geometry appears or black does. This is now a direct
   experiment rather than an inference.
3. **The mouse path is also now known** (`GetDeviceData`) if finer control than
   the stick's granularity is wanted later.

---

# Axis mapping and the CLAMP (2026-08-28, same session)

## The mapping

| axis | index | controls |
| --- | --- | --- |
| `lZ` | 2 | **pitch** — `+1000` drives the forward vector's Z to **+0.855**, i.e. looking steeply up |
| `lRz` | 5 | **yaw** — sweeps roughly 97 degrees across `0 .. +1000` |
| `lX` / `lY` | 0 / 1 | small jitter only (17..25, 33..41) — not the camera |
| `lRx` / `lRy` | 3 / 4 | pinned at `-1000` — genuinely unmapped |

Yaw sweep measured on axis 5 (world up is Z, so heading is `atan2(y, x)`):

| axis 5 | yaw |
| --- | --- |
| −1000 / −500 / 0 | −148.2° (all identical — parked against a limit) |
| +500 | −107.9° |
| +1000 | −51.5° |

## ⚠️ The axis is an ABSOLUTE CLAMPED OFFSET, not a rotation rate

Held at `padaxis 5 1000` for **12 seconds**, sampled four times: the camera sat
at **−82.2° the entire time, byte-identical**. A rate input would have kept
orbiting; this does not.

**This bounds what Candidate 1 can achieve.** Feeding head yaw into the stick
rotates the game camera — with culling following, which is the whole point — but
**only within the game's own free-look clamp**, measured at roughly 100–150° of
total range depending on the axis combination. It cannot follow a head that
turns fully behind the player.

So the honest position on the void:

- **Within the clamp**: the game camera genuinely turns and culls correctly.
  This is real, and better than any FOV widen, which was measured on 2026-08-27
  to be structurally incapable of closing the void.
- **Beyond the clamp**: the game camera stops while the headset view keeps going,
  so the unrendered region returns. Candidate 1 is a **partial** cure, not a
  complete one, unless a way past the clamp is found.

## No void found within reach

At maximum yaw the frame renders normally — trees, buildings, scenery. Measured
black went 0.72% → 4.0% → 6.37% across the sweep, but inspecting the captures
shows that is **Raz's own body and shadowed scenery**, not unrendered space.
**The void was not reproduced within the reachable range**, which is consistent
with it living beyond the clamp, where the headset can look but the game camera
cannot follow.

## Next

1. **Find whether the clamp can be lifted.** It is presumably a value the camera
   code compares against; if it is a float in the camera object, the existing
   `dump` command can hunt for it near the known fields. That would turn a
   partial cure into a complete one.
2. **Failing that, measure exactly where the clamp sits** in degrees, then decide
   whether head yaw should be scaled into that range (comfortable, no void, but
   head and view disagree) or mapped 1:1 (correct, but void beyond the clamp).
   That is a design decision with a comfort trade-off and belongs to the user.
3. The **buffered mouse path** (`GetDeviceData`) is now known and is untested
   as an input route — worth checking whether it is subject to the same clamp,
   since it is a different code path into the same camera.
