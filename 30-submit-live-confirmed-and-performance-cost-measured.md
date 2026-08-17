# Session 30 — Submit Live-Confirmed Working, VR-Bridge Performance Cost Measured With Real Evidence

Date: 2026-08-17. Picks up immediately after notes/29's fix: the user closed the old buggy session
and relaunched with the fixed DLL (`PSYVR_ENABLE_SUBMIT=1`, PID 18284, started 14:12:00) — **this is
the live test notes/29 asked for.** This session read the live proxy log and SteamVR's compositor
log passively (no attach, no kill, no write to the running process) to (1) confirm the fix actually
works, and (2) investigate the user's separate report that the game feels noticeably laggier since
the VR-bridge integration was added, with real measured evidence rather than a guess.

## 0. Summary (read this first)

1. **`IVRCompositor::Submit` is now definitively, verifiably firing every frame.** Confirmed two
   independent ways: the live proxy log shows the new `"VRBridge: Submit(eye=%d) OK (frame=N)"` line
   for both eyes, throttled ~1/sec, with the frame counter climbing continuously (frame=3 at
   14:12:09 up through frame=1637+ by 14:12:57, still climbing as of this write-up) — and SteamVR's
   own compositor-side log (`vrclient_Psychonauts.txt`) shows `"Loading shaders"` /
   `"Shaders loaded in 0.030342 seconds"` / six `"Created shared texture 'Scene create D3D11, 0/1'
   640x480"` lines appearing within **23ms** of `"Capturing Scene Focus"` — the exact signature the
   working notes/27 POC showed and the broken notes/29 session never reached. notes/29's deadlock fix
   is confirmed working, live, in the real game process.
2. **The "noticeably laggier" report is real and now quantified, not dismissed.** Using the
   pre-existing throttled `"Present() hit - total frame #N"` log line (present since notes/13, unrelated
   to this session) as a hard per-real-frame counter sampled ~1/sec, six separate pre-VR-bridge game
   sessions earlier in the same log file (spanning ~11:23 to ~13:55, ~1.7 hours of real play, several
   different PIDs) average a rock-solid **~119 fps (8.4ms/frame)**. Both VR-bridge sessions — the
   OLD broken one (PID 23696, Submit never fires) and the NEW fixed one (PID 18284, Submit fires every
   frame) — average **~57-60 fps (16.8-17.6ms/frame)**. That is a real, reproducible **~2x frame-time
   regression (+8.3 to +9.3ms/frame)**, not a guess and not placebo.
3. **The regression is present with or without Submit actually succeeding**, which is itself a real,
   useful finding: the broken session (57 fps, zero successful Submits, all 835s of it) and the fixed
   session (60 fps, Submit succeeding every frame) cost almost exactly the same. This rules OUT
   `IVRCompositor::Submit` itself (and the OpenVR IPC round-trip inside it) as the dominant cost —
   since it's barely called at all in the broken session yet the cost is identical. It points at the
   code that runs **unconditionally every frame regardless of Submit's outcome**:
   `VRBridge_OnFrameComposited`'s `WaitGetPoses` call (once/frame) and `VRBridge_PumpEye`'s
   `GetRenderTargetData`+`LockRect`+`memcpy`+`UpdateSurface` chain (twice/frame, once per eye).
4. **Honest limitation**: the available evidence (aggregate frame-time deltas from the existing
   throttled log line) cannot cleanly separate `WaitGetPoses`'s cost from the CPU-readback pipeline's
   cost — both run unconditionally every frame in both sessions, so both are equally consistent with
   the data. notes/28's own hypothesis ("`GetRenderTargetData` is the likely single expensive
   culprit") is **partially supported, not confirmed**: it's very likely part of the ~8-9ms, but
   `WaitGetPoses` — which OpenVR documents as a call that can legitimately block/pace on the
   compositor's own frame timing — is at least as plausible a contributor and wasn't distinguished
   from it this session. See §3 for why, and §4 for the precise next step to resolve it.
