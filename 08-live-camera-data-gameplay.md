# Live Camera Data — Real Changing Values From Both Injection Points

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install, never modified — no files added/changed inside the game directory this
session; only the two approved helper scripts, `silence-intro-videos.ps1` /
`restore-intro-videos.ps1`, touched anything under it, and both were reverted before finishing).

**Goal**: reach real 3D gameplay under x64dbg, hit the two confirmed camera-matrix wrapper
functions (`notes/07-camera-matrix-injection-point.md`) live, and prove real, per-frame-changing
`pEye`/`pAt`/`pUp` and FOV/aspect/near/far values flow through them — the final piece needed
before attempting any actual write-hook.

## Result summary

- **Reaching player-controlled gameplay was blocked** by a simulated-input problem, not a
  debugger or hook problem (details in §2). Extensive troubleshooting narrowed it to a specific,
  well-understood cause.
- **The core milestone was still achieved**, via a different real 3D scene: Psychonauts' own
  attract/title screen renders a live, continuously animated 3D camera orbiting a brain model
  (this is not a static 2D menu — it's a real `D3DXMatrixLookAtRH`/`D3DXMatrixPerspectiveFovRH`
  scene, unlike the actual post-title menu screen observed in the prior session, which had no
  active 3D camera). Breakpoints on both `BuildViewMatrix` (`exe+0x292480`) and
  `BuildProjectionMatrix` (`exe+0x2924D0`) hit repeatedly and yielded **real, smoothly changing
  vector data across dozens of hits over 15+ seconds** — direct proof both hook points carry live
  camera data, not one-shot/static values. Full logs in §3.

## 1. Method

Same `x64dbg_automate` Python client used in prior sessions (MCP tools for x64dbg were checked
again this session via `ToolSearch` — still not registered/available, so the raw Python client
was driven directly, exactly as every prior live-debug session). Scripts lived in the session
scratchpad (disposable, not copied into the workspace):

- `common.py` — shared session helpers (`new_client`, `reattach` via the lockfile-based session
  discovery, `clear_all_breakpoints`).
- `step1_launch.py` / `step1c_resume.py` / `step1d_resume2.py` — launch the EXE directly under
  x64dbg and run past the default entry breakpoint plus a first-chance-exception churn during
  startup (same phenomenon `notes/04-live-debug-findings.md` first flagged, though this session
  saw it recur at several different addresses across restarts, not just the one fixed address
  from that note — always resolved by patiently re-issuing `go(pass_exceptions=True)` in a loop
  until `is_running()` stays `True` across a multi-second settle window).
- `sendkey.py` / `postkey.py` — Windows `SendInput` (both virtual-key and scan-code flavors) and
  `PostMessage`-based keyboard/mouse simulation, with window-focus diagnostics
  (`GetForegroundWindow`, `GetGUIThreadInfo`).
- `shot.py` — screenshots the game's own window (by PID, via `ImageGrab.grab` on its
  `GetWindowRect`) so menu state could be inspected visually between input attempts, rather than
  guessing blind.
- `step4_capture.py` / `step4b_capture_more.py` — set breakpoints on both wrapper functions,
  resume, and on each `EVENT_BREAKPOINT` read the stack args and dereference the vector/float
  pointers, logging every hit with a timestamp.

Extra precaution beyond the required intro-video silencing: `tools/soundvolumeview/mute-psychonauts.ps1`
was run immediately after every new game-process launch this session (both the direct-under-debugger
launch and the later Steam-launched one), muting the process's audio session as a second layer of
protection before any menu/attract audio could play, on top of the pre-menu splash videos being
silenced. Both intro-video renames and the process mute were transient — the videos were restored
and the muted process was killed at cleanup.

## 2. Blocker: simulated input did not reach the game (reaching gameplay)

The main menu / attract screen shows "Press [space-bar glyph] to begin." Extensive attempts to
dismiss it and navigate to New Game all failed to produce any visible state change, despite the
window unambiguously having real OS-level input focus. This was diagnosed thoroughly rather than
assumed:

1. **`SendInput` (virtual-key), `SendInput` (scan-code + `KEYEVENTF_SCANCODE`), legacy
   `keybd_event`, and `PostMessage(WM_KEYDOWN/WM_KEYUP)`** were all tried, individually and
   combined, against `space`, `enter`, `esc`, and `wasd` — no effect on any of them.
