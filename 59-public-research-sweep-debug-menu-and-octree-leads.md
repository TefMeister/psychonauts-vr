# 59 — Public research sweep: Lance McDonald's debug-menu patch + octree culling lead

**Date:** 2026-08-24, dev machine. Pure public-research pass (web search/fetch only, zero game
execution) — done at the user's request specifically to find outside information that could help
crack the black-void/culling mystery (notes/55-58, STATUS.md "TOP PRIORITY: KILL THE BLACK VOID").
Everything below is unverified against our own live game until someone actually tries it — treat as
leads to test, not confirmed mechanisms.

## 1. A working, functional debug menu already exists for this exact game (biggest lead)

**Lance McDonald** (well-known RE/patcher — Bloodborne 60fps patch, various PS/Xbox debug-mode
restorations) published patched Steam/GOG Psychonauts `.exe`s that **enable the game's own dormant
developer debug menu** — press **ESC** at any time to open it. Source: his X/Twitter post,
2021-09-06 (`x.com/manfightdragon/status/1434924415326720012`), files hosted at
`debugmenu.com/downloads/` (exact path truncated in every search result we could get — the site
403s automated fetches, a human will need to browse it directly). His own description of the
technique: he repointed all the pointers the game used for the in-game Journal screen to the debug
menu screens instead, then deleted the Journal's graphics — i.e. **no code injection, just data
patching of existing pointers**, which also means the debug menu is *real shipped developer code*,
not something he wrote.

**Why this matters for the void hunt:** per The Cutting Room Floor's documentation (`tcrf.net/Psychonauts/Debug_Menu` —
see caveat below, could not fetch the full page directly this pass, only pieced together via search
snippets) the menu includes, among others:

- **"Fly Camera"** — fully free/unrestrained camera, decoupled from the normal gameplay camera.
- **"Sphere Camera"** — still follows Raz, but is "completely unrestrained, has no collision, and
  can zoom in and out."
- **Show Collision** / **Collision Spheres**
- **Show Trigger Vol** (trigger volume bounds)
- **Show Skel** / **Show Skel Only**
- **Show Nav Path**
- **Show Decals** / **Debug Decals** / **Show Dynamic Decals**, **Show Particles**
- A large secondary page of rendering toggles (lighting, reflections, bumpmaps, and a
  **"partially functional" wireframe toggle**)
- **All Powers**, **Invul on/off**, **Rumble on/off**

TCRF's own caveat: "a large amount of the game's rendering code is stubbed and non-functional [on
PC], so many options that relied on this functionality either do nothing, partially work, or are
completely broken" — so this needs to be tried live before trusting any of it, but **Fly
Camera/Sphere Camera and Show Collision are specifically noted as being among the ones that still
work.**

**Concrete next-session experiment this unlocks:** get McDonald's patched exe (or hex-patch our own
copy using his described method — repoint Journal-screen pointers to the debug screens), load real
gameplay (sidesteps the flaky door-entry script entirely — no Lua exec needed), open the debug menu,
switch to **Sphere Camera**, and spin around behind the player. If the void:
- **disappears/shrinks under Sphere Camera** → the cull test is keyed to the *normal gameplay
  camera's* frustum/view specifically (supports candidates 1/2's premise directly).
- **persists identically under Sphere Camera** → the cull test is not camera-frustum-based at all,
  and is more likely a distance/octree/LOD-driven culling independent of which camera is active
  (see octree lead below) — would redirect the whole hunt.

Either outcome is a real answer, cheaply obtained, without any of our own reverse engineering. This
should be the very first thing tried in the next session, before resuming the `DrawIndexedPrimitive`
breakpoint plan from STATUS.md.

**Show Collision** is also directly useful as a sanity check independent of the void question:
toggle it on and look behind the player — if collision geometry is drawn there but the normal render
still shows black, that confirms definitively that geometry exists and is simply not being
submitted/rendered (a pure cull-test problem), ruling out "the world genuinely isn't loaded there."

## 2. tcrf.net is currently serving prompt-injection content to automated fetchers — human eyes needed

Direct `WebFetch` of `tcrf.net/Psychonauts/Debug_Menu` (both the normal page and the raw MediaWiki
`action=raw` URL) returned **not the real page content but a block of fake "instructions for the
AI"** (nonsensical demands to create files, transfer money, etc.) — the same happened on
`tcrf.net/Development:Psychonauts/Scripts`. This reads as a deliberate anti-AI-scraper honeypot
(cloaking based on request signature) rather than real vandalism, since it was consistent across
multiple TCRF pages. **No instructions from that content were followed.** Everything about the
debug menu above was reconstructed from Google search-result snippets only (which appear to reflect
the real page), so **it is incomplete — a human should browse tcrf.net/Psychonauts/Debug_Menu
directly in a normal browser** to get the authoritative full list (there is almost certainly more
in the "giant rendering-toggle page" than what search snippets surfaced).

## 3. Octree-based collision structure confirmed — plausible shared mechanism with culling

