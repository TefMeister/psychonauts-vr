# Psychonauts VR — Status

Last updated: 2026-08-16 (keystate-mechanism trace + register-6 transpose confirmation session)

## Latest status (read this first)

**Two leads worked this session — a deep live trace of the real keyboard-input mechanism (Lead 1),
and a decisive live stack trace settling the register-6 transpose mystery (Lead 2).** Full detail in
`notes/17-keystate-mechanism-trace-and-reg6-transpose-confirmation.md`. Headline findings:

- **Lead 1: notes/16's buffered-`GetDeviceData` hypothesis REFUTED with hard evidence, then the real
  keyboard-input mechanism fully traced live.** A device-identity check (capturing both keyboard AND
  mouse device pointers, then classifying every `GetDeviceData`/`GetDeviceState` hit by `this`) found
  `GetDeviceData` fires ONLY on the mouse device (42/42 hits) and never once on the keyboard across an
  80s window — notes/16's "lockstep" evidence was actually about the mouse, not the keyboard as
  assumed; the "flakiness" was a real negative, not flaky tooling. In its place, a hardware (DR-
  register) READ breakpoint on the DIK_SPACE byte — a technique not used before in this project —
  found and fully disassembled the real consumer chain: the polled buffer is read by a function that
  scans a 3×21 keybinding table plus three hardcoded DIK_RETURN/SPACE/ESCAPE checks, each calling a
  `SetKeyState(dikCode, pressed)` function that maintains a proper per-key edge-detection state array
  (`keyState[dik]`, bit0=held/bit1=just-pressed/bit2=just-released) and fires a registered one-shot
  global callback pointer on a fresh press/release edge. Re-testing the already-proven buffer-patch
  technique while watching this callback pointer live showed it correctly reaches
  `SetKeyState(SPACE, pressed=1)` (state byte transitions exactly as predicted, 0x00→0x03) but the
  global callback slot is **null** throughout — no listener is armed at the observed title-screen
  state, which is the real, now-understood reason two sessions of correctly-delivered synthetic input
  produce no visible effect. A further hardware-watch found a second consumer layer outside
  `SetKeyState` itself: a per-frame edge-bit-clearing sweep and a keybinding-table-driven translator
  converting raw DIK codes into abstracted 0x00/0xFF "digital button" output bytes, plus simple
  `IsKeyHeld`/`IsKeyJustPressed` query helpers — a real, generic input-abstraction system. Identifying
  which specific binding slot the title screen reads is the well-scoped next step, not yet done.
- **Lead 2: FULLY CONFIRMED (upgraded from notes/16's "partial support")**: a clean 4-breakpoint live
  stack/register trace (8/8 samples, both pointer identity and raw-float content compared at each
  stage) proves the register-6 upload is the in-place TRANSPOSE of the second matrix-multiply's
  result — the transpose call overwrites multiply #2's own output buffer, so the pointer never
  changes but the content does. Manually verified by hand against one sample's raw numbers (transposing
  the captured pre-transpose matrix exactly reproduces the uploaded matrix). Bonus: the same trace
  independently confirms notes/16's "non-identity World" interpretation — multiply #1 reduces to a
  bare projection matrix, multiply #2 then carries a real, non-trivial object-space translation.
- **Two reusable x64dbg-automate tooling gotchas found and fixed**: (1) `wait_for_debug_event`'s
  single-check pattern can silently lose a targeted single-shot breakpoint's event when an unrelated,
  high-frequency breakpoint (here, an unconditional `SetKeyState` breakpoint firing ~66x/poll) is also
  active — fixed with a proper drain-and-retry `wait_for_named_breakpoint()` helper, confirmed fixed
  by a clean rerun; (2) hardware and memory breakpoints persist across `start_session()` calls
  independently of software breakpoints — `clear_breakpoint(None)` does not clear them,
  `clear_hardware_breakpoint(None)`/`clear_memory_breakpoint(None)` are needed too.
- **Mod repo untouched this session** (Lead 1 didn't reach gameplay; Lead 2 is analysis/confirmation,
  not a code change). Workspace notes, modding-notes, and dev-archive synced as usual.

## Prior milestone (buffered-input chase + transpose decomposition retry session, still valid)

**Two independent leads worked in parallel this session — a deeper chase of the DirectInput input
blocker, and a re-run of the matrix-decomposition mystery against the transposed candidate matrix.**
Full detail in `notes/16-buffered-input-chase-and-transpose-decomposition.md`. Headline findings:

