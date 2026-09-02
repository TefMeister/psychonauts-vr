# Astralathe's source corroborates the dossier at five points, and fixes the `lua_State` at a static offset

**Status:** 🆕 new · **Priority:** ⭐ high — it closes two of the three open items that gate the
engine-native Lua route (dossier §12), names the camera's own visibility test (dossier §11's "cull
test we could not find"), and hands the modding lane four one-minute static checks.

**How it was read:** through GitLab's REST API (`/api/v4/projects/34250039/repository/files/<path>/raw`),
the route the modding lane found on 2026-09-01 — the web UI renders client-side, the API returns raw
files. Astralathe is **GPLv3**: nothing below is copied. Offsets, function names and calling
conventions are interface metadata about Double Fine's binary; the few byte signatures quoted are
bytes of that binary, not Astralathe's expression, and are here so the modding lane can check them
against known addresses without a browser. `[reported 2026-09-02, read from the project's own source]`
throughout unless tagged otherwise.

## 1. Five independent matches with what the dossier inferred

Astralathe is a second, unrelated reverse-engineering of the same `psychonauts.exe`. Where its
published offsets land on ours, the dossier's `[inferred-static]` claims gain an independent reader.

| Astralathe (its names) | Our dossier | What it adds |
| --- | --- | --- |
| `GameApp::pPlayer` at byte **33164 = `0x818C`** (`EEntity*`) | `engine + 0x818C` = the player object, `[inferred-static 2026-09-01, n=3]` | an **independent n=4**, from a different reverser working from different evidence |
| `DFDInput_WasInputPressed(input)` at **`exe+0x5930`** | `IsKeyJustPressed` **`0x00405930`** (§10) | same function, same reading, independently |
| `ECameraProxy` exposes exactly one field: **`m_vecPos` at `ECamera+0x8`** | camera position at `camera+0x08` `[verified-live]` | independent match; Astralathe found nothing else on the camera object worth exposing, which fits our "every other matrix is a derived output" finding |
| `lua_dobuffer(lua_State*, const char* buff, int size, const char* name)`, **`__cdecl`** | `0x6B0C00(L, buf, len, chunkname_or_NULL)`, traced statically in notes/57 | the shape is identical: our primitive **is the Lua 4.0 API's own `lua_dobuffer`**, which is why it takes a raw buffer and needs no `lua_pushstring`. Signature to check at `0x6B0C00`: `55 8B EC 51 8B 45 ? 50 8B 4D ? 51 8B 55 ? 52 8B 45 ?` |
| `EMat4` stored `M00 M10 M20 M30 M01 …` with translation at `M03 M13 M23` — i.e. floats 12–14, bytes `+0x30..+0x38` from the matrix start | `+0x40` is the translation row of a 4×4 that starts at `node+0x10` (`0x10 + 0x30 = 0x40`) | the same bytes, read the same way, by both parties |

## 2. The `lua_State` is at a static offset — §12's "runtime capture" item is closable on paper

Astralathe never scans for the Lua state. It reads it from the game object:

- `GameApp` **embeds** its `EScriptVM` at byte **39476 = `0x9A34`** (its `GameApp_InitLua` hook
  pattern ends in `81 C1 34 9A 00 00`, an `add ecx, 0x9A34` — the constructor computing `this + 0x9A34`).
- **`lua_State*` is the dword at `EScriptVM + 8`.**

So if `g_pGameApp` is our singleton `*(void**)0x0078BC20` — and it must be, since both carry the player
pointer at `+0x818C` — then:

```
lua_State* L = *(void**)( *(char**)0x0078BC20 + 0x9A3C );
```

**One static check settles the identity.** Astralathe's pattern for `EScriptObject_SetTableValue`
contains `A1 ? ? ? ? 05 34 9A 00 00`: `mov eax, [global]` then `add eax, 0x9A34`. The game reads a
global pointer and adds the ScriptVM offset. If that `A1` operand is `0x0078BC20`, the identity is
proven from our own binary. `[hypothesis until checked]`

