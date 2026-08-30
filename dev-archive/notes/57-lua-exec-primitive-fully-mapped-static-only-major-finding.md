# 57 — Lua exec primitive fully mapped statically: `0x6B0C00(L, buf, len, name)` runs arbitrary Lua

**Date:** 2026-08-20 evening, dev machine. Continuation of the static-only research session
(notes/55-56). This is the biggest finding of the session — **potentially reversing notes/52's
"Lua exec is a multi-session lift" cost estimate.** Zero game execution; pure disassembly of the
non-relocatable exe, cross-checked against known Lua 4.0/5.0-family VM call conventions.

## Headline: a single, complete, arbitrary-Lua-string-execution function

```
int RunLuaString(lua_State *L, const char *buf, size_t len, const char *chunkname /* or NULL */)
```
at **`0x6B0C00`**. Traced completely:

```c
// 0x6B0C00 - outer wrapper
int f_0x6B0C00(lua_State *L, const char *buf, size_t len, const char *chunkname) {
    int loadStatus = f_0x6B0C40(L, buf, len, chunkname);   // compile + push chunk-function
    if (loadStatus == 0)
        return f_0x6B08E0(L, 0, -1);   // success -> CALL it (0 args, LUA_MULTRET results)
    return loadStatus;                  // compile/load error -> propagate the error code
}

// 0x6B0C40 - compile+load, pushes a callable function onto the Lua stack on success
int f_0x6B0C40(lua_State *L, const char *buf, size_t len, const char *chunkname) {
    if (chunkname == NULL) chunkname = "?";              // confirmed string @0x718410
    LexState local[280 bytes];                            // parser/lexer state, stack-allocated
    f_0x6BC550(&local, len, buf, chunkname);               // the real parser/compiler entry
    BOOL isPrecompiled = (buf[0] == 0x1B);                 // 0x1B = Lua bytecode signature byte
    return f_0x6B0B30(L, &local, isPrecompiled);            // finish load, push closure, return status
}

// 0x6B08E0 - protected call of whatever's on top of the Lua stack
int f_0x6B08E0(lua_State *L, int nargs, int nresults) {
    // computes L->top - (8*nargs + 8) as the call-base stack slot (8 bytes/TObject, matches
    // notes/50's already-confirmed TObject size), packages (nargs, nresults) into a local
    // struct, and passes it through f_0x6B0E10(L, f_0x6B0940, &struct) - a luaD_pcall-style
    // PROTECTED call wrapper (error-jump-buffer semantics). f_0x6B0940 is the actual "perform
    // the call" inner routine, unwrapping (nargs, nresults) from the struct at offsets +0/+4.
    ...
}
```

**This is functionally a complete `luaL_dostring`/`lua_dobuffer` equivalent — exactly the
primitive every prior session (notes/45-50) was hunting for**, and it takes a **raw buffer + length
directly**, meaning **no `lua_pushstring` is needed at all**. notes/50's framing (arbitrary-string
exec needs `lua_pushstring` + a string runner, OR fully decoding compile/run to accept a raw buffer)
turns out to have been solved by the second option — `0x6B0C00`/`0x6B0C40` already take the raw
buffer directly; there's no Lua-stack argument staging step required.

## Why this differs from `luaB_dostring` (the original starting point, notes/46/50)

`luaB_dostring` (`0x6B73B0`, the Lua *base-library* `dostring` global) is a thin wrapper that reads
its string **from the Lua stack** (arg 1, via `luaL_check_lstr`/`lua_gettop`) and then calls this
SAME `0x6B0C00`/`0x6B0C40` machinery — i.e. `0x6B0C00` is the shared, lower-level primitive that
BOTH the Lua-visible `dostring()` global AND (presumably) the engine's own script-loading code use.
Calling `0x6B0C00` directly from our DLL skips `luaB_dostring`'s Lua-stack-argument step entirely —
we don't need to push anything onto the Lua stack, because `0x6B0C00` never reads its source from
the stack; it reads it from the `buf`/`len` C arguments we pass straight in.

## What's now confirmed vs. what's still genuinely unverified

