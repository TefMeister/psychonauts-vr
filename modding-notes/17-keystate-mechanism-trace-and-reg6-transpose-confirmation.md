# Session 17 — Full Keyboard-Input-Mechanism Trace (Lead 1) and Register-6 Transpose Confirmation (Lead 2)

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; `silence-intro-videos.ps1` run once before this session's first launch,
`restore-intro-videos.ps1` run once after the last launch and verified reverted — all `.bik` files
present under their real names, no `.silenced` remnants; `move-window-offscreen.ps1` run after every
individual launch this session, per the new standing requirement, and verified working every time).
No headset available this session (work PC) — everything here is debugger evidence, live memory
captures, and disassembly, not in-headset testing. No game files copied into any git repo; the only
files ever copied into the game directory were disposable scratch scripts' own debugger sessions
(nothing persisted — no `d3d9.dll` or other file was left in the game directory at any point this
session, verified at the end).

**Task**: continue notes/16's two leads — be patient and retry the buffered-`GetDeviceData` forge for
Lead 1 (and if it works, immediately re-check stereo separation past the title screen); do a live
stack/register trace to directly settle which buffer feeds the register-6 upload for Lead 2.

## Result summary

**Lead 1: the buffered-`GetDeviceData` hypothesis from notes/16 is REFUTED (not just "still flaky") —
and in its place, this session fully traced the REAL keyboard-input mechanism live, all the way from
the raw polled buffer through a proper edge-detection state machine to a generic keybinding
abstraction layer.** The title screen still does not visibly respond to synthetic input, but the
reason is now understood mechanistically rather than being an unresolved "we patch the buffer and
nothing happens" mystery. Concrete next steps are identified, not vague ones.

**Lead 2: FULLY CONFIRMED, not just "partial support."** A clean live stack/register trace (8/8
samples) directly proves the register-6 upload's matrix is the in-place TRANSPOSE of the second
matrix-multiply's result — settling notes/16's own flagged uncertainty outright. Bonus: the same
trace independently confirms notes/16's "non-identity World" interpretation with direct numeric
evidence.

Two real, reusable x64dbg-automate tooling gotchas were found and fixed this session (see §3).

## 1. Lead 1: refuting the buffered-GetDeviceData hypothesis, then tracing the real mechanism

### 1a. First check: is `GetDeviceData` even called on the keyboard device? (No.)

Before retrying notes/16's buffered-event forge, this session's leftover logs were reviewed and a
real doubt was found: notes/16 §1d's "GetDeviceData fires in lockstep with GetDeviceState, 68 hits"
evidence never actually verified *which device* (`this` pointer) those hits belonged to — it only
counted hits on the breakpoint's *name*. Since `GetDeviceData`'s code address is one shared
implementation used by both the keyboard and mouse device objects (same vtable-resolved function,
`this` differentiates instances), this was a real gap.

