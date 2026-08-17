# Session 21 — Second-Cause Diagnosis: Frozen Left / Dark Right Persist After notes/20's Fix

Date: 2026-08-17. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install). This session picked up a **live, already-running** gameplay session
(PID 7188, started 11:00:12, the same day) — confirmed via the log's own `DLL_PROCESS_ATTACH` marker
to already be running WITH notes/20's fix deployed (the risky `BuildViewMatrix` CPU rewrite disabled,
the `g_eye2Presented` once-per-invoke guard added). The user, actively playing and watching their own
screen, reported: **right eye now correctly tracks camera movement but is "super dark"**; **left eye
still freezes "most of the time," beyond what the game's own focus-loss auto-pause explains** (the user
has been alt-tabbing to communicate, which does pause the game, but the freeze reportedly persists
beyond that).

**Method, same as notes/20**: pure read-only diagnosis against the live log
(`%TEMP%\psychonautsvr_proxy.log`) — no debugger attach, no process interference. This session located
the log's current-session boundary (line 19887, `DLL_PROCESS_ATTACH (pid=7188)`) and analyzed only that
session's ~1900+ lines.

## 1. notes/20's fix is confirmed working exactly as designed — the remaining bugs have a SECOND cause

- **The premature/duplicate-internal-Present bug notes/20 fixed is genuinely gone.** Every single
  `Present() hit` log line this session (331 total) shows `phase=2` (or `phase=0` during the first
  ~5 seconds of startup) — **zero** `phase=1` hits ever reach the real-Present passthrough, meaning
  the `g_eye2Presented` guard is doing exactly its job: eye 1's internal Present is always fully
  suppressed, and eye 2's internal Present only ever fires the real composite+Present once per
  double-invoke cycle. This is real, confirmed, positive evidence for notes/20's fix, not just an
  absence-of-crash inference.
- **The `BuildViewMatrix` pointer instability notes/20 flagged is still present but now provably
  irrelevant** (112 "BVM cache SET" samples this session: 110/112 at `0x0019EAF0`, 2/112 at
  `0x0019E380` — same alternation notes/20 found, now harmless since the CPU-side rewrite through that
  pointer was disabled).
- **Yet the SVSCF (register-6 stereo-correction) phase skew that notes/20 used as supporting evidence
  for the dark-right bug is UNCHANGED by the fix**: 167:13 (eye1:eye2) before → **109:15 after** —
  essentially the same ~8-9:1 ratio. Since the fix that closed the premature-Present path is confirmed
  working (§ above), this skew was **never actually caused by that bug** — it reflects a real,
  independent, still-unexplained difference in how much rendering work happens during eye 1's `CandB`
  invocation vs eye 2's, that neither notes/14's Clear() fix nor notes/20's guard fix ever touched.
  **This is the key finding of this session**: both reported symptoms need a genuinely separate second
  cause, not a residual of the already-fixed bug.

## 2. Three concrete, independently-justified, low-risk fixes implemented

None of these required new live debugging of the running process (which the task explicitly ruled out)
— each is either a well-known D3D9 correctness gap this codebase never handled, or a safe,
overwhelmingly-likely-correct behavior change for our specific compositing use case, so each is safe to
ship even without 100% certainty it's THE root cause:

1. **Force the real hardware `Present` to always blit the full backbuffer** (`Hook_Present` now calls
   `g_pRealPresent(This, NULL, NULL, hDestWindowOverride, NULL)` instead of passing the game's own
   `pSourceRect`/`pDestRect`/`pDirtyRegion` through). Rationale: our composite step unconditionally
   overwrites the **entire** real backbuffer with two full `StretchRect` calls every real frame — if
   the game's own `Present` call assumed a partial/dirty-rect optimization based on what *its own*
   (single-eye, pre-stereo) rendering changed that frame, passing that same partial rect through here
   could cause the hardware Present to only actually flip **part** of our new side-by-side image to the
   screen, leaving the rest showing stale pixels from a prior frame — a mechanism that would look
   exactly like "one half of the screen frozen." This is always safe for our use case regardless of
   whether this specific mechanism is the real cause. Also added logging of the game's original
   `pSourceRect`/`pDestRect`/`pDirtyRegion` pointer values (non-NULL or NULL) into the composite log
   line, so a fresh log read after relaunch can directly confirm or rule this out rather than leaving it
   speculative.
2. **Added an `IDirect3DDevice9::Reset` hook** (vtable slot 16, immediately before `Present`'s slot 17
   — confirmed via the actual LLVM-MinGW `d3d9.h` used by this project's build, not guessed). Previously
   `Reset` was entirely unhandled. `Reset()` invalidates every `D3DPOOL_DEFAULT` resource on the device,
   including all three surfaces this code holds (`g_pRealBackBuffer`, `g_pEye1Surf`, `g_pEye2Surf` — all
   implicitly `D3DPOOL_DEFAULT`); running any of our code after a real `Reset` without releasing and
   recreating those first means operating on dangling COM pointers, which — depending on driver
   behavior — can plausibly produce exactly the asymmetric, hard-to-explain per-surface symptoms
   reported (one eye's surface still showing stale/frozen content if its GPU memory wasn't reclaimed
   yet, the other looking dark/corrupted if it was). This is newly relevant because **this is the first
   session where the user has been alt-tabbing during live testing** — no prior session (title-screen
   only) ever exercised this variable, so this real gap was never caught before now. The fix releases
   all three surfaces (and sets `g_stereoReady = FALSE`, which every other hook already correctly
   guards on) before forwarding to the real `Reset`, then recreates them via the same
   `SetupStereoSurfaces()` used at device creation, only if the real `Reset` succeeded.
3. **Exact per-real-frame eye1/eye2 draw-call counters** (`g_svscfCountEye1`/`g_svscfCountEye2`,
   incremented in `Hook_SetVertexShaderConstantF` whenever the stereo correction actually applies,
   logged and reset once per real composite in `Hook_Present`). Pure instrumentation, no behavior
   change — replaces notes/20's throttle-race-based sampling (which could only show which phase
   happened to be active when a shared 2-second timer polled, not real counts) with an exact number
   per real frame. This directly sets up the next live-log read to either confirm the ~8-9:1 asymmetry
   with hard per-frame numbers or reveal it was a sampling artifact — needed because, per §1, the
   underlying cause of that asymmetry is still genuinely unknown (see §3 below) and not fixed this
   session; only the tooling to nail it down without further guessing was.

