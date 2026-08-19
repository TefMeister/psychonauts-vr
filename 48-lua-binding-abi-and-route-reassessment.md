# 48 — Lua binding ABI mapped; head-lock route reassessment (Lua vs render-level bone extraction)

**Date:** 2026-08-19, dev machine, x32dbg live disassembly of `Psychonauts.exe`. Goal: build the
head-bone-locked first person via the Lua route (user's choice, notes/47). **Result: mapped the
Lua binding ABI far enough to cost the Lua route honestly — it's a genuine multi-session RE lift.
A render-level bone-extraction path reaches the SAME head-lock with much less work and no VM-call
risk. Recommendation + decision below; no code changed this session.**

## Lua binding ABI (every one of the 1129 bindings follows this)

Each `Name` binding is a two-layer function:

1. **Outer shim** `B` (the address in `tools/lua-bindings.def`): a coroutine/fiber stack-switch
   wrapper. It swaps ESP to a saved coroutine stack (globals `0x0078CBD0` flag / `0x0078CBCC`
   saved-ESP), calls the real impl, then restores. Boilerplate; identical across bindings.
2. **Real impl** `B_impl` (immediately after the shim): `int B_impl(lua_State *L)` — L at `[ebp+8]`,
   standard Lua 4.0 C-function shape.

Inside `B_impl`, the Lua-C stack ABI (confirmed from `SetCameraPosition_impl` 0x569000 and
`GetBoneWorldPosition_impl` 0x5B1690):

| Addr | Role | Signature seen |
|---|---|---|
| 0x6AEF20 | get arg 0 / "self" (the object the method is called on) | `(L)` -> object ptr |
| 0x5AF750 | arg count/type validation | `(L, nargs, maxidx)` -> bool (al) |
| 0x5B0230 | **lua_tonumber** — read numeric arg at index | `(L, index, 0)` -> float in st(0) |

`SetCameraPosition_impl` reads args 1/2/3 via three `0x5B0230(L, i, 0)` calls (the x/y/z), each
`fstp`'d into a local. `GetBoneWorldPosition_impl` gets self (0x6AEF20), validates `(L,2,3)`, then
reads the bone arg and calls an engine routine, pushing 3 numbers back.

Still UNMAPPED for a full primitive: `lua_pushnumber`/`lua_pushstring` (push side), and the
string-exec entry (`lua_dobuffer`, or driving the dispatcher 0x006C1A40 with a pushed string).
`lua_tonumber` (0x5B0230) is the read side and is done.

## Why the Lua route to head-lock is a multi-session lift

To get Raz's head world position via Lua we must, in order:
1. **Finish the exec/stack primitive** — locate push-side funcs + a string runner, add a runtime
   `lua_State` capture (heap ptr, changes per launch — notes/46), and a game-thread command pump
   (Lua 4.0 is non-reentrant). ~1-2 debugger sessions.
2. **Discover the game's script API** — how to obtain the player entity and the head bone id
   (a `GetPlayerEntity`-equivalent + `GetBoneID`, plus Raz's bone naming). Not documented; needs
   experimentation, e.g. running `DumpSkeletonInfo` (0x00571F10) via the primitive and reading its
   output. ~1 session, uncertain.
3. **Integrate** per-frame `GetBoneWorldPosition` calls + facing into the FP `X1` machinery.

Real, but several sessions, with crash risk from calling into the VM.

## The faster path to the SAME head-lock: render-level bone extraction (no Lua)

Raz's skeleton already flows through our existing `SetVertexShaderConstantF` hook as the c96 bone
palette (BONEPROBE, notes/44), and we cache everything needed to place it in the world:

- **World matrix**, per skinned draw: `World = WVP · P⁻¹ · V⁻¹`. WVP is the register-6 upload we
  already intercept; V is rebuildable from the BVM cache (eye/at/up); P from the BPM cache
  (xScale/yScale/zn/zf). All in hand.
- **Head bone position (model space)** from the c96 palette (32 bones × 4×3); world =
  `World · boneMatrix · origin`.
- **Identify Raz's draw**: heuristic — the skinned draw nearest the camera / the persistent main
  character (refine empirically; the chase camera always frames Raz).
- **Head bone index**: from the palette empirically (topmost bone), or once via `DumpSkeletonInfo`.

This needs no VM calls (no crash/reentrancy risk), no game-script-API discovery, and reuses data
already captured. ~1-2 sessions. It ALSO produces exactly the per-draw entity + bone-index
groundwork that hand IK needs later — so it's not throwaway.

## Recommendation

**Pivot the head-lock feature to render-level bone extraction (Option B).** It reaches the same
"eye locked to Raz's head + HMD-only orientation" result faster and safer, and doubles as IK
groundwork. Keep the **Lua control plane as a parallel, longer-horizon track** — it's still the
clean enabler for scripted things (hiding specific meshes by entity, camera-mode swaps, level
scripting) and worth finishing, just not on the critical path to comfortable first person.

If the user prefers to stay on the Lua route regardless, the next step is a debugger session to
finish the exec/stack primitive (push funcs + string runner + L-capture), then `DumpSkeletonInfo`
to learn Raz's rig.

## Addresses banked this session (this build; exe non-relocatable)

- Binding ABI: shim→impl pattern; impl is `int f(lua_State* L)`, L at [ebp+8].
- `lua_tonumber` = 0x5B0230(L, index, 0) -> st(0); arg-check 0x5AF750(L,nargs,maxidx)->bool;
  arg0/self getter 0x6AEF20(L).
- Real impls: SetCameraPosition 0x569000, GetBoneWorldPosition 0x5B1690 (behind shims 0x568FA0 /
  0x5B1630). Coroutine stack-switch globals: flag 0x0078CBD0, saved-ESP 0x0078CBCC.

🤖 Static/live disassembly only; no game files modified, no code changed, debuggee detached cleanly.
