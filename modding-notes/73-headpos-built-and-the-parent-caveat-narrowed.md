# notes/73 — `headpos` built and deployed (home PC), the `+0xB8` parent caveat narrowed, `/gr` inbox drained

`/pd`, home PC, 2026-09-02. **The game was not launched, no debugger was attached, and nothing in
this note has been run.** Everything below is static analysis of `Psychonauts.exe` on disk plus a
compile.

Picked up both `[PD]` items on the board.

---

## 1. `headpos` — the measured eye height (built, deployed, NOT run)

### Why it exists

The board wanted eye height for free as camera-minus-player, and notes/71 disproved that: the camera
position sits **+327.8** in Y and the render eye ~**149**, but both are the height of a *third-person*
camera, not an eye. Recording either would have baked a wrong constant in at exactly the spot where
`fpheight`'s default of 60 is already flagged as a guess. The head bone gives the number with no
third-person camera in the path and no unit-scale conversion:
**`headWorldPos − playerpos` IS the eye height, in the engine's own units.**

### What the binding actually does

`GetBoneWorldPosition`'s shim is `0x005B1690`. Stripped of Lua marshalling it is four engine calls,
and it returns **six** values — position *and* euler:

```
bone     = FindBoneByName(owner, "headJA_1")       0x00438B70   __thiscall, ret 4
           GetBoneMatrix(bone, boneMtx[16])        0x00492B70   __thiscall, ret 4
ownerMtx = GetOwnerWorldMatrix(owner)              0x00438390   __thiscall, ret 0
           MatMul(ownerMtx, worldMtx[16], boneMtx) 0x00433E50   __thiscall, ret 8
```

`[inferred-static 2026-09-02]` **Every `ret` form was read off the binary**, so the four `__thiscall`
signatures are checked rather than assumed — a stack-convention mismatch here would corrupt the
render thread's stack rather than fail cleanly. `0x00433E50` is confirmed a 4×4 multiply from its
float body; `0x00438B70` iterates `count = (owner[0x54] >> 5) & 0x7FFFFFF` over an array at
`owner[0x5C]`, comparing names via `0x00492C10`.

Translation is **floats 12..14** (row 3 of a row-major 4×4), corroborated twice independently in this
engine — the node transform (`+0x40` = row 3 of the 4×4 at `node+0x10`) and the camera world matrix
(rows `0x90`/`0xA0`/`0xB0`, translation `0xC0`).

The bone name is passed as **the exe's own string constant at `0x00703F94`**, not a literal of ours,
so the comparison sees exactly the bytes the engine shipped. Citation check, both re-read from
`.rdata` this pass: `0x00703F94` → `headJA_1`, `0x0071195C` → `handJEndLf_2`.

### ⚠️ The thing I could NOT settle statically — and what I did about it

**Which object owns the bone array is genuinely ambiguous in the binary:**

- the Lua binding gets its owner from `0x005B01E0`, which returns `luaEntityWrapper->[0xA8]`;
- the attach path uses `targetEntity->[0x10]` — the same node our position chain already walks
  (`0x00421DD0` passes `[entity+0x10]`).

Two different accessors, and I could not prove which applies to the object at `engine+0x818C`.

Rather than guess and risk a crash, `headpos` **shape-checks four candidates with pure reads before
calling anything into the engine** — `player+0x10`, `player`, `player+0xA8`, `player+0x10+0xA8` —
requiring a sane bone count at `+0x54` and a sane array pointer at `+0x5C`, exactly what
`0x00438B70` will do with it. All the guessing is therefore read-only; the engine calls only ever
run against a plausible object, and **the command reports which candidate matched**, so one run
settles it. Each stage also logs *before* it calls, so if the game does die the log names the exact
stage instead of leaving a mystery.

### Also wired, for free: the parent pointer

`headpos` prints `node+0xB8` beside the position (see §2 for why that is the interesting number).

### Status

