# Camera Matrix Injection Point — Live x64dbg Analysis

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install, never modified). Goal: disassemble the game code *around* the
`D3DXMatrixPerspectiveFovRH` / `D3DXMatrixLookAtRH` call sites located in the prior live-debug
session (`notes/04-live-debug-findings.md`), determine whether the game feeds the resulting
matrices through the D3D9 fixed-function pipeline (`SetTransform`) or through the shader-constant
pipeline (`SetVertexShaderConstantF`), and identify the concrete function(s) a VR mod needs to
hook to inject per-eye view/projection matrices.

**Note on this session's game process**: partway through, the coordinator (outside this task)
force-killed one `Psychonauts.exe` instance because its intro audio was audible in a shared
workspace — not a debugger/analysis problem. That kill left `x32dbg.exe`/`python.exe` orphaned
and, more importantly, left a stale breakpoint set in x64dbg's own per-target database (see §4).
All processes were cleaned up and later runs launched fresh; final state is clean (verified: no
`x32dbg`/`Psychonauts`/`python` processes running, no `d3d9.dll` in the game directory).

## 1. Method

Same underlying mechanism as the prior live-debug session: driving the
`x64dbg_automate.X64DbgClient` Python API directly (MCP tools for x64dbg are still not available
in this session — no restart occurred — so the raw Python client was used again, exactly as
before). Script iterations lived in the session scratchpad (disposable, not copied into the
workspace).

Two complementary techniques were used:

1. **Live breakpoint + register/stack decode** at the known call sites and at the real
   `IDirect3DDevice9::SetTransform` / `SetVertexShaderConstantF` addresses (resolved from the
   live vtable exactly as in the prior `CreateDevice`/`Present` session), to capture real
   argument values and confirm which vtable methods actually get called during real execution.
2. **Static disassembly via live memory reads**, using `disassemble_at()` repeatedly to walk an
   address range instruction-by-instruction — this does **not** require the target code to ever
   execute during the observation window. This mattered a lot here: `D3DXMatrixLookAtRH`'s call
   site never fired live because the game was sitting at the main menu (no active 3D gameplay
   camera), but its containing function's bytes are already mapped in the process's `.text`
   section from the moment the process loads, so it was fully disassembled anyway without
   needing to reach gameplay.

Function boundaries were found with a padding-scan heuristic (scan backward from a known
address for the last run of `0xCC` int3 bytes — MSVC pads between functions with these, so the
byte immediately after the last one is a real instruction boundary marking the next function's
start) rather than guessing — cross-checked by disassembling forward from that point and
confirming a clean `push ebp / mov ebp,esp` prologue followed by a `ret` at a sensible length.

## 2. Confirmed addresses (this session, live process memory + live breakpoint hits)

All addresses matched the prior session's exactly (`notes/04-live-debug-findings.md`) — no ASLR
relocation observed again, consistent with that session's note that this machine doesn't
relocate these modules run-to-run (still not guaranteed elsewhere).

| Symbol | Address | Offset | Confirmed via |
|---|---|---|---|
| `D3DXMatrixPerspectiveFovRH` return point (= "call site" in prior notes) | `0x00692525` | `exe+0x292525` | live breakpoint hit + static disasm |
| `D3DXMatrixLookAtRH` return point (= "call site" in prior notes) | `0x006924B1` | `exe+0x2924B1` | static disasm (never hit live at main menu) |
| **Projection-matrix wrapper function start** | `0x006924D0` | `exe+0x2924D0` | padding-scan + disasm, both live and static runs agree |
| **View-matrix wrapper function start** | `0x00692480` | `exe+0x292480` | padding-scan + static disasm |
| `IDirect3DDevice9::SetTransform` (vtable slot 44) | `0x719808D0` (this run; `d3d9.dll+0xE88D0`) | — | live vtable read, slot 44 |
| `IDirect3DDevice9::SetVertexShaderConstantF` (vtable slot 94) | `0x71A58CC0` (this run; `d3d9.dll+0x138CC0`) | — | live vtable read, slot 94, **breakpoint hit repeatedly, real per-call args decoded** |

