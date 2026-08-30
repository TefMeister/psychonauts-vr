# Session 38 — The notes/15-18 Input Blocker: SOLVED (plain SendInput)

Date: 2026-08-18 (fifth session today). Also this session, before the input work: notes/37
written and synced (housekeeping from the cut-short session), **v0.1.4-alpha released** with
the PSYVR_FOV_SCALE knob, and a documented `Launch-Psychonauts-VR.bat` template added as a
release asset + vendored under `tools/proxy-d3d9/`.

## 0. Summary

**Synthetic keyboard input into the game works** — the long-standing blocker that consumed
notes/15 through 18 (debugger-forged DirectInput buffer contents, forged GetDeviceData event
records; delivered correctly to `SetKeyState` yet never produced a visible effect) falls to the
simplest possible approach, which those sessions never tried:

1. Make the game window genuinely foreground (`SetForegroundWindow`, verified with
   `GetForegroundWindow` before sending — abort if it fails so keys can't land elsewhere).
2. `SendInput` with `KEYEVENTF_SCANCODE` events (DIK-style scan codes; arrows need
   `KEYEVENTF_EXTENDEDKEY` too).
3. Nobody touches the physical keyboard/mouse during the send (the standing focus-warning
   protocol covers this — the user acked hands-off for the test window).

**Verified live, twice:**
- SPACE (scan 0x39) at the "Press ▭ to begin" brain screen → advanced to the main menu
  (eye-dump before/after: Raz appears standing on the brain, prompt text gone; mean pixel diff
  38.7 vs <15 same-screen animation noise).
- RIGHT arrow (scan 0xCD + extended) at the main menu → Raz visibly walked across the brain
  (menu navigation responds).

**Why the old attempts failed while this works**: they injected synthetic STATE into
DirectInput's data structures mid-frame under a debugger — but the engine's edge-detection and
its input focus (the null-callback finding in notes/17) evidently key off the real input stack
and real window focus. Real OS-level events through the real stack, with the game genuinely
focused, exercise the entire path the way a physical key does. (The notes/17 "no listener
armed" observation was likely an artifact of the debugger-session conditions, not the shipping
input path.)

## 1. What this unlocks

- **Autonomous gameplay testing.** Future sessions can drive the game past the title screen and
  through menus without asking the user to press keys — the last human-only step in the test
  loop. Deliberately NOT yet exercised: selecting menu items (New Game could create/overwrite
  save data; navigating into a saved game needs the user's blessing on which slot is safe —
  ask before building the "auto-load a save" sequence).
- Reusable helper: `tools/input/send_key.ps1` (self-contained Add-Type + foreground guard +
  scan-code send; common DIK codes documented in its header).

## 2. Protocol notes for future sessions

- ALWAYS verify foreground==game after SetForegroundWindow and abort otherwise — a stray key
  into the user's other windows is the failure mode that matters.
- The user must be warned hands-off (physical input mid-send steals focus).
- PowerShell tool calls don't share state: the Add-Type class must be defined in the SAME
  invocation that uses it (a first attempt silently no-op'd on this — the "diff" measured was
  just idle animation; re-run self-contained gave the real result).

## 3. Repo state

- `tools/input/send_key.ps1` new; this note; sync to dev-archive + modding-notes.
- Releases today: v0.1.1 → v0.1.4-alpha. Tonight's headset test should use **v0.1.4**.
