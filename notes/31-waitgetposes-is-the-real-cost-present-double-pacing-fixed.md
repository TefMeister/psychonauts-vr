# Session 31 — Real Per-Span Timing Finds the True Cost: `WaitGetPoses`, Not `GetRenderTargetData`

Date: 2026-08-17. Picks up exactly where notes/30 left off: §4 of that note specified adding
per-span `QueryPerformanceCounter` timing around the two candidate culprits (`WaitGetPoses`,
`GetRenderTargetData`+readback chain) to settle which one dominates the measured ~8-9ms/frame
VR-bridge cost. This session did that, then chased the result through two further rounds of
instrumentation and A/B testing when the first answer didn't add up — and found a genuine
methodology bug in the process (both this project's own and a Windows/DWM-level one) before
reaching a settled, well-evidenced conclusion. The user's game was closed the entire session; all
testing was this session's own isolated launches (silent intro videos, muted audio), per the task's
explicit freedom to do so.

## 0. Headline results (read this first)

1. **`GetRenderTargetData` and the `LockRect`/`memcpy`/`UpdateSurface` readback chain are
   DEFINITIVELY ruled out as the cost.** Direct measurement: `GetRenderTargetData` costs
   **~0.003-0.015ms** per call (both eyes), the full readback+upload chain costs **~0.35-0.45ms**
   per eye when it actually runs. Combined, well under 1ms/frame — negligible against a ~14-25ms
   frame budget. notes/28's own hypothesis ("GetRenderTargetData is the likely culprit") does NOT
   hold up against real data. The task's suggested GPU-side-shared-surface / every-other-frame
   readback optimizations would be optimizing a cost that isn't there.
2. **`IVRCompositor::WaitGetPoses` is the real, dominant, measured cost: ~25-27ms per call, every
   call, steady-state.** This is CONFIRMED (not inferred) required for `Submit` to succeed — an A/B
   test that skips it entirely restores full baseline framerate but breaks every `Submit` call with
   `VRCompositorError_DoNotHaveFocus` (error 101). It is not a misuse bug: called exactly once per
   frame, exactly as OpenVR's documented usage pattern requires.
3. **Two architecture fixes were implemented, both are correct/textbook, both were measured to have
   ~zero effect on this session's actual framerate** — an honest negative result, not swept under
   the rug (§3, §4). The `WaitGetPoses` cost turns out to be an (apparently) fixed, external floor
   imposed by SteamVR's own compositor process, confirmed independent of window OS focus, window
   visibility (once a separate real confound was controlled for - see below), and call ordering.
4. **A real, separate methodology bug was found and is important for this project going forward**:
   this session's own "silent+offscreen" test convention (moving the game window off-screen via
   `SetWindowPos`) triggers Windows DWM occlusion-throttling, capping real hardware `Present()` at
   **~30fps regardless of VR-bridge state** — a confound that would have completely invalidated any
   fps comparison done under it. Caught by testing bridge-OFF under the offscreen convention and
   finding it was ALSO ~30fps (it should have been ~60fps) — see §2.
