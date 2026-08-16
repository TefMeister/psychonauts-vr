# Psychonauts VR — Status

Last updated: 2026-08-16 (CreateDevice/Present vtable-hook session)

## Latest status (read this first)

The proxy `d3d9.dll` (`tools/proxy-d3d9/`) now **vtable-hooks `IDirect3D9::CreateDevice` (slot 16)
and `IDirect3DDevice9::Present` (slot 17)**, in addition to the previously-validated
`Direct3DCreate9` forwarding — still pure observation, every hook calls straight through to the
real implementation and returns its result unmodified. Both slot indices were cross-checked two
ways (counting fields in mingw-w64's own `d3d9.h` vtbl structs, and the prior live x64dbg session
that read the same slots out of live process memory) and patched by assigning the named vtbl
struct fields (`lpVtbl->CreateDevice = ...`, `lpVtbl->Present = ...`) rather than raw pointer-index
math, so the compiler — not manual offset arithmetic — guarantees the correct slot. Live-validated
by copying into the game dir and launching `Psychonauts.exe` directly: `CreateDevice` fired once
with full `D3DPRESENT_PARAMETERS` logged (640×480 windowed, `D3DFMT_A8R8G8B8` backbuffer,
`D3DFMT_D24S8` depth/stencil, `BehaviorFlags=0x46` matching the live-debug session exactly), and
`Present` fired repeatedly at a steady ~30 fps (frame counter `1→29→59→89→119→149` across six
~1-second-apart throttled log lines) — proving the per-frame hook point is real and durable, not a
one-shot artifact. Test DLL removed from the game directory and the game process killed
immediately after validating, exactly as before. Full detail:
`notes/06-createdevice-present-hooks.md`.

**Proposed next milestone**: use x64dbg to disassemble the functions *containing* the
`D3DXMatrixPerspectiveFovRH` (`exe+0x292525`) / `D3DXMatrixLookAtRH` (`exe+0x2924B1`) call sites
(already located, not yet disassembled) to find where FOV/aspect ratio and view matrices are
actually computed — the real per-eye injection point. In parallel/afterward, use the now-hooked
`CreateDevice` to create a second render target (`CreateRenderTarget`/`CreateTexture`, sized to
match the backbuffer) purely to prove a second surface can be stood up against this device/driver
— still no compositing/stereo logic yet, just infrastructure.

## Prior milestone (proxy-DLL validation session, still valid)

The minimal logging proxy `d3d9.dll` was **built and validated end-to-end** as plain
load-and-forward (no vtable hooking yet at that point). No C/C++ compiler existed on this machine;
installed LLVM-MinGW (`winget install MartinStorsjo.LLVM-MinGW.UCRT`) to get an
`i686-w64-mingw32-clang` 32-bit target compiler (game is 32-bit). The real `Direct3DCreate9`
address resolved by the proxy (`d3d9.dll base + 0x64B20`) exactly matched the offset independently
found via x64dbg in the live-debug session — good cross-confirmation. Full detail:
`notes/05-proxy-dll-validation.md`.

## Prior milestone (live-debug session, still valid)

x64dbg, a real Python 3.12, the `x64dbg_automate[mcp]` pip package, and the
(separately-downloaded, not bundled) x64dbg-automate debugger plugin are all installed and
working. First live/dynamic analysis pass **fully confirmed the static-recon hook plan**:
`Direct3DCreate9` (`d3d9.dll+0x64B20`) → `IDirect3D9::CreateDevice` (`d3d9.dll+0x6F750`, called
from `exe+0x27BD52` with `D3DCREATE_HARDWARE_VERTEXPROCESSING`) → `IDirect3DDevice9::Present`
(`d3d9.dll+0xE6120`, called from `exe+0x27E755`) were all hit live with a debugger and their
addresses read from real process memory, not guessed. Camera matrix call sites
(`D3DXMatrixPerspectiveFovRH` / `D3DXMatrixLookAtRH`, both called from the `exe+0x2925xx`
region) were also located. No anti-debug/anti-tamper behavior encountered. Full detail, exact
addresses, and two non-obvious debugger gotchas (default entry breakpoint; first-chance-AV loop
when launching outside Steam under x64dbg) are in `notes/04-live-debug-findings.md`.

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
