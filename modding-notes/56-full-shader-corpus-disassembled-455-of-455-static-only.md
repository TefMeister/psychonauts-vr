# 56 — Full 455-shader corpus disassembled and categorized (static only)

**Date:** 2026-08-20 evening, dev machine. Continuation of the static-only research session
(notes/55). The `PSYVR_REG_HISTO=1` diagnostic (built session 36, reused session 52) already had
every one of the game's 455 compiled vertex shaders sitting on disk as `.bin` files from an earlier
capture — this session batch-disassembled **all 455**, offline, via the same `D3DXDisassembleShader`
+ Python `ctypes` method notes/52 validated, and categorized the corpus by register usage and
instruction pattern. Zero game execution.

## Corpus-wide stats

| Property | Count / 455 |
|---|---|
| Uses register 6 (WVP world-transform) | 445 |
| Uses register 50 | **455 (100%)** |
| Uses relative addressing (`c0[a0.x]`) + `m4x3` bone-blend | 239 |
| Does NOT use register 6 | 10 |

**Correction to a long-standing project belief:** register 50 appears in **every single shader**,
not just the "UI" ones. This fully confirms notes/52's finding (from one sample) at corpus scale:
c50 is the standard D3D9 half-pixel texel-alignment offset every properly-authored shader adds to
`oPos`, not something UI-specific. The earlier characterization (notes/36) of c50 as "UI depth
shift target" was correct about WHERE we choose to apply our own UI-depth correction, but wrong
about c50 being UI-exclusive in the game's own usage.

## The 10 non-c6 shaders, exactly identified

Shader dump indices **3, 447, 448, 449, 450, 451, 452, 453, 454, 455** are the complete, exact set
of shaders that skip the world-transform path — confirms notes/36's "10 screen-space UI shaders"
claim precisely, for the first time with exact indices rather than just a count. All ten share the
same declare signature (`dcl_position v0, dcl_color v2, dcl_texcoord v3`, growing sets of extra
texcoord-stage registers `c10..c19` for some variants) — consistent with a single UI/HUD shader
family with a few permutations (probably by texture-stage count).

## The skinning cohort, exactly reconfirmed

239/455 shaders use `m4x3` bone-blend + relative addressing off a computed bone index (the pattern
fully decoded in notes/52: `mad r7, v14, c23.x, c23.y; mova a0.x, r7.x; m4x3 r0.xyz, v0, c0[a0.x]`)
— this is the exact same 239 count session 52 found live via a cross-check scan, now independently
reconfirmed via a full, separate offline pass. This is the real "is this shader skinned" signal —
**a naive "does it reference any register ≥64" check is NOT reliable** (see next section).

## False lead caught and corrected this session

Initially flagged 183 shaders as "bone-range but not skin-blend" using a crude
"references any constant register ≥64" heuistic. Checked the simplest one (dump #12) directly:

```
vs_2_0
dcl_position v0
dcl_normal v1
dcl_texcoord v3
mov r11, v0
mov r1, v1
m4x4 r4, v0, c6          ; rigid transform, straight from v0 - NOT bone-blended
add oPos, r4, c50
mov oD0, c2
mul oT0.xy, v3, c94       ; <- this is what tripped the naive heuristic
```

`c94` here is an ordinary **per-material UV tiling/scale constant**, completely unrelated to the
c96+ bone palette — it just happens to number ≥64 because the shader compiler's constant allocator
assigned it there. **Lesson banked**: "references register N" is not a reliable proxy for "is this
a skinning shader" — only the `m4x3` + relative-addressing pattern is. Recording this so a future
session doesn't re-derive the same false lead.

## Open item: shadow-caster shaders not identifiable from vertex-shader text alone

Tried to spot notes/44's hypothesized shadow-pass shaders by instruction count / minimal-output
patterns (a `dcl_position + dcl_color`-only shader, dump #2, is a plausible candidate — simplest in
the whole corpus, only touches c6+c50). **Could not conclusively identify shadow casters this way**
— vertex-shader bytecode alone doesn't reveal which render target / pass a shader is bound during;
that needs a live, render-target-correlated capture (the existing `PSYVR_TRACE_FRAME=1` diagnostic,
notes/35) which requires running the game. Flagged as a live-session item, not resolved statically.

## Files

Full categorized data (per-shader: version, instruction count, referenced registers, relative-
addressing flag, `m4x3`/`m3x3` flags, `dcl_*` list) written to
`shader_corpus.json` in this session's scratchpad — not committed (large, derived/regenerable data,
not source). Regenerate anytime from the `.bin` dumps with `PSYVR_REG_HISTO=1` (or `SHADER_DUMP=1`)
+ the disassembly script pattern from notes/52.

🤖 Pure offline disassembly of previously-captured shader bytecode; zero game execution this session.
