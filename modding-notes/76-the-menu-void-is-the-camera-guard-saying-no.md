# 76 — the main-menu void is the camera guard saying "no camera here"

**2026-10-01, dev PC, `/pd`. The game was not launched. Nothing here has been run.**

The board row: *"THE MAIN MENU STILL SHOWS THE VOID (big brain screen) — the void fixes only apply in gameplay"*
(Tefa, `[reported 2026-09-10, n=1]`), with the cheap question "is `PSYVR_FOV_SCALE` applied per scene camera, and
does the menu use a different one?"

## Answer `[inferred-static 2026-10-01]`, from our own source and notes 07/08

1. **The FOV widening does reach the menu.** It is applied once, at the entry of the game's `BuildProjectionMatrix`
   (`0x6924D0`, `Hook_BuildProjectionMatrix` scales `rawFov` in place), and notes/08 recorded that the brain title
   screen is a real 3D scene whose `BuildViewMatrix`/`BuildProjectionMatrix` calls hit repeatedly
   `[verified-live 2026-08, n=1 session]`. So this part is not menu-specific.
2. **The camera-turning part of the void fix switches itself off on the menu, by design.** The notes/67
   "candidate 1" fix yaws the GAME camera so the engine renders and culls toward where you look. It finds that
   camera through `AutoGetCamera()` (`px_07_engine_camera.c.inc`), which calls the engine's own guard
   `0x504220` first and **returns NULL "whenever the engine says there is no usable camera right now (loading,
   menus)"** (our own comment). On the brain screen the guard says no, so the fix does nothing there.
3. Together: on the menu only the widening acts, and notes/67 already recorded that a symmetric widening cannot
   cover a turned head. That is exactly Tefa's report: void on the menu, gone in gameplay.

## What would fix it (next, `[hypothesis]`)

On the brain screen `BuildViewMatrix` (`0x692480`) still fires every frame, and our `Hook_BuildViewMatrix` already
sees its eye/at/up. A fallback that, **only when `AutoGetCamera()` returns NULL**, turns that look-at by the head
yaw/pitch before the game builds the view would make the menu scene turn with the head the same way. The brain scene
is small, so culling there is unlikely to matter.

## Diagnostic that would show this reading is wrong

A log line in `CandB`/the void-fix path printing whether `AutoGetCamera()` is NULL on the brain screen. If it is
NOT NULL there, the menu void has another cause and this note is wrong.
