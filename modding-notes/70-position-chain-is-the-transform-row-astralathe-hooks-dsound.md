# 70 — The position chain is the node transform's translation row; Astralathe hooks `dsound.dll`, not `d3d9.dll`; the proxy builds on the home PC

**Date:** 2026-09-01, home PC, `/pd` lane. **Static work only — the game was not launched, no
debugger was attached, and nothing here has been run.** No file in the game folder was touched.

Three things moved without a launch:

1. notes/69's player-position chain (`+0x10 -> +0x40`) is now corroborated by a **write** path and
   by a **generic** entity binding, and it has a structural explanation: `+0x40` is row 3 of a 4x4
   local transform that starts at `+0x10`.
2. The Astralathe collision question recorded this morning as "needs ten minutes in a browser" is
   answered from its source: it is a **`dsound.dll` proxy** (no filename clash with our `d3d9.dll`),
   **GPLv3**, and it **IAT-hooks `Direct3DCreate9` and `DirectInput8Create` and vtable-swaps the
   D3D9 device** — the same seams our proxy owns. No file collision; a real functional overlap.
3. `proxy_d3d9.c` **compiles on the home PC** (llvm-mingw is installed here), producing a DLL the
   same size as the committed one. The home PC's installed `d3d9.dll` is the 2026-08-18 build and
   has none of the commands the queued test needs.

---

## 1. `+0x40` is the translation row of a 4x4 at `+0x10` — chain upgraded

notes/69 recorded the tail of the chain, `obj = [player+0x10]`, `pos = obj+0x40`, at
`[inferred-static, n=2]`: `GetPlayerPosition_impl` reads it, and `GetPlayerDist` uses the same
three floats as a position. Three more sightings today, each a different *kind* of evidence.

### 1a. The generic entity getter walks the same two offsets — for ANY entity

`GetPosition_impl` (`0x005C1AA0`) is a one-line tail-call into `GetAbsPosition_impl`
(`0x005C0C70`), which does:

```
obj  = 0x005AFFF0(L, 1, 0, 0)     ; arg 1 -> entity object
node = *(void **)(obj + 0x10)
pos  = (float *)(node + 0x40)     ; x, y, z -> lua_pushnumber x3
```

That is the player chain with `[engine+0x818C]` replaced by "whatever entity Lua handed me". So
`+0x10 -> +0x40` is not a Raz-specific layout; it is how this engine's script-facing entity object
reaches its native node's position, for every entity that has one. `[inferred-static 2026-09-01]`

### 1b. The setter shows WHY `+0x40`: it is row 3 of the node's 4x4 transform

`SetAbsPosition_impl` (`0x005C0EC0`) takes the same `node = [obj+0x10]` and, instead of poking
`+0x40`, calls a `__thiscall` setter — `0x0046CA80(node, &xyz, 3, 0)` on one branch, or directly
`0x0046F1B0(node, &xyz, flag)` on the other; `0x0046CA80` itself ends in `0x0046F1B0`. That setter:

```
0x0046F1F2   lea eax, [this + 0x10]              ; unless [this+0xB8] != 0, see 1c
0x0046F211   mov ecx, 0x10 ; rep movsd           ; copy 16 dwords = one 4x4 matrix
0x0046F281   fstp [local + 0x30]                 ; new x  -> matrix row 3, column 0
0x0046F28E   fstp [local + 0x34]                 ; new y
0x0046F29B   fstp [local + 0x38]                 ; new z
0x0046F2C2   call 0x0046EB20(this, &local, flags); apply the modified matrix
```

`local + 0x30` is `this + 0x10 + 0x30 = this + 0x40`. **So the position the getters read at
`node+0x40` is the translation row of a row-major 4x4 local transform that starts at
`node+0x10`**, and the engine's own setter writes it as exactly that. `[inferred-static 2026-09-01]`

Afterwards `SetAbsPosition_impl` clears `node+0xA0` and calls `0x0046CE10(node, "Lua setpos")`
(string at `0x00711BC4`) — a tagged invalidate/notify. Not needed for reading; noted so nobody
reads `+0xA0` as a position field.

### 1c. A caveat the read side does not have: `+0xB8` is a parent

The setter branches on `[this+0xB8]`: when non-null it converts the requested position through
`0x0046F860`/`0x004136D0` on that parent object before storing. So **`+0x40` is a LOCAL
translation; it equals the world position only when the node has no parent (`+0xB8 == 0`).**
`GetPlayerPosition_impl` reads `+0x40` with no parent check, so the engine itself treats Raz's
node as unparented in normal play — but if `playerpos` ever prints a small, static-looking
position while Raz is visibly riding or attached to something, `+0xB8` is the first thing to
print. `[inferred-static 2026-09-01]` — the parent interpretation of `+0xB8` is from the setter's
shape alone (n=1), weaker than the rest of this section.

