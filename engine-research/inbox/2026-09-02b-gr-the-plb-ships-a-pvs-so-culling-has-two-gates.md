# The `.plb` ships a precomputed visibility set — culling has two gates, and only one of them follows the camera matrix

Filed by: `/gr`, 2026-09-02
Topic: `external-research/topics/2026-09-02b-the-level-format-ships-a-precomputed-visibility-set-so-there-are-two-culling-gates.md`
Dossier sections: §8 (pass inventory), §11 (dead ends — the cull test), §9b/§12 (the void)
Partly supersedes: this lane's own `topics/2026-08-24-debug-menu-and-octree-culling.md` hypothesis that one octree serves both collision and visibility

`[reported 2026-09-02, from PsychoPortal's source]` / `[inferred-static 2026-09-02, n=1]`

- **The scene format has a `VisibilityTree` separate from the `CollisionTree` and the `NavMesh`.** It is an **octree plus one bit-buffer per leaf**, sized from `LeavesCount − 1` and packed into 32-bit words — i.e. **one bit per other leaf: a from-region PVS.** So the August guess that the cull test might *be* the collision-octree walk is unlikely; there are two trees.
- **Two gates in series:** the PVS keys on **which leaf the camera is in** (position, orientation-independent), and the frustum test keys on the **`camera+0x150` basis** (proved by the 2026-08-28 yaw sweep). A residual void that survives every rotation is the PVS gate's signature, not a transform bug.
- **First-person is probably unaffected** — moving the eye to Raz's head is a small translation that stays in the same leaf. **A flown free camera is affected** — outside the level, the leaf's visible set can be empty, blacking the screen for reasons unrelated to the transform.
- **Likely explains `pVisCache`** in `ECamera::BoxVisible(EBox3, void* pVisCache, bool)` (this morning's drop): a decoded current-leaf visible set cached between per-object calls. `[hypothesis]` — cheap check: log whether the pointer is stable while the camera is still.
- **Test that separates them:** fixed position, sweep yaw, record near-black (the 2026-08-28 method); then translate across a leaf boundary and repeat. Frustum culling varies smoothly with yaw; a PVS shows a step change with position and none with yaw.

Suggested dossier change: add the two-gate model to §9b/§11 and note that the visibility structure is precomputed level data, so the "cull test" that four sessions hunted is partly a **data lookup**, not only a function.
