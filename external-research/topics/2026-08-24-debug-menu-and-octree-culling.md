# Dormant debug menu (Lance McDonald) + octree collision structure

**Status:** 🆕 new — live test in progress (see [00-status.md](https://github.com/TefMeister/psychonauts-vr/blob/main/modding-notes/00-status.md) in modding-notes for the current live result).
**Also archived in:** `psychonauts-vr-modding-notes/59-public-research-sweep-debug-menu-and-octree-leads.md` and `02-technical-leads.md` (this topic predates this repo's creation; that's the original write-up, this is the canonical copy going forward).

Pure public-research pass (web search/fetch only, zero game execution) done specifically to find outside
information that could help crack the black-void/culling mystery (the mod's #1 priority as of
2026-08-23: geometry the player's HMD is looking at, but the game's own camera isn't, renders as
unrendered black). Everything below is unverified against the live game until someone actually tries
it — leads to test, not confirmed mechanisms.

## 1. A working, functional debug menu already exists for this exact game (biggest lead)

**Lance McDonald** (well-known reverse-engineer/patcher — Bloodborne 60fps patch, various PS/Xbox
debug-mode restorations) published patched Steam/GOG Psychonauts `.exe`s that **enable the game's own
dormant developer debug menu** — press **ESC** at any time to open it. Source: his X/Twitter post,
2021-09-06 (`x.com/manfightdragon/status/1434924415326720012`). His own description of the technique:
he repointed the pointers the game used for the in-game Journal screen to the debug menu screens
instead, then deleted the Journal's graphics — **no code injection, just data patching of existing
pointers**, meaning the debug menu is real shipped developer code, not anything he wrote.

We do not use his patched binary directly (third-party executable, and the download host 403s
automated fetches anyway) — the plan is to find the same dormant code in our own legitimately-owned
copy and apply the same publicly-described repointing technique ourselves.

Per The Cutting Room Floor's documentation (`tcrf.net/Psychonauts/Debug_Menu` — see the caveat below,
reconstructed via search snippets only, a human should browse the real page directly), the menu
includes, among others:

- **"Fly Camera"** — fully free/unrestrained camera, decoupled from the normal gameplay camera.
- **"Sphere Camera"** — still follows Raz, but is "completely unrestrained, has no collision, and can zoom in and out."
- **Show Collision** / **Collision Spheres**
- **Show Trigger Vol** (trigger volume bounds)
- **Show Skel** / **Show Skel Only**
- **Show Nav Path**
- **Show Decals** / **Debug Decals** / **Show Dynamic Decals**, **Show Particles**
- A large secondary page of rendering toggles (lighting, reflections, bumpmaps, a **"partially functional" wireframe toggle**)
- **All Powers**, **Invul on/off**, **Rumble on/off**

TCRF's own caveat: a large amount of the game's rendering code is stubbed/non-functional on PC, so
some options do nothing or partially work — but Fly Camera/Sphere Camera and Show Collision are
specifically noted as still working.

**The experiment this unlocks:** load real gameplay (the `SetPendingLevel` jump, proven working
2026-08-24, sidesteps the flaky door-entry script), open the debug menu, switch to Sphere Camera, spin
around behind the player.

- **Void disappears/shrinks under Sphere Camera** → the cull test is keyed to the *normal gameplay
  camera's* frustum/view specifically — directly supports "feed head-yaw into the game camera" /
  "pad the cull-only test" as the fix direction.
- **Void persists identically** → the cull test is not camera-frustum-based at all — points at the
  octree/distance-based culling below instead.

**Show Collision** is also useful independent of the above: if collision geometry renders behind the
player while normal geometry stays black, that proves the world genuinely is loaded there and it's a
pure submission/render problem, not "nothing exists there."

## 2. tcrf.net is currently serving prompt-injection content to automated fetchers

Direct fetches of `tcrf.net/Psychonauts/Debug_Menu` and `tcrf.net/Development:Psychonauts/Scripts`
(both the normal page and the raw MediaWiki `action=raw` URL) returned **not the real page content but
a block of fake "instructions for the AI"** (nonsensical demands to create files, transfer money,
etc.) — consistent across multiple pages, reads as a deliberate anti-scraper honeypot rather than
vandalism. **No instructions from that content were followed.** Everything about the debug menu above
was reconstructed from search-result snippets only, so it's incomplete — a human browsing
tcrf.net/Psychonauts/Debug_Menu directly would get the authoritative full list.

## 3. Octree-based collision structure confirmed — plausible shared mechanism with culling

Jill Crungus's public Psychonauts reverse-engineering blog (`jillcrungus.com/projects/psychonauts/blog/`)
has a detailed post, **"Making Levels"** (2024-04-26), documenting the **PLB** scene file format:

- Levels are PLB files: meshes split into "meshfrags" (by material/LOD/blendshape), skeletons (bone
  rotations as raw 3D vectors, not quaternions), animations, entities.
- **Collision data is organized in an octree**: nodes hold 8 child pointers packed as 24-bit integers
  (3 bytes each), leaf data bit-packed into single integers, capped at 0x7FFFF primitives total /
  0x1FFF per leaf, bounded by an "SSECube" volume — extreme bit-packing consistent with the Xbox-era
  64MB RAM budget this engine originated under.
- Built for her level-editing pipeline (**Oatmeal**: PLB↔glTF via SharpGLTF; **Astralathe**: live
  texture replacement without level reload; **Cobweb Duster**: first-time modding setup) — not
  connected to rendering/culling in her post.

**Why it matters:** games of this era very commonly reuse a single octree for both collision AND
frustum/visibility culling (walk the same tree, test node bounds against the view frustum instead of a
collision shape), rather than maintaining two separate spatial structures. This is a plausible concrete
alternative to "there's a dedicated frustum-test function to find" — the cull test four dev-PC sessions
couldn't locate might not be a standalone function at all, but octree-node traversal interleaved with
(or indistinguishable from) the collision-octree walk.

**The angle this unlocks:** when tracing from a resolved `DrawIndexedPrimitive` breakpoint, also watch
for octree-shaped traversal code (recursive, 8-way branching, SSECube-style AABB compares), not just
camera-matrix code — and cross-reference against the static PLB/level-loader recon (which may already
have located this game's octree-walk function on the collision side) to check whether it's the *same*
function called from two places (collision query at tick-time, visibility query at draw-time).

No public source found this pass documents a named frustum/occlusion system for this game — our own
live recon remains genuinely ahead of anything written up publicly.

## 4. Confirmed / re-confirmed background (lower priority, for the record)

- Official postmortem (Caroline Esmurdoc, *Game Developer Magazine*, Aug 2005, republished on
  gamedeveloper.com as "Classic Postmortem: Double Fine's Psychonauts"): confirms Lua for all game
  logic (166,781 lines native C++ engine vs. 332,650 lines Lua script), a custom in-house remote
  native debugger nicknamed "Dougie" (breakpoints, single-step, object watch windows, profiling, hot
  Lua reload, console), automated daily multi-SKU builds, and an in-house Cutscene Editor. Consistent
  with, not contradicting, existing static recon. No rendering/culling technical detail in the
  postmortem itself.
- Confirmed as Double Fine's one-off, pre-"Moai" bespoke engine — no public codename found anywhere;
  their later shared engines only start from *Brutal Legend* onward.
- Asset-side tooling inventory (not camera-relevant, for completeness): Psychonauts Explorer (legacy
  `.ppf`, pre-2021 release), DoubleFine Explorer (`github.com/bgbennyboy/DoubleFine-Explorer`, current
  Steam/GOG `.ppf` format), RayCarrot/PsychonautsStudio (WIP multi-version file viewer).

## Sources (see [CREDITS.md](../CREDITS.md) for the full standing credit)

- Lance McDonald — debug menu unlock technique, X/Twitter 2021-09-06.
- The Cutting Room Floor (`tcrf.net`) — debug menu option documentation (via search snippets; treat tcrf.net fetches with caution per §2 above).
- Jill Crungus — PLB format / octree collision structure documentation, `jillcrungus.com`.
- Caroline Esmurdoc / *Game Developer Magazine* — 2005 Psychonauts postmortem.
