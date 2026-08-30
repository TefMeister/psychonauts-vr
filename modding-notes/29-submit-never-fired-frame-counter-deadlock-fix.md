# Session 29 — Submit Was Never Actually Firing: A Real Deadlock Bug, Found And Fixed

Date: 2026-08-17. Picks up immediately after notes/28's deploy: the user launched the game (PID
23696) with `PSYVR_ENABLE_SUBMIT=1`, and init succeeded cleanly per the live proxy log
(D3D9Ex/D3D11 devices created, `VR_InitInternal2` success, `IVRCompositor` obtained, eye buffers
created both eyes, `g_vrBridgeReady = TRUE`) — but across 1763+ frames there was zero log evidence
of the per-frame `Submit` call. This session investigated that gap and found it is **not** a missing
log statement — it is a real, reproducible deadlock bug in `VRBridge_PumpEye`'s port from the
proven standalone POC. Fixed, rebuilt, **not yet live-tested** (the user's live session, still
running the old buggy DLL, was never touched).

## 0. The verdict, precisely

**`IVRCompositor::Submit` has never been called even once from the real game process, for the
entire 1763+-frame session.** This is now confirmed two independent ways, not inferred from silence
alone:

1. **Code-level root cause, found by reading, not guessing**: `VRBridge_PumpEye`'s double-buffer
   index (`bCur`, selecting which `sysmemB[]`/game-readback slot to write this call) was computed as
   `eye->hop1Count % 2` — but `hop1Count` only increments when hop 1 fully succeeds (readback done
   AND promoted into Device A). Since `hop1Count` starts at 0 and the very readback that would let it
   advance depends on `sysmemB[1]` (the `bPrev` slot when `bCur=0`) having been written by some prior
   call with `bCur=1` — which can only happen once `hop1Count` is odd — this is a closed circular
   dependency: `bCur` is permanently stuck at `0`, `sysmemB[1]` is **never written**,
   `pendingB[bPrev]` is **never `TRUE`**, the hop-1 promotion branch never executes,
   `hop1Count` never leaves `0`, and the `Submit` gate (`if (eye->hop1Count >= 2 ...)`) is
   permanently false. This is a genuine deadlock, not a slow ramp-up.
2. **Independent, real-world confirmation from SteamVR's own compositor-side log**
   (`D:\Program Files (x86)\Steam\logs\vrclient_Psychonauts.txt`, read-only, never touched the
   live process): this per-application log ends at `"Capturing Scene Focus"` (13:56:54, ~3s after
   `g_vrBridgeReady=TRUE`, consistent with the first `WaitGetPoses` call landing) and contains
   **nothing after that** — no `"Loading shaders"`, no `"Created shared texture"`. Compared directly
   against `vrclient_submit_test_poc.txt` (the notes/27 POC that DID call `Submit` successfully,
   4/4 runs) — that log shows `"Loading shaders"`/`"Shaders loaded"`/`"Created shared texture 'Scene
   create D3D11, 0' 64x64"` etc. appearing **immediately** (within 15ms) after its own `"Capturing
   Scene Focus"` line, because the compositor only creates its internal scene textures once it
   actually receives a real submitted frame. Psychonauts' log never reaches that point, at any time
   across the whole session — hard, compositor-side proof that no frame was ever received, matching
   the code-level root cause exactly.

This settles the ambiguity the task was scoped around: it is **not** "Submit is working but
unverifiable due to a missing log line" — it is a real bug that prevented Submit from ever being
reached at all. (A missing success-path log statement was *also* true and is fixed below, but it is
not the reason for the silence — the deadlock is.)

## 1. How the bug got in: a subtle porting mistake from a proven design

notes/28 §3 describes `VRBridge_PumpEye` as "a direct, line-for-line-equivalent port of the proven
`poc_dual_device_shared` steady-state loop" — true for almost everything, but one line differs in a
way that matters. In `tools/vr-bridge/poc_dual_device_shared/dual_device_poc.c` (the proven,
3/3-clean-runs standalone version):

```c
for (int i = 0; i < N2; i++) {
    int bCur = i % 2, bPrev = (i + 1) % 2;      /* <-- driven by the LOOP counter i */
    ...
    if (pendingB[bPrev]) {
        ...
        int aCur = hop1Count % 2;               /* <-- driven by hop1Count, correctly */
        ...
        hop1Count++;
```

The POC has two *separate* counters: the loop's own iteration index `i` (which the standalone test
harness increments every iteration regardless of what happens inside), and `hop1Count` (which only
advances on a successful promotion, used correctly only for `aCur`/`aConsume`, Device A's own
double-buffer index). `proxy_d3d9.c`'s per-eye state struct (`VRBridgeEyeState`) never carried an
equivalent to the POC's external `i` — the port reused `hop1Count` for `bCur` too, silently
collapsing the two counters into one. That one-line divergence from an otherwise faithful port is
the entire bug.

