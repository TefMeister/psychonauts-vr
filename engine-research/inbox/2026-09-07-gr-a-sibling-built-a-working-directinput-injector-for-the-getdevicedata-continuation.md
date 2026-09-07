# For the recorded `GetDeviceData` continuation: a sibling just built a working DirectInput injector, host-tested

**From:** `/gr` (estate sweep, 2026-09-07) · **For:** the modding lane, for `ENGINE-DOSSIER.md`'s
dead-ends section

**One ask:** add a pointer beside the "hooking `GetDeviceData` is the untested continuation" line, so
whoever picks it up starts from a solved reference.

**Not urgent** — this project's board carries no input row today.

**Full write-up:** [`external-research/topics/2026-09-07-the-sibling-just-built-a-working-directinput-injector-and-its-apply-logic-is-the-hard-part.md`](../../external-research/topics/2026-09-07-the-sibling-just-built-a-working-directinput-injector-and-its-apply-logic-is-the-hard-part.md)

## ⚠️ What does NOT transfer

`prince-of-persia-2008-vr` hooks **`GetDeviceState`**; we need **`GetDeviceData`**. That difference
*is* our dead end and it is unchanged. This is not a drop-in.

## ⭐ What does — the apply logic, host-tested with no game running

Their injector is a pure function, **36 checks, 0 failures, on the host**. Four encoded behaviours,
two of which bite hardest on a buffered high-rate path like ours:

1. ⭐ **One-shot relative mouse motion, so a written delta cannot repeat at 200 Hz.** We measured
   **3598 `GetDeviceData` calls while the mouse moved** — a naive delta either re-serves on every poll
   (a camera that spins forever) or is consumed once and lost.
2. **OR semantics, so a physically held key is never cleared** — the injector composes with real input
   instead of fighting it.
3. **Both `DIMOUSESTATE` sizes handled, and refusal on an unrecognised size** — a mere "big enough"
   check is precisely how this project's Escape-synthesis incident happened.
4. **Bit-for-bit no-op when disabled or on bad magic** — so the hook can stay installed and be *proved*
   inert, making "is the hook the problem?" answerable without uninstalling it.

**The shape worth stealing:** the apply step is a pure function of (buffer, size, desired state) and is
**unit-testable on the host with no launch** — a `[PD]`-tier way to de-risk work that has cost this
project live sessions before.

Their live result, for calibration: one held key drove `applied kb` to 355 in six seconds, the game's
own log flipped `keys currently down` 0 → 1, and two injected taps moved a menu highlight exactly two
rows `[verified-live 2026-09-07]`.

## ✅ It corroborates our own `UNIVERSAL.md` rule from the opposite direction

Their first version **hooked only the first device**, and that game creates the **mouse** first — so
its keyboard was never instrumented and the log looked dead. Ours patched via the mouse and the hook
fired for the **keyboard** too, landing deltas in the key array and synthesising Escape.

Same fact (devices of a class share one vtable), two opposite failure modes. `UNIVERSAL.md`'s fix —
record the device *instance* pointer and require `device == that pointer` — is the right shape, and
their "register **every** device, keep originals **per vtable**" is the complementary half. **Both
halves are needed.** Their run confirms all devices did share one vtable.

## ⚠️ A hypothesis I disproved before filing — so nobody retries it

I first thought their fix might explain **our** 2026-08-27 negative — i.e. that the `GetDeviceState`
test failed from the shared-vtable bug rather than from the path being unused. **It does not.** Our
own record already establishes the mouse arrives via **buffered `GetDeviceData`**, with
`WM_MOUSEMOVE` and `WM_INPUT` both at zero `[verified-live]`. The dead end's stated cause stands.
The two explanations look alike and the wrong one is the more attractive; recorded to close it.

## Suggested dossier change

One or two lines beside the existing continuation: name
`staging/prince-of-persia-2008-vr/proxy-dinput8/` and its harness `tools/pop_input.py`
(`on`, `tap`, `down`/`up`, `mouse`, `status`) as the reference implementation, note that the hook
point differs, and record the four apply behaviours as requirements for our own version — with
`GetDeviceData`'s buffered semantics (a queue of events, not a state snapshot) needing its own
equivalent of the one-shot rule.

## Credit

Our own `prince-of-persia-2008-vr` and `staging` work, and `ai-game-control-profiles/UNIVERSAL.md`.
No public source involved.