Notes/46 recorded the state as "heap address, changes per launch". It does — but the *pointer to it*
lives at a fixed place, which is all a proxy needs.

## 3. Thread and timing safety — a working precedent, not a proof

§12's item (2) asks whether calling into Lua from our own hook is reentrancy-safe. Astralathe's
`DebugConsole.cpp` calls `lua_dobuffer(GetLuaState(), text, len, text)` **synchronously, from inside its
ImGui overlay, which runs in its `IDirect3DDevice9::EndScene` vtable hook** — the render thread, the
same thread our proxy's hooks run on. No deferral, no lock, no return-value check. It is the console
PsychoRando and the Archipelago integration are built on. `[reported 2026-09-02]` That is a precedent
that the game's own thread inside a D3D9 callback is an acceptable place to run Lua; it is not a proof
that every moment in the frame is (a script callback re-entering the VM mid-binding is the case to
avoid, and `EndScene` is outside any binding).

## 4. The camera has a named visibility test — `ECamera::BoxVisible`

`topics/2026-08-24-debug-menu-and-octree-culling.md` and dossier §11 record that four dev-PC sessions
could not locate the cull test. Astralathe locates two `ECamera` methods by signature:

| method | signature (`__thiscall` on `ECamera*`) | pattern |
| --- | --- | --- |
| **`BoxVisible`** | `bool (EBox3 box /*by value: mins, maxs*/, void* pVisCache, bool)` | `55 8B EC 83 EC 28 89 4D ? 8B 45 ? 8A 88 ? ? ? ?` |
| `CalculateScreenDiagonal` | `float (EBox3* pBox)` | `55 8B EC 83 EC 44 89 4D ? 8B 45 ? 8B 48 ? 89 4D ? C7 45 ? 00 00 00 00` |

Astralathe uses them for its debug drawing, not for culling changes — but for this project they are:

- **A probe.** Log calls per frame and the bool returned; that is the per-object culling decision the
  void investigation wanted to watch, on a function with a known prologue. The `pVisCache` argument
  says results are cached per something — worth knowing before trusting a one-frame count.
- **A second void mitigation.** Forcing `BoxVisible` to return true (or testing against a widened
  box) changes *what is submitted*, upstream of draw traversal, without moving the camera and without
  the FOV-widen ceiling measured in the library. Cost: over-submission. It composes with the
  `+0x150` basis write rather than competing with it.
- `CalculateScreenDiagonal` is the LOD/size metric; a first-person eye that is closer to geometry than
  the chase-cam will change it, which is a thing to expect rather than a bug to chase.

The 2026-08-28 solution (rotate the `+0x150` matrix before culling) stands; this is the function that
reads it.

## 5. Four engine-level entry points the dossier does not list