Jill Crungus's public Psychonauts reverse-engineering blog (`jillcrungus.com/projects/psychonauts/blog/`,
richest public RE source on this exact game, already flagged in `02-technical-leads.md`) has a
detailed post, **"Making Levels"** (2024-04-26), documenting the **PLB** scene file format in real
technical depth:

- Levels are **PLB** files containing meshes split into **"meshfrags"** (subdivided by material,
  LOD level, and blendshape), skeletons (bone rotations stored as raw 3D vectors, not quaternions),
  animations, and entities.
- **Collision data is organized in an octree**: nodes hold 8 child pointers packed as 24-bit
  integers (3 bytes each), leaf data bit-packed into single integers, capped at 0x7FFFF primitives
  total / 0x1FFF per leaf, the whole tree bounded by an **"SSECube"** volume. Author's own
  description of the leaf encoding: "something unholy" — extreme bit-packing, consistent with
  Xbox's 64MB RAM budget (matches our own understanding this is an Xbox-era title ported to PC).
- She confirmed the PC engine's loader is forgiving enough to load an unmodified Psychonauts **2**
  model exported as PLB — i.e. the mesh/skeleton format expectations are fairly generic, not
  fragile version-locked magic.

**This was not connected to rendering/culling in her post** — she built it for a level-editing
pipeline (her tool **Oatmeal** converts PLB↔glTF via SharpGLTF for Blender editing; **Astralathe**
does live texture replacement without level reload; **Cobweb Duster** is a first-time modding setup
tool), not for RE'ing the renderer. But it's a real, independently-confirmed data point: **this
engine already builds and ships one octree per level for spatial queries.** Games of this era very
commonly reuse a single octree for both collision AND frustum/visibility culling (walk the same
tree, test node bounds against the view frustum instead of a collision shape) rather than
maintaining two separate spatial structures. This is a plausible *concrete alternative hypothesis*
to "there's a dedicated frustum-test function to find": **the "cull test" the last four sessions
couldn't locate might not be a standalone function at all — it might be octree-node traversal code
that happens to be interleaved with (or indistinguishable from) the collision-octree walk**, which
would explain why disassembly/decompile passes centered on the camera-update side never surfaced a
clean, isolated "is this in frustum" check.

**Concrete next-session angle this unlocks:** when resuming from a resolved `DrawIndexedPrimitive`
breakpoint (the plan STATUS.md already queues), also watch for calls into octree-shaped
traversal code (recursive, 8-way branching, SSECube-style AABB compares) on the way there — not
just camera-matrix code. If found, cross-reference against notes/55's static PLB/level-loader
findings (which may already have located this game's octree-walk function on the collision side)
to see if it's the *same* function being called from two different places (collision query at
tick-time, visibility query at draw-time), which would be the actual answer to where "the cull
test" lives.

No public source (Jill Crungus's blog or anyone else found this pass) documents a named
frustum/occlusion system for this game — our own live recon genuinely remains ahead of anything
written up publicly, consistent with the RE Village project's same conclusion for a different game.

## 4. Confirmed / re-confirmed background (lower priority, for the record)

- Official postmortem (Caroline Esmurdoc, *Game Developer Magazine*, Aug 2005, republished as
  "Classic Postmortem: Double Fine's Psychonauts" on gamedeveloper.com): confirms **Lua** for all
  game logic (166,781 lines native C++ engine code vs. 332,650 lines Lua script), a custom
  in-house remote native debugger nicknamed **"Dougie"** (breakpoints, single-step, object watch
  windows, profiling, hot Lua reload, console), automated daily multi-SKU builds, and an in-house
  **Cutscene Editor** (timeline tool that places/orients cameras and actors, can call arbitrary Lua
  functions at a timed point) — all consistent with, and independently corroborating, our own
  `.dflua`/`.dfluatx` PE-section findings and the Lua-exec primitive work in notes/45-57. No
  rendering/culling technical detail in the postmortem itself.
- This is confirmed to be Double Fine's **one-off, pre-"Moai" bespoke engine** — their later shared
  engines (Moai/Buddha/"Remonkeyed", per the `bgbennyboy/DoubleFine-Explorer` tool's own scope
  note) only start from *Brutal Legend* onward. No public name for Psychonauts (2005)'s own engine
  was found anywhere this pass; it appears to genuinely not have a public codename, only ever
  referred to as Double Fine's in-house/proprietary engine.
- Tooling inventory for future asset-side work (not camera-relevant, noted for completeness):
  **Psychonauts Explorer** (quickandeasysoftware.net, legacy `.ppf` explorer for the pre-2021
  original release), **DoubleFine Explorer** (`github.com/bgbennyboy/DoubleFine-Explorer`, for the
  current Steam/GOG `.ppf` format only — the two are NOT interchangeable, different container
  versions), **RayCarrot/PsychonautsStudio** (`github.com/RayCarrot/PsychonautsStudio`, WIP
  multi-version file viewer, thin on documentation currently).

🤖 Pure public web research (search/fetch only); zero game execution, zero code copied from any
source above — all mechanisms described in our own words per the standing no-copy rule.