**Confirmed statically, high confidence:**
- Exact 4-argument signature of `0x6B0C00` (buf/len/chunkname, `NULL`→`"?"` default — matches the
  reference Lua idiom, a reassuring sanity check).
- `0x6B08E0(L, nargs, nresults)` is a real protected-call primitive (`luaD_pcall`-style jump-buffer
  wrapper confirmed via its call structure into `0x6B0E10` + inner `0x6B0940`).
- The `0x1B` precompiled-bytecode check matches `luaB_dostring`'s own identical check (same
  constant, same semantics) — strong cross-validation these are genuinely part of one consistent
  Lua 4.0 implementation, not two unrelated functions that happen to look similar.

**Still genuinely open (do NOT treat as solved without a live test):**
1. **Runtime `lua_State*` capture** — still a real heap pointer that changes per launch (notes/46's
   finding stands unchanged). Needs the planned one-shot hook (snapshot `[esp+4]` on the first
   binding-cfunc call) — not attempted this session (needs a live process).
2. **Thread/reentrancy safety** — Lua 4.0 is not reentrant; `0x6B0C00` must be called on the
   correct thread, outside of any nested VM call, from a point in the frame where the engine isn't
   already mid-script-execution (e.g. from our `Present`/`CandB` hook, not from inside a binding
   callback). Not verified live.
3. **Whether `0x6B0C00` operates on the MAIN Lua state or must target a specific coroutine** —
   notes/46 found the engine runs game scripts on pooled coroutine threads; calling this on the
   wrong thread object could have unintended scoping effects (e.g. global writes not visible where
   expected, though for camera/entity calls via existing engine globals this is likely fine
   regardless of thread).
4. **Crash/error-safety of a genuinely bad first test string** — even with the protected-call
   wrapper (`0x6B08E0`'s `luaD_pcall`-style protection should catch a Lua-level runtime error
   gracefully), a wrong native-level assumption (bad `L`, wrong calling convention detail) could
   still crash the process. First live test should be the most trivial possible payload (e.g. a
   no-op or a single global-write) under a debugger, not straight into `SetCameraPosition`.

## Consequence for the project

**This substantially lowers the estimated cost of notes/52's deprioritized "engine-native Lua"
route for first-person** (`SetCameraPosition`/`SetCameraOrientation`/`GetBoneWorldPosition`/
`DumpSkeletonInfo`, per notes/44) — the exec primitive that blocked it appears to be a single
function call away, not a multi-session reversal, PENDING live verification of the four open items
above. Recommend this be the first thing tried in the next live session: capture `g_L` (one-shot
hook on any binding cfunc's entry, per notes/46's plan), then call `0x6B0C00(g_L, "print('psyvr
alive')", 22, NULL)` or similar trivial payload, watching for a log line/crash, BEFORE attempting
anything camera- or entity-related.

## Addresses banked this session (this build; exe non-relocatable)

| Symbol (inferred role) | Address | Confirmed via |
|---|---|---|
| `RunLuaString(L,buf,len,name)` — compile+execute a Lua string | `0x6B0C00` | Full disassembly, structurally coherent |
| `CompileAndLoad(L,buf,len,name)` | `0x6B0C40` | Full disassembly |
| Real parser/compiler entry | `0x6BC550` | Called by `0x6B0C40`, args not further decoded |
| Finish-load / push-closure | `0x6B0B30` | Called by `0x6B0C40`, args not further decoded |
| `ProtectedCall(L,nargs,nresults)` | `0x6B08E0` | Full disassembly, `luaD_pcall`-style structure |
| Protected-call jump-buffer setup | `0x6B0E10` | Called by `0x6B08E0` |
| Inner "perform the call" routine | `0x6B0940` | Called by `0x6B0E10`, unwraps (nargs,nresults) |
| Default chunkname string `"?"` | `0x718410` | Read directly from the binary |
| Precompiled-bytecode signature byte | `0x1B` | Matches `luaB_dostring`'s identical check |

🤖 Pure static disassembly this session; zero game execution, zero debugger attachment, zero files
modified. All addresses read directly from the non-relocatable exe on disk.
