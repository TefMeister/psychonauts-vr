# Live Debug Findings — x64dbg / x64dbg Automate

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install, never modified). This session installed the debugger toolchain the
prior session flagged as a blocker, then did the first live/dynamic analysis pass.

## 1. Toolchain installed

| Component | Method | Result |
|---|---|---|
| **x64dbg** (debugger, 2026.05.27) | `winget install x64dbg.x64dbg` | Installed to `C:\Users\Tefa\AppData\Local\Microsoft\WinGet\Packages\x64dbg.x64dbg_Microsoft.Winget.Source_8wekyb3d8bbwe\release\` (`x32\x32dbg.exe` used throughout, since the target is 32-bit) |
| **Python 3.12.10** (real interpreter, not the Store alias stub) | `winget install Python.Python.3.12` | `C:\Users\Tefa\AppData\Local\Programs\Python\Python312\python.exe` — the bare `python`/`pip` commands on PATH still resolve to the Store alias stub first in this shell; the plugin's own scripts should invoke the full path, or PATH order needs fixing in a future session |
| **`x64dbg_automate[mcp]`** (pip, v0.9.2) | `python.exe -m pip install "x64dbg_automate[mcp]" --upgrade` | Confirmed via `pip show x64dbg_automate`. Pulled in `mcp`, `pyzmq`, `pydantic`, `pywin32`, etc. |
| **x64dbg-automate plugin** (the debugger-side ZMQ server — **not mentioned in the plugin's own README prerequisites, but required** — nothing under `plugins/` ships with vanilla x64dbg) | Downloaded `release32-0.8.1-ghost_fungus.zip` from `github.com/dariushoule/x64dbg-automate/releases` (via GitHub API, latest release), extracted `x64dbg-automate.dp32` + `libzmq-mt-4_3_5.dll` into `release\x32\plugins\` | Version tag `ghost_fungus` matches the pip client's `COMPAT_VERSION` constant exactly — confirmed compatible |
| **x64dbg MCP server registration** | `claude mcp add --scope user x64dbg -e X64DBG_PATH=<x32dbg.exe> -- <x64dbg-automate-mcp.exe>` | Registered in user-scope Claude Code config. **Not usable in the session that registered it** — Claude Code only picks up new MCP servers on restart. Verified separately via `claude mcp list` → `x64dbg: ... - √ Connected`, so it will be live for the *next* session/restart. |

**Real blocker not mentioned anywhere in the plugin's own docs**: the x64dbg-skills README lists
"x64dbg and x64dbg Automate installed" as one prerequisite bullet, implying x64dbg Automate
ships with or alongside x64dbg. It does not — it's a **separate GitHub project**
(`dariushoule/x64dbg-automate`, distinct from the pip client `x64dbg-automate-pyclient`) whose
release zip has to be manually extracted into x64dbg's `plugins/` folder per architecture. First
launch attempt failed with `TimeoutError: Session did not appear in a reasonable amount of time`
because no ZMQ server was listening — this was the actual root cause, diagnosed by comparing
`release\x32\plugins\` (empty) against what the plugin needs.

## 2. Live analysis approach

Since the MCP tools weren't available mid-session (registration requires a restart), analysis
was done by driving the `x64dbg_automate.X64DbgClient` Python API directly against a freshly
launched `x32dbg.exe` — the same underlying mechanism the Claude-Code-facing skills use, just
without the MCP/skill wrapping layer. Script: `live_debug.py` (scratchpad; can be recreated on
request, not copied into the workspace since it's a disposable analysis tool).

Method: launch Psychonauts.exe under the debugger (`start_session`), set a software breakpoint
on `d3d9.dll!Direct3DCreate9`, resume, capture the call site; step out (via a one-shot breakpoint
placed directly on the return address — more reliable than x64dbg's `rtr`/"run to return"
heuristic, which misbehaved on this target) to read the returned `IDirect3D9*`, read its vtable,
resolve the real `CreateDevice` function pointer from vtable slot 16, breakpoint it, confirm the
hit, decode its stdcall args, step out again to get the created `IDirect3DDevice9*`, read *its*
vtable, resolve `Present` from slot 17, breakpoint it, and confirm a real per-frame hit.

### Two non-obvious debugger behaviors hit and worked around

1. **x64dbg's default one-shot entry breakpoint** fires before any user breakpoint that lies
   later in the control flow. The very first `go()` after loading landed at `0x6ed79f`
   (`call psychonauts.6EDE5E`) — the EXE's own entry point, not our Direct3DCreate9 breakpoint.
   Fixed with a small retry loop (`run_until`) that keeps resuming through intermediate stops
   until CIP matches the intended target.
2. **Launching the EXE directly (not through Steam) causes a first-chance access violation
   loop** shortly after `CreateDevice` returns, at a fixed address `0x6b0c74` inside the EXE
   (`movsx edx, byte ptr [ecx]` — reading a byte from what's likely an uninitialized/null
   string pointer, comparing it against `0x1B`). With `go()`'s default `pass_exceptions=False`,
   x64dbg breaks on the first-chance AV and re-issuing a plain "run" re-faults on the exact same
   instruction forever (confirmed: 20 identical stops at `0x6b0c74` in a row). Passing
   `pass_exceptions=True` (x64dbg's `erun`) let the game's own SEH handle it and execution moved
   on normally to real `Present()` calls. **Likely cause: some init path expects a
   command-line/config value Steam normally supplies** (language, launch args, etc.) that's
   absent when launching the raw EXE under a debugger. Not investigated further — out of scope
   for this pass, but worth knowing before scripting any "run to steady state" automation later
   (always pass exceptions through past device creation, or launch via Steam and attach instead).

## 3. Confirmed live addresses

All addresses below were read from **live process memory** (vtables, stack args, breakpoint
`cip` on actual hits) in a fresh run with `pass_exceptions=True` past device creation — not
guessed from static analysis. Base addresses were consistent across every run this session (no
ASLR relocation observed between relaunches on this machine, but that's not guaranteed on a
different machine/reboot — always re-resolve rather than hardcoding).

| Symbol | Address | Module offset | Confirmed via |
|---|---|---|---|
| `psychonauts.exe` image base | `0x00400000` | — | `memmap()` |
| `d3d9.dll` base | `0x71920000` | — | `memmap()` |
| `d3d9.dll!Direct3DCreate9` | `0x71984B20` | `d3d9.dll+0x64B20` | breakpoint hit |
| → call site (in game code) | `0x0067DC6A` | `exe+0x27DC6A` | return address on stack |
| `IDirect3D9::CreateDevice` | `0x7198F750` | `d3d9.dll+0x6F750` | vtable slot 16 read live + breakpoint hit |
| → call site (in game code) | `0x0067BD52` | `exe+0x27BD52` | return address on stack |
| `IDirect3DDevice9::Present` | `0x71A06120` | `d3d9.dll+0xE6120` | vtable slot 17 read live + **breakpoint hit confirmed** (per-frame call) |
| → call site (in game code) | `0x0067E755` | `exe+0x27E755` | return address on stack |
| `d3dx9_40.dll!D3DXMatrixPerspectiveFovRH` call site | `0x00692525` | `exe+0x292525` | breakpoint hit |
| `d3dx9_40.dll!D3DXMatrixLookAtRH` call site | `0x006924B1` | `exe+0x2924B1` | breakpoint hit |

### `CreateDevice` call arguments (decoded live, one real run)

Reminder for future scripting: `CreateDevice` is a COM vtable method — `__stdcall` with an
**explicit `this` pointer as the first stack argument** (COM ABI), not a plain free function. My
first pass got this wrong (treated `esp+4` as `Adapter` instead of `this`), which silently
shifted every argument by one slot and made the subsequent device-pointer read fail with
`XERROR_READ_FAILED`. Corrected layout: `[esp+0]`=return addr, `[esp+4]`=`this`, `[esp+8]`=
`Adapter`, `[esp+0xC]`=`DeviceType`, `[esp+0x10]`=`hFocusWindow`, `[esp+0x14]`=`BehaviorFlags`,
`[esp+0x18]`=`pPresentationParameters`, `[esp+0x1C]`=`ppReturnedDeviceInterface`.

- `Adapter` = `0x0` → `D3DADAPTER_DEFAULT`
- `DeviceType` = `0x1` → `D3DDEVTYPE_HAL`
- `hFocusWindow` = real HWND (varied per run, e.g. `0x049A091C`)
- `BehaviorFlags` = `0x46` → `D3DCREATE_FPU_PRESERVE (0x2) | D3DCREATE_MULTITHREADED (0x4) |
  D3DCREATE_HARDWARE_VERTEXPROCESSING (0x40)` — confirms hardware vertex processing, not
  software/mixed. Relevant for a stereo hook: matrix constants likely go through the hardware
  T&L / vertex shader pipeline, consistent with the static-recon finding of a real shader
  pipeline (not fixed-function).
- `pPresentationParameters` → valid pointer; first dword (`BackBufferWidth`) read as `0x320`
  = 800 in one probe, plausible default/windowed resolution.

## 4. What this confirms vs. the static recon

- The static-recon hook plan (`03-static-recon.md`) is validated end-to-end: `Direct3DCreate9`
  → `IDirect3D9::CreateDevice` → `IDirect3DDevice9::Present` is a real, single, linear call
  chain with no surprises (no Ex variant, no extra indirection, no obfuscation/anti-tamper
  encountered while breakpointing and single-stepping this path).
- `D3DXMatrixPerspectiveFovRH` / `D3DXMatrixLookAtRH` (imported per static recon) are indeed
  called from the game's own code (call sites `0x692525` / `0x6924B1`, both close together in
  the `0x69xxxx` region of the EXE — likely the same camera/render-setup source file) — good
  candidates to investigate further for the per-eye view/projection injection point, but their
  *callers* (the functions containing those call sites) haven't been examined yet — that's the
  natural next disassembly target once we're back in x64dbg.
- No anti-debug / anti-tamper behavior observed. The only obstacle (the first-chance AV loop)
  is explained by bypassing Steam's normal launch path, not by any deliberate protection.

## 5. Clean shutdown

`client.terminate_session()` was called at the end of every successful run — this issues
x64dbg's `StopDebug`, which kills the debuggee since it was launched as x32dbg's own child
process (not merely attached), then closes the ZMQ connection. Verified after every run via
`Get-Process x32dbg,x64dbg,Psychonauts` that no stray processes remained. Two earlier runs that
errored before reaching the `finally` block *did* leave orphaned `x32dbg.exe`/`Psychonauts.exe`
processes behind — cleaned up manually each time with `Stop-Process`. **Note for future
sessions**: wrap the whole live-debug script body in try/finally with `terminate_session()` in
the finally, not just at the natural end of `main()`, to avoid this class of leftover process.
One `Psychonauts.exe` instance was also observed with **Steam (`steamid 8796`) as its parent**
rather than our x32dbg-launched instance — cause not fully diagnosed (Steam may auto-detect a
running copy of an owned game's EXE and spawn its own tracking instance); killed manually as
part of cleanup. Final state confirmed clean: only the pre-existing Steam client process remains
running, no `x32dbg`/`x64dbg`/`Psychonauts` processes, no stray `xauto_session.*.lock` files.

## 6. Loose ends for next session

- The x64dbg MCP server is now registered (`claude mcp list` shows it connected) but needs a
  Claude Code **restart** to actually appear as `mcp__x64dbg__*` tools — untested from inside
  Claude Code itself this session, only validated via the raw Python client.
- `python`/`pip` bare commands on PATH still resolve to the Windows Store alias stub ahead of
  the real 3.12 install; every command in this session had to use the full
  `C:\Users\Tefa\AppData\Local\Programs\Python\Python312\python.exe` path. Worth fixing PATH
  order (or disabling the Store alias under Settings > Apps > Advanced app settings > App
  execution aliases) so the plugin's own `SKILL.md`-documented bare `python ...` invocations
  work as written.
- The `0x6b0c74` first-chance-AV location (exe+0x2B0C74) was disassembled but not root-caused
  beyond "probably missing Steam launch context" — low priority, but flag if it recurs once we
  start driving the game further (e.g. past the main menu) for shader/matrix work.
