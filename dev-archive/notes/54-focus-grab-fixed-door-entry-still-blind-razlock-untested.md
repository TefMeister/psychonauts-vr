# 54 — Automation: focus-grab bug FOUND+FIXED; door-entry still unreliable; RAZLOCK test still pending

**Date:** 2026-08-20, dev machine, direct continuation of notes/53. Goal was the natural next step
after proving the FP composition math correct: capture `g_razNearValid`'s real-gameplay hit rate
(the new leading suspect for the FP bug). **Result: a genuine, durable automation bug was found and
fixed (the actual RAZLOCK test remains untested - stopped for the day per the user, resume
tomorrow).**

## Bug found: SetForegroundWindow grant is single-use, not durable

`tools/input/send_key.ps1` called `SetForegroundWindow` once per key send and threw immediately on
the first miss. Across several attempts today this consistently failed after 1-3 sends. Root cause
(confirmed via a live `GetForegroundWindow()` probe): **another process was actively holding/
contesting the foreground window** (a `powershell.exe` instance, almost certainly part of the
tool-execution harness), and Windows' `AllowSetForegroundWindow` grant is consumed by the very next
`SetForegroundWindow` call, not durable across a whole sequence of future calls. A longer pre-input
delay makes this WORSE (lets a temporary post-launch allowance expire before the first real send) -
tried and confirmed counterproductive.

**Fix (in `send_key.ps1`):** retry loop - re-grant `AllowSetForegroundWindow(pid)` fresh immediately
before every `SetForegroundWindow` attempt, up to 5 tries with a short pause between. This is a
genuine, durable fix: two full 13-key `enter_gameplay.ps1` sequences completed with zero send
failures after the change (previously: 3 consecutive failures, aborting after 1-3 keys each time).
**This fix should be considered permanent infrastructure for all future automated input sessions on
this project**, not a one-off workaround.

## Still unresolved: enter_gameplay.ps1's blind walk misses the CONTINUE door

With focus-grab now reliable, the walk itself was tested 3 more times (fixed script, unchanged
timings) and landed at the **exact same coordinates** every time (`eye≈(-88,593,16)`) - not random
bad luck, a deterministic miss. A tuning attempt (2 UP steps instead of 3) was tried once but the
diagnostic eye-dump captures proved too noisy to read cleanly (the dump fires on its own internal
~5s throttle, decoupled from the input sequence's own timing, so a single snapshot doesn't reliably
correspond to "the state right after the sequence finished"). One screenshot did clearly show the
camera orbited to a steep overhead angle over the brain with the blue CONTINUE card visible but the
character not obviously lined up for a head-on approach - real, useful evidence, but not enough
alone to converge on a fix before the session ended.

A final manual-assist capture (user pressed Continue directly) ALSO stayed at menu-space coordinates
for ~103 seconds / 6221 frames (confirmed via `RAZLOCK: total=6221 ... nearHit=0%`), suggesting even
the manual attempt didn't land inside real gameplay this time (or landed somewhere the detector
still can't see Raz) - unclear which, not resolved before stopping for the day.

## What's proven vs. still open

- **PROVEN**: input delivery (`SendInput` + reliable foreground-grab) now works durably.
- **STILL OPEN**: reaching real gameplay reliably (either the door-entry walk needs proper tuning
  with a clean before/after feedback loop - not noisy throttled screenshots - or needs a smarter
  in-engine level-jump instead of a blind walk, per playbook §2.1's own recommendation).
- **STILL UNTESTED**: the actual `g_razNearValid` hit-rate hypothesis from notes/53 (Raz-lock
  reliability during real gameplay) - the whole point of tonight's session, blocked by the above.

## Next session (resume point)

1. Get INTO real gameplay reliably first - either fix `enter_gameplay.ps1`'s walk with a proper
   iterative tuning loop (capture on-demand right after each candidate sequence, not the throttled
   eye-dump timer), or find an in-engine Lua/console equivalent of "jump to level" (playbook §2.1).
2. THEN run the `PSYVR_RAZLOCK_STATS=1` capture (already built, notes/53) during real walking to
   test the leading FP-bug hypothesis.
3. Diagnostic surface from tonight is all still in place and unchanged:
   `tools/input/auto_razlock_test.ps1`, `tools/input/auto_door_probe.ps1` (needs `enter_gameplay.ps1`
   updated back from any tuning experiments - currently points at the original, un-tuned script).

🤖 Session via Claude Code. No game files modified. Game closed and intro videos restored at
session end (confirmed: zero `.silenced` files remaining).