| function | shape | why it matters |
| --- | --- | --- |
| **`GameApp_GetMainChannel(GameApp*) -> ECamera*`** — pattern `55 8B EC 51 89 4D ? 6A 00 8B 4D ? E8 ? ? ? ? 8B 00` | `__thiscall` | almost certainly our `GetCamera` **`0x004FA5A0`** (§9's `__thiscall void*(mgr)`). The name says the engine has camera **channels**, plural — main, and presumably cutscene/boss/first-person. Check the bytes at `0x4FA5A0`. |
| **`GameApp_CallFunctionf(GameApp*, EScriptVM*, const char* fn, const char* fmt, ...)`** — pattern `55 8B EC 83 EC 08 8D 45 ? 89 45 ? 6A 00` | `__fastcall` with edx unused | **call any of the 1,129 Lua bindings by name with printf-style arguments**, no stack marshalling and no source text — the engine's own bridge. The cleanest possible `SetCameraPosition`/`AttachCameraToEntity`/`GetBoneWorldPosition` call once `L` is known. |
| `EScriptVM_RunLuaCommands(EScriptVM*, const char*, EScriptObject* ctx, float*, unsigned)` | `__thiscall` | run a command string in the context of a script object — the engine-level wrapper above `lua_dobuffer` |
| `EScriptObject_CallMethodf(obj, const char* method, const char* fmt, ...)` | `__stdcall` | call a method on a specific script object (Raz's, a camera's) |

Also fixed by Astralathe, RVA-relative to the exe base `0x400000`: `GameApp::InitUIMenu` **`0x4FA2E0`**,
`GameApp::UpdateCheatCodes` **`0x506CB0`**, `EScriptObject::GetName` **`0x5CAE50`**, the keyboard poll
`GetKeyboardInput` **`0x402CF0`** (Astralathe hooks it to read the DIK-indexed state array, its
`c_dfDIKeyboard`, which is our `0x00782D78`). And on `GameApp`: `m_cRazInvincible` at `+0x3E`,
`pUIMenu` at `+0x8C64`, **`m_bStartupComplete` at `+0x9035`** — a clean "engine is up" gate for any hook
that must not run during startup. `GameApp_RenderFrame`, pattern `55 8B EC 83 EC 20 89 4D ? 68 ? ? ? ? FF 15`,
is where Astralathe does all its per-frame work; whether it is `CandB` (`0x004FEDA0`) or its caller is
a one-line byte check.

## 6. What Astralathe does NOT have, so nobody looks again

- **No entity layout.** `EEntity.h` is a 12-byte pad and a script-object pointer (notes/70 already
  said so); `EEntityManager.h` and `EEntityIter.h` are empty classes with two constructor/iterate
  signatures. Nothing on the `+0xB8` parent question, nothing on the 96-byte bone record. Those stay
  ours to decode.
- **No unit scale**, anywhere public — Oatmeal converts PLB to glTF but its posts do not state a
  metres-per-unit figure. The `headpos`-minus-`playerpos` measurement the board already queued is the
  right answer.
- **No first-person camera API.** The community Lua API docs (readthedocs, GitLab-backed) are index
  stubs: one member per page. The strongest public hint that the engine ships a first-person camera at
  all is content, not code: a 2011 write-up notes some boss fights are played "looking out the boss's
  eyes" `[reported]`, consistent with notes/44's `FirstPersonCamera` string. Its Lua entry point is
  not documented publicly; `GetMainChannel`'s "channel" vocabulary is the thread to pull in the binary.

## Concrete next steps, in order (all static, no game running)

1. Read the `A1` operand in the `mov eax,[global]; add eax,0x9A34` sequence — if `0x0078BC20`, the
   `lua_State` chain in §2 is proven from our own binary and §12 item (1) closes.
2. Check the bytes at `0x6B0C00`, `0x4FA5A0` and `0x4FEDA0` against the three signatures above.
3. Signature-scan the exe for `BoxVisible` and add a call counter next to the existing camera probes.
4. Consider `CallFunctionf` as the Lua bridge instead of hand-marshalling — one call, by name.

## Sources

- https://gitlab.com/scrunguscrungus/astralathe — `Astralathe/Psychonauts/{GameApp,EScriptVM,ECamera,Lua,DFDInput,PsychoMaths,EScriptObject,EEntityIter,EEntityManager,ERenderer,ERenderState}.h`, `Proxying/ECameraProxy.h`, `ImGui/DebugConsole.cpp`, `Astralathe.cpp` (GPLv3; read online via the REST API, nothing copied)
- https://psycholuaapi.readthedocs.io/en/latest/ — community Psychonauts Lua API docs (work in progress)
- https://www.killtenrats.com/2011/11/10/psychonauts-camera-control/ — the boss-fight first-person note
- https://jillcrungus.com/projects/psychonauts/blog/2022/09/30/docs-and-anims.html — the state of engine-side API documentation
