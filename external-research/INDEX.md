# Research index

**Last `/gr` pass: 2026-09-01 — CHECK-IN** (board read closely + INDEX; the dossier was skimmed by section only, so a FULL pass is still owed)**.** Inbox was empty. One new topic and it is the largest gap this
lane had: **Astralathe** — a mod loader / debugger / API extender for this exact modern release —
already ships an **in-game Lua console**, the **native debug menu and level select**, and restored
debug rendering. That touches a primitive recorded as unfinished, a test still marked "in progress",
and an **unassessed injection-collision risk** with our own `d3d9.dll` proxy. Its GitLab pages render
client-side and returned empty shells to every automated fetch, so licence and install method are
**unread, not absent** — a browser is needed. Also answered smaller: the `fpheight` guess does not
need the unit scale, only one delta the build already prints.

Every research topic gathered for this project, newest first. Each row links to a self-contained
write-up in `topics/`. Status tags:

- 🆕 **new** — found, not yet acted on by the modding side.
- 👀 **reviewed** — a modding session has read it and factored it into a decision, but nothing shipped from it yet.
- ✅ **incorporated** — directly led to a real change (code, a test, a note) in one of the other five repos; linked below.
- ❌ **dead end** — checked out, didn't pan out; kept for the record so it isn't re-investigated from scratch.

| Date | Topic | Status | Summary |
| --- | --- | --- | --- |
| 2026-09-01 | [Astralathe already ships the Lua console and the debug menu](topics/2026-09-01-astralathe-already-ships-the-lua-console-and-the-debug-menu.md) | 🆕 new | ⭐ A public, actively-used loader/debugger for the same Steam/Xbox build provides an **in-game Lua console**, the game's **native debug menu + level select**, restored **debug rendering**, and widescreen — with PsychoRando and an Archipelago integration built on top of it, so the community's programmatic reach here is deeper than recorded. **⚠️ Do not install it into the working game folder until its DLL/injection method is read** — it may collide with our proxy. GitLab renders client-side; licence and install steps could not be fetched and need a browser. Plus: measure `fpheight` as the camera-to-player up-axis delta instead of guessing a unit scale nobody documents. |
| 2026-08-25 | [Known flat-game pop-in: AMD driver bug](topics/2026-08-25-known-flat-game-popin-amd-driver-bug.md) | 🆕 new | Object pop-in is a documented flat-game issue, but it's a GPU-vendor-specific (AMD) driver/AA-mode bug, not obviously the same phenomenon as the void — flagged as a cheap confound to rule out, not a new primary hypothesis. Postmortem-digging avenue confirmed exhausted (no rendering/culling technical content exists). |
| 2026-08-24 | [DirectInput mouse-delta injection for Candidate 1](topics/2026-08-24-directinput-mouse-injection-for-candidate-1.md) | 🆕 new | Cross-project transfer from Far Cry 2 research: Vireio Perception's VRBoost technique (inject synthetic deltas into `IDirectInputDevice8::GetDeviceState`'s `DIMOUSESTATE`, masquerading HMD yaw as mouse-look) is a strong match for Candidate 1 — notes/60 already confirmed Psychonauts has a persistent mouse-driven camera-yaw state decoupled from body facing, and was already planning to hook the same `GetDeviceState` call to *observe* it. Injecting there directly would skip the harder "trace where the deltas get stored" step entirely. |
| 2026-08-24 | [Dormant debug menu (Lance McDonald) + octree collision structure](topics/2026-08-24-debug-menu-and-octree-culling.md) | 🆕 new — live test in progress | Psychonauts ships a real, unused developer debug menu (Sphere/Fly camera, Show Collision) reachable by repointing existing pointers, no code injection. Levels also use a confirmed octree for collision, a plausible shared mechanism with the still-unfound frustum/cull test. Directly aimed at the black-void-behind-player investigation. |

## How to add a topic

1. New file in `topics/`, named `YYYY-MM-DD-short-slug.md`.
2. One row added to the table above, newest at the top.
3. Update the status tag here as it moves through review → incorporated/dead-end (the modding side should update this when it acts on a lead, so the index reflects reality without the research side needing to poll).
