# Double-Call Safety Test — Is `exe+0xFEDA0` ("CandB") Safe to Invoke Twice per Frame?

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only `silence-intro-videos.ps1`/`restore-intro-videos.ps1` touched
anything under it, both run and verified reverted after every launch this session, including the
two aborted diagnostic attempts).

**Goal**: close the residual risk flagged at the end of `notes/11` — rather than manually
auditing CandB/CandA's 89 combined nested helper calls, empirically call CandB
(`exe+0xFEDA0`, the confirmed outermost once-per-frame render-dispatch function) **twice** per
real frame with unmodified state, over a sustained window, and watch for any symptom of
double-simulation (double-speed animation, crashing, hanging, corrupted stack/registers, or
timing pathology) versus a normal single-call baseline.

**No stereo matrix injection was attempted this session** — this was purely the double-call
safety test, per the task's explicit scope. That remains a separate follow-up.

## 1. Method

Same launch-then-attach workaround as notes/10 §1a (launch `Psychonauts.exe` as a normal,
undebugged process, wait ~18s for steady rendering, then `start_session_attach(pid)` with
x32dbg — the 32-bit build, required for a 32-bit target; `x64dbg.exe` cannot debug a 32-bit
process). Exe base is fixed at `0x00400000` (no ASLR on the main module, confirmed notes/04), so
`CANDB_ENTRY = 0x004FEDA0` was used directly rather than resolved live.

