# 45 — Lua execution beachhead: static recon of the in-process VM (step 1 of notes/44)

**Date:** 2026-08-19, dev machine (static analysis of `Psychonauts.exe`, no process running, no
files modified). Follows notes/44's recommendation to make in-process Lua execution the first
build target, since it unlocks the entire scriptable engine (camera, bones, visibility,
shadows, input) as a control plane instead of per-function reverse engineering.

**Result: the VM's structure is fully mapped statically; one live step remains (the runtime
`lua_State*` and a confirming test call), which requires the game under x64dbg.**

## The binary is a gift: Lua glue lives in its own PE sections

`Psychonauts.exe` is a non-relocatable (no ASLR, `DllCharacteristics` DYNAMICBASE=0, ImageBase
`0x00400000`) 32-bit PE — so every address below is stable across runs. Double Fine even gave the
Lua layer dedicated sections: `.dflua` (0x00797000, binding-name/descriptor data) and `.dfluatx`
(0x006FC000, the registration thunks).

## Every engine binding follows one uniform thunk (1129 of them)

```
55 8B EC              push ebp; mov ebp,esp
68 <nameVA>           push  "BindingName"
68 <cfuncVA>          push  c_function
B9 <descriptorVA>     mov   ecx, per_binding_descriptor
E8 <rel>              call  register_fn  (== 0x005AF520 for ALL 1129)
5D C3                 pop ebp; ret
```

Parsed all 1129 into **`tools/lua-bindings.def`** (name → C-function VA → descriptor VA) — this
is generated interface metadata (allowed to commit; not game content). Category spread: 222
`Set*`, 122 `Get*`, 86 camera, 83 entity, 40 anim, 36 sound, 18 light, 7 physics, 5 shadow, 4
bone. This file IS the moddable API surface of the game.

High-value confirmed addresses (ImageBase 0x00400000):

| Binding | C func | Use |
|---|---|---|
| SetCameraPosition | 0x00568FA0 | first-person camera |
| SetCameraOrientation | 0x005691C0 | first-person camera |
| SetCameraFieldOfView | 0x00569420 | FOV |
| GetCurrentCamera | 0x00568EE0 | camera handle |
| AttachCameraToEntity | 0x0056AF40 | head-lock |
| GetBoneWorldPosition | 0x005B1630 | IK targets / hand anchor |
| GetBoneID | 0x005B18E0 | bone lookup by name |
| EntityHasSkeleton | 0x005C3DD0 | rig query |
| DumpSkeletonInfo | 0x00571F10 | **dump Raz's bone names/indices** (solves the notes/44 bone-map problem) |
| SetEntityVisible | 0x00593AA0 | hide Raz in first person |
| SetEntityAlpha | 0x00593DA0 | fade Raz |

## The Lua 4.0 standard library and the real loader

The base library is registered via a classic `luaL_reg` array (name-ptr / func-ptr pairs) in
`.rdata`, found by searching for the `"dostring"` string pointer as data:

- `dostring` wrapper `luaB_dostring` @ **0x006B73B0**
- `dofile`   wrapper `luaB_dofile`   @ **0x006B74B0**
- neighbors confirm it's the base lib: `collectgarbage` 0x006B72F0, `error` 0x006B6E30,
  `foreach` 0x006B78C0, `gcinfo` 0x006B72B0, etc.

Disassembling `luaB_dostring` (0x006B73B0) nails the calling convention and the real API:

```
mov  eax,[ebp+8]          ; L   <-- engine C bindings are standard Lua 4.0 int f(lua_State* L)
push eax
call 0x006AEF20           ; luaL_check_string(L) -> const char* (into [ebp-8])
...build a local descriptor at [ebp-0C]...
push &desc ; push 1 ; push L
call 0x006C1A40           ; lua_dobuffer / lua_dostring core (the arbitrary-script runner)
```

So: **`luaL_check_string` = 0x006AEF20**, **the script-runner (lua_dobuffer family) = 0x006C1A40**,
and every binding — engine and stdlib alike — takes `lua_State* L` at `[ebp+8]`.

## What that gives us, and the one remaining live step

To execute arbitrary Lua from our `d3d9.dll` we need exactly two runtime facts the static image
can't give us:

1. **The live `lua_State*`** — the engine calls `lua_open` once and holds the singleton in a C++
   object (the base-lib functions receive L as a parameter, so it isn't a plain global we could
   read statically; the holder is the script-system object). Easiest capture: breakpoint any
   binding C-func (e.g. 0x00568FA0) in x64dbg while the game runs a script, read `[ebp+8]`. It's
   then a fixed heap pointer for the process lifetime — cache it in the DLL at first hook.
2. **Confirm `0x006C1A40`'s exact prototype** (arg order/count for `lua_dobuffer(L, buff, size,
   name)` vs a thinner `lua_dostring(L, s)`), by inspecting the live call at the breakpoint.

Both are ~10 minutes under x64dbg with the game at the menu (our `enter_gameplay`/silent-launch
tooling already gets us there). Once captured, the DLL primitive is:

```c
/* one-time: grab L from the first binding-call we breakpoint/hook, cache it */
typedef int (__cdecl *lua_dobuffer_t)(lua_State* L, const char* buf, size_t sz, const char* name);
((lua_dobuffer_t)0x006C1A40)(g_L, script, strlen(script), "psyvr");
```

…and from that single call we can drive **all 1129 bindings** as Lua text —
`SetCameraPosition(...)`, `DumpSkeletonInfo(raz)` to print the bone map, `SetEntityAlpha(...)`,
the lot — which is the whole point of doing this first (notes/44 attack order).

## Risk / cleanliness notes

- Calling into the VM must happen on the game's own thread (Lua 4.0 is not reentrant). Our hooks
  already run on the render/main thread — feed a command queue drained inside a per-frame hook
  (e.g. the Present or CandB hook we already own), never from an arbitrary thread.
- No game files are touched; this is code-level interaction with our own injected DLL, same
  posture as every other hook in this project.
- Addresses are hardcodable because the exe is non-relocatable (verified), but the DLL should
  sanity-check the first bytes at each address (e.g. `55 8B EC` at the thunks / known opcodes at
  the loader) before trusting them, and no-op with a log line if they don't match — so a
  different game build fails safe instead of crashing.

## Next actions

1. **Live (x64dbg):** capture `g_L` and confirm 0x006C1A40's signature (test-call
   `DumpSkeletonInfo` on Raz to also harvest the bone-index map for IK in one shot).
2. Add a `PSYVR_LUA=<script>` / command-file channel to the DLL, drained on the game thread.
3. First real use: `SetCameraPosition/Orientation` at the head bone each frame = first-person
   prototype (notes/44 step 2).

🤖 Session driven autonomously via Claude Code (static PE analysis only; the live capture is
deliberately deferred to a debugger session).
