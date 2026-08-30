# 62 — Visibility Tree Culling flag toggle built and confirmed working (NUMPAD9, zero UI); live A/B test blocked by scene navigation, not by the toggle itself

**Date:** 2026-08-24, dev machine. Direct follow-up to notes/61: decompile `sub_629490` to find whether
"Visibility Tree Culling" (item id 117) resolves to a directly-flippable flag, then build a zero-UI
hotkey and run the actual disappears-vs-persists void test in real gameplay.

## Part 1: `sub_629490` decompiled — confirmed a direct, single-byte flag

```c
int sub_629490(struct_1 *idx)
{
    if (idx->field_8 != 2) return v28;                 // not a boolean-type item, bail
    else if (idx->field_4 != 0xffffffff) {              // generic-ID item (Visibility Tree Culling: id=117)
        v27 = idx->field_4;                              // v27 = 117
        v28 = _INSERT(v27, 0, *((char *)(g_78bc20 + v27 + 44)));
        idx->field_18 = *((char *)(g_78bc20 + v27 + 44)); // syncs the menu checkbox's displayed state
        return v28;
    }
    else { /* string-compare special-cases for Fly Camera / Sphere Camera / Drop Player only */ }
}
```

**This is exactly the simple case hoped for.** For any item registered through `sub_629410` (the
generic-ID path — includes Visibility Tree Culling, Allow Debug Display, Show Trigger Vol, NPC
Debug, and most of the render-toggle page), the flag lives at a single predictable address:

```
flag_address = *(void**)0x78BC20 + 44 + item_id
```

For Visibility Tree Culling (`item_id = 117`): `*(void**)0x78BC20 + 161` (0xA1), one byte. This
function only *reads* that byte (to sync the menu's own checkbox display) — the real click/write
handler was never traced, and turned out to be unnecessary: writing the byte directly from our own
code has exactly the same effect, with zero menu/UI interaction needed.

## Part 2: built and confirmed working — NUMPAD9 direct toggle

Added an opt-in hotkey (`PSYVR_CULL_TOGGLE_KEY=1`, NUMPAD9, in `CandB_AfterBoth_asm` — same
unconditional-every-frame location as notes/60's F12) that reads `*(void**)0x78BC20`, computes
`engine_ptr + 44 + 117`, and flips that byte. Built clean (same two pre-existing warnings), deployed.
**Live-confirmed working via the log, twice**:

```
CullToggle: NUMPAD9 pressed - Visibility Tree Culling flag @ 02E59201 (engine+161) now 0
```

`02E59160 (engine ptr, from the same session's F12 LevelJump log line) + 0xA1 = 02E59201` — exact
match, confirms the address arithmetic is correct against the live engine object, not just in theory.

## Part 3: the live A/B test — infrastructure proven, empirical answer not reached this session

**Real lesson learned and worth flagging for every future session**: the Journal/pause-menu's
**"Continue"** option does NOT resume the current session — it loads the most recent save file. Our
first attempt this session accidentally navigated into it while trying to dismiss an unrelated
focus-loss auto-pause dialog, silently discarding the F12 level-jump and loading a mid-cutscene
autosave instead (a locked-camera "save 4" scene, per the user's live observation while watching).
**Fix used successfully on the retry: after F12, avoid all further menu navigation entirely** — send
input via raw `SendInput` without any `SetForegroundWindow` calls (which seem to be what triggers the
focus-loss pause in the first place; the clean retry launch never lost focus and never saw the pause
dialog until later, self-inflicted, navigation).

The retry reached real, controllable gameplay in Sasha's Lab (`CAJA`) via F12, confirmed by a normal
free-roam screenshot with no dialogue/pause UI. From there, two separate obstacles prevented reaching
a clean comparison:

1. **A persistent NPC greeting trigger** at (or very near) the `CAJA` spawn point that re-fires
   within seconds of being dismissed, even after attempting to walk away — camera-relative movement
   input combined with our render-level head-yaw override may itself be part of why "walk away"
   attempts didn't reliably increase distance from the trigger (an eyebrow-raising, on-topic
   complication: this may be a live, small-scale demonstration of exactly the camera/input coupling
   candidate 1 is about, though not confirmed causally this session).
2. **An unplanned scene transition** (most likely triggered by one of the movement inputs sent while
   trying to escape the NPC trigger) landed in a visually distinct area — a surreal suburban
   neighborhood (matching "The Milkman Conspiracy" mental-world aesthetic) at world coordinates
   `(52500, -33016, -4388)`-ish, `zf=80000` (vs. `CAJA`'s `zf=50000`) — genuinely a different level or
   sub-area, not just a different room. **In this area, the logged camera `right` vector stayed
   bit-for-bit frozen** (`(-0.6072,-0.1420,0.7818)`) across 8+ seconds of real time despite
   `PSYVR_FAKE_POSE`/`PSYVR_FAKE_POSE_YAW_DEG` being active the whole session — our head-tracking
   render override was not visibly rotating this particular scene's camera, for a reason not
   diagnosed this session (a locked/scripted intro camera is the leading guess, consistent with
   Psychonauts' mental-world levels often opening with a short non-interactive establishing shot).

Toggled the flag OFF partway through this sequence (confirmed in the log, per Part 2), but with the
camera not rotating in the scene reached, there was no meaningful "look behind" to compare before vs.
after. **No clean disappears-vs-persists screenshot pair was obtained this session** — this is a
scene-navigation/camera-activity problem, not a problem with the toggle itself, which is confirmed
correctly wired end to end.

No save file changes were needed or made this session — the successful run avoided the
`Continue`-loads-a-save trap entirely, so nothing needed deleting (the user had pre-authorized
deleting the "save 4" cutscene save if it became a blocker; it didn't come to that).

## Recommendation for next session

The toggle itself is done and should not need revisiting. To actually get the disappears-vs-persists
answer:

1. **Pick a spawn point away from any NPC trigger.** `CAJA` (Sasha's Lab) puts Raz immediately next
   to a conversation trigger that's proven hard to escape reliably via scripted input. Try a
   different confirmed-real level code (session notes/55's table has several `CA*` Campgrounds
   sub-areas, unconfirmed but worth a quick try) or find a way to skip/suppress dialogue triggers
   entirely (perhaps another debug-menu-style flag, or the `field_5e`/`field_65`-gated NPC-debug
   items found in notes/61's decompile of `sub_627590`).
2. **Confirm head-tracking is actually rotating the camera before trusting a "no void" reading** —
   check the `right`/`eye` vector in the log is genuinely changing frame to frame (as it reliably did
   in earlier `CAJA` captures) before concluding anything about culling. A frozen camera and a
   correctly-disabled cull flag look identical in a screenshot (no void either way) for two very
   different reasons.
3. Once both are true simultaneously, the actual test is now one keypress: toggle NUMPAD9, compare.

🤖 `x64dbg-skills:decompile` (angr) for `sub_629490`; live x64dbg-free build/deploy/launch/log
verification for the hotkey. One deployed DLL rebuild this session (superset of notes/59-61's build,
same pre-existing backup from notes/59 still covers rollback — no new backup needed since nothing
this build removes). Game install itself untouched; no save files modified or deleted.
