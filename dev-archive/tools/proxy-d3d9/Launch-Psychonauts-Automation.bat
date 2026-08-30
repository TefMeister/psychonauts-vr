@echo off
rem ============================================================
rem  Psychonauts - AUTOMATION launcher (notes/67)
rem  Put this next to Psychonauts.exe and double-click it.
rem
rem  Starts the game with the external command harness ON, so a
rem  session can drive it afterwards without touching the keyboard:
rem  commands are appended to psyvr_automation_cmds.txt (created
rem  next to the exe) and results go to the proxy log.
rem
rem  Log: %TEMP%\psychonautsvr_proxy.log
rem
rem  NOTE: no VR here on purpose - this is the flat/monitor
rem  automation path. Use the normal launchers for headset runs.
rem ============================================================

rem --- the harness itself ---
set PSYVR_AUTOMATION=1

rem How often the command file is read, and how often a camera
rem telemetry line is written (milliseconds).
rem set PSYVR_AUTOMATION_POLL_MS=200
rem set PSYVR_AUTOMATION_TELEM_MS=1000

rem --- keeps the engine ticking while the window is not focused ---
rem (notes/65: without this the game auto-pauses the moment anything
rem else takes focus, which stalls the command poll.)
set PSYVR_SUPPRESS_AUTOPAUSE=1

start "" "%~dp0Psychonauts.exe"
