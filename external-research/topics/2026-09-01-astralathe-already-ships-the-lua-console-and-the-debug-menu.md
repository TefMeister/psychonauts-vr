# Astralathe already ships an in-game Lua console, the native debug menu, and restored debug rendering

**Status:** 🆕 new · **Priority:** ⭐ high — it touches a primitive this project recorded as blocked,
a live test that is currently "in progress", and an injection-collision risk nobody has checked.

## What was found

**Astralathe** (author: Jill / `scrunguscrungus`, hosted on **GitLab**, not GitHub) is described by
its own publishing as *"an all-in-one mod loader, debugging tool, API extender and patcher for
Psychonauts"* `[reported 2026-09-01]`. It targets **the modern digital release** — the same Steam/Xbox
PC build this project mods — and is in beta.

Its stated feature set, in the terms that matter here:

| Astralathe provides | Why this lane cares |
| --- | --- |
| **An in-game Lua console** | this project has recorded a "Lua exec primitive" as unfinished since notes/47 |
| **The game's own native debug tools — level select, debug menu** | `topics/2026-08-24-debug-menu-and-octree-culling.md` is still marked *"live test in progress"* |
| **Restoration of debug rendering functions** | directly relevant to §8's pass inventory and to anything culling-related |
| Full mod loading system + in-game management UI | not needed here |
| Engine-side bugfixes and **widescreen support** | widescreen means it already touches projection/aspect |

There is also an active surrounding ecosystem built **on** it: **PsychoRando** (a randomiser) and a
**Psychonauts Archipelago integration**, both current. An Archipelago integration necessarily reads
and writes live game state and hooks game events, so the community's programmatic reach into this
game is materially deeper than this lane had recorded.

Separately, **PsychonautsStudio** (RayCarrot, **MIT**) is a file-format tool covering all versions
including the PS2 build and early Xbox prototypes, able to view contents and **serialization logs**;
its README lists data modification and level-entity editing as *upcoming*, not present
`[reported 2026-09-01]`.

## Why this matters, stated carefully

The estate's own playbook has a rule for this — *before you build it, check whether the game shipped
it*. This is the adjacent case: **before you finish building it, check whether someone else already
did.**

Three concrete consequences:

1. **The Lua exec primitive may not need finishing.** Note that notes/69 has already made it
   *unnecessary* for the immediate goal — `GetPlayerPosition_impl` was decoded statically and
   `playerpos` reads the chain directly, so the FP work is no longer blocked on Lua. But a working
   in-game Lua console remains a general-purpose lever for everything after that, and one exists.
2. **The debug-menu test may already be answered in public.** Our topic from 2026-08-24 (Lance
   McDonald's dormant debug menu, reachable by repointing pointers) is still marked *live test in
   progress*. Astralathe claims to restore that menu, plus level select and debug rendering, as a
   shipped feature. Whatever it does is at minimum a **cross-check on our own approach**, and at best
   an answer.
3. **⚠️ An injection-collision risk that has not been assessed.** Astralathe is a loader/patcher for
   the same game we inject into with a `d3d9.dll` proxy. If it uses the same wrapper name or patches
   the same code, running both is a conflict — and if it uses a *different* seam, that is worth
   knowing too, because it would mean a second injection point exists here. **Neither is established.
   Nobody should install it into the working Psychonauts folder until this is checked.**

## ⚠️ What this pass could NOT establish, and why it is not a negative

Astralathe's repository and wiki are on **GitLab**, whose pages render client-side. Every automated
fetch returned only the page shell — `Loading` — with no content. So the following are **unread, not
absent**:

- **Its licence.** Unknown. Nothing may be reused, referenced in code, or redistributed until someone
  reads it. (Under the project's own rules, a third-party non-tool mod is study-and-reimplement
  anyway — never rebuild-from-files.)
- **Its installation method and which DLL it uses.** This is the collision question above, and it is
  the single most important unknown here. A dependent project's setup guide confirms only that the
  game must be *"Psychonauts for PC"* via **Steam or Xbox**, and delegates the actual install steps to
  an external document.
- Its exact debug-console command surface.

The GameBanana listing fetched as navigation chrome only, for the same class of reason. **All of the
above needs ten minutes in a browser, not another automated pass** — an automated fetch that returns a
page skeleton is a tooling failure that looks exactly like an empty page.

## A separate, smaller answer: the `fpheight` guess does not need the unit scale

`status/psychonauts-vr.md` records *"Height default 60 is a guess — unit scale is recorded nowhere and
a respawn is ~26,000 units."* Public sources do not document Psychonauts' world unit scale either;
that search came back empty, and the modding scene's published work is about file formats (`.ppf`
level pack files, `.plb` scene/model files) rather than coordinate conventions.

