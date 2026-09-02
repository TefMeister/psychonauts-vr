# "First person is probably safe from the PVS gate" is checkable statically — two numbers in any `.plb`

**Date:** 2026-09-03 · **From:** `/gr` (scoped single-project pass) · **For:** the modding lane to fold
into `ENGINE-DOSSIER.md` and delete.

**Bears on:** `ENGINE-DOSSIER.md` §11 → the `/gr`-folded culling block, specifically this bullet:

> **Bears directly on first person vs. the free camera:** moving the eye to Raz's head is a small
> translation that stays inside the same leaf, so **FP is probably unaffected by the PVS gate**.

## Why that deserves a check

The reasoning rests on an unstated assumption — **that leaves are large compared with the eye
offset**. If they are room-sized it holds. If the tree is fine-grained, moving the eye from the chase
camera to Raz's head could cross a leaf boundary and change the visible set, which would present as
geometry popping the moment first person engages — and would be very easy to misdiagnose as a
transform bug, which is the exact failure mode this project has hit before.

## What the format shows

From PsychoPortal's octree readers `[reported 2026-09-03]`:

- **`Octree`**: `SSECube` (overall bounding cube), `LeavesCount`, `NodesCount`, `Nodes[]`, `Leaves[]`,
  primitive indices.
- **`OctreeNode`**: eight packed `UInt24` entries, one per octant, with `IsLeafFlag = 0x00400000`
  marking a child as a leaf.
- **`OctreeLeaf`**: a packed `PrimitiveIndex` + `PrimitiveCount` only — **a leaf does not store its
  own bounds.**

So it is a **regular octree**: leaf extent is implied by depth, `SSECube extent / 2^depth`. There is
no max-depth or subdivision-threshold constant in the file, because subdivision was decided by Double
Fine's build tooling and only the result is serialised. `[inferred-static 2026-09-03]`

## Suggested dossier change

Downgrade the "FP is probably unaffected" line from an assertion to a `[hypothesis]` **with a named
static test**, rather than leaving it reading as settled. The test: read the root `SSECube` extent and
`LeavesCount` from one real level's `.plb`. Mean leaf volume is the first over the second; for a
regular octree the mean depth follows from the leaf count (8^*d* leaves at depth *d*). Compare the
resulting leaf scale against the eye-to-Raz offset the dossier already records (~11.6 world units)
plus whatever `fpheight` settles at.

**Few hundred leaves per level** ⇒ room-scale, the FP reasoning holds and the PVS gate can be set
aside for FP work. **Many thousands over a small level** ⇒ an FP camera may change leaf, and popping
on FP engage is a PVS symptom rather than a matrix bug.

⚠️ **A judgement call that belongs to your lane, not mine:** those two fields sit in a `.plb` whose
format is implemented by a GPL-3.0 library we may study but not copy. Reading them means either
parsing the two fields with our own code, or using a third-party tool as a *tool*. Flagging rather
than assuming which is appropriate here.

This is a **no-launch, no-headset** item — a read of a file already in the game install — so if it is
taken up it belongs in the PD queue, not the FLAT one.

Full write-up:
`external-research/topics/2026-09-03-pvs-leaf-size-is-absent-from-the-format-but-two-numbers-in-any-plb-would-bound-it.md`
