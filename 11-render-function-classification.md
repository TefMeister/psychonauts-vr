# Render-Function Classification — Are `exe+0x115F36`/`exe+0xFEFEE` Safe to Call Twice?

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only `silence-intro-videos.ps1`/`restore-intro-videos.ps1` touched
anything under it, both run and verified reverted after each of the two launches this session).

**Goal**: resolve the open risk flagged at the end of `notes/10` — does calling the "draw one
eye's worth of the scene" wrapper a second time (with a different view/projection matrix, for
the second eye) only re-render the already-simulated frame, or does it also advance game/
animation/physics logic (which would double-tick simulation if called twice per frame)?

## 0. Correction to notes/10: the two candidate addresses are NOT function entry points

`notes/10`'s EBP-chain walk produced *return addresses* (the address execution resumes at after
a `call` instruction returns), not function entry points. Setting a breakpoint on a return
address is a valid way to confirm code passes through that point, but disassembling "the
function" starting there only shows the tail end of it. This session found each function's real
entry by scanning backward from the candidate address for a `55 8B EC` (`push ebp; mov ebp,esp`)
prologue and verifying a forward linear disassembly from that candidate start lands exactly on
the target address at a clean instruction boundary (rules out false-positive byte matches inside
other instructions' operands):

| Candidate (notes/10 name) | Breakpoint addr (mid-function) | **True entry point** | Body size |
|---|---|---|---|
| `exe+0x115F36` ("CandA") | `exe+0x115F36` | **`exe+0x115610`** | 695 instructions to a `ret` |
| `exe+0xFEFEE` ("CandB") | `exe+0xFEFEE` | **`exe+0xFEDA0`** | 282 instructions to a `ret` |

**Both addresses correspond to real, live code and both get hit** — confirmed via a launch-then-
attach session (per `notes/10` §1a workaround) with breakpoints on `BuildViewMatrix` ("View"),
`exe+0x115F36`, and `exe+0xFEFEE` on the title screen's live camera. Over a captured window of 6
`BuildViewMatrix` hits (= 2 real frames, since View fires 3x/frame per notes/10 §2), **CandA and
CandB each fired exactly once**, always in the order `View x3 → CandA → CandB → (next frame's
Proj batch)`.

**Key structural finding**: `exe+0xFEDA0` (CandB) directly **calls** `exe+0x115610` (CandA) —
the call instruction is at `exe+0xFEFE9` (`call 0x00515610`), and `exe+0xFEFEE` (the original
notes/10 breakpoint address) is literally the return address right after that call (`0xFEFE9 + 5`
bytes for a near `call`). This is airtight, self-consistent confirmation of the hierarchy already
guessed at in notes/10: **`exe+0xFEDA0` is the true outer "render this frame" wrapper; `exe+0x115610`
is a nested function it calls partway through its body.** Both fire exactly once per real Present
frame, matching the "draw the whole scene once" behavior the task is trying to identify.

## 1. Static disassembly evidence

Full instruction-by-instruction disassembly of both function bodies (695 + 282 instructions)
was captured and classified into: stack-local writes (normal/expected, `[esp+N]`/`[ebp-N]`
destinations), non-stack memory writes (destinations based on other registers or absolute
addresses — the category that would carry persistent game/animation state), D3D-vtable-pattern
calls (`call dword ptr [reg+offset]` matching a known slot offset from notes/10's live-resolved
vtable), and other calls (direct calls to fixed exe addresses — nested helpers).

| | CandA (`exe+0x115610`) | CandB (`exe+0xFEDA0`) |
|---|---|---|
| Total instructions | 695 | 282 |
| Stack-local writes | 89 | 11 |
| **Non-stack memory writes** | **11** | **10** |
| D3D-vtable-pattern calls | **0** | **0** |
| Other calls (nested helpers) | 48 | 41 |

**Neither function issues a single D3D API call directly** — all work is delegated to nested
helper calls (89 combined, none individually disassembled — see §3 for the residual risk this
leaves). This is consistent with these being scene-traversal/dispatch-level functions sitting
*above* the actual `DrawIndexedPrimitive`-issuing code identified in notes/10's EBP chain (frames
0–2), not the bottom-level draw code itself.

