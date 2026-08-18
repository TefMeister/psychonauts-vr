@echo off
rem ============================================================
rem  Psychonauts VR launcher (v0.1.4-alpha)
rem  Put this next to Psychonauts.exe. Start SteamVR first, then
rem  double-click this. Edit the knobs below and relaunch to tune.
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- tuning knobs (remove "rem" to activate) ---

rem Eye render resolution multiplier (default 2 when VR is on; 3 = sharper, costs perf)
rem set PSYVR_RENDER_SCALE=3

rem Field-of-view multiplier (default 1.0). If the world feels zoomed-in/magnified,
rem check the log for "suggested PSYVR_FOV_SCALE" and set that value here.
rem set PSYVR_FOV_SCALE=1.5

rem HUD/menu virtual depth in world units, 100 per meter (default 200 = 2m; 0 = off)
rem set PSYVR_UI_DEPTH=200

rem Disable head tracking (fixed view, like the very first headset test)
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
