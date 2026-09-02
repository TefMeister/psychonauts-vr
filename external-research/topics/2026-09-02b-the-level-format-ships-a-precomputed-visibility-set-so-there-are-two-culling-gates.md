# The level format ships a precomputed visibility set — so there are two culling gates, not one

**Status:** 🆕 new · **Priority:** ⭐ high — it changes the model of the black-void problem, refines a
hypothesis this lane published in August, and explains the `pVisCache` argument on the camera test
found earlier today.

## What was found

**PsychoPortal** (Jill / `scrunguscrungus`), the .NET library that reads and writes Psychonauts'
formats and underpins the Oatmeal level converter, models the `.plb` scene as containing a
**`Scene/VisibilityTree`** — a structure entirely separate from `Scene/Domain/Mesh/CollisionTree` and
from `Scene/NavMesh`. Read online through the GitLab REST API. `[reported 2026-09-02, from the
library's own source]`

Its shape, from the class that reads it:

| Field | Type | What it is |
| --- | --- | --- |
| `Octree` | `Octree` | the spatial partition — eight children per node, leaves indexed |
| `LeafFlags` | `VisibilityTreeLeafFlags[]` | **one entry per octree leaf** |
| `Pre_Version` | `uint` | version handed to the octree reader |

and each leaf's flags hold a bit buffer whose length is derived from **`Octree.LeavesCount − 1`**,
packed into 32-bit words (`(bufferLength >> 5) + 1` of them).

**One bit per other leaf, stored per leaf, is a potentially-visible-set.** This is the classic
from-region precomputed visibility scheme (the same idea as Quake's PVS): for the leaf you are
standing in, the bitset says which other leaves can possibly be seen, and everything else is skipped
before any frustum test happens. `[inferred-static 2026-09-02, n=1 — from the field layout, not from
running the game]`

## ⚠️ This refines a hypothesis this lane published on 2026-08-24

`topics/2026-08-24-debug-menu-and-octree-culling.md` reasoned:

> games of this era very commonly reuse a single octree for both collision AND frustum/visibility
> culling … rather than maintaining two separate spatial structures

**Psychonauts keeps two.** The collision octree Jill documented and the visibility octree are
different structures in the same file, with different leaf payloads (collision primitives against
visibility bitsets). The suggestion that the cull test might turn out to *be* the collision-octree
walk is therefore unlikely — they are separate trees. The rest of that topic stands.

## Why this matters for the void, and what it predicts

The project has one confirmed culling fact: rotating the camera's real basis at `camera+0x150`
changes what is culled, and **turning 90° rendered *less* near-black than not turning**
(`[measured 2026-08-28]`). That is frustum culling following the matrix.

A PVS is a **second, independent gate, and it keys on position rather than orientation.** So the
model to carry forward is two gates in series:

| Gate | Keyed on | Changes when you rotate in place? |
| --- | --- | --- |
| PVS (this finding) | which octree **leaf the camera is in** | **no** |
| frustum test | the camera **basis/matrix** | yes |

Three consequences worth having before the next void session:

1. **It bounds the 2026-08-28 result.** Rotating improved the void because it moved the frustum, and
   it could only ever reveal geometry the PVS had already allowed. A residual void that will not go
   away under any rotation is the signature of the PVS gate, not a frustum bug.
2. **First-person is probably safe from it.** Moving the eye from the chase camera to Raz's head is a
   translation of a few tens of units; that stays inside the same leaf almost always, so the PVS
   result is unchanged. This is reassuring for the `headpos`/`fpheight` work rather than a new risk.
3. **A free camera is not safe from it.** Flying outside the level or into geometry can put the
   camera in a leaf whose visible set is empty or wrong, which would black the screen for a reason
   that has nothing to do with the transform. Worth remembering when `cammove` is used to explore.

## It also explains today's other finding

Earlier today this lane recorded `ECamera::BoxVisible(EBox3 box, void* pVisCache, bool)` from
Astralathe's signatures — the per-object visibility test, with a **`pVisCache`** argument nobody
could account for. A precomputed visibility set is exactly the thing a "visibility cache" would hold:
the resolved leaf, or the current leaf's decoded bitset, carried between calls so it is computed once
per camera position rather than per object. `[hypothesis]` That is a coherent fit, not a confirmed
one, and it is cheap to check — log the pointer's value across a frame and see whether it is constant
while the camera is still.

## The test that separates the two gates

At one spot, with `camhold 1`: rotate through several yaws and record the near-black fraction (the
2026-08-28 method). Then **translate** the camera far enough to cross a leaf boundary and repeat.
Frustum-only culling gives smooth variation with yaw and little with small translations; a PVS gate
shows as a **step change** at some position, with yaw making no difference to that component.

## Sources

- https://gitlab.com/scrunguscrungus/psychoportal — `PsychoPortal/Psychonauts/Packs/MeshPack/Scene/VisibilityTree/{VisibilityTree,VisibilityTreeLeafFlags}.cs`, and the sibling `CollisionTree/` and `NavMesh/` folders (read online via the GitLab REST API; nothing copied)
- https://jillcrungus.com/projects/psychonauts/blog/2024/04/26/making-levels.html — the PLB/scene overview by the same author
