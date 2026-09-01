# 69 — Raz's position found without Lua; first person rebuilt on the camera object

**Date:** 2026-09-01, dev machine. **Static disassembly only — the game was never launched**
(a parallel session owns the machine's one "game may run" slot). Built and deployed; **nothing
here has been run yet.**

**Result:** the FP sub-project's long-standing blocker — "we cannot get Raz's world position
without finishing the Lua exec primitive" — was **never true**. Three pointer dereferences off a
global the proxy already uses give it directly. FP has been rebuilt on the two pre-culling camera
fields and is deployed, awaiting a launch.

---

## 1. The blocker dissolved: `GetPlayerPosition_impl` is fifteen instructions

notes/47 recorded FP as blocked on Raz's position; notes/48 costed the Lua route at "several
sessions" (finish the exec primitive, discover the script API, learn the rig). notes/67 then found
that Lua bindings are a marshalling shim over plain engine work and that the shim can be skipped.
Applying that to `GetPlayerPosition` (shim `0x005C1C80`, impl `0x005C1CE0`) ends the matter:

```
engine = *(void **)0x0078BC20          ; the same global AutoGetCamera already guards
player = *(void **)(engine + 0x818C)
obj    = *(void **)(player + 0x10)
pos    = (float *)(obj + 0x40)         ; x, y, z contiguous
```

That is the **entire** function apart from three `lua_pushnumber` calls. No Lua state, no engine
call, no VM re-entrancy risk — a read-only pointer walk that is safe from the render-path hook
under the same rule the rest of the harness follows.

**Confidence.** The player object at `engine+0x818C` is `[inferred-static 2026-09-01, n=3]` —
three independent bindings reach it identically: `GetPlayerPosition` (`0x005C1CE0`),
`GetPlayerLSO` (`0x005C0BD0`), `IsRazZLocked` (`0x005B6CB0`). The `+0x10 → +0x40` position tail is
`[inferred-static 2026-09-01, n=1]` — `GetPlayerPosition` alone. **If the numbers look wrong live,
distrust the tail first.** Nothing here is verified against a running game.

## 2. `GetBoneWorldPosition` fully decoded — and it returns six values, not three

`GetBoneWorldPosition_impl` (`0x005B1690`), decoded statically end to end:

```
argc = 0x006AEF20(L)                        ; lua_gettop
if (!0x005AF750(L, 2, 3)) return 0          ; require 2..3 args
ent  = 0x005B01E0(L, 1, 0)                  ; arg1 -> native node ([scriptobj + 0xA8])
if (!ent) return 0
name = 0x005B02B0(L, 2, 0)                  ; arg2 -> string (bone name)
idx  = (argc >= 3) ? (int)0x005B0230(L, 3, 1) : 0
bone = idx ? __thiscall 0x00438F30(ent, (WORD)idx)    ; by numeric id
           : __thiscall 0x00438B70(ent, name)         ; by name
if (!bone) return 0
__thiscall 0x00492B70(bone, float mat[16])  ; bone transform, parent offset applied
x = __thiscall 0x00438390(ent)              ; entity world-transform provider
__thiscall 0x00433E50(x, out[16], mat)      ; compose -> world matrix
push out row 3 as (x, y, z)                 ; WORLD POSITION
__thiscall 0x00692890(&out, vec3)           ; matrix -> euler (fabs + epsilon compare
                                            ; against the double at 0x00713000)
push those three                            ; ORIENTATION
return 6
```

So the binding yields **position and orientation**, which is more than the notes assumed.

### ⚠️ Correction to notes/48's ABI table

notes/48 recorded `0x006AEF20` as "get arg 0 / self (the object the method is called on)". **It is
`lua_gettop(L)` — the argument count.** The function is nine instructions:
`(L->top - L->base) >> 3`, an 8-byte Lua 4 TObject stride. Every `impl` that "checks self against
3" is really branching on `argc >= 3` for an optional argument. Reading it as "self" makes the
optional-argument logic in these bindings incomprehensible, which is worth fixing before anyone
builds on that table.

Corrected/added ABI rows, all `[inferred-static 2026-09-01]`:

| Address | Role |
|---|---|
| `0x006AEF20` | `lua_gettop(L)` → argument count **(was mis-recorded as "self")** |
| `0x005AF750` | argument-count check `(L, min, max)` → bool |
| `0x005B01E0` | arg at index → native node (`[scriptobj + 0xA8]`) |
| `0x005B01B0` | arg at index → the script object itself (the layer above) |
| `0x005B02B0` | arg at index → string |
| `0x005B0230` | `lua_tonumber(L, index, flag)` → st(0) |
| `0x006AF600` | `lua_pushnumber(L, float)` |
| `0x006AF650` | push bool · `0x006AFB00` push int/handle |
| shim → impl | **impl is always shim + 0x60** in this build |

## 3. Raz's rig bone names are in the exe — no `DumpSkeletonInfo` session needed

notes/48 said the rig naming was undocumented and needed a live `DumpSkeletonInfo` run. The names
are sitting in `.rdata`:

| Bone | VA |
|---|---|
| `headJA_1` | `0x00703F94` |
| `HeadJA_1` | `0x00707FA4` |
| `headJEnd_1` | `0x0070D828` |
| `spineJC_1` | `0x00704E60` |
| `handJEndLf_2` | `0x0071195C` |
| `handJEndRt_1` | `0x0070E5F0` |
| `bubbleJC_1` | `0x00713D00` |

`handJEndLf_2` is the **default attach bone** `AttachInventoryEntityToPlayer` falls back to when
its bone argument is omitted (`0x005B0D6A`), which is what makes these real rig names rather than
unrelated strings. `headJA_1` is passed as a node name via `__thiscall(node, "headJA_1")` at
`0x0043CA90`. `[inferred-static 2026-09-01, n=1 per name]` — none has been resolved against a live
skeleton, and whether `0x0043CC00` *looks up* or *assigns* the name is not established, so treat
"headJA_1 is the head joint" as strong but unconfirmed.

## 4. What was built: `fpcam` — first person on the camera object

The existing `PSYVR_FIRST_PERSON` path composes a translation into the register-6 WVP upload.
notes/68 proved that layer is **post-culling**: it moves the picture, not the camera. That is the
likely explanation for notes/51–53's "zero visible eye movement" mystery, and it is why the black
void survives FP.

The rebuild writes the two fields that are **pre-culling inputs** and are both now known-writable:

* **position** — `camera+0x08` (already exercised by `campos`/`camhold`)
* **orientation** — `camera+0x150` (already exercised by `camfollow`)

applied at **BeforeEye1**, not from the automation tick — the tick runs from `AfterBoth`, one frame
late and after culling, which is the same trap the look-direction hold already fell into.

New commands (all default off, nothing changes unless asked):

| Command | Effect |
|---|---|
| `playerpos` | log Raz's world position — **the one-command check that §1 is real** |
| `fpcam 0\|1` | park the camera at Raz's position each frame, before culling |
| `fpheight <wu>` | eye height above Raz's origin (default 60) |
| `fpforward <wu>` | push the eye forward along camera forward, to clear the head mesh |
| `fpaxis <0\|1\|2>` | which world axis the height is applied to (default 2 = Z) |

**Deliberately a fixed eye height, not the head bone.** The bone chain is fully mapped in §2 and
costs one more call, but a fixed height answers the only question that matters first: *does the
engine cull correctly for a camera parked inside the player?* Wiring the bone before knowing that
would risk attributing a bone bug to the camera.

**The height/forward defaults are guesses.** Raz is a child, the game's unit scale is recorded
nowhere in the notes, and notes/67 measured a respawn at ~26,000 units of travel — so the scale is
large and 60 may be far too small or far too large. Both are live commands for that reason.

`fpaxis` exists because the evidence is split: notes/68 found the camera world matrix's right
vector reads `Z = 0.0000` exactly, which puts world up on **Z**, while the dossier warns this
engine's levels do **not** all share an up axis. Selectable rather than guessed.

## 5. Status: built, deployed, never run

`d3d9.dll` rebuilt (clean, only the two pre-existing warnings) and deployed to the game folder;
the previous build is backed up beside it as `d3d9.dll.bak-20260901-*`. **No claim in §1–§4 has
been observed live.**

### Next launch, in this order

1. **`playerpos`** — first, before anything else. It must print plausible coordinates, they must
   change as Raz walks, and they must **not** change when only the camera turns. That last check is
   the one that separates "player position" from "another copy of the camera position", and it is
   cheap.
2. `fpcam 1` and look at the framing; tune with `fpheight` / `fpforward`; if height moves the eye
   sideways or forward instead of up, the up axis is wrong for that level — try `fpaxis 1` / `0`.
3. **The actual question:** with `fpcam 1` and a head sway, is the void gone? The camera is now
   genuinely inside the player and pre-culling, which is the configuration notes/68 predicted would
   cull correctly.
4. Only then consider replacing the fixed height with `headJA_1` via `0x00438B70` → `0x00492B70`.

### Not done, deliberately

Raz's mesh is not hidden, so first person will show the inside of his head until either
`fpforward` clears it or the mesh is culled. Naming it here so it is not diagnosed as a rendering
bug on first sight.

🤖 Static disassembly of `Psychonauts.exe` only; the game was not launched and no game file was
modified other than the mod's own `d3d9.dll` (backed up first).
