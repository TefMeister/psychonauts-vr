# Session 34 — Shutdown-Zombie Fix, VREvent_Quit Handling, and FIRST WORKING HEAD TRACKING

Date: 2026-08-18. Dev machine. Picks up notes/33's priority list in order — and lands the big
one: **head tracking is implemented and visually confirmed working** (monitor/fake-pose testing;
real-headset verification pending on the gaming PC).

## 0. Summary (read this first)

1. **User debrief on notes/33's headset test** (the open questions from §6): stereo **looked
   correct and comfortable** in-headset — so the brand-new `kSrc=openvr` off-axis branch passes
   its first real-hardware inspection, no correction-math bug to chase. Headset is a **Quest 3
   via Virtual Desktop** (latest version) — consistent with the measured 72Hz pacing.
2. **notes/33 §4 shutdown-hang/zombie: root-caused and fixed** — but note it did NOT reproduce
   on the dev machine (§1). Root cause is a documented-DllMain-contract violation, not anything
   environment-specific: `lpvReserved != NULL` at `DLL_PROCESS_DETACH` means the process is
   terminating and every other thread (vrclient's IPC threads, D3D driver workers) is ALREADY
   dead — `VRBridge_Shutdown`'s blocking teardown then waits on corpses under loader lock.
   Fix: skip ALL teardown on process termination (per MSDN, the OS reclaims everything); full
   teardown only on a dynamic FreeLibrary unload. The old DllMain literally had
   `(void)lpvReserved;` — discarding the one parameter that distinguishes the two cases.
3. **VREvent_Quit is now handled, live-verified end-to-end** (§2): user exited SteamVR while the
   game ran → event 700 received → `AcknowledgeQuit_Exiting` → full bridge teardown in 250ms
   (outside loader lock, vrserver still alive) → **game kept running in monitor mode** (stereo
   correction automatically fell back `dSrc=hardcoded kSrc=focus-est`), was NOT killed by
   vrserver, and exited cleanly afterwards. This likely also covers notes/33's actual trigger.
4. **HMD identity now logged at init** (notes/33 §3 ask): slot 28
   `GetStringTrackedDeviceProperty`, verified against the null driver
   (`trackingSystem="null" model="Null Model Number"`).
5. **HEAD TRACKING (6DOF) IMPLEMENTED AND WORKING** (§3): the HMD pose `WaitGetPoses` was
   already returning every frame (and discarding) now drives the rendered view — rotation AND
   position. User reaction watching the fake-pose sway test live: *"whoa did you just get head
   tracking working?!"*. Static-pose regression clean. **Not yet tested with the real headset.**

## 1. Zombie fix details + honest repro status

Local repro was attempted properly before fixing: bridge-active exit with vrserver alive →
clean; bridge-active exit after force-killing all SteamVR processes mid-game → *still* clean
(this machine/driver combo happens not to block). The gaming PC's log nonetheless pins the hang
exactly inside `VRBridge_Shutdown` at DETACH ("releasing VR bridge resources" then silence,
1-thread `UserRequest`-wait zombie), and the fix removes that entire code path on process
termination, so it cannot regress the clean case. DllMain DETACH now logs which case it took
(`process terminating` vs `dynamic unload`). Verified locally: bridge-active exit logs the new
line and exits clean. **Real verification = next gaming-PC session should exit without a zombie.**

## 2. Quit-event handling design

`VRBridge_PollQuitEvents` runs once per frame from `VRBridge_PumpPoses` (same render thread as
every other VR call — no locking): non-blocking `PollNextEvent` (slot 30), reacting to
`VREvent_Quit` (700), `VREvent_ProcessQuit` (701), `VREvent_DriverRequestedQuit` (704). On any of
them: latch `g_vrQuitRequested`, acknowledge (slot 47), run the full existing
`VRBridge_Shutdown` immediately — at a moment when it's actually safe (threads alive, vrserver
responsive) — and let the game continue flat. `VRBridge_Init` refuses re-init after a quit
(guards a later device Reset re-triggering `SetupStereoSurfaces`). SteamVR's kill-timer never
fired in the live test; disconnecting via `VR_ShutdownInternal` appears to be enough to be left
alone.

