# The level format has no entity parenting — so a non-null `+0xB8` on Raz could only ever be assigned at runtime

**Status:** 🆕 new · **Priority:** ⭐ high — it removes one whole class of the `+0xB8` worry without a
launch, and it narrows what the queued `headpos` reading actually has to catch.

## The open item this is aimed at

`ENGINE-DOSSIER.md` → *"The `+0xB8` parent caveat — narrowed hard, 2026-09-02"* closes with the one
thing static work could not settle:

> **Can Raz ever get a non-null parent? No evidence found; not ruled out.** `[hypothesis]` A scan of
> `.text` for field writes to `+0xB8` found only the generic node cluster plus null-clears, and no
> player-specific parenting path — but the attach machinery is class-generic, so it *could* apply.

That scan covered the **code**. It could not cover the other way an entity might acquire a parent:
being **authored** as somebody's child in the level file itself. This checks that half.

## What the level format actually stores

**PsychoPortal** (Jill Nesbit / `scrunguscrungus`), the .NET library that reads and writes
Psychonauts' formats, models a `.plb` scene as a tree of **`Domain`** objects. Read online through
the GitLab REST API. `[reported 2026-09-03, from the library's own source]`

`Domain` carries:

| Field | Type | Note |
| --- | --- | --- |
| `Name` | `String4b` | |
| `Bounds` | `Box3` | an AABB for the domain |
| `Meshes` | `Mesh[]` | |
| `EntityInitDatas` | `EntityInitData[]` | |
| `DomainEntityInfos` | `DomainEntityInfo[]` | the placed entities |
| `RuntimeReferences` | `String4b[]` | |
| `Children` | `Domain[]` | nested domains |

**A `Domain` has `Children` but no parent back-reference, and no transform of its own** — no matrix,
no position, no rotation.

And the per-entity placement record, `DomainEntityInfo`, carries:

| Field | Type |
| --- | --- |
| `Name` | `String4b` |
| `ScriptClass` | `String4b` |
| `HasEditVars` / `EditVars` | `int` / `String4b` |
| **`Position`** | `Vec3` |
| **`Rotation`** | `Vec3` |
| **`Scale`** | `Vec3` |
| `UnknownUint`, `NewInt1`, `NewInt2` | `uint` / `int` |

**There is no parent field, no attach-to-entity field, and no attach-to-bone field on a placed
entity.** `[reported 2026-09-03, from the format library's source]` `EntityInitData` — the sibling
record — turns out to carry only line-collision data, so it is not hiding one either.

## What that does and does not settle

**Does:** entities are authored with an **absolute** position/rotation/scale inside a domain, and the
file format has no way to express "entity A is a child of entity B". So there is no such thing as an
entity that arrives from the level file already parented. Any non-null `+0xB8` on any entity is
therefore **assigned at runtime, by code** — which is precisely the surface the 2026-09-02 `.text`
scan already covered, and which found only the generic node cluster, null-clears, and one attach path
running in the *opposite* direction (`AttachInventoryEntityToPlayer` gives the **item** Raz as its
parent).

So the two halves now meet: the code scan found no player-parenting path, and the data cannot supply
one either. `[inferred-static 2026-09-03]`

**Does not:** this is the **on-disk format**, not the runtime class layout. `Domain` is not the
runtime node at `node+0x10`, and nothing here names `+0xB8` or proves what the runtime scene-graph
triple does. A gameplay system could still reparent Raz at runtime — a lift, the levitation ball, a
grab — through code that the `.text` scan classified as generic rather than player-specific. **This
narrows the hypothesis; it does not retire it.**

## What it changes about the queued test

The dossier's plan stands unchanged and is still the thing that closes this:

> **Settled by one live reading, now wired:** the `headpos` command prints `node+0xB8` beside
> `playerpos`. Zero while riding a moving platform, the levitation ball, or while grabbed ⇒ the
> caveat is empirically dead.

What changes is the **prior**. Two independent lines of static evidence now point the same way, so a
non-zero reading would be the surprise rather than the coin-flip. The practical consequence: the
reading is worth taking specifically in the states where runtime reparenting is plausible — **on a
moving platform, on the levitation ball, and while grabbed** — because a reading taken standing still
on flat ground is now close to a foregone conclusion and proves the least.

## Sources

- https://gitlab.com/scrunguscrungus/psychoportal — `PsychoPortal/Psychonauts/Packs/MeshPack/Scene/Domain/{Domain.cs, DomainEntityInfo/DomainEntityInfo.cs, EntityInitData/EntityInitData.cs}` (read online via the GitLab REST API; nothing copied). **Licence: GPL-3.0 with a custom exception, Copyright (c) 2022 Jill Nesbit** — study-only under this project's rules and under its own terms.
- https://jillcrungus.com/projects/psychonauts/blog/2024/04/26/making-levels.html — the same author's write-up of the scene/level work the library implements.

## ✅ Outcome 2026-09-03 — folded into the dossier's `+0xB8` section as a narrowing, not a closure (from `inbox/`)

Exactly as asked. The useful part, per the modding side, is that the data side now agrees with the
code side; the practical consequence is recorded on the board: the baseline `headpos` reading on flat
ground is close to a foregone conclusion, so the informative states are **on a moving platform, on
the levitation ball, and while grabbed**. Nothing further for research here.
