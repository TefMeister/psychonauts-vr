# Nobody documents Psychonauts' world scale — and there is a structural reason, so stop expecting a cross-check

**Status:** 🆕 new · **Priority:** medium — a confirmed negative. It does not unblock anything, but it
tells the `fpheight` measurement that it will have no external number to check itself against, which
changes how that measurement should be taken.

## The question

The dossier records, in two places, that this is unknown:

> *"Height default 60 is a guess — unit scale is recorded nowhere."*

and

> *"No public source documents Psychonauts' unit scale — the modding scene's published work is file
> formats (`.ppf`, `.plb`), not coordinate conventions."*

The plan — measure eye height live as `headpos` minus `playerpos`, in the engine's own units, needing
no conversion — is correct and is unaffected by anything below. But a measured number with **no
independent cross-check** is exactly how a wrong constant gets baked in and believed, so it was worth
one pass to see whether a public figure exists to check it against.

## The answer: no, and the search was aimed where a positive would have been

Checked this pass, all `[reported 2026-09-03]` unless noted:

- **PsychoPortal**, the format library — models geometry, skeletons, octrees, domains and entity
  placement, and defines no unit convention anywhere. Placement is bare `Vec3` position / rotation /
  scale with no stated units.
- **Jill Crungus's Psychonauts RE blog**, including the *Making Levels* post — this is the strongest
  negative of the set. That post is a detailed account of what it took to get **custom levels**
  working: importing models from external tools, rigging, animation, collision, the octree
  structures. **A guide to bringing outside geometry into the game is precisely where "one unit is
  about this big" would have to appear if anyone had needed it** — and it does not appear.
- **The Oatmeal `.plb` → glTF converter** and the Blender workflow built on it — no scale factor
  documented.
- The general modding tooling (**PsychonautsStudio**, **Psychonauts Explorer**) and a general web
  search for a units/character-height figure — nothing.

**Why this is a real negative and not a fetch artefact:** each fetch returned substantive content
that correctly described what the page *was* about (the *Making Levels* post came back with its real
subject matter — importing, rigging, animation, collision, octrees), so the pages were read rather
than truncated or shelled. This lane has been burned before by a truncated fetch reading as an
absence; this is not that. `[reported 2026-09-03]`

## The structural reason, which is the part worth keeping

The community's pipeline is a **round trip in native units**: `.plb` → glTF → Blender → glTF →
`.plb`, with rigging and animation surviving intact. A workflow shaped like that **never needs a
metres conversion at any point** — the model comes out in game units, gets edited in game units, and
goes back in game units. Nobody has published the scale because nobody in that community has ever had
a reason to establish it. `[hypothesis 2026-09-03 — this is inference from the shape of the
workflow, not a statement anyone made]`

So this is not an "it exists somewhere and we have not found it yet" gap that a future sweep might
close. **It is very likely absent by construction, and this lane should stop spending passes on it.**

## What that changes about taking the measurement

Since there will be no external figure to check the result against, the cross-check has to be built
into how the measurement is taken. Concretely, for the queued `headpos` run:

1. **Take it more than once, in more than one place.** A single sample on one patch of ground is
   `n=1` and cannot detect a bad reading. Two or three widely separated spots that agree is the
   cheapest available substitute for an external cross-check.
2. **The already-recorded ground hazard still governs.** The dossier is explicit that the parking lot
   is not level — Raz's Y moved `113.04 → 139.35 → 84.72` across one short walk. A height measured
   there is measuring the terrain, not Raz.
3. **Sanity-check the magnitude against something already known in engine units** rather than against
   metres — the project already has in-engine yardsticks on record (Raz sits ~11.6 world units from
   the eye; a respawn is ~26,000). An eye height that is not comfortably inside that ordering is
   wrong regardless of what any conversion would say.

That is the whole actionable content: the measurement stands alone, so make it `n>1` and take it on
ground that has been confirmed level.

## Sources

- https://gitlab.com/scrunguscrungus/psychoportal — the format library, checked for any unit convention (GPL-3.0 with a custom exception, Copyright (c) 2022 Jill Nesbit; read online via the GitLab REST API, nothing copied)
- https://jillcrungus.com/projects/psychonauts/blog/ and https://jillcrungus.com/projects/psychonauts/blog/2024/04/26/making-levels.html — the custom-levels write-up, the strongest place a scale statement would have appeared
- https://cohost.org/JillCrungus/post/5742197-psychonauts-custom-l — the same author's account of the custom-level pipeline and the `.plb` ↔ glTF round trip
- https://github.com/RayCarrot/PsychonautsStudio and https://quickandeasysoftware.net/readmes/PsychonautsExplorerHelp/psychonautsfiles.htm — community format tooling, checked for the same