**All 21 non-stack writes found are either literal-constant stores or freshly-read-register
stores — none are floating point, and none are increment/decrement/add operations**:

```
CandA (exe+0x115610):
  mov dword ptr [edx+0x228], 0x00
  mov dword ptr [eax+0x230], 0x00
  mov dword ptr [ecx+0x9DB8], eax        <- stores a live pointer
  mov dword ptr [ecx+0x40], edx
  mov dword ptr [ecx+0x44], edx
  mov dword ptr [eax+0x230], 0x00
  mov dword ptr [ecx+0x230], 0x00
  mov dword ptr [eax+0x228], 0x00
  mov dword ptr [eax+0x9DB8], 0xFFFFFFFF <- sentinel/invalidate, same offset as the pointer store above
  mov dword ptr [eax+0x228], 0x04
  mov dword ptr [eax+0x228], ecx

CandB (exe+0xFEDA0):
  mov byte ptr [eax+0xCA], 0x00
  mov byte ptr [ecx+0x530], al
  mov byte ptr [ecx+0xCB], 0x00
  mov byte ptr [ecx+0xCC], 0x00
  mov byte ptr [edx+0xC0], 0x00
  mov byte ptr [eax+0xC1], 0x00
  mov byte ptr [ecx+0xC2], 0x00
  mov byte ptr [edx+0x208], 0x00
  mov byte ptr [eax+0x209], 0x00
  mov byte ptr [eax+0x9035], dl
```

No writes resembling floating-point positions/velocities, no timer/frame-counter increments, no
animation-blend-weight-looking arithmetic. The pattern (small constant offsets, values `0`, `4`,
`-1`, or a directly-copied register) reads as **field resets and set/clear pairs on transient
per-call structures** — exactly what per-frame render-list/visibility bookkeeping looks like, not
game-logic state.

CandB also contains two calls through a fixed global function-pointer table
(`call dword ptr [0x007000B0]` and `[0x007000B4]`, at `exe+0xFEDAE`/`exe+0xFEDDC`, and the latter
again at `exe+0xFF19E`) — not matched against the known D3D vtable slot offsets, so not
identified; possibly an allocator or a different callback table. Not investigated further
(out of scope for a classification pass; flagged for a future session if it matters).

## 2. Empirical evidence (live memory watch across multiple frames)

A first watch attempt (reading `arg[].memvalue` from a fresh `disassemble_at()` call) mostly
failed to resolve real addresses (returned `None` or clearly-wrong tiny values like `0x1`/`0x6e`)
— a tooling bug, not evidence of anything; discarded. **Fixed by reading the base register
directly via `get_reg()` at the moment the breakpoint hit** (unambiguous — the exact register the
instruction is about to use) and computing `effective_addr = reg + offset` by hand, on 5 of the
11 non-stack write sites, across 8 real frames on the title screen's live camera:

| Site | Instruction | Hits | Per-frame count | Distinct base-reg values | Distinct effective addrs |
|---|---|---|---|---|---|
| A1 | `mov [ecx+0x9DB8], eax` | 0 | — | — | — |
| A2 | `mov [eax+0x9DB8], -1` | 2 | 1 | 2 | 2 |
| A3 | `mov [edx+0x228], 0` | 3 | 1 | 3 | 3 |
| B1 | `mov byte[eax+0xCA], 0` | 0 | — | — | — |
| B2 | `mov byte[ecx+0x530], al` | 3 | **1** | 2 (one repeat, one change) | 2 |

**Two findings that matter directly for the "safe to call twice" question**:

1. **Every write site fires at most once per real frame** — never more (no site was hit twice
   within the same View-hit-triple/frame boundary). This rules out "loop over many scene nodes,
   each getting a state write" as the mechanism (which would show many hits clustered inside one
   frame) — these are single, one-shot field writes during a single traversal.
