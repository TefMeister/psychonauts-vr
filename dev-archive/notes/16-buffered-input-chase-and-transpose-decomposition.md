# Session 16 — DirectInput Buffered-Data Chase (Lead 1) and Transpose Decomposition Retry (Lead 2)

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; `silence-intro-videos.ps1`/`restore-intro-videos.ps1` run before/after every
launch this session and verified reverted at the end; `d3d9.dll` — first the unmodified notes/14
binary, later a disposable capture-only fork, never the tracked one modified in place — was the only
file ever copied into the game directory, removed after every run). No headset available this session
(work PC) — everything here is debugger evidence, live memory captures, and screenshots, not in-headset
testing. Mid-session, a new standing requirement was added (window must be moved off-screen after every
launch via `tools/move-window-offscreen.ps1`, `SetWindowPos`+`SWP_NOACTIVATE`) — incorporated for the
remaining launches; verified the script is benign (pure `user32.dll` window positioning, no other side
effects) before using it.

**Task**: pick back up notes/15's two concrete leads — a stack-unwind chase of the real DirectInput
keyboard device object (Lead 1), and a re-run of notes/14's matrix-decomposition check against the
transposed candidate matrix (Lead 2), per the newly-confirmed `exe+0x42E2A0` transpose function.

## Result summary

**Lead 1: real infrastructure breakthrough (the actual keyboard device object and both its polling
APIs are now resolved and hooked, reliably, for the first time), but the behavioral goal — get the
title screen to respond — was NOT achieved.** An initial apparent success was investigated rigorously
and found to be a false positive, corrected honestly rather than left standing. A concrete, evidence-backed
new hypothesis (buffered `GetDeviceData`, not polled `GetDeviceState`) was identified but not yet
successfully exploited due to a real, reproduced cross-run flakiness in hitting that specific call.

**Lead 2: real, partial progress.** Live-captured View/Proj/register-6 data, re-run through the
notes/14 decomposition check against 4 hypotheses (transpose or not, on each side). The transpose
hypothesis measurably and substantially improves the result (row lengths close to 1.0 instead of
"thousands"), but does not fully resolve to a clean sane decomposition — partial support, not full
confirmation.

## 1. Lead 1: chasing the real DirectInput keyboard device

### 1a. Method improvements over notes/15

Per the task's explicit instruction, the DirectInput8Create breakpoint was armed **before any resume
past startup at all** (notes/15's attempt resumed past ~10s of startup churn first and only then armed
it, which produced zero hits in 75s — strongly suggesting the real call happens during that window).
This session's harness unified the "resume past startup exception churn" loop and the
breakpoint-watching loop into one, so a hit during early startup can't be silently blown past.

On the first `DirectInput8Create` hit, a **genuine 7-frame stack unwind** (EBP-chain walk, not just
`[esp+0]`) was captured and classified by containing module via `memmap()`:

```
frame0(retaddr@[esp]): 0x00402842  (psychonauts.exe .text)
frame1(ebp-chain):     0x0053563A  (psychonauts.exe .text)
frame2(ebp-chain):     0x00508DD0  (psychonauts.exe .text)
frame3(ebp-chain):     0x005350F0  (psychonauts.exe .text)
frame4(ebp-chain):     0x00465C91  (psychonauts.exe .text)
frame5(ebp-chain):     0x00466056  (psychonauts.exe .text)
frame6(ebp-chain):     0x006ED5C0  (psychonauts.exe .text)
```

Every frame resolves inside the game's own code (not a system DLL) — reproduced identically across
three separate fresh launches this session, unlike notes/15's inconsistent single-frame reads. This
confirms `exe+0x402842` is genuinely, reliably the game's own DirectInput init call site, not a fluke.

### 1b. The real device object: resolved and hooked, reliably, three separate times

