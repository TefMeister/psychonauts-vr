# xr/ - the OpenXR headset output, copied from Hard Reset VR

Copied 2026-10-17 from `staging/hard-reset-vr/proxy-d3d9/src` (commit `9a4dbac`) so Psychonauts can use the
OpenXR Simulator and any OpenXR headset. The files are the same as Hard Reset's except:
`hxr.c` (ini name `psyvr_xr.ini`, app name, BeG0nE game name) and the comments in `hxr.h`.
When Hard Reset's copy gets a fix, bring it here too (diff the two folders).

The Psychonauts side that feeds it is `../px_04b_xrbridge.c.inc`: switched on with `PSYVR_OPENXR=1`.

OpenXR headers in `openxr/`: Khronos, Apache-2.0 OR MIT.
