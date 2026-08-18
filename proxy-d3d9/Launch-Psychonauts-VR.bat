@echo off
rem ============================================================
rem  Psychonauts VR launcher (v0.1.6-alpha)
rem  Put this next to Psychonauts.exe. Start SteamVR first, then
rem  double-click this. Edit the knobs below and relaunch to tune.
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  Quest 3 owner? Use Launch-Psychonauts-VR-Quest3.bat instead ???
rem  it ships with knobs pre-tuned from real Quest 3 geometry.
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- tuning knobs (remove "rem" to activate) ---

rem Eye render resolution multiplier (default 2 when VR is on; 3 = sharper, costs perf)
rem set PSYVR_RENDER_SCALE=3

rem Field-of-view multiplier (default 1.0). If the world feels zoomed-in/magnified,
rem raise this. Rule of thumb: your headset's vertical FOV divided by the game's 52.
rem (Known issue: the "suggested PSYVR_FOV_SCALE" log line is missing from the
rem  0.1.4 DLL ??? see dev-archive notes/40 for how to compute it from the log.)
rem set PSYVR_FOV_SCALE=1.5

rem HUD/menu virtual depth in world units, 100 per meter (default 200 = 2m; 0 = off)
rem set PSYVR_UI_DEPTH=200

rem Disable head tracking (fixed view, like the very first headset test)
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
