# `BoxVisible` disassembled — and the engine ships its own "cull from a different camera" switch

**2026-09-03, dev PC, `/pd` (parallel development).**
**The game was not launched. Nothing here has been run.**

The board's `[PD]` row asked for this before any counter or mitigation is built:

> `ECamera::BoxVisible` is now located (`0x004CDC60`, notes/74) but unhooked — disassemble the helper
> `0x4130B0` and the two by-value-struct call sites (`0x4CDCE1`/`0x4CDD11`/`0x4CDD34`) fully before
> building any counter or mitigation, since the function is `__thiscall` + self-recursive and a
> rushed trampoline is exactly this account's own known bug shape.

Done, and the caution paid off twice: the "self-recursion" is not what it looked like, and **a
trampoline may not be needed at all.** `[inferred-static 2026-09-03]`

---

## 1. The signature, settled

```
bool __thiscall ECamera::BoxVisible( Box24 box /*by value, 6 dwords*/,
                                     void* pVisCache,
                                     BOOL  bUseCache )
```

`ret 0x20` pops 32 bytes: 24 for the by-value box (`[ebp+8]`…`[ebp+0x1c]`, two `Vec3`s) plus two
dwords. `this` arrives in `ecx` and is spilled to `[ebp-0x28]`. The stack juggling the board flagged
(`sub esp,0x18` then copying six dwords through `eax`) is just the compiler re-pushing that
by-value box for an onward call — it appears twice, at `0x4CDCE1` and again at `0x4CDE2D`.

## 2. The "self-recursion" is a delegation, not recursion

`0x4CDCE1` calls `0x4CDC60` — itself. But look at what `ecx` holds:

```
cmp  dword [0x788CB0], 0        ; a global camera pointer
je   skip
mov  ecx, [0x78BC20]            ; the camera-manager singleton
call 0x4FA5A0                   ; -> its element [0]
cmp  [ebp-0x28], eax            ; is `this` the primary camera?
jne  skip
cmp  eax, [0x788CB0]            ; ...and not already the override?
je   skip
  ... re-push the box ...
mov  ecx, [0x788CB0]            ; <-- a DIFFERENT `this`
call 0x4CDC60                   ; delegate
```

So it is a **tail-delegation to another camera object**, entered at most once, and only when the
global at `0x788CB0` is non-null and `this` is the primary camera. It cannot recurse without bound.
That materially lowers the risk the board row was written to guard against — though a trampoline
would still have to survive being re-entered once.

## 3. ⭐ The global is an engine-provided cull-camera override, with a public setter

Every reference to `0x788CB0` resolves:

| Address | What it is |
|---|---|
| `0x004D0DA0` | **`void __cdecl SetCullCamera(ECamera*)`** — six instructions, two of substance: `mov eax,[ebp+8]; mov [0x788CB0],eax`. |
| `0x004D0DB0` | **`ECamera* __cdecl GetCullCamera()`** — the matching getter. |
| `0x004CAD80` | `__thiscall` teardown guard: *if the global points at `this`, clear it.* |
| `0x004D36EF` | another clear-to-null, on level teardown. |
| the rest | the reads inside `BoxVisible` / the frustum test. |

**The engine already supports culling from one camera while rendering from another**, and exposes it
as a plain one-argument function. For the void problem — where the rendering camera has moved but the
visible set should still follow the player — that is precisely the shape of lever wanted, and it
needs no hook at all: one call.

⚠️ **What this is not:** proof it does what we want. It is a clean, complete static reading of the
control flow. Whether pointing it at the player camera actually keeps the world drawn while the eye
moves is **untested**, and the delegation only fires when `this` is the manager's element `[0]`, so a
second camera may not go through this path at all.

## 4. Two one-bit disables — one shared, one not

```
0x004CDC60  mov cl,[this+0x530]; shr cl,4; and cl,1; test -> if zero, return TRUE
0x004CDB70  ... the identical three instructions at the top of the frustum test ...
```

⚠️ Only the **first** of the two is shared. The frustum test `0x004CDB70` contains **zero**
references to `+0x531` `[measured 2026-09-03]` — that bit gates the PVS stage, which lives only in
`BoxVisible`.

