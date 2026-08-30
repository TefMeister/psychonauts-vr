# Session 33 — FIRST PHYSICAL-HEADSET TEST: Confirmed Working In-Headset; New Shutdown-Hang Bug

Date: 2026-08-17 (evening). **This note was written on a DIFFERENT machine than every prior
note**: the user's own gaming/VR PC (Windows 11 Pro, NVIDIA GeForce RTX 5080, Steam install at
`C:\Steam\steamapps\common\Psychonauts`), not the dev machine. No dev toolchain here (no git —
this note was pushed via `gh api`; no compiler used). The mod was deployed from the released
`psychonauts-vr-0.1.0-alpha.zip` (`d3d9.dll` + `openvr_api.dll` copied next to `Psychonauts.exe`,
binaries byte-identical to the release). Everything below is from the release build — no code
was changed this session.

## 0. Summary (read this first)

1. **THE HEADLINE: the VR-bridge path works with a real physical headset.** The user launched
   with `PSYVR_ENABLE_SUBMIT=1` and SteamVR running with their real HMD, and confirmed the game
   was visible inside the headset. Exact words: *"can't close the game, but it is definitely in
   my vr headset, so in the right direction!"* This is the milestone every note since 27 has
   been pointing at — the first time any physical headset has been used in this project.
   - **Scope of the confirmation**: presence only. The user confirmed the game displays in the
     headset. No detail yet on visual correctness, stereo comfort, scale, or how the (expected)
     lack of head tracking felt. That debrief hadn't happened when this note was written.
2. **The notes/30/31 performance fear did NOT reproduce on real hardware.** With the real HMD
   pacing `WaitGetPoses`, the run sustained ~72 submits/sec per eye for its whole ~2min duration
   (Submit frame counter reached 8459 in ~131s), with `WaitGetPoses` avg ~12.9ms — i.e. the
   HMD's own ~72Hz cadence, not the null driver's pathological blocking. The 28–31fps problem
   appears to have been a null-driver artifact, exactly as notes/31 hoped. (§3)
3. **First-ever real data through `VRBridge_QueryRealGeometry`, and it exercised the real
   off-axis path for the first time**: real IPD 62.88mm, and — unlike the null driver's
   symmetric `l=-1,r=1` — genuinely asymmetric `GetProjectionRaw` (centerOffset ±0.176327),
   so the live per-draw correction ran with `dSrc=openvr kSrc=openvr`. Notes/32 built this path
   but could never execute the `kSrc=openvr` branch; this session proves it runs. Whether its
   output *looks right* in-headset is NOT yet verified — note that the real `k` (±0.176) is
   ~10× larger than the focus-estimate values (~0.017) all monitor testing used. (§2)
4. **NEW BUG (P1 for next dev session): the game hangs on exit when the VR bridge is active,
   and becomes an unkillable zombie process.** Last log lines are `DLL_PROCESS_DETACH` →
   `VRBridge_Shutdown: releasing VR bridge resources` — then nothing, ever. By the time it was
   inspected, SteamVR's processes (`vrserver`/`vrcompositor`/`vrmonitor`) had already exited;
   `Stop-Process -Force` and `taskkill /F` were accepted but the process remained as a 1-thread
   zombie (thread in `Wait`/`UserRequest`, 628 handles) that only a reboot will clear. (§4)
5. **Environment differences from the dev machine worth knowing**: backbuffer (and thus eye
   buffers) here is **800×600**, not the dev machine's 640×480; the real HMD's
   `GetRecommendedRenderTargetSize` is **2496×2688 per eye** (vs. 1656×1840 null-driver),
   making the resolution gap even bigger than notes/32 §4 assumed. (§2)

## 1. Timeline of tonight's four runs (all from `%TEMP%\psychonautsvr_proxy.log`)

