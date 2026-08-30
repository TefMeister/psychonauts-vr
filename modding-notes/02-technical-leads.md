# Technical Leads From Prior-Art Research

Date: 2026-08-16. All fetched via WebFetch summaries (AI-summarized page content, not raw
verbatim text) — treat specifics as directionally reliable, re-verify anything load-bearing
before acting on it.

## Jill Crungus's Psychonauts RE blog
https://jillcrungus.com/projects/psychonauts/blog/

Blog index only (didn't crawl every individual post this pass). Confirmed topic areas:
- Lua scripting API documentation (the engine embeds Lua — corroborated independently by our
  own static recon, see `03-static-recon.md`: the exe has `.dflua` / `.dfluatx` PE sections,
  i.e. "Double Fine Lua" data/text, meaning Lua bytecode/scripts are linked directly into the
  executable, not just loose files).
- ASD file format (Nov 2022 post) — an "obsolete but still-used" data format in parts of the
  game. Relevant if we need to touch level/entity data for VR comfort features (e.g. attaching
  metadata to trigger volumes), not directly relevant to the rendering hook itself.
- Making custom levels work (Apr 2024) — level/entity construction pipeline.
- Various posts on cut content, level history — not relevant to VR work, skipped.

**Action for later**: this blog is the richest engine-internals source we know of. Worth a
deeper pass (read individual posts, not just the index) once we're past the initial rendering
hook and need to touch entity/camera data structures for VR comfort options (e.g. exposing an
FOV/head-position hook beyond stereo rendering alone).

**Update 2026-08-24 (deeper pass, see notes/59 for full detail):** read the "Making Levels"
(2024-04-26) post in full. Levels are **PLB** files; meshes split into **meshfrags**; collision
is an **octree** (8-way, bit-packed nodes, "SSECube" AABB bound). Flagged in notes/59 as a
plausible shared mechanism with the still-unfound frustum/cull test — worth checking whether the
octree-walk code collision uses is the *same* function the renderer calls at draw-time. Also
found via the same pass: Lance McDonald's working debug-menu patch for Steam/GOG (ESC to open),
which exposes a "Sphere Camera" (no collision, unrestrained) directly useful for isolating
whether the black-void bug is tied to the normal gameplay camera specifically — see notes/59,
now the top actionable lead for the void hunt.

## Helix Mod / 3Dmigoto fix (2013)
https://helixmod.blogspot.com/2013/05/psychonauts.html

This is the single most directly relevant prior-art data point: **confirmed proof the game's
D3D9 shaders are inspectable and patchable with standard tools**, and specifically that the
game had (and needed) fixes for **stereoscopic 3D** — i.e. someone has already made this exact
game display correctly in per-eye stereo before us.

Key facts extracted:
- Bug fixed: skies/stars/moons/suns rendered at the wrong size/depth when stereo convergence
  was increased ("too close"). Fix covers "all skies, stars, moons, and suns... in every
  level" — implying a small, identifiable set of shaders handles sky/celestial rendering
  across the whole game (good — a tractable shader surface, not everything).
- After the fix, convergence and depth are freely adjustable, including "toyified" depth —
  i.e. the underlying stereo depth pipeline is fully controllable once the offending shaders
  are patched.
- Approach used: NVIDIA 3D Vision profile-based shader injection (references the "Aion"
  profile as a base), with individually extracted/patched shader files — this is the
  classic pre-3Dmigoto-standalone workflow but the same underlying mechanism 3Dmigoto/Helix
  Mod formalized. Confirms **shader-level, not fixed-function-level, patching is the known
  working approach** for this game.
- Included a "texture finding tool" toggle to identify which shader is driving a given visual
  element — a technique we should replicate with our own tooling once we're doing live shader
  dumps (3Dmigoto itself, or ReShade/RenderDoc, can do this against d3d9 titles).

**Combined with our own static recon** (`d3dx9_40.dll` imports include `D3DXCompileShader`,
`D3DXAssembleShader`, `D3DXGetShaderConstantTable`): confirms the game is **not** purely
fixed-function — it compiles/uses real vertex/pixel shaders, consistent with Helix Mod needing
to patch shaders rather than just fixed-function texture stage state. This is good news for a
stereo approach: shader constant tables mean per-eye view/projection matrices can likely be fed
in via existing constant registers once we intercept `SetVertexShaderConstantF`-style calls or
the higher-level D3DX matrix calls (`D3DXMatrixPerspectiveFovRH`, `D3DXMatrixLookAtRH` are both
directly imported — see recon notes).

## RayCarrot/PsychonautsStudio
https://github.com/RayCarrot/PsychonautsStudio

Early-stage / WIP file-format toolkit. README (per fetch) is thin on specifics: claims support
for "different file formats" across PS2/original Xbox-proto/PC versions, currently
open/view/serialization-log capability, texture replacement planned but not implemented. No
engine-architecture or rendering documentation currently in the README. **Low value for the
rendering-hook phase** — worth revisiting later if/when we need to touch level or texture data
directly, but not a blocker or lead for the D3D9 hook work.

## Brobert-in-aus/guides (unverified, general — not Psychonauts-specific)
https://github.com/Brobert-in-aus/guides — two files under `/vr/`:

- `vr/legacy-framebuffer-to-spatial-vr.md`
- `vr/native-abi-godot-vr.md`

**Important caveat**: these are *generic* methodology documents for porting arbitrary legacy
games to VR via Godot + OpenXR as an external host process. They are not specific to
Psychonauts, D3D9, or even native/compiled games necessarily (the framebuffer doc reads as
aimed more at software-rendered/2D titles). Treat as unverified hypotheses per the task
briefing, not vetted engineering guidance.

Relevant ideas if we ever pivot to an external-host architecture instead of an in-process D3D9
hook (unlikely to be our first approach, given Helix Mod already proved in-process shader
patching works for this exact game, but worth having in back pocket):
- Own the presentation boundary: intercept the single "frame complete" point (their framework:
  `Present`-equivalent) as the synchronization node.
- Keep native game simulation authoritative; treat the VR layer as a thin presentation/input
  shim over a stable C ABI, versioned explicitly.
- Run simulation on its native fixed tick rate, interpolate at HMD refresh rate — directly
  applicable to us regardless of architecture, since Psychonauts is presumably fixed/variable
  tick and HMDs run 90Hz+.
- Recommends deterministic replay/verification harnesses before any visual changes — good
  general practice, may be overkill for a single-player single-user personal mod but the
  category-by-category suppression idea (verify one rendering category maps correctly to VR
  before moving to the next) is directly transferable to our stereo-shader-patch approach: fix
  one shader family (e.g. skies, per Helix Mod's own scope) and verify before broadening.

**Given Helix Mod already proved per-eye stereo rendering + shader patching works on this
specific title**, the more directly applicable path for us is: **in-process D3D9 hook +
shader patch**, not the external-host Godot re-architecture these guides describe. The guides
remain useful as a fallback/alternative if the in-process approach hits a wall (e.g. if
Double Fine's shader constant layout turns out to be too tangled to cleanly inject stereo
matrices).

## dxwrapper (elishacloud/dxwrapper)
https://github.com/elishacloud/dxwrapper

- Loads by **DLL stub replacement**: drop a `d3d9.dll` stub (matching what the game imports —
  confirmed by our recon the game imports exactly `d3d9.dll` → `Direct3DCreate9`) next to
  `dxwrapper.dll` and `dxwrapper.ini` in the game directory. Standard Windows DLL search order
  means the game loads the local stub instead of `System32\d3d9.dll`.
  the game loads the local stub instead of `System32\d3d9.dll`.
- Includes an ASI loader compatible with the Ultimate ASI Loader standard, for further
  plugin injection on top of the D3D wrapper.
- Config via `dxwrapper.ini` — no code changes needed for basic wrapping.
- Exposes Present/CreateDevice interception, d3d8-to-9 and d3d9-to-9Ex/12 conversion helpers —
  more machinery than we need (we don't need d3d8/d3d12 conversion), but the core
  wrap-and-intercept mechanism is exactly the injection primitive we want.

**Assessment**: dxwrapper's stub-DLL approach is directly viable and low-risk for this game —
confirmed the game imports d3d9.dll by exactly the name and single entry point
(`Direct3DCreate9`) dxwrapper's `d3d9.dll` stub is designed to intercept. No custom DLL
injector needed; this is a drop-in-folder mechanism, which is also easy to keep entirely inside
our own workspace/tools folder and out of the read-only game directory during dev (test by
copying the *game* files needed for a run into a throwaway folder, or eventually placing the
wrapper DLL directly alongside the exe once we're ready to test live — that step does write
into the game folder and should be treated as a deliberate, reversible, user-visible action,
not implied by this recon phase).