## 2. The fix (`tools/proxy-d3d9/proxy_d3d9.c`)

Added a genuinely per-call counter, `frameCount`, to `VRBridgeEyeState`, incremented unconditionally
once per `VRBridge_PumpEye` call (i.e. once per real frame, regardless of whether a promotion or
Submit happens that frame) — mirroring the POC's external `i` exactly. `bCur`/`bPrev` now derive
from `frameCount`; `aCur`/`aConsume` are untouched, still correctly derived from `hop1Count`.

```c
int bCur = eye->frameCount % 2;
int bPrev = (bCur + 1) % 2;
HRESULT hr;
eye->frameCount++;
```

`frameCount` is reset to 0 in `VRBridge_ReleaseEyeBuffers` (alongside the existing `hop1Count = 0`
reset) so a `Reset`-triggered eye-buffer recreation starts the alternation cleanly, matching the
existing lifecycle pattern for every other per-eye state field.

**Also fixed the actual reported symptom's proximate cause**: the `Submit`-success path had no log
statement at all (only the error path logged, throttled 2s) — so even once Submit does fire, nothing
would confirm it without this change. Added a throttled (~1/sec per eye, matching this codebase's
existing throttle convention) success log:
`"VRBridge: Submit(eye=%d) OK (frame=%d)"`. Both the deadlock fix and this log statement were needed
— the deadlock is why nothing fired; the missing log is why, if it eventually does, it would have
stayed silent again.

## 3. Build

`tools/proxy-d3d9/build.ps1` rerun clean — same two pre-existing, unrelated warnings as notes/28
(`EXTERN_C` macro redefinition from the vendored OpenVR header; harmless `dllexport`-redeclaration
note for `Direct3DCreate9`), no new warnings or errors. `d3d9.dll` rebuilt in place at
`tools/proxy-d3d9/d3d9.dll` (115,712 bytes) with `openvr_api.dll` copied alongside it, per the
existing build script behavior. **Not deployed to the game directory** — the user's live session
(PID 23696) is still running the old, buggy DLL and was never touched (no kill, no attach, no write
to the game directory's loaded `d3d9.dll`), consistent with this project's standing safety rule.

## 4. What was and wasn't touched this session

- **Read-only**: the live proxy log (`%TEMP%\psychonautsvr_proxy.log`), SteamVR's `vrserver.txt` and
  `vrclient_Psychonauts.txt`/`vrclient_submit_test_poc.txt` (all under
  `D:\Program Files (x86)\Steam\logs\`), `Get-Process -Id 23696` (existence check only). No debugger
  attach, no process kill, no write anywhere the live game reads from.
- **Edited**: `tools/proxy-d3d9/proxy_d3d9.c` (the `frameCount` fix + the new success log, both
  described above). Rebuilt `tools/proxy-d3d9/d3d9.dll`/`openvr_api.dll` — output only, not deployed.

## 5. Concrete next step — needs the user

**The user needs to close the current Psychonauts session (PID 23696)** so the orchestrating session
can: copy the newly-built `tools/proxy-d3d9/d3d9.dll` and `tools/proxy-d3d9/openvr_api.dll` into the
game directory (replacing the old, buggy pair), relaunch with `PSYVR_ENABLE_SUBMIT=1` still set, and
read the live proxy log for the new `"VRBridge: Submit(eye=%d) OK (frame=%d)"` lines (expect them
starting a handful of frames after `g_vrBridgeReady = TRUE`, roughly once/sec per eye given the
throttle) — and cross-check `D:\Program Files (x86)\Steam\logs\vrclient_Psychonauts.txt` for the
same `"Loading shaders"`/`"Created shared texture"` signature the working POC showed, as the
independent, compositor-side confirmation this session used to catch the bug in the first place.

**Not pushed to the mod repo** — unverified in real gameplay, and this is exactly the kind of
"looks like a fix, needs a real relaunch to confirm" situation this project's own standing practice
(notes/22/23) treats as not yet push-worthy. Per this session's explicit instruction, the mod repo
(`psychonauts-vr`) was not touched regardless of how confident the fix looks on paper.

## 6. Repo sync

- **Workspace** (`C:\Users\Tefa\Documents\PsychonautsVR`): this note; `tools/proxy-d3d9/proxy_d3d9.c`
  fix; `d3d9.dll`/`openvr_api.dll` rebuilt in place (not deployed to the game directory).
- **modding-notes / dev-archive**: synced this session (this note + the changed `proxy_d3d9.c` source
  only — no game assets, consistent with this project's legal/scope boundaries).
- **Mod repo** (`psychonauts-vr`): explicitly NOT touched this session, per this session's own
  instruction — unverified fix, needs a real relaunch first.
