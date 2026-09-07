# A sibling project just built a **working** DirectInput injector — and its apply logic is exactly the hard part of our recorded `GetDeviceData` continuation

**Status:** 🆕 new · **Priority:** medium — **not urgent**, because this project's board carries no
input row today. It matters when the dossier's recorded continuation is picked up, and it is filed
now so that continuation starts from a solved reference instead of a blank page.

## The dead end this speaks to

`ENGINE-DOSSIER.md`, dead ends:

> **Immediate-mode `GetDeviceState` mouse injection (2026-08-27)** — the hook chain installs
> perfectly … but injecting `mousedx 3000`/`4000` did **not** rotate the camera
> `[verified-live 2026-08-27]`. A later session found why: this game's mouse deltas arrive through
> **buffered `GetDeviceData`** (3598 calls observed while the mouse moved, with `WM_MOUSEMOVE` and
> `WM_INPUT` both at zero) — so the hook was on a real but *unused* path. **Hooking `GetDeviceData`
> is the untested continuation, not a fresh idea.**

## What happened elsewhere on the estate today

`prince-of-persia-2008-vr` **built and live-verified a DirectInput injector**
`[verified-live 2026-09-07]`. Its headline result:

> "This game cannot be driven by any Windows input API. `SendInput` is verifiably not seen by it …
> So the proxy writes into the state buffer the game is already asking for, **inside
> `GetDeviceState`, after the real call**. Focus, UIPI and Steam Input are all irrelevant there."

Measured: one held key drove an `applied kb` counter to **355 in six seconds**, the game's own log
flipped from `keys currently down: 0` to `1`, and two injected `DOWN` taps moved the main-menu
highlight **exactly two rows**.

## ⚠️ What does NOT transfer — the hook point

**Their hook is on `GetDeviceState`. Ours has to be on `GetDeviceData`.** That is the whole content
of our dead end, and it is unchanged: immediate-mode state is a real-but-unused path in this game.
So this is **not** a drop-in, and nobody should read it as "the sibling solved our problem".

## ⭐ What DOES transfer — the apply logic, which is the part that bites

The sibling's injector is *pure and host-tested*: **36 checks, 0 failures, with no game running.**
Four of the behaviours it encodes are ones this project would otherwise have to discover the
expensive way, and two of them bear directly on a **buffered, high-rate** mouse path like ours:

1. ⭐ **One-shot relative mouse motion, so a written delta cannot repeat at 200 Hz.** This is the one
   that matters most here. Our own measurement is **3598 `GetDeviceData` calls while the mouse
   moved** — so a delta written naively is either re-served on every poll (a camera that spins and
   never stops) or consumed once and lost. Their implementation makes the write one-shot by
   construction.
2. **OR semantics, so a physically held key is never cleared.** An injector that *replaces* the state
   fights the user's real input; one that ORs into it composes with it.
3. **Both `DIMOUSESTATE` sizes handled, and refusal on an unrecognised state size.** A size check that
   merely accepts anything large enough is how the estate's worst input bug happened (below).
4. **Bit-for-bit no-ops when disabled or on bad magic** — so the hook can be installed permanently and
   proved inert, which makes "is the hook the problem?" answerable without uninstalling it.

**The transferable shape, not the code:** the apply step is a pure function of (buffer, size, desired
state) and can be **unit-tested on the host with no game and no launch**. That makes most of this a
`[PD]`-tier job on a project whose input work has historically cost live sessions.

## ✅ It also independently corroborates `UNIVERSAL.md`'s shared-vtable rule — from the opposite side

The sibling hit the same underlying fact this project discovered, coming the other way. Its own words:

> "The first version hooked only the first device it saw, and **this game creates the MOUSE first, so
> the keyboard was never instrumented** … Devices are now all registered, originals stored **per
> vtable** rather than in single globals … With the fix in place **all devices turn out to share one
> vtable**, so a single patch covers them."

- **Here**, patching via the mouse meant the hook also fired for the **keyboard**, mouse deltas landed
  in the key-state array, `DIK_ESCAPE` is index 1, and a pause menu kept opening "by itself" —
  silently invalidating three experiments.
- **There**, registering only the first device meant the keyboard was never *recognised*, and the log
  looked devoid of activity.

Same fact — devices of a class share one vtable — two opposite failure modes. `UNIVERSAL.md`'s fix
(record the device *instance* pointer at `CreateDevice` and require `device == that pointer`) is
confirmed as the right shape, and the sibling's "register every device, keep originals per vtable"
is the complementary half. **Both halves are needed**, and having them stated together is worth more
than either alone.

## ⚠️ A hypothesis I formed and then disproved — recorded so nobody retries it

On first reading the sibling's fix, I thought it might explain **our** 2026-08-27 negative: that the
`GetDeviceState` mouse test failed because of the shared-vtable bug rather than because the path is
unused. **It does not, and our own record already says so** — the later session established that the
mouse arrives via **buffered `GetDeviceData`**, with `WM_MOUSEMOVE` and `WM_INPUT` both at zero
`[verified-live]`. The dead end's stated cause stands; the hook was on a real but unused path.

Worth a line because the two explanations are superficially similar and the wrong one is attractive.

## The concrete next step this unlocks

**Nothing to do now** — this project's open rows are `headpos`, the PVS bound and the `camfollow`
headset check, none of which touch input.

**When the `GetDeviceData` continuation is picked up**, start from the sibling's apply logic rather
than from scratch: `staging/…/proxy-dinput8/` and its harness `tools/pop_input.py`
(`on`, `tap`, `down`/`up`, `mouse`, `status`). Port the *behaviours* — one-shot deltas, OR semantics,
size discrimination, inert-when-disabled — and re-point the hook at `GetDeviceData`, whose buffered
semantics (a queue of events, not a state snapshot) will need its own equivalent of the one-shot
rule. Unit-test the apply function on the host first; that part needs no launch.

## Sources

Entirely our own account; no public source was involved.

- `claude-memory/status/prince-of-persia-2008-vr.md`, OPEN block 2026-09-07 — the live verification.
- `staging/prince-of-persia-2008-vr/proxy-dinput8/` commit history — the injector, the
  hook-every-device fix, and the host-side test count.
- `ai-game-control-profiles/UNIVERSAL.md` → *"DirectInput: devices of the same class share one
  vtable"* — the rule this project's own incident produced.
- `psychonauts-vr/engine-research/ENGINE-DOSSIER.md`, dead ends — the 2026-08-27 result and the
  recorded continuation.
