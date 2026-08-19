@echo off
rem ============================================================
rem  Psychonauts VR - EXPERIMENTAL FIRST-PERSON test launcher
rem  (private WIP - not a public release)
rem
rem  Put this + d3d9.dll + openvr_api.dll next to Psychonauts.exe.
rem  Start SteamVR first, then double-click this.
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  First person slides the VR eye onto Raz. Confirmed reaching
rem  inside his head; still WIP - the goggles mesh shows and the
rem  chase camera adds some sway. Tune live with the F-keys below.
rem ============================================================

rem --- required for the VR path ---
set PSYVR_ENABLE_SUBMIT=1

rem --- first person ---
set PSYVR_FIRST_PERSON=1
set PSYVR_FP_FORWARD=2.2
set PSYVR_FP_HEIGHT=-20
set PSYVR_FP_SMOOTH=0.12

rem --- base VR tuning (Quest 3) ---
set PSYVR_FOV_SCALE=1.8
set PSYVR_RENDER_SCALE=3

rem --- live tuning keys while the game runs ---
rem   F7 / F8  : move viewpoint back / forward  (onto Raz)
rem   F9 / F10 : eye down / up
rem   F5 / F6  : camera smoothing  smoother / snappier
rem   F11      : recenter head tracking
rem   Values are printed to the log as "FP tune:" lines.

start "" "%~dp0Psychonauts.exe"
