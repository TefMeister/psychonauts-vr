@echo off
rem ============================================================
rem  Psychonauts VR launcher - Meta Quest 3 tuned (v0.1.7-alpha)
rem  Put this next to Psychonauts.exe. Start SteamVR first
rem  (Quest via Link / Air Link / Virtual Desktop), then
rem  double-click this. Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  v0.1.7: the zoom is now fixed properly by tangent-matched
rem  submit bounds (the compositor's angular mapping is exactly
rem  1:1 whatever FOV scale you render at). FOV scale now only
rem  sets how much EXTRA frame is rendered around the visible
rem  window: more = less over-the-shoulder culling void and
rem  exact lens coverage, at the cost of pixel density.
rem  1.8 = full Quest 3 lens coverage (recommended).
rem  1.5 = sharper center, slight vertical stretch at the top
rem        edge (frame doesn't quite reach the lens top).
rem  The old "HUD invisible above 1.2" bug is gone - the HUD
rem  sits at the lens periphery at any scale now.
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- Quest 3 tuning ---
set PSYVR_FOV_SCALE=1.8
rem set PSYVR_FOV_SCALE=1.5
set PSYVR_RENDER_SCALE=3

rem --- optional knobs (remove "rem" to activate) ---

rem Revert to the old full-texture submit (brings the zoom back; debug only)
rem set PSYVR_SUBMIT_BOUNDS=0

rem HUD/menu virtual depth in world units, 100 per meter (default 200 = 2m; 0 = off)
rem set PSYVR_UI_DEPTH=200

rem Disable head tracking (fixed view)
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
