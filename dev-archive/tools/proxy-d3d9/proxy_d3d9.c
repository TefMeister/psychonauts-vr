/* proxy_d3d9.c -- the Psychonauts VR d3d9.dll proxy.
 *
 * 2026-09-17: SPLIT, MOVE ONLY. The source is the files included below, pasted back together in
 * this exact order by the preprocessor, so it is still ONE translation unit and build.ps1 is
 * unchanged. Proof: a build from this file equals the pre-split build (tag pre-split-2026-09-17)
 * byte for byte except the 4 link-timestamp bytes. Do not reorder the includes: later parts use
 * statics, typedefs and asm labels declared in earlier ones.
 */
#include "px_01_base.c.inc"   /* lines 1-445: includes, logging, loading the real d3d9.dll, shared device globals (with the milestone history comment above) */
#include "px_02_globals.c.inc"   /* lines 446-1010: OpenVR declarations, env-flag and hotkey globals, the VR eye pipeline struct */
#include "px_03_config.c.inc"   /* lines 1011-1306: the PSYVR_* environment-variable reader (VRBridge_ReadEnableFlag) */
#include "px_04_vrbridge.c.inc"   /* lines 1307-1899: the OpenVR/D3D11 bridge: eye buffers, real IPD/projection, init, submit, poses, teardown */
#include "px_05_headtrack.c.inc"   /* lines 1900-2573: engine addresses, camera cache, submit crop, matrix maths, head tracking, render-level first person */
#include "px_06_camera_hooks.c.inc"   /* lines 2574-2954: trampolines, BuildViewMatrix/BuildProjectionMatrix entries, per-eye targets, CandB before eye 1/2 */
#include "px_07_engine_camera.c.inc"   /* lines 2955-3690: engine camera/player/bone access, look/basis/fpcam/follow writes (automation-driven) */
#include "px_08_dinput.c.inc"   /* lines 3691-3990: DirectInput hooks: mouse injection, pad override, input probe */
#include "px_09_automation.c.inc"   /* lines 3991-4639: the automation command-file interpreter and tick */
#include "px_10_inline_hooks.c.inc"   /* lines 4640-5031: AfterBoth, debug hotkeys, naked asm hooks, inline-hook install, eye surfaces */
#include "px_11_stereo.c.inc"   /* lines 5032-5471: the stereo derivation, SetVertexShaderConstantF hook, register-6 stereo patch, UI depth */
#include "px_12_present.c.inc"   /* lines 5472-5869: eye BMP dump, Present and Reset hooks, viewport scaling */
#include "px_13_device_hooks.c.inc"   /* lines 5870-6375: render-target/depth/StretchRect redirects, UI shader detection, Draw hooks, vtable install */
#include "px_14_export.c.inc"   /* lines 6376-6550: CreateDevice hook, the exported Direct3DCreate9, DllMain */
