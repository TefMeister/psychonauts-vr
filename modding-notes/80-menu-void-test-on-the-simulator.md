# 80. The menu void test, on the simulator

2026-10-29, dev PC, `/lm`, OpenXR Simulator, build `7d418a1381e4`, two launches (Tefa: "can you do the menu void
test on the simulator too?"). The test notes/77 asked for: the brain menu at `PSYVR_FOV_SCALE` 1.0 and 3.0.

## What was seen

| | FOV scale 1.0 | FOV scale 3.0 |
| --- | --- | --- |
| projection (log) | rawFov 104, fovy 0.9076, xScale 1.5377 | rawFov 312, fovy 2.7227 (156 deg), xScale 0.1594 |
| brain | fills half the eye | a small dot in the middle |
| background | the teal pattern cards fill the whole view | the same cards, stretched outward, still fill the whole view |
| void (empty black) | none | none |

`BPM cache SET` fired on the menu in both runs (14 lines each), so the menu's projection goes through the hooked
`BuildProjectionMatrix` `[verified-live 2026-10-29, n=1 launch each]`. Pictures and log lines:
`dev-archive/recon/2026-10-29-menu-void-fov-scale/`.

## What it means

- Possibility 1 of notes/77 (the menu builds its projection outside the hook) is ruled out: the widening reaches the
  menu `[verified-live 2026-10-29]`.
- On the OpenXR route there is no menu void at 3.0: the background covers every direction the widened view reaches.
  So possibilities 2 (empty scene) and 3 (culling) are not seen here either.
- The void Tefa saw on 2026-09-10 came through the OpenVR route, which placed the picture with the old notes/43 crop;
  the OpenXR route replaced that crop. So the likeliest remaining reading is that the void came from how the old route
  placed the picture, not from the game `[hypothesis]`. The real headset decides it: wear the OpenXR route and look
  at the brain menu.
- Scale 3.0 squeezes 156 degrees into the headset's ~98: everything is tiny and smeared outward, which matches the
  "very low graphics" report. A scale near 1.0 is what the OpenXR route wants, since it already sends each eye with the
  angles it was drawn with.