2. **The effective write address is different almost every single hit** (`edx=0x100B2444` one
   frame, `edx=0x05DAA170` the next; `ecx=0x19E4F8` twice then `0x17962F50`) — i.e., these writes
   target **transient, frame-to-frame-varying memory** (heap-allocated scratch/pool objects, or
   objects reused from a pool with a changing address), not a **fixed global address**. A genuine
   persistent simulation variable (an animation clock, a physics tick counter, an entity's
   position) would live at a *stable* address (a global or a member of a long-lived singleton) and
   would be readable/writable at the *same* address every frame — that is not what was observed.
   `A3`'s value-before-write was `0x0` in 2 of 3 samples (writing `0` over an already-`0` field),
   consistent with a fresh/reset structure rather than an accumulating counter.

## 3. Verdict

**Leans safe to call twice — not proven with absolute certainty, but the evidence converges
cleanly and no contrary signal was found anywhere it was looked.**

Supporting evidence, all pointing the same direction:
- Zero D3D calls skipped by re-entering here (both functions delegate to nested code for actual
  drawing, matching notes/10's deeper EBP frames) — a second call re-issues the same delegation,
  it doesn't duplicate work that already happened once.
- Zero floating-point writes, zero increment/decrement/accumulate instructions targeting
  non-stack memory, at either function's own level (89 nested calls not individually audited —
  see caveat below).
- Every non-stack write observed is a single-shot literal-constant reset or a transient-pointer
  store/clear pair, firing **at most once per frame**, to addresses that **change every frame** —
  the opposite of what a persistent game-state variable would look like.
- The call hierarchy (`CandB@exe+0xFEDA0 → calls → CandA@exe+0x115610`) is now fully confirmed,
  not just inferred from EBP-chain depth — this narrows the actual hook target to a single,
  well-understood function.

**Caveat (why this isn't a 100%-certain verdict)**: 89 nested calls combined (48 from CandA, 41
from CandB) were identified by address but **not individually disassembled** — it remains
possible (though nothing found suggests it) that one of those nested helpers does something
logic-like that this pass didn't see. A full manual audit of 89 functions was judged not to be a
good use of a single session; the recommendation below proposes a cheaper way to close this gap.

## 4. Recommendation for the next session

**Proceed to the dual-render hook, targeting `exe+0xFEDA0` (CandB's true entry point) as the
call-twice target** — this is the outermost confirmed once-per-frame render-dispatch function,
sitting directly downstream of the already-proven `BuildViewMatrix`/`BuildProjectionMatrix`
injection points (notes/07, notes/09) and upstream of the already-mapped draw-call path
(notes/10). Concrete plan (unchanged from notes/10 §6.2): hook `exe+0xFEDA0`, and on each real
invocation call it twice — once per eye, with the proven per-eye view-matrix offset (notes/09)
and separate render targets stood up via the already-hooked `CreateDevice` (notes/06) — compositing
before the real `Present`.

**Close the residual risk empirically rather than by manual audit**: rather than disassembling
all 89 nested calls, the cheapest way to fully retire the "does a second call double-tick logic"
question is to **just do the actual double-call experiment as an instrumented test**: hook
`exe+0xFEDA0`, call it twice with identical (unmodified) view/projection matrices both times
(no stereo offset yet — isolates "does calling twice break anything" from "does the offset math
work"), and watch for any visible symptom of double-simulation over several seconds — particle
counts/positions drifting twice as fast, any animated object (the title screen's rotating
brain/props) advancing at double rate, or a crash/assert from double-freeing whatever the `-1`
sentinel at `+0x9DB8` guards. If nothing anomalous appears over a sustained observation window,
that is strong additional confirmation on top of this session's static/empirical evidence, at far
lower cost than auditing 89 functions by hand. If something *does* show double-rate behavior, the
fallback from notes/10 §6.3 (single-render + post-process reprojection/warp) remains available.