`[compile-verified 2026-09-02]` — built with llvm-mingw i686, 211,456 bytes. Export surface checked
against the previously committed build: **2/2 identical** (`Direct3DCreate9`, `Direct3DCreate9@4`),
so the change is purely additive. The five new log strings were confirmed present in the built
binary, so the build is not a silent no-op.

---

## 2. The `+0xB8` parent caveat — narrowed hard, not dissolved

This was the last unresolved thing about `playerpos`.

- **`+0xB8` IS the parent pointer** `[inferred-static 2026-09-02, n=3 independent uses]`, upgraded
  from n=1. Two new uses beyond the setter branch already on record: `0x00466609–0x0046665C`
  transfers `[src+0xB8]` → `[dst+0xB8]`, clears the source, then walks a `+0xC0`/`+0xBC` chain; and
  `0x00466E9B` sets `[node+0xB8]`, sets `[node+0xBC]` from `[other+0xC0]`, then writes
  `[[node->0xB8]+0x40]+0xC0] = node` — head-insertion into a parent's child list. The triple reads as
  **`+0xB8` parent · `+0xBC` next sibling · `+0xC0` first child**.
  **Unresolved and said plainly:** at `0x0046F21A` the setter `rep movsd`s 16 dwords *directly from*
  `[this+0xB8]`, which type-checks as "pointer to a 4×4", while the child-list code dereferences
  `[parent+0x40]`. Those reconcile only if different classes share the offset. Not settled.

- **⭐ The decisive finding: the read side never composes the parent chain.**
  `[inferred-static 2026-09-02, n=2]` `GetAbsPosition_impl` (`0x005C0C70`) and
  `GetPlayerPosition_impl` (`0x005C1CE0`) contain **zero** references to `+0xB8`. The engine's own
  "absolute position" accessor ignores the parent link entirely — getter and setter genuinely
  disagree. **Every script-facing position read in this engine already lives with this**, so
  `playerpos` is exactly as correct as the engine's own `GetAbsPosition` and no more wrong.

- **Can Raz get a non-null parent? No evidence found, not ruled out** `[hypothesis]`. A `.text` scan
  for field writes to `+0xB8` found only the generic node cluster plus null-clears — no
  player-specific parenting path — but the attach machinery is class-generic. Note the *direction* of
  the one path decoded end to end: `AttachInventoryEntityToPlayer` → `0x00421DD0` → `0x00466DE0`
  reparents **the inventory item, giving it Raz as parent**; Raz does not acquire one.
  **Two false positives worth recording so nobody re-cites them:** `0x0050A36B` writes a *texture*
  pointer at `+0xB8` (string: `workresource/textures/phatline.tga`), and `0x00440EDC` sets
  `+0xB0`/`+0xB4`/`+0xB8` together — both different classes sharing the offset.

**Verdict: the caveat does not fully dissolve, but it stops being load-bearing.** One live reading
settles it, and it is now wired into `headpos`.

---

## 3. Bonus corroboration found while decoding: `engine+0x818C` is at n=5

`AttachInventoryEntityToPlayer`'s impl **defaults its target entity to
`*(*(0x0078BC20) + 0x818C)`** at `0x005B0DC9` when the optional argument is omitted — the engine's
own idea of "the player" is our chain's first two steps. `[inferred-static 2026-09-02]` Combined with
`/gr`'s independent report of Astralathe naming `GameApp::pPlayer` at byte 33164 = `0x818C`, the
offset is now at n=5 with two derivations independent of us.

---

## 4. `/gr` inbox drained (2 files) — one of them closes a four-session hunt

Folded into `ENGINE-DOSSIER.md` §9/§10/§11/§12, then deleted:

- **The `lua_State` no longer needs a runtime capture** — and I confirmed the load-bearing half in
  *our own* binary rather than taking it on trust: `0x005B0E0E` does literally
  `mov eax, [0x78BC20]; add eax, 0x9A34`, our singleton at that exact offset, inside a function
  decoded independently for the bone work. `/gr` predicted the check would show up in
  `SetTableValue`; it showed up somewhere else, which is stronger than the predicted result because
  it was not where we went looking. **Still `[reported]`, and it is the half that would crash if
  wrong: that `EScriptVM+8` is the `lua_State*`.** Read and print it before passing it anywhere.