- **`[cam+0x530]` bit 4** — master cull enable. Clear it and `BoxVisible` *and* the frustum test both
  return "visible" immediately. Everything draws.
- **`[cam+0x531]` bit 0** — one of three conditions gating the PVS stage specifically. Clear it and
  the PVS query is skipped while the frustum test still runs.

So there are three non-hooking levers before any trampoline is written: those two bits, and
`SetCullCamera`.

## 5. The two gates, and why the board's PVS reasoning is now structurally supported

After the delegation check, the flow is:

1. **If `bUseCache`:** translate both box corners by the camera's `Vec3` at `[this+8]`, via the helper
   at `0x004130B0` — which is a plain **`Vec3 += Vec3`** (`__thiscall`, `ret 4`, returns `this`), one
   `fld/fadd/fstp` per component. Called twice, once per corner (`0x4CDD11`, `0x4CDD34`). Nothing
   clever, and nothing that a hook needs to preserve beyond the arguments.
2. **PVS gate**, entered only if *all* of: `[this+0x514] != -1`, `[this+0x531] & 1`,
   `byte [[0x78BC20]+0xA1] != 0`.
   - `0x00489A60( ecx = [[[0x78BC20]+0x1CC]] + 0x140, &box, [this+0x514], this+0x520 )` is the query.
   - `0x00489C00` / `0x00489BB0` are the cache get/set, both keyed by **`[this+0x514]`**.
   - Fails ⇒ return false immediately.
3. **Frustum test** `0x004CDB70`, called last with the box by value and a trailing `0`.

**`[this+0x514]` is the camera's leaf/cluster index** — it keys the query *and* the cache, and nothing
in this path consults orientation. That is direct structural support for the board's model that the
PVS gate steps with **position** while the frustum gate varies smoothly with **yaw**. It was a
behavioural prediction; it is now visible in the control flow. `[inferred-static 2026-09-03]`

## 6. What is NOT established

- **That any of the three levers produces the desired picture.** All of this is control flow read off
  disk. No value has been observed, nothing has been written, nothing has run.
- **Which object the flags live on at runtime, and whether the camera we care about is the manager's
  element `[0]`.** The delegation path depends on that and it is untested.
- **What `0x00489A60` actually computes.** It is called with the box, the leaf index and `this+0x520`,
  and its result is cached per leaf — consistent with a PVS query, but it has not been disassembled.
  Called it "the PVS query" on the strength of its arguments and its caching key, not its body.
- The names `SetCullCamera` / `GetCullCamera` are **mine, describing behaviour** — the binary has no
  symbols. Do not quote them as if they were the game's own.

### The diagnostic that would show this reading is wrong

Set `[cam+0x530]` bit 4 to 0 on the active camera and the whole world should draw regardless of where
the camera points, because both functions short-circuit to "visible". **If geometry still disappears
with that bit clear, then `BoxVisible` is not the gate that produces the void** — and the void's cause
is somewhere this analysis has not looked, not in a mis-set flag. That is a cheaper and more decisive
first test than any trampoline.

## 7. On the queued PVS-leaf-size item — the cost is higher than it looked

`/gr` proposed bounding PVS leaf size by reading two fields (root `SSECube` extent, `LeavesCount`)
from "a file already in the game install", and suggested it belongs in the `[PD]` queue. The
reasoning is good and the item is real, but **the `.plb` is not a loose file on disk**
`[measured 2026-09-03]`: there are zero `.plb` files in the install. Levels ship as **`PPAK`
containers** — `WorkResource/PCLevelPackFiles/*.ppf`, **50** of them (beside 50 `.apf`), up to
33 MB — whose headers begin `50 50 41 4B` and whose interior interleaves asset paths with
compressed data.

So the item stays `[PD]` (still no game needed) but the true cost is *parse the PPAK container first,
then locate the level binary inside it, then walk to the Octree* — not a two-field read. Recorded on
the board with that cost, rather than left looking like a five-minute job.

## Files

- `dev-archive/recon/2026-09-03-boxvisible-disassembled/2026-09-03-boxvisible-disassembly.txt` — the
  full listings this note is derived from, reproducible with `static-disasm.py`.