### Confidence after today

| Claim | Before | Now |
|---|---|---|
| `[engine+0x818C]` = player object | `[inferred-static, n=4]` | unchanged |
| `[player+0x10]` = native node, `node+0x40` = position (x,y,z) | `[inferred-static, n=2]` | **`[inferred-static 2026-09-01, n=4]`** — two reads (`GetPlayerPosition`, `GetAbsPosition`), one independent use (`GetPlayerDist`), one **write** (`SetAbsPosition` via `0x0046F1B0`), plus a structural explanation (row 3 of the 4x4 at `+0x10`) |
| `node+0x10` = 4x4 local transform, row stride 0x10 | new | `[inferred-static 2026-09-01, n=1]` — from the setter's copy-and-patch alone |
| `node+0xB8` = parent, non-null means `+0x40` is local | new | `[inferred-static 2026-09-01, n=1]` |

Still true: **nothing has been read from a running game.** The `playerpos` test remains the
verification, and it is unchanged from notes/69 — plausible numbers, change when Raz walks, do
**not** change when only the camera turns.

**What would show the derivation is wrong rather than a knob needing tuning:** `playerpos` printing
values that track the camera exactly (then `+0x818C` is not the player), or NaN/denormals/huge
magnitudes (then `+0x10` is not a node or `+0x40` is not floats). Values that are plausible but
offset from where Raz visibly stands by a fixed vector point at `+0xB8` (parent) instead.

---

## 2. Astralathe: what it hooks, what it is licensed under

`/gr` recorded this morning that Astralathe's GitLab renders client-side and could not be read.
The **GitLab REST API returns plain JSON and raw files**, which is how this was read — no browser,
no clone. Project id `34250039`, path `scrunguscrungus/astralathe`, default branch `master`, last
activity 2026-06-04. Everything below is `[reported 2026-09-01]` in the sense that it is read from
the project's own source, not observed running.

### 2a. Not `d3d9.dll` — `dsound.dll`

The repository's `AstralatheAutohook/` project contains `dsound.def` and a `dllmain.cpp` that
builds `GetSystemDirectoryA() + "\\dsound.dll"`, forwards twelve DirectSound exports
(`DirectSoundCreate`, `DirectSoundCreate8`, `DirectSoundEnumerateA/W`, `DirectSoundCapture*`,
`DirectSoundFullDuplexCreate`, `DllCanUnloadNow`, `DllGetClassObject`, `GetDeviceID`), and — only
when the process name lower-cases to `psychonauts.exe` and its `CONFIG_AUTOHOOK` setting is on —
`LoadLibraryA("Astralathe.dll")`. The packing list `release_files_to_pack.txt` ships **`dsound.dll`,
`Astralathe.dll`, `AstralatheSteam.dll`, `PsychoPortal.dll`, `AstralatheLauncher.exe`,
`Astralathe_CobwebDuster.exe`** and support files. `AstralatheLauncher.exe` is the fallback when
AutoHook is disabled. **There is no `d3d9.dll` in its shipped set.** So dropping Astralathe next to
our proxy does **not** overwrite or shadow our file.

### 2b. But it hooks the same seams our proxy owns

`Astralathe/DXHooks.cpp` (PolyHook2):

| Astralathe hook | Mechanism | Our proxy at the same seam |
|---|---|---|
| `Direct3DCreate9` — `PLH::IatHook("d3d9.dll", ...)` on the exe's import table | IAT detour | **We ARE that import** — the exe's `d3d9.dll!Direct3DCreate9` resolves to our DLL |
| `IDirect3D9::CreateDevice` | vtable swap on the returned `IDirect3D9` | we patch the same vtable slot (16) |
| `IDirect3DDevice9::EndScene`, `::Reset` | vtable swap on the device (ImGui overlay; ImGui teardown/reinit) | we wrap the device |
| `DirectInput8Create` — `PLH::IatHook("dinput8.dll", ...)` | IAT detour | we IAT-patch `DirectInput8Create` too (notes/65 path) |
| `IDirectInputDevice8A::GetDeviceState`, `GetDeviceData`, `SetCooperativeLevel` | vtable swap (suppress input while ImGui has focus) | we hook `GetDeviceState` (and `GetDeviceData` is our untested continuation) |

So the collision is **functional, not by filename**: two independent parties re-pointing the same
IAT entries and the same COM vtables, in an order that depends on DLL load order (`dsound.dll` and
`d3d9.dll` both load at process start as exe imports). It would probably *run* — an IAT hook over
an import that already resolves to our export simply wraps us — but it is exactly the fragile
stack the estate's one-writer rule exists to avoid, and any misbehaviour would be undiagnosable.
**Verdict stands: do not install Astralathe into the working folder. On a separate copy of the
game it is a fine observation instrument.**