- **⭐ Culling has TWO gates, and the cull test has a name.** `ECamera::BoxVisible(EBox3, void*
  pVisCache, bool)` is the per-object decision, so four sessions of "we cannot find the cull test"
  closes *as a location*. And the `.plb` ships a **`VisibilityTree` separate from the
  `CollisionTree`** — an octree plus one bit per other leaf, i.e. a **from-region PVS**. So the PVS
  gate keys on *which leaf the camera is in* (position, orientation-independent) while the frustum
  gate keys on the `camera+0x150` basis. This **partly supersedes** the August guess that one octree
  served both.
  - **The diagnostic that separates them is cheap:** frustum culling varies smoothly with yaw; a PVS
    steps with *position* and does nothing with yaw. So the residual void that survived the
    2026-08-28 work (92% → 18.5%) has the PVS gate's signature, not a transform bug.
  - **First person is probably unaffected** (head-eye is a small translation inside the same leaf); a
    *flown free camera* is affected — outside the level the leaf's visible set can be empty, blacking
    the screen for reasons that are not a matrix bug. Worth knowing before misdiagnosing one.

---

## 5. Deployed here, which clears the standing `[USER]` item

The home PC's install was still the **2026-08-18 build (146,944 B)** with no `playerpos`/`fpcam`/
`camfollow` and no automation launcher. Now deployed into `C:\Steam\steamapps\common\Psychonauts\`:

- `d3d9.dll` → the new 211,456-byte build
- `openvr_api.dll` → refreshed (byte-size identical, backed up anyway)
- `Launch-Psychonauts-Automation.bat` → was absent entirely; uses `%~dp0`, no hardcoded paths

**Backups, reversible in one step:** `d3d9.dll.bak-2026-09-02-pre-notes73` and
`openvr_api.dll.bak-2026-09-02-pre-notes73`. Restoring those returns the install to its
headset-verified 2026-08-18 state.

---

## What is NOT established

- **`headpos` has never run.** It is compile-verified and deployed, nothing more.
- **The bone owner is a guess with a safety net, not a determination.** The most likely failure is a
  clean `FAILED (no bone-owning object off the player)` — that is the probe working, not a crash.
  The diagnostic that would say the *derivation* is wrong rather than the candidate list being
  short: a candidate shape-checks (plausible bone count and array) but `FindBoneByName` returns null
  for `headJA_1` — that would mean the object owning `+0x54`/`+0x5C` is not Raz's rig.
- **The four `__thiscall` conventions are read, not exercised.** If one is wrong the symptom is a
  crash at the stage the log last named, not a wrong number.
- **This route is NOT read-only, and it must not inherit `playerpos`'s safety claim.** `0x00438390`
  branches on flag bits at `owner+0x94` across 143 instructions — the shape of a lazy "recompute the
  world transform if dirty, then cache it" accessor — so calling it can **write** engine state. That
  is exactly what the engine does whenever a script asks for a bone position, so it is not novel, but
  it is a different risk class from `PsyGetPlayerPos`'s pure pointer walk. It is why `headpos` is a
  **manual one-shot command and nothing per-frame**, and why the FP camera should keep using the
  fixed `fpheight` constant that this measurement produces rather than calling this route every frame.
- **Candidate probing reads fixed offsets (`+0xA8`) on objects whose size we have not established.**
  Very likely fine — the sibling node is known to extend past `+0xB8` — but it is a raw read, not a
  bounds-checked one.
- **`ECamera::BoxVisible` has not been located in our own binary** by the published signature. Do
  that before relying on it.
- The eye-height number itself does not exist yet — and when it does, notes/71's warning stands:
  **not the parking lot**, where Raz's Y moved 113.04 → 139.35 → 84.72 across one short walk.