- **Input blocker: real infrastructure breakthrough, but the behavioral goal still not achieved.**
  This session finally resolved and hooked the actual live `IDirectInputDevice8` keyboard object (both
  `GetDeviceState` and `GetDeviceData`), closing out notes/15's exact open item, via one concrete fix
  (arm the `DirectInput8Create` breakpoint *before* resuming past startup, not after) plus two reusable
  debugger-tooling bugs found and fixed (x64dbg persists breakpoints across `start_session()` calls
  and needs an explicit `clear_breakpoint(None)`; `wait_for_debug_event` silently discards non-matching
  events and needs a drain-and-retry wrapper). Patching `GetDeviceState`'s polled output buffer
  (`DIK_SPACE` forced to "pressed") initially looked like a genuine win — a real "Loading" screen with
  the game's falling-Raz vortex animation appeared shortly after — **but this was investigated further
  and found to be a false positive**: a clean, fine-grained-timed control launch (no debugger, no
  input at all) showed that exact same Loading screen appears naturally at ~4.5s into *any* launch,
  input or no input. A longer, rigorous re-test (13 press/release cycles over ~2 minutes at the
  confirmed-idle title screen) found zero further effect. A new, better-motivated hypothesis was found
  instead: the game also polls **buffered** `GetDeviceData` (`DIDEVICEOBJECTDATA2`-sized records) in
  lockstep with `GetDeviceState`, strongly suggesting the title screen's actual gate reads buffered
  transition events, not polled state — a forged-event fix for this was implemented but not yet
  confirmed working, blocked by a reproduced cross-run flakiness in hitting that specific call (the
  same kind of flakiness notes/15 first flagged for `CreateDevice`, now understood but not fully tamed).
  Opportunistically confirmed the stereo hook (unmodified notes/14 binary) stayed robust through ~2
  minutes of concurrent debugger/DirectInput-hooking activity, and found a new nuance: the natural
  Loading-transition screen renders as a single non-split image (the hooked render path apparently
  isn't invoked for it), while the title screen itself continues to show correct split-screen
  divergence throughout.
- **Transform-path mystery: real, partial progress, not fully resolved.** Live-captured View/Proj/
  register-6 data (fresh, since no raw floats from notes/14's session survived) was re-run through the
  decomposition check against 4 hypotheses (transpose or not, on each side). The transpose hypothesis
  measurably and substantially improves the result — several samples now show row lengths within a few
  percent of 1.0, a qualitatively saner signature than notes/14's original "thousands to hundreds of
  thousands" — but no sample fully resolves (the `[3][3]` homogeneous element and column-3 residuals
  remain non-trivial), so this is partial support for the transpose hypothesis, not full confirmation.
  Leading interpretation: the transpose is very likely real; the remaining gap is most likely a
  genuine non-identity "World" component for whatever specific object register 6's draw call renders,
  which the original check never actually assumed away correctly in the first place.
- **Mod repo untouched this session** (neither lead reached a confirmed functional improvement).
  Workspace notes, modding-notes, and dev-archive synced as usual.

## Prior milestone (input-blocker retry + stereo robustness/quality session, still valid)

**This session split into two parts — an honest partial result on the long-standing gameplay-input
blocker, followed by a pivot to concrete quality/robustness work on the existing stereo prototype.**
Full detail in `notes/15-input-blocker-retry-and-stereo-robustness.md`. Headline findings:

- **Input blocker (notes/08), retried via a DirectInput device-state poke as suggested — partial,
  unresolved, but genuinely refines the prior diagnosis.** Live capture proved synthetic
  `PostMessage`/`SendInput` keystrokes **do** correctly arrive in the game's own real message queue
  (`WM_KEYDOWN`/`WM_CHAR`/`WM_KEYUP`, correct `hwnd`/`VK_SPACE`/scancode, retrieved by the game's own
  `PeekMessageA`) — refining notes/08's broader "messages don't get through" framing. `GetAsyncKeyState`
  (never called), `GetKeyState`/`GetKeyboardState` (called only by `msctf.dll`/IME, not game logic),
  and Steam Overlay hooking (not loaded in-process) were all ruled out as the actual gate. The
  `DIEmWin` window (confirming *some* DirectInput keyboard device exists) was found, but the
  specific `IDirectInput8::CreateDevice` call that created it could not be pinned down consistently
  across two fresh process launches (inconsistent stack/caller behavior each time) within this
  session's budget — stopped and pivoted per the task's own explicit guidance, rather than
  continuing to chase diminishing signal. Concrete next-session leads are in notes/15 §1d.
- **Stereo prototype robustness: 4/4 clean consecutive full launch cycles**, using the exact
  unmodified `d3d9.dll` from notes/14 (no code changes this session) — no crashes, `Stereo ready = 1`
  every run, identical `xScale`/correction values every run, and pixel-identical left/right-divergence
  screenshots across all 4 runs (the title screen's attract-mode camera turns out to be a fully
  deterministic scripted playback, not random). This is a materially stronger robustness claim than
  notes/14's single session.
- **IPD value cross-validated on comfort grounds, kept at `3.25` (no code change)**: an independent
  world-scale estimate (from `zNear`/`zFar` plausibility, "1 world unit ≈ 1cm") converges within 3%
  of both the shipped value and notes/13's original proportional-to-distance estimate — and maps the
  current value to **≈6.5cm real-world separation, within 1mm of average adult human IPD (63mm)**,
  the standard comfort target for VR stereo. The proportional-to-shot-distance derivation method
  itself is flagged as a real limitation (framing-dependent, not fixed like real human IPD) that
  should be replaced with a fixed, scale-calibrated constant once real gameplay is reachable.
- **Untraced transform-path (notes/14 §6.1): real static-disassembly progress.** `exe+0x433E50`
  confirmed as a matrix-multiply helper with an SSE/FPU-dispatch flag; `exe+0x42E2A0` newly
  confirmed as a **4×4 matrix transpose** (previously unknown). Recovered the register-6 upload's
  actual call sequence: multiply → multiply → transpose → upload. New, concrete (not yet confirmed)
  hypothesis: the transpose step plausibly explains notes/14's matrix-decomposition negative result,
  since a naive row-major decomposition check would fail exactly as observed against a transposed
  matrix. Cheap, no-live-debugging next step identified: rerun the decomposition check against
  `Transpose(candidate)` too.
- **Mod repo untouched this session** (no functional code change — the robustness test exercised the
  already-pushed notes/14 binary); workspace notes, modding-notes, and dev-archive synced as usual.

## Prior milestone (shader-constant stereo hook session, still valid)

**REAL PROGRESS: the camera-offset injection now reaches the GPU and produces a confirmed,
reproducible, magnitude-scaling visual effect — categorically different from the prior session's
complete null result — plus the independent missing-background bug is root-caused and fixed.**
Honest caveat: the evidence is a controlled 0/3.25/60-unit offset comparison (matching at zero,
diverging predictably as magnitude increases) rather than a single obviously-legible "same object
shifted sideways" screenshot, because the specific geometry driven by the identified register is a
detailed background texture, not a discrete foreground object. Full detail, derivation, and
screenshots description in `notes/14-shader-constant-stereo-hook.md`. Headline findings:

- **Found the real per-draw shader-constant upload**: `IDirect3DDevice9::SetVertexShaderConstantF`,
  `StartRegister=6`, `Vector4fCount=4`, from one consistent call site (`exe+0x11D343`) — identified
  via three live probes (register/call-site survey, ground-truth View/Proj matrix capture at
  `BuildViewMatrix`/`BuildProjectionMatrix`'s own `ret` instructions, and an EBP-chain
  "caller-of-caller" read that separated registers by subsystem, since every
  `SetVertexShaderConstantF` call shares one generic wrapper's return address). A pure-Python
  matrix-decomposition check found this register's matrix does NOT derive from the specific
  View/Proj instances the existing hooks observe (0/20 samples decomposed sanely) — a real,
  reported negative result pointing at an untraced second transform-composition path
  (`exe+0x433E50`/`0x42E2A0`), not papered over.
- **Implemented a closed-form correction that works without tracing that indirection**: patch the
  uploaded matrix's row-3/column-0 element by `-d * Proj[0][0]` — the exact algebraic result of
  inserting a rigid (non-toe-in) eye-space translation between View and Proj in a row-vector
  `v*World*View*Proj` pipeline, valid regardless of what World/View individually were. Needs only
  one live scalar (`xScale = Proj[0][0]`).
- **A real dangling-pointer bug found and fixed while computing that scalar**: caching
  `BuildProjectionMatrix`'s output-buffer *pointer* (mirroring the proven-safe pattern for
  `BuildViewMatrix`'s `pEye`/`pAt`/`pUp`) produced wildly inconsistent readbacks (`1.0`, `0.0`,
  `0.0849` instead of the real `1.5377`) because, unlike `BuildViewMatrix`'s persistent object
  fields, `BuildProjectionMatrix`'s output buffer is a short-lived caller stack temp that's reused
  well before a later frame reads it back. Fixed by computing `xScale` directly from
  `BuildProjectionMatrix`'s entry *arguments* using the exact conversion formula notes/07
  disassembled — stable `1.5377` every time, matching the independently-captured ground truth.
- **The notes/13 missing-background bug is root-caused and fixed**: both eyes' offscreen render
  targets shared the device's one auto depth-stencil surface; an explicit `Clear()` (color+depth+
  stencil) on eye 2 only (clearing both flipped which eye broke, rather than fixing both) resolved
  it — confirmed via screenshots showing real textured background content on both eyes across
  multiple runs.