5. **No code changes this session.** `proxy_d3d9.c` is unchanged from notes/29's fix. Per this
   session's own explicit instruction, an optimization (e.g. every-other-frame submission) was
   considered but NOT implemented, because it could not be verified without redeploying into the
   live process — and this session was not asked to, and should not, touch the user's live game
   session again. Documented clearly instead, per the task's own fallback instruction.

## 1. Submit fix: confirmed working, with concrete evidence

### 1a. Proxy log evidence

```
[2026-08-17 14:12:06.098] VRBridge_Init: SUCCESS - g_vrBridgeReady = TRUE (640x480 per eye)
[2026-08-17 14:12:09.409] VRBridge: Submit(eye=0) OK (frame=3)
[2026-08-17 14:12:09.413] VRBridge: Submit(eye=1) OK (frame=3)
[2026-08-17 14:12:10.435] VRBridge: Submit(eye=0) OK (frame=36)
...
[2026-08-17 14:12:57.599] VRBridge: Submit(eye=0) OK (frame=1637)
[2026-08-17 14:12:57.600] VRBridge: Submit(eye=1) OK (frame=1637)
```

100 `"Submit(eye=N) OK"` lines logged by the time of this check (throttled ~1/sec per eye, so this
represents continuous per-frame success across ~50 seconds of real gameplay so far, not a one-off),
frame counter climbing steadily for both eyes in lockstep — exactly the expected steady-state
behavior of the fixed `frameCount`-driven double-buffer selector.

### 1b. Independent SteamVR compositor-side confirmation

`D:\Program Files (x86)\Steam\logs\vrclient_Psychonauts.txt`, read-only, same method notes/29 used to
prove the OLD session never reached the compositor:

```
Old (broken) session, PID 23696:
Mon Aug 17 2026 13:56:54.386 [Info] - Capturing Scene Focus
  (log ends here - nothing after, ever, for the whole session)

New (fixed) session, PID 18284:
Mon Aug 17 2026 14:12:08.682 [Info] - Capturing Scene Focus
Mon Aug 17 2026 14:12:09.363 [Info] - Loading shaders
Mon Aug 17 2026 14:12:09.393 [Info] - Shaders loaded in 0.030342 seconds
Mon Aug 17 2026 14:12:09.408 [Info] - Created shared texture 'Scene create D3D11, 0' 640x480 (1 mips)
Mon Aug 17 2026 14:12:09.408 [Info] - Created shared texture 'Scene create D3D11, 0' 640x480 (1 mips)
Mon Aug 17 2026 14:12:09.409 [Info] - Created shared texture 'Scene create D3D11, 0' 640x480 (1 mips)
Mon Aug 17 2026 14:12:09.410 [Info] - Created shared texture 'Scene create D3D11, 1' 640x480 (1 mips)
Mon Aug 17 2026 14:12:09.411 [Info] - Created shared texture 'Scene create D3D11, 1' 640x480 (1 mips)
Mon Aug 17 2026 14:12:09.411 [Info] - Created shared texture 'Scene create D3D11, 1' 640x480 (1 mips)
```