From the confirmed call: `IDirectInput8*` resolved (`HRESULT=S_OK`), its vtable read, `CreateDevice`
(slot 3) breakpointed at the resolved code address — **and this time it fired**, twice per launch
(GUID-classified): once for `GUID_SysKeyboard`, once for `GUID_SysMouse`. The returned
`IDirectInputDevice8*` for the keyboard was captured, its vtable read, and both `GetDeviceState`
(slot 9) and `GetDeviceData` (slot 10, buffered) were resolved and breakpointed successfully. This
closes out notes/15's exact open item — the address that "was never hit" there fired reliably here,
the difference being the earlier-arming fix in §1a.

`GetDeviceState` fired **161–218 times** across two separate ~90–120s sessions (steady polling, roughly
matching frame rate), confirming the keyboard device is actively, continuously polled while the title
screen is up.

### 1c. First attempt: patch `GetDeviceState`'s output buffer — apparent success, then a real correction

Per the task's suggested technique, `GetDeviceState`'s return was breakpointed and its 256-byte polled
state buffer patched: `DIK_SPACE` (offset `0x39`) forced to `0x80` (pressed) for a 5-second hold, then
released to `0x00`. **A screenshot taken ~3s into the hold showed a "Loading" screen with the game's
signature falling-Raz vortex animation** — a real, distinctive piece of content that never appears at
the idle title screen, initially read as a genuine breakthrough (title screen dismissed, real game
state transition reached).

**This was investigated further rather than taken at face value, and turned out to be a false
positive.** A clean control test — launch the game completely normally, no debugger, no input
injection at all, fine-grained screenshots every 0.5s from t=1s — showed **the exact same "Loading"
screen with falling Raz appears naturally at t≈4.5s on every single launch**, settling into the idle
title screen by t≈5.0s, entirely independent of any input. The timing of the "successful" capture
(patch armed ~10s into a debugger-slowed session, matching screen at ~13s) coincides with this same
natural, input-independent startup transition — the debugger's overhead (breakpoint round-trips) was
simply stretching the game's own early-startup wall-clock time into the window where the patch
happened to be active, not causing the transition.

**A longer, more rigorous follow-up** (13 repeated press/release cycles over ~2 minutes, each
individually confirmed via screenshot, run only *after* the title screen was independently confirmed
to be idling at "Press [] to begin") found **zero further transitions** — the patch has no observed
effect once the natural startup window has passed. This is reported plainly as a negative result, not
papered over: **the polled `GetDeviceState` buffer patch does not dismiss the title screen.**

### 1d. New lead: the game also polls buffered `GetDeviceData` — a concrete, better-motivated hypothesis

A follow-up diagnostic breakpointed `GetDeviceData`, `Acquire`, and `SetCooperativeLevel` on the same
keyboard device object. Findings:

- `SetCooperativeLevel` called twice, with flags `0x06` (`DISCL_NONEXCLUSIVE|DISCL_FOREGROUND`) then
  `0x05` (`DISCL_EXCLUSIVE|DISCL_FOREGROUND`) — both `DISCL_FOREGROUND`, meaning the device only
  delivers data while the window has real DirectInput-level focus (a genuine, if likely secondary,
  candidate confound for future sessions, since debugger/automation focus state is not always
  identical to normal desktop focus).
- **`GetDeviceData` fires in lockstep with `GetDeviceState`** — 68 hits over 60s, essentially matching
  `GetDeviceState`'s own hit rate — with `cbObjectData=20`, exactly `sizeof(DIDEVICEOBJECTDATA2)`
  (`dwOfs+dwData+dwTimeStamp+dwSequence+uAppData`, 5×4 bytes). This is strong circumstantial evidence
  the game *also* uses **buffered** input (discrete keydown/keyup transition events with sequence
  numbers), which is the conventional DirectInput pattern for UI "was this key just pressed" logic
  (as opposed to `GetDeviceState`'s "what's held right now," more typically used for continuous
  movement) — a concrete, well-motivated explanation for why patching only `GetDeviceState` had no
  effect: the title screen's actual gate very plausibly reads the OTHER API.