**But the scale is not the quantity you need.** The build already prints both the camera position and,
as of `playerpos`, the player position. Stand Raz on flat ground and read the **difference along the
up axis** between the two — that *is* the engine's own eye-height-above-feet, in whatever units it
uses, measured rather than guessed, and it needs no conversion to metres to be correct. One reading
during the already-queued `playerpos` run replaces the guess, at zero extra cost.

(If a real metres-per-unit figure is ever wanted — for IPD, say — the honest route is a known
real-world distance in a level measured through a tool like PsychonautsStudio, not a public source,
because no public source states it.)

## Concrete next steps, in order

1. **Do not install Astralathe into the working game folder.** First read, in a browser: its licence,
   its install method, and which DLL it uses. The proxy this project ships is `d3d9.dll`; a clash
   there is the thing to rule out.
2. If it is compatible, treat it as an **observation instrument and a cross-check**, on a separate
   copy of the game — not as a dependency and not as a source to copy from.
3. During the next `playerpos` run, **also record the camera-to-player up-axis delta** and set
   `fpheight` from it.

## Sources

- https://gamebanana.com/tools/12094
- https://gitlab.com/scrunguscrungus/astralathe/-/tree/master
- https://gitlab.com/scrunguscrungus/astralathe/-/wikis/Installing-Astralathe
- https://gitlab.com/scrunguscrungus/astralathe/-/releases
- https://github.com/RayCarrot/PsychonautsStudio
- https://github.com/Akashortstack/PsychoRando
- https://github.com/Akashortstack/Psychonauts-AP-Integration/blob/main/worlds/psychonauts/docs/setup_en.md
- https://quickandeasysoftware.net/readmes/PsychonautsExplorerHelp/psychonautsfiles.htm

## ✅ Outcome — assessed from its source by the modding lane, 2026-09-01 (home PC, `/pd`)

All three "needs ten minutes in a browser" unknowns are answered, and no browser was needed: **GitLab's
REST API returns raw files** (`/api/v4/projects/34250039/repository/files/<url-encoded path>/raw?ref=master`)
even though the web UI renders client-side. Full write-up: `modding-notes/70-…` §2.

- **Injection file: `dsound.dll`**, not `d3d9.dll` — it forwards twelve DirectSound exports and loads
  `Astralathe.dll` only when the process is `psychonauts.exe`. Shipped set has **no `d3d9.dll`**, so
  there is no filename clash with our proxy. `[reported 2026-09-01, from source]`
- **Licence: GPLv3.** Study-only is now a legal requirement, not just our rule.
- **Same seams, though:** it IAT-hooks `Direct3DCreate9` and `DirectInput8Create` and vtable-swaps
  `CreateDevice`, `EndScene`/`Reset` and `GetDeviceState`/`GetDeviceData`/`SetCooperativeLevel` — the
  slots our proxy owns. **Verdict: no file collision, a real functional one. Keep it off the working
  install; use it on a separate copy only.** Its menu key is F10.
- Feature claims confirmed by the tree: `ImGui/LuaPad.cpp`, `ImGui/DebugConsole.cpp`,
  `DebugDraw/EDebugLineManager.cpp`, `EScriptVM.cpp`, `ERenderer.cpp`, `ECamera.cpp`.

The "unassessed proxy-collision risk" wording above is retired. The follow-up read of its source for
what it *knows* about the binary is a separate topic:
`2026-09-02-astralathe-source-corroborates-the-dossier-and-fixes-the-lua-state.md`.
