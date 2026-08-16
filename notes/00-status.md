# Psychonauts VR — Status

Last updated: 2026-08-16 (live-debug session)

## Latest status (read this first)

The x64dbg blocker from the first session is **resolved**: x64dbg, a real Python 3.12, the
`x64dbg_automate[mcp]` pip package, and the (separately-downloaded, not bundled) x64dbg-automate
debugger plugin are all installed and working. First live/dynamic analysis pass is done and
**fully confirms the static-recon hook plan**: `Direct3DCreate9` (`d3d9.dll+0x64B20`) →
`IDirect3D9::CreateDevice` (`d3d9.dll+0x6F750`, called from `exe+0x27BD52` with
`D3DCREATE_HARDWARE_VERTEXPROCESSING`) → `IDirect3DDevice9::Present` (`d3d9.dll+0xE6120`, called
from `exe+0x27E755`) were all hit live with a debugger and their addresses read from real process
memory, not guessed. Camera matrix call sites (`D3DXMatrixPerspectiveFovRH` /
`D3DXMatrixLookAtRH`, both called from the `exe+0x2925xx` region) were also located. No
anti-debug/anti-tamper behavior encountered. Full detail, exact addresses, and two non-obvious
debugger gotchas (default entry breakpoint; first-chance-AV loop when launching outside Steam)
are in `notes/04-live-debug-findings.md`. Debugger and game were both fully closed at the end of
the session — verified no stray processes or lockfiles remain.

**Proposed next milestone**: build the minimal proxy `d3d9.dll` in `tools/` (hand-rolled or via
dxwrapper) that forwards every call to the real system `d3d9.dll` with logging, and confirm it
loads and the game runs identically with it in place — before writing any actual stereo-rendering
logic. This doesn't require x64dbg at all and is safe/reversible (proxy DLL lives in `tools/`,
only copied into the game folder for an explicit, user-visible test run). Once that's confirmed,
use the now-working x64dbg toolchain to disassemble the two functions containing the
`D3DXMatrixPerspectiveFovRH`/`D3DXMatrixLookAtRH` call sites found this session, to find the
actual per-eye view/projection injection point.

## Summary

First recon pass complete. Workspace stood up, both trusted tools installed, key prior-art
resources read and summarized, and read-only static analysis of `Psychonauts.exe` performed.
No changes made to the game install. Findings line up well: this looks like a tractable
in-process D3D9 hook + shader patch project, closely following the path Helix Mod already
proved works for this exact game in 2013.

## What's set up

- Workspace: `C:\Users\Tefa\Documents\PsychonautsVR\` with `notes/`, `tools/` (empty, reserved),
  `recon/` (raw PE dump).
- Git installed (was missing; required by the plugin marketplace clone mechanism — installed
  via winget, standard low-risk dev tool, not the "third-party debugger" caution).
- **x64dbg-skills** plugin installed & enabled (`claude plugin marketplace add
  dariushoule/x64dbg-skills` + `claude plugin install`). **Now fully usable**: x64dbg, x64dbg
  Automate (debugger plugin), a real Python 3.12, `x64dbg_automate[mcp]`, and the MCP server
  registration are all installed and verified — see `notes/04-live-debug-findings.md` for the
  install log and the first successful live-debug pass. The MCP tools need a Claude Code
  restart to appear (registered mid-session); the raw Python client works immediately.
- **superpowers** plugin installed & enabled from the official marketplace, fully usable now.
  `SUPERPOWERS_DISABLE_TELEMETRY=1` set in the global `~/.claude/settings.json` `env` block.

Full detail: `notes/01-tooling-setup.md`.

## What was learned from prior art

- **Helix Mod's 2013 fix is the strongest signal**: someone already got per-eye stereo working
  on this exact game by patching a small, identifiable family of shaders (sky/celestial
  objects). Proves the shader pipeline is patchable and that once fixed, convergence/depth are
  freely controllable.
- **dxwrapper's `d3d9.dll` stub-replacement mechanism is directly applicable** — the game
  imports `d3d9.dll` by plain name with one call (`Direct3DCreate9`), no Ex variant, nothing in
  the game folder to conflict with a proxy DLL.
- Jill Crungus's blog is confirmed to cover Lua scripting + ASD format + level construction;
  independently corroborated by our own recon (the exe has `.dflua`/`.dfluatx` PE sections).
  Deeper read deferred until we need entity/camera data, not just the rendering hook.
- RayCarrot/PsychonautsStudio: thin, WIP, low value for this phase.
- Brobert-in-aus's two guides are generic Godot/OpenXR external-host porting methodology, not
  Psychonauts-specific — useful only as a fallback architecture if in-process shader hooking
  hits a wall; the in-process path is better supported by actual prior art for this game.

Full detail: `notes/02-technical-leads.md`.

## Static recon findings (Psychonauts.exe, read-only)

- 32-bit PE, VS2008/linker 9.0, Steam-era patched build (timestamp 2016, not the original 2005
  build — pulls in steam_api/XInput/DirectInput8).
- `d3d9.dll` import: exactly `Direct3DCreate9`, plain D3D9 (no Ex) — confirms `CreateDevice` →
  `Present`/`SetVertexShaderConstantF` as the hook points, and confirms the stub-DLL injection
  approach is viable.
- `d3dx9_40.dll` imports include `D3DXCompileShader`, `D3DXAssembleShader`,
  `D3DXGetShaderConstantTable` — real shader pipeline, not fixed-function, matching how Helix
  Mod's fix operated. Also imports `D3DXMatrixPerspectiveFovRH`/`D3DXMatrixLookAtRH` — likely
  call sites for injecting per-eye projection/view matrices.
- No packing/anti-tamper signals in the import table; everything is plaintext standard Win32/
  D3D9/MSVC runtime names.

Full detail + raw dump: `notes/03-static-recon.md`, `recon/psychonauts_exe_imports.txt`.

## Blocker (resolved 2026-08-16, live-debug session)

~~x64dbg-skills needs x64dbg + x64dbg Automate + a working Python 3 install~~ — all installed
and verified working this session (x64dbg 2026.05.27, Python 3.12.10, `x64dbg_automate[mcp]`
0.9.2, plus the separately-downloaded x64dbg-automate debugger plugin the README doesn't
mention as a distinct download). See `notes/04-live-debug-findings.md` for the full install log
and the live analysis results. Superseded by "Latest status" at the top of this file.

## Proposed next milestone

See "Latest status" at the top of this file — build the minimal proxy `d3d9.dll` in `tools/`
next (doesn't need x64dbg), then use x64dbg to disassemble the camera-matrix call sites found
this session.
