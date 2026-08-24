# 65 — Collision Wireframe void test (negative result) + a real fix for the auto-pause blocker

**Date:** 2026-08-24, dev PC. Continues notes/59-64 (the black-void-behind-player investigation) and
notes/64's own recommendation to wire `COLLISION_WIREFRAME_ITEM_ID 22` (the theoretically-correct
tool for distinguishing "real gap" from "dark terrain," vs. notes/64's own render-wireframe, which
was confirmed unable to answer that question at all).

## Part 1: the auto-pause blocker — root-caused and fixed (mostly)

Two sessions (notes/60, notes/64) failed to dismiss the "while you were away, your game was
automatically paused" dialog via `SetForegroundWindow`, `AttachThreadInput`, a coordinate-verified
mouse click, minimize/restore, and `SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT)` (ACCESS_DENIED).

**First fix attempt, partial**: an opt-in WndProc subclass (`PSYVR_SUPPRESS_AUTOPAUSE=1`) on the
game's own window, swallowing `WM_ACTIVATEAPP`/`WM_ACTIVATE`/`WM_NCACTIVATE`/`WM_KILLFOCUS` before
they reach the game's original WndProc. This worked cleanly on its FIRST test (35+ seconds of stable
rotating-camera gameplay, zero pause) but failed on the very next launch — the pause dialog still
appeared, and the log showed only ONE swallowed message the entire run (a harmless activate-TRUE at
init). That's the key diagnostic: if the pause were purely reactive to window messages, suppressing
them should have worked every time. It didn't, which means the game is (very likely) **polling
`GetForegroundWindow()` directly once per tick** and comparing it to its own hwnd — a common
"auto-pause when not really foreground" pattern — rather than reacting to a message at all.

**Second fix, this is the one that actually works**: an IAT patch. `PatchIATEntry()` (new general-
purpose helper, walks the exe's own PE import directory rather than byte-patching a function body,
so it doesn't depend on knowing the target function's prologue/codegen) redirects the game exe's
import-table entry for `user32.dll!GetForegroundWindow` to `Hook_GetForegroundWindow()`, which
unconditionally returns the game's own captured hwnd (`g_gameHwnd`, captured in `Hook_CreateDevice`
from `hFocusWindow`) whenever `PSYVR_SUPPRESS_AUTOPAUSE=1`. **Confirmed working across two full
relaunch-and-test cycles this session, 20-30+ seconds of continuous rotating-camera gameplay each
time, zero pause dialogs, zero additional swallowed messages beyond the one harmless init-time one.**
Both fixes are kept (opt-in, same flag) since the WndProc swallow is cheap insurance for whatever
DOES arrive as a real message.

This should unblock every future live-gameplay test this project runs, not just this one — it was
explicitly flagged in notes/64 as blocking "two of the last three sessions' actual empirical goals."

## Part 2: the actual test — Collision Wireframe (item 22) vs. the void

Wired `COLLISION_WIREFRAME_ITEM_ID 22` as NUMPAD7, identical direct-byte-write pattern to notes/62's
NUMPAD9 (Visibility Tree Culling) and notes/64's NUMPAD8 (Render Wireframe) — confirmed live at
`engine+66` (=44+22, the same flat-array formula holding for a third item in a row).

**Method**: `SetPendingLevel` → Campgrounds beach (`CABH`), `PSYVR_FAKE_POSE=1` +
`PSYVR_FAKE_POSE_YAW_DEG=95` + `PSYVR_FIRST_PERSON=1` (the render-gate combo from notes/64) for a
wide synthetic sway, `PSYVR_DUMP_EYES=1` for periodic BMP capture. Polled the eye-dump file every
~2s across ~20-24s windows (the dump only refreshes every ~5s internally, so many polls just re-copy
a stale frame — harmless, just wasted samples) and matched phases between the wireframe-off and
wireframe-on runs by eye (the horizon-line/cloud-swirl shape is distinctive and repeats each ~12.6s
sway cycle, per notes/63).

