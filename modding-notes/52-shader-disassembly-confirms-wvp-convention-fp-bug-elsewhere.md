# 52 — Shader disassembly (playbook Phase 3.3) CONFIRMS the WVP convention; FP bug is elsewhere

**Date:** 2026-08-20, dev machine. Following the user sharing `the-evil-within-vr-engine-research`'s
`PLAYBOOK.md`, scaffolded `psychonauts-vr-engine-research` (playbook + a full `ENGINE-DOSSIER.md`
distilled from notes 1-51). Writing the dossier's camera section (§6) forced an honest admission:
the row/column-vector convention the ENTIRE render-level camera model has assumed since notes/24
was **never verified against the compiled vertex shader**, which is exactly the failure mode
playbook §3.3 exists to catch ("do NOT trust content-heuristics alone... disassemble the
world-geometry vertex shader"). Flagged as the dossier's #1 open risk and the leading suspect for
notes/51's mystery (a 212wu computed FP translation producing zero visible eye movement).
**This session closed that risk with real evidence. Result: the convention is CONFIRMED CORRECT —
the FP bug's root cause is NOT here, and must be sought elsewhere.**

## Method (fully automated, per the user's request - "can we automate this")

1. Reused pre-existing infrastructure: `PSYVR_REG_HISTO=1` already dumps every compiled vertex
   shader's raw bytecode to `%TEMP%\psyvr_vs_NN.bin` (built in an earlier session for the
   "audited all 455 shaders" pass, notes/36) - no new dump machinery needed.
2. Small addition to `proxy_d3d9.c` (`PSYVR_SHADER_DUMP=1`): `Hook_CreateVertexShader` now also
   records a `shader pointer -> dump index` map; `Hook_SetVertexShader` tracks the currently-bound
   shader pointer; `Hook_SetVertexShaderConstantF` logs which dump index is bound the first time a
   c96 (32-bone skinned) upload is seen. This answers "which of the 455 .bin files is the skinned
   world shader" without needing to eyeball anything.
3. **New: `tools/input/auto_shader_dump.ps1`** - a fully autonomous capture combining
   pre-existing pieces (silence intro videos → launch off-screen → `enter_gameplay.ps1` → poll the
   proxy log for the `SHADERDUMP:` line → kill the process → restore intro videos, all in
   try/finally). Requested by the user directly ("can we automate this... you did manage to get
   into the continue door at home") instead of asking them to manually drive each capture. Ran with
   the standard focus-steal warning/all-clear. The `enter_gameplay.ps1` door-entry sequence
   partially aborted (its foreground-window check failed after the off-screen move) but the capture
   still succeeded at 6 seconds in - the title/menu "brain" screen's decorative skinned character
   already triggers a c96 upload (the same phantom-Raz behavior notes/51 found), which turned out to
   be fine: skinning shaders are shared by bone-count/vertex-format, not per-character.
4. **Offline disassembly, zero game involvement**: `D3DX9_40.dll` (confirmed present in
   `System32`, genuinely x64) called directly via Python `ctypes` (`D3DXDisassembleShader`) against
   the captured `.bin` files - no live process, no debugger, matches playbook §Tools' "shader
   reflection/disassembly" prescription exactly.
5. Cross-checked: scanned all 455 dumped shaders for the same instruction pattern - **239 of 455**
   share it, so this isn't a one-shader fluke.

## What the disassembly shows (psyvr_vs_208.bin, representative of the 239-shader family)

```
dcl_texcoord6 v14      ; bone index pair (packed)
dcl_texcoord7 v15      ; bone weight
mad r7, v14, c23.x, c23.y      ; r7 = bone index * stride + base  (base lands in the c96.. palette)
mova a0.x, r7.x
m4x3 r0.xyz, v0, c0[a0.x]      ; bone-matrix skin, blended by weight (r8/r11) — model-space result
...
m4x4 r4, r11, c6               ; <-- THE constant we hook: WVP, 4 consecutive float4 registers
add oPos, r4, c50              ; c50 = standard D3D9 half-pixel offset (not UI-specific after all)
```

**`m4x4 dst, src, cN` in D3D9 SM2 is literally 4 `dp4`s: `dst.i = dot4(src, c[N+i])` for i=0..3.**
With position `src.w = 1` (affine), the constant term added to `dst.i` — i.e. the pure translation
contribution, independent of the vertex's x/y/z — is exactly `c[N+i].w`. So:

| Output component | Constant term (translation) | = upload flat-array index |
|---|---|---|
| `oPos.x` | `c6.w` | `[3]` |
| `oPos.y` | `c7.w` | `[7]` |
| `oPos.z` | `c8.w` | `[11]` |
| `oPos.w` | `c9.w` | `[15]` |

**This is EXACTLY `g_lastC6[3], g_lastC6[7], g_lastC6[11], g_lastC6[15]` — the "WVP row 3 /
translation row" our code has extracted since notes/24, confirmed correct against the real
bytecode, not inferred.** The `m4x4` semantics also confirm the row-vector-on-the-left,
rows-as-consecutive-registers convention the stereo eye-shear patch (notes/24) and the
`g_trackYt` head-tracking premultiply both assume — all consistent, all now evidence-backed rather
than heuristic.

Bonus finding: `c0[a0.x]` relative addressing + `mad r7, v14, c23.x, c23.y` is the bone-index → 3
consecutive-register lookup that reads the c96.. palette (matches the `StartRegister=96,
Vector4fCount=96` = 32 bones × 3 registers our hook already observed) - direct confirmation the c96
recovery in notes/49-51 is reading the right thing too.

## Consequence: the dossier's #1 open risk is CLOSED, not open

`ENGINE-DOSSIER.md` §6 updated: the row/column-vector convention is now **CONFIRMED**, not
assumed. **This means notes/51's mystery — a computed 212wu forward translation producing zero
visible eye movement — is NOT a convention bug.** The bug is real and still unsolved, but it lives
somewhere else in the pipeline: candidates now narrowed to (a) the composition order/math of
`X1 * T` and the `Transpose(P⁻¹ · T · P)` premultiply in `VRBridge_UpdateHeadTracking`, (b) a sign
or unit error specific to large-magnitude translations that a small rotation/shear perturbation
wouldn't expose, or (c) the translation being computed correctly but overwritten/clamped somewhere
downstream before reaching the GPU. Playbook-correct next step, if the user reopens the FP goal: **a
direct t=(0,0,X) test with increasing X, disassembling/logging the actual patched register-6 values
sent to the GPU (not just the intermediate T/X1 matrices)**, rather than another guess-and-tune
cycle. FP itself remains paused per the user's decision - this session only closed the one
concretely flagged risk, cleanly, with evidence.

🤖 Session via Claude Code: fully automated capture (per user request) + fully offline disassembly;
no game files modified; intro videos silenced during capture and restored after; ~40s of focus-steal
with prior warning and an explicit all-clear on completion.
