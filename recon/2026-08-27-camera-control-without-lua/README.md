# Free camera control without Lua — `SetCameraPosition` / `SetCameraOrientation` decoded statically

**Date:** 2026-08-27 (dev PC) · **Method:** static disassembly of `Psychonauts.exe`
from the file only — the game was **not** launched and no debugger was attached.

## Why this matters

The user's stated goal for this project (and the others): *"you can freely move
around the game world… i launch the game and you go through the menus… as you
please."* Free camera movement was assumed to be gated behind **Lua exec**
(`0x6B0C00`, statically traced, never live-verified) because
`SetCameraPosition`/`SetCameraOrientation` are listed as Lua bindings in
`tools/lua-bindings.def`.

**It is not.** Both bindings are thin wrappers over plain field writes on a
camera object reachable from the same singleton `SetPendingLevel` already uses.
The whole capability is two `__thiscall` calls and a few float stores.

## The shim layer (why the bindings *looked* Lua-only)

`SetCameraPosition`'s shim at `0x00568FA0` takes **one** argument and calls the
impl at `0x00569000`:

```
00568FA0  push ebp / mov ebp,esp / sub esp,8
00568FAD  movzx eax,byte [0078CBD0]        ; "on lua stack" flag
00568FB8  cmp dword [0078CBCC],0           ; alternate stack pointer
00568FC1  mov [ebp-8],esp                  ; save esp
00568FC4  mov esp,[0078CBCC]               ; SWITCH STACK
00568FD1  mov ecx,[ebp+8]                  ; lua_State* L
00568FD4  push ecx
00568FD5  call 00569000                    ; impl(L)
```

One arg + a stack switch = the classic `int f(lua_State* L)` signature. The impl
then does `push [ebp+8]; call 006AEF20` (a Lua API call), which is what makes
these look like they *require* the interpreter. They don't — that's just the
argument-marshalling half.

## `SetCameraPosition` impl (`0x00569000`) — fully decoded

```
mov eax,[ebp+8] / push / call 006AEF20      ; lua_gettop(L)
push 3 / push 3 / push L / call 005AF750    ; arg-count check (3,3) -> bool
push 0 / push 1 / push L / call 005B0230    ; get number arg 1 -> x
push 0 / push 2 / push L / call 005B0230    ; arg 2 -> y
push 0 / push 3 / push L / call 005B0230    ; arg 3 -> z
mov ecx,[0078BC20]                          ; THE SINGLETON (same as SetPendingLevel)
call 00504220                               ; __thiscall guard -> bool; 0 = bail
mov ecx,[0078BC20]
call 004FA5A0                               ; __thiscall -> camera object
add ecx,8                                   ; camera + 0x08 = position
mov [ecx],edx / mov [ecx+4],eax / mov [ecx+8],edx   ; x, y, z
mov cl,[eax+0x530] / or cl,1 / mov [edx+0x530],cl   ; dirty flag, bit 0
xor eax,eax / ret                           ; 0 Lua return values
```

**There is no further call.** Once past the Lua argument extraction, setting the
camera position is *three float stores and one bit*.

## `SetCameraOrientation` impl (`0x00569220`)

Identical shape — same `lua_gettop`, same `(3,3)` arg check, same three number
reads, same singleton, same guard `0x00504220`, same `GetCamera 0x004FA5A0` —
then:

```
lea ecx,[ebp-0x10] / push ecx                ; the 3 input floats (euler)
lea ecx,[ebp-0x20]                           ; output buffer
call 0069F400                                ; __thiscall convert -> rotation matrix
mov ecx,[0078BC20] / call 004FA5A0           ; camera
add edx,0x20                                 ; camera + 0x20 = orientation matrix
fld/fstp [eax], [ecx+4], [edx+8], …          ; writes the matrix out
```

## The recipe (no Lua)

```c
void  *mgr = *(void **)0x0078BC20;            /* engine singleton */
if (!Guard(mgr))  return;                     /* __thiscall BOOL  @ 0x00504220 */
void  *cam = GetCamera(mgr);                  /* __thiscall void* @ 0x004FA5A0 */

*(float *)((char *)cam + 0x08) = x;           /* position */
*(float *)((char *)cam + 0x0C) = y;
*(float *)((char *)cam + 0x10) = z;
*((unsigned char *)cam + 0x530) |= 1;         /* dirty flag */
```

| thing | address / offset | notes |
|---|---|---|
| engine singleton | `*(void**)0x0078BC20` | same one `SetPendingLevel` uses |
| validity guard | `0x00504220` | `__thiscall BOOL(mgr)` — bail if 0 |
| get camera | `0x004FA5A0` | `__thiscall void*(mgr)` |
| camera position | `camera + 0x08` | 3 × float |
| camera orientation | `camera + 0x20` | 3×3 matrix |
| camera dirty flag | `camera + 0x530` | set bit 0 after writing position |
| euler → matrix helper | `0x0069F400` | `__thiscall(outMatrix, float* euler)` |
| Lua arg-count check | `0x005AF750` | `(L, min, max) -> bool` |
| Lua get-number arg | `0x005B0230` | `(L, index, 0) -> st(0)` |
| Lua stack-switch flag / ptr | `0x0078CBD0` / `0x0078CBCC` | used by every binding shim |

## Caveats — none of this is live-verified yet

Static only. Specifically unconfirmed: that the guard's meaning is "a camera
exists" (assumed from the bail-out), that `camera+0x20` is row-major, the euler
angle order/units the converter expects, and whether the position dirty flag
also needs setting after an orientation write (the orientation path's tail was
not fully dumped). **Position is the safest first live test** — it is the
simplest path and needs no converter.

## Bonus: this generalises

Every one of the **1129** entries in `tools/lua-bindings.def` has this same
two-layer shape (shim → impl → plain engine work). Any binding whose underlying
work is field writes or a `__thiscall` on the singleton can be reached the same
way, without the interpreter. `GetBoneWorldPosition` (`0x005B1630`/`0x005B1690`)
— which the stuck first-person/orientation investigations have been waiting on
(§11's "needs an orientation source") — is the obvious next one to decode.
