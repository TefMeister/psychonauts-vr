# Psychonauts VR — Live Headset Test Runbook

A step-by-step guide for running a VR test at home on the gaming PC, and exactly what to look
for. Written to be reusable for every build; the **build-specific checklist** at the bottom says
what's new to verify in the current release.

- **Test PC (home):** Windows 11, RTX 5080, game at `C:\Steam\steamapps\common\Psychonauts`,
  Meta Quest 3 via Virtual Desktop.
- **Dev PC:** the Win10/GTX 1660 machine where the build is made — low-powered, poor FPS there is
  expected and not a bug.
- The proxy log is always written to `%TEMP%\psychonautsvr_proxy.log` on whichever PC ran the game.

---

## Part A — Pre-flight (do this before putting the headset on)

1. **Get the build.** Download the release ZIP from
   https://github.com/TefMeister/psychonauts-vr/releases (latest tag), or use the pre-tuned
   launcher assets attached to the release.
2. **Deploy into the game folder** (next to `Psychonauts.exe`):
   - `d3d9.dll`  (the mod)
   - `openvr_api.dll`  (Valve's runtime — required for the VR path)
   - a launcher `.bat` (use `Launch-Psychonauts-VR-Quest3.bat` on the Quest 3)
   - Keep a backup of any pre-existing `d3d9.dll` (there normally isn't one).
3. **Start SteamVR first**, with the headset already connected and tracking (Quest 3 via Virtual
   Desktop → SteamVR). Confirm you can see the SteamVR home/void in the headset before launching
   the game. *SteamVR must be up before the game, or the VR bridge falls back to flat rendering.*
4. **Delete the old log** so you're reading a clean run:
   delete `%TEMP%\psychonautsvr_proxy.log` (paste `%TEMP%` into Explorer's address bar).
5. **Launch via the `.bat`** (double-click `Launch-Psychonauts-VR-Quest3.bat`). Do **not** launch
   `Psychonauts.exe` directly — the `.bat` sets the env vars the VR path needs.

If nothing appears in the headset within ~15s but the game is running on the monitor: SteamVR
wasn't running first, or `openvr_api.dll` is missing next to the game. Close the game, fix, retry.

---

## Part B — What to check, in order (each is a distinct pass/fail)

Go through these deliberately. For each, note **pass / partial / fail** and a one-line
description — that's what makes the log-plus-notes useful back on the dev side.

### 1. It displays in the headset at all
- **Look for:** the game rendering in stereo inside the Quest 3, tracking your head.
- **Fail signs:** flat/2D, only one eye, or nothing in-headset (game only on monitor).

### 2. Zoom / scale feels natural (the headline item this cycle)
- **Look for:** the world at a natural, life-sized scale — a doorway looks doorway-sized, Raz
  looks child-sized, turning your head feels 1:1.
- **Fail signs:** everything magnified/telephoto (too zoomed in) or shrunken/dollhouse (too zoomed
  out). Note which.

### 3. HUD / menus are visible and readable
- **Look for:** the HUD (health, etc.) and the pause/menu text visible without hunting for it,
  sitting at a comfortable depth (~2 m).
- **Fail signs:** HUD invisible, pushed to the far edges, doubled/cross-eyed, or floating at a
  wrong depth.

### 4. Head tracking quality
- **Look for:** smooth, solid tracking on all axes (yaw/pitch/roll) plus positional lean when you
  move your head; horizon stays level.
- **Fail signs:** juddery/laggy, drifting, locked on an axis, or a tilted horizon.
- **Recenter:** press **F11** to re-center if the forward direction feels off.

### 5. The over-the-shoulder "void" (known issue — measuring, not fixing)
- **Look for / expect:** turn your head well off to the side or look behind you. There will be an
  area of unrendered **black nothingness** where the world vanishes until the game camera turns
  that way. This is expected (the engine's culling doesn't know about head rotation yet).
- **Report:** at roughly what head angle it starts, and whether it feels better or worse than the
  previous build. (It's a comfort issue, so honest impressions matter — "unsettling" is useful.)

### 6. Distant foliage / LOD billboards (known issue)
- **Look for / expect:** far-off trees and bushes (the flat 2D "billboard" versions) may flicker
  or look cross-eyed/doubled until you get close and they pop to solid 3D.
- **Report:** how noticeable/bothersome, and whether it changed from last build.

### 7. Performance / comfort
- **Look for:** steady motion, no stutter, comfortable to be in.
- **Note:** frame rate if the SteamVR/Virtual Desktop overlay shows it (target is the headset's
  refresh, 72 Hz). Any nausea, and what triggered it.

### 8. Clean exit
- **Look for:** quitting the game closes it fully.
- **Fail signs:** the game hangs on exit or lingers as a process you have to kill in Task Manager.

---

## Part C — After the test (this is the part that feeds the next build)

1. **Grab the log:** copy `%TEMP%\psychonautsvr_proxy.log` somewhere safe and send it over (paste
   into a GitHub issue, or save the file). It contains the real evidence — the summary below is
   secondary.
2. **Write a short pass/partial/fail line for each of the 8 checks above**, plus anything that
   surprised you.
3. **Key log lines to glance at yourself** (optional — the dev side reads the full log anyway):
   - `VRBridge_Init: SUCCESS` and `HMD identity: ... "Meta Quest 3"` — bridge came up on the right
     headset.
   - `Submit(eye=0) OK` / `Submit(eye=1) OK` repeating — frames are reaching the compositor.
   - `submit bounds eye=… u=[…] v=[…]` — the new crop. **`(CLAMPED…)` means the rendered frame
     was smaller than the lens on some axis** — see the FOV note below.
   - `full lens coverage needs PSYVR_FOV_SCALE>=X` — the exact value for zero clamping.
   - `suggested PSYVR_FOV_SCALE=…` — sanity value from your headset's geometry.

---

## Part D — Tuning knobs you can try in the field

Edit the `.bat`, save, relaunch. All are `set NAME=value` lines.

- `PSYVR_FOV_SCALE` — how much frame is rendered around the visible window (since v0.1.7 this does
  **not** change zoom, only edge coverage / culling margin). If the log says
  `full lens coverage needs PSYVR_FOV_SCALE>=1.8`, try 1.8. Higher = less edge stretch and a
  smaller over-the-shoulder void, but softer image; pair with `PSYVR_RENDER_SCALE=3` for sharpness.
- `PSYVR_RENDER_SCALE=1..4` — eye render resolution (sharpness vs performance). 3 is validated on
  the 5080.
- `PSYVR_UI_DEPTH=<world units>` — HUD/menu virtual depth (200 ≈ 2 m; 0 = off).
- `PSYVR_SUBMIT_BOUNDS=0` — **debug only:** turns off the v0.1.7 zoom fix (brings the zoom back).
  Useful only to A/B confirm the fix is doing something.
- `PSYVR_DISABLE_TRACKING=1` — fixed view, no head tracking (comfort fallback / comparison).
- **F11** in-game — re-center head tracking.

---

## Build-specific checklist — v0.1.7-alpha

The big change this build is the **real zoom fix** (tangent-matched submit bounds). Focus the
test on:

- [ ] **Check 2 (zoom):** should now be *exactly* natural scale, not just "better" — this is the
      headline. If it still feels off, note magnified vs shrunken.
- [ ] **Check 3 (HUD):** should be visible at the launcher's default `PSYVR_FOV_SCALE=1.8`
      (previous builds hid the HUD above ~1.2 — that bug should be gone).
- [ ] **Check 5 (void):** compare to the FOV 1.5 session — 1.8 renders a wider frame, so the void
      should start at a larger head angle.
- [ ] **Performance:** confirm 72 Hz holds at `FOV 1.8 + RENDER_SCALE 3` on the 5080.
- [ ] **Log:** `submit bounds eye=…` lines present; ideally **no `(CLAMPED…)`** at 1.8. If you see
      clamping, note the `needs PSYVR_FOV_SCALE>=` value.
- [ ] **Recenter (F11)** and **clean exit** still work.

If zoom is natural and the HUD is visible, that's the two-headline success for v0.1.7. Everything
in Checks 5–6 is known and queued — report impressions, don't expect fixes yet.
