# PVS leaf size is not in the format — but two numbers inside any `.plb` would bound it, with no launch

**Status:** 🆕 new · **Priority:** medium — it turns yesterday's *"first person is probably safe from
the PVS gate"* from a bare `[hypothesis]` into a question with a named, static, cheap answer.

## The claim this is aimed at

Yesterday's topic, `2026-09-02b-the-level-format-ships-a-precomputed-visibility-set-…`, concluded:

> **First-person is probably safe from it.** Moving the eye from the chase camera to Raz's head is a
> translation of a few tens of units; that stays inside the same leaf almost always, so the PVS
> result is unchanged.

That reasoning is only as good as an assumption nobody had checked: **that octree leaves are large
compared with a head offset.** If leaves are room-sized it is right; if they are fine-grained, moving
the eye from a chase camera to Raz's head could cross a leaf boundary and change the visible set —
which would show up as geometry popping when first person engages, and would be very easy to
misdiagnose as a transform bug.

## What the format says about leaf size

Nothing directly, and the shape of that "nothing" is the useful part. From PsychoPortal's readers,
online via the GitLab REST API `[reported 2026-09-03, from the library's own source]`:

**`Octree`** holds `SSECube` (the tree's overall bounding cube), `LeavesCount` (int), `NodesCount`
(int), `Nodes[]`, `Leaves[]` and a primitive index array.

**`OctreeNode`** is eight packed `UInt24` entries — one per octant — with a leaf marker bit
(`IsLeafFlag = 0x00400000`) distinguishing "this child is a leaf" from "this child is another node".

**`OctreeLeaf`** stores only a packed `PrimitiveIndex` and `PrimitiveCount`. **A leaf does not store
its own bounds.**

So this is a **regular octree**: one root cube, subdivided eight ways, and a leaf's extent is implied
entirely by how deep it sits — `leaf extent = SSECube extent / 2^depth`. There is no max-depth
constant and no subdivision threshold anywhere in the file, because the subdivision was decided at
build time by Double Fine's own tooling and only the *result* is serialised.
`[inferred-static 2026-09-03]`

## The consequence: it is answerable, statically, per level

Leaf size is not a constant of the engine — it is a property of each level's baked tree. But two
numbers that **are** in every `.plb` bound it immediately:

1. **the root `SSECube`'s extent** — how much world the whole tree spans, and
2. **`LeavesCount`** — how many leaves that space was cut into.

The mean leaf volume is the first divided by the second, and for a regular octree the mean depth
follows from `LeavesCount` (a fully-subdivided depth *d* gives 8^*d* leaves, so *d* ≈ log₈ of the
count). Against Raz's chase-camera-to-head offset — a known quantity, roughly the ~11.6 world units
the dossier already records between the eye and Raz, and whatever `fpheight` settles at — that says
directly whether an eye move can plausibly cross a boundary.

**This needs no headset, no launch, and no debugger** — it is a read of a file already on disk in the
game install. It is the same class of work as the `.rdata` bone-name reads: a static answer to a
question that has been sitting as a guess.

⚠️ **One honest caveat about the numbers.** Both live in the `.plb`, whose layout is documented
publicly and implemented by a GPL-3.0 library we may study but not copy. Reading them means either
parsing those two fields with our own code, or using a third-party tool as a **tool** rather than as
source. Which of those is appropriate is the modding lane's call, not this lane's — flagged rather
than assumed.

## What each outcome would mean

| Reading | Meaning for first person |
| --- | --- |
| Few hundred leaves over a whole level | Leaves are room-scale; the "FP is safe" reasoning holds, and the PVS gate can be set aside for FP work |
| Many thousands over a small level | Leaves may be smaller than the eye offset; an FP camera could change leaf, and geometry popping on FP engage would be a **PVS** symptom, not a matrix bug |

Either way the free-camera warning from yesterday is unaffected — flying `cammove` far outside the
level can land in a leaf with an empty or wrong visible set regardless of how coarse the tree is.

## Sources

- https://gitlab.com/scrunguscrungus/psychoportal — `PsychoPortal/Psychonauts/Common/Octree/{Octree,OctreeNode,OctreeLeaf}.cs`, `PsychoPortal/Psychonauts/Common/SSECube.cs`, and `Packs/MeshPack/Scene/VisibilityTree/` (read online via the GitLab REST API; nothing copied). **Licence: GPL-3.0 with a custom exception, Copyright (c) 2022 Jill Nesbit.**
- https://jillcrungus.com/projects/psychonauts/blog/2024/04/26/making-levels.html — the same author's account of the custom-level work, which covers the octree structures among the systems that had to be understood.

## ✅ Outcome 2026-09-03 — folded, now the board's `[PD]` row; ⚠️ but the cost estimate was wrong (from `inbox/`)

The reasoning was accepted: "FP is probably unaffected" is downgraded to `[hypothesis]` with the two
named numbers as its test. **But "a read of a file already in the game install" was wrong**
`[measured 2026-09-03]`:

- There are **zero loose `.plb` files** anywhere in the Psychonauts install.
- Levels ship as **`PPAK` containers** — `WorkResource/PCLevelPackFiles/*.ppf`, 50 of them (beside 50
  `.apf`), up to 33 MB, header magic `50 50 41 4B`, interleaving asset paths with compressed data. A
  200 KB scan of one found no `.plb` name at all.

So the real shape is *open the PPAK container → locate the level binary inside → walk to the Octree →
read two fields*. Still `[PD]`, still no game needed, but the modding side recorded it as a
container-format job so nobody picks it up expecting five minutes. The licensing flag carried across:
own parsing code, or a third-party tool used *as a tool*.

**That cost has since come down — see the 2026-09-03b topic:** the Steam-era `.ppf` is already
opened by two public tools, one of them explicitly written for the re-release format.
