@echo off
rem ============================================================
rem  Psychonauts - HEAD-FOLLOW CAMERA TEST (notes/68)
rem  Put next to Psychonauts.exe and double-click.
rem
rem  Monitor-only test of the void fix: a SYNTHETIC head sway
rem  drives the game's own camera transform (camera+0x150), so
rem  the engine culls and renders where the "head" is looking.
rem  No headset, no SteamVR - this is the desk check that has to
rem  pass before anything is worn or released.
rem
rem  Once in gameplay, arm it with:   camfollow 1
rem  Sign wrong (view turns the wrong way)?  camfollowscale -1
rem
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem ============================================================

rem --- external command harness ---
set PSYVR_AUTOMATION=1

rem --- keep the engine ticking while unfocused, so the poll runs ---
set PSYVR_SUPPRESS_AUTOPAUSE=1

rem --- synthesized head sway, standing in for a real HMD ---
rem A wide sweep on purpose: the whole question is whether the
rem scene stays rendered well past the 87.4-degree free-look clamp
rem that caps the gamepad route.
set PSYVR_FAKE_POSE=1
set PSYVR_FAKE_POSE_YAW_DEG=60

start "" "%~dp0Psychonauts.exe"
