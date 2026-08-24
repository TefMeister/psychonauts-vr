# 61 — Dormant debug menu located in our own exe; "Visibility Tree Culling" independently confirms the octree lead; live Sphere-Camera/Show-Collision test not reached this session

**Date:** 2026-08-24, dev machine. Follow-up to a parallel session's public-research pass
(modding-notes `59-public-research-sweep-debug-menu-and-octree-leads.md`): Lance McDonald's 2021
patch unlocks Psychonauts' dormant developer debug menu by repointing the Journal screen's pointers
to it (no code injection — real shipped dev code). Goal this session: find the same dormant code in
**our own** exe (no third-party binary, no tcrf.net fetch — that domain is currently serving fake
prompt-injection content to automated fetchers per the research note, correctly not followed),
repoint it ourselves, and use Sphere Camera / Show Collision to get a direct, cheap answer to
whether the void is camera-frustum-keyed or something else (octree/visibility-tree-keyed).

## Confirmed: the debug menu is real and fully present in our exe

Static string search (`grep -a` on the raw exe file) found every string the research note listed —
`Fly Camera`, `Sphere Camera`, `Show Collision` (x2), `Journal` (x84) — all present. Resolved their
file offsets to live virtual addresses by reading the PE section table directly out of the running
process (6 sections; `.rdata` raw-to-VA delta is `+0x1000` on this build) and verified live:
`0x717C24` = `"Fly Camera\0\0Enable/disable fly camera mode\0"`, `0x717BF0` = `"Sphere Camera\0Enable/disable sphere camera mode\0"` — byte-exact matches, confirmed against the running process's own memory.

**Found and fully decompiled the entire debug-menu construction function**, `sub_627590`
(`exe+0x627590`, thiscall, 1909 bytes) — this is the single function that builds the whole menu, item
by item. It calls `sub_656ce0("InGameMenu")` then `sub_656c50("DebugPage")` on itself (a "this"
pointer passed in via `ecx`, stored at `[this+0x130]`/`[this+0xE4]`/`[this+0xF4]` — presumably a
UI-manager/menu-system object, not yet traced to its own global source), then registers ~60 items via
a uniform `AddItem(name, desc, id)` pattern. **Every item from the research note's TCRF-sourced list
is present and intact**: Fly Camera, Sphere Camera, Drop Player, Show Trigger Vol, Collision
Wireframe, Collision Spheres, Show Nav Path, Show Decals, Debug Decals, Show Particles, Show
Dynamic Decals, Show Skel, Show Skel Only, Invul on/off, Rumble on/off, all the render toggles
(lights, ambient/diffuse/specular, reflections, glare, bumpmaps, detail maps), and a large "Debug
\*" visualization page (lights, normals, mesh frags, bumpmaps, shadows, glare, light ranges,
influences) — this is real, complete, unmodified developer code sitting inert in our copy exactly as
McDonald described.

## The big independent-confirmation find: "Visibility Tree Culling"

Two items in the decompiled list are the actual headline result of this session:

```c
sub_628e60("Visibility Tree Culling", "Enable/disable use of visibility tree for culling", 117);
sub_629410("Visibility Tree Culling", "Enable/disable use of visibility tree for culling", 117);
...
sub_628e60("Debug Visibility Tree", "Debug display of visibility tree nodes", 56);
sub_629410("Debug Visibility Tree", "Debug display of visibility tree nodes", 56);
```

**This independently confirms notes/59's octree-culling hypothesis directly from the game's own
shipped developer terminology** — not "plausibly the same structure," but a named, toggleable
subsystem the original developers explicitly called "Visibility Tree" and explicitly used *for
culling*, with a dedicated node-visualization debug view alongside it. This is strong, concrete
support for the idea that four (now five) sessions centered on camera-matrix code never found an
isolated frustum-plane test because there isn't one — visibility is decided by walking this tree
(very likely the same PLB-format octree Jill Crungus documented on the collision side, per notes/59
§3), not by a standalone 6-plane comparison.

## What's still missing: the live experiment

Traced one level further into the item-registration helpers (`sub_628e60`/`sub_629410`/`sub_629370`,
all decompiled) to find where the numeric ID (117 for Visibility Tree Culling) gets consumed —
confirmed the item object stores it at `field_4` (`a0->field_4 = a3` inside `sub_629410`), and that a
further function, `sub_629490` (called from both `sub_629410` and `sub_629370`, not yet decompiled),
is the likely click/toggle handler that would read this ID to know which actual global flag to flip.
**Ran out of session time before reaching that global flag, or the menu-system object's own source
(needed either for McDonald's pointer-repoint technique, or for a simpler "just call
`SelectMenu`/`SelectPage` directly" shortcut this session was exploring as a lower-risk alternative
to raw pointer patching).** No memory writes were made this session — everything above came from
read-only live memory inspection and static/decompiled analysis; **the exe and the running process
were never patched**, so there is nothing to revert.

## Recommendation for next session

**Two viable next steps, in order of promise:**

1. **Decompile `sub_629490`** (the shared toggle/select handler) to find how item ID 117 maps to an
   actual flag — if it's a direct index into a global bool/flag array (plausible, given the uniform
   small-integer IDs across every item), a hotkey could flip `g_debugFlags[117]` (or whatever the
   real storage turns out to be) **directly, with zero menu UI needed at all** — sidesteps both the
   menu-activation-trigger problem below AND the still-unresolved window-focus blocker from notes/60
   that would otherwise block in-menu navigation anyway. This is probably the fastest remaining path
   to the actual disappears-vs-persists answer.
2. If that doesn't pan out, find `sub_627590`'s own caller (angr's static call-graph analysis found
   none — the call site is likely indirect/vtable-dispatched, needs a live EBP-chain capture instead,
   the technique used successfully throughout notes/59-60) to identify the menu-system object's real
   source, then either call `SelectMenu("InGameMenu")`/`SelectPage("DebugPage")` on it directly from a
   hotkey (untested whether re-selection alone makes an already-open menu system display it), or fall
   back to McDonald's original repoint technique once the Journal's equivalent pointer is located the
   same way.

Either path still needs `SetPendingLevel` (notes/60's F12 hotkey, already proven working) to reach
real gameplay first, and may still need notes/60's window-focus problem solved if genuine mouse/menu
navigation turns out to be required rather than a direct flag flip.

🤖 Live x64dbg (attach, PE-header read, live memory verification of string addresses) + `x64dbg-skills:decompile`
(angr) for `sub_627590` and the item-registration helpers. Zero memory writes, zero file writes to
the exe or game install; nothing to back up or revert this session.
