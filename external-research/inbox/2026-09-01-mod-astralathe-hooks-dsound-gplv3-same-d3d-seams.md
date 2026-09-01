# Verdict on the Astralathe lead: it is a `dsound.dll` proxy (no filename clash), GPLv3, and it hooks the same D3D9/DirectInput seams our proxy uses

Filed by: the modding lane (`/pd`, home PC), 2026-09-01
For: the research session — `topics/2026-09-01-astralathe-already-ships-the-lua-console-and-the-debug-menu.md` and its INDEX.md entry
Full write-up: `modding-notes/70-position-chain-is-the-transform-row-astralathe-hooks-dsound.md` §2

The topic recorded three unknowns as "needs ten minutes in a browser": which file it hooks, its
licence, and therefore whether it collides with our `d3d9.dll`. All three are answered from its
source. **The trick: GitLab's REST API returns plain JSON and raw files even though the web UI
renders client-side** — `https://gitlab.com/api/v4/projects/34250039/repository/tree`,
`.../repository/files/<url-encoded path>/raw?ref=master`, `.../wikis/<slug>`. Worth adding to the
research lane's toolbox for any GitLab-hosted project.

- **Injection file: `dsound.dll`** (`AstralatheAutohook/dsound.def` + `dllmain.cpp`: forwards twelve
  DirectSound exports to `GetSystemDirectoryA()\dsound.dll`, and `LoadLibraryA("Astralathe.dll")`
  only when the process is `psychonauts.exe` and its AutoHook config is on). Shipped set per
  `release_files_to_pack.txt`: `dsound.dll`, `Astralathe.dll`, `AstralatheSteam.dll`,
  `PsychoPortal.dll`, `AstralatheLauncher.exe` (fallback when AutoHook is off),
  `Astralathe_CobwebDuster.exe`. **No `d3d9.dll`.** `[reported 2026-09-01, from source]`
- **Licence: GPLv3** (`LICENSE` at repo root). Study-only is now a legal requirement, not just our
  rule. `[reported 2026-09-01]`
- **Functional overlap, same seams:** `Astralathe/DXHooks.cpp` uses PolyHook2 to IAT-hook
  `Direct3DCreate9` (on `d3d9.dll` — i.e. on the import that resolves to OUR proxy) and
  `DirectInput8Create`, and vtable-swaps `IDirect3D9::CreateDevice`, `IDirect3DDevice9::EndScene`
  / `::Reset` (ImGui overlay) and `IDirectInputDevice8A::GetDeviceState` / `GetDeviceData` /
  `SetCooperativeLevel`. Our proxy patches the same `CreateDevice` slot, the same
  `DirectInput8Create` IAT entry and `GetDeviceState`. **Verdict: no file collision, real
  functional collision; keep it off the working install, use it only on a separate copy.**
- Feature claims confirmed by file names in the tree: `ImGui/LuaPad.cpp`, `ImGui/DebugConsole.cpp`
  (Lua console), `Psychonauts/DebugDraw/EDebugLineManager.cpp` (debug drawing),
  `Psychonauts/EScriptVM.cpp`, `ERenderer.cpp`, `ECamera.cpp`. Its in-game menu key is **F10**.
- Suggested status change for the lead: 🆕 → **assessed / keep off the working folder**; the
  "unassessed proxy-collision risk" wording can be retired.
