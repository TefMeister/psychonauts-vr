# First Stereo-Rendering Prototype — Inline Hooks, Double-Call, Partial Result

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only `silence-intro-videos.ps1`/`restore-intro-videos.ps1` touched
anything under it, both run before/after every launch this session and verified reverted; the
only file ever copied into the game directory was the test `d3d9.dll`, removed after every run).

**Goal**: combine every previously-proven primitive (proxy DLL injection, `CreateDevice`/
`Present` vtable hooks, the `BuildViewMatrix` camera injection point, `CandB`
(`exe+0xFEDA0`)'s confirmed double-call safety) into an actual working side-by-side stereo
prototype: two distinct camera perspectives rendered into two halves of the screen in one frame.

## Result summary

**Partial success, with a clear, well-diagnosed stopping point.** Built and validated real,
non-guessed inline hooks directly into `Psychonauts.exe`'s own code (not just COM vtable
patching) for the first time this project — this is genuinely new infrastructure, not just this
session's stereo attempt, and it worked cleanly: no crash, no hang, stable across hundreds of
frames, game fully playable/responsive throughout. `CandB` was confirmed invoked twice per real
frame into two separate offscreen render targets and composited into the left/right halves of the
real backbuffer, live, in the running game window — a real screenshot shows two **visibly
different** images side by side. However, the difference is **not** a camera-angle/parallax
difference (the success criterion) — it's a rendering-completeness artifact (see §4). The camera
eye-offset injection itself was empirically confirmed to have **zero effect** on the final image,
even at 18x the realistic magnitude, and a specific, well-supported root cause was identified
(§5) but not fixed this session. Per the task's own framing, this is reported honestly as
"got partway, here's exactly where it broke," not oversold as a working stereo renderer.

## 1. What was built

Extended `tools/proxy-d3d9/proxy_d3d9.c` (same file as notes/05/06, not a new one) with real
**inline hooks** — byte-patches directly into `Psychonauts.exe`'s own `.text` section, a first
for this project (every prior hook was a COM vtable patch, which only works for interface
methods; `BuildViewMatrix` and `CandB` are plain functions with no vtable slot).

### 1a. Exact byte-level design (not guessed)