- **Visible-effect evidence**: at `STEREO_HALF_IPD=0` both eyes render matching silhouette/text at
  matching positions (differing only in brightness) — proof the pipeline introduces no spurious
  effect. At the realistic `3.25` and a diagnostic `60` (18×), the same background content
  diverges between eyes in a reproducible, magnitude-scaling way (different pattern character at
  each magnitude), while unrelated screen-space UI text stays fixed in both halves as expected.
  This is real, causal evidence the correction reaches the GPU — not yet a single obviously-legible
  "object visibly moved sideways" shot, since the driven content is a detailed background texture
  rather than a discrete object, and the attract-mode camera didn't reliably hold a
  discrete-object shot long enough this session to capture one directly comparable across offsets.
- **Disposition**: judged, on balance, to clear the "confirmed visible stereo separation" bar for
  the specific register-6-driven content (controlled, reproducible, magnitude-correlated, not a
  one-off artifact) — pushed to the public mod repo this session, with the evidence's real
  character (scaling comparison, not a single clean demo shot) stated plainly rather than oversold.
  Concrete next steps (trace the untraced transform-composition path, extend the correction to
  other matrix registers for skinned content, reach a discrete-object shot for a cleaner demo) are
  in `notes/14` §6.

## Prior milestone (first stereo prototype session, still valid)

