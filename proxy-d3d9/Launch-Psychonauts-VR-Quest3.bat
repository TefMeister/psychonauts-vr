@echo off
rem ============================================================
rem  Psychonauts VR launcher - Meta Quest 3 tuned (v0.1.6-alpha)
rem  Put this next to Psychonauts.exe. Start SteamVR first
rem  (Quest via Link / Air Link / Virtual Desktop), then
rem  double-click this. Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  FOV tuning vs HUD visibility (see dev-archive notes/40+42):
rem  the geometric zoom-fix value for Quest 3 is 1.5, BUT at
rem  FOV scale >~1.2 the game's screen-space HUD gets pushed to
rem  the far periphery and becomes invisible (bug, fix queued).
rem  Default below is the 1.2 compromise: HUD visible, most of
rem  the zoom removed. Swap which line is active if you prefer
rem  the full zoom fix and can live without the HUD.
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- Quest 3 tuning ---
set PSYVR_FOV_SCALE=1.2
rem set PSYVR_FOV_SCALE=1.5
set PSYVR_RENDER_SCALE=3

rem --- optional knobs (remove "rem" to activate) ---

rem HUD/menu virtual depth in world units, 100 per meter (default 200 = 2m; 0 = off)
rem set PSYVR_UI_DEPTH=200

rem Disable head tracking (fixed view)
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