2. **The injection mechanism itself was validated as working** in this exact environment/sandbox
   against a control target: a freshly launched Notepad window received `SendInput`-driven `wasd`
   keystrokes correctly (confirmed visually — the literal text `wasd` appeared in the edit
   control). This ruled out a blanket sandbox/session/window-station restriction on synthetic
   input.
3. **Window focus was independently confirmed correct** via `GetForegroundWindow()` (matched the
   game's HWND) and `GetGUIThreadInfo()` on the game's own GUI thread (`hwndActive` and
   `hwndFocus` both equal to the game window) — not just "SetForegroundWindow returned success,"
   actual OS focus state was read back and matched.
4. **Tested across three different process/debugger configurations**, all with identical
   (non-)results: (a) launched directly under x64dbg with `pass_exceptions=True`, (b) launched
   normally via Steam (`steam://rungameid/3830`) and then attached to with x64dbg, and (c) fully
   detached from any debugger (`client.detach()`, the real debug-level detach, not just closing
   the client connection) so the process ran completely independent of any debugger. Input still
   didn't register in any of the three.
5. **Ruled out a missing-Steam-context config problem**: `profiles\profile 1\Profile 1- Raz.ini`
   (an existing profile on this install) has a full, valid `[Input]` keybinding table
   (`Jump=Spacebar`, `MoveForward=W`, etc.), so the game's config/keybinding system is loading
   fine — the title screen's "any key" prompt just isn't reacting to synthetic input at all.
6. **Root cause narrowed to DirectInput's hook-based input path**: enumerating all windows owned
   by the game process (not just the visible top-level one) turned up a hidden helper window with
   class name **`DIEmWin`** — DirectInput's internal keyboard-emulation window, used when the
   game's input device runs in a hook/emulation mode rather than reading the standard Win32
   message queue. Notepad (which worked) consumes ordinary `WM_KEYDOWN`/`WM_CHAR` messages;
   Psychonauts' menu/attract-screen input appears to be serviced through this different,
   hook-based DirectInput path, which did not respond to any of the four injection methods tried
   in this sandboxed environment.

This is a real, reasonably-bounded blocker, not a stopping-too-early call — four independent
injection mechanisms, a validated-working control case, confirmed correct OS focus state, and
three debugger configurations were all tried before concluding the block sits specifically in how
this environment's synthetic input reaches DirectInput's hook-based consumption path. A future
session should try: a genuine hardware-level input source (e.g. a physical/virtual HID driver
rather than `SendInput`), or driving input from *inside* the process via the debugger itself
(write directly into DirectInput's keyboard state buffer / call `IDirectInputDevice8::Acquire` +
poll through x64dbg's own memory-write primitives instead of OS-level input injection).

## 3. What was captured instead: live data from both hook points

Rather than stall further on menu navigation, the two breakpoints were set and left running while
the game sat on its title/attract screen — which was noticed to render a **real, continuously
animated 3D camera** (the rotating brain, with a small character walking on top of it), i.e. an
actual `D3DXMatrixLookAtRH`/`D3DXMatrixPerspectiveFovRH`-driven scene, unlike the static/2D main
menu screen the prior session observed (`notes/07-camera-matrix-injection-point.md` §4, where
`D3DXMatrixLookAtRH` never fired at all). This gave a legitimate way to validate both hook points
end-to-end without needing to solve the input blocker first.

Two capture runs (continuing the same live session), ~15 seconds and ~55 breakpoint hits total
(15 `BuildViewMatrix` + 45 `BuildProjectionMatrix`, plus a handful of unrelated `OTHER` breakpoint
events from other debugger-internal stops that were skipped). Full raw log:
scratchpad `capture_log.txt` (not copied into the workspace — representative excerpts below are
the complete evidence needed).

### `BuildViewMatrix` (`exe+0x292480`) — real per-frame changing eye/at/up

```
VIEW  hit#1  t=3.81s  eye=(-39.910, 593.014, 40.629)  at=(-23.907, 399.239, 27.748)  up=(0, -0.0663, 0.9978)
VIEW  hit#4  t=4.31s  eye=(-40.363, 591.972, 41.554)  at=(-24.495, 399.822, 28.781)  up=(0, -0.0663, 0.9978)
VIEW  hit#7  t=7.36s  eye=(-40.373, 591.786, 41.766)  at=(-24.532, 399.970, 29.015)  up=(0, -0.0663, 0.9978)
VIEW  hit#10 t=10.40s eye=(-40.388, 591.564, 42.018)  at=(-24.580, 400.143, 29.293)  up=(0, -0.0663, 0.9978)
VIEW  hit#13 t=13.44s eye=(-40.417, 591.360, 42.323)  at=(-24.632, 400.223, 29.617)  up=(0, -0.0663, 0.9978)
```

Every logged `eye`/`at` value is distinct and drifts smoothly (monotonic, small-magnitude changes
frame to frame, consistent with a slow orbit/pan animation) — this is unambiguously live,
continuously-updating camera data, not a cached/static one-shot value. Interesting secondary
observation: `BuildViewMatrix` fires **three times per animation step** in this scene (e.g. hits
1/2/3 all share one `eye` but the 2nd/3rd share a different `at`/`up` from the 1st) — likely two
or three separate view matrices being built per frame in this particular scene (e.g. a reflection
or secondary camera in addition to the primary), worth confirming once real gameplay is reachable
since it affects how many times a per-eye stereo hook would need to fire.

### `BuildProjectionMatrix` (`exe+0x2924D0`) — real, stable-while-unchanged FOV/aspect/near/far

```
PROJ  hit#1  t=0.25s  rawFov=104.0  aspect=1.33333337  zn=10.0  zf=50000.0
PROJ  hit#20 t=6.60s  rawFov=104.0  aspect=1.33333337  zn=10.0  zf=50000.0
PROJ  hit#45 t=15.22s rawFov=104.0  aspect=1.33333337  zn=10.0  zf=50000.0
```

Constant across all 45 hits, as expected — this matches the projection wrapper's documented
behavior from `notes/07` (only rebuilt when FOV/aspect/near/far actually change, not every
frame), and confirms real values are flowing: `rawFov=104.0` (a plausible raw pre-conversion FOV
value — recall the wrapper divides this by a global `double` at `0x703698` and multiplies by a
global `float` at `0x793444` before calling `D3DXMatrixPerspectiveFovRH`, so 104.0 is *not*
itself the final radians value passed to D3DX9), `aspect=1.3333333` (exactly 4:3, matching the
`640×480` windowed backbuffer from the prior `CreateDevice` session), `zn=10.0` /
`zf=50000.0` (near/far planes in game units).

**Both hook points are now confirmed carrying real, live, changing data** — this closes out the
observation-only phase of the injection-point work. No matrices were modified and no code was
patched this session, per the task's observation-only constraint.

## 4. Cleanup

- Debugger session terminated via `client.terminate_session()` (kills the debuggee since it was
  the active target), followed by a manual `Stop-Process` on one leftover `x32dbg.exe` (a second,
  now-orphaned debugger instance from the earlier direct-launch attempt whose session had already
  been superseded by the Steam-attach session — `terminate_session()` only tears down the
  *current* session, so the first debugger process needed an explicit kill) and removal of its
  stale `xauto_session.*.lock` file.
- `restore-intro-videos.ps1` run; verified all four splash videos
  (`INTRO.bik`/`DFLogo.bik`/`MajescoLogo.bik`/`transgaming.bik`) are back under their original
  names (no `.silenced` files remain in `WorkResource\Cutscenes\Prerendered\`).
- Verified via `Get-Process` that no `x32dbg`/`x64dbg`/`Psychonauts`/stray `notepad` processes
  remain, and via `Test-Path` that no `d3d9.dll` (or any other file) was left in the game
  directory.

## 5. Recommended next step

See the top of `notes/00-status.md` for the current recommendation — this session's evidence
(both hook points confirmed live and stable) makes the natural next step **prototyping an actual
write-hook**: install a real inline/detour hook at `exe+0x292480` (or `exe+0x2924D0`), and as a
first behavior-modifying experiment (not full stereo), compute the camera's right vector from
`at - eye` and `up` (standard `cross(normalize(at-eye), up)`), then offset `pEye` by a small
fixed distance along that axis before letting the real function run — and confirm visually (e.g.
via the same `shot.py` screenshot mechanism used this session) that the rendered scene shifts
sideways as expected. That's the first real step past pure observation. The input-simulation
blocker in §2 should be revisited in parallel or first, since actual gameplay (not just the title
screen) is still needed to validate a stereo hook under real player camera control, not just an
attract-mode animation.