**PARTIAL: the first real stereo prototype was built and ran stably (no crash across six live
runs), and proved two independent renders can be composited live in one frame — but the camera
offset itself had zero visible effect on the image, so that session's success bar (two visibly
different CAMERA ANGLES) was not met.** Full detail, root-cause analysis (later resolved above) in
`notes/13-first-stereo-prototype.md`. Headline findings:

- **New infrastructure proven**: real inline (byte-patch + trampoline) hooks directly into
  `Psychonauts.exe`'s own code now exist and work — `BuildViewMatrix` (`exe+0x292480`) and `CandB`
  (`exe+0xFEDA0`) are both hooked with a naked-asm detour mechanism (not just COM vtable patching,
  which was all prior sessions used). `CandB`'s hook genuinely invokes the real function body
  twice per frame in the live, undebugged game process (extending notes/12's 15-frame debugger
  test to a real sustained in-process double-call) into two separate offscreen render targets,
  composited via `StretchRect` into the left/right halves of the real backbuffer before the one
  real `Present` — all `S_OK`, every frame, six separate runs, zero crashes/hangs.
- **New discovery**: `CandB`'s own nested call tree (not `CandB`/`CandA` themselves — notes/11
  found zero D3D calls at that level) calls the real `Present` internally, partway through
  rendering. Proven via a reentrancy flag read back `TRUE` from inside the Present hook while
  still nested inside `CandB`'s double-call region. Worked around this session with a 3-state
  phase (suppress eye 1's premature internal Present; repurpose eye 2's internal Present as the
  real "both eyes done, composite and flip" signal) — this fix is real and necessary, not
  speculative.
- **Camera offset doesn't reach the screen**: tested at both a realistic ~3.25-unit half-IPD and,
  diagnostically, at 60 units (18x larger) — zero visible parallax either way, even though the
  CPU-side matrix write was confirmed (byte-level read-back) to land correctly. Leading
  explanation: the camera flows to the GPU via `SetVertexShaderConstantF` (established in
  notes/07) at an upload call site that was never pinned down, most likely firing once per frame
  *before* `CandB` runs — rewriting the CPU-side matrix buffer afterward has no path back to the
  GPU. This is the single highest-value next step (see notes/13 §7).
- **Second, independent, unexplained finding**: the second `CandB` invocation's render is missing
  its background layer (near-white instead of the game's textured backdrop) even with the
  Present-reentrancy bug fixed — a materially different open question from "is it safe to call
  twice" (notes/12 already answered that for stack/register/return-value/timing; this is about
  whether every individual draw call fires identically on a second invocation, which turns out to
  be a different, unanswered question).
- **Disposition**: proxy DLL source/build synced to modding-notes and dev-archive as a detailed
  record of real working infrastructure plus a well-diagnosed partial result. **Not** pushed to
  the public mod repo this session, since the literal success criterion (two different camera
  angles) wasn't met — per the task's own explicit instruction not to oversell partial results.

## Prior milestone (double-call safety test session, still valid)

**GO: the double-call safety test passed cleanly** — this closed the last open empirical question
from notes/11 by actually doing the thing (call `exe+0xFEDA0` twice per frame with unmodified state)
instead of continuing to reason about it
statically. Full detail, methodology, and two reusable x64dbg-automate tooling gotchas found
along the way are in `notes/12-double-call-safety-test.md`:

- **15/15 consecutive double-invokes of CandB (`exe+0xFEDA0`) succeeded cleanly** over a
  sustained real-frame window: every one landed at the correct return address with the stack
  pointer byte-identical before/after both calls, entry register state was bit-identical across
  all 15 real hits, and the second call's return value (`eax=1`) exactly matched the first's on
  every hit (no sign of an "already rendered this frame" reentrancy guard silently short-circuiting
  the second call).
- **Baseline frame cadence immediately before and after the double-call window is statistically
  identical** (~0.204s/hit both times) — no lasting corruption, no growing lag, no delayed crash
  once double-invoking stopped.
