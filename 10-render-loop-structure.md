# Render-Loop Structure — Frame Order, Draw-Call Count, and the Dual-Render Question

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only the two approved helper scripts,
`silence-intro-videos.ps1` / `restore-intro-videos.ps1`, touched anything under it, both
run/verified reverted at the end of every attempt this session, including failed ones; no
other file in the game directory changed).

**Goal**: purely observational — map where actual geometry draw calls
(`DrawPrimitive`/`DrawIndexedPrimitive`) happen relative to `BuildViewMatrix`/
`BuildProjectionMatrix` (`notes/07`) and `Present` (`notes/06`), find a plausible "draw the
whole scene" wrapper function to evaluate as a "call it twice, once per eye" candidate, and
check whether the engine already has any multi-pass structure (multiple `Clear`/
`SetRenderTarget`/`BeginScene`/`EndScene` calls per frame) to learn from. No memory writes, no
new render targets, no second render pass attempted — mapping only.

## 1. Method and tooling notes (read this before repeating this kind of capture)

Same `x64dbg_automate` Python client as every prior session (raw Python API, MCP tools still not
registered this session). Scripts lived in the session scratchpad, not copied into the workspace.
Getting a *correct* live capture took three attempts and surfaced two real, reusable bugs/gotchas
in the debugging harness itself — documented here because they'll bite the next session too if not
known in advance.

### 1a. Launching directly under x64dbg hit a persistent access-violation retry loop this session

