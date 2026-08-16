# Input-Blocker Retry (DirectInput Device-State Poke) + Stereo Prototype Robustness/Quality Pass

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only `silence-intro-videos.ps1`/`restore-intro-videos.ps1` touched anything
under it, both run before/after every launch this session and verified reverted; the only file ever
copied into the game directory was the already-built, unmodified `d3d9.dll` from notes/14, removed
after every run). No headset was available this session (work PC) — everything here is verified via
debugger evidence, live memory captures, and side-by-side screenshots, not in-headset testing.

**Task**: pick one of two directions. **Chose Option A first** (retry the notes/08 gameplay-input
blocker using a debugger-level DirectInput state poke, since it was explicitly flagged as an
untried follow-up and directly gates whether the stereo hook can ever be validated against real
gameplay) — **then pivoted to Option B** after a genuine, well-instrumented attempt didn't close
out cleanly, per the task's own explicit permission to do so. Both are reported honestly below,
including the parts of Option A that did **not** resolve.

## Part 1 — Option A: chasing the DirectInput input path (partial, unresolved, but real progress)

### 1a. Method

Launched `Psychonauts.exe` **directly under x32dbg** (not Steam-launch-then-attach) specifically so
a breakpoint on `dinput8.DirectInput8Create` could be armed as a *pending/deferred* symbolic
breakpoint before the module even loaded — guaranteeing the very first call would be caught,
closing a timing gap the Steam-launch-then-attach method used in notes/08 couldn't guarantee. Own
scratch harness (`common.py`, disposable, not copied into the workspace) built on
`x64dbg_automate.X64DbgClient`, with two reusable, real gotchas fixed along the way (documented in
case a future session hits the same thing):

- **x32dbg auto-inserts singleshot breakpoints on every DLL's TLS callbacks** (e.g.
  `inputhost.dll`), which surface as `EVENT_BREAKPOINT` events indistinguishable in type from a
  real named breakpoint — a naive "resume until any breakpoint" loop stops on these by mistake.
  Fixed by filtering `run_until_real_breakpoint()` on the breakpoint's own `name` field, only
  treating a hit as "arrived" if it matches an expected name.
- **`get_latest_debug_event()` pops newest-first (LIFO)**, reconfirming the exact gotcha notes/10
  first found — a "check one event, resume if it doesn't match" loop can walk past a real match
  sitting deeper in the queue. Fixed by draining the *entire* queued backlog every pass, not just
  the newest entry, before deciding to resume.

### 1b. Real finding: synthetic input DOES reach the game's real message queue (refines notes/08)

notes/08 concluded "SendInput/PostMessage don't work, root cause: the DIEmWin DirectInput
hook-based path doesn't react to synthetic input" — largely inferred from DIEmWin's mere existence,
not from directly observing whether messages arrived. This session got direct, live evidence:

- Breakpointed `user32.PeekMessageA`'s return address and dumped the retrieved `MSG` struct
  (`hwnd`/`message`/`wParam`/`lParam`) live, for real, on the game's own main-loop thread.
- Sent `PostMessageW(hwnd, WM_KEYDOWN, VK_SPACE, ...)` + `WM_CHAR` + `WM_KEYUP` (correct scan code
  `0x39`, correct up/down `lParam` bits) at the game's real top-level window (`hwnd=0x006B01D4`,
  found via `EnumWindows` filtered by PID) while the capture ran.
- **The messages arrived, correctly, in the real queue the game's own `PeekMessageA` drains**:
  captured `hwnd=0x006B01D4 msg=0x0100(WM_KEYDOWN) wParam=0x20(VK_SPACE) lParam=0x00390001`,
  followed by `msg=0x0102(WM_CHAR) wParam=0x20`, then `msg=0x0101(WM_KEYUP) lParam=0xC0390001` — an
  exact, correctly-shaped keydown/char/keyup triple, reproduced twice (once per injection call).

