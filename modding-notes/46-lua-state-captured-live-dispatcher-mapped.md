# 46 — Live capture: lua_State validated, script dispatcher mapped (step 1b of notes/44/45)

**Date:** 2026-08-19, dev machine, x32dbg + x64dbg-automate MCP against a live `Psychonauts.exe`
at the main menu. No game files modified; debuggee terminated cleanly, splash videos restored.
Follows the static recon in notes/45.

**Result: the in-process Lua state is real, capturable, and structurally confirmed. The function
I had tentatively called "the loader" (0x006C1A40) turns out to be the engine's per-frame script
*dispatcher*, not a clean `lua_dostring` — so the arbitrary-string primitive needs one more
focused dig into the Lua C API. Everything needed to do that is now pinned down.**

## What was confirmed live

- **All static addresses match the running image byte-for-byte** (exe is non-relocatable, as
  notes/45 predicted): `luaB_dostring` @0x006B73B0 and the dispatcher @0x006C1A40 read identical
  bytes live. Hardcoding engine addresses in the DLL is safe for this build.
- **x64dbg session hygiene:** the game lazy-loads many DLLs, each firing a one-time TLS-callback
  breakpoint. Set `[Events] TlsCallbacks=0` / `DllEntry=0` (applies next session), or just `bc`
  (clear breakpoints) and re-arm only the address you want. Breakpoints land at the function's
  first byte *before* the prologue runs, so args are at `[esp+4..]`, not `[ebp+8]` yet.

## The live lua_State (validated by structure)

Breakpoint at 0x006C1A40 entry; `arg1` (=`[esp+4]`, also in `edx`) = **0x05DAB940**. Dumping it:

```
+00 top        = 0x05DABB10   (into the Lua value stack heap block)
+04 base       = 0x05DABAD8
+08 stack      = 0x05DADAD0
+0C stacksize  = 0x00000400   (1024)  <-- unmistakable Lua 4.0 lua_State signature
+10 stack_last = 0x05DABAF8
...
```

`stacksize == 0x400` right after three stack pointers is the Lua 4.0 `lua_State` layout. This is a
genuine, usable state.

**Critical caveat: 0x05DAB940 is a HEAP address — it changes every launch.** The DLL must obtain
L at runtime, two ways:
1. **Runtime capture (preferred):** add a tiny inline hook (or a one-shot breakpoint-style
   trampoline) at any binding/dispatcher entry that snapshots `[esp+4]` into a cached `g_L` the
   first time it runs, then removes itself. Robust, no static-global hunt needed.
2. **Static global:** if the engine stores the main state in a fixed `.data` slot, read
   `*(void**)0xXXXXXXXX`. Not yet located; complicated by the LuaPool/thread design (see below),
   so (1) is the plan of record.

## Why 0x006C1A40 is NOT the string-runner (corrects notes/45)

Disassembled live:

```
0x006C1A40(L, sel, outPtr):
    r = 0x6AF410(L, sel)              ; primary
    if r == 0:  0x6C19B0(L, sel, 3)   ; secondary path
    if outPtr:  *outPtr = 0x6AF470(L, sel)
    return r
```

And the caller (…0x6B46B0):

```
push 1 ; push L ; call 0x006C1A40      ; dispatch selector 1
push 0 ; push 2 ; push L ; call 0x006C1A40   ; dispatch selector 2   <-- our hit
```

`arg2` is a small **selector/slot index (1, 2, …)** the caller iterates — this is the engine
running its Lua script threads per frame, not executing a string. `0x6AF410(L, sel)` descends into
`0x6AF0C0` / `0x6B89E0` (Lua thread/value internals), consistent with the `LuaPool` /
"Lua Threads in Used/Free List" strings: **the engine runs game scripts on pooled coroutine
states**, dispatched by slot. So `luaB_dostring`'s call into 0x006C1A40 uses the same dispatcher
with the string already staged on the Lua value stack — not a `(L, char*)` C entry we can call
directly.

## Consequence for the primitive (narrowed next step)

To run our own script text from the DLL we need the **Lua 4.0 C API**, not the dispatcher:
- Minimal set: `lua_dobuffer(L, buff, size, name)` alone is enough to run a string; or the trio
  `lua_getglobal` + `lua_pushstring` + `lua_call` to invoke the existing global `dostring`.
- These are locatable by breakpointing *inside* `luaB_dostring` (0x006B73B0) while the game runs a
  real `dostring("…")` and single-stepping to the function that first touches the `char*` returned
  by `luaL_check_string` (0x006AEF20) — that callee is `lua_dobuffer` (or its parser). ~15 min,
  one focused debugger session.
- Threading: run our chunk on the **main** state (or a freshly `lua_newthread`'d one), never on a
  pooled thread mid-execution. Drain a command queue from a per-frame hook we already own
  (Present/CandB), on the game thread — Lua 4.0 is not reentrant.

## Confirmed address table (this build)

| Symbol | Addr | Notes |
|---|---|---|
| luaB_dostring (global `dostring`) | 0x006B73B0 | reads Lua stack; not a C(L,char*) entry |
| luaB_dofile (global `dofile`) | 0x006B74B0 | ditto |
| luaL_check_string | 0x006AEF20 | (L) -> const char* |
| script dispatcher | 0x006C1A40 | (L, selector, outPtr) — engine glue, NOT dostring |
| dispatch primary | 0x6AF410 | (L, selector) -> Lua thread/value internals |
| register_fn (all 1129 bindings) | 0x005AF520 | from notes/45 |
| live lua_State (this run only) | 0x05DAB940 | HEAP — changes per launch; capture at runtime |

## Two viable ways forward (for the next monitor session)

**A. Finish the Lua control plane** — one focused debugger dig to pin `lua_dobuffer` + the stack
API, then a `PSYVR_LUA` command channel in the DLL drained on the game thread. Force-multiplier
for everything (camera, bones, visibility, shadows). ~1 session.

**B. Render-level first-person prototype now, no Lua needed** — we already own BuildViewMatrix and
have live bone data (BONEPROBE, notes/44). Identify Raz's head bone, lock the eye to it, hide Raz
via draw-skip/alpha at the render level. This needs zero further RE and is pure monitor work; the
Lua plane makes it *cleaner* later but isn't a prerequisite. Testable on-monitor via eye dumps
(gameplay-entry reliability permitting — see the enter_gameplay drift caveat, notes/43).

Recommendation: **B first** (visible progress toward the user's goal with tools already in hand),
then **A** as the force-multiplier for IK/visibility/shadows.

🤖 Session driven autonomously via Claude Code (live x64dbg capture; clean detach, no files modified).
