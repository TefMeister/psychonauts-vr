# 60 — SetPendingLevel live-verified working (first time, no crash); mouse-camera-yaw hunt redirected mid-session, then blocked on window focus

**Date:** 2026-08-24, dev machine. Continuation of the void-behind-player thread (notes/59). User
reframed candidate 1 mid-session: in flat play the mouse-driven camera is already fully decoupled
from Raz's body facing (you can run with Raz facing any direction while the camera looks wherever
the mouse points, and flat rendering follows the CAMERA, not the body) — only in VR does looking
somewhere the mouse-camera isn't pointed produce the void. This means the engine already has a
persistent camera-yaw/pitch state driven by mouse input each frame, independent of body/entity
logic — a much lower-risk candidate-1 injection point than anything inside `CandB`'s per-entity tick
(notes/59). This session's job was to find that mouse-camera-yaw storage, and separately prove out
`SetPendingLevel` (notes/55) as a real-gameplay test harness. Two different outcomes:

## Part 2 (done first, out of order — big result): SetPendingLevel WORKS, live-verified

Added an opt-in, off-by-default hotkey (`PSYVR_LEVEL_JUMP_KEY=1`, F12) in `CandB_AfterBoth_asm`
(runs unconditionally every real frame, no VR bridge/headset needed) that calls exactly the
function notes/55 found and no session had ever actually invoked:

```c
void *engine = *(void **)0x78BC20;
((void (__thiscall *)(void *, const char *, BOOL))0x4FFA40)(engine, g_levelJumpCode, TRUE);
```

`g_levelJumpCode` defaults to `"workresource\\levels\\CAJA.plb"` (Sasha's Lab — the one notes/55
confirmed via a literal loading-screen string match, the safest bet for actually being a real
loadable level), overridable via `PSYVR_LEVEL_JUMP_CODE` for any other catalogued code.

**Live-tested from a cold title screen, first try: no crash, and the log shows a real world-space
camera jump within ~5 seconds**:

```
LevelJump: F12 pressed - engine=05D29560, calling SetPendingLevel("workresource\levels\CAJA.plb")
LevelJump: SetPendingLevel call returned (no crash) - watch for a world-space BVM camera-coord jump...
...
BVM cache SET: pOut=0x0019EAF0 eye=(2590.31,1303.26,-1418.06) at=(2590.31,1147.85,-1418.06) right=(-1.0000,0.0000,0.0000)
```

Coordinates in the thousands (not menu-space ~100s, not title-screen-near-origin) — the documented
notes/39 success signal. Repeated on a second launch with the same result. **This is the first time
this project has actually invoked `SetPendingLevel` live** — every prior mention (notes/55, and
notes/59's own recommendation) was static/theoretical. It works, and it's a MUCH more reliable path
into real gameplay than `enter_gameplay.ps1`'s door-timing dance (flagged unreliable since session
54, never fixed) — genuinely useful beyond this session for any future test needing a real level.

**One real limitation found alongside it, separate from the mechanism itself**: the freshly-loaded
level auto-pauses with a "while you were away" dialog (a window-focus-loss pause feature), and this
session could not reliably bring the game window to real OS foreground/focus to dismiss it and
actually look around. Tried, in order: `send_key.ps1`'s existing retry-loop technique (worked for
menu navigation in past sessions, failed consistently this session — 5+ retries, still failing),
`AttachThreadInput` + `BringWindowToTop` + `SetForegroundWindow`, a direct `SendInput` mouse click on
the dialog's OK button (registered a hover-highlight on the button but did not dismiss it — some
input reaches the window even unfocused, but not enough to commit a click), minimize/restore, and an
Alt+Tab simulation (this one DID change real OS foreground — proving the restriction isn't absolute —
but landed on an unrelated window, not Psychonauts, and a follow-up `SetForegroundWindow` right after
still failed). **Net: `GetAsyncKeyState`-based hotkeys (like this session's own F12) work regardless
of focus (confirmed twice); anything requiring the window to be genuinely active/focused (dismissing
a UI dialog, mouse-look, `enter_gameplay.ps1`-style DirectInput menu nav) does not, this session.**
Not a mechanism problem — a session-specific tooling/environment limitation, consistent with the
"tool-harness's own console actively contending for foreground" issue notes/00-status.md session 54
already flagged as intermittent, just harder than usual this time.

## Part 1: mouse-camera-yaw hunt — redirected mid-session, not completed

Original framing (before the user's correction) was to keep tracing `CandB`/Site A's entity-tick
call chain. Traced `sub_5123d0` (0x5123d0, size 4136 bytes — the function `CandB` calls conditionally
via `if (idx->field_5e) { sub_5123d0(); sub_513400(); }`, containing Site A's `BuildViewMatrix` call
at `exe+0x512C9B`) via angr decompile: **no mouse-input references anywhere in it.** Its `pEye`/`pAt`
values get built through a chain of generic vector-math helpers (`sub_401DC0`, `sub_401E80` x2,
`sub_401E20`) rather than a direct read off a persistent object field, and the code immediately
after the `BuildViewMatrix` call computes an eye-to-at distance (`sqrt`, matches the mod's own
existing `g_focusDistance` concept) rather than storing the output matrix anywhere persistent — so
this call's output looks like a local, throwaway computation (consistent with `sub_5123d0` being a
conditional camera-follow/focus-distance helper, not the place mouse-look would live). This is a
real, if partial, answer to notes/59's own two questions (pEye/pAt source = math-chain, not a direct
field; output = discarded locally, not stored) — but given the user's mouse-decoupling insight, this
whole call site is now believed to be the WRONG place to keep digging.

Pivoted to searching for the actual mouse-input path: the exe imports only `DirectInput8Create`
(everything else is COM vtable calls, expected). Time ran out chasing a live capture of the mouse
device's `GetDeviceState`/vtable slot 9 (needs catching device creation, which — like `Direct3DCreate9`
two sessions ago — is a one-shot startup call this session didn't manage to catch cleanly before
pivoting to Part 2). **Not resolved this session**: where the persistent mouse-driven camera yaw/pitch
actually lives.

## Recommendation for next session

1. **Reuse the F12 `SetPendingLevel` mechanism** — it's proven, committed, and removes the biggest
   practical blocker every prior FOV/void test hit (notes/59's title-screen-only limitation).
2. **Solve the foreground-focus problem before relying on mouse interaction** — worth its own short,
   focused investigation (a real interactive Windows session for this specific step, or a different
   automation technique/environment) since it now blocks BOTH the mouse-yaw hunt AND any real
   in-gameplay FOV_SCALE test from notes/59.
3. **Resume the mouse-yaw hunt from the input side, not the CandB side**: catch `DirectInput8Create`'s
   return early (same technique that stalled for `Direct3DCreate9` two sessions ago — may need the
   same fresh approach that eventually works there), get the mouse `IDirectInputDevice8*`, breakpoint
   its vtable slot 9 (`GetDeviceState`), and walk the caller chain from there — a completely different,
   untried entry point from every prior session's `BuildViewMatrix`-outward approach.

🤖 Live x64dbg (attach + breakpoints) for Part 1's live confirmation and Part 2's F12 test; angr
decompile for `sub_5123d0`'s structure. Game install itself untouched (only the mod's own
`d3d9.dll`/`openvr_api.dll` replaced, already backed up from notes/59; no saves/levels/assets
modified — `SetPendingLevel` is the engine's own normal level-transition path, not a file edit).