The first attempt launched `Psychonauts.exe` directly under x64dbg from process creation (the
method every prior session used successfully). This time it did not reach a steady render loop:
**a first-chance `EXCEPTION_ACCESS_VIOLATION` (0xC0000005) at a fixed address (`exe+0x1CEB91`)
repeated 250+ times over 150+ seconds** without ever progressing to `BuildViewMatrix`/
`BuildProjectionMatrix`/`Present`. Steam was confirmed running throughout (ruling out the obvious
"missing Steam context" explanation). This is almost certainly a debugger-overhead-induced timing
issue in early init (each passed-through exception round-trips through the automation IPC layer,
turning what's normally a fast retry into tens of milliseconds — long enough to expose a latent
race in Steamworks/D3D bring-up that doesn't manifest at full speed), not a real crash, since
multiple earlier sessions (notes 04, 07, 08, 09) launched this exact way and worked fine. Time was
not spent root-causing it further.

**Fix that worked**: launch the game as a **normal, undebugged** process (plain `subprocess.Popen`,
no debugger attached), wait ~15 seconds for it to reach steady rendering on its own, *then* attach
x64dbg to the running PID (`start_session_attach(pid)`). This sidesteps the fragile early-init
window entirely and reached breakpoint hits within ~0.2s of attaching, every time it was tried.
**Recommended default for future sessions that don't specifically need to observe process startup**:
launch-then-attach, not launch-under-debugger.

### 1b. `get_latest_debug_event()` is LIFO, not FIFO — do not use it in a drain loop

The x64dbg_automate library's `get_latest_debug_event()` pops from the *newest* end of its internal
event deque. When several debug events queue up in a burst (routine — `PAUSE_DEBUG`/`RESUME_DEBUG`
pairs, DLL-load notifications, TLS-callback auto-breakpoints, and real breakpoint hits can all land
within the same millisecond), popping newest-first can hand back a real breakpoint event while one
or more **older, still-unprocessed** events sit behind it in the queue — meaning the debuggee is
actually still paused at that older event (nobody has resumed it yet), and any register/memory read
"at" the newer event is reading stale/wrong live state. This produced visibly nonsensical data in
one attempt this session: a "captured" `IDirect3DDevice9*` that was actually a nearby stack address
(`0x19DDD0`, differing from `ESP` by only ~680 bytes), and vtable slot reads built from it that were
consequently pure garbage (implausible addresses, repeating byte patterns).

**Fix**: drain events strictly oldest-first — `client._debug_events_q.popleft()` (bypassing the
library's own `get_latest_debug_event()`/`wait_for_debug_event()` helpers, which are LIFO-pop and
type-filtered-but-not-drained respectively), resuming (`go(pass_exceptions=True)`) for every
non-wanted event encountered along the way, in strict chronological order, only stopping when a
breakpoint the script actually cares about is reached. This single fix took the capture from
"nonsensical vtable garbage" to "live vtable slot 17 read back exactly equal to the real breakpoint
address" (see §3) — a clean, verified match.

### 1c. `wait_for_debug_event(EVENT_BREAKPOINT, ...)` alone can deadlock the debuggee

A related, separate bug in an earlier attempt: filtering for only `EVENT_BREAKPOINT` and ignoring
all other event types (in particular `EVENT_EXCEPTION`) meant that whenever the debuggee stopped on
some other event type, nothing ever called `go()` for it — the debuggee sat paused indefinitely
while the script kept waiting for a breakpoint event that could never arrive because the process
was stuck. Fixed by draining *every* event type generically (§1b's drain loop), resuming past
anything that isn't specifically wanted.

## 2. Confirmed frame order (Phase A: BuildViewMatrix / BuildProjectionMatrix / Present)

Breakpoints on `BuildViewMatrix` (`exe+0x292480`), `BuildProjectionMatrix` (`exe+0x2924D0`), and
`Present` (resolved live off the device vtable, not a hardcoded d3d9.dll offset — see §3) were hit
repeatedly on the title/attract screen's live camera (same scene used in notes 08/09). Across three
full cycles, the order and counts were **exactly reproducible**:

```
BuildProjectionMatrix  x8
BuildViewMatrix         x3
Present                 x1
--- repeat ---
BuildProjectionMatrix  x8
BuildViewMatrix         x3
Present                 x1
--- repeat ---
BuildProjectionMatrix  x8   (partial, run ended)
```

Timing: each full cycle (8 Proj + 3 View + 1 Present) took ~0.2–0.4 real seconds under the debugger
(includes IPC overhead from ~40 breakpoint round-trips; the game's own unthrottled frame time is
almost certainly far shorter — `PresentationInterval=0x0`/driver-default vsync per notes/06, and no
evidence of frame throttling was observed).

**Interpretation**: `BuildProjectionMatrix` firing 8x/frame is new information — notes/07 found it
fired only once in ~75s at the (2D) main menu and assumed it was rebuilt only on FOV/aspect/near/far
changes; here, on the animated 3D title scene, it fires 8 times *every* frame. Given notes/08 already
confirmed the *values* (`rawFov=104.0`, `aspect=1.333`, `zn=10`, `zf=50000`) stay constant call to
call, this 8x/frame pattern is almost certainly **8 separate camera-ish objects/views each calling
through the same shared wrapper with the same projection parameters** (e.g. one call per
mesh-instance batch, or one per light/shadow-frustum setup, all using the same global FOV) rather
than the projection matrix actually changing 8 times. `BuildViewMatrix` firing 3x/frame matches
notes/08's earlier finding exactly ("fires three times per animation step... likely two or three
separate view matrices being built per frame, e.g. a reflection or secondary camera").
**Present always comes last in the cycle, after both matrix builders have run** — confirms camera
setup happens at the *start* of a frame's work, not interleaved with `Present`.

## 3. Draw-call, Clear, and SetRenderTarget counts (Phase B)

Once the device pointer was captured correctly (see §1b fix), the following were resolved **live
off the actual IDirect3DDevice9 vtable** — not fixed offsets, since d3d9.dll's ASLR base shifts
run to run (confirmed: `Present` landed at `d3d9.dll`-relative addresses `0x73006120` and
`0x73046120` in two different runs, offset from base `0xE6120` both times, consistent with earlier
sessions but resolved live here regardless for safety):

| Method | Slot | Live address (this run) |
|---|---|---|
| `SetRenderTarget` | 37 | `0x73097AC0` |
| `BeginScene` | 41 | `0x72FC47F0` |
| `EndScene` | 42 | `0x72FC48F0` |
| `Clear` | 43 | `0x72FC4720` |
| `DrawPrimitive` | 81 | `0x72FBFD60` |
| `DrawIndexedPrimitive` | 82 | `0x72FBFC00` |
| `Present` | 17 | `0x73046120` (live vtable read **matched** the breakpoint hit address exactly — cross-validation passed) |

**One full captured Present cycle** (`BuildViewMatrix`/`BuildProjectionMatrix` breakpoints still
installed, plus all six above):

```
View=1  Proj=7  Clear=3  SetRT=8  Begin=1  End=2  DrawPrimitive=15  DrawIndexedPrimitive=74
```

**~89 real geometry draw calls per frame** (`15 DrawPrimitive + 74 DrawIndexedPrimitive`) on the
title screen's rotating-brain scene — a modest, era-appropriate count, not remotely close to a
volume that would make "run the draw portion twice" performance-prohibitive on its face.

The View/Proj counts in this specific captured cycle (1 and 7, vs the clean 3/8 pattern in §2) are
lower because breakpoint installation happened mid-cycle (partway through the prior frame's tail);
treat the §2 numbers as the reliable per-frame View/Proj counts and this section's Clear/SetRT/
Begin/End/Draw numbers as the reliable per-frame counts for *those* six methods specifically (they
were all installed together and their relative counts are self-consistent).

### `SetRenderTarget` and `Clear` are already called multiple times per frame — the key finding

Full order log across the capture window (spans the tail of one frame into the next):

```
t=0.157s SetRenderTarget
t=0.190s SetRenderTarget
t=0.192s Clear
t=0.366s SetRenderTarget   ┐
t=0.370s SetRenderTarget   │ five SetRenderTarget calls
t=0.375s SetRenderTarget   │ in an 18ms burst
t=0.380s SetRenderTarget   │
t=0.385s SetRenderTarget   ┘
t=0.387s EndScene
t=0.389s BeginScene
t=0.391s Clear
t=1.522s SetRenderTarget
t=1.525s Clear
t=1.595s EndScene
```

`SetRenderTarget` fired **8 times** and `Clear` **3 times** in this window (roughly 1.3 frames'
worth). This directly answers one of the task's open questions: **the engine already has
multi-target/multi-pass plumbing exercised every single frame**, even for the simple title-screen
scene — a burst of 5 `SetRenderTarget` calls followed immediately by `EndScene`→`BeginScene`→`Clear`
looks exactly like a discrete render pass boundary (most plausibly a shadow-map or render-to-texture
pass finishing, followed by the main color pass beginning fresh). This was not confirmed to be a
shadow pass specifically (would need to inspect the actual surface handles passed to each
`SetRenderTarget` call, not done this session — pure count/order observation only, per scope) but
the *shape* of the evidence (repeated target switches, paired with a Begin/End boundary and a Clear)
is a strong, reusable signal that the renderer is not a monolithic
"one Clear → one SetRenderTarget → draw everything → Present" structure. It already knows how to
tear down and stand up a fresh render target + clear + scene bracket mid-frame, which is exactly
the mechanism a second (right-eye) pass would need to reuse.

## 4. Candidate "draw everything" wrapper function — EBP chain walk

From the first `DrawIndexedPrimitive` hit, the EBP frame-pointer chain was walked 8 levels
(`ebp`/return-address pairs, each frame's `saved ebp` from `[ebp+0]`, return address from
`[ebp+4]`, walked until the chain broke a sanity check — see `renderloop2.py`'s
`ebp_chain_walk`):

| Frame | Return address | Interpretation (unconfirmed, no symbols) |
|---|---|---|
| 0 | `exe+0x2833D3` | Immediate draw-issuing code — the function that actually calls `IDirect3DDevice9::DrawIndexedPrimitive` |
| 1 | `exe+0x52597` | Likely per-mesh/per-batch submission |
| 2 | `exe+0x5242D` | Likely per-object rendering |
| 3 | `exe+0x4B559` | Likely scene-graph node traversal |
| 4 | `exe+0x46BED` | Likely scene-graph traversal (outer) |
| 5 | `exe+0x111C0F` | Likely "render this view/pass" level |
| 6 | `exe+0x115F36` | **Best candidate for "render the whole scene for one camera"** |
| 7 | `exe+0xFEFEE` | Likely top-level per-frame render dispatch (closest to "RenderFrame") |

No symbol names are available (matches every prior session's finding — plaintext import table,
no debug symbols), so this ordering is inferred purely from call depth and is **not** independently
confirmed to be the true call graph beyond what the raw return addresses show. It is, however, a
concrete, addressable starting point: **`exe+0x115F36` and `exe+0xFEFEE` are the two strongest
candidates to disassemble next** if a future session wants to identify (not yet call twice) the
actual "draw one eye's worth of the scene" entry point.

## 5. Answering the task's core question

**Structure confirmed**: `[BuildProjectionMatrix x8, BuildViewMatrix x3] → [multiple
SetRenderTarget/Clear/BeginScene/EndScene brackets, ~89 draw calls total] → Present`, once per
frame, with camera matrices always built *before* any draw calls and `Present` always last. This
is the "compute camera once [per view], draw everything, present" shape the task asked about —
**with the added nuance that "everything" is itself already split across at least two internal
render-target brackets**, not one flat draw list.

**Feasibility read for a dual-render (stereo) hook**:

- **Encouraging**: draw-call volume is low (~89/frame) — re-running the draw portion twice per
  frame is not a performance concern at this scene complexity. The engine already routinely
  tears down/stands up render targets and Begin/End brackets mid-frame without visible cost or
  instability, which is exactly the primitive a second eye pass needs.
- **Encouraging**: camera matrices are fully built (both view and projection) *before* any draw
  calls happen, and via the two small, already-fully-disassembled wrapper functions from notes/07
  — meaning per-eye matrix injection (already proven writable, notes/09) naturally precedes the
  draw work it needs to affect, with no interleaving to worry about.
- **Open risk, not yet resolved**: it is not yet confirmed whether the draw-issuing code path
  (frames 0–5 in §4) reads *only* from the freshly-built view/projection matrices each time it's
  invoked, or whether it also depends on other per-frame state that gets mutated/consumed
  exactly once per frame (animation pose updates, particle simulation steps, occlusion/culling
  results, etc.) — if any of that state is consumed destructively (e.g. an animation timestep
  advanced as a side effect of the "render" call, not decoupled from it), naively calling the
  outer wrapper (`exe+0x115F36` or `exe+0xFEFEE`) twice would double-advance game logic, not just
  redraw the same scene from a second eye. This was flagged as an open unknown in notes/00's
  prior milestone and **remains unresolved after this session** — this session only mapped
  *where* the draw calls sit, not what non-rendering state (if any) their call chain touches.

## 6. Concrete recommendation for the next implementation session

1. **Disassemble `exe+0x115F36` and `exe+0xFEFEE`** (§4) to determine which one is the cleaner
   "draw one eye's scene" entry point, and specifically check whether either function's body
   contains any code that looks like game-logic/animation/simulation update (timers, velocity
   integration, physics-looking float math unrelated to rendering) as opposed to purely
   traversal-and-draw code. If the split between "simulate" and "draw" isn't cleanly separated at
   either of these two levels, walk one or two frames further out in the same EBP-chain style to
   find where the split actually is (the frame-pointer chain technique used in §4 is reusable and
   cheap — a few minutes of work, not a new investigation).
2. **Once a clean render-only entry point is identified**, the concrete dual-render hook plan is:
   hook that function (a plain inline/detour hook, matching the pattern already proven in
   notes/09's write-hook), and on each real invocation: (a) let the original call proceed once
   for the left eye using the already-proven per-eye `BuildViewMatrix` offset injection
   (notes/09) into a render target set up via the already-hooked `CreateDevice`
   (notes/06) — e.g. `CreateRenderTarget`/`CreateTexture` sized to match the backbuffer, standing
   this up was already queued as low-risk infrastructure in notes/06 §7 and still hasn't been
   done; (b) re-set the eye offset for the right eye and call the same render-only function a
   second time into the second render target; (c) composite both targets into the real backbuffer
   before the real `Present` call proceeds. This reuses every already-proven primitive from
   notes 06/07/09 plus this session's mapping — no remaining *unscoped* unknowns, only the
   "confirm the split point is clean" check in step 1 above.
3. **If step 1 finds the split is NOT clean** (i.e. every candidate wrapper mixes simulation and
   drawing inextricably), the fallback recommended in notes/00's prior milestone remains the right
   move: don't try to re-invoke game code twice at all — instead render the scene once normally,
   and reconstruct the second eye's view via a post-process reprojection/warp of the single
   rendered frame (asymmetric but much lower-risk than fighting a tangled call graph, and a
   well-precedented technique in the flat-to-VR modding space).

## 7. Cleanup

Three failed/diagnostic attempts and one fully successful capture this session, each wrapped in
the same guaranteed-cleanup pattern: `silence-intro-videos.ps1` run before every launch,
`restore-intro-videos.ps1` run after every kill (including the two runs that hit bugs and were
manually terminated mid-script), verified via `Get-ChildItem`/`Test-Path` after each. Final state
verified clean: no `Psychonauts`/`x32dbg`/`x64dbg`/`python` processes running, no `.silenced`
files remaining under `WorkResource\Cutscenes\Prerendered`, no `d3d9.dll` (or any other stray
file) in the game directory. No anti-debug/anti-tamper reaction encountered at any point,
consistent with every prior session for this game.