**Result: matched-phase comparison shows collision wireframe adds nothing — the void stays
completely, uniformly black.** Screenshots in `notes/assets/65-collision-wireframe-void-test/`:
- `01_baseline_void_wireframe_off.png` / `02_matched_phase_collision_wireframe_ON.png` — same
  horizon-line "valley" cloud pattern, same proportions, both solid black below the horizon. No
  wireframe lines, no geometry outlines, no visible difference of any kind in the void region.

**Important caveat, and it's a real one — not a formality**: `00_sanity_beach_scene_wireframe_off.png`
vs. `03_sanity_beach_scene_wireframe_ON.png` (both at a similar near-fence camera angle, definitely-
present geometry — sand, fence, plants, rocks) **also show no visible wireframe overlay of any kind**.
I could not positively confirm that toggling Collision Wireframe has ANY visible rendering effect
anywhere in this build, on geometry that's unambiguously present. This matches TCRF's own documented
caveat (also flagged in the original public-research lead, notes/59) that many of this dormant debug
menu's options are "partially functional or completely broken" on the PC port. **This means the void
result is a clean negative, but not a fully conclusive one**: it's consistent with "there's genuinely
no collision geometry there either" (supporting a real, not-just-rendered gap), but it's equally
consistent with "this debug visualization simply doesn't draw anything in this build regardless of
where it's pointed." I don't have evidence to distinguish those two explanations this session.

## What this does and doesn't settle

- Doesn't overturn notes/63's negative result on Visibility Tree Culling, or add a positive
  confirmation of a true render/collision gap — the sanity-check caveat above keeps this honestly
  inconclusive on the core "real gap vs. just dark" question notes/59 originally posed.
- Does add one more data point consistent with Jill Crungus's octree hypothesis IF collision
  wireframe is actually working (per notes/64's own flagged possibility: "if collision detection also
  walks the same visibility tree... Collision Wireframe might ALSO fail to show geometry in a culled
  region... suggesting collision and render visibility are the same gate").
- Definitely fixes the auto-pause blocker for future sessions — this is the most durable, reusable
  outcome of the session regardless of how the void question itself resolves.

## Recommendation for next session

1. **Establish a positive control before trusting any more debug-menu visualization results.** Find
   an option confirmed working on PC (TCRF/notes/59 specifically named Fly Camera, Sphere Camera, and
   Show Collision as still-functional) and verify it visibly changes the picture before relying on
   Collision Wireframe's negative result further. Show Collision (a different item ID, not yet
   located/wired) is the most promising next try since it was TCRF's own specific "still works" call-
   out, distinct from Collision *Wireframe*.
2. With the auto-pause fix now solid, the `DrawIndexedPrimitive`-side cull-hunt (paused since notes/58)
   is viable again if the user wants to resume it — testing is no longer the bottleneck it was.
3. The Sphere Camera test from notes/59's ORIGINAL recommendation (never actually tried) is now cheap
   to attempt with a working auto-pause fix in hand.

## Cleanup

Collision Wireframe flag restored to default (0) before each process exit. Game process killed both
times. No save files touched (only F12/NUMPAD7 sent all session — verified via log grep for
Journal/save/continue, zero hits). SteamVR (vrserver/vrcompositor, null driver) left running per
standing practice. Deployed `d3d9.dll`/`openvr_api.dll` include this session's changes; previous
build backed up as `d3d9.dll.pre-notes65-backup`. Scratch capture directories in `%TEMP%` removed
after archiving the 4 evidence screenshots into this repo.

🤖 Live x64dbg-free session (no debugger needed — pure build/deploy/test with the already-proven
direct-byte-write hotkey pattern and a new IAT-hook technique), PowerShell `SendInput`-by-virtual-key
only (no `SetForegroundWindow` at any point), `System.Drawing` for BMP→PNG conversion, direct visual
inspection of captured frames.
