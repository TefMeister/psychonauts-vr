@echo off
rem ============================================================
rem  Psychonauts VR launcher - Meta Quest 3 tuned (v0.1.5-alpha)
rem  Put this next to Psychonauts.exe. Start SteamVR first
rem  (Quest via Link / Air Link / Virtual Desktop), then
rem  double-click this. Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  Values below were computed from a real Quest 3's OpenVR
rem  projection geometry (projRaw l=-1.1918 r=0.8391 t=-1.0355
rem  b=0.6745 => per-eye FOV ~90x80 deg vs the game's ~66x52):
rem    FOV_SCALE 1.5  ~= 80 deg / 52 deg vertical match
rem    RENDER_SCALE 3 keeps the wider view sharp
rem  See dev-archive notes/40 for the full derivation.
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- Quest 3 tuning ---
set PSYVR_FOV_SCALE=1.5
set PSYVR_RENDER_SCALE=3

rem --- optional knobs (remove "rem" to activate) ---

rem HUD/menu virtual depth in world units, 100 per meter (default 200 = 2m; 0 = off)
rem set PSYVR_UI_DEPTH=200

rem Disable head tracking (fixed view)
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