| # | pid | launch | PSYVR_ENABLE_SUBMIT | outcome |
|---|-----|--------|--------------------|---------|
| 1 | 11152 | 23:05:35 | unset | Monitor side-by-side only (expected). User, not yet knowing the submit path is opt-in, reported "just a 2d screen with a split screen". |
| 2 | 21268 | 23:19:13 | unset | Same — short run, exited normally. |
| 3 | 22520 | 23:20:20 | unset | Same — ~18s, exited normally. |
| 4 | 21536 | 23:20:43 | **=1** | **THE run: in-headset confirmed. Exit hang (§4).** |

Runs 1–3 confirm the release build's baseline behavior is healthy on this machine: all hooks
installed (`BuildViewMatrix @0x00692480`, `BuildProjectionMatrix @0x006924D0`,
`CandB @0x004FEDA0` — same addresses as the dev machine, as expected for the same exe), stereo
correction running (`dSrc=hardcoded kSrc=focus-est`, `d=±3.250`, focus ~190–233 matching the
known title-screen range), clean DETACH with no hang **when the VR bridge never initialized**.
The env var for run 4 was set via a `Launch-Psychonauts-VR.bat` created this session in the game
folder (sets `PSYVR_ENABLE_SUBMIT=1`, starts the exe — left in place for future tests).

## 2. Run 4: first real-hardware init data (verbatim log)

```
VRBridge_Init: game device adapter = "NVIDIA GeForce RTX 5080"
VRBridge_Init: private D3D9Ex Device A created OK
VRBridge_Init: D3D11 device created OK, feature level=0xB000
VRBridge_Init: VR_InitInternal2 -> token=1 error=0 (No Error (0))
VRBridge_Init: IVRCompositor ready (vtable=6489F5A8)
VRBridge_Init: IVRSystem ready (vtable=64894F3C)
VRBridge_QueryRealGeometry: real IPD = 0.062880 m (62.88 mm), err=0
VRBridge_QueryRealGeometry: eye=0 eyeToHead.x=-0.031440m (-3.144 world units) projRaw l=-1.1918 r=0.8391 t=-1.0355 b=0.6745 centerOffset=-0.176327
VRBridge_QueryRealGeometry: eye=1 eyeToHead.x=0.031440m (3.144 world units) projRaw l=-0.8391 r=1.1918 t=-1.0355 b=0.6745 centerOffset=0.176327
VRBridge_QueryRealGeometry: GetRecommendedRenderTargetSize = 2496 x 2688 per eye (current VR-submit eye buffers are 800x600, matching the game's own backbuffer - see notes/32 Sec4 for why this session did not change that)
VRBridge_QueryRealGeometry: g_vrGeomValid = TRUE - stereo correction now uses real OpenVR-sourced IPD/eye-offset values instead of the hardcoded STEREO_HALF_IPD constant
VRBridge_Init: SUCCESS - g_vrBridgeReady = TRUE (800x600 per eye)
```

Notable, in order of importance:

- **Real asymmetric frustum data at last.** `projRaw` is mirror-asymmetric between eyes
  (`l=-1.1918 r=0.8391` / `l=-0.8391 r=1.1918`), t/b also asymmetric (`t=-1.0355 b=0.6745`).
  So `centerOffset = ±0.176327` and the notes/32 `k = (l+r)/2` substitution went **live for the
  first time**: every subsequent throttled per-draw line reads
  `d=∓3.144 k=∓0.176327 dSrc=openvr kSrc=openvr` (e.g.
  `SVSCF stereo-correct: reg=6 phase=1 xScale=1.5377 d=-3.144 focus=193.98 k=-0.176327
  Y20=-0.483365 Y30=0.7546 dSrc=openvr kSrc=openvr`). Caveat for next session: real `k` is ~10×
  the focus-distance estimates all previous visual verification used (|k|≈0.176 vs ≈0.017), and
  `Y30` correspondingly jumped ~0.47 → 0.75. Nobody has yet confirmed the picture is
  *undistorted* in-headset — only that it's present. If the user reports skew/eyestrain, this
  brand-new `kSrc=openvr` branch is the first suspect. Note also the vertical component: real
  `t/b` are asymmetric too (center is not vertically centered on real HMDs), and the correction
  only shears horizontally — unknown whether that matters visually yet.
