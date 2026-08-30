# 50 — Lua 4.0 C-API static map: `dostring` decoded, exec-primitive cost revised UP

**Date:** 2026-08-20, dev machine. **Pure static** disassembly of `Psychonauts.exe` on disk
(`llvm-objdump -d -M intel`, exe non-relocatable so VA==disassembly addr). No game running, no
debugger, no files modified. Goal: finish the Lua string-exec primitive (the "master key" for
`DumpSkeletonInfo` → Raz's head/shoulder bone index, which is the notes/49 blocker for a
shoulder-anchored, stable first person).

## Headline

The static dig **corrects two mislabels from notes/46 & 48** and shows the arbitrary-string exec
primitive is **not the ~15-min dig notes/46 implied** — it's a multi-function reversal of the
Lua 4.0 C API. `luaB_dostring` is a self-contained *compile-then-run*, not a callable
`lua_dobuffer(L, char*, len, name)`. Getting our own text to run needs either (a) a real
`lua_pushstring` to stage the arg then call `luaB_dostring`, or (b) decoding the compile+run pair
to feed a raw `char*`. Both are more work + carry VM-call/reentrancy risk. This revises the cost
estimate the user's "finish Lua" choice was based on.

## Corrected function identities (supersede notes/46 & 48 where they conflict)

| Addr | notes/46-48 called it | **Actually is** | Evidence |
|---|---|---|---|
| `0x6AEF20` | "luaL_check_string / arg0-self getter → ptr" | **`lua_gettop(L)`** → arg **count** | body computes `(L->top - L->base) >> 3` (TObject=8B); returns an integer, not a ptr |
| `0x6C1A40` | "per-frame script **dispatcher** (L, selector, outPtr)" | **arg-string getter** `luaL_check_lstr(L, idx, &len)` | `dostring` calls `f(L, 1, &len)`, then type-tag-checks the result and errors "cannot run pre-compiled code" if tag==0x17 |
| `0x6B0CF0` | — | **error reporter** (sets `_ERRORMESSAGE`, raises) | pushes const `"_ERRORMESSAGE"` (@0x711e98) via the interner, then raises |
| `0x6BB980` | — | **`luaS_new(L, const char*)`** string interner → TString* | called `(L, "_ERRORMESSAGE")`, result written as an 8-byte TObject to `L->top` |

(The notes/46 "dispatcher (L, selector 1/2/…)" observation was from a *different* live caller
`0x6B46B0`; statically, `dostring`'s use of `0x6C1A40` is plainly the string-arg getter. Both
uses may share the routine, but for our purposes `0x6C1A40(L,1,&len)` = "get string arg 1".)

## `luaB_dostring` @ 0x6B73B0 decoded (it's `int f(lua_State*)`, no coroutine shim)

```c
int luaB_dostring(lua_State *L){
    int n     = lua_gettop(L);                 // 0x6AEF20
    TObject *v= luaL_check_lstr(L, 1, &len);   // 0x6C1A40 -> arg1 as string+len
    if (v->tag == 0x17)                        // pre-compiled chunk?
        error(L, "`dostring' cannot run pre-compiled code");  // 0x6B0CF0
    proto = compile(L, 2, s, 0, n);            // 0x6C1AA0  (args not fully decoded)
    r     = run(L, s, len, proto);             // 0x6B0C00  (ditto)
    return finish(L, r);                       // 0x6B7440
}
```

Prologue is a plain `push ebp; mov ebp,esp` reading `L` at `[ebp+8]` — **no coroutine
stack-switch shim** (base-lib funcs aren't registered through `0x5AF520`), so `0x6B73B0` is
directly callable as `int(*)(lua_State*)`. The catch: it reads its script from **Lua stack index
1**, so we must first *push* our script string there.

## What a working primitive still needs (the honest remaining list)

1. **`lua_pushstring`/`lua_pushlstring`** — to stage our script at stack index 1 before calling
   `luaB_dostring`. Not yet pinned (the pushes seen so far are specialised: `0x6B0D20` pushes the
   fixed `_ERRORMESSAGE` name). *Or* skip pushstring by decoding the compile(`0x6C1AA0`) +
   run(`0x6B0C00`) pair to accept our own `char*`+len directly — needs their arg layouts decoded
   (partially reversed, tangled with lua_tonumber/tostring helpers `0x6AF080`/`0x6AF380`).
2. **Runtime `g_L` capture** — heap ptr, changes per launch (notes/46). Inline hook snapshots
   `[esp+4]` on first binding call.
3. **Game-thread command pump** — Lua 4.0 is non-reentrant; drain a queue from a hook we own
   (Present/CandB), never mid-dispatch.

Banked-solid addresses this build: `lua_gettop 0x6AEF20`, `luaL_check_lstr 0x6C1A40`,
`luaS_new 0x6BB980`, error reporter `0x6B0CF0`, `luaB_dostring 0x6B73B0`, `luaB_dofile 0x6B74B0`,
compile `0x6C1AA0`, run `0x6B0C00`, TObject size = 8 (tag@+0, value@+4), `L->top`=`*L`,
`L->base/Cbase`=`*(L+0x10)`.

## Consequence for today's route (decision surfaced to user)

The exec primitive is a genuine **multi-session** RE lift with VM-call risk — matching notes/48's
honest assessment, and *contradicting* notes/46's "~15 min" framing that the "finish Lua" choice
leaned on. Meanwhile the **actual goal** (shoulder-anchored, stable FP on the monitor) is blocked
only on **Raz's head/shoulder bone index** (notes/49), which can be obtained **empirically from
the live c96 bone-probe with zero VM risk** — pure monitor work, faster and safer. Recommending we
get the bone index empirically *now* to unblock the shoulder anchor, and keep the Lua plane as the
longer-horizon force-multiplier (this static map is real progress banked toward it, and de-risks
the eventual live dig by naming exactly which funcs to validate).

🤖 Pure static disassembly via Claude Code; no game files touched, no debugger, no code changed.
