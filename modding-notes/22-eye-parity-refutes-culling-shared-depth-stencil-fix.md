# Session 22 — The Draw-Call Asymmetry Was Never Real; Shared Depth-Stencil Buffer Fixed Instead

Date: 2026-08-17. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install). This session picked up exactly where notes/21 left off — the open question
of *why* the eye1:eye2 register-6 draw-call ratio stayed skewed (~9:1) across two full rounds of
unrelated fixes (notes/20, notes/21). The user's game (PID 2340) was already running, mid-level, when
this session started; per this project's standing safety rule ("ask, don't kill, if it's already
running"), it was never touched — no kill, no debugger attach, no file copy into the game directory.
Everything below came from **passively reading the live proxy log of the user's own already-running
gameplay session**, the same technique notes/20 and notes/21 used.

## 1. The core finding: eye1 and eye2 do EXACTLY the same amount of work

notes/21 shipped exact, unthrottled per-real-frame draw-call counters
(`g_svscfCountEye1`/`g_svscfCountEye2`, logged every composite) specifically to replace the old
throttle-sampled "phase=1 vs phase=2" ratio that had produced 167:13 and then 109:15. That
instrumentation had never actually been read from a live session until now. Reading it against the
user's current session (camera position traveled from `(-371, 457, 17)` at the start to
`(52198, -32867, -3980)` later in the session — unambiguously real level traversal, not the title
screen) found:

```
206/206 composite log lines this session: eye1 == eye2, EXACTLY, every single time
Sum across the session: eye1 = 16960, eye2 = 16960  (ratio = 1.000)
Per-frame values ranged 39-150 (varying with scene complexity) but ALWAYS matched between eyes
```

This is a hard, direct refutation of the premise both prior sessions worked from. **There is no
eye1:eye2 draw-call asymmetry.** The frustum-culling-cache-reuse hypothesis this session was asked to
prioritize has nothing left to explain — the two eyes draw the identical number of primitives every
single frame, so no culling divergence is occurring.

## 2. Root-caused why the OLD metric looked skewed: a shared print-throttle, not a shared engine flag

`Hook_SetVertexShaderConstantF`'s "SVSCF stereo-correct: phase=X" log line (the metric notes/20 and
notes/21 actually measured) is throttled by a single `static DWORD s_lastLog` declared *inside* that
function — shared across both phases, since it's the same function instance handling both eyes' calls.
Within any one real frame, eye 1's whole burst of corrections (39-150 calls) fires completely before
eye 2's burst starts (`CandB_BeforeEye1_asm` runs, then the real body, then `CandB_BeforeEye2_asm`).
Since a real frame (~8-16ms) is vastly shorter than the 2-second throttle window, whichever correction
happens to be the first one to run after the throttle reopens almost always belongs to eye 1's burst,
claims the log line, and re-arms the 2-second block — systematically starving eye 2's log line
regardless of real relative work. This is a textbook print-throttle sampling bias, not a signal about
engine state. It fully and mundanely explains why notes/14's Clear() fix, notes/20's Present-guard fix,
and notes/21's Reset/full-backbuffer fixes never moved that ratio: none of them could have, because the
ratio was never measuring what it appeared to measure.

## 3. Redirected the investigation: found and fixed a real structural bug instead

With the culling hypothesis eliminated, the code was re-examined for any asymmetry that *doesn't*
depend on relative draw-call volume. One was found, and it had been sitting in the code (and in a
half-connected clue in notes/14) the entire time:

**Both offscreen eye render targets have always shared the device's single auto depth-stencil surface**
(`EnableAutoDepthStencil=1`, live-confirmed `AutoDepthStencilFormat=75`/`D3DFMT_D24S8`) —
`IDirect3DDevice9::SetDepthStencilSurface` was never called anywhere in `proxy_d3d9.c` before this
session. On top of that, only eye 2 ever explicitly `Clear()`ed (color+depth+stencil); eye 1 never
cleared at all (`SetEyeAndTarget(..., explicitClear=FALSE)` for eye 1 since notes/13).

This exactly matches a clue notes/14 already recorded but didn't fully connect: *"clearing BOTH eyes
flipped which eye's background was missing (eye1 went blank, eye2's fixed itself) rather than fixing
both."* That is the textbook signature of two render passes contending for one physical depth buffer —
whichever eye's `Clear()` runs last in a given frame gets a genuinely reset depth buffer for its own
draws; the other eye's draws run against stale depth data left over from the *other* eye's last
completed pass, causing depth-test rejections that look exactly like missing/stale content. Under the
pre-existing code (eye 2 always clears last, eye 1 never clears), the predicted failure mode is: eye 1
renders against eye 2's leftover depth data every frame → the persistent-looking symptom read as
"frozen"; eye 2 clears to solid black immediately before drawing → any depth-rejected or simply
undrawn pixels show through as black → "dark." This maps onto both reported symptoms simultaneously,
from one single, previously-undiagnosed resource-sharing bug — and needed real level geometry (varied
depth ranges across a frame) to manifest, consistent with the task's own observation that the pause
menu (near-zero real depth complexity) never showed it.