- **IPD**: 62.88mm real vs 63.00mm null-driver default — the hardcoded 3.25 world-unit
  half-IPD estimate lands within ~3.4% of this user's real 3.144. Eye transforms symmetric
  (±0.031440m), identity rotation, same as null driver.
- **Resolution gap is worse than planned for**: recommended 2496×2688/eye vs 800×600 actual.
  Also note this machine runs the game at **800×600** (fullscreen, 75Hz), not the dev machine's
  640×480 — the eye-buffer sizing correctly followed the backbuffer, as designed in notes/28.
- The D3D9Ex + D3D11 bridge devices both initialized first-try on this machine/GPU.

## 3. Performance: the real HMD fixed the pacing problem

Steady-state every ~1s window through the whole ~2min run (verbatim sample):

```
VRBridge_Timing: WaitGetPoses           n=72   avg=12.9269ms min=12.3994ms max=13.2289ms
VRBridge_Timing: GetRenderTargetData[0] n=72   avg=0.0046ms min=0.0028ms max=0.0101ms
VRBridge_Timing: ReadbackChain[0]       n=72   avg=0.3111ms min=0.2606ms max=0.4309ms
VRBridge_Timing: RealPresentCall        n=72   avg=0.0234ms min=0.0178ms max=0.0349ms
VRBridge: Submit(eye=0) OK (frame=8458)
VRBridge: Submit(eye=1) OK (frame=8459)
```

- `n=72` per ~1s window with `WaitGetPoses` avg 12.9ms ⇒ the loop is being paced at the HMD's
  own ~72Hz refresh (12.9ms ≈ 72.2Hz wait + ~1ms of work). Sustained: frame counter 8459 over
  the ~131s between init and detach ≈ 71.7/s for both eyes, no degradation, `Submit` returned
  OK continuously to the end. Occasional windows show wider spread
  (`min=5.79 max=17.47`) — normal compositor re-sync jitter, no stalls.
- Compare notes/30/31: null-driver `WaitGetPoses` cost forced ~28–31fps. On real hardware the
  wait *is* the frame budget (13.9ms at 72Hz) and everything else — readback chain ~0.3ms/eye,
  real Present ~0.02ms — fits inside it with ~10× headroom at 800×600. **The performance
  limitation in USAGE.md's known-issues list can be rewritten**: it's not "the mod is slow", it's
  "the null driver paces badly". (Higher eye-buffer resolutions will eat into that readback
  headroom — remeasure when notes/32 §4's resolution work happens.)
- 72Hz is a common standalone-HMD-over-link refresh; the headset model wasn't captured this
  session (it never appears in the proxy log — ask the user, or log
  `Prop_TrackingSystemName_String`/`Prop_ModelNumber_String` next time; worth adding to
  `VRBridge_QueryRealGeometry` for exactly this reason).

## 4. NEW BUG: exit hang → unkillable zombie when VR bridge is active

**Symptom (user-reported live)**: game wouldn't close after the in-headset test.

**Evidence**:

- Log ends (nothing follows, file never touched again):
  ```
  [23:22:57.306] ==== psychonautsvr proxy d3d9.dll: DLL_PROCESS_DETACH (pid=21536) ====
  [23:22:57.307] VRBridge_Shutdown: releasing VR bridge resources
  ```
  In runs 1–3 (bridge never initialized) DETACH completed cleanly — the hang is specific to
  the VR-bridge teardown path actually having resources to release.
- ~40 minutes later: `Get-Process` showed pid 21536 `Responding=False`; `Stop-Process -Force`
  succeeded-but-didn't-kill; `taskkill /F` then reported "no running instance" while
  `Get-Process` still showed the pid alive with exactly **1 thread** (state `Wait`, reason
  `UserRequest`) and 628 handles — the classic terminated-but-for-one-stuck-thread zombie.
  Windows cannot reap it until that thread's blocking call returns; it never will (reboot
  clears it). Being stuck in `UserRequest` (not `Executive`) fits a user-mode
  `WaitForSingleObject`-style wait, e.g. on an IPC event or thread-join, rather than a GPU
  driver kernel call.