A clean diagnostic (`test8_gdd_device_classify.py`) captured **both** device pointers (keyboard from
`CreateDevice` hit #1, mouse from hit #2 — confirmed reliable ordering) and classified every
`GetDeviceState`/`GetDeviceData` hit by `this` against both, over an 80s window:

```
FINAL: GDS_by_class={'KEYBOARD': 41, 'MOUSE': 1}   GDS_other=0
       GDD_by_class={'KEYBOARD': 0,  'MOUSE': 42}  GDD_other=0
```

**`GetDeviceData` fired 42/42 times on the MOUSE device and zero times on the keyboard.** This
directly refutes notes/16's framing: the "flakiness" in getting `GetDeviceData` to fire on the
keyboard (0 hits in test6's 75s attempt) was never flaky tooling — the keyboard device genuinely
never calls buffered `GetDeviceData` at all. The buffered-forge hypothesis is dead for the keyboard
specifically. This is real, valuable, hard evidence, not more indirect inference.

### 1b. Tracing the real consumer: a hardware read-watchpoint on the DIK_SPACE byte

Rather than retry a refuted hypothesis, this session used a technique not tried before in this
project: a **hardware (DR-register) READ breakpoint**, 1 byte, on `lpvData+0x39` (the DIK_SPACE byte
inside the keyboard's persistent polled-state buffer, confirmed constant at `0x00782D78` across many
polls, matching notes/16). This directly answers "does anything even read this byte" instead of only
ever writing to it and observing screenshots.

After fixing a **real, newly-discovered tooling gotcha** (see §3a), a clean run
(`test9_watch_dik_space_reads.py` / `test9b_log.txt`) found exactly 3 real per-poll readers (plus 2
DirectInput/OS-internal ones), each firing 27/27 times over the observation window, in lockstep:

```
reader eip=0x00402E28  (game code, caller=0x00403673)
reader eip=0x00402E62  (game code, caller=0x00403673)
reader eip=0x7090300D  (dinput8.dll internal)
```

### 1c. Full disassembly: the real DIK_SPACE consumer chain

Read-only disassembly (`disasm_consumer.py`, `disasm_consumer.txt`) of the function containing
`0x00402E28`/`0x00402E62` revealed a complete, sensible mechanism:

- A loop scans a **3×21 keybinding table** at `0x00782008` (`ecx*0x54 + edx*4`, i.e. 3 categories ×
  21 slots = 63 possible bindings), reading a DIK code per slot and calling a shared
  `SetKeyState(dikCode, pressed)`-style function (`0x00405470`) for every bound, in-range entry.
- **After** the table scan, three DIK codes are read **directly, by hardcoded literal**, each also
  passed to the same `SetKeyState`:
  - `0x00782D94` (offset `0x1C` = **DIK_RETURN**)
  - `0x00782DB1` (offset `0x39` = **DIK_SPACE**) — confirmed, this is our byte
  - `0x00782D79` (offset `0x01` = **DIK_ESCAPE**)

This is a strong, direct structural match for a title screen's "press [] to begin / Esc to quit"
input path — Return/Space/Escape are exactly the three keys such a prompt would care about.

### 1d. `SetKeyState` fully decoded: a real edge-detection state machine with a one-shot listener slot

Disassembling `0x00405470` (`disasm_consumer.txt`, "SetKeyState-like sink") revealed a complete,
correct-looking input state machine:

- A **persistent per-DIK-code state byte array** at `0x00782130` (`keyState[dikCode]`).
- On a **fresh press** (`pressed=1` and `keyState[dik]` bit0 not already set — a debounce guard):
  writes `keyState[dik] = oldValue | 0x03` (sets bit0=held, bit1=just-pressed), THEN checks a global
  callback function pointer at **`[0x00782F14]`** — if non-null, calls it as
  `callback(dikCode, 1, userdata=[0x00782F18])`; if the callback returns 0, the pointer is cleared
  (**one-shot unregister-on-return-0** pattern).
- On a **release** (`pressed=0` and bit0 was set): writes `keyState[dik] = 0x04` literal (bit0
  cleared, bit2=just-released set), and performs the same callback-if-registered check with
  `pressed=0`.
- Two mirror callback-invoke sites exist (press-path at `0x004054A9`, release-path at `0x0040550F`).

This `[0x00782F14]`/`[0x00782F18]` pair is exactly the shape of a "register a listener, get called
once on the next key event, then auto-unregister" idiom — precisely what a "press any key to
continue" prompt would plausibly use.

### 1e. The decisive test: is the listener actually armed? (No — settles why the patch has no effect)

`test11_setkeystate_callback.py` breakpointed `SetKeyState` itself (filtered to DIK_RETURN/SPACE/
ESCAPE) and logged the live value of `[0x00782F14]` at every call, while re-applying the
already-proven "hold DIK_SPACE=0x80 for 5s then release" buffer patch at the idle title screen:

```
[19:00:38] ARMED patch window: holding DIK_SPACE=0x80 for 5s
[19:00:39]  SetKeyState(dik=0x39/SPACE, pressed=1) callback_ptr=0x00000000 prior_state_byte=0x00
[19:00:43]  SetKeyState(dik=0x39/SPACE, pressed=1) callback_ptr=0x00000000 prior_state_byte=0x03
[19:00:45] released DIK_SPACE=0x00
```

**The patch correctly and verifiably reaches `SetKeyState(SPACE, pressed=1)`** — the state byte
transitions exactly as the decoded semantics predict (`0x00` → `0x03` on the fresh-press edge). But
**`callback_ptr` is `0x00000000` both before and during the forced press** — no listener is
registered on this specific global slot while idling at "Press [] to begin." This is a genuine,
positive finding, not just another null result: it explains *why* two full sessions of correctly-
delivered synthetic input produce no visible effect — the mechanism this session traced isn't
currently listening, at least not via this global one-shot slot, at this exact screen state.

### 1f. One more consumer layer found: a generic keybinding-abstraction system (bounded stop point)

A follow-up hardware-watch on `keyState[DIK_SPACE]` itself (`0x00782130+0x39`, `test12_...py`) found
additional readers **outside** `SetKeyState`'s own body, further disassembled:

- `0x00405590`: a per-frame sweep clearing bits 1–2 (`and edx, 0xFFFFFFF9`) across the **entire**
  keyState array (0..0x140) — the mechanism that resets "just pressed"/"just released" edge flags
  each poll cycle.
- `0x004055D0`/region B: re-scans the **same 3×21 binding table** from §1c, and for each bound key
  with any nonzero `keyState` bit, translates it into an abstracted **0x00/0xFF "digital button"
  output byte** written into a per-binding-slot output structure — a proper input-abstraction layer
  translating raw DIK codes into named actions.
- `0x00405910` / `0x00405930` / (a third, uninspected, likely at `0x00405950`): simple query helpers
  — `IsKeyHeld(dik) = keyState[dik] & 0x01`, `IsKeyJustPressed(dik) = keyState[dik] & 0x02`, each
  via the standard "boolean via neg/sbb/neg" idiom.

**This is a genuinely deep, well-understood generic system now** (raw buffer → edge-detected
per-key state → binding-table abstraction → named digital actions), but identifying *which specific
binding slot* the title screen reads for its "confirm" action, and what higher-level UI code
consumes that slot's output byte, is a legitimately open further investigation — not resolved this
session. Per the task's own bounded-effort guidance, this is the stopping point for Lead 1: real,
concrete, evidence-backed forward progress (a full mechanism trace, a corrected prior finding, and a
positive explanation for the null result), but the title screen still does not advance.

### 1g. Stereo hook: not re-checked past the title screen this session

Since gameplay was not reached (same as every prior session), the "does stereo separation survive
into real gameplay" question remains unanswered — it still requires either reaching gameplay via
Lead 1's remaining open thread, or the home headset setup with real keyboard input.

## 2. Lead 2: register-6 upload fully confirmed as `Transpose(multiply2_result)`, live

### 2a. Method

Reusing the exact, reliable (no-ASLR-on-the-main-exe) address-breakpoint method notes/14 and this
session's own Lead 1 work both rely on, `test10_reg6_livetrace.py` set four breakpoints along the
already-statically-disassembled call sequence at `exe+0x11D2CD`–`0x11D33E`
(absolute here: `0x0051D2CD`–`0x0051D33E`, exe base `0x00400000`):

| Breakpoint | Meaning |
|---|---|
| `0x0051D2D2` | right after `MatrixMultiply` #1 returns — `eax` = result pointer |
| `0x0051D307` | right after `MatrixMultiply` #2 returns — `eax` = result pointer |
| `0x0051D315` | right after `Transpose` returns — `eax` = result pointer |
| `0x0051D33E` | the `SetVertexShaderConstantF`-style call itself — reads the actual `pConstantData` pointer off the stack, filtered to `StartRegister==6 && Count==4` |

At each hit, both the **pointer** and the **raw 16 floats** at that pointer were captured, so the
final comparison checks both address identity and content identity — not just one or the other.

### 2b. Result: 8/8 samples, unambiguous

```
MATCH: upload==mul1_ptr? False   MATCH: upload==mul2_ptr? True    MATCH: upload==xpose_ptr? False
CONTENT_MATCH: upload==mul1_floats? False
CONTENT_MATCH: upload==mul2_floats? False
CONTENT_MATCH: upload==xpose_floats? True
```

At first glance this looks contradictory (`upload_ptr == mul2_ptr` but `upload_floats != mul2_floats`
while `upload_floats == xpose_floats`) — it isn't. The transpose call **writes its result in place,
back into multiply2's own output buffer** (confirmed via the earlier static stack-offset analysis in
notes/16, now empirically proven live): by the time the code reaches the upload call, the memory at
that shared address has already been overwritten by the transpose. So the pointer is unchanged
throughout, but its *contents* are transposed before upload.

Manually verifying against one live sample's raw floats settles it beyond doubt. `mul2_floats`
interpreted as a row-major 4×4 matrix:

```
Row0: -1.5377  -0.0001   0.0000   0.0000
Row1: -0.0001   2.0503   0.0000   0.0000
Row2:  0.0000   0.0000   0.9902   1.0000
Row3:  0.0154 -615.0911 683.2366 699.9999
```

Transposing this by hand gives exactly `upload_floats`:

```
Row0: -1.5377  -0.0001   0.0000   0.0154
Row1: -0.0001   2.0503   0.0000 -615.0911
Row2:  0.0000   0.0000   0.9902  683.2366
Row3:  0.0000   0.0000   1.0000  699.9999
```

**This directly and fully confirms notes/16's flagged-uncertain reading**: the register-6 upload
genuinely is the transposed matrix, not (as notes/16 tentatively worried) an unused byproduct with
the untransposed value silently uploaded instead. Settled via live trace, not decomposition-sanity
inference, exactly as the task requested.

### 2c. Bonus: independent confirmation of the "non-identity World" interpretation

`mul1_floats`, read as a matrix, is essentially a bare perspective-projection matrix (diagonal-
dominant, `[0][0]=1.5377` matching the independently-known `xScale` value from notes/07/14, `[2][2]`/
`[2][3]`/`[3][2]` shaped like a standard D3DXMatrixPerspectiveFovRH output) — consistent with
`MatrixMultiply #1` reducing to (approximately) pure `Proj` because the other operand was near-
identity for this particular draw call. `mul2_floats` then carries a **real, non-trivial translation**
in row 3 (`[0.0154, -615.09, 683.24, 699.9999]`) — i.e. a genuine object/world-space position, not
identity. This is direct, independent numeric confirmation of notes/16 §2b's own "leading
interpretation" (the remaining decomposition gap is a real non-identity World transform for whatever
object register 6's draw call renders), obtained as a free byproduct of the same trace that settled
the transpose question — no further work needed to support that interpretation.

### 2d. Disposition

Lead 2 is **fully closed** this session: the transpose hypothesis is proven, live, with both pointer-
and content-level evidence across 8 independent samples, and the reasoning is simple enough to
manually re-verify (as done in §2b) rather than resting on an opaque decomposition-sanity score. No
further live debugging is needed on this specific question. Whether this changes anything about the
existing register-6 stereo correction (notes/14's closed-form `-d*Proj[0][0]` patch) is a separate,
not-yet-assessed question — that correction was derived to be valid "regardless of what World/View
individually were," so it may not need to change, but this session did not re-derive or re-test it
against the now-confirmed transpose; flagged as a reasonable, cheap next-session check (pure math, no
live debugging required).

## 3. Reusable tooling gotchas found and fixed this session

### 3a. `wait_for_debug_event` event-stealing under a high-frequency unrelated breakpoint

`test11`'s first attempt silently stalled (patch never armed, no error) because its ret-catching
logic used the naive single-check `wait_for_debug_event(EventType.EVENT_BREAKPOINT, timeout=N)`
pattern — the exact gotcha notes/16 already documented, but not fully internalized into every call
site this session initially. With `SetKeyState` breakpointed unconditionally (firing ~66 times per
poll, once per keybinding-table entry plus the 3 special keys), any single one of those same-typed
`EVENT_BREAKPOINT` events could be popped and silently discarded by a `wait_for_debug_event` call
that was specifically waiting for an unrelated single-shot `GDS_ret` breakpoint, permanently losing
that return-catch and silently breaking the arm logic. Root-caused by comparing a clean run (test9b,
no other breakpoint competing) against the stalled one (test11, `SetKeyState` competing), and fixed
by replacing every "wait once for a specific name" call site with a small
`wait_for_named_breakpoint()` helper that drains and resumes past every non-matching breakpoint event
in a loop until the wanted one arrives. Confirmed fixed: rerunning test11 with the fix produced the
clean §1e result on the first try.

### 3b. Hardware/memory breakpoints persist across `start_session()` calls independently of software ones

notes/16 already documented that **software** breakpoints persist across separate `start_session()`
calls unless explicitly cleared with `clear_breakpoint(None)`. This session found the same is true
for **hardware** (DR-register) breakpoints, but `clear_breakpoint(None)` does **not** clear them — a
separate `clear_hardware_breakpoint(None)` call is required (and, for completeness,
`clear_memory_breakpoint(None)` for guard-page-style memory breakpoints). `test12`'s first attempt
was contaminated by a stale hardware read-watchpoint left over from `test9b`'s session (reloaded
automatically, firing at whatever now lived at that stale relative address under fresh ASLR),
producing nonsensical high-frequency "reader" hits before `GetDeviceState` had even been resolved.
Fixed by adding both clear calls immediately after every `start_session()`, alongside the existing
`clear_breakpoint(None)`. **Recommendation for all future sessions using hardware or memory
breakpoints**: always call all three clear functions at session start, not just
`clear_breakpoint(None)`.

## 4. Cleanup

Every launch this session was preceded by an already-active `silence-intro-videos.ps1` (run once at
session start) and followed by `move-window-offscreen.ps1` (run individually after every single
launch, per the new standing requirement — verified working, handle returned, every time);
`restore-intro-videos.ps1` was run once at the very end and verified (all `.bik` files present under
their real names, zero `.silenced` files remaining). Verified clean at session end: no
`Psychonauts`/`x32dbg`/`x64dbg`/`python` processes running, no `d3d9.dll` or other file left in the
game directory at any point (this session never needed to copy the proxy DLL in — all work was pure
debugger-side breakpoint/disassembly work against the unmodified game exe). No game files were
copied into any git repo.

## 5. Disposition

- **Lead 1**: real, substantial, *positive* progress — refutes a specific prior-session hypothesis
  with hard evidence (device-identity classification), then goes on to fully trace the actual
  keyboard-input mechanism (raw buffer → `SetKeyState` edge-detection state machine → one-shot
  listener slot → generic keybinding-abstraction layer), and pinpoints exactly *why* the
  already-proven-correct patch technique produces no visible title-screen effect (the specific
  global listener slot the game *could* use isn't armed at the observed screen state). The
  behavioral goal (title screen responds) was **not** achieved, but the open question changed
  character entirely — from "we don't know why patching does nothing" to "we know the patch reaches
  a real, correctly-functioning input subsystem, and the next concrete step is identifying which
  binding-table slot and higher-level consumer the title screen actually reads." This is real
  forward motion, not a repeat of prior sessions' inconclusive flakiness.
- **Lead 2**: **fully resolved**, upgraded from notes/16's "partial support" to a rigorous, live,
  8/8-sample, pointer-and-content-verified confirmation, cross-checked by hand against raw numbers.
- **Mod repo**: **not touched this session** — Lead 1 did not reach gameplay (no functional
  improvement to publish) and Lead 2 is analysis/confirmation of existing understanding, not a code
  change. Workspace notes, modding-notes, and dev-archive synced as usual.
- **What still needs the home headset setup**: unchanged — real in-headset comfort/fusion
  validation, and gameplay-camera stereo validation, both still blocked on reaching actual gameplay.
- **What can still be done headset-free**: (1) identify which of the 63 keybinding-table slots (or
  the hardcoded DIK_RETURN/SPACE/ESCAPE path) the title screen's own logic actually reads via
  `IsKeyHeld`/`IsKeyJustPressed` (§1f) or via the abstracted 0x00/0xFF binding output bytes, and
  trace that consumer forward — the concrete, well-scoped continuation of Lead 1; (2) re-derive/
  re-check notes/14's register-6 stereo correction formula against the now-fully-confirmed transpose
  relationship (§2d) — pure offline math, no live debugging needed; (3) extend the now-understood
  transform pipeline (multiply → multiply → transpose → upload) to the other, still-uncorrected
  skinning registers flagged since notes/14.