## 4. Fix implemented in `tools/proxy-d3d9/proxy_d3d9.c`

Gave each eye its own **private** depth-stencil surface instead of sharing the device's one auto
depth-stencil surface:

- `SetupStereoSurfaces()`: captures the original auto depth-stencil (`GetDepthStencilSurface`, so it
  can be restored later) and creates two new private depth-stencil surfaces
  (`g_pEye1DepthStencil`/`g_pEye2DepthStencil`, via `CreateDepthStencilSurface`, matching format/size),
  gated into `g_stereoReady`.
- `SetEyeAndTarget()`: now takes a `depthSurf` parameter (replacing the old `explicitClear BOOL`),
  calls `SetDepthStencilSurface` alongside `SetRenderTarget`, and unconditionally `Clear()`s
  color+depth+stencil for **both** eyes every frame — safe now that there's no shared physical resource
  left for the two eyes to contaminate.
- `CandB_AfterBoth_asm`: restores both the real backbuffer *and* the real depth-stencil surface after
  both eye passes complete.
- `Hook_Reset`: releases and recreates all five `D3DPOOL_DEFAULT` surfaces now (the pre-existing three
  plus the two new depth-stencil surfaces), same lifecycle as notes/21's Reset hook.

Build: clean (`build.ps1`), only the pre-existing harmless `Direct3DCreate9` redeclaration warning.
`tools/proxy-d3d9/d3d9.dll` rebuilt.

## 5. Self-test status: NOT live-tested this session, deliberately

The user's own game (PID 2340) was already running, mid-level, for the entire session. Per this
project's standing safety rule this session was explicitly told to follow — "the user is done testing
live for now… if it happens to still be running, ask via task end rather than killing it" — the process
was **never** touched: no `Stop-Process`, no debugger attach, no write to the game directory's
`d3d9.dll` (which that live process has loaded). `validate.ps1` was not run either — its own built-in
`Test-Path $targetDll` safety abort would fire immediately for the same reason notes/21 skipped it
(the game directory's `d3d9.dll` is the live process's own copy), so running it would have added no
information.

This means the actual visual fix (private per-eye depth-stencil buffers resolving dark/frozen) is
**not yet empirically confirmed** — everything in §4 rests on a well-evidenced mechanism (matching
notes/14's own recorded "flip" symptom exactly) and a clean build, not a screenshot or fresh log showing
the symptoms gone. What **is** empirically confirmed, directly and rigorously, is §1-2: the draw-call
asymmetry that drove two prior sessions is provably not real, traced to its exact root cause in the
logging code, with hard numbers (206/206 exact matches) from the user's own live gameplay session.

## 6. What the user needs to do

1. Close the currently-running Psychonauts process (Alt+F4, Steam overlay, or Task Manager).
2. The orchestrating session (or a follow-up) copies `tools\proxy-d3d9\d3d9.dll` into the game
   directory and relaunches.
3. Get back into gameplay far enough to reproduce the previously-split dark/frozen state.

A fresh look should show, either directly (screenshot: both halves lit, both updating) or in the log
(both eyes' `svscfEye1`/`svscfEye2` counts staying equal, as they already reliably were — that part
isn't in question anymore) whether the shared-depth-stencil fix actually resolves the visual symptoms.
If it doesn't, the next-best lead is CandB's own internal per-invocation state (notes/13's original,
still-not-directly-tested "second invocation missing background" hypothesis) — but the depth-stencil
fix should be ruled in or out first, since it's a concrete, previously-undiscovered bug that was
guaranteed to cause *some* visible symptom regardless of whatever else might also be wrong.

## 7. Honest disposition

- **Diagnosis (§1-2)**: fully confirmed, not speculative — hard per-frame numbers from the user's own
  live gameplay session, plus the exact throttle-sharing code responsible for the old misleading metric,
  read and cited by line number.
- **Fix (§4)**: concrete, well-motivated by a mechanism that independently explains a symptom notes/14
  already recorded but didn't resolve, builds clean — but **not yet empirically verified** against the
  actual dark/frozen symptoms, since the live process could not safely be swapped onto the new binary
  this session.
- **Mod repo**: not pushed this session (untested, per this project's standing rule). Workspace notes
  and the updated `proxy_d3d9.c`/`d3d9.dll` synced to modding-notes and dev-archive as a detailed record
  of real diagnostic progress and a well-justified, unverified fix.