## 3. Head tracking — how it works

Reuses the ONLY insertion point this project has ever proven live: the per-draw register-6
upload `Transpose(M*P)` (M = unknown World*View, P = known projection). Any rigid X between M
and P is a right-multiplication `WVP*Y`, `Y = P^-1*X*P` — the notes/24 per-eye patch is the
special case where Y only touches column 0. Head tracking needs a full rotation, so its Y is a
dense 4x4 computed ONCE per frame (`VRBridge_UpdateHeadTracking`, fed `renderPoses[0]` from
`WaitGetPoses`) and applied as one extra 4x4 multiply per register-6 upload (~80/frame,
negligible; only when a valid pose exists — otherwise the code path is byte-identical to
notes/32 behavior).

- **Spaces**: OpenVR tracking space and the game's RH eye space share x-right/y-up/-z-forward,
  so the axis map is identity; translation scaled by the established `WORLD_UNITS_PER_METER=100`.
- **Reference**: captured at first valid pose — position + yaw ONLY (pitch/roll stay absolute so
  the game's horizon is level regardless of head tilt at init). `T` = inverse of head motion
  relative to that reference.
- **Composition order**: `WVP * Y_track * Y_eye` — rotate the head first, then apply the per-eye
  IPD offset inside the rotated head frame, matching real VR SDK eye-pose composition.
- **Math validated before building**: `tools/proxy-d3d9/validate_headtrack.py`, 6 numpy checks
  (analytic P^-1; identity pose ⇒ Y=I; full pipeline reproduces `M*T*P` exactly against ground
  truth; reference logic; yaw extraction; composition with the per-eye patch). All pass.
- **Env flags**: `PSYVR_DISABLE_TRACKING=1` (off switch), `PSYVR_FAKE_POSE=1` (synthesized ±25°
  yaw / ±8° pitch / ±6cm sway — this is how it was verified visually on the monitor, since the
  null driver's pose never moves; user watched live and confirmed smooth, solid motion).
- **Regression**: real null-driver run — static pose absorbed into reference, T stays identity,
  zero NaNs, Submit unaffected.

**Known limitations, same as stereo itself**: only register 6 is corrected (skinned geometry's
bone-matrix register still uncorrected — head rotation will make that mismatch more visible than
static stereo did; watch for it in-headset). Only tested against fake + static poses so far.

## 4. What the next gaming-PC session should do

1. Deploy the new build (`d3d9.dll` alone suffices; `openvr_api.dll` unchanged) next to
   `Psychonauts.exe`, launch via the existing `Launch-Psychonauts-VR.bat`.
2. **Expect: the view in the headset follows your head** (both rotation and leaning). If motion
   feels inverted/mirrored on some axis, note WHICH axis — that pins any remaining convention
   error immediately.
3. Confirm the game now **exits cleanly** (no zombie, no reboot needed).
4. Try exiting SteamVR/Virtual Desktop while the game runs — game should drop to flat monitor
   mode and keep working.
5. Check the log's new `HMD identity:` line (curiosity + confirms slot 28 on real vrclient) and
   `HeadTrack:` lines.
6. Comfort debrief: judder (72Hz readback path under real motion), scale of positional motion
   (WORLD_UNITS_PER_METER=100 is calibrated from IPD — leaning should feel 1:1), skinned-geometry
   artifacts while rotating.

## 5. Repo state

- Commits this session: zombie fix, quit-event+HMD-identity, head tracking (each live-verified
  to the extent possible without the physical headset, as described above).
- Synced to dev-archive + modding-notes. Release repo (psychonauts-vr) NOT touched — a
  v0.1.1-alpha with all three changes is drafted as a proposal, pending the user's explicit
  go-ahead per standing project rule.