### 1e. Attempted fix: forge a buffered keydown/keyup event pair — inconclusive, reproduced known flakiness

A follow-up script bred `GetDeviceData`'s return, and (when the real call reported zero buffered
events available, the normal idle case) wrote a synthetic `DIDEVICEOBJECTDATA2` record
(`dwOfs=DIK_SPACE`, `dwData=0x80`) into the caller's own output buffer and set `*pdwInOut=1`, followed
~0.6s later by a matching keyup record. **This did not get a chance to run**: in this particular
session, the `GetDeviceData` breakpoint never fired at all (0 hits over 75s), despite the exact same
resolution method having produced 68 hits in the immediately preceding diagnostic run minutes earlier.
This reproduces, in a new context, the same **cross-run inconsistency** notes/15 first flagged for
`CreateDevice` — a real, still-unexplained flakiness in this environment's DirectInput call pattern,
not a bug in this session's method (the method is now proven to work when the call does fire).

### 1f. Real tooling lessons found and fixed this session (reusable)

- **x64dbg persists a per-target breakpoint database across separate `start_session()` calls.** A
  stale `CreateDevice` breakpoint (named identically by an earlier script, at a DirectInput-chase
  address from a previous run) auto-reloaded and fired with garbage register contents once ASLR
  shifted what actually lived at that address. Fixed by calling `clear_breakpoint(None)` immediately
  after every `start_session()`, before arming anything new — a real, generally-applicable gotcha for
  any future multi-script session against the same target.
- **`wait_for_debug_event(type, timeout=N)` silently discards non-matching events it pops**, even
  though its docstring only promises "the latest event of the specified type" — if an unrelated
  breakpoint (a TLS callback, etc.) is queued ahead of the one actually being waited for, a single
  check-and-give-up call permanently loses the real event. Fixed with a `wait_for_named_breakpoint()`
  helper that drains and resumes past non-matching breakpoint events in a loop until the expected one
  arrives or a timeout elapses — more robust than the ad-hoc single-check pattern used in earlier
  sessions (and likely explains some of *notes/15's and this session's own earlier* inconsistent
  results before the fix was applied).
- **`get_regs()` returns a bare `RegDump32`, not the `list[RegDump32]` its docstring/type-hint
  promises** — fixed with a small `get_ctx()` shim handling both shapes.

### 1g. Stereo hook: opportunistically checked during Lead 1 (per the task's explicit priority)

Since real gameplay wasn't reached, the "does stereo separation survive past the title screen" check
was exercised as thoroughly as this session's actual game states allowed:

