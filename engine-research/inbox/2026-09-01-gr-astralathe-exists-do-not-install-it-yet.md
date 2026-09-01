# A public loader for this exact build already has the Lua console and the debug menu — and it may collide with our proxy

Filed by: `/gr`, 2026-09-01
For: the modding session (curator of `engine-research/`)
Full write-up: `external-research/topics/2026-09-01-astralathe-already-ships-the-lua-console-and-the-debug-menu.md`

## ⚠️ Safety first

**Do not install Astralathe into the working Psychonauts folder yet.** It is a loader/patcher for the
same modern Steam/Xbox release we inject into with a `d3d9.dll` proxy, and **which file it uses is
unread** — its GitLab repo and wiki render client-side and returned empty page shells to every
automated fetch. If it wraps `d3d9.dll` too, running both is a conflict; if it uses a different seam,
that itself is worth knowing. Ten minutes in a browser settles it. Its **licence is unread for the
same reason**, so nothing from it may be reused or referenced in code until someone looks.

## What it is

**Astralathe** (Jill / `scrunguscrungus`, GitLab) — *"an all-in-one mod loader, debugging tool, API
extender and patcher for Psychonauts"*, beta, targeting the modern digital release
`[reported 2026-09-01]`. Its published feature list includes:

- **an in-game Lua console**
- **the game's own native debug tools — level select and the debug menu**
- **restoration of debug rendering functions**
- engine-side bugfixes and widescreen support

**PsychoRando** (randomiser) and a **Psychonauts Archipelago integration** are both built on it and
current — so the community reads and writes live game state and hooks game events routinely.

## What it bears on in the dossier

- **§9 / the Lua exec primitive.** notes/69 already made it unnecessary for the FP work (`playerpos`
  reads the pointer chain directly), so this is not a blocker — but a working in-game Lua console
  exists publicly, which is worth knowing before any further effort goes into building one.
- **The dormant debug menu.** `external-research/topics/2026-08-24-debug-menu-and-octree-culling.md`
  is still marked *"live test in progress"*. Astralathe claims that menu as a shipped, working
  feature. At minimum it is a cross-check on the repointing approach; possibly it is the answer.
- **§8 pass inventory.** "Restored debug rendering functions" is exactly the kind of thing that
  makes a pass inventory cheap.

Under the project's own rules this is a **third-party non-tool mod**: study the method and reimplement
in our own words, never rebuild from its files. Treating it as an *observation instrument on a
separate copy of the game* is the safe use.

## One small thing that needs no browser

`status/psychonauts-vr.md` notes *"Height default 60 is a guess — unit scale is recorded nowhere."*
No public source documents Psychonauts' unit scale either — that search came back empty, and the
modding scene's published work is file formats (`.ppf` level packs, `.plb` scenes/models), not
coordinate conventions.

**You do not need the scale.** The build already prints the camera position and, now, the player
position. On flat ground, the **up-axis difference between them is the engine's own eye height**, in
its own units, measured rather than guessed — and it needs no conversion to be correct. Worth
recording during the already-queued `playerpos` run; it costs nothing extra and retires the guess.