- **Zero crashes, hangs, or unresolved exceptions** across the whole test. (Two earlier attempts
  this session failed for tooling reasons unrelated to CandB itself — an event-queue
  desynchronization bug, and x64dbg's `rtr`/"run to return" command not being reliably
  call-depth-aware across CandB's ~90-deep nested call tree, landing short and cascading into a
  self-inflicted bad state after 4 iterations — both diagnosed, fixed by switching to a targeted
  single-shot breakpoint at the known return address, and confirmed fixed by the clean 15/15 run
  that followed. Neither failure mode implicated CandB's own safety.)
- **Honest limitation carried forward**: no visual/screenshot confirmation of animation speed was
  performed (verdict rests on stack/register/return-value/timing consistency, not a direct look
  at the screen); six brief, non-repeating, unexplained debugger stops in unrelated-looking DLL
  address space were observed once mid-test and are noted but not folded into the verdict either
  way (didn't correlate with any double-invoke failure, never recurred). Neither limitation was
  judged to block proceeding.
- **Next session**: attempt the actual dual-render hook (per the plan already laid out in
  notes/10 §6 / notes/11 §4) — hook `exe+0xFEDA0`, call it twice per real frame (once per eye,
  proven per-eye `BuildViewMatrix` offset from notes/09) into two render targets stood up via the
  already-hooked `CreateDevice` (notes/06), compositing before the real `Present`. No remaining
  *unscoped* unknowns block this.

## Prior milestone (render-function classification session, still valid)

**The dual-render open question was resolved via static + empirical analysis: the candidate
"render one eye's scene" wrapper function was classified as pure-render-only, converging with
this session's actual double-call test above.** Two launch-then-attach x64dbg captures (see
`notes/11-render-function-classification.md` for full detail):

- **Corrected notes/10's two candidate addresses**: `exe+0x115F36`/`exe+0xFEFEE` were return
  addresses (mid-function), not entry points. Their real entry points were found by backward
  prologue scan + forward-alignment verification: **`exe+0x115610`** (695-instruction body) and
  **`exe+0xFEDA0`** (282-instruction body). **`exe+0xFEDA0` directly calls `exe+0x115610`**
  (confirmed via the exact call/return-address byte offset) — this is the true, fully-confirmed
  call hierarchy, not just an EBP-depth guess. Both fire **exactly once per real Present frame**.
- **Full disassembly of both function bodies (695 + 282 instructions) found zero D3D API calls**
  (all drawing is delegated to nested helpers, matching notes/10's deeper EBP frames) and only 21
  non-stack memory writes total, **none floating-point, none increment/accumulate** — all
  single-shot literal-constant resets or transient-pointer set/clear pairs.
- **Empirical live-memory watch across 8 real frames** (register-resolved effective addresses,
  fixing a first attempt's broken address-parsing) confirmed every write site fires **at most
  once per frame** (not a loop) and targets a **different memory address almost every frame** —
  the opposite of what a persistent game-state variable (timer, position) would look like; that
  would live at one stable address.
- **Verdict: leans safe to call twice**, not airtight (89 nested helper calls were identified by
  address but not individually disassembled — judged not worth a full manual audit this
  session). **Recommended next step, not another investigation**: hook `exe+0xFEDA0` and do the
  actual double-call experiment (call it twice with unchanged matrices first, watch for
  double-simulation symptoms over a sustained window) as the cheapest way to close the remaining
  gap, then proceed to the real stereo hook (per-eye offset + second render target + composite)
  using every already-proven primitive from notes 06/07/09/10.

## Prior milestone (render-loop structure session, still valid)

**The frame's render-loop structure was mapped, closing out the "how does the game
structure a frame" unknown flagged at the end of the write-hook session.** Live x64dbg capture
(three failed/diagnostic attempts plus one fully successful one — see
`notes/10-render-loop-structure.md` §1 for two reusable debugging-harness bugs found and fixed
along the way: launching directly under x64dbg hit a persistent access-violation retry loop this
session, worked around by launching the game normally and attaching after ~15s; and the
automation library's event-queue helper pops newest-first (LIFO) which silently corrupted a live
pointer read until fixed to drain strictly oldest-first) confirmed:

- **Frame order**: `[BuildProjectionMatrix x8, BuildViewMatrix x3] → [~89 draw calls across
  multiple SetRenderTarget/Clear/BeginScene/EndScene brackets] → Present`, every frame, camera
  matrices always built before any draw calls, `Present` always last.
- **The engine already calls `SetRenderTarget` (8x) and `Clear` (3x) multiple times per single
  frame**, with a `SetRenderTarget` burst immediately followed by `EndScene`→`BeginScene`→`Clear`
  — strong evidence of an existing multi-pass structure (most plausibly a shadow/render-to-texture
  pass) that a stereo second-eye pass could piggyback on the same mechanism.
- **A candidate "draw the whole scene" wrapper was identified via an 8-level EBP frame-pointer
  walk from the first live `DrawIndexedPrimitive` hit**: `exe+0x115F36` and `exe+0xFEFEE` are the
  two outermost/best candidates (no symbols, not yet disassembled).
- **Open risk, not yet resolved**: whether that candidate wrapper's call chain does *only*
  rendering or also touches per-frame game-logic/animation state that would double-advance if
  called twice — this is the concrete next step (disassemble those two addresses and check),
  not a new open-ended unknown. Full plan in `notes/10-render-loop-structure.md` §6.

**Proposed next milestone**: disassemble `exe+0x115F36`/`exe+0xFEFEE` to confirm one is a clean
render-only entry point, then attempt the actual dual-render hook: call it twice per frame (once
per eye, using the already-proven per-eye matrix offset from notes/09) into two render targets
stood up via the already-hooked `CreateDevice` (notes/06), compositing before the real `Present`.
If the split turns out not to be clean, fall back to single-render + post-process
reprojection/warp instead of fighting a tangled call graph.

## Prior milestone (write-hook proof-of-concept session, still valid)

**The write-hook mechanism is proven end-to-end — this is the first behavior-modifying
experiment, and it worked.** A real live write was installed at `BuildViewMatrix`
(`exe+0x292480`): on each hit, `pEye`/`pAt`/`pUp` were read, a right vector was computed
(`normalize(cross(normalize(at-eye), up))`), and `*pEye` was overwritten with `eye + right*40.0`
before letting the real function proceed. A second breakpoint directly on the
`call D3DXMatrixLookAtRH` instruction (`exe+0x2924AC`) re-read `*pEye` immediately before the
call executed, to prove the write wasn't overwritten before use. Result: **40/40 writes
succeeded, 39/39 call-site checks matched the written value exactly, 0 mismatches**, sustained
across ~25 seconds / ~40 frames on the title screen's live camera — not a one-frame flicker. No
crash, no anti-tamper reaction. Full detail, exact values, and one important surprise (below) in
`notes/09-write-hook-proof-of-concept.md`.

**Important surprise**: `*pEye` is a live pointer into the camera's own persistent state, not a
fresh copy each call — writes on one hit partially carry forward into what the next hit on the
same pointer reads as its "original" value. This produced **bounded compounding**: the first few
writes on a given pointer stack close to additively, then the game's own camera smoothing visibly
damps it and the value converges to a stable offset rather than diverging unboundedly or being
cleanly reset. This is a concrete, actionable finding for the stereo work, not just a curiosity —
see the recommendation below.

**Proposed next milestone (the real stereo-rendering step)**: this is the big one and will likely
need its own dedicated deep-dive session. Two parts:
1. **Per-eye matrix math**: duplicate the offset logic proven this session for two eyes with
   opposite-sign offsets, but **cache/derive a fresh unmodified base eye position each frame**
   rather than reading back an already-offset `*pEye` (to sidestep the compounding behavior found
   above) — either by capturing the pre-write value once per real frame and computing both eyes
   from that cached base, or by not mutating the shared pointer in place at all and instead
   calling `D3DXMatrixLookAtRH` (or the `BuildViewMatrix` wrapper) twice into two separate output
   buffers.
2. **Actual dual rendering** (the hard, unscoped part): the game's main render loop needs to run
   *twice* per frame — once per eye — into two separate render targets, then composite. This
   needs its own investigation into how the game's frame/draw-call loop is structured (where
   `Present` is called relative to the full scene draw, whether the render path can be
   re-entered cleanly a second time with a different view/projection matrix and target surface,
   whether any per-frame state would need resetting between the two passes). The already-hooked
   `CreateDevice`/`Present` proxy (`notes/06-createdevice-present-hooks.md`) is the natural place
   to stand up a second render target, but nothing about invoking the render path twice has been
   explored yet.

Revisit the gameplay-input blocker (`notes/08`) in parallel/afterward — real player camera
movement will eventually be needed to validate the stereo hook, not just the title screen's
attract-mode animation.

## Prior milestone (live-camera-data session, still valid)

Both camera-matrix hook points were confirmed carrying real, live, changing data. Breakpoints on
`BuildViewMatrix` (`exe+0x292480`) and `BuildProjectionMatrix` (`exe+0x2924D0`) were hit
repeatedly (15 + 45 hits over ~15s) with real `pEye`/`pAt`/`pUp` vectors that drift smoothly
frame-to-frame (proving a live, moving camera, not a cached one-shot value) and a stable,
plausible `rawFov=104.0` / `aspect=1.3333` (4:3) / `zn=10.0` / `zf=50000.0`. This used the
title/attract screen's own animated 3D camera (a real `D3DXMatrixLookAtRH` scene, unlike the
static post-title menu observed previously) rather than player-controlled gameplay — **reaching
actual gameplay was blocked this session by a simulated-input problem**: `SendInput` (VK and
scan-code), legacy `keybd_event`, and `PostMessage` all failed to dismiss the title screen's
"press any key" prompt, despite confirmed-correct OS-level window focus and a validated-working
injection mechanism (control-tested against Notepad). Root cause narrowed to the game's `DIEmWin`
DirectInput hook-based input path not reacting to synthetic input in this environment — not a
debugger, hook, or config problem (full diagnostic trail in
`notes/08-live-camera-data-gameplay.md`).

## Prior milestone (camera-matrix injection-point session, still valid)

**The camera-matrix injection point is identified with concrete, live-confirmed addresses.**
Two small wrapper functions were fully disassembled — `exe+0x292480` (builds the view matrix,
`BuildViewMatrix(pOut, pEye, pAt, pUp)`, all three vector args passed as pointers straight
through to `D3DXMatrixLookAtRH`) and `exe+0x2924D0` (builds the projection matrix,
`BuildProjectionMatrix(pOut, rawFov, aspect, zn, zf)`, feeding a FOV unit conversion into
`D3DXMatrixPerspectiveFovRH`) — both are the actual hook points for per-eye view/projection
matrix injection. Also resolved, with live evidence: **the game uses the shader-constant
pipeline, not the fixed-function pipeline** — `IDirect3DDevice9::SetTransform` was never called
by the game's own code across ~75 seconds of live observation (zero hits on a real breakpoint at
its resolved address, versus 300+ hits on `SetVertexShaderConstantF` and 40+ `Present` frames in
the same window); the `SetTransform` hits that did occur came from the D3D9 runtime's own
`CreateDevice` bootstrap, not the game. The specific `SetVertexShaderConstantF` call/register
range carrying the actual camera matrix wasn't pinned down yet — the observation window was the
main menu (no active 3D camera), so `D3DXMatrixLookAtRH` never fired live and the
`SetVertexShaderConstantF` hits observed all looked like 2D UI/screen-space constants from one
call site (`exe+0x27EF03`). Full detail, full disassembly listings, and the concrete next step
(reach real gameplay, watch for a `Vector4fCount=4` constant upload) are in
`notes/07-camera-matrix-injection-point.md`.

