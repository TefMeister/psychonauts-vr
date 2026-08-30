# 66 — Positive-control hunt settled: the debug menu's visual toggles are broadly non-functional on this PC build

**Date:** 2026-08-24, dev PC. Continuation of notes/59-65's void investigation, specifically
notes/65's open question: is "Collision Wireframe"'s negative void result trustworthy, given its own
sanity check (definitely-present nearby geometry) also showed nothing?

## What this session did

1. **Fresh full decompile of `sub_627590`** (angr, this time capturing the ENTIRE debug-menu
   registration function cleanly, 147 lines) — this settles a standing discrepancy: **"Show
   Collision" is NOT a real registered menu item anywhere in this exe.** No such string/id exists.
   TCRF's page (or the search-snippet reconstruction of it in notes/59) was simply wrong about this
   game's actual PC item list, or describes a different platform's build. The real, closest
   equivalent visual-collision-debug item is **"Collision Spheres" (id 0x11=17, "Display collision
   spheres")**, registered via the exact same `sub_629410` generic-ID checkbox path as items
   117/21/22 (all already proven direct-byte-toggleable in prior sessions).
2. **Wired NUMPAD6** as a fifth hotkey toggle for Collision Spheres (`PSYVR_COLLISION_SPHERES_TOGGLE_KEY=1`),
   same pattern as NUMPAD7/8/9. Built, deployed (`d3d9.dll.pre-notes66-backup` kept).
3. **Fixed a test-setup mistake from earlier in this same session**: forgot `PSYVR_LEVEL_JUMP_CODE=CABH`
   on the first two launches (defaults to `CAJA`/Sasha's Lab) and `PSYVR_FAKE_POSE_YAW_DEG=95` on the
   first (defaults to 25.2°, nowhere near enough to swing behind-facing) — both caught and corrected
   before any test screenshots were taken, no wasted analysis.
4. **Matched-phase A/B, Collision Spheres OFF vs ON, at the same void-facing phase** (`fwd.z≈+0.99`,
   i.e. facing ~180° from spawn orientation) — screenshots `01`/`02` in this note's asset folder.
   **Void region identical, no spheres, no visible change of any kind.**
5. **Positive-control check, take 2**: pointed the camera at DEFINITELY-PRESENT geometry with
   Collision Spheres still on — Raz's own body, trees, rocks, a wooden fence, a rope bridge
   (screenshot `03`). **Zero visible sphere overlays anywhere**, despite the flag write confirmed
   landing correctly (`engine+61`, log-verified).
6. **Checked whether a master gate was the real blocker**: "Allow Debug Display" (id 143,
   *"Enable/disable all debug display"*, registered first in the whole menu) — live-read via x64dbg,
   **already 1 (on) by default.** Not the missing piece.
7. **One more, structurally different debug item as a last positive-control attempt**: "Show Skel"
   (id 23, *"Draw skeleton"*) — a simple standalone skeleton-line debug draw, a completely different
   rendering code path from the material/wireframe-pipeline-dependent Collision Wireframe/Spheres.
   Live-flipped via a direct x64dbg memory write (no rebuild needed, same `engine+44+id` formula).
   Captured Raz in full clear view, standing on open sand (screenshot `04`). **Zero skeleton lines
   visible anywhere on his body.**

## The conclusion

**Four different debug-menu visual toggles now — Render Wireframe (notes/64), Collision Wireframe
(notes/65), Collision Spheres, and Show Skel (this session) — all confirmed writing to the correct
memory address, and all four show ZERO visible effect in this PC build**, tested against both the
void and definitely-present, clearly-visible geometry (Raz's own body, terrain, trees, fence,
bridge). This is no longer plausibly "we keep picking the wrong specific item" — it's convergent
evidence that **the debug menu's entire visual-overlay/display rendering system is non-functional on
this PC port**, exactly matching TCRF's own blanket caveat ("a large amount of the game's rendering
code is stubbed and non-functional [on PC]") — just confirmed now against four independent items
spanning two structurally different rendering code paths (material/wireframe-pipeline draws AND
standalone primitive-line draws), not one item's quirk.

**Practical implication: this whole avenue (unlock the dormant debug menu, use its visual toggles to
inspect the void) is exhausted.** Further items from the same menu (Show Trigger Vol, Show Nav Path,
Show Particles, Debug Normals, etc.) are very unlikely to behave differently — not worth spending a
future session cycling through more of them expecting a different result.

**What DOES still work from this menu**: functional/behavioral toggles that change actual game logic
rather than draw an overlay — "Visibility Tree Culling" (notes/62/63) is proven to actually gate real
culling behavior (even though disabling it didn't change the void, the flag write demonstrably DOES
affect something, unlike the four display toggles above which affect nothing visible at all). Worth
keeping this distinction in mind for any future item from this menu: behavioral flags are plausible,
display/draw flags are not.

## Where this leaves the void hunt

Six-plus sessions in, the debug-menu detour (started notes/59, based on real public research that
turned out to be based on an inaccurate secondary source for this specific item) is now closed out
with a clear, honest answer rather than an open thread. The two paths that remain live:

1. **The original cull-mechanism hunt** (paused at notes/58-61: still hasn't found the actual
   CPU-side visibility decision point, `DrawIndexedPrimitive`-side tracing never fully converged with
   the camera-update side).
2. **The real-headset test on the home PC** — unaffected by any of this PC build's debug-rendering
   limitations, and remains the single most decisive test available: reproducing the bug there is
   trivial (just look behind), with no dev-PC-specific rendering-stub complications to work around at
   all.

Given how much dev-PC effort the debug-menu and cull-hunt threads have both now consumed without a
confirmed root cause, **the home-PC in-headset test is the strongest next move** — it sidesteps every
constraint this session's findings just confirmed (broken debug-menu rendering, no found cull test)
entirely, using the real target hardware instead.

## Cleanup

Collision Spheres flag toggled back to 0 (log-confirmed) before process kill. Show Skel's flag was
set via a direct one-off memory write (not through a persisted code path) and needed no explicit
reset — it lived only in that now-terminated process's memory. `d3d9.dll`/`openvr_api.dll` (with the
new NUMPAD6 toggle) remain deployed in the game directory; previous build backed up as
`d3d9.dll.pre-notes66-backup`. No save files touched (only F12/`SetPendingLevel` used for level
access, no menu/Journal navigation at any point). Debugger session terminated cleanly, no stray
processes.
