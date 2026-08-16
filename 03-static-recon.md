# Static Recon: Psychonauts.exe (read-only)

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(4,150,272 bytes). No modification made to the game install; only bytes read.

Method: no PE-dump tool was preinstalled (no `dumpbin`/Visual Studio build tools, no real
Python — see `01-tooling-setup.md`), so a small read-only PE import-table parser was written
in PowerShell (manual DOS/PE/COFF/optional-header/import-directory parsing) and run against
the file. Script + full raw output saved at:
- `C:\Users\Tefa\Documents\PsychonautsVR\recon\psychonauts_exe_imports.txt` (full output)
(the parser script itself lived in the session scratchpad, not copied into the workspace since
it's a disposable analysis tool, not a game asset — can be recreated on request)

## Header summary

| Field | Value |
|---|---|
| Machine | IMAGE_FILE_MACHINE_I386 (x86, 32-bit) |
| PE type | PE32 |
| Subsystem | IMAGE_SUBSYSTEM_WINDOWS_GUI |
| Linker version | 9.0 (Visual C++ 2008 / VS9, consistent with `MSVCR90.dll` runtime import) |
| Sections | 6 |
| Linker timestamp | 2016-01-05 23:53:22 UTC |

The 2016 timestamp is later than the original 2005 release or the ~2011 Steam port — consistent
with a later Steam-era compatibility patch build (this copy imports `steam_api.dll`,
`XINPUT1_3.dll`, `DINPUT8.dll` — all things not needed in a pure 2005 XP-era build).

## Sections (notable)

Two custom sections beyond the standard `.text/.rdata/.data/.rsrc`:
- **`.dflua`** (44,644 bytes) and **`.dfluatx`** (15,694 bytes) — "Double Fine Lua". This
  independently corroborates Jill Crungus's blog documentation that the engine embeds a Lua
  scripting layer: the exe itself carries dedicated PE sections for Lua bytecode/text, not just
  loose script files on disk.

## Imported DLLs (full list)

`DINPUT8.dll`, `d3d9.dll`, `d3dx9_40.dll`, `WINMM.dll`, `DSOUND.dll`, `binkw32.dll`,
`isactwin.dll`, `AudioDrv.dll`, `XINPUT1_3.dll`, `steam_api.dll`, `KERNEL32.dll`, `USER32.dll`,
`SHELL32.dll`, `ole32.dll`, `OLEAUT32.dll`, `MSVCR90.dll`.

### d3d9.dll — the whole point of this recon

**Exactly one imported function: `Direct3DCreate9`.** This is the classic (non-Ex) D3D9 entry
point. Concrete implications:
- The game does **not** use `Direct3DCreate9Ex` / D3D9Ex — plain D3D9, so `IDirect3D9` /
  `IDirect3DDevice9` vtables are the ones to hook, not the Ex variants.
- Confirms `d3d9.dll` stub-replacement (dxwrapper's approach, and the classic 3Dmigoto/Helix
  Mod injection vector) is directly viable: the loader resolves `d3d9.dll` by name via normal
  DLL search order, and the game's only touchpoint into it is this single factory call, which
  a proxy DLL can trivially forward to the real d3d9.dll after hooking the returned
  `IDirect3D9` vtable (specifically `CreateDevice`, which is the point at which `Present`,
  `SetTransform`, `SetVertexShaderConstantF` etc. on the resulting `IDirect3DDevice9` become
  hookable).
- No `d3d9.dll` or `d3dx9_40.dll` ships next to the exe in the game folder — both are resolved
  from system paths (`redist/` only carries DirectX End-User Runtime installers: `DSETUP.dll`,
  `DXSETUP.exe`, `d3dx9_31`/`d3dx9_43` redistributable cabs, `vcredist_x86.exe`). This is
  good: it means a d3d9.dll placed in the game's own directory will shadow the system one per
  standard Windows DLL search order, with nothing else in the game folder to conflict.

### d3dx9_40.dll — confirms shader-based (not pure fixed-function) rendering

Full function list (only 15 imports, so exhaustive):
`D3DXVec4Transform`, `D3DXQuaternionMultiply`, `D3DXQuaternionRotationMatrix`,
`D3DXMatrixInverse`, `D3DXMatrixRotationQuaternion`, `D3DXMatrixReflect`,
`D3DXMatrixOrthoRH`, `D3DXMatrixPerspectiveFovRH`, `D3DXSaveSurfaceToFileA`,
`D3DXAssembleShader`, `D3DXMatrixLookAtRH`, `D3DXCreateBuffer`, `D3DXCompileShader`,
`D3DXFilterTexture`, `D3DXGetShaderConstantTable`.

Implications:
- `D3DXCompileShader` / `D3DXAssembleShader` / `D3DXGetShaderConstantTable` prove the engine
  compiles/assembles HLSL or shader-asm **shaders at runtime or ships precompiled ones with
  runtime constant-table introspection** — i.e. this is a real (if era-appropriate simple)
  shader pipeline, not fixed-function T&L. This matches Helix Mod's fix needing to patch
  specific shaders (sky/celestial) rather than fixed-function render states.
- `D3DXMatrixPerspectiveFovRH` / `D3DXMatrixLookAtRH` / `D3DXMatrixOrthoRH` are the
  camera/projection matrix builders — right-handed coordinate convention confirmed. These (or
  their call sites) are exactly where a stereo hook would want to intercept and produce two
  per-eye projection/view matrices instead of one. Worth locating these call sites dynamically
  once x64dbg is available (see next milestone).
- `D3DXMatrixReflect` suggests mirror/reflection-plane rendering exists somewhere (e.g. water
  or a mirror level gimmick) — a possible extra render pass to account for in a stereo pipeline
  (reflections would need per-eye treatment too).

### Other imports of note

- `DINPUT8.dll` (`DirectInput8Create`) + `XINPUT1_3.dll` (2 ordinal imports) — game has both
  legacy DirectInput and XInput controller paths; relevant later for VR controller mapping, not
  for the rendering hook itself.
- `steam_api.dll` — Steamworks integration (achievements/cloud saves at minimum). No DRM
  circumvention implied or needed for our purposes; noted only because a d3d9.dll proxy sitting
  alongside a Steam game is a completely standard, benign pattern (this is exactly what
  dxwrapper and every other injection-based mod for Steam D3D9 games already does).
- `MSVCR90.dll` / linker 9.0 — VS2008 toolchain, matches the 32-bit target and era-appropriate
  D3DX version (`d3dx9_40`, i.e. DirectX SDK August 2009 vintage).

## Conclusion for injection strategy

Everything here lines up with the plan implied by prior art (Helix Mod fix + dxwrapper):
1. **Injection vector**: `d3d9.dll` proxy DLL placed next to `Psychonauts.exe`, forwarding to
   the real system `d3d9.dll`, hooking `Direct3DCreate9` → `IDirect3D9::CreateDevice` →
   `IDirect3DDevice9::Present` (frame-pacing/eye-swap point) and
   `SetVertexShaderConstantF`/`SetTransform` (to inject per-eye view/projection matrices).
   dxwrapper is a credible off-the-shelf implementation of exactly this stub; a hand-rolled
   minimal proxy DLL (à la standard "d3d9 wrapper" boilerplate) is also very tractable given
   the game's D3D9 usage is this simple (one factory call, no Ex).
2. **Rendering pipeline is shader-based**, not fixed-function — stereo matrix injection should
   target the shader constant tables / D3DX matrix builder call sites, consistent with how
   Helix Mod's 2013 fix operated (patching specific shaders) rather than legacy fixed-function
   texture-stage hacking.
3. No blockers found in the static import surface — nothing here suggests anti-tamper,
   packing, or obfuscation (imports are all plaintext, standard Win32/D3D9/MSVC runtime names,
   no unusual sections beyond the expected `.dflua*` Lua sections).