**Proposed next milestone**: get the debugger past the main menu into actual gameplay (simulated
input or attach-after-manual-launch) and repeat the `SetVertexShaderConstantF` observation to
find the exact call site/register range for the camera matrix upload — that's the second,
possibly primary, injection point alongside the two wrapper functions already found. In
parallel/afterward, use the already-hooked `CreateDevice` (`notes/06-createdevice-present-hooks.md`)
to create a second render target, purely to prove a second surface can be stood up against this
device/driver — still no compositing/stereo logic yet, just infrastructure.

## Prior milestone (CreateDevice/Present vtable-hook session, still valid)

The proxy `d3d9.dll` (`tools/proxy-d3d9/`) **vtable-hooks `IDirect3D9::CreateDevice` (slot 16)
and `IDirect3DDevice9::Present` (slot 17)**, in addition to the previously-validated
`Direct3DCreate9` forwarding — still pure observation, every hook calls straight through to the
real implementation and returns its result unmodified. Both slot indices were cross-checked two
ways (counting fields in mingw-w64's own `d3d9.h` vtbl structs, and the prior live x64dbg session
that read the same slots out of live process memory) and patched by assigning the named vtbl
struct fields (`lpVtbl->CreateDevice = ...`, `lpVtbl->Present = ...`) rather than raw pointer-index
math, so the compiler — not manual offset arithmetic — guarantees the correct slot. Live-validated
by copying into the game dir and launching `Psychonauts.exe` directly: `CreateDevice` fired once
with full `D3DPRESENT_PARAMETERS` logged (640×480 windowed, `D3DFMT_A8R8G8B8` backbuffer,
`D3DFMT_D24S8` depth/stencil, `BehaviorFlags=0x46` matching the live-debug session exactly), and
`Present` fired repeatedly at a steady ~30 fps (frame counter `1→29→59→89→119→149` across six
~1-second-apart throttled log lines) — proving the per-frame hook point is real and durable, not a
one-shot artifact. Test DLL removed from the game directory and the game process killed
immediately after validating, exactly as before. Full detail:
`notes/06-createdevice-present-hooks.md`.