## 3. What's still an open, honestly-unresolved question: WHY eye2 does far less rendering work

The ~8-9:1 eye1:eye2 draw-call skew (§1) is real and reproducible across two independent sessions
(before and after notes/20's unrelated fix), but this session did **not** find its root cause — doing
so would need a live debugger trace inside `CandB`'s ~89-deep nested call tree during an actual second
invocation, which is out of scope for a read-only live-log diagnosis session and wasn't attempted.
Leading hypothesis (not confirmed): some internal "already did per-frame setup" state (a common engine
pattern for expensive per-frame-not-per-eye work like shadow-map generation or light accumulation) gets
set by `CandB`'s first invocation and never reset before the second, causing the second call to take a
cheaper/shorter internal path that skips a large fraction of its normal draws. This is plausible and
consistent with notes/13's original, independent finding ("the second CandB invocation's render is
missing its background layer") but remains a hypothesis, not a finding — flagged honestly rather than
implemented as a speculative, risky rewrite of the double-invoke mechanism without evidence.

## 4. Build and self-test status

`build.ps1` ran clean (only the pre-existing harmless `Direct3DCreate9` redeclaration warning that has
appeared in every prior session's build; the tool wrapper reports exit code 1 for this because it
mis-detects native stderr output as failure — the DLL's on-disk timestamp confirms a real successful
rebuild). `tools\proxy-d3d9\d3d9.dll` rebuilt.

**Isolated self-test via `validate.ps1` was deliberately NOT run**: `validate.ps1`'s own built-in safety
check (`if (Test-Path $targetDll) { throw "SAFETY ABORT" }`) would fire immediately, since the game
directory's `d3d9.dll` is currently the user's own live session's copy — confirmed present
(`Test-Path` = `True`) while PID 7188 is running. Beyond that abort (which happens before
`validate.ps1`'s `try`/`finally` block even starts, so it wouldn't touch any process), `validate.ps1`'s
cleanup step (`Get-Process -Name "Psychonauts" | Stop-Process -Force`) is **indiscriminate by process
name** — it would kill the user's live process too if it ever got that far. Running it right now was
judged not safe even though the immediate abort would prevent actual harm, since relying on that as the
only safeguard is fragile. No workaround was attempted (e.g., temporarily moving the live copy aside) —
manipulating the game-directory file the user's actively-running process has loaded was judged an
unnecessary risk to their live session for a self-test that isn't required to ship a build-clean,
carefully-reasoned fix.

## 5. What the user needs to do

The fix is built (`tools\proxy-d3d9\d3d9.dll`, rebuilt just now) but not yet live-tested. To pick it up:

1. Close the currently-running Psychonauts process (Alt+F4, Steam overlay, or Task Manager — whatever's
   natural).
2. The orchestrating session will copy `tools\proxy-d3d9\d3d9.dll` into the game directory and relaunch.
3. Get back into gameplay far enough to reproduce the split-screen state, ideally without alt-tabbing
   this time (or alt-tab once, deliberately, to specifically test whether the new Reset handling
   changes the freeze behavior around a focus-loss cycle).

Once that's done, re-reading the fresh `%TEMP%\psychonautsvr_proxy.log` should show, per fix:
- Fix 1: `origSrcRect=`/`origDestRect=`/`origDirtyRgn=` values in the composite log line — non-NULL
  values would confirm this was a real, previously-unaccounted-for mechanism.
- Fix 2: any `Reset() called` / `Real Reset returned` log lines (especially correlated with alt-tab
  timing) — their mere presence would confirm Reset is actually happening in this game/session, settling
  whether that theory applies at all.
- Fix 3: exact `svscfEye1=`/`svscfEye2=` counts per real frame, replacing the throttle-sampled ratio
  with hard numbers — the natural starting point for whoever picks up §3's still-open question next.

## 6. Honest disposition

- **Diagnosis**: real, evidence-backed for the claim that notes/20's fix works as designed and that
  both remaining symptoms need a second, different cause — not speculation. The SPECIFIC second cause
  for the dark-right asymmetry (§3) is explicitly **not** found this session, stated plainly.
- **Fixes 1 and 2**: concrete, well-justified by general D3D9 correctness reasoning and the specific
  live evidence (alt-tabbing being a new variable this session), builds clean, low-risk/no-plausible-
  downside for this codebase's specific use case — but **not yet empirically confirmed** to resolve
  either symptom.
- **Fix 3**: pure instrumentation, zero behavior change, unambiguously safe.
- **Not yet validated empirically** — no fresh log or screenshot confirms the fixes resolve the two
  symptoms, and an isolated self-test was deliberately skipped for the safety reasons in §4. This is
  stated plainly rather than assumed.
- **Mod repo**: not pushed this session (untested, per this project's standing rule). Workspace notes
  will be synced to modding-notes/dev-archive as a detailed record of real diagnostic progress, per
  usual practice for a well-evidenced-but-unvalidated result.