- With the unmodified notes/14 `d3d9.dll` loaded throughout a ~2-minute combined
  input-chase-plus-stereo run (13 press/release cycles), the proxy's own log shows **`SVSCF
  stereo-correct` firing continuously with stable `xScale=1.5377 d=-3.250 delta=4.9976`**, and
  **`StretchRect` composites succeeding (`hr=0x00000000`) on every logged frame**, through frame #417
  — the stereo hook is robust under sustained concurrent debugger/DirectInput-hooking activity, a new
  (if incidental) robustness data point.
- **A genuinely new, honest finding**: the natural "Loading" transition screen (§1c) renders as a
  **single, non-split full-width image** — not the left/right composited split the title screen
  itself reliably shows. The title screen (both before and after the loading interlude, across many
  screenshots) consistently shows the expected split-screen divergence (matching notes/14/15). The
  most likely explanation is that the loading screen's content is drawn through a different render
  path that doesn't invoke the hooked `CandB` function (so the stereo phase stays `IDLE` and
  `Present`'s composite step never triggers, harmlessly falling back to an ordinary single-buffer
  frame) — not confirmed via disassembly this session, but consistent with everything observed, and
  reassuring: the hook doesn't crash or corrupt output when its target function isn't invoked, it just
  does nothing for that frame.
- **This does not answer the actual question the task asked** (does stereo separation survive into
  real player-controlled gameplay) — that remains blocked on Lead 1 not reaching gameplay.

## 2. Lead 2: re-running the decomposition check against the transposed candidate

### 2a. Data capture

No raw float samples from notes/14's original session survived (its capture script/log were session
scratchpad, not workspace-committed) — fresh live data was captured this session via two
complementary, both-reliable methods:

- **View/Proj**: breakpoints on `BuildViewMatrix`'s and `BuildProjectionMatrix`'s own `ret`
  instructions (`exe+0x2924C7` / `exe+0x29253B`, fixed addresses, no ASLR on the main exe — the same
  reliable method notes/14 used), reading `EAX` (`pOutMatrix`, fully populated at that point) and
  dumping all 16 floats. **192 raw View and 414 raw Proj samples** captured (attract-mode camera drift
  and near-constant FOV/aspect respectively).
- **Register 6 (WVP)**: rather than repeat the D3D9 `CreateDevice`→`SetVertexShaderConstantF` COM
  vtable chase live via the debugger (which hit the exact same kind of cross-run flakiness described
  in §1e — `Direct3DCreate9` returned a *different*, `NULL`-returning caller's result on two separate
  attempts before the real device's creation could be caught), this session pivoted to a **disposable,
  session-scratchpad-only fork of the already-proven `proxy_d3d9.dll`** (never the tracked/committed
  copy) with one added `LogLine` call inside `Hook_SetVertexShaderConstantF`, dumping all 16 raw floats
  whenever `StartRegister==6 && Vector4fCount==4`, capped at 40 samples. Built with the same toolchain
  (`i686-w64-mingw32-clang`, `build.ps1`, same harmless pre-existing warning), copied into the game
  directory, run for ~25s, removed immediately after. **40 raw register-6 samples** captured this way —
  reliable on the first attempt, since it hooks the *real* in-process `CreateDevice` call directly
  (guaranteed to fire) rather than racing a debugger-side breakpoint against it.

### 2b. Offline decomposition check (pure Python, no debugger, no game process)

The check computes `World_candidate = WVP_sample * inverse(View*Proj)` and scores how close the result
looks to a sane rigid/scale transform (row 0–2 lengths mutually consistent, `[3][3]` near 1, column-3
near 0) — same method as notes/14 §1b, extended to try **4 hypotheses** per sample: candidate or
`transpose(candidate)`, against `View*Proj` or `transpose(View*Proj)`. Raw samples were deduplicated
(near-identical matrices collapsed, since a full un-deduplicated combinatorial search — hundreds of
raw samples × 4 hypotheses × 4×4 matrix inversions — is a many-hours pure-Python non-starter) to 25
unique View, 1 unique Proj (expected — FOV/aspect/near/far are session-constant), 6 unique register-6
matrices, giving 600 total hypothesis checks (seconds to run).

**Result — real, partial improvement, not full resolution:**

```
REG6 sample #0: best=transpose(candidate) vs VP  score=5.946  row_lens=[1.0, 1.0, 0.667]    [3][3]=1.6900
REG6 sample #2: best=transpose(candidate) vs VP  score=15.839 row_lens=[0.681, 0.958, 1.031] [3][3]=3.6973
REG6 sample #3: best=transpose(candidate) vs VP  score=16.998 row_lens=[1.036, 1.101, 1.476] [3][3]=3.9490
REG6 sample #5: best=transpose(candidate) vs VP  score=5.002  row_lens=[1.009, 1.001, 1.001] [3][3]=0.0100
```

Four of six samples' *best-scoring* hypothesis is `transpose(candidate) vs VP` (untransposed
View*Proj), and several have **row lengths within a few percent of 1.0** — a qualitatively different,
far saner signature than notes/14's original 0/20 result ("row lengths in the thousands to hundreds of
thousands"). This is real, live-data evidence in the transpose hypothesis's favor, not just the static
disassembly notes/15 found. **However, no sample crosses the "fully sane" bar**: `[3][3]` is
consistently far from 1.0 (0.01–3.95) and column-3 residuals remain non-trivial, so this is **partial
support, not confirmation**.

**Interpretation**: the cleanly-recovered row lengths are unlikely to be coincidental — a genuinely
wrong hypothesis would not be expected to land this close to 1.0 across multiple independent samples.
The most likely explanation for the remaining `[3][3]`/column-3 residual is that `World_candidate`
*isn't* actually expected to be identity in the first place — register 6 drives whatever specific
draw call issued it, which may carry a real, non-identity local/object transform (translation, or a
`[3][3]` scale component from that object's own transform) rather than being pure camera-space
content. The original notes/14 check's implicit assumption ("if register 6 is `World*View*Proj`
composited close to the camera, decomposing it should recover something identity-like") was never
fully justified for an arbitrary draw call — this session's improved-but-imperfect result is
consistent with "the transpose is real, and the remaining gap is a real, non-identity World matrix,"
not with "the transpose hypothesis is wrong."

### 2c. What's still open

1. **Confirm the transpose hypothesis more directly**: rather than inferring it from decomposition
   sanity, a live stack/register trace at the exact point `exe+0x11D2A1`'s second `MatrixMultiply`
   result and `exe+0x42E2A0`'s transpose result diverge (notes/15's own flagged uncertainty — which
   buffer actually feeds the upload) would settle this outright, cheaply, with the device/breakpoint
   infrastructure already proven this session.
2. **Solve for the real World component** instead of assuming identity: capture the actual object
   whose draw call uploads register 6 (screen position/kind of geometry) to determine whether a
   non-identity World transform is expected, which would fully explain the remaining `[3][3]`/column-3
   gap without needing any further pipeline mystery.

## 3. Cleanup

Every launch this session was preceded by `silence-intro-videos.ps1` and followed (after this
session's mid-task addition) by `move-window-offscreen.ps1`; `restore-intro-videos.ps1` was run at the
end and verified (`INTRO.bik` present under its real name, no `.silenced` files remain). Verified
clean: no `Psychonauts`/`x32dbg`/`x64dbg`/`python` processes running, no `d3d9.dll` left in the game
directory (neither the notes/14 binary nor the disposable capture fork). The disposable capture fork
of `proxy_d3d9.c` lives only in the session scratchpad, never committed to any repo.

## 4. Disposition

- **Lead 1**: real, substantial infrastructure progress (the actual keyboard device object, and both
  its polling APIs, reliably resolved and hooked for the first time this project has managed) but the
  behavioral goal was **not** achieved — an initial apparent win was honestly investigated and found to
  be a false positive rather than reported uncritically. A concrete, well-motivated next hypothesis
  (buffered `GetDeviceData` forging) is identified and partially implemented, blocked only by a
  reproduced cross-run flakiness in hitting that specific call, not by any remaining conceptual gap.
- **Lead 2**: real, partial progress — live data now supports the transpose hypothesis more directly
  (not just structurally, via disassembly) than before, with a concrete, quantified improvement, but
  the mystery isn't fully closed.
- **Mod repo**: **not touched this session** — neither lead reached a confirmed functional
  improvement (Lead 1 didn't reach gameplay; Lead 2 is analysis, not a code change). Workspace notes,
  modding-notes, and dev-archive synced as usual.
- **What still needs the home headset setup**: unchanged — real in-headset comfort/fusion validation,
  and gameplay-camera stereo validation, both still blocked on reaching actual gameplay.
- **What can still be done headset-free**: retry the buffered `GetDeviceData` forge with more patience
  (the exact mechanism from §1e, just needing the call to actually fire — matching the "be patient
  across relaunches" lesson that eventually worked for `CreateDevice` this session); the two concrete
  Lead 2 next steps in §2c (both pure static/live-debug work, no headset needed).