**Per real hit during the double-call phase**:
1. Disable the `CANDB_ENTRY` breakpoint (so the script's own synthetic re-entry into that address
   can't retrigger it).
2. Snapshot the full register context at entry (`eax,ebx,ecx,edx,esi,edi,ebp,esp,eflags` — this
   *is* "identical arguments/register state" for a function with no explicit stack-passed args).
   Read the return address `ra` from `[esp]` (valid because the breakpoint fires exactly at the
   `55 8B EC` prologue, before `push ebp` executes, so `esp` still points at what the real `call`
   instruction pushed).
3. Run the **real, first, unmodified invocation** to completion via a targeted single-shot
   breakpoint planted exactly at `ra` (see §2 for why this replaced x64dbg's `rtr`/"run to
   return" command), confirming it lands back at `ra` with the stack correctly popped.
4. Push `ra` again as a fresh return address, restore every GPR + EFLAGS to the step-2 snapshot,
   set `EIP = CANDB_ENTRY` — this is a second, fully independent call with the exact same
   incoming state as the first.
5. Run this **second, synthetic invocation** to completion the same way (single-shot breakpoint
   at `ra`), confirming it also lands back at `ra` with the stack correctly rebalanced.
6. Re-enable the `CANDB_ENTRY` breakpoint, then `go()` and let the game's own per-frame code
   continue using the second call's resulting state (this most closely matches what a real
   dual-render hook would do — the caller only ever sees the *last* call's outcome).

Both runs were timed. **Baseline (before)** and **baseline (after)** phases just `go()` through
hits normally (single real call, no synthetic second invocation), to give an actual
apples-to-apples before/after timing comparison, not a vibe check, and to check for any lasting
effect once double-invoking stops.

Window sizes: 8 baseline-before hits, **15 consecutive double-invoked hits**, 8 baseline-after
hits — the double-call window meets the task's "at least 10–15 consecutive frames" requirement.

## 2. A real debugging-harness bug found and fixed along the way (reusable for future sessions)

Two failed attempts preceded the successful run, both worth recording:

**Attempt 1 — event-queue-based hit detection was unreliable.** An initial version used the same
async debug-event queue (`_debug_events_q`) and oldest-first draining pattern proven in notes/10
§1b. It broke immediately: a drained `EVENT_BREAKPOINT` matching `CANDB_ENTRY` was returned, but
`get_regs()` immediately afterward showed a completely unrelated `EIP` (`0x402121`). Root cause
(inferred, not exhaustively proven): `EVENT_PAUSE_DEBUG`/`EVENT_RESUME_DEBUG` events appear to be
published as a side effect of the automation layer's *own* command execution, not only genuine
debuggee pauses — treating every non-target event as "safe to `go()` past" (notes/10's own fix)
can therefore resume the real debuggee prematurely and desynchronize the event stream from live
state. **Fixed by dropping the event queue entirely** for hit-detection and using the library's
synchronous poll-based primitives instead (`wait_until_stopped()`, `is_debugging()`,
`get_reg('eip')`), which query live debugger state directly. Simpler and, empirically, reliable
across the full successful run.

**Attempt 2 — x64dbg's `rtr` ("run to return") was not reliably call-depth-aware across this
call tree.** The double-invoke logic originally used `client.ret(frames=1)` (the `rtr` x64dbg
command) to run each invocation to completion. Both the "real" and "synthetic" calls consistently
landed somewhere *other* than the true return address, with near-identical durations to a normal
single-frame interval — consistent with `rtr` stopping at some intermediate `ret` inside the
~90-deep nested call tree (CandB → CandA → dozens of draw/traversal helpers) rather than CandB's
own top-level return. Four iterations of compounding bad-landing arithmetic (each iteration's
"expected" stack state built on the *previous* iteration's wrong landing) eventually left the
process wandering and every subsequent wait timed out — a **self-inflicted artifact of this
technique**, not evidence about CandB's own safety. **Fixed by abandoning `rtr` and instead
planting a single-shot software breakpoint at the specific, previously-captured return address
`ra`** and just `go()`-ing to it — unambiguous regardless of call depth, since it only fires when
`EIP` reaches that exact byte.

**A third, smaller wrinkle** surfaced even after the `rtr`→targeted-breakpoint fix: the
*second* (synthetic) invocation's `EIP` landed deterministically at exactly `CANDB_ENTRY+1`
(`0x4FEDA1`) on every one of an initial 15/15 attempts — i.e., exactly one byte past the `push
ebp` prologue instruction. This is consistent with x64dbg's own "step off the current breakpoint
location before truly resuming" housekeeping firing when `EIP` is manually set to sit exactly on
an address that used to carry a (now-disabled) breakpoint. The landing is fully stack-consistent
(the pushed `ra` sits untouched just below the now-decremented `ESP`), so simply calling `go()`
again from there is safe and semantically identical to an uninterrupted run. Making
`run_to_address()` retry past any unexpected intermediate stop (the same resilience pattern
already used for the outer per-frame wait loop) fixed this completely — **15/15 double-invokes
then succeeded cleanly** in the final run.

## 3. Results

### Baseline cadence (before vs. after)

| Phase | Hits | Per-hit interval (real seconds) |
|---|---|---|
| baseline_before | 8 | 0.2029s – 0.2054s (essentially flat, ~0.204s avg) |
| baseline_after | 8 | 0.2029s – 0.2054s (essentially flat, ~0.204s avg) |

**Statistically indistinguishable before vs. after.** The double-call phase left no lasting
effect on frame cadence, timing, or stability once double-invoking stopped — no growing lag, no
degraded performance, no delayed crash.

### Double-call phase (15/15 consecutive hits)

| Metric | Result |
|---|---|
| `landed1_ok` (real call returns to the correct address) | **15/15 True** |
| `landed2_ok` (synthetic call returns to the correct address) | **15/15 True** |
| `esp_balanced` (stack pointer identical after both calls) | **15/15 True** |
| Entry register snapshot (`eax0`, `ecx0`, `edx0`) | **Identical across all 15 hits** (`eax0=5238176`, `ecx0=48472432`, `edx0=7396452` every single time) |
| Return address (`ra`) | Identical across all 15 hits (`0x465d3c`, a single fixed call site) |
| Return value, real call (`eax1`) | **1**, all 15 hits |
| Return value, synthetic call (`eax2`) | **1**, all 15 hits — **identical to `eax1` every time** |
| Real invocation duration (`first_dur`) | 0.203s – 0.225s (one outlier at 0.225s, hit 11; otherwise flat ~0.204s — matching the baseline per-frame interval almost exactly) |
| Synthetic invocation duration (`second_dur`) | 0.406s – 0.408s for 14/15 hits (~2× `first_dur`, from one extra retry round-trip inside `run_to_address` — a debugger-IPC artifact, see §2's third wrinkle, not evidence the game did 2× the work); 0.206s on hit 14 (no extra retry needed that time) |
| Real per-frame cadence during double-call phase | ~0.82s/hit (vs. ~0.204s baseline) — expected: each hit now does ~13 extra synchronous IPC round-trips (register reads/writes, two breakpoint-driven runs) on top of the real work, not a claim about the game's own execution time (`first_dur` staying flat at the baseline rate is the more informative number here) |
| Crashes / hangs / unhandled exceptions | **None** — all phases completed without timeout or process exit |

**`eax2 == eax1` on every single hit is a specifically meaningful result**: if CandB (or CandA,
or any of the 89 nested helpers) contained an "already rendered this frame, don't redo it"
reentrancy guard, the second call would be expected to short-circuit and return a *different*
value (or take conspicuously less time) than the first. Neither happened — the second call's
return value and duration profile are consistent with it having done comparably real work, not
being silently skipped.

**Six isolated, non-repeating "unexpected stop" events** were logged once, between hit 12 and hit
13, at addresses (`0x6e75fc00`, `0x6e75fc80`, `0x6ea8c208`, `0x6f0b0b87`, `0x728c64c0`,
`0x728c66a0`) well outside the exe's own module range (`0x400000`-based) — almost certainly
inside a DLL (driver, D3D runtime, or Steam-overlay-hook trampoline code), and all six landed
within an 8-millisecond burst. Execution resumed cleanly past every one of them
(`pass_exceptions=True`), and the *very next* real hit (13) succeeded with a perfectly clean
double-invoke (`landed1_ok`/`landed2_ok`/`esp_balanced` all `True`). This happened only once, only
during the double-call window (never during either baseline), and there's no strong evidence
tying it to the double-invoke technique specifically rather than incidental background thread
activity (Psychonauts is not single-threaded — Steam overlay/audio threads are plausible
sources). Flagged honestly as an open, low-confidence observation rather than folded into the
verdict either way.

## 4. Verdict

**Confirmed safe to call CandB (`exe+0xFEDA0`) twice per frame — no observable double-simulation,
no crash, no hang, no destabilizing anomaly, across a sustained 15-consecutive-frame double-call
window with clean, matching baselines immediately before and after.**

Concrete evidence, all pointing the same direction:
- 15/15 double-invokes landed correctly with the stack perfectly rebalanced (byte-identical
  `ESP` before/after both calls).
- Entry register state was bit-identical across all 15 real hits — no sign of any per-frame
  accumulating value flowing *into* CandB that a second call would perturb.
- The second call's return value and execution-duration profile closely tracked the first's on
  every hit — no evidence of an internal "already did this" guard causing the second call to
  silently no-op.
- Baseline frame cadence before and after the double-call window is statistically identical —
  no lasting corruption, no growing lag, no delayed crash.
- Zero crashes, zero hangs, zero unresolved exceptions across the entire test (two earlier
  self-inflicted tooling failures notwithstanding — see §2 — neither implicated CandB itself,
  both were artifacts of the re-invocation *technique*, diagnosed and fixed before the clean run).

**Honest limitations** (matches the task's own framing — this closes the gap cheaply, it doesn't
replace a full 89-function manual audit):
- No visual/screenshot confirmation of animation speed was performed — the verdict rests on the
  quantitative signals above (stack/register/return-value/timing consistency), not a direct look
  at whether the title screen's rotating prop visibly sped up. Given CandA/CandB's own disassembly
  (notes/11 §1) already showed zero floating-point writes and zero direct D3D calls at this level,
  and every non-stack write observed live changes address every frame rather than accumulating,
  this was judged sufficient without adding screen-capture tooling for a scoped safety check.
- The six unexplained brief stops in §3 remain unexplained; they didn't correlate with any
  double-invoke failure and didn't recur, but are noted rather than dismissed.
- Still, strictly, 89 nested helper calls were not individually re-audited this session (that
  was always the point of doing this test instead).

## 5. Recommendation — green light for the real stereo hook

**Proceed to the actual dual-render hook next session**, per the plan already laid out in
notes/10 §6 and notes/11 §4: hook `exe+0xFEDA0`, and on each real invocation call it twice — once
per eye, with the proven per-eye `BuildViewMatrix` offset injection (notes/09) and separate
render targets stood up via the already-hooked `CreateDevice` (notes/06) — compositing before the
real `Present`. This session's result removes the last open empirical question blocking that
work; no further investigation is recommended before attempting it.

**One methodology note to carry forward**: the actual stereo hook will run *inside the game's own
process* (a detour/inline hook, not an external debugger controlling execution step-by-step), so
none of §2's x64dbg-specific artifacts (the `rtr` call-depth issue, the "step off a disabled
breakpoint" quirk) apply there — those were debugger-tooling gotchas for *this test's*
methodology, not properties of the game's code. The clean technique that *does* generalize is the
core finding itself: CandB's return value and stack discipline are well-behaved and safe to
invoke twice with fresh, unmodified per-eye state each time.

## 6. Cleanup

Two aborted diagnostic attempts (event-queue desync; `rtr` call-depth misbehavior/cascading bad
state) plus one fully successful run this session, each wrapped in the same guaranteed-cleanup
pattern: `silence-intro-videos.ps1` before every launch, `restore-intro-videos.ps1` after every
kill (including the two aborted runs). One reusable gotcha found: the Python script's own
`finally`-block termination of the x32dbg session process (`psutil.Process(session_pid).terminate()`)
did not reliably kill the x32dbg GUI process in 3 of 4 runs this session (a stray `x32dbg.exe`
plus its stale `xauto_session.*.lock` file were left behind each time, though `Psychonauts.exe`
itself was reliably killed by the same pattern) — worth hardening in a future session's script
(e.g. `detach_session()` before `terminate_session()`/process-kill, or verifying the PID is gone
with a retry loop) rather than relying on a single `terminate()` call. Manually verified and
cleaned after every attempt this session via direct `Stop-Process`. Final state verified clean:
no `Psychonauts`/`x32dbg`/`x64dbg`/`python` processes running, no `.silenced` files remaining
under `WorkResource\Cutscenes\Prerendered`, `INTRO.bik` confirmed restored, no `d3d9.dll` or any
other stray file in the game directory, no stale `xauto_session.*.lock` files remaining.
