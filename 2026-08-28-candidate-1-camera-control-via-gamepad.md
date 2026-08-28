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

---

# The clamp is real and cannot be beaten from outside (2026-08-28)

Three independent attempts, all negative, so this is now settled rather than
suspected.

## 1. Over-driving the axis does nothing

The axis range is −1000…+1000. Feeding values far beyond it:

| axis 5 | resulting yaw |
| --- | --- |
| 1,000 | −55.6° |
| 5,000 | −55.6° |
| 20,000 | −55.6° |
| 100,000 | −55.6° |

Identical to one decimal place. **The game clamps the axis value on input.**

## 2. The yaw scalars are derived copies, not drivers

`+0x154` and `+0x174` looked like the camera's yaw in radians, and moved by
~30° when the camera swung 34.4°. But forcing either of them 180° round, held
every frame:

- the write **stuck** — reads back our value, unchanged by the engine
- the camera **did not move at all**

That is the `camera+0x20` signature again: **written, never read.** They are
downstream copies of the camera's state, not inputs to it.

**Tally of camera fields that hold camera data but do not drive the render:**
`+0x20` (what `SetCameraOrientation` writes), `+0x154`, `+0x174`. And
`+0x90` (the real world matrix) *is* read, but is recomputed inside the frame
so a write cannot survive. Four fields, four different reasons it does not work.

## Conclusion: Candidate 1 is a PARTIAL cure

- **It genuinely works.** The camera turns, causally and repeatably, and the
  engine's culling follows — which no FOV widen can achieve.
- **It is bounded** by the game's own free-look clamp, ~100–150° of total range,
  enforced on the input value and not reachable by memory writes.
- **So a head turning fully behind cannot be followed.** Within the clamp there
  is no void; beyond it the game camera stops while the headset view continues,
  and the unrendered region returns.

## What is left to try, in order of promise

1. **Find the clamp constant in code, not data.** It is enforced somewhere in
   the camera update; a comparison against a float or a `fclamp`-style call.
   Static disassembly around the camera update is the honest next step — the
   data-side search has now failed four times and should stop.
2. **The buffered mouse path** (`GetDeviceData`) is known and untested as an
   input route. It is a different code path into the same camera and may not
   share the stick's clamp. Cheap to test, and the one remaining input avenue.
3. **Accept the clamp and scale.** Map head yaw into the available range: no
   void, but head and view disagree. A comfort trade-off that needs a headset to
   judge, so it belongs to the user, not to a monitor here.

## Also learned, from the user

**The Milkman Conspiracy interior has no exit door until a gated sequence is
done** — open the fridge, take the merit badge, use it, *then* the door appears.
An earlier session spent a long stretch hunting for a door that did not yet
exist. Recorded in the game profile so no future session repeats it: an exit
that is absent until a trigger fires looks exactly like a navigation failure.

---

# ⭐ THE CLAMP IS AVOIDABLE — turn the BODY, not the free-look (2026-08-28)

Five attempts to lift the free-look clamp failed (see below). The sixth attempt
was to stop attacking it and ask whether it needs beating at all. **It does not.**

## Measured

Turning Raz with movement input, sampling camera yaw:

```
-57.8 -> -140.5 -> -179.9 -> +137.8 -> -144.8 -> -147.4 -> -109.1 -> -158.9
```

**Total excursion 317.7°**, passing cleanly through the ±180° wrap. No
saturation, no clamp, no hard stop.

Against the free-look stick, measured the same way: **87.4° total**, pinned at
both ends (`axis <= 250` and `axis >= 750` both saturate).

## Why this matters

There are **two different rotations** in this game and only one of them is
clamped:

| rotation | mechanism | range |
| --- | --- | --- |
| **free-look** | right-stick offset from the follow position | **~87°, hard-clamped** |
| **body facing** | the character turns; the camera follows | **unlimited, 360°** |

Every previous attempt targeted the clamped one. The unclamped one was available
the whole time and drives the *same* camera — so the engine's culling follows it
just as it follows free-look.

## Consequence for the void

Candidate 1 was written up as a **partial** cure bounded at ~100–150°. That
bound was a property of the mechanism chosen, **not of the game**. Feeding head
yaw into *body facing* instead of free-look should rotate the rendered frustum
a full 360°, which is the coverage the void needs.

The trade-off is real and is a design decision, not a technical one: **the
character turns with the head.** In a third-person VR mod that may be entirely
acceptable — many VR games do exactly this — but it means the body no longer
faces independently of the view. That is a comfort/feel judgement for the user
in a headset, not something a monitor can settle.

## Failed approaches, so nobody repeats them

Five attempts to lift or bypass the free-look clamp, all negative:

1. **Over-drive the axis** — 1000 / 5000 / 20000 / 100000 all give identical
   yaw. The game clamps the axis value on input.
2. **Write `camera+0x154` / `+0x174`** (the yaw scalars) — writes stick,
   camera does not move. Derived copies, not drivers.
3. **Write `camera+0x90`** (the real world matrix) — recomputed inside the
   frame; the write comes back bit-for-bit identical.
4. **Write `camera+0x20`** (what `SetCameraOrientation` writes) — survives
   untouched, never read by the renderer.
5. **Hunt the clamp constant statically** — the measured 87.4° range suggested a
   ±45° clamp; the binary has six exact `45.0` constants, but every code
   reference is an `fld` (load), none is a compare, and the one sitting near
   the camera code (`0x004FD7DC`) merely stores 45.0 into a local for a call.
   No clamp comparison found.

The lesson is the one the debugging discipline already prescribes: after three
failures, question the approach rather than trying a fourth variant of it. That
took until the fifth here.

## Next

1. **Drive body facing from head yaw** rather than the free-look stick, then
   re-measure peak void with the synthetic sway. If the frustum follows a full
   360°, the void should approach zero rather than 18.5%.
2. Movement input is keyboard/analog, so the mapping is rate-based rather than
   absolute — head yaw would need converting into a turn *rate* with a
   settling term, which is a different control problem from setting an angle.
3. Then the headset judgement on whether body-follows-head feels right.
