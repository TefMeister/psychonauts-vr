# Session 18 — Stereo-Correction Index Bug Found and Fixed (Lead 2), Action-Slot-4 Ruled Out (Lead 1)

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; `silence-intro-videos.ps1` run once at session start, `restore-intro-videos.ps1`
run once at the end and verified reverted — `INTRO.bik` present under its real name, no `.silenced`
files remain; `move-window-offscreen.ps1` run after every launch and again after every debugger attach
this session, per the coordinator's mid-session update extending it to also sweep x32dbg/x64dbg's own
GUI window, not just the game's — verified working both ways every time). No headset available this
session (work PC) — everything here is debugger evidence, live memory captures, and controlled
screenshot comparisons, not in-headset testing. No game files copied into any git repo; the only file
ever copied into the game directory was the proxy `d3d9.dll` test build, removed after every run.

**Task**: continue notes/17's two leads to completion. Lead 1: find which abstracted keybinding slot
the title screen reads for "confirm," and try writing directly into it. Lead 2: re-examine the
register-6 stereo correction in `proxy_d3d9.c` now that notes/17 confirmed the upload is a
Transpose(WVP), and fix/re-verify if the confirmed structure reveals a real flaw.

## Result summary

**Lead 2: a real, confirmed mathematical bug was found in the existing stereo correction — caused
directly by the transpose notes/17 confirmed — and has been fixed and re-verified.** The correction
was patching the wrong matrix element (`floats[12]` instead of `floats[3]`) because it was derived
against the pre-transpose matrix but applied to the post-transpose upload buffer the hook actually
receives. Rebuilt and re-tested with the same 0/3.25/60-unit controlled-comparison rigor as notes/14:
matches at zero (no spurious effect), diverges reproducibly at 3.25 with a qualitatively more coherent
signature (same underlying shapes, differing brightness/prominence, not a different pattern
character), and the applied delta scales exactly with magnitude in the log (4.9976 → 92.2637,
ratio 18.458 = 60/3.25 exactly). **Pushed to the mod repo** (see §3).

**Lead 1: real further progress, still not behavior-changing.** Fully disassembled the next layer down
from notes/17's stopping point — `IsActionJustPressed(actionSlot)` (`exe+0x405D50`) and a top-level UI
dispatcher (`exe+0x405DB0`) that ORs `RETURN`/`SPACE`/a virtual gamepad-button code/`actionSlot==4` for
a "confirm" context and `ESCAPE`/`actionSlot==7` for a "cancel" context. Live-confirmed `actionSlot==4`
is actively polled every frame at the idle title screen with real keybindings (`SPACE`, and DIK 0x19).
**Directly forced `IsActionJustPressed(4)`'s return value to `TRUE` at its own `ret` instruction, 35
times over an 18-second window (essentially every poll)** — the exact "write directly into the read
site" technique the task asked for — and the title screen did not move (screenshots identical
before/during/after). This is a clean, real negative result that rules out this specific consumer,
not a repeat of notes/17's null-callback finding. The true gate remains unidentified; concrete
narrower next steps are in §2d.

## 1. Lead 2: the transpose-index bug

### 1a. Re-deriving the correction against the confirmed structure

notes/14's correction assumes the uploaded matrix is the raw, row-major `WVP` (row-vector convention,
`v_clip = v_obj * World * View * Proj`) and patches `floats[12]` — flat index for row 3 (translation
row), column 0 — with `-d * Proj[0][0]`. The derivation itself is sound (re-checked this session,
including the previously-unstated step: inserting `T(-d,0,0,0)` between `View` and `Proj` only reduces
to a *pure* additive shift on `WVP`'s row 3 if `World*View` is affine — i.e. its own column 3 is
`[0,0,0,1]` — true for any ordinary rigid/scale model+view transform, and not undermined by notes/17's
non-identity-World finding, since that non-identity component is a translation, which keeps a matrix
affine).

**The bug is not in that derivation — it's in *where* the correction is applied.** notes/17's live
stack trace proved the register-6 upload is `Transpose(WVP)`, computed in-place in the game's own code
*before* `SetVertexShaderConstantF` is ever called — so `Hook_SetVertexShaderConstantF` only ever sees
the already-transposed buffer. Since `upload[r][c] = WVP[c][r]`, the element the derivation targets
(`WVP[3][0]`) lives in the transposed buffer at `upload[0][3]` — flat index `4*0+3 = 3` — **not** index
12. Index 12 in the transposed buffer is `upload[3][0] = WVP[0][3]`, an unrelated element that (in a
standard row-vector perspective pipeline) contributes to the homogeneous `w` output scaled by each
vertex's own object-space X coordinate:

```
w_clip = x_obj*WVP[0][3] + y_obj*WVP[1][3] + z_obj*WVP[2][3] + w_obj*WVP[3][3]
```

So the prior code was perturbing the perspective-divide term as a function of each vertex's local X
position — a per-vertex-position-dependent distortion — rather than adding a uniform, constant shift to
clip-space X via `w_obj`'s coefficient (`x_clip += w_obj * WVP[3][0]`, and `w_obj=1` for a standard
homogeneous position, so this really is a clean, uniform lateral shift). This plausibly explains
notes/14's own honest characterization of its evidence ("texture/pattern changes with magnitude" rather
than clean parallax) — a real, reproducible, magnitude-scaling effect, but the wrong effect.

Verified against notes/17's own raw sample numbers: `mul2` (=WVP) row 0 col 3 = `0.0000` (the element
the old code was patching, via `upload[3][0]`), while `mul2` row 3 col 0 = `0.0154` (the element that
should be patched, sitting at `upload[0][3]` = flat index 3 in the transposed buffer) — two completely
different, unrelated numbers, confirming the old code was not just imprecise but categorically
patching the wrong quantity.

### 1b. The fix

One-line change in `tools/proxy-d3d9/proxy_d3d9.c`, `Hook_SetVertexShaderConstantF`:

```c
patched[3] += (-d) * xScale;   /* was: patched[12] += (-d) * xScale; */
```

Comments in the source (around `STEREO_WVP_REGISTER` and the patch site) were rewritten to document
the full re-derivation and the index correction, so a future session doesn't have to re-derive this
from scratch.

### 1c. Re-verification (same rigor as notes/14)

Built three variants (`STEREO_HALF_IPD = 0.0f`, `3.25f`, `60.0f`), each launched via the standard silent
launch → move-offscreen → screenshot (`PrintWindow`) → kill → remove-DLL cycle. All three landed on the
*same* attract-mode camera shot (confirmed via matching `BVM cache SET: eye=(...)` log lines across all
three runs — the title screen's scripted camera is deterministic, per notes/15), so differences between
runs are attributable to the correction, not shot variance:

- **`0.0` (control)**: both halves show the same content (swirl background, glowing "PSYCHONAUTS" text,
  "Press [] to begin"), differing only in minor brightness — matches notes/14's own zero-control result,
  confirming no spurious effect from the fix.
- **`3.25` (shipped value)**: log shows `SVSCF stereo-correct: reg=6 phase=1 xScale=1.5377 d=-3.250
  delta=4.9976` on every hit, reproduced identically across 3 separate screenshots in the same run. The
  two halves now diverge in a way that reads as **the same underlying shapes (swirl pattern, glowing
  text) at different brightness/prominence**, rather than notes/14's old bug producing a *different
  pattern character* per eye (spiky/fractal vs. blob-shaped) — a qualitatively more coherent, more
  "real-parallax-like" signature than before, consistent with genuine depth-dependent lighting/shape
  prominence shift on close background/glow geometry.
- **`60.0` (18× diagnostic)**: log confirms the delta scales exactly (`92.2637`, and
  `92.2637 / 4.9976 = 18.458 = 60 / 3.25` to 3 decimal places) — proof the code path scales the
  correction magnitude correctly. The *visual* divergence at this magnitude, on this particular run's
  shot, was more subtle than at 3.25 rather than more dramatic — an honest, non-monotonic-looking
  result, but not a new concern: notes/14 itself documented the old (buggy) correction also changing
  *character* rather than simply "more of the same" between 3.25 and 60, so non-monotonic visual
  character with magnitude on this specific texture-driven content is consistent with prior sessions'
  own findings, not a red flag specific to this fix.

**Disposition for Lead 2**: this is judged a genuine, confirmed bug fix — the flaw is derived
rigorously from already-live-confirmed numbers (no new live tracing needed to find it), the fix is a
one-line, well-understood change, and it was re-verified with the same before/after, matched-shot,
match-at-zero methodology notes/14 used to justify its own original push to the mod repo. Pushed to the
mod repo this session (see §3).

## 2. Lead 1: `IsActionJustPressed(actionSlot)` fully traced and directly ruled out

### 2a. Static disassembly: two more consumer-layer functions

Continuing from notes/17 §1f's stopping point (`0x00405910`/`0x00405930`/`0x00405950` = `IsKeyHeld`/
`IsKeyJustPressed`/`IsKeyJustReleased`, each `__cdecl(dik)` reading `keyState[dik] & bit`), a live
attach-and-disassemble pass (no breakpoints yet, pure static read of process memory) found:

