@echo off
rem ============================================================
rem  Psychonauts - VOID TEST launcher (notes/67)
rem  Put this next to Psychonauts.exe and double-click it.
rem
rem  Reproduces the black-void-behind-player bug ON A MONITOR, by
rem  driving the mod's own head-tracking path with a SYNTHESIZED
rem  sway instead of a real headset. The view swings left-right
rem  well past what the game actually renders, which is exactly
rem  the condition the void appears under.
rem
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem ============================================================

rem --- the synthetic head sway ---
rem notes/63: PSYVR_FAKE_POSE silently requires PSYVR_FIRST_PERSON=1
rem as well. A previous session set only FAKE_POSE, saw nothing move,
rem and lost time to it - both must be set.
set PSYVR_FAKE_POSE=1
set PSYVR_FIRST_PERSON=1

rem Sway amplitude in degrees (0..175, default 25.2). 170 sweeps
rem almost fully behind the player, which is where the void lives.
set PSYVR_FAKE_POSE_YAW_DEG=170

rem --- widen the rendered frustum so the void's EDGE is visible ---
rem Leave at 1.0 first: that is the worst case and shows the void at
rem its largest. Raise later to measure how much it shrinks.
set PSYVR_FOV_SCALE=1.0

rem --- automation, so the session can capture and measure ---
set PSYVR_AUTOMATION=1
set PSYVR_SUPPRESS_AUTOPAUSE=1

start "" "%~dp0Psychonauts.exe"