5. **A second, retroactive correction to notes/28/29/30's own fps methodology**: the per-Present
   frame counter (`g_frameCounter`) those sessions used increments **twice per real displayed
   frame** (`CandB` invokes the hooked `Present()` once per eye - one suppressed for eye 1, one real
   for eye 2), so notes/30's reported absolute numbers (119fps baseline, 57-60fps bridged) are ~2x
   inflated. The *relative* "~2x regression" finding stays valid (the artifact applies equally to
   both), but the true absolute numbers are roughly half: **~60fps true baseline, ~28-30fps true
   VR-bridge** (before this session's fixes) — see §2c. This actually lines up with the user's own
   stated expectation that this hardware isn't expected to exceed ~60fps for 2x (stereo) rendering.
6. **Submit re-verified working after all changes**, same evidence method as notes/30: proxy log
   `"VRBridge: Submit(eye=N) OK (frame=N)"` lines climbing continuously for both eyes, throughout
   every test configuration in this session including the final combined-fixes build.

## 1. Instrumentation added (`tools/proxy-d3d9/proxy_d3d9.c`)

A small `VRBridgeTimingStat` struct (min/max/sum/count over a throttled ~1sec window, same
log-throttle convention as the rest of this file) plus a `VRBridge_RecordSpan()` helper, both
zero-cost when unused. Wired around:
- `WaitGetPoses` (now in `VRBridge_PumpPoses`, see §4) - logged as `WaitGetPoses`.
- Each eye's `GetRenderTargetData` call alone - logged as `GetRenderTargetData[0]`/`[1]`.
- Each eye's `LockRect`+`memcpy`+`UpdateSurface` promotion chain, only when it actually executes
  end-to-end (not when `D3DERR_WASSTILLDRAWING` causes an early skip) - logged as
  `ReadbackChain[0]`/`[1]`.
- The real hardware `Present()` call itself (`g_pRealPresent`), unconditionally (not gated on the
  VR flag) so the same build can measure a true bridge-OFF baseline too - logged as
  `RealPresentCall`. This turned out to be the single most useful span, and the only genuinely
  direct (non-double-counted) per-real-frame counter in the file.

Two diagnostic-only runtime toggles were also added (`PSYVR_SKIP_WAITPOSES=1`,
`PSYVR_SKIP_PUMPEYE=1`, both default off) purely to enable fast A/B comparisons via relaunch
instead of rebuild-per-experiment. Left in the shipped file, clearly commented as diagnostic-only.

## 2. The investigation, in the order it actually happened (including the wrong turns)

### 2a. First pass: spans sum to ~1.3ms/frame, but the regression is ~9-10ms/frame

Launched the freshly-instrumented, `PSYVR_ENABLE_SUBMIT=1` build in an isolated session (offscreen,
per this session's test convention). Steady-state: `WaitGetPoses` avg ~0.55ms, `GetRenderTargetData`
~0.01ms/~0.003ms, `ReadbackChain` ~0.37ms/~0.35ms per eye - summing to ~1.3ms/frame. But the
existing frame-counter-delta method (same as notes/30) put this session at ~55-58fps, i.e. an
~8-9ms/frame VR-bridge cost - matching notes/30's own numbers almost exactly, but nothing in the
new instrumentation explained where ~8ms of that was going.

### 2b. Second pass: the real hardware `Present()` call itself is the missing cost - or is it?

Added the `RealPresentCall` span (§1) around `g_pRealPresent`. Result: **~31ms average**, with
n≈28-30 calls/sec - i.e. `Present()` alone was consuming almost the entire frame budget, and the
`Present() hit - total frame #N` counter delta (58-60 per ~1sec window) was **exactly double** the
`RealPresentCall` count (28-30) over the same window - the first hint of the double-counting
artifact in point 5 above (`CandB`'s per-eye internal `Present()` calls all increment
`g_frameCounter`, but only the real, un-suppressed one reaches `g_pRealPresent`).

Crucially, this ~31ms `RealPresentCall` cost was present **even with `PSYVR_ENABLE_SUBMIT` fully
unset** (bridge completely off) - a huge red flag that something in the *test methodology*, not the
VR bridge, was the actual cause.

### 2c. The DWM occlusion-throttling confound, found and controlled for

Directly tested: with the game window moved off-screen (`SetWindowPos` to x=-32000, this session's
"silent+offscreen" convention), `RealPresentCall` costs ~31ms (n≈30/sec) **regardless of whether
the VR bridge is on or off**. Moving the SAME running window back on-screen (`SetWindowPos` to
0,0, `SWP_NOACTIVATE` so it still isn't OS-focused) immediately dropped `RealPresentCall` to
**~13-14ms (n=60/sec)** with the bridge off - i.e. **true baseline is ~60fps**, not the ~30fps the
offscreen convention was reporting. This is Windows DWM's well-known behavior of throttling
presentation for occluded/invisible windows - a real confound in this project's own "silent+offscreen"
automated-testing convention that would have invalidated any fps comparison done under it. **All
further measurement this session used a visible (but still unfocused, `SWP_NOACTIVATE`) window.**
This is flagged clearly for future sessions: fps/performance comparisons must use a visible window;
offscreen positioning is fine for pure functional/log-based testing but not for timing.

This also directly explains point 5 above: with the window genuinely visible, the pre-existing
`g_frameCounter`-delta method (used by notes/28/29/30) gives ~119-120 "fps" at baseline - but since
that counter double-counts (§0.5), the TRUE baseline is ~60fps, matching this session's direct
`RealPresentCall` measurement almost exactly (13.3-14.4ms avg -> 59.5-60.2fps) and matching the
user's own stated expectation for this hardware doing 2x (stereo) rendering.

### 2d. With a valid (visible-window) methodology: `WaitGetPoses` is unambiguously the cost

Re-ran bridge-ON with the window visible: `RealPresentCall` now measures fast (~0.9-1.3ms - the
real hardware Present call itself was never slow), but `WaitGetPoses` measures **~25-27ms,
consistently, every call, steady-state**, and the overall real frame rate (directly counted via
`RealPresentCall`'s own per-second sample count) sits at **~30-31fps** - i.e. `WaitGetPoses` alone
accounts for essentially the entire gap between the ~60fps true baseline and the ~30fps VR-bridge
rate.

### 2e. Is `WaitGetPoses` actually required, or just badly used? A/B confirms required.

Relaunched with `PSYVR_SKIP_WAITPOSES=1` (bridge otherwise fully on, `VRBridge_PumpEye` - the
readback+Submit pipeline - still runs every frame): `RealPresentCall` immediately jumped to
**n=60/sec (~60fps)** - the full baseline framerate, fully restored. But every `Submit` call now
fails: `"VRBridge: Submit(eye=0) error=101"` repeating (`VRCompositorError_DoNotHaveFocus` per
`openvr_capi.h`). This is decisive: `WaitGetPoses` is not optional pacing overhead we can drop -
OpenVR's "scene focus" mechanism (visible in SteamVR's own log as `"Capturing Scene Focus"`, first
seen in notes/29) is granted to whichever app actually calls `WaitGetPoses`, and `Submit` requires
that focus. Removing the call removes the cost AND breaks the feature notes/29/30 just proved
working. This settles the task's own question #2a cleanly: `WaitGetPoses` is being called
correctly (once/frame, standard usage) - it is not a misuse bug, it is a real, required,
compositor-imposed cost in this specific test configuration.

### 2f. Is the ~25ms itself something we can influence? Two honest negative results.

- **OS window focus**: gave the game window genuine OS foreground focus (`SetForegroundWindow`, not
  just visibility) mid-session. `WaitGetPoses` cost was unchanged (~25-27ms before and after,
  6 samples each side). Ruled out as an OS-focus-based compositor throttle.
- **The null driver's own declared `displayFrequency: 90.0`** (`drivers\null\resources\settings\
  default.vrsettings`) would predict an ~11.1ms pace if `WaitGetPoses` were cleanly following it.
  The measured ~25-27ms doesn't match that cleanly (closer to a self-imposed ~38-40Hz), suggesting
  the null driver's compositor path uses some other, more conservative internal pacing when there is
  no physical display to derive real vsync timing from - plausible, not proven (no access to
  `vrcompositor.exe`'s source, out of this project's scope to reverse-engineer a third-party SteamVR
  binary). **Flagged honestly as very likely a null-driver/no-physical-HMD testing-environment
  artifact, not necessarily representative of how a real headset would pace this same call** - this
  project has no physical VR hardware to confirm either way.

## 3. Fix #1: redundant double frame-pacing (`PresentationInterval`)

**The finding**: the game's own D3D9 device requests `D3DPRESENT_INTERVAL_DEFAULT` (confirmed via
the existing `CreateDevice` log line: `PresentationInterval=0x0`) - i.e. vsync-on. With the VR
bridge active, `WaitGetPoses`'s own ~25ms compositor wait is a SECOND, independent frame-pacing
mechanism on top of the game's own vsync wait inside `Present()` - two separate pacing sources
serially stacking when only one (the VR/compositor path) should gate the frame, exactly the
pattern real VR titles avoid by running their desktop "mirror" window unthrottled.

**The fix**: `Hook_CreateDevice` and `Hook_Reset` now force
`pPresentationParameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE` whenever
`g_vrSubmitEnabled` is true, before calling through to the real `CreateDevice`/`Reset`. Gated
entirely on the VR flag - the non-bridge path is completely unaffected (confirmed: baseline-off
testing this session used the same build, unaffected).

**Measured effect**: `RealPresentCall`'s own duration dropped from a vsync-dependent ~14ms (with
occasional spikes) to a consistent **<5ms**. This is real and correct - the redundant vsync wait
genuinely was being removed. **However, honestly: this had ~zero measurable effect on overall
framerate this session** (still ~30-31fps before and after) - `WaitGetPoses`'s own ~25ms wait
already dominates the frame so completely that by the time `Present()` was reached, any vsync
deadline it might have blocked on had typically already passed during the `WaitGetPoses` stall, so
the "double pacing" was already being silently absorbed rather than truly adding twice. **Kept
anyway** - it's the textbook-correct architecture (a VR app's desktop mirror should never vsync-gate
against the display when the real HMD path is the actual timing source), it measurably does what it
claims (Present() call duration itself dropped), and it removes a latent risk: if `WaitGetPoses`
ever becomes much cheaper (e.g. against a real HMD at a tight ~11ms pace instead of this test
environment's ~25ms), the redundant vsync wait would very plausibly start mattering again.

## 4. Fix #2: `WaitGetPoses` repositioned to standard OpenVR call-site convention

**The finding**: `WaitGetPoses` was being called from `VRBridge_OnFrameComposited`, invoked from
`CandB_AfterBoth_asm` - i.e. AFTER both eyes have already finished rendering for the current frame.
Standard OpenVR integration guidance calls `WaitGetPoses` as early as possible each frame (right
after the previous `Present()`, at the very top of the next frame's work) specifically so its
compositor-side wait overlaps with the game's own CPU-side per-frame simulation, instead of sitting
as pure added tail latency after rendering is already done.

**The fix**: split the old `VRBridge_OnFrameComposited` into `VRBridge_PumpPoses()` (just the
`WaitGetPoses` call+timing) and a slimmed-down `VRBridge_OnFrameComposited()` (just the per-eye
pump, unchanged). `VRBridge_PumpPoses()` is now called from `Hook_Present`'s tail, immediately
after the real hardware `Present()` returns - the earliest point in this file's hook chain where a
"new frame is starting" boundary naturally exists. `VRBridge_OnFrameComposited` (the eye-buffer
pump, which needs the actual rendered surfaces) stays at its original call site.

**Measured effect, honestly**: **no measurable change** (still ~30-31fps, `WaitGetPoses` still
~25-27ms). This game's render pipeline, as hooked, is fully single-threaded - there is no
concurrent CPU-side game logic actually running WHILE `WaitGetPoses` blocks, regardless of where in
the sequential call chain it sits, so there was never any real work available for the wait to
overlap with. **Kept anyway** - it is the textbook-correct position per OpenVR's own documented
usage pattern, does not regress anything (Submit confirmed still working after the move), and would
become genuinely beneficial if this pipeline were ever restructured to be multi-threaded (a much
larger, out-of-scope change this session did not attempt).

## 5. Submit re-verified after all changes

Same evidence method as notes/29/30: the live proxy log shows `"VRBridge: Submit(eye=0) OK
(frame=N)"` / `"VRBridge: Submit(eye=1) OK (frame=N)"` climbing continuously (frame numbers in the
900s-1600s range across the various test runs this session, both eyes, throttled ~1/sec per eye) in
every configuration tested this session, including the final build with both architecture fixes
combined. The one exception (§2e, `PSYVR_SKIP_WAITPOSES=1`) was an intentional diagnostic that is
explicitly NOT the shipped default behavior (`g_vrSkipWaitPoses` defaults to `FALSE`).

## 6. Honest final numbers (same direct-measurement methodology used throughout this session)

All measured with the game window genuinely visible (not the offscreen convention - see §2c),
`PSYVR_ENABLE_SUBMIT=1` where noted, isolated self-test sessions (title-screen attract mode, not
real gameplay - this project's own game was closed the entire session):

| Configuration | Direct fps (RealPresentCall count/sec) | Dominant cost |
|---|---|---|
| Bridge OFF (true baseline) | **~60fps** (13.3-14.4ms/call) | display vsync |
| Bridge ON, pre-this-session (notes/29's fix, unmodified) | **~30-31fps** | `WaitGetPoses` ~25-27ms |
| Bridge ON, `WaitGetPoses` skipped (diagnostic only) | ~60fps | n/a - but Submit broken |
| Bridge ON, both this session's fixes applied | **~31fps** | `WaitGetPoses` ~25-27ms (unchanged) |

**Honest conclusion**: this session did not close the gap between ~60fps baseline and ~30fps
VR-bridge-active, because the dominant cost (`WaitGetPoses`) turned out to be a real, required,
externally-imposed wait from SteamVR's own compositor process in this specific
null-driver/no-physical-HMD test configuration - not a bug or inefficiency in this project's own
code that further engineering could remove. What WAS accomplished: (1) the original two candidate
culprits were definitively distinguished with real data instead of guessing (`GetRenderTargetData`
cleared, `WaitGetPoses` confirmed dominant); (2) `WaitGetPoses`'s usage was verified correct against
OpenVR's documented pattern, not misused; (3) two genuine architecture improvements were
implemented and verified safe (Submit still works), even though neither moved this session's
measured fps; (4) a real testing-methodology bug (DWM occlusion-throttling under the offscreen
convention) was found and must be avoided in future timing-sensitive sessions; (5) a real,
retroactive correction to three prior sessions' own fps arithmetic was found and documented. Given
the user's own stated expectation that ~60fps is a reasonable ceiling for 2x (stereo) rendering on
this hardware, the ~60fps true baseline is a good, expected number - the open gap is specifically
the VR-bridge activation cost, and it is honestly attributed to SteamVR's compositor rather than
`proxy_d3d9.c`. Whether a real physical HMD would show a much tighter `WaitGetPoses` pace (closer to
the null driver's own declared 90Hz/~11ms) than this ~25-27ms is a real open question this project
cannot answer without physical VR hardware.

## 7. What was and wasn't touched this session

- **Code changed**: `tools/proxy-d3d9/proxy_d3d9.c` - timing instrumentation (§1), diagnostic-only
  bypass flags, the `PresentationInterval` fix (§3), the `WaitGetPoses` repositioning (§4). Build
  clean throughout (same two pre-existing, unrelated warnings as every prior session touching this
  file). `d3d9.dll`/`openvr_api.dll` rebuilt several times across the investigation; final combined
  build deployed into the game directory (`D:\...\Psychonauts\d3d9.dll`), replacing the notes/29
  build that was there (backed up first, to this session's scratchpad, in case a rollback is ever
  needed) - additive/off-by-default, so the user's next launch is unaffected unless
  `PSYVR_ENABLE_SUBMIT=1` is set.
- **Isolated self-testing only**: every launch this session was this session's own (silenced intro
  videos, muted audio, `PSYVR_ENABLE_SUBMIT` set/unset/diagnostic-toggled as needed) - the user's
  own game was closed the entire session, matching the task's explicit freedom to iterate.
- **Read-only**: the null driver's `default.vrsettings` (checked `displayFrequency` again, §2f).

## 8. Concrete next steps for a future session

- If the user or project ever gets access to a real physical VR headset, re-run this exact
  `WaitGetPoses` timing instrumentation (already in the shipped code, just needs
  `PSYVR_ENABLE_SUBMIT=1`) against a real HMD instead of the null driver - this would definitively
  answer whether the ~25-27ms cost is null-driver-specific (likely) or a genuine, unavoidable
  compositor cost (possible but less likely given it doesn't match the declared 90Hz pacing).
- The `PSYVR_SKIP_WAITPOSES`/`PSYVR_SKIP_PUMPEYE` diagnostic toggles are left in the shipped file
  (default off, zero risk) for exactly this kind of future A/B work without needing to rebuild.
- This project's other automated test scripts that rely on `move-window-offscreen.ps1` for anything
  timing-sensitive should be revisited given §2c's finding - fine for functional/log verification,
  NOT valid for fps/performance measurement.