## Prior milestone (proxy-DLL validation session, still valid)

The minimal logging proxy `d3d9.dll` was **built and validated end-to-end** as plain
load-and-forward (no vtable hooking yet at that point). No C/C++ compiler existed on this machine;
installed LLVM-MinGW (`winget install MartinStorsjo.LLVM-MinGW.UCRT`) to get an
`i686-w64-mingw32-clang` 32-bit target compiler (game is 32-bit). The real `Direct3DCreate9`
address resolved by the proxy (`d3d9.dll base + 0x64B20`) exactly matched the offset independently
found via x64dbg in the live-debug session — good cross-confirmation. Full detail:
`notes/05-proxy-dll-validation.md`.

## Prior milestone (live-debug session, still valid)

x64dbg, a real Python 3.12, the `x64dbg_automate[mcp]` pip package, and the
(separately-downloaded, not bundled) x64dbg-automate debugger plugin are all installed and
working. First live/dynamic analysis pass **fully confirmed the static-recon hook plan**:
`Direct3DCreate9` (`d3d9.dll+0x64B20`) → `IDirect3D9::CreateDevice` (`d3d9.dll+0x6F750`, called
from `exe+0x27BD52` with `D3DCREATE_HARDWARE_VERTEXPROCESSING`) → `IDirect3DDevice9::Present`
(`d3d9.dll+0xE6120`, called from `exe+0x27E755`) were all hit live with a debugger and their
addresses read from real process memory, not guessed. Camera matrix call sites
(`D3DXMatrixPerspectiveFovRH` / `D3DXMatrixLookAtRH`, both called from the `exe+0x2925xx`
region) were also located. No anti-debug/anti-tamper behavior encountered. Full detail, exact
addresses, and two non-obvious debugger gotchas (default entry breakpoint; first-chance-AV loop
when launching outside Steam under x64dbg) are in `notes/04-live-debug-findings.md`.

