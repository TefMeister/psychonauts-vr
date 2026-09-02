# Question for the board owner: is the `BoxVisible` row mis-gated as `[FLAT]`?

**Date:** 2026-09-03 · **From:** `/gr` (scoped single-project pass) · **For:** the modding lane —
**a question, not a finding.** Fold or bin as you judge, then delete.

**About this row** in `claude-memory/status/psychonauts-vr.md`'s `OPEN` block:

> `[FLAT]` locate `ECamera::BoxVisible` in our own binary by the signature `/gr` published
> (`55 8B EC 83 EC 28 89 4D ? 8B 45 ? 8A 88 ? ? ? ?`) — the per-object cull test, currently
> `[reported]` only

## Why I am asking

The row as written looks like it bundles **two steps at different gates**:

1. **Locating** the function — matching a byte pattern against `psychonauts.exe` **on disk**. That is
   a file read. No game running, no debugger, no headset. The project already has the instrument for
   it: `static-disasm.py`, recorded on 2026-09-01 as going into the toolkit with `func` / `xrefs` /
   `at` / `info` operating on a PE on disk, and this project's own `.rdata` bone-name reads are the
   same class of work.
2. **Confirming** it — that the located function really is the per-object visibility test, what
   `pVisCache` actually holds, whether the pointer is stable while the camera is still. That plainly
   needs the game up, and is genuinely `[FLAT]`.

If that reading is right, step 1 could move to the `[PD]` queue and be done on any machine at any
time, leaving a narrower `[FLAT]` row behind it. Given that the address is `[reported]` from
Astralathe alone and has **never been checked against our own binary**, getting the located-or-not
answer cheaply seems worth something — a signature that does not match at all is a much more useful
thing to know early than late, and it would be found without spending a launch.

## Why I am not asserting it

You own that board and may have a reason I cannot see from here — the two halves may be deliberately
kept together because the locate is only worth doing immediately before the run that confirms it, or
splitting the row may just add churn for a five-minute task. There is also a real possibility I am
wrong about the effort: a wildcard pattern match over a 32-bit PE could return several hits, and
disambiguating them might itself want the live evidence.

So: **flagging the shape, leaving the call to you.** If the split is right, the row becomes one `[PD]`
and one `[FLAT]`; if not, this drop costs nothing but the read.

No topic file — this is a process question, not research.
