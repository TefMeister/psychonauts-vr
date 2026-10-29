# 79. First run through OpenXR, and what made head-follow "crazy glitchy"

2026-10-29, dev PC, `/lm` (Tefa asked: "can you see if you can make progress with psychonauts? we have not worked
on this game with all the tools we got now"). No headset: the OpenXR Simulator played the headset.

Supersedes: notes/78 §"which fault" (both of its candidates, FEEDBACK and OVERWRITE, were the wrong place to look).

## 1. The OpenXR path runs

Build `47ca750ca30c` with `PSYVR_OPENXR=1`, on the OpenXR Simulator's 32-bit runtime:

- First launch: `xrCreateInstance failed (XrResult -32)`. With `XR_LOADER_DEBUG=all` the loader said why: two
  64-bit implicit API layers registered on this PC (the OBS mirror layer `XR_APILAYER_NOVENDOR_OBSMirror` and
  `XR_APILAYER_ELLIOTTTATE_xrfps`) cannot load into a 32-bit process ("not a valid Win32 application"), and the
  loader then gives up on the whole instance ("Failed loading layer information") `[verified-live 2026-10-29, n=2]`.
- Fix: their own switch-off variables, `DISABLE_XR_APILAYER_NOVENDOR_OBSMirror=1` and `XRFPS_DISABLE=1`, set in
  `Launch-Psychonauts-OpenXR.bat` (this game only). Then: session running, two 1280x960 eye swapchains, frames
  submitted climbing at about 60 a second, readback handed ~= frames, both controllers found every frame,
  `g_vrGeomValid = TRUE from the OpenXR views (HMD vertical FOV 98.2 deg)` `[verified-live 2026-10-29, n=3 launches]`.
- Any 32-bit game going through OpenXR on a PC with 64-bit-only layers will hit the same wall (Hard Reset uses the
  same headset thread). A general fix (skip every implicit layer whose DLL is not 32-bit) is a board row.

## 2. Driving the game with the simulator open

The simulator's preview window lives inside the game's process. It became the process's "main window" and took
keyboard focus back, so `send_key.ps1` was pressing keys into the preview. Fixed: `send_key.ps1` now finds the window
titled exactly "Psychonauts", and the new `dev-archive/tools/input/sim_windows.ps1` puts the game in front and
minimises the preview without activating it. The simulator's screenshot tool still works with the preview minimised.
The quickest way into play is still the automation command `level CAJA` (title screen straight to Sasha's Lab;
needs `PSYVR_AUTOMATION=1`, now in the OpenXR launcher). Leave Ford's conversation with Down x4, Space.

## 3. The head turn reaches the game; the picture follows only with camfollow

- `HeadTrack: T fwd=(0.500,-0.000,-0.866)` while the simulator head was at 30 degrees: the yaw arrives.
- With `camfollow 0` (the default) the picture does not turn: geometry identical at 0 and 30 degrees.

## 4. The glitch: reproduced, explained, fixed

`camfollow 1` with the head held STILL at 0 degrees shredded the picture: near geometry torn into shards, Raz gone,
the floor see-through (`dev-archive/recon/2026-10-29-openxr-sim-camfollow/2-*.jpg`). Same with `camfollowrestore 0`
(3-*.jpg); `camfollow 0` clean again (1-*.jpg) `[verified-live 2026-10-29, n=2 on, n=2 off]`. The stats lines said
0 rewrites, 0 drift: neither FEEDBACK nor OVERWRITE. This is what Tefa saw on 2026-09-10 after typing IN.

Cause, from a live dump of the camera's matrix at `+0x150` in Sasha's Lab: it is a view-PROJECTION matrix, and its
depth column carries the projection's own constant: `c2 = A*c3`, `row3[2] = A*row3[3] + B`, A = 1.0002,
B = -10.002 (zn 10, zf 50000) `[measured 2026-10-29]`. `PsySolveOrigin` solved the camera origin from c0, c1 and
c2, so the constant pushed the origin about 10 units along the view; `PsyApplyBasisYaw` then rewrote row 3 of every
column from that origin, which took 10 off `w` for every vertex even at zero yaw. Anything close to the camera got a
wrong or negative `w`, hence the shards. The 2026-08-28 note in the dossier already saw the "few units" mismatch.

Fix (`px_07_engine_camera.c.inc`): solve the origin from c0, c1, c3 (no constant), and write each column's row 3 as
the snapshot's own row 3 plus the change, so every constant survives and zero yaw writes back exactly what the engine
had. `tests/camfollow_test.c` now also runs the real dumped block: the old code moves `w` at the eye by -10.16 at zero
yaw; the new code leaves it at 0.0000 and keeps the camera in place at +-20 degrees `[verified-numerically 2026-10-29]`.
The old model engines had no projection constant, which is why they passed.

New build `7d418a1381e4` (built with llvm-mingw 20261006; size differs from 47ca750ca30c for that reason too),
installed and stamped on the dev PC. On the simulator, `camfollow 1`: clean picture at 0 and at 30 degrees, in a
conversation and in free play; Raz centred at 0 degrees, slid off to the side at 30 (the picture turns with the head);
stats show 0 rewrites and 0 drift over steady windows (one window with a single 121-degree jump is a conversation camera
cut) `[verified-live 2026-10-29, n=1 launch, simulator]`.

## 5. Not yet known

- How it feels in a real headset (stretch, shake, the void behind). That is the re-wear row.
- Whether the "stretching" Tefa reported on 2026-09-10 was the same fault (likely part of it: a wrong `w` also scales
  the picture), `[hypothesis]`.
- Pitch is still handled by the register-6 path, not the camera; only yaw goes through camfollow.