## Summary

First recon pass complete. Workspace stood up, both trusted tools installed, key prior-art
resources read and summarized, and read-only static analysis of `Psychonauts.exe` performed.
No changes made to the game install. Findings line up well: this looks like a tractable
in-process D3D9 hook + shader patch project, closely following the path Helix Mod already
proved works for this exact game in 2013.

## What's set up

- Workspace: `C:\Users\Tefa\Documents\PsychonautsVR\` with `notes/`, `tools/` (empty, reserved),
  `recon/` (raw PE dump).
- Git installed (was missing; required by the plugin marketplace clone mechanism — installed
  via winget, standard low-risk dev tool, not the "third-party debugger" caution).
- **x64dbg-skills** plugin installed & enabled (`claude plugin marketplace add
  dariushoule/x64dbg-skills` + `claude plugin install`). **Now fully usable**: x64dbg, x64dbg
  Automate (debugger plugin), a real Python 3.12, `x64dbg_automate[mcp]`, and the MCP server
  registration are all installed and verified — see `notes/04-live-debug-findings.md` for the
  install log and the first successful live-debug pass. The MCP tools need a Claude Code
  restart to appear (registered mid-session); the raw Python client works immediately.
- **superpowers** plugin installed & enabled from the official marketplace, fully usable now.
  `SUPERPOWERS_DISABLE_TELEMETRY=1` set in the global `~/.claude/settings.json` `env` block.

Full detail: `notes/01-tooling-setup.md`.

## What was learned from prior art

- **Helix Mod's 2013 fix is the strongest signal**: someone already got per-eye stereo working
  on this exact game by patching a small, identifiable family of shaders (sky/celestial
  objects). Proves the shader pipeline is patchable and that once fixed, convergence/depth are
  freely controllable.
- **dxwrapper's `d3d9.dll` stub-replacement mechanism is directly applicable** — the game
  imports `d3d9.dll` by plain name with one call (`Direct3DCreate9`), no Ex variant, nothing in
  the game folder to conflict with a proxy DLL.
- Jill Crungus's blog is confirmed to cover Lua scripting + ASD format + level construction;
  independently corroborated by our own recon (the exe has `.dflua`/`.dfluatx` PE sections).
  Deeper read deferred until we need entity/camera data, not just the rendering hook.
- RayCarrot/PsychonautsStudio: thin, WIP, low value for this phase.
- Brobert-in-aus's two guides are generic Godot/OpenXR external-host porting methodology, not
  Psychonauts-specific — useful only as a fallback architecture if in-process shader hooking
  hits a wall; the in-process path is better supported by actual prior art for this game.

Full detail: `notes/02-technical-leads.md`.

## Static recon findings (Psychonauts.exe, read-only)

- 32-bit PE, VS2008/linker 9.0, Steam-era patched build (timestamp 2016, not the original 2005
  build — pulls in steam_api/XInput/DirectInput8).
- `d3d9.dll` import: exactly `Direct3DCreate9`, plain D3D9 (no Ex) — confirms `CreateDevice` →
  `Present`/`SetVertexShaderConstantF` as the hook points, and confirms the stub-DLL injection
  approach is viable.
- `d3dx9_40.dll` imports include `D3DXCompileShader`, `D3DXAssembleShader`,
  `D3DXGetShaderConstantTable` — real shader pipeline, not fixed-function, matching how Helix
  Mod's fix operated. Also imports `D3DXMatrixPerspectiveFovRH`/`D3DXMatrixLookAtRH` — likely
  call sites for injecting per-eye projection/view matrices.
- No packing/anti-tamper signals in the import table; everything is plaintext standard Win32/
  D3D9/MSVC runtime names.

Full detail + raw dump: `notes/03-static-recon.md`, `recon/psychonauts_exe_imports.txt`.

## Blocker (resolved 2026-08-16, live-debug session)

~~x64dbg-skills needs x64dbg + x64dbg Automate + a working Python 3 install~~ — all installed
and verified working this session (x64dbg 2026.05.27, Python 3.12.10, `x64dbg_automate[mcp]`
0.9.2, plus the separately-downloaded x64dbg-automate debugger plugin the README doesn't
mention as a distinct download). See `notes/04-live-debug-findings.md` for the full install log
and the live analysis results. Superseded by "Latest status" at the top of this file.

## Proposed next milestone

See "Latest status" at the top of this file for the current one — this section is left as
historical context from the first live-debug session; each subsequent session's "Latest status"
supersedes it.
