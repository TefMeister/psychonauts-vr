@echo off
rem ============================================================
rem  Psychonauts VR launcher - OpenXR (2026-10-17)
rem  The same mod as Launch-Psychonauts-VR.bat, but the headset is
rem  reached through OpenXR instead of SteamVR's OpenVR, so it works
rem  with the OpenXR Simulator and any OpenXR headset runtime.
rem  Put this, openxr_loader.dll (32-bit) and psyvr_xr.ini next to
rem  Psychonauts.exe. Log: %TEMP%\psychonautsvr_proxy.log
rem ============================================================

rem --- the VR path, through OpenXR ---
set PSYVR_OPENXR=1

rem Hand Freeze registers a 64-bit OpenXR layer that stops 32-bit games
rem from starting OpenXR at all; this skips it for this game only.
set DISABLE_HAND_FREEZE_LAYER=1

rem --- tuning knobs (remove "rem" to activate), as in Launch-Psychonauts-VR.bat ---
rem set PSYVR_RENDER_SCALE=3
rem set PSYVR_FOV_SCALE=1.8
rem Send each eye with the headset's own angles instead of the angles it was drawn with (debug only)
rem set PSYVR_SUBMIT_BOUNDS=0
rem set PSYVR_UI_DEPTH=200
rem set PSYVR_DISABLE_TRACKING=1

rem In-game: press F11 to re-center head tracking.

start "" "%~dp0Psychonauts.exe"