The compositor only ever creates its internal per-eye scene textures once it has actually received a
real submitted frame (this is the exact mechanism notes/29 used to prove the old session's silence).
Here it does so **23ms after** `"Capturing Scene Focus"` — three texture-creation lines each for
eye 0 and eye 1 (`640x480`, matching the proxy log's own `"640x480 per eye"` buffer size exactly) —
and the timestamp of the first one (`14:12:09.408`) lands **within 1ms** of the proxy log's own first
`"Submit(eye=0) OK (frame=3)"` line (`14:12:09.409`). Two independently-logged systems (the injected
DLL and SteamVR's own compositor process) agree to the millisecond. This is as close to
airtight as evidence gets without a physical headset: **Submit is genuinely reaching the compositor,
every frame, right now, in the live session.**

## 2. Performance cost: measured, not guessed

### 2a. Method

The existing `Hook_Present` code (unchanged since notes/13/21) logs `"Present() hit - total frame
#N phase=2 (throttled ~1 log/sec)"` on every real hardware Present, throttled to ~1 log line/sec via
`g_lastPresentLogTick`, but `N` itself is `g_frameCounter` — an `InterlockedIncrement`, unconditional,
real per-Present counter, not throttled. Between any two consecutive throttled log lines, `(N2 - N1) /
(t2 - t1)` is therefore a real, precise average fps over that ~1-second window, exactly the technique
the task suggested. The proxy log file (`%TEMP%\psychonautsvr_proxy.log`) has accumulated continuously
across every game session today (multiple `DLL_PROCESS_ATTACH`/`DETACH` pairs in one file, oldest
data from ~11:18), including six full sessions **before** `PSYVR_ENABLE_SUBMIT` was ever set,
providing a real same-machine, same-hardware, same-log-format "before" baseline — not an assumption
or a different session's archived numbers.

### 2b. Results

| Session (PID) | VR bridge | Submit firing? | Samples | Span | Avg fps | Avg ms/frame |
|---|---|---|---|---|---|---|
| 2340 | off | n/a | 932 | 932s | 119.00 | 8.40 |
| 9188 | off | n/a | 2978 | 2978s | 119.68 | 8.36 |
| 16672 | off | n/a | 289 | 289s | 117.73 | 8.49 |
| 21588 | off | n/a | 438 | 438s | 118.71 | 8.42 |
| 7052 (notes/28 session) | off | n/a | 3643 | 3657s | 118.71 | 8.42 |
| 2784 | off | n/a | 776 | 776s | 118.52 | 8.44 |
| **23696 (notes/29, broken)** | **on** | **NO (deadlocked)** | 827 | 836s | **56.92** | **17.57** |
| **18284 (this session, fixed)** | **on** | **YES, every frame** | 113 | 115s | **59.57** | **16.79** |

Six independent pre-VR-bridge sessions, spanning ~1.7 hours of real cumulative play across several
processes, all land within a tight 117.7-119.7 fps band — a stable, repeatable baseline, not a
one-off number. Both VR-bridge sessions (regardless of whether Submit itself ever succeeds) land in a
tight 57-60 fps band. **The user's "noticeably laggier" report is real, large (roughly half the frame
rate, +8-9ms added per frame), and now has hard, reproducible, same-machine evidence** — not
dismissible as "expected/placebo."

### 2c. A second real finding buried in the same numbers

The fixed session (60fps, Submit succeeding) is not slower than the broken session (57fps, Submit
never succeeding) — if anything marginally faster, well within session-to-session/content variance.
**This means the newly-fixed, actually-working `Submit` call is not adding the cost.** The dominant
cost was already fully present in notes/29's broken build, which never got past the hop-1 promotion
gate — i.e. it comes from code that runs unconditionally every frame in `VRBridge_OnFrameComposited`
regardless of what happens downstream:
- `IVRCompositor::WaitGetPoses` — called once per frame, unconditionally, before either eye is
  pumped. OpenVR's own documented contract for this call is that it can legitimately block/pace to
  the compositor's own frame timing — a real, IPC-crossing (separate `vrcompositor` process) call,
  not a trivial local one.
- `VRBridge_PumpEye`'s hop-1 readback (`GetRenderTargetData` + a `LockRect`/`memcpy`/`UpdateSurface`
  chain when the previous frame's buffer is ready) — called twice per frame (once per eye),
  unconditionally, regardless of whether hop 2 (the part that actually calls `Submit`) ever runs.

## 3. Which part costs the most: partially confirmed, honestly not fully resolved

notes/28 §2c already measured `GetRenderTargetData` itself as **not** a stall in isolation
(`avg=0.0097ms` over 150 iters, synthetic `Clear()`-only 1920x1080 workload) — so if the readback
chain is a real contributor here, it's more likely the **combination** (two `GetRenderTargetData`
calls + two `LockRect`+`memcpy`+`UpdateSurface` chains, every frame, against a real, GPU-busy scene
rather than a trivial `Clear()`) than `GetRenderTargetData` alone being newly slow.

This session's aggregate before/after timing data can confirm the **VR-bridge path as a whole** costs
~8-9ms/frame and can confirm Submit itself isn't the driver of that cost (§2c) — but it **cannot**
cleanly separate `WaitGetPoses`'s share from the readback chain's share, because both run
unconditionally every frame in both the broken and fixed sessions equally; the available log line
(`Present() hit - total frame #N`) only has one data point per second, not per-call breakdown. Rather
than guess which of the two dominates, this is flagged honestly as the next well-scoped step (§4)
instead of overclaiming "confirmed: it's `GetRenderTargetData`" the way the task's framing invited.

One data point worth noting: 60fps (16.7ms) is suspiciously close to a clean vsync-style boundary,
while the null driver's own `default.vrsettings` (`drivers\null\resources\settings\default.vrsettings`)
declares `"displayFrequency": 90.0` — if `WaitGetPoses` were pacing to that value the floor would be
~11.1ms, not ~16.7ms, so a clean single-vsync-at-90Hz explanation doesn't fit cleanly either. This is
noted as a loose end, not resolved — plausibly compositor-side per-frame processing overhead on a
headless/null driver behaves differently than a real HMD's vsync, but this wasn't chased further this
session since it would require code changes to test and this session was scoped to stay read-only
against the live process.

## 4. Concrete next step to fully resolve §3 (not done this session, needs a redeploy+relaunch)

Add `QueryPerformanceCounter` timing around three spans separately inside
`VRBridge_OnFrameComposited`/`VRBridge_PumpEye` (guarded by the same `PSYVR_ENABLE_SUBMIT` flag, so
zero cost when the flag is off): (a) the `WaitGetPoses` call alone, (b) each eye's
`GetRenderTargetData` call alone, (c) each eye's `LockRect`+`memcpy`+`UpdateSurface` chain alone —
logged throttled ~1/sec like the existing convention. This would definitively settle whether
`WaitGetPoses`'s IPC/pacing cost or the CPU-readback chain dominates the ~8-9ms, which in turn
determines which optimization is worth pursuing (e.g. an every-other-frame readback if the readback
chain dominates vs. investigating `WaitGetPoses` call placement/threading if it dominates). This
needs the user to close and relaunch the game again, so it was deliberately not attempted this
session per the explicit instruction not to risk the just-verified working `Submit` path on an
unverified change, and per the standing project rule against touching a live-loaded DLL.

## 5. What was and wasn't touched this session

- **Read-only**: the live proxy log (`%TEMP%\psychonautsvr_proxy.log`, all of it — including
  sessions from earlier today before this task began), SteamVR's `vrclient_Psychonauts.txt`, the null
  driver's `default.vrsettings` (just to read `displayFrequency`), `Get-Process -Id 18284` (existence
  check only). No debugger attach, no process kill, no write anywhere the live game reads from.
- **No code changes.** `proxy_d3d9.c` is byte-identical to notes/29's fix. The optimization idea in
  §4 is documented only, deliberately not implemented, per this session's own instruction to not risk
  the newly-verified-working `Submit` path on a speculative, unverifiable-this-session change.

## 6. Repo sync

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; `notes/00-status.md` updated.
- **modding-notes / dev-archive**: synced this session (this note only — no source changed, no game
  assets, consistent with this project's legal/scope boundaries).
- **Mod repo** (`psychonauts-vr`): explicitly NOT touched this session, per the task's explicit
  instruction that the user must personally test and approve before anything gets pushed there.