A raw PE file parse (Python script reading `Psychonauts.exe` directly off disk, no live process
involved — see the session's scratchpad `dump_bytes.py`, not copied into the workspace) confirmed
the exact prologue bytes at both hook addresses, resolving section RVA→file-offset mapping from
the PE section table:

```
BuildViewMatrix (exe+0x292480 / 0x00692480):
  55 8B EC 83 E4 F0 83 EC 48 56 57 ...
  push ebp; mov ebp,esp; and esp,0xFFFFFFF0 | sub esp,0x48; push esi; push edi ...

CandB (exe+0xFEDA0 / 0x004FEDA0):
  55 8B EC 83 EC 20 89 4D E0 68 90 14 79 00 FF 15 B0 00 70 00 ...
  push ebp; mov ebp,esp; sub esp,0x20 | mov [ebp-0x20],ecx; push ...; call [0x700B0] ...
```

Both functions have a clean, **fully position-independent 6-byte prologue** (`push ebp; mov
ebp,esp; <one more 3-byte instruction>`) — exactly enough room for a 5-byte `E9 rel32` jump plus
one NOP pad, with no instruction split mid-boundary and nothing relative-addressed inside those 6
bytes, so they can be safely copied byte-for-byte into a trampoline. `CandB`'s own first real
instruction after the prologue, `mov [ebp-0x20],ecx`, is a new, useful confirmation: it captures
its incoming `ECX` into a local immediately, consistent with `ECX` being a `this`-like context
pointer for a singleton renderer object (matches notes/12's finding that `ecx0` was bit-identical
across all 15 double-call hits — a stable object pointer, not per-frame data).

### 1b. Hook mechanism

- **`MakeTrampoline()`**: `VirtualAlloc`s an executable stub containing a byte-for-byte copy of
  the original 6-byte prologue, followed by a generated `E9` jump back to `original+6`. Calling
  this stub via `call` runs the real, unmodified original function body exactly as if never
  hooked — used both as the "let the real call proceed" path inside each hook, and as a general
  "call the pristine original" function pointer from arbitrary other code.
- **`PatchJump()`**: `VirtualProtect`s the original 6 bytes writable, overwrites them with `E9
  <rel32-to-hook>` + one `0x90` NOP pad, restores protection, `FlushInstructionCache`s.
- **`Hook_BuildViewMatrix`** (`__attribute__((naked))`, raw AT&T inline asm): entered via the
  patched `JMP`, so CPU state is exactly the real function's own entry state. Reads the 4 stack
  args, calls a C helper (`BVM_OnEntry_asm`) to observe/cache them, then **tail-`JMP`s** into the
  trampoline — the trampoline's own eventual `ret` pops the real original return address directly,
  fully transparent, single real invocation.
- **`Hook_CandB`** (also naked asm): a **"call the real body twice"** hook. It uses `CALL` (not
  `JMP`) to invoke the trampoline both times. Because each invocation is a `CALL`, it pushes a
  fresh return address on top of the stack; the real function's own standard epilogue (`mov
  esp,ebp; pop ebp; ret`) pops exactly that top return address and hands control back to the hook
  — the *real* original return address (pushed by the game's own `call CandB`) sits untouched
  further down the stack the entire time. The hook's own final `ret` pops that, returning control
  to the game exactly once, after both eye passes complete. This "call resolves inward" pattern is
  what lets a single interception point invoke the game's own render-dispatch function twice per
  frame while still returning cleanly to the one real caller.
- Both C-callable helpers referenced from inside the raw asm blocks (`BVM_OnEntry_asm`,
  `CandB_BeforeEye1_asm`, etc.) and the trampoline-pointer globals are declared with explicit
  `asm("exact_name")` labels (a GCC/clang extension) to sidestep i686-mingw's default
  underscore-prefixing of C symbols — this bit once during the build (an `undefined symbol:
  CandB_This_asm` link error, because a `static` global written only from raw asm, never from C
  code, was dead-store-eliminated by the compiler; fixed with `__attribute__((used))`).

### 1c. Camera injection design

- `BuildViewMatrix`'s hook caches a **clean, unmutated** `eye`/`at`/`up` plus the `pOutMatrix`
  pointer on the **first** call of each real frame (tracked via a flag reset in `Hook_Present`
  after presenting), and computes the camera's right vector
  (`normalize(cross(normalize(at-eye), up))`) — deliberately never mutating `*pEye` in place, to
  avoid the "persistent pointer, writes compound frame-to-frame" gotcha documented in notes/09.
  The real call is always let through completely unmodified.
- Before each of `CandB`'s two invocations, a C helper (`SetEyeAndTarget`) switches the active
  render target to that eye's dedicated offscreen surface (`SetRenderTarget`), then **directly
  re-invokes `BuildViewMatrix`'s own real body** (through its trampoline — genuinely calling the
  pristine original function, not reimplementing its math) with a fresh eye position computed
  from the cached clean base each time: `eye = baseEye + rightVec * halfIPD * sign`.
- **IPD magnitude reasoning**: the title screen's live camera eye→at distance was previously
  measured at ~190–200 world units (notes/08, notes/09). Scaling a real human IPD (~6.3cm)
  proportionally against a plausible ~2m real-world viewing distance for a similarly-framed shot
  gives a ratio of ~0.0315; applied to ~195 world units that's ~6.1 units full separation
  (~3.05 half-offset) — closely matching the task's own suggested "~6-7 world units" ballpark.
  `STEREO_HALF_IPD = 3.25f` (6.5 full) was the shipped value, a round number in that range.

### 1d. Render targets and compositing

- `Hook_CreateDevice` now also calls `SetupStereoSurfaces()`: grabs (and retains) the real
  backbuffer surface via `GetBackBuffer`, and creates two offscreen render targets
  (`CreateRenderTarget`, `D3DFMT_A8R8G8B8`, matching backbuffer dimensions) for eye 1 and eye 2.
  All three succeeded (`hr=0x00000000`) every run.
- Final composite: two `StretchRect` calls copy each eye surface into the left/right half of the
  real backbuffer before the one real hardware `Present` — the "split viewport" idea from the
  task brief was rejected in favor of dedicated offscreen surfaces per eye specifically because
  `CandB`'s internal `Clear()` calls (3x/frame per notes/10) clear the **entire** target surface
  regardless of the current viewport — sharing one render target between both eye passes would
  have let eye 2's `Clear()` wipe out eye 1's already-drawn pixels. Two independent surfaces sidestep
  that entirely.

## 2. Build

Same toolchain (`i686-w64-mingw32-clang`, `build.ps1`, unchanged). One new build issue hit and
fixed: `ld.lld: error: undefined symbol: CandB_This_asm` — a `static` global referenced only from
raw inline asm (never from C-visible code) was dead-code-eliminated despite its `asm()` label;
fixed with `__attribute__((used))`. No other build issues; only the pre-existing harmless
`Direct3DCreate9` redeclaration warning.

## 3. Live validation method

Own test script (not `validate.ps1`, which doesn't silence intro videos or screenshot): silences
intro videos, copies the built `d3d9.dll` into the game directory, launches
`Psychonauts.exe` directly (no debugger), waits for steady `Present` activity, screenshots the
game window (`GetWindowRect` + `Graphics.CopyFromScreen`), then unconditionally kills the process,
removes the copied DLL, and restores intro videos in a `finally` block — run successfully (with
full cleanup) across **six** separate launches this session while iterating.

## 4. What worked

- **No crash, hang, or instability across any of the six runs**, including hundreds of real
  double-invoked `CandB` calls per run — extends notes/12's 15-frame debugger-driven safety test
  to a real, sustained, undebugged in-process double-call hook, which is a stronger and more
  relevant confirmation than the original debugger-based test.
- **The composite pipeline works**: `GetBackBuffer`/`CreateRenderTarget` (both eyes)/`StretchRect`
  (both halves) all returned `S_OK` every single frame, every run. The screenshot genuinely shows
  a left half and a right half with different pixel content, confirming two independent render
  passes really did happen and really did get composited into one window.
- **A real, novel discovery**: `CandB`'s own nested call tree (not `CandB`/`CandA`'s own bodies —
  notes/11's static disassembly of those two functions specifically found zero D3D-vtable-pattern
  calls at that level) **calls the real `Present` internally**, somewhere in the 89 combined
  nested helper calls that notes/11 flagged as not individually audited. This was proven, not
  inferred: a reentrancy flag set for the duration of `Hook_CandB`'s two-call region read back
  `TRUE` from inside `Hook_Present` on ~50% of samples once the title screen's animated scene was
  active. This means each `CandB` invocation is really a **"render this eye AND flip it to the
  screen"** unit, not a pure render-only unit as notes/11's analysis (correctly, given its scope)
  assumed. **Fixed within this session**: a 3-state phase (`IDLE`/`EYE1`/`EYE2`) lets
  `Hook_Present` fully suppress eye 1's premature internal `Present` (no composite, no real
  present — just return `D3D_OK`) and repurpose eye 2's internal `Present` as the actual "both
  eyes done, composite and flip now" signal. This fix is real, tested, and load-bearing — without
  it the frame rate was roughly doubled and compositing happened against inconsistent
  partially-rendered state.

## 5. What did not work, and the most likely reason why

**The stereo camera offset had zero visible effect on the rendered image**, tested at both the
realistic `3.25`-unit half-offset and, as a diagnostic, at `60.0` units (18x larger) — the
silhouette in both eye images sits at the identical screen position and scale in every screenshot
taken this session. Diagnostic logging (added and later stripped back down for the shipped code)
confirmed the mechanism runs correctly at the CPU level: `BuildViewMatrix`'s hook does cache a
clean base eye/at/up once per frame, and `SetEyeAndTarget` does call through to the real
`BuildViewMatrix` body with the computed per-eye offset before each `CandB` invocation — the
write itself was verified to land in `*pOutMatrix` via a byte-level read-back. Despite that, the
final image never moved.

**Most likely explanation** (matches an open gap flagged back in notes/07, not newly speculative):
notes/07 established that this game's camera flows through the **shader-constant pipeline**
(`SetVertexShaderConstantF`), not the fixed-function `SetTransform` pipeline, but the *exact* call
site/register range that uploads the camera view/projection matrix to the GPU was never pinned
down (notes/07 §5, still open). The most likely reading of this session's evidence: that upload
happens **once per real frame, before `CandB` ever runs** (matching the documented frame order —
matrices built, then draws happen). Rewriting the CPU-side `D3DXMATRIX` buffer that
`BuildViewMatrix` writes into, *after* that upload has already happened, has no way to reach the
GPU unless something re-uploads it — and nothing in `CandB`'s call tree appears to do so (a second
`CandB` call renders with whatever constants are already bound, unaffected by our CPU-side matrix
edit). This is consistent with — and does not contradict — the write-hook proof from notes/09,
which mutated `*pEye` *before* the game's own single natural call to `BuildViewMatrix` each frame
(i.e. before that frame's one-and-only upload), which is exactly the case where this mechanism
does work.

**A second, independent, and unexplained finding**: the second (`sign=+1`) `CandB` invocation's
render consistently produces a **materially different image** from the first — not a camera
shift, but a **missing background**: the first eye's surface shows the game's normal textured
background (a cyan hexagonal pattern), while the second eye's surface is nearly pure white behind
the same (unshifted) silhouette, with a distinct cyan/blue glow/rim artifact around the shape's
edge. This did not change when the reentrant-`Present` bug (§4) was fixed, so it's a separate
issue. The leading hypothesis (not confirmed) is that some draw call responsible for the
background layer is gated by an internal "already drawn this frame" condition inside one of the
89 unaudited nested helpers — notes/11/12's double-call safety analysis checked for
stack/register/return-value/timing corruption and animation-speed doubling, but did not check
"does every individual draw call actually re-fire identically on the second invocation," which
is a materially different and, it turns out, not-actually-tested question. An untouched, freshly
`CreateRenderTarget`'d D3D9 surface's default (never explicitly `Clear()`'d) contents being a
uniform light color is also consistent with "the background-clearing/drawing step was skipped."

## 6. Honest verdict against the task's success criterion

The task's success bar was: *"a visibly different image in the left half vs right half of the
screen, proving two distinct camera angles rendered."* The left/right halves **are** visibly
different (screenshot evidence: textured cyan background + full silhouette detail on the left,
white background + glow artifact on the right) — but the difference is demonstrably **not** a
camera-angle difference (identical silhouette position/scale both sides, confirmed even at 18x
the intended offset). This does not meet the letter of the success criterion, even though it does
prove the harder infrastructure question (two real, independent, non-corrupting renders of the
same scene composited live in one frame) works. Reported honestly as a partial result, not
oversold.

## 7. What to try next (concrete, not another open-ended investigation)

1. **Find the actual shader-constant upload call site.** This is the single highest-value next
   step — notes/07 already scoped this exact gap. Live-debug the title screen's animated camera,
   breakpoint `SetVertexShaderConstantF`, and specifically watch for a `Vector4fCount=4` (or
   4-multiple) call shortly after `BuildViewMatrix`/`BuildProjectionMatrix` fire, in the window
   *before* `CandB` is called. Once found, the stereo hook should overwrite the actual shader
   constants directly (calling `SetVertexShaderConstantF` itself, with a manually-computed
   per-eye view/projection matrix) right before each `CandB` invocation, instead of rewriting the
   CPU-side matrix buffer that this session's evidence suggests is never re-read.
2. **Confirm or rule out the missing-background hypothesis.** Live-debug a real double-invoke
   (reusing notes/12's harness) and diff which of `CandB`'s ~41 direct nested calls actually fire
   on the first invocation versus the second — if one is conditionally skipped, that's the
   background-draw gate; if all fire identically both times but simply produce different visual
   output, the explanation is something else (e.g. a state flag consumed by a shader, not a
   skipped draw call).
3. **If both of the above prove intractable**, fall back to the alternative already flagged in
   notes/10 §6.3: single-render (one `CandB` call, camera correctly offset via whatever mechanism
   #1 above finds) plus post-process reprojection/warp to synthesize the second eye, rather than
   fighting a render pipeline that turns out to have more hidden per-invocation state than
   notes/11/12's analysis could see.

## 8. Cleanup

Confirmed after every run and again after the final run: no `Psychonauts` process running, no
`d3d9.dll` in the game directory, no `.silenced` files under
`WorkResource\Cutscenes\Prerendered`, `INTRO.bik` present under its original name. Six
launch/kill/restore cycles this session, every one wrapped in the same guaranteed-cleanup
pattern as every prior session.

## 9. Disposition

Per the task's own instruction, this result does **not** meet the "got any visible two-perspective
rendering working" bar (the two renders differ, but not by camera angle) — the built proxy
DLL source and binary are being synced to the modding-notes and dev-archive repos as a real,
detailed record of working inline-hook infrastructure and a well-diagnosed partial result, but
are **not** being pushed to the public mod repo this session.
