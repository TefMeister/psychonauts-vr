# Credits & Attribution

This project is a reverse-engineering and modding effort built on the public
research, tools, and documentation of many people who came before us. None of
this would be possible without their work. We list every source we've drawn
on below — including work that helped only as inspiration — by name or
handle, as accurately as we could verify it.

## The game itself

This mod modifies, at runtime, the original **Psychonauts** (2005) by
**Double Fine Productions** (https://www.doublefine.com). The game, its
engine, and all of its assets are theirs, and the game is the entire reason
this project exists. **No game files, code, or assets are distributed in any
of this project's repositories** — only code, notes, and tools we wrote
ourselves, plus third-party components whose licenses permit redistribution
(noted below).

## Prior art, tools, and research this repo draws on

| Source / Work | Creator(s) | Link |
|---|---|---|
| Dormant developer debug-menu unlock technique (pointer repointing, no code injection) | Lance McDonald | https://x.com/manfightdragon/status/1434924415326720012 |
| Debug menu option documentation | The Cutting Room Floor community | https://tcrf.net/Psychonauts/Debug_Menu |
| Psychonauts Reverse Engineering Blog (PLB format, octree collision structure) | Jill (JillCrungus) | https://jillcrungus.com/projects/psychonauts/blog/ |
| PsychonautsStudio (file tools) | RayCarrot | https://github.com/RayCarrot/PsychonautsStudio |
| DoubleFine Explorer | bgbennyboy | https://github.com/bgbennyboy/DoubleFine-Explorer |
| "Classic Postmortem: Double Fine's Psychonauts" | Caroline Esmurdoc, *Game Developer Magazine* (Aug 2005) | https://www.gamedeveloper.com |
| Vireio Perception / VRBoost (DirectInput mouse-delta injection technique, cross-applied from Far Cry 2 research) | cybereality and the Vireio Perception contributors | https://github.com/cybereality/Perception |
| dinput8-hook (minimal DirectInput hook reference) | fiki574 | https://github.com/fiki574/dinput8-hook |
| "Psi-Epilepsy: Fixing Object Pop-In and Refresh Rate Bugs" guide | Steam Community guide author | https://steamcommunity.com/sharedfiles/filedetails/?id=841015059 |
| AMD flickering-fix forum thread | GOG.com forum community | https://www.gog.com/forum/psychonauts/solution_resolving_flickering_issues_on_amd_cards |
| Astralathe (mod loader / debugging tool / API extender for Psychonauts — in-game Lua console, native debug menu, level select, restored debug rendering) | Jill (`scrunguscrungus`) | https://gitlab.com/scrunguscrungus/astralathe |
| PsychonautsStudio (file-format tools and serialization logs for all Psychonauts versions, MIT) | RayCarrot | https://github.com/RayCarrot/PsychonautsStudio |
| PsychoRando (Psychonauts randomiser, built on Astralathe) | Akashortstack and contributors | https://github.com/Akashortstack/PsychoRando |
| Psychonauts Archipelago integration (evidence of deep programmatic reach into the live game) | Akashortstack and contributors | https://github.com/Akashortstack/Psychonauts-AP-Integration |
| Psychonauts Explorer file-format notes (`.ppf` level pack files, `.plb` scene/model files) | Quick and Easy Software | https://quickandeasysoftware.net/readmes/PsychonautsExplorerHelp/psychonautsfiles.htm |
| Astralathe source (published offsets, signatures and calling conventions for `psychonauts.exe`, read online via the GitLab REST API, GPLv3 — studied, nothing copied) | Jill (`scrunguscrungus`) | https://gitlab.com/scrunguscrungus/astralathe |
| Psychonauts Lua API documentation (community, work in progress) | its GitLab contributors | https://psycholuaapi.readthedocs.io/ |
| "Psychonauts: Camera Control" (the boss-fight first-person note) | Kill Ten Rats | https://www.killtenrats.com/2011/11/10/psychonauts-camera-control/ |
| injector (pattern-scan / hooking library Astralathe builds on; named for context only) | thelink2012 | https://github.com/thelink2012/injector |
| PolyHook 2 (hooking library Astralathe builds on; named for context only) | Stevemk14ebr | https://github.com/stevemk14ebr/PolyHook_2_0 |
| PsychoPortal (.NET library for Psychonauts' formats — the `VisibilityTree` / `CollisionTree` / `NavMesh` scene structures, the `Domain` / `DomainEntityInfo` / `EntityInitData` scene-graph and entity-placement records, and the `Octree` / `OctreeNode` / `OctreeLeaf` / `SSECube` spatial classes; read online via the GitLab REST API, nothing copied. **Licence: GPL-3.0 with a custom exception, Copyright (c) 2022 Jill Nesbit** — studied only) | Jill Nesbit (`scrunguscrungus`) | https://gitlab.com/scrunguscrungus/psychoportal |
| "Psychonauts custom levels — how we got here" (the `.plb` ↔ glTF round-trip pipeline, and the evidence that the community works natively in game units) | Jill (JillCrungus) | https://cohost.org/JillCrungus/post/5742197-psychonauts-custom-l |
| "Making Levels" (custom-level reverse-engineering write-up — model import, rigging, animation, collision and octree structures) | Jill (JillCrungus) | https://jillcrungus.com/projects/psychonauts/blog/2024/04/26/making-levels.html |

Development on this project is AI-assisted: much of the research, code, and
documentation was produced with **Claude (Anthropic)** (https://claude.com)
working alongside the project owner.

## Missing from this list?

If you — or someone whose work you know — contributed to, influenced, or
even just inspired anything used in this project and you aren't credited
here, please **open a GitHub issue on this repo** and we'll correct it as
soon as possible. We would much rather over-credit than leave anyone out.

## Respecting creators

This project exists because other people generously shared their
reverse-engineering research, tools, and modding know-how in public — we've
tried to credit every one of them by name or handle above, as accurately as
we could verify. If you are the creator or rightful owner of anything
credited or used here and you'd rather your work not be referenced in this
repo, or you want specific content removed or no longer used by the mod,
please tell us: **open a GitHub issue on this repo**. We'll act on that
request promptly — no argument, no delay — and we'll find another way to get
the job done that doesn't rely on your material. This is your work; we're
just grateful to have learned from it.