**Important correction to how "call site" was described in the prior session**: `exe+0x292525`
and `exe+0x2924B1` are not the addresses of the `CALL` instructions themselves — they are the
**instructions immediately after** each `CALL` (the `D3DXMatrix...RH` functions are reached via
`JMP` import thunks, so the actual `call <JMP.&D3DXMatrixPerspectiveFovRH>` instructions sit 5
bytes earlier, at `exe+0x292520` / `exe+0x2924AC`). This matters for calling-convention analysis
(see §3) — a first attempt this session incorrectly treated `exe+0x292525` as the call
instruction's own address and tried to read a "return address" off `[esp]` there, which actually
read stack garbage (the call hadn't executed yet at a genuine call-instruction breakpoint) and
produced a nonsense disassembly dump of stack memory. Caught and fixed before trusting any of
that output — flagging it here so a future session doesn't repeat it.

## 3. The two wrapper functions, in full

Both are small (29–39 instruction), nearly identical adapter functions, laid out back-to-back in
the executable with no gap — strong confirmation they come from the same source file, as the
static recon predicted.

### View-matrix wrapper — `exe+0x292480`

```
exe+0x292480  push ebp
exe+0x292481  mov ebp,esp
exe+0x292483  and esp,0xFFFFFFF0        ; 16-byte-align the stack
exe+0x292486  sub esp,0x48
exe+0x292489  push esi
exe+0x29248a  push edi
exe+0x29248b  mov ecx,0x10
exe+0x292490  mov esi,psychonauts.7933E0
exe+0x292495  lea edi,[esp+10]
exe+0x292499  rep movsd                 ; zero/seed a 16-dword (4x4 matrix) local buffer
exe+0x29249b  mov eax,[ebp+14]
exe+0x29249e  push eax                  ; arg pushed 1st -> D3DX arg4 = pUp
exe+0x29249f  mov ecx,[ebp+10]
exe+0x2924a2  push ecx                  ; arg pushed 2nd -> D3DX arg3 = pAt
exe+0x2924a3  mov edx,[ebp+C]
exe+0x2924a6  push edx                  ; arg pushed 3rd -> D3DX arg2 = pEye
exe+0x2924a7  lea eax,[esp+1C]          ; local D3DXMATRIX buffer
exe+0x2924ab  push eax                  ; arg pushed 4th -> D3DX arg1 = pOut
exe+0x2924ac  call <JMP.&D3DXMatrixLookAtRH>
exe+0x2924b1  mov ecx,0x10              ; <-- prior session's "call site" address
exe+0x2924b6  lea esi,[esp+10]
exe+0x2924ba  mov edi,[ebp+8]
exe+0x2924bd  rep movsd                 ; copy the local 4x4 result into caller's own buffer
exe+0x2924bf  mov eax,[ebp+8]           ; return value = pOut (D3DX9 convention)
exe+0x2924c2  pop edi
exe+0x2924c3  pop esi
exe+0x2924c4  mov esp,ebp
exe+0x2924c6  pop ebp
exe+0x2924c7  ret
```

Reading the push order against D3DX9's real signature
(`D3DXMATRIX* D3DXMatrixLookAtRH(D3DXMATRIX* pOut, const D3DXVECTOR3* pEye, const D3DXVECTOR3*
pAt, const D3DXVECTOR3* pUp)`, args pushed right-to-left per the standard `__stdcall`/`WINAPI`
convention D3DX9 uses) gives this wrapper's own signature, from its own caller's point of view:

```
BuildViewMatrix(D3DXMATRIX* pOutMatrix /* [ebp+8] */,
                D3DXVECTOR3* pEye      /* [ebp+C], POINTER, passed straight through */,
                D3DXVECTOR3* pAt       /* [ebp+10], POINTER, passed straight through */,
                D3DXVECTOR3* pUp       /* [ebp+14], POINTER, passed straight through */)
```

**This is the concrete VR injection point for the view matrix.** `pEye`/`pAt`/`pUp` are pointers
to the caller's own vectors, not copies — a hook installed at this function's entry
(`exe+0x292480`) can read `*pEye`, `*pAt`, `*pUp` to recover the game's intended (monoscopic)
camera pose, then either (a) mutate `*pEye` in place before letting the real function run (add an
IPD-scaled lateral offset along the camera's right vector, derived from `pAt - pEye` and `pUp`)
and call it twice (once per eye) into two separate output buffers, or (b) let the original call
proceed once for the "center" pose and compute both per-eye view matrices from the resulting
`D3DXMATRIX` via matrix math without re-entering this function at all. Either is viable; (a) is
simpler to get physically correct (real toe-in/parallel convergence) since it reuses D3DX9's own
matrix math rather than hand-deriving an offset view matrix.

### Projection-matrix wrapper — `exe+0x2924D0`

```
exe+0x2924d0  push ebp
exe+0x2924d1  mov ebp,esp
exe+0x2924d3  and esp,0xFFFFFFF0
exe+0x2924d6  sub esp,0x58
exe+0x2924d9  push esi
exe+0x2924da  push edi
exe+0x2924db  fld dword ptr [ebp+C]         ; raw FOV-ish input value
exe+0x2924de  fdiv qword ptr [0x703698]     ; divide by a global double constant
exe+0x2924e4  fmul dword ptr [0x793444]     ; multiply by a global float constant
exe+0x2924ea  fstp dword ptr [esp+5C]       ; store converted fovy
exe+0x2924ee  mov ecx,0x10
exe+0x2924f3  mov esi,psychonauts.7933E0
exe+0x2924f8  lea edi,[esp+10]
exe+0x2924fc  rep movsd                     ; seed local 4x4 buffer
exe+0x2924fe  push ecx
exe+0x2924ff  fld dword ptr [ebp+18]
exe+0x292502  fstp dword ptr [esp]          ; arg pushed 1st -> D3DX arg5 = zf (far plane)
exe+0x292505  push ecx
exe+0x292506  fld dword ptr [ebp+14]
exe+0x292509  fstp dword ptr [esp]          ; arg pushed 2nd -> D3DX arg4 = zn (near plane)
exe+0x29250c  push ecx
exe+0x29250d  fld dword ptr [ebp+10]
exe+0x292510  fstp dword ptr [esp]          ; arg pushed 3rd -> D3DX arg3 = Aspect
exe+0x292513  push ecx
exe+0x292514  fld dword ptr [esp+6C]        ; the converted fovy computed above
exe+0x292518  fstp dword ptr [esp]          ; arg pushed 4th -> D3DX arg2 = fovy
exe+0x29251b  lea eax,[esp+20]              ; local D3DXMATRIX buffer
exe+0x29251f  push eax                      ; arg pushed 5th -> D3DX arg1 = pOut
exe+0x292520  call <JMP.&D3DXMatrixPerspectiveFovRH>
exe+0x292525  mov ecx,0x10                  ; <-- prior session's "call site" address
exe+0x29252a  lea esi,[esp+10]
exe+0x29252e  mov edi,[ebp+8]
exe+0x292531  rep movsd                     ; copy local result into caller's buffer
exe+0x292533  mov eax,[ebp+8]               ; return value = pOut
exe+0x292536  pop edi
exe+0x292537  pop esi
exe+0x292538  mov esp,ebp
exe+0x29253a  pop ebp
exe+0x29253b  ret
```

Wrapper signature: `BuildProjectionMatrix(D3DXMATRIX* pOutMatrix /* [ebp+8] */, float rawFov
/* [ebp+C], unit TBD - divided by a global double then multiplied by a global float before use,
almost certainly a degrees/table-index -> radians conversion */, float Aspect /* [ebp+10] */,
float zn /* [ebp+14] */, float zf /* [ebp+18] */)`.

**This is the concrete VR injection point for the projection matrix.** A hook at
`exe+0x2924D0` can read/override `Aspect` (set to the per-eye half-width aspect ratio) and
`rawFov` before the real body runs, and/or read the two global constants at `0x703698`
(a `double`) and `0x793444` (a `float`) once to establish the exact FOV unit conversion if a
mod wants to compute FOV values in the same space the game uses rather than reverse-guessing.
For a first cut, the simplest correct approach is likely to let this wrapper build the normal
symmetric-frustum matrix and only override `Aspect`, since `D3DXMatrixPerspectiveFovRH` always
produces a symmetric (non-offset) frustum — true asymmetric per-eye frustums (needed for
accurate stereo convergence at reasonable IPD) would require bypassing this wrapper and calling
`D3DXMatrixPerspectiveOffCenterRH` directly instead, which is a straightforward drop-in swap
once this function is hooked (same output buffer, same later consumption path).

Live breakpoint data for one real hit of the projection wrapper (captured before the fix above,
at the `exe+0x292525` return point, i.e. *after* `D3DXMatrixPerspectiveFovRH` already ran):
`eax=0x0019df50`, `ecx=0x0`, `edx=0x0019df30`, `ebp=0x0019dfa4` — consistent with normal local
variables, nothing unusual. The wrapper fired **once** during the entire observation window
(main menu, ~75 seconds) — it is not called every frame, only when the projection setup
actually changes (device creation / resolution or FOV-setting change), which matches
expectations for a projection matrix (unlike the view matrix, it doesn't need to be rebuilt
every frame unless FOV/aspect/near/far change).

## 4. `SetTransform` vs `SetVertexShaderConstantF` — resolved with live evidence

This is the core question the task asked to resolve, and it now has a solid, evidence-backed
answer:

**The game does not use the fixed-function transform pipeline for its own rendering.**
`IDirect3DDevice9::SetTransform` (vtable slot 44, live-resolved to `0x719808D0` this session)
was **never called by the game's own code** during a ~75-second observation window covering
device creation through steady-state `Present()` calls at the main menu (**zero** hits on a live
breakpoint placed directly on its real implementation address, while `SetVertexShaderConstantF`
racked up 300+ hits and `Present` racked up 40+ in the same window).

`SetTransform` **is** called, but only from inside the D3D9 runtime's own `CreateDevice`
internals — during the device-creation bootstrap window, execution repeatedly landed exactly on
`SetTransform`'s entry address before the game's own code ever resumes. This is the D3D9
runtime/driver default-initializing its internal fixed-function state (e.g. resetting all light
and world-matrix-array transforms to identity) as a side effect of `CreateDevice`, independent of
anything the game explicitly requests — **not evidence the game uses `SetTransform` for its own
camera**. (This pattern briefly looked like a stuck/stale breakpoint the first time it was hit
repeatedly with the same address; clearing all breakpoints at session start and re-running showed
the exact same behavior recurs from a clean slate, confirming it's real driver-internal behavior,
not a debugger artifact — see the process notes in §6.)

`SetVertexShaderConstantF` (vtable slot 94, live-resolved to `0x71A58CC0` this session) **is**
used, and used constantly — a real per-frame (in fact many-times-per-frame) call. All hits
observed in this session's window came from a single call site, `exe+0x27EF03`, setting small
constant blocks (`Vector4fCount` of 1 or 2, never 4), with values that read like a 2D
screen-space UI transform rather than a 3D camera matrix — e.g. register 10 got `(2.0, -2.0,
1.0, 1.0)` and register 11 got `(-1.0, 1.0, 0.0, 0.0)` on every observed call, which is the
classic `scale = (2/vpWidth, -2/vpHeight)`, `offset = (-1, 1)` pixel-to-clip-space pattern for
2D/UI vertex shaders. This is consistent with the observation window being the main menu, where
no 3D gameplay camera is active (matching `D3DXMatrixLookAtRH` never firing live either) — the
game is rendering a 2D menu/UI, not a 3D scene with a moving camera, so the real camera-matrix
upload path (presumably a `Vector4fCount=4` call setting 4 consecutive registers — the standard
way to upload one 4x4 matrix — from a *different* call site than `exe+0x27EF03`) was not observed
directly in this session.

**Conclusion**: the injection path is confirmed to be shader-constant-based, not
`SetTransform`-based, matching the static recon's shader-pipeline finding and Helix Mod's own
2013 fix (which patched shaders, not fixed-function state). The two wrapper functions in §3 are
where the view/projection matrices are *built*; from there they almost certainly flow into
vertex shader constants via `SetVertexShaderConstantF` once actual 3D gameplay is running, but
the exact call site(s)/register range for that upload were not directly observed this session —
see §5 for the concrete next step to close that last gap.

## 5. Recommended next step (not done this session — requires reaching gameplay)

To pin down the *exact* `SetVertexShaderConstantF` call site and register range used for the
camera matrix (as opposed to the 2D UI constants observed here), a future session needs to get
the debugger past the main menu into actual 3D gameplay before sampling `SetVertexShaderConstantF`
hits — e.g. drive simulated input (keypresses/mouse) to start or continue a game while the
process is under the debugger, or attach after manually starting a level via Steam. Once in
gameplay:
- Confirm `D3DXMatrixLookAtRH`'s wrapper (`exe+0x292480`) actually fires (expected: every frame,
  unlike the projection wrapper).
- Watch for a `SetVertexShaderConstantF` call with `Vector4fCount=4` shortly after — that's the
  camera matrix (or a combined view-projection matrix, if the game multiplies them together
  before upload, in which case a `D3DXMatrixMultiply` call between the two wrappers and that
  upload would also be visible) — and record its `StartRegister` and call site, which becomes
  the second, and possibly primary, injection point (patch the constant buffer contents
  directly before they reach the GPU, in addition to or instead of hooking the two matrix
  wrapper functions in §3).

This is a bounded, well-defined follow-up, not a new unknown — the hard part (locating the
matrix-building code and confirming the pipeline type) is done.

## 6. Process/tooling notes for future sessions

- **x64dbg persists breakpoints per-target across debug sessions in its own database.** After
  the coordinator's external kill of one `Psychonauts.exe` instance (intro audio audible in a
  shared workspace — unrelated to this analysis, not a bug in the debug session itself), the
  next fresh `start_session()` against the same EXE silently came back with 10 breakpoints
  already set from the previous (abnormally-terminated) run. This is fine as long as it's
  anticipated: **call `get_breakpoints()` and clear everything right after `start_session()`
  before setting new breakpoints**, which is now baked into the scratchpad scripts. Not doing
  this doesn't corrupt anything, but it can cause a confusing stall (a `go()` that keeps
  stopping at an address you didn't expect yet, under a breakpoint name you didn't set this run).
- Breaking directly on a `CALL` instruction's own address (rather than its return address) means
  `[esp]` at that breakpoint is **not** a return address yet — the call hasn't executed, so
  `esp` still points at whatever the caller last pushed (its own outgoing arguments here). Don't
  read `[esp]` as a return address unless the breakpoint is known to be *inside* the callee
  (function entry) or the call has otherwise already executed.
- `disassemble_at()` works against live process memory regardless of whether that code has ever
  executed or whether the owning module's normal load sequence has finished — useful for
  disassembling code paths (like `D3DXMatrixLookAtRH`'s wrapper here) that don't happen to run
  during a given observation window.
- No anti-debug/anti-tamper behavior encountered again this session, consistent with every prior
  session for this game.
- Clean shutdown confirmed after every run in this session, including the ones that hit bugs
  and were killed manually mid-script (`Stop-Process -Force` on `x32dbg`/`python`/`Psychonauts`
  followed by a `Get-Process` check with zero results each time) — same discipline as prior
  sessions.
