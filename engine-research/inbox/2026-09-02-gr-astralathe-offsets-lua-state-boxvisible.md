# Astralathe's published offsets land on the dossier's — and fix the `lua_State`, name the cull test, and expose `CallFunctionf`

Filed by: `/gr`, 2026-09-02
Topic: `external-research/topics/2026-09-02-astralathe-source-corroborates-the-dossier-and-fixes-the-lua-state.md`
Dossier sections: §9 (bindings table), §10 (input internals), §11 (dead ends — "cull test"), §12 (open risks: Lua exec items 1 and 2)

Read from Astralathe's source (GPLv3, via the GitLab REST API, nothing copied). `[reported 2026-09-02]` throughout.

## Corroborations — suggested dossier edits

- **§9, player chain:** `engine + 0x818C` gains an independent reader — Astralathe's `GameApp::pPlayer` is at byte 33164 = `0x818C`. Suggest `[inferred-static 2026-09-01, n=3]` → `n=4 (one independent)`.
- **§10:** `IsKeyJustPressed 0x00405930` — Astralathe names the same address `WasInputPressed(input)`. Its keyboard poll hook is at **`0x402CF0`** (`GetKeyboardInput`), reading the same DIK-indexed array we know at `0x00782D78`.
- **§12 item (3) / notes/57:** `0x6B0C00(L, buf, len, name)` has exactly the shape of Lua 4.0's own `lua_dobuffer`; pattern to confirm: `55 8B EC 51 8B 45 ? 50 8B 4D ? 51 8B 55 ? 52 8B 45 ?`.

## §12 item (1) — the `lua_State` does not need runtime capture

`GameApp` embeds its `EScriptVM` at `+0x9A34`; `lua_State*` is the dword at `EScriptVM + 8`. If `g_pGameApp` is our `*(void**)0x0078BC20` (same object — same player offset), then
`L = *(void**)(*(char**)0x0078BC20 + 0x9A3C)`. **Check:** the game's `EScriptObject::SetTableValue` begins `mov eax,[global]; add eax,0x9A34` (`A1 ?? ?? ?? ?? 05 34 9A 00 00`) — if that operand is `0x0078BC20`, this is proven from our own binary. Suggested dossier change: replace "runtime `lua_State*` capture (heap address, changes per launch)" with the static chain plus its verification status.

## §12 item (2) — a precedent for calling Lua from the render thread

Astralathe's console calls `lua_dobuffer(GetLuaState(), s, len, s)` synchronously inside its `IDirect3DDevice9::EndScene` vtable hook, no deferral, no guard; PsychoRando and the Archipelago integration run on it. Precedent, not proof — outside any binding callback is the safe case.

## §11 — the cull test has a name and a signature

`ECamera::BoxVisible(EBox3 box /*by value*/, void* pVisCache, bool)`, `__thiscall`, prologue `55 8B EC 83 EC 28 89 4D ? 8B 45 ? 8A 88 ? ? ? ?`. Also `ECamera::CalculateScreenDiagonal(EBox3*)` (LOD metric), `55 8B EC 83 EC 44 89 4D ? 8B 45 ? 8B 48 ? 89 4D ? C7 45 ? 00 00 00 00`. Suggest a §11 note that the per-object visibility decision is this function, so "could not locate the cull test" is closed as a *location*, and a probe/force-true mitigation is available upstream of draw traversal.

## §9 — likely identities and new entry points

- `GameApp_GetMainChannel(GameApp*) -> ECamera*`, `55 8B EC 51 89 4D ? 6A 00 8B 4D ? E8 ? ? ? ? 8B 00` — probably our `GetCamera 0x004FA5A0`; the name implies camera **channels**.
- **`GameApp_CallFunctionf(GameApp*, EScriptVM*, const char* fn, const char* fmt, ...)`** (`__fastcall`, edx unused), `55 8B EC 83 EC 08 8D 45 ? 89 45 ? 6A 00` — call any Lua binding by name with printf-style args. Worth a row in the §9 table as the engine's own bridge.
- `GameApp_RenderFrame`, `55 8B EC 83 EC 20 89 4D ? 68 ? ? ? ? FF 15` — is it `CandB 0x004FEDA0` or its caller? One byte check.
- Fixed addresses: `InitUIMenu 0x4FA2E0`, `UpdateCheatCodes 0x506CB0`, `EScriptObject::GetName 0x5CAE50`; `GameApp` fields `m_cRazInvincible +0x3E`, `pUIMenu +0x8C64`, `m_bStartupComplete +0x9035`.

Nothing in Astralathe on the `+0xB8` parent, the 96-byte bone record, or the unit scale — those remain ours.