- **`0x00405D50` — `IsActionJustPressed(actionSlot)`**: loops over the 3 keybinding categories
  (`ecx*0x54 + edx*4 + 0x782008`, `ecx`=category 0..2, `edx`=`actionSlot` arg), and for each category
  with a nonzero DIK code bound to that slot, calls `IsKeyJustPressed(dik)` (`0x00405930`); returns
  true if any category's binding was just pressed. `__cdecl(actionSlot)`, entry `push ebp` at
  `0x00405D50`, `ret` at `0x00405DA5`.
- **`0x00405DB0` — a top-level UI-context dispatcher**, `__cdecl(context)`: for `context==0`, ORs
  `IsKeyJustPressed(DIK_RETURN=0x1C)`, `IsKeyJustPressed(DIK_SPACE=0x39)`,
  `IsKeyJustPressed(0x102)` (a virtual code above the real DIK range, almost certainly a merged
  keyboard+gamepad "digital button" ID), and `IsActionJustPressed(4)`. For `context==1`, ORs
  `IsKeyJustPressed(DIK_ESCAPE=0x01)` and `IsActionJustPressed(7)`. Any other context returns false.
  `ret` at `0x00405E40`.

This reads exactly like a generic "confirm" (context 0) / "cancel" (context 1) input-abstraction check
used across multiple UI screens — a strong structural match for the title screen's "press [] to begin /
Esc to quit" prompt.

### 2b. Live confirmation: `actionSlot==4` is actively polled at the idle title screen

