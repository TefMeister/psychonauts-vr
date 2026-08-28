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
