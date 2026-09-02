# The level format cannot express entity parenting — the other half of the `+0xB8` question

**Date:** 2026-09-03 · **From:** `/gr` (scoped single-project pass) · **For:** the modding lane to fold
into `ENGINE-DOSSIER.md` and delete.

**Bears on:** `ENGINE-DOSSIER.md` → *"The `+0xB8` parent caveat — narrowed hard, 2026-09-02"*, and the
`[hypothesis]` it ends on:

> **Can Raz ever get a non-null parent? No evidence found; not ruled out.** `[hypothesis]` A scan of
> `.text` for field writes to `+0xB8` found only the generic node cluster plus null-clears, and no
> player-specific parenting path — but the attach machinery is class-generic, so it *could* apply.

## The finding

That scan covered **code**. The other way an entity could acquire a parent is by being **authored**
as a child in the level file — and the `.plb` format has no way to say that.

From PsychoPortal's readers (GPL-3.0 w/ exception, Jill Nesbit; read online via the GitLab REST API,
nothing copied) `[reported 2026-09-03]`:

- **`Domain`** — the scene-graph container — has `Children` (nested domains), `Bounds`, `Meshes`,
  `EntityInitDatas`, `DomainEntityInfos`, `RuntimeReferences`. **No parent back-reference, and no
  transform of its own.**
- **`DomainEntityInfo`** — the per-entity placement record — has `Name`, `ScriptClass`, edit vars, and
  **`Position` / `Rotation` / `Scale` as bare `Vec3`s**. **No parent field, no attach-to-entity field,
  no attach-to-bone field.**
- **`EntityInitData`** carries only line-collision data, so it is not hiding one either.

So entities are placed **absolutely** within a domain, and no entity can arrive from the level file
already parented. **Any non-null `+0xB8` on any entity is therefore assigned at runtime, by code** —
exactly the surface the 2026-09-02 `.text` scan already swept. `[inferred-static 2026-09-03]`

## Suggested dossier change

In the `+0xB8` section, add that the data side has now been checked as well as the code side, and
that the two agree: no authored parenting exists in the format, so the hypothesis is narrowed to
runtime reparenting only. **Keep it as a narrowing, not a closure** — this is the on-disk format, not
the runtime class layout, and a gameplay system (lift, levitation ball, grab) could still reparent
through the generic machinery.

The practical consequence is about *where* the queued `headpos` reading is worth taking: a reading
standing still on flat ground is now close to a foregone conclusion. The informative states are **on
a moving platform, on the levitation ball, and while grabbed**, which is what the board already asks
for — this just says why those three matter more than the baseline reading does.

Full write-up, with the field tables:
`external-research/topics/2026-09-03-the-level-format-has-no-entity-parenting-so-any-parent-is-runtime-only.md`