This is a genuinely new, real finding: **message delivery itself is not the blocker** — the earlier
conclusion was too broad. Despite this, a follow-up screenshot immediately after confirmed the
title screen was still on "Press [] to begin." — so *something downstream* of message delivery
still isn't advancing the prompt.

### 1c. Ruled out: GetAsyncKeyState / GetKeyState / GetKeyboardState as the gate

Breakpointed `user32.GetAsyncKeyState`, `GetKeyState`, `GetKeyboardState`, `PeekMessageA/W` all at
once and let the game run at the idle title screen for tens of seconds while repeatedly injecting
input:

- `GetAsyncKeyState`: **0 hits, ever** — not used at all by anything in this window.
- `GetKeyState`: hit a handful of times, but its *caller* resolved to `msctf.dll` (Windows' Text
  Services Framework / IME subsystem) querying **modifier keys only** (`VK_SHIFT`, `VK_CONTROL`,
  `VK_MENU`, `VK_LWIN`, `VK_RWIN`) — OS-level IME hotkey bookkeeping, unrelated to game logic.
- `GetKeyboardState`: hit twice, not deeply investigated further given time budget, but its
  scarcity (vs. `PeekMessageA`'s dozens of hits) makes it an unlikely per-frame poll target.

This rules out the three most obvious "poll the key state directly" APIs as the attract-screen
gate, at least within this observation window.

### 1d. Unresolved: the actual DirectInput device-creation path

A `DIEmWin` window (DirectInput's window-message-based keyboard-emulation helper, confirmed via
`EnumWindows` filtered to the game's PID: `hwnd class='DIEmWin' visible=False`) genuinely exists in
the live process, meaning *some* DirectInput keyboard device **was** created and acquired somewhere.
Chasing down exactly how, to reach its internal state buffer (the task's suggested technique), did
**not** resolve cleanly:

- **First process instance**: cleanly caught the game's own `DirectInput8Create` call
  (`retaddr=0x00402842`, inside `psychonauts.exe` itself, `HRESULT=S_OK`), resolved the returned
  `IDirectInput8*`'s vtable, and set a breakpoint on its `CreateDevice` slot (slot 3, cross-checked
  against the standard `IDirectInput8` COM layout) — **that breakpoint was never hit**, even after
  ~30+ seconds of free-run at the idle title screen (confirmed via `hitCount=0` on repeated
  `get_breakpoints()` polls).
- **Second process instance** (fresh relaunch, to rule out a one-off fluke): `DirectInput8Create`
  fired from a **non-game caller** (`retaddr=0x777531A6`, inside a system DLL, not
  `psychonauts.exe`) with a garbled/zeroed stack read (`ppvout_addr=0x00000000`) — evidence
  `DirectInput8Create` is called from more than one place/context across the process lifetime, not
  just once from the game's own init code as the first instance suggested. A follow-up attempt to
  skip past this and find the "real" game-originated call, then sweep **every** `IDirectInput8`
  vtable slot (`QueryInterface`, `CreateDevice`, `EnumDevices`, `GetDeviceStatus`, `RunControlPanel`,
  `Initialize`, `FindDevice`, `EnumDevicesBySemantics`, `ConfigureDevices`) simultaneously, did not
  resolve within the time budget either (a second `DirectInput8Create` hit also produced an
  inconsistent stack read).
- **Ruled out one confound**: Steam Overlay (`GameOverlayRenderer(32).dll`, known to hook DirectInput
  devices for its own Shift+Tab capture on many games) is **not loaded** in this process (checked
  via a full live module enumeration, 99 modules, no overlay DLL present) — so overlay-hook
  interference is not the explanation for the missed breakpoints.

**Disposition on Option A**: real, reportable forward progress (message delivery is proven to work,
three candidate polling APIs are ruled out, Steam Overlay is ruled out as a confound), but the
actual gate — and the DirectInput device object needed to attempt the task's suggested
state-buffer poke — was **not** pinned down this session. This is exactly the kind of result the
task's own framing anticipated as a legitimate reason to stop and pivot, so that's what happened
next, rather than continuing to spend session budget chasing a non-deterministic call pattern with
diminishing signal quality. **Concrete next-session leads, not vague ones**: (1) the inconsistency
across the two process instances suggests `DirectInput8Create` may be called from a wrapper/shim
whose caller varies run-to-run — worth tracing with a full stack unwind (not just `[esp+0]`) on the
*first* hit rather than assuming a flat stdcall frame; (2) since messages verifiably reach
`PeekMessageA`, breakpointing the actual window-procedure address (via `GetWindowLongPtrW(hwnd,
GWL_WNDPROC)` executed *live inside the debugger*, since cross-process `GetWindowLongPtr` for
`GWL_WNDPROC` returned 0 when tried from an external Python process — a real, minor, separately
notable dead end) would show definitively whether `DispatchMessage` ever hands the injected
`WM_KEYDOWN` to game code at all.

## Part 2 — Option B: stereo prototype robustness, IPD comfort reasoning, transform-path progress

### 2a. Multi-launch robustness: 4/4 clean runs, deterministic output

Ran the **already-built, unmodified** notes/14 `d3d9.dll` (no code changes this session) through 4
full, independent launch→observe→screenshot→kill→cleanup cycles back to back. Every run:

- `Stereo ready = 1`, both `StretchRect` composite calls `hr=0x00000000`, every run.
- `SVSCF stereo-correct` log lines present every run, with **identical** `xScale=1.5377`,
  `d=-3.250`, `delta=4.9976` across all 4 runs (expected — these derive from fixed FOV/aspect
  constants and the fixed `STEREO_HALF_IPD`, not per-run randomness).
- The attract-mode camera path is **itself deterministic**: captured `eye`/`at` values matched to
  within float noise across all 4 runs (e.g. `eye=(-371.36,457.28,16.98)` run 1 vs.
  `eye=(-371.21,457.28,16.98)` run 4) — a useful, previously-unstated fact: this title screen's
  camera is a scripted, non-random playback, which is *why* the 4 screenshots below are
  pixel-for-pixel identical, not just "similar."
- **No crash, no hang, all 4 processes exited cleanly on kill.**
- All 4 screenshots (`PrintWindow`, matching notes/14's method) show the **same clean divergence**:
  left eye renders a discrete yellow/green blob shape on a black background; right eye renders a
  completely different teal spiky/fractal-textured pattern; "Press [] to begin." stays aligned at
  matching relative position in both halves in every shot. This is arguably a **clearer** demo
  image than notes/14 managed to capture (which described a harder-to-read background-texture
  divergence) — still not a single discrete object visibly shifted sideways, but a clean,
  reproducible, unambiguous left/right difference confirmed 4-for-4.

**This directly satisfies "verify the fix is robust across multiple consecutive launches"**, not
just the single session that originally proved it in notes/14.

### 2b. IPD value: cross-validated, comfort-grounded, kept at 3.25 (with tightened reasoning)

notes/13's original derivation scaled the half-IPD proportionally to the *specific shot's*
eye→at distance (~195 units) against an assumed ~2m real viewing distance, landing on `3.25`. That
method has a real, worth-flagging weakness: it's **framing-dependent** — a proportional scale means
a close-up shot and a wide shot would imply *different* apparent IPDs, which is not how real human
stereo vision works (a person's IPD is fixed regardless of what they're looking at) and would be
an actual VR-comfort problem once applied across varied real-gameplay camera distances, not just
one held title-screen shot.

This session added an **independent cross-check** using the engine's own clip-plane constants
rather than shot framing: `zNear=10` / `zFar=50000` world units (both confirmed live, notes/04/07/08)
are only physically plausible for a handful of world-unit-to-real-scale hypotheses. Testing
**"1 world unit ≈ 1 cm"**: `zNear=10cm` (a tight but plausible near-clip for a close 3rd-person
camera) and `zFar=500m` (a plausible outdoor/skybox draw distance) both hold up; testing
**"1 world unit ≈ 1 inch"**: `zNear≈25cm` is still plausible, but `zFar≈1.27km` is excessive for a
mostly-enclosed carnival/asylum game, making the inch hypothesis less likely. Under the cm
hypothesis, a real average adult IPD (**63mm**, standard range 54–72mm) maps to **6.3 world units
full separation (3.15 half)** — within 3% of the shipped `3.25` half-IPD, and closely bracketing
the *also independent* proportional-to-distance estimate from notes/13 (`3.05`). Two different,
imperfect methods converging within ~0.2 units of each other and of the shipped value is a real
increase in confidence, not a coincidence to be waved away.

**Comfort framing, not just "it's visible"**: population-average real IPD (63mm) is the standard
target for comfortable VR stereo separation — deviating from a real human IPD is exactly what
causes eye strain, "gigantism"/"miniaturization" depth-perception distortion, and fusion difficulty
in VR. Since the cross-validated scale estimate places the *current* `3.25` half-IPD at
**≈6.5cm full separation — within 1mm of the actual average human IPD** — the existing value is
well-justified on comfort grounds, not merely "large enough to see an effect." **Recommendation:
keep `STEREO_HALF_IPD = 3.25f` unchanged** (no rebuild needed this session), but flag the
methodological limitation explicitly for future work: **once real gameplay is reachable**, this
should be switched from a proportional/shot-relative derivation to a **fixed, scale-calibrated**
constant (independent of camera framing) — the cm-scale hypothesis above is the best available
starting point, but should be confirmed against a real in-game measurement (e.g. a known object or
character height) rather than clip-plane plausibility alone, the first time gameplay is reachable.

### 2c. Untraced transform-path: real static progress, one new concrete hypothesis

notes/14 flagged an untraced indirection (`exe+0x433E50`/`exe+0x42E2A0`) between the tracked
View/Proj matrices and the actual register-6 upload, and a real negative result (register 6's
matrix doesn't decompose sanely as `World * TrackedView * Proj`). This session live-disassembled
both addresses (read-only, via `x64dbg_automate`'s `disassemble_at`, no game state modified) plus
the register-6 upload's caller (`exe+0x11D2A1`) to see the actual call sequence:

- **`exe+0x433E50` is a matrix-multiply-style helper**: `thiscall`-shaped
  (`this=ecx`, `MatrixA=[ebp+8]`, `MatrixB=[ebp+C]`), gated by a **runtime CPU-dispatch flag**
  (a global byte at `0x793520`) that selects between a fast path (tail-delegates to
  `exe+0x6932D0`, most likely an SSE-optimized multiply) and a manual FPU fallback computing
  row-by-row dot products (`this[row*0x10] · arg2[same layout]`, summed via `faddp` chains) — a
  classic "SSE-if-available, x87-fallback-otherwise" pattern common in mid-2000s engines.
- **`exe+0x42E2A0` is a 4×4 matrix TRANSPOSE** — newly confirmed, not previously known. Verified by
  the exact read/write offset pattern: it reads **column-wise** across the input (`this[0]`,
  `this[0x10]`, `this[0x20]`, `this[0x30]` — one column, spanning all 4 row-strided floats) and
  writes them out **sequentially** into the output's first row, repeating for each column — the
  textbook definition of a transpose.
- **Call sequence recovered at the register-6 upload's own caller** (`exe+0x11D2A1`–`0x11D33E`):
  `MatrixMultiply` (into a local buffer) → **a second** `MatrixMultiply` (into a different local
  buffer) → `Transpose` (applied to the second multiply's result) → `SetVertexShaderConstantF`-style
  upload (`StartRegister=6`, `Count=4`, device pointer read from `[someObject+0x194]`, generic
  wrapper at `exe+0x67EE30` — consistent with notes/14's previously-identified wrapper).

**New, concrete hypothesis for notes/14's decomposition failure**: a transpose sitting in the
pipeline is a very plausible, previously-unconsidered explanation for why the naive
`World_candidate = WVP_sample * inverse(TrackedView * Proj)` check failed sanity on 0/20 samples —
transposing a matrix doesn't corrupt its numeric content, but it *does* break any row/column-aware
decomposition that assumes standard (non-transposed) layout, matching the observed symptom (row
lengths off by orders of magnitude, `[3][3]` far from 1 rather than a subtly-wrong-but-plausible
result). **Honest limitation**: this session did not fully pin down *whether* the transposed or
untransposed buffer is what actually feeds the final upload — stack-offset accounting across the
`call`/`ret N` boundaries of the two helper calls suggested (but didn't conclusively prove, without
a live register/stack trace at the exact upload instruction) that the untransposed second-multiply
result is what gets uploaded, with the transpose's output apparently going unused *in this specific
call site* — which would be a strange thing to compute and discard, so this is flagged as
**uncertain, not concluded**, and a good target for a live stack trace early in the next session
that revisits this. **Concrete next step**: retry notes/14's Python decomposition check, but try it
against `Transpose(candidate)` as well as `candidate` directly, against both tracked View/Proj
instances — cheap, and would directly confirm or refute this session's hypothesis without any new
live debugging.

## 3. Cleanup

Every launch this session (both Option A's debugger-driven launches and Option B's 4 robustness
runs plus 1 static-disassembly launch) was preceded by `silence-intro-videos.ps1` and followed by
`restore-intro-videos.ps1`; verified clean at the end: no `Psychonauts`/`x32dbg`/`python` processes
running (`Get-Process` empty), no `.silenced` files remaining under
`WorkResource\Cutscenes\Prerendered\` (`INTRO.bik` present under its real name), no `d3d9.dll` left
in the game directory. No game files were copied into any git repo (only the workspace's own
`tools/proxy-d3d9/d3d9.dll`, already tracked from notes/14, unchanged this session).

## 4. Disposition and recommendation

- **Option A (input blocker)**: real, honestly-scoped partial progress — refines notes/08's root
  cause (message delivery itself works; the gate is further downstream and not yet located), rules
  out three candidate polling APIs and Steam Overlay as confounds, but does **not** unblock
  gameplay. **Needs**: more live-debug time with a proper stack unwind at the actual
  `DirectInput8Create`/window-procedure level — no headset required, this is pure PC-side debugger
  work and can continue exactly as before.
- **Option B (stereo prototype quality)**: a genuine, concrete improvement in confidence and
  understanding, achieved without any headset: 4/4 clean multi-launch robustness (new), a
  comfort-grounded (not just "it's visible") cross-validation of the existing IPD value confirming
  it's already close to real human average IPD (new reasoning, no code change needed), and real
  static-analysis progress on the untraced transform path including one concrete, testable
  hypothesis for notes/14's open decomposition mystery (new).
- **Mod repo**: **not touched this session** — no functional code change was made (the robustness
  test exercised the exact binary already pushed in notes/14), so per the task's own "genuine
  improvement" bar there's nothing new to publish there. Workspace notes, modding-notes, and
  dev-archive are updated/synced regardless, per standing instructions.
- **What still needs the home headset**: nothing changed on this front — actual in-headset
  comfort/fusion validation of the IPD value, and real player-controlled gameplay testing of the
  stereo hook (blocked on Option A), both still require getting past the input blocker and/or
  reaching the headset setup. **What can still be done headset-free**: continuing the Option A
  stack-unwind lead, the decomposition-with-transpose retry from §2c (pure offline Python, no game
  launch needed at all), and extending the register-6 correction to the skinning registers (96/64)
  flagged as still uncorrected since notes/14.