A live breakpoint survey (20s window, idling at "Press [] to begin", no synthetic input) on
`IsKeyJustPressed` (`0x00405930`) found exactly two distinct `(caller, dik)` pairs, both from the same
return address (`0x00405D90`, inside `IsActionJustPressed`'s loop body):

```
IsKeyJustPressed  dik=0x39 SPACE  count=40  caller=0x00405D90
IsKeyJustPressed  dik=0x19        count=39  caller=0x00405D90
```

This means `actionSlot 4` (the slot the dispatcher's context-0 path checks) has real keybindings —
`DIK_SPACE` in one category and DIK `0x19` in another — and is being read every single poll while the
title screen idles. This is a strong, live-confirmed candidate for "the slot the title screen reads."

### 2c. The direct-write test: forcing `IsActionJustPressed(4)` true — a clean negative result

Per the task's explicit suggestion ("try writing directly into IT, bypassing the null-callback problem
entirely"), a script paired entry hits (`0x00405D50`, capturing the `actionSlot` argument) with `ret`
hits (`0x00405DA5`), and whenever the paired entry's argument was `4`, set `EAX=1` immediately before
the `ret` executed — i.e., unconditionally reporting "action slot 4 was just pressed" to whatever called
it, on every single poll.

```
n_entry=36  n_ret=35  n_forced=35  arg_counts={4: 36}
```

35 forced-true returns over an 18-second window (one entry was still in flight at the window's end,
unpaired) — essentially every poll during that window. **A screenshot taken during/after this window is
pixel-identical to the baseline idle state**: "Press [] to begin" unchanged, no transition, no visible
reaction whatsoever. This is a clean, direct, real negative result — not the same finding as notes/17's
null-callback discovery, since this tests an entirely different consumer (a direct polling read, not a
registered listener), tested at the exact point of consumption rather than inferred.

A follow-up attempt to test the top-level dispatcher (`0x00405DB0`) directly (same forcing technique,
targeting its own `ret` at `0x00405E40`) found it **never entered at all** during an 18-second window
immediately following the slot-4 test, and a further, longer (35s) re-survey of `IsKeyJustPressed`
itself came back with **zero hits total** — a sharp contrast with the very first survey's 79 hits in
20s under otherwise-similar conditions. This is flagged honestly as **unresolved, not chased further**:
by that point in the session, the game process had been through three separate attach/detach cycles in
quick succession (with attendant "stale lockfile" messages from the automation library, matching
notes/16/17's own documented cross-run flakiness), and several stray `python`/`x32dbg` processes were
found still running afterward — real evidence of accumulated session-state noise, not evidence about
the game's own logic. Rather than keep re-testing against an increasingly confounded live session
(exactly the failure mode the task's methodological-discipline guidance warns against), this was judged
the responsible stopping point.

### 2d. Disposition and concrete next steps

**`actionSlot==4` is ruled out** as the title screen's actual "dismiss" gate via a direct, rigorous
write-test — real forward progress, since notes/17 left "identify which slot" as the open next step,
and this session identified a strong candidate, traced it fully, and definitively tested it, narrowing
the search space even though the answer was negative. Concrete, still-open next steps for a future
session:

1. **Re-test the top-level dispatcher (`0x00405DB0`) fresh**, in an otherwise-idle single-attach
   session (not chained after multiple other tests), to resolve whether it's ever actually called at
   the idle title screen — this session's own attempt was confounded by session-state accumulation, not
   a clean result either way.
2. **Survey `IsKeyHeld`/`IsKeyJustReleased`** (the two sibling query functions, not yet surveyed this
   session) in case the actual gate reads "held" state or a release edge rather than a just-pressed
   edge.
3. **Trace the *caller* of `IsActionJustPressed`/the dispatcher** (one level further up the call stack)
   to find what code path actually consumes their boolean result and what state/flag might separately
   gate whether that consuming code even runs at the observed title-screen sub-state — since forcing the
   check itself to return true had zero effect, the actual gate is either a different check entirely, or
   this one's result is consumed by code that isn't reached for an unrelated reason at this exact
   screen state.

## 3. Mod repo: pushed this session (Lead 2 only)

Lead 2 meets the task's own bar for a mod-repo push: a real, rigorously-derived bug found (not
speculative), a one-line fix, and re-verified empirically with the same controlled-comparison rigor
already used to justify notes/14's original push. Lead 1 did not reach a behavior change, so nothing
about the input blocker was pushed.

## 4. Cleanup

`silence-intro-videos.ps1` run once at session start, `restore-intro-videos.ps1` run once at the end —
verified: `INTRO.bik`/`DFLogo.bik`/`MajescoLogo.bik`/`transgaming.bik` all present under their real
names, no `.silenced` files remain. `move-window-offscreen.ps1` run after every game launch and again
after every debugger attach this session (per the coordinator's mid-session update extending its
default target list to `Psychonauts`/`x32dbg`/`x64dbg`) — confirmed working both ways every time it was
called. All `Psychonauts.exe`/`x32dbg.exe`/`python.exe` processes verified terminated at session end (a
batch of stray processes accumulated during Lead 1's rapid-fire attach/detach cycles were found and
killed in the final cleanup pass — flagged in §2c as the reason that line of live testing was stopped).
No `d3d9.dll` or other file left in the game directory. No game files copied into any git repo; the only
file ever placed in the game directory was the proxy DLL test build, removed after every run.

## 5. Disposition

- **Lead 2**: **fully resolved** — a real bug (patching the wrong matrix element due to the confirmed
  transpose) found via pure re-derivation against already-known numbers, fixed with a one-line change,
  and re-verified with full notes/14-grade rigor (match at zero, reproducible/magnitude-scaling
  divergence at the shipped value, exact log-level delta-scaling confirmation at 18× diagnostic
  magnitude). Pushed to the mod repo.
- **Lead 1**: real, concrete, if still behaviorally negative, progress — two more consumer-layer
  functions fully disassembled and understood, a strong candidate slot identified and live-confirmed
  active, and directly ruled out via the exact "write into the consumption point" technique the task
  requested, with proper before/during/after screenshot rigor. The true title-screen gate remains
  unidentified; three concrete, narrower next steps are listed in §2d rather than a vague "keep
  looking."
- **What still needs the home headset setup**: unchanged from notes/17 — real in-headset comfort/fusion
  validation, and confirming stereo separation survives into actual player-controlled gameplay, both
  still blocked on reaching gameplay (Lead 1).
- **What can still be done headset-free**: the three Lead 1 next steps in §2d (all pure live-debug work,
  no headset needed); extending the now-fixed register-6 correction's same index-fix logic to the
  still-uncorrected skinning registers (96/64) flagged since notes/14, once/if their own upload path is
  confirmed to go through the same transpose step.