- By inspection time **SteamVR itself was no longer running** (no `vrserver`/`vrcompositor`/
  `vrmonitor`). Order not directly observed — either the user closed SteamVR while the game
  was still up, or SteamVR exited during/after the hang. Either way the shutdown path ended up
  waiting on something whose other end (almost certainly the vrserver IPC, or the bridge's own
  submit/WaitGetPoses thread blocked on that IPC) was gone.

**Likely mechanism, for whoever fixes it (has the source; this machine doesn't)**:
`VRBridge_Shutdown` runs inside `DLL_PROCESS_DETACH` — i.e. under loader lock — and appears
to do something blocking: waiting for the bridge/submit machinery to finish, calling into
IVRCompositor/`VR_ShutdownInternal` while a call is in flight, or joining a thread that is
itself parked in `WaitGetPoses` (which, per notes/31, blocks — and with vrserver dying/dead,
may simply never return). Note the DETACH context makes thread-joins doubly dangerous: threads
can't run to completion during process-teardown DETACH, so any join there deadlocks by design.

**Recommended fix shape (next dev session)**: make `VRBridge_Shutdown` non-blocking and
best-effort — set a `g_shuttingDown` flag the per-frame path checks (skip WaitGetPoses/Submit
once set); never join threads or wait on events with `INFINITE` at DETACH (bounded timeout,
then abandon — the process is dying anyway, leaking is fine); consider doing the real teardown
earlier (device release / `Present` hook noticing the device is gone / `WM_CLOSE`-time) rather
than at DETACH; guard every IVRCompositor/IVRSystem call in the teardown path against vrserver
already being gone. Also worth handling the reverse case this bug implies: SteamVR quitting
*while the game is playing* would leave `WaitGetPoses` blocked mid-frame with the same result —
a `VR_IsRuntimeInstalled`/connection-lost check or `IVRSystem::PollNextEvent` for
`VREvent_Quit` (and acknowledging via `AcknowledgeQuit_Exiting`) would cover both directions.
SteamVR sends `VREvent_Quit` and then **kills non-acknowledging apps**, so handling it is not
optional polish — it may be this exact bug's trigger.

## 5. State of this machine after the session

- Game folder: release `d3d9.dll` + `openvr_api.dll` still installed; new
  `Launch-Psychonauts-VR.bat` added (SteamVR first, then double-click — that's the whole
  user-facing flow now).
- Zombie pid 21536 still present until the user reboots; harmless (0% CPU) and does not block
  relaunching the game.
- Full log preserved at `%TEMP%\psychonautsvr_proxy.log` (~1,840 lines, all four runs) — not
  committed here (contains nothing sensitive, just large); grab it from this machine if the
  per-frame detail is ever needed.
- Pushed to **dev-archive only** via `gh api` (no git on this machine); `modding-notes` was NOT
  synced — do that from the dev machine as usual.

## 6. What the next dev session should do with this

1. **Fix the shutdown hang (§4)** — it's the only thing that made tonight's test end badly,
   and it will hit every real user of the submit path.
2. **Debrief the user** on in-headset visual quality: distortion/skew (new `kSrc=openvr` branch,
   §2), scale, comfort, how bad 800×600-upscaled looks, whether the fixed no-tracking view is
   tolerable even briefly. Also get the headset model, and/or add HMD-identity logging (§3).
3. **Rewrite the performance known-issue** in USAGE.md/README per §3 — real hardware sustains
   the HMD's native 72Hz; the 28–31fps figure was a null-driver artifact.
4. **Head tracking is now clearly the top feature gap** — the image is in the headset but
   fixed. `WaitGetPoses` is already returning real pose data every frame at 72Hz; it's being
   discarded.
5. Resolution work (notes/32 §4) now has its real target: 2496×2688/eye vs 800×600 actual.