Also relevant: Astralathe's in-game menu is on **F10**, which is why our hotkeys are numpad-only
(and why F10 is a bad key in general — it is `WM_SYSKEYDOWN`).

### 2c. Licence: GPLv3

`LICENSE` at the repository root is the GNU General Public License, Version 3. Under this
project's rules a third-party non-tool mod is study-and-reimplement anyway; GPLv3 makes that a
hard requirement rather than a preference — **no code, no headers, no signature strings copied**.
Reading its `Psychonauts/*.h` class layouts to *compare* offsets is fine (interface metadata);
copying them is not. For the record, its `EEntity.h` is nearly empty (a 12-byte pad and a
script-object pointer) and its `ECamera.h` has no fields, only two byte-pattern signatures — it
would not have shortcut notes/69 even if the licence allowed it.

### 2d. What it does that we recorded as open

The source tree confirms the feature claims from the `/gr` topic: `ImGui/LuaPad.cpp` and
`ImGui/DebugConsole.cpp` (the in-game Lua console), `Psychonauts/DebugDraw/EDebugLineManager.cpp`
(restored debug drawing), `Psychonauts/EScriptVM.cpp` (its own view of the Lua VM we mapped in
notes/57), `Psychonauts/ERenderer.cpp` / `ERenderState.cpp` / `ECamera.cpp`. Its widescreen support
means it already touches projection. None of it changes our plan — notes/69 removed the Lua-exec
dependency — but if a live Lua console is ever wanted for exploration, a working one exists on a
separate install.

---

## 3. The proxy builds on the home PC; the installed DLL here is three weeks old

`build.ps1` looks for LLVM-MinGW's `i686-w64-mingw32-clang.exe` in the winget package path; **it is
present on the home PC** (`llvm-mingw-20260616-ucrt-x86_64`, clang 22.1.8). Compiling
`proxy_d3d9.c` + `proxy_d3d9.def` + the vendored `openvr_api.lib` exactly as `build.ps1` does, but
into the scratch directory so the committed binary was not touched, produced a **208,896-byte
`d3d9.dll` with the same two pre-existing warnings notes/69 reports** (`EXTERN_C` redefinition,
`dllexport` on redeclaration) — the same size as the committed 2026-09-01 build.
`[compile-verified 2026-09-01, home PC]`. The home PC can therefore rebuild, not just test.

What is installed in `C:\Steam\steamapps\common\Psychonauts\` today (read, not modified):

| | installed `d3d9.dll` | committed `dev-archive/tools/proxy-d3d9/d3d9.dll` |
|---|---|---|
| Size / date | 146,944 B, 2026-08-18 15:12 | 208,896 B, 2026-09-01 |
| Exports | `Direct3DCreate9` (+`@4` alias) | same |
| `playerpos` / `fpcam` / `fpheight` / `fpforward` / `fpaxis` | **absent** | present |
| `camfollow` / `camfollowscale` / `cambasisyaw` | **absent** | present |
| `level` / `campos` / `camhold` / `padaxis` (automation harness) | **absent** | present |
| `PSYVR_AUTOMATION` / `psyvr_automation_cmds` | **absent** | present |
| `GetForegroundWindow` IAT patch (auto-pause fix, notes/65) | **absent** | present |
| `PSYVR_FOV_SCALE` / `PSYVR_FAKE_POSE` | present | present |
| `openvr_api.dll` beside it | 631,960 B (identical size to committed) | 631,960 B |

The folder also has **no `Launch-Psychonauts-Automation.bat`**, only `Launch-Psychonauts-VR.bat`
from 2026-08-19. So the STATUS.md open action is exactly right: **nothing in the queued
`playerpos` test can run on this machine until the committed DLL and the automation launcher are
copied in.** That copy is the user's decision (headset-verified install), and this note does not
make it. What the copy changes is now enumerated above: it adds commands and the auto-pause fix,
changes no export, and keeps `openvr_api.dll` as is.

---

## 4. Not done, deliberately

- Nothing deployed here. The user reserved that.
- `0x0046EB20` (the apply-matrix routine the setter ends in) was not decoded; it is not needed to
  read a position.
- Astralathe's `Astralathe.cpp` / `HookDef.cpp` (which engine functions it detours, beyond D3D and
  DirectInput) were not read; the D3D/DirectInput overlap already settles the install question.
- The eye-height measurement is still a live item (camera position minus `playerpos` on flat ground).

🤖 Static disassembly of `Psychonauts.exe` and a read of Astralathe's public source via the GitLab
API only. The game was not launched; no game file was modified.
