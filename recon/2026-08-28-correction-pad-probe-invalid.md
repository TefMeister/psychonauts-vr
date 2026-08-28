# CORRECTION (2026-08-28): the "gamepad axes never move" result was INVALID

The 2026-08-27 probe run that reported every DIJOYSTATE axis constant
(`min == max` on all six: X=57 Y=33 Z=33 Rx=-1000 Ry=-1000 Rz=25) was written
up as evidence that **"the pad injection path is dead too"**.

**That conclusion is withdrawn.** The user was not moving the right stick during
the probe window. The axes were constant because nothing was moving them.

## What the data actually shows

Those numbers are still useful - they are the **resting values** of the pad's
axes, which is exactly what read-before-write was meant to establish:

| axis | at rest |
| --- | --- |
| lX | 57 |
| lY | 33 |
| lZ | 33 |
| lRx | -1000 |
| lRy | -1000 |
| lRz | 25 |

What they do **not** show is whether the axes respond to the stick. That is
still unknown and needs a valid re-run.

## Root cause of the invalid test - a METHOD bug, worth keeping

The probe was armed inside a shell call that then **blocked for the whole 26
second window**, printing "PROBE ARMED - move the stick now" as part of its
output. Terminal output only reaches the user when the call returns - so the
instruction arrived *after* the window had already closed. The user had no
signal the probe was open.

**Fix: arm the probe and return immediately**, ending the turn so the
instruction actually reaches the user while the window is open, then read the
results in a separate call afterwards.

This is a variant of a mistake already recorded twice today: running an
experiment without confirming its precondition held. Previously it was "verify
the game is not in a menu"; here it is "verify the human knows the probe is
open". Same shape.

## Still valid from the earlier run

The **mouse** finding is unaffected and stands: over 1198 polls the game never
called `GetDeviceState` on the mouse device at all, while polling a keyboard
(cb=256) and a joystick (cb=80) every frame. That test only required the user to
move the mouse, which they did.
