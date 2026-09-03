# Engine Dossier — Psychonauts (2005) (Double Fine, bespoke in-house engine)

> One consolidated, living reference for this game's engine, filled in as the
> `PLAYBOOK.md` phases are worked. Chronological blow-by-blow belongs in the
> `psychonauts-vr-dev-archive` / `psychonauts-vr-modding-notes` repos; this file
> is the *distilled current truth*. Update it whenever a fact changes; correct
> false leads in place.

**Status:** Phase 6 done (game confirmed in a real Quest 3 headset with head
tracking — the North Star); working through Phase 7+ (first-person view
polish). **VR-readiness verdict:** proven — stereo + head tracking work in
a real headset. First-person (camera anchored to the player character) is
the open sub-project; sessions 52-53 (playbook §3.3 shader disassembly +
two isolated empirical composition tests) proved the entire render-level
transform mechanism correct end-to-end — the FP bug is real but is NOT a
transform-math problem. New leading suspect: Raz-lock reliability during
real gameplay — still UNTESTED as of session 54: a real foreground-focus
automation bug was found and fixed (§10, durable fix), but the door-entry
walk turned out to be a deterministic miss, not random drift, blocking the
actual test. **Sessions 55-58 (pure static research, zero game execution):
the Lua exec primitive — previously a "multi-session lift" blocking the
clean engine-native FP fix — was fully traced statically to a single
function call (`0x6B0C00`, §11), pending live verification. Also found a
Lua-free level-load path (`SetPendingLevel`, §11) that could solve the
automation blocker without walk-tuning, and disassembled the entire
455-shader corpus offline (§8-ish, corrects the c50/UI belief).** Next live
session priority: verify the Lua primitive with a trivial payload, then
either the FP engine-native route or the level-jump automation fix. See
§6, §10, §11, §12.

## 1. Identity

- Game / build / version: Psychonauts (2005, Double Fine Productions). This
  copy is a **2016 Steam-era compatibility-patched build** (not the original
  2005 release), confirmed by PE timestamp and imports (`steam_api.dll`,
  `XInput`, `DirectInput8` pulled in that the 2005 build wouldn't have had).
- Platform & store: Steam, Windows. Not an unofficial port — genuine Steam
  release, standard install layout.
- Legitimacy: owned copy confirmed, installed at
  `Steam\steamapps\common\Psychonauts`.

## 2. Engine lineage

- Family: **bespoke in-house Double Fine engine**, not a licensed third-party
  engine — no family knowledge to import from prior VR work on other titles.
- Middleware: Bink video (`.bik` cutscenes/splash), an in-process **Lua 4.0
  VM** for scripting (see §6/§10), presumably a bespoke or licensed physics/
  animation system (not yet identified by name).
- Distinctive file formats / build tags: `.dflua`/`.dfluatx` PE sections hold
  the Lua glue. Level/entity data format not yet reverse engineered (deferred
  — not needed for the VR pipeline so far).

## 3. Binary & memory

- **32-bit PE**, VS2008 / linker 9.0. **Non-relocatable** — ImageBase
  `0x400000`, no ASLR, ~confirmed by every live capture matching static
  addresses byte-for-byte across sessions. This is a major simplification:
  every function/data address found once is hardcodable for this build.
- Renderer API: **Direct3D 9** (not Ex). Confirmed by the import table: a
  single `Direct3DCreate9` import from `d3d9.dll`, nothing Ex-variant.
  `d3dx9_40.dll` imports (`D3DXCompileShader`, `D3DXAssembleShader`,
  `D3DXGetShaderConstantTable`, `D3DXMatrixPerspectiveFovRH`,
  `D3DXMatrixLookAtRH`) confirm a real (non-fixed-function) shader pipeline.
- No developer console found. The functional equivalent is the **in-process
  Lua VM** (1129 registered engine bindings incl. camera/entity/bone
  functions — see §10) — the "console/cvar system" for this engine is really
  "can we execute Lua".

## 4. DRM / anti-debug & injection foothold

### ⚠️ A public loader for this exact build exists — Astralathe. DO NOT install it into the working folder yet. (`/gr`, folded 2026-09-01)

**Astralathe** (Jill / `scrunguscrungus`, GitLab) is *"an all-in-one mod loader, debugging tool, API
extender and patcher for Psychonauts"*, in beta, targeting **the same modern digital release we inject
into with a `d3d9.dll` proxy**. `[reported 2026-09-01]` Its published feature list includes an
**in-game Lua console**, the game's **native level select and debug menu**, **restored debug rendering
functions**, engine bugfixes and widescreen support. PsychoRando and a Psychonauts Archipelago
integration are both built on it and current.

**✅ Assessed from its source 2026-09-01 (home PC, notes/70 §2):** Astralathe injects as a **`dsound.dll`**
proxy (no filename clash with our `d3d9.dll`), is **GPLv3** (study-only is a legal requirement, not
just our rule), and uses PolyHook2 to IAT-hook `Direct3DCreate9` + `DirectInput8Create` and to
vtable-swap `IDirect3D9::CreateDevice`, `IDirect3DDevice9::EndScene`/`Reset` and
`IDirectInputDevice8A::GetDeviceState`/`GetDeviceData`/`SetCooperativeLevel` — **the same seams our
proxy owns**. `[reported 2026-09-01, read from the project's source via the GitLab REST API]`
Verdict: no file collision, a real functional one; keep it off the working install, use it on a
separate copy only. Its menu key is F10. The paragraph below is kept as the original warning.

**🚨 The safety issue, and why this is a warning rather than a recommendation: which file it hooks is
UNREAD.** Its GitLab repo and wiki render client-side and returned empty page shells to every
automated fetch. **If it also wraps `d3d9.dll`, running it alongside our proxy is a direct conflict**;
if it uses a different seam, that is itself worth knowing. Ten minutes in a browser settles it, and
that has not happened. **Its licence is unread for the same reason, so nothing from it may be reused
or referenced in our code until someone looks.**

Under this project's own rules it is a **third-party non-tool mod**: study the method and reimplement
in our own words, never rebuild from its files. The safe use is as an **observation instrument on a
separate copy of the game**.

**What it bears on:** the Lua-exec primitive (already made unnecessary for the FP work by notes/69's
direct pointer chain — but a working public Lua console exists, worth knowing before more effort goes
into building one); the dormant debug menu, which
`external-research/topics/2026-08-24-debug-menu-and-octree-culling.md` still lists as *"live test in
progress"* and which Astralathe claims as shipped and working; and §8, where "restored debug rendering
functions" would make a pass inventory cheap.

### ✅ Free with the queued `playerpos` run: measure the eye height instead of guessing it

`status/psychonauts-vr.md` records *"Height default 60 is a guess — unit scale is recorded nowhere."*
No public source documents Psychonauts' unit scale — the modding scene's published work is file
formats (`.ppf`, `.plb`), not coordinate conventions. **But the scale is not needed.** The deployed
build already prints the camera position, and now the player position too. **On flat ground the
up-axis difference between them IS the engine's own eye height, in its own units — measured rather
than guessed, and needing no conversion to be correct.** Record both during the already-queued
`playerpos` run; it costs nothing extra and retires the guess.


- **No DRM, no anti-tamper, no anti-debug** observed (confirmed live,
  notes/04). This is a plain Steamworks-integrated build (achievements/cloud
  saves), not copy protection.
- One quirk, not protection: launching the EXE directly (bypassing Steam)
  causes a first-chance access-violation loop early in startup (missing a
  Steam-supplied launch value). Workaround: launch via Steam and attach, or
  pass exceptions through past device creation.
- **Injection vector: proxy `d3d9.dll`.** The game imports exactly one D3D9
  entry point (`Direct3DCreate9`) by plain name — textbook case for the
  classic same-named-DLL-in-game-folder proxy technique (same mechanism
  `dxwrapper` and most 2000s-era D3D9 mods use). Our proxy forwards
  `Direct3DCreate9` (resolved dynamically via `GetProcAddress` against the
  real system DLL, never statically linked, to avoid the loader resolving
  our own import back to ourselves) and exports nothing else the game needs.

## 5. Threading & frame structure

- **D3D9, single immediate device — no deferred contexts / command lists**
  (that's a D3D11+ concept; doesn't apply here). One render thread does
  everything.
- Per-frame structure, confirmed live: the game calls one internal function
  twice per frame — nicknamed **"CandB"** (`exe+0xFEDA0` /
  `0x004FEDA0`) — once per eye-equivalent pass. Our `Hook_CandB` wraps it
  with a **call-twice** trampoline: `BeforeEye1 → real CandB → BeforeEye2 →
  real CandB → AfterBoth`, redirecting each invocation's render target/depth-
  stencil to a private per-eye surface (see §7/§8). This "render the world
  twice by re-invoking the engine's own top-level render function" is exactly
  the playbook's §5.1 "re-execute recorded work once per eye" pattern, just
  without command lists — CandB *is* the re-invocable unit.
  **✅ 2026-09-02 (`/pd`, static signature scan, NO LAUNCH): CandB has an engine name.** Astralathe's
  `GameApp_RenderFrame` signature (`55 8B EC 83 EC 20 89 4D ? 68 ? ? ? ? FF 15`) has exactly one
  match at `0x004FEDA0` — CandB's own address, independently identified by multiple live sessions
  before this. `[inferred-static 2026-09-02, n=1 signature match at a previously-independently-
  identified address]` **CandB is `GameApp_RenderFrame`**, not an inner helper of it — this confirms
  (rather than assumes) that this mod's whole camera/void/automation stack is hooked on the engine's
  own top-level per-frame entry point. Detail: `modding-notes/74-…` §3.
- `IDirect3DDevice9::Present` fires once per real frame; our hook there does
  the eye-composite (StretchRect both private surfaces into the real
  backbuffer side-by-side) and, when the VR submit path is enabled, pumps
  OpenVR poses/submission.

## 6. Camera & projection delivery (the crucial section)

- **Delivery mechanism: per-draw MVP in a vertex-shader constant register —
  the playbook's harder "case (b)".** No single shared view/projection
  buffer; the game calls `IDirect3DDevice9::SetVertexShaderConstantF` before
  every draw. **`StartRegister = 6`, `Vector4fCount = 4`** carries the
  combined **World·View·Projection matrix, TRANSPOSED, per draw** — this is
  the D3D9 constant-register analog of a per-draw MVP constant buffer.
  Confirmed live via `x64dbg` at `Direct3DCreate9`/`CreateDevice`/`Present`
  call sites (notes/04) and validated over dozens of sessions by the stereo
  correction actually looking right on screen and in a real headset.
- **✅ CONFIRMED 2026-08-20 (session 52) via real shader disassembly** (playbook
  §3.3 — `D3DXDisassembleShader` called offline against captured bytecode,
  no live game needed): the row-vector-on-the-left, rows-as-4-consecutive-
  registers convention IS how the shaders actually consume register 6.
  Representative instruction: `m4x4 r4, r11, c6` (D3D9 SM2 semantics = 4×
  `dp4`, `dst.i = dot4(src, c[6+i])`); with position `w=1`, the pure
  translation term for each output component is exactly `c[6+i].w` —
  i.e. flat-array indices `[3,7,11,15]`, matching our code's extraction
  exactly. Cross-checked against 239 of the 455 captured shaders (all share
  the pattern). **This closes the risk flagged below in session 51 — the
  convention was right all along.** Consequence: notes/51's mystery (a
  computed 212wu forward FP translation producing zero visible eye
  movement) is **not** a convention bug; the actual cause is still open,
  narrowed to the `X1 * T` composition / `Transpose(P⁻¹·T·P)` premultiply
  pipeline in `VRBridge_UpdateHeadTracking`, not the register-6 read/write
  itself. See notes/52 for the full disassembly and method (a reusable,
  fully-automated, fully-offline verification recipe worth reusing on any
  future register/convention question for this engine).
- **CPU-side upload chain (traced live, session 17, 8/8-sample register
  trace):** the register-6 matrix is built at `exe+0x11D2CD`–`0x11D33E`
  (absolute `0x0051D2CD`–`0x0051D33E`): two `MatrixMultiply` calls, then an
  in-place `Transpose` (`exe+0x42E2A0`) whose result feeds
  `SetVertexShaderConstantF(6, …)`. Useful as a pre-API patch point (earlier
  than the vtable hook) if one is ever needed — the vtable hook has been
  sufficient so far.
- Projection `P`: recovered from the game's own
  `D3DXMatrixPerspectiveFovRH`-style call site — `xScale`/`yScale`/`zn`/`zf`
  are cached once per frame (`BuildProjectionMatrix`, "BPM") from that
  call's inputs, not guessed.
- **Per-eye override maths in use (stereo, playbook §3.4/§5.2), CONFIRMED
  correct in-headset:** rather than decomposing per-object, every register-6
  upload is patched by a small in-place correction (notes/24's derivation):
  `patched[r] = src[r] + src[8+r]*Y20 + src[12+r]*Y30` for `r in 0..3`, where
  `Y20 = -d·xScale/B`, `Y30 = (-k - A·d/B)·xScale`, `d` = half-IPD (real
  OpenVR value when available), `k` = eye shear, `A,B` derived from
  `zn,zf`. This is algebraically the same idea as the playbook's
  `K_eye = P_eye · T_eye(±IPD/2) · P⁻¹` left-multiply, specialized to a
  single off-axis column patch instead of a full 4×4 multiply — cheaper, and
  it doesn't require decomposing per-object M, exactly per the playbook's
  case-(b) escape hatch. **This part is proven correct**, unlike the
  first-person world-move (above), which is a different, larger-magnitude
  operation on the same register.

## 7. Constant-buffer fill mechanism (D3D9 analog)

- No CB-style memory-mapping trap to worry about — D3D9's
  `SetVertexShaderConstantF` is a direct API call we hook at the vtable
  (slot 94), so every upload is visible to us with no staging/read-back
  needed. This sidesteps the playbook §3.5 "persistent-mapped buffer"
  failure mode entirely (that's a D3D11+/D3D12 concern).
- Override patch point: inside `Hook_SetVertexShaderConstantF`, gated on
  `StartRegister == 6 && Vector4fCount == 4` and the current eye phase; the
  patched array is forwarded to the real
  `SetVertexShaderConstantF`. Fail-safe: every other register passes through
  untouched, and the whole patch is skipped (falls through to
  `pConstantData` unmodified) if any of the cached projection/eye state
  isn't valid yet.

## 8. Pass inventory (by render target)

- **Main scene**: color + depth, rendered once per eye via the CandB
  call-twice hook, into two private offscreen render targets sized
  `bbWidth*scale × bbHeight*scale` (`PSYVR_RENDER_SCALE`, default 2×; 3×
  validated live at 1920×1440/eye on an RTX 5080 test PC).
- **Depth-stencil**: each eye has its **own private depth-stencil surface**
  (format matches the game's `AutoDepthStencilFormat`, live-confirmed
  `D3DFMT_D24S8`) — sharing one between both eyes was an early, since-fixed
  root cause of a frozen/dark-eye bug (notes/22).
- **UI/HUD**: drawn through a small, distinct family of shaders (**exactly
  10 of 455 total vertex shaders — dump indices 3, 447-455, confirmed
  session 56 via a full offline corpus disassembly**) that bypass the
  register-6 world-transform path entirely (screen-space, "HUD at
  infinity"). Given explicit VR depth via a separate mechanism
  (`PSYVR_UI_DEPTH`, a per-eye `c50.x` shift) rather than the world-transform
  correction. **Correction (session 56):** register 50 is NOT UI-exclusive —
  it appears in **100% of the 455 shaders** (the standard D3D9 half-pixel
  texel-alignment offset every shader adds to `oPos`); our own choice to
  reuse it for UI depth is what's UI-specific, not the register itself.
  **Dead end**: attempting to *shrink* the UI viewport for comfort (notes/42)
  failed live because the game's fullscreen fade/backdrop overlays share the
  exact same UI shader signature as real HUD elements — shrinking one
  shrinks/darkens the other; there is no shader-identity-level way to tell
  them apart, would need per-draw texture/geometry classification instead.
  Demoted to an opt-in, off-by-default knob (`PSYVR_UI_SCALE`).
- **Shadows**: the engine has real **projected-texture shadows**
  (`SetLightShadowEntity`, `SetShadowBlendMode`, `SetShadowFixedDirection`
  etc., all live Lua bindings — notes/44). Not yet traced in a live capture;
  hypothesis (untested) is that the shadow pass re-uploads the same bone
  palette as the main skinned draw, so our bone/pose overrides would
  propagate into shadows "for free" — flagged as an open risk, not a
  confirmed fact.
- No AA/post-process chain has been characterized yet (not on the critical
  path so far; the 2005-era engine likely has little to none).

## 9. cvar / console cheat sheet

This engine has no traditional console; the closest equivalent is calling
these Lua-registered engine bindings once in-process execution exists
(§10/§6's open item — as of session 57, believed close: a single confirmed
function call away, pending live verification). None of these are wired up
live yet — table lists what's **known to exist** in the binary, not what
we've exercised:

| Lua binding | VA (this build) | Effect / use |
|---|---|---|
| `SetCameraPosition` | `0x00568FA0` (shim) / `0x00569000` (impl) | Direct camera position control |
| `SetCameraOrientation` | `0x005691C0` | Direct camera orientation control |
| `AttachCameraToEntity` | (in the 1129-binding table, `tools/lua-bindings.def`) | Camera-follows-entity mode |
| `SetEntityCameraAlphaRadius` | ″ | Fade an entity near-camera (candidate for hiding Raz's head mesh in FP) |
| `GetBoneWorldPosition` | `0x005B1630` (shim) / `0x005B1690` (impl) | Query a bone's world position. **Fully decoded statically, notes/69 (2026-09-01) — needs no Lua exec.** Returns **six** values: world position *and* euler orientation. Chain: `__thiscall 0x00438B70(ent, name)` (or `0x00438F30(ent, id)`) -> bone, then `__thiscall 0x00492B70(bone, float[16])`, composed with `0x00433E50` and converted by `0x00692890`. |
| `GetPlayerPosition` | `0x005C1C80` (shim) / `0x005C1CE0` (impl) | **Raz's world position with no Lua and no call at all** — see the player-pointer-chain section below. |
| `DumpSkeletonInfo` | `0x00571F10` (shim) / `0x00571F70` (impl) | Dumps an entity's bone map. **Partially traced, session 58**: entity `+0x54` = packed bone-count bitfield `(dword>>5)&0x7FFFFFF`, `+0x5C` = bone-pointer array; per-bone records are 96 (`0x60`) bytes, layout not decoded. |
| `SetEntityAlpha` / `SetEntityVisible` | `0x00593DA0` / `0x00593AA0` | Hide/fade an entity |
| `SetShadowFixedDirection` etc. | (see §8) | Shadow control, untested |
| `LoadNewLevel` | `0x005BBA90` (shim) / `0x005BBAF0` (impl) | **Fully mapped, session 55, NOT via Lua** — see the level-loader entry below and §11. Formats `name` into `workresource\levels\<name>.plb` (or uses `name` as-is if it already has an extension), then calls `SetPendingLevel`. |
> **⚠️ LEVEL-CODE LABELS ARE FILENAME-DERIVED AND AT LEAST ONE IS WRONG
> (2026-08-27).** `CAJA` is labelled "Sasha's Lab" below from the
> `CAJA_sashalab_load.dds` string match. Live, that level's NPC is **Ford
> Cruller** — the conversation is about raking leaves in disguise and the
> equipment in his underground sanctuary, and a close-up confirms the old man
> with the big nose and white hair, not Sasha Nein. **Content beats filename.**
> Treat every notes/55 label as a hypothesis until seen in game; if one is
> wrong on filename evidence, others may be.
>
> **Driving a conversation** (verified 2026-08-27): `tools/input/send_key.ps1`
> works on the dialogue UI — DOWN (`0xD0` **extended**) moves the option
> marker, ENTER (`0x1C`) selects, and picking the last ("I'd better go") option
> ends the conversation and returns live gameplay. ESC opens the **Journal**,
> it does not dismiss dialogue. Needs the window foreground, unlike the
> focus-independent camera/level commands.

| `SetPendingLevel` (not a Lua binding — internal, called BY `LoadNewLevel`'s impl) | `0x004FFA40` | `__thiscall void(void* levelMgr, const char* path, BOOL flag)` on the singleton at `*(void**)0x78BC20` — stages an async level-transition request. **Callable directly from our own DLL code, no Lua needed at all.** See notes/55 for the full 49-code level list and the confirmed `STMU`=menu / `CA*`=Campgrounds / `CAJA`=Sasha's Lab identifications.

Non-Lua, engine-native (already in use): `FirstPersonCamera` mode is
reported to exist in the engine (notes/44, from string/function evidence);
not yet exercised.

### ⚠️ Correction (2026-08-27): the camera bindings do NOT require Lua exec

The table above frames these as "callable once in-process Lua execution
exists". **For the camera bindings that is wrong**, and it has been gating the
wrong work. Static disassembly of the shims (file only — game not launched)
shows every binding is a **two-layer** construction:

- **shim** (e.g. `SetCameraPosition` `0x00568FA0`) — `int f(lua_State* L)`,
  does a stack switch via `0x0078CBCC`/`0x0078CBD0`, calls the impl.
- **impl** (`0x00569000`) — pulls args off the Lua stack, then does **plain
  engine work**.

Strip the argument-marshalling half and `SetCameraPosition` is three float
stores and a bit:

```c
void  *mgr = *(void **)0x0078BC20;      /* same singleton as SetPendingLevel */
if (!Guard(mgr))  return;               /* __thiscall BOOL  @ 0x00504220 */
void  *cam = GetCamera(mgr);            /* __thiscall void* @ 0x004FA5A0 */
*(float *)((char *)cam + 0x08) = x;
*(float *)((char *)cam + 0x0C) = y;
*(float *)((char *)cam + 0x10) = z;
*((unsigned char *)cam + 0x530) |= 1;   /* dirty flag */
```

| thing | address / offset |
|---|---|
| engine singleton | `*(void**)0x0078BC20` |
| validity guard (bail if 0) | `0x00504220` — `__thiscall BOOL(mgr)` |
| get camera | `0x004FA5A0` — `__thiscall void*(mgr)` |
| camera position | `camera + 0x08` (3 × float) |
| camera orientation | `camera + 0x20` (3×3 matrix) |
| camera dirty flag | `camera + 0x530`, bit 0 |
| euler → matrix helper | `0x0069F400` — `__thiscall(outMatrix, float* euler)` |
| Lua arg-count check / get-number | `0x005AF750` / `0x005B0230` |

`SetCameraOrientation` (`0x00569220`) is the same shape, writing a matrix to
`camera+0x20` via the converter.

**✅ 2026-09-02 (`/pd`, static signature scan, NO LAUNCH): `get camera` (`0x004FA5A0`) re-verified
byte-for-byte against Astralathe's `GameApp_GetMainChannel` signature** (one match, same address) —
the engine's own name suggests it exposes camera *channels*, plural (main/cutscene/first-person?),
of which this is only the main one. `[inferred-static 2026-09-02, n=1 signature match, corroborating
a pre-existing independent reading]` Also located while scanning: **`GameApp_CallFunctionf`**
(`__fastcall`, call any of the 1,129 Lua bindings by name with printf-style args, no marshalling) at
**`0x005CEC10`** — new, not previously in this dossier, and not yet exercised. Detail:
`modding-notes/74-boxvisible-located-and-three-astralathe-signatures-confirmed.md` §2.

**Consequence:** free camera movement — the project's stated North-Star-adjacent
goal — needs **no Lua interpreter and no `0x6B0C00`**. The same two-layer trick
should reach any of the 1129 bindings whose real work is field writes or a
`__thiscall` on the singleton; `GetBoneWorldPosition` (`0x005B1630`/`0x005B1690`)
is the obvious next target, since §11 records the FP/orientation work as blocked
waiting for exactly that.

### Player pointer chain - Raz's world position, no Lua, no call (notes/69, 2026-09-01)

`GetPlayerPosition_impl` (`0x005C1CE0`) is fifteen instructions and walks three
pointers off the **same global the camera already uses**:

```
engine = *(void **)0x0078BC20
player = *(void **)(engine + 0x818C)
obj    = *(void **)(player + 0x10)
pos    = (float *)(obj + 0x40)        // x, y, z contiguous
```

Read-only, calls nothing, safe from the render-path hook. Wired as `playerpos`
and `fpcam` in the proxy; **built and deployed 2026-09-01 but NOT yet run.**

* `engine + 0x818C` = the player object - `[inferred-static 2026-09-01, n=3]`,
  corroborated by `GetPlayerPosition` (`0x005C1CE0`), `GetPlayerLSO`
  (`0x005C0BD0`) and `IsRazZLocked` (`0x005B6CB0`), which all reach it the same way.
* `+0x10 -> +0x40` = the position - `[inferred-static 2026-09-01, n=4]`. **Upgraded
  from n=1 on review the same day:** `GetPlayerDist` (`0x005C0920`) walks the identical chain and
  then uses those three floats as a POSITION in a distance computation against another entity.
  That corroborates the *meaning*, not just the offsets, which is stronger than a second identical
  read would have been. **Upgraded again to n=4 the same evening (home PC, notes/70):** the generic
  `GetAbsPosition_impl` (`0x005C0C70`) walks `+0x10 -> +0x40` for ANY entity Lua hands it, and the
  WRITE side `SetAbsPosition_impl` (`0x005C0EC0` -> `0x0046F1B0`) stores the new xyz at
  `node+0x10+0x30`, i.e. **`+0x40` is row 3 (translation) of a row-major 4x4 local transform that
  starts at `node+0x10`** `[inferred-static 2026-09-01, n=1]`. Caveat from the setter: when
  `[node+0xB8]` is non-null the position is converted through that (parent) object first, so
  `+0x40` is LOCAL and equals world only for an unparented node `[inferred-static 2026-09-01, n=1]`;
  `GetPlayerPosition_impl` reads it with no parent check, so the engine treats Raz as unparented.

* **`engine + 0x818C` upgraded to n=5, two of them independent of us (2026-09-02).** `/gr` reports
  Astralathe's source names `GameApp::pPlayer` at byte 33164 = `0x818C` `[reported 2026-09-02]` — an
  independent reader arriving at the same offset from a different direction. And a fifth in-binary
  sighting turned up while decoding the bone route (notes/73): `AttachInventoryEntityToPlayer`'s impl
  **defaults its target entity to `*(*(0x0078BC20) + 0x818C)`** at `0x005B0DC9` when the optional
  argument is omitted — i.e. the engine's own idea of "the player" is exactly our chain's first two
  steps. `[inferred-static 2026-09-02]`

### The `+0xB8` parent caveat — narrowed hard, 2026-09-02 (`/pd`, static)

The one-line caveat above was the last unresolved thing about `playerpos`. Static investigation this
pass:

* **`+0xB8` IS the parent pointer, and it sits in a three-field scene-graph cluster.**
  `[inferred-static 2026-09-02, n=3 independent uses]` — upgraded from n=1. Beyond the setter branch
  already on record (`0x0046F1CC`, `0x0046F223`): `0x00466609–0x0046665C` transfers `[src+0xB8]` to
  `[dst+0xB8]`, **clears the source**, then walks a `+0xC0`/`+0xBC` chain; and `0x00466E9B` sets
  `[node+0xB8]` from a call result, sets `[node+0xBC]` from `[other+0xC0]`, then writes
  `[[node->0xB8]+0x40]+0xC0] = node` — head-insertion into a parent's child list. So the triple reads
  as **`+0xB8` parent · `+0xBC` next sibling · `+0xC0` first child**.
  ⚠️ **Unresolved type ambiguity, stated rather than smoothed over:** at `0x0046F21A` the setter does
  a `rep movsd` of 16 dwords *directly from* `[this+0xB8]`, which type-checks as "pointer to a 4x4",
  while the child-list code dereferences `[parent+0x40]` as a pointer. Those reconcile only if
  different classes share the offset. Not settled.
* **⭐ THE DECISIVE ONE: the read side never composes the parent chain.**
  `[inferred-static 2026-09-02, n=2]` `GetAbsPosition_impl` (`0x005C0C70`, 65 insns) and
  `GetPlayerPosition_impl` (`0x005C1CE0`, 40 insns) contain **zero** references to `+0xB8`. So the
  engine's own "absolute position" accessor **ignores the parent link entirely** — getter and setter
  genuinely disagree (the setter converts world→local through the parent; the getter returns the raw
  row). **Every script-facing position read in this engine already lives with this**, which means
  `playerpos` is exactly as correct as the engine's own `GetAbsPosition` and no more wrong.
* **Can Raz ever get a non-null parent? No evidence found; not ruled out.** `[hypothesis]` A scan of
  `.text` for field writes to `+0xB8` found only the generic node cluster plus null-clears, and no
  player-specific parenting path — but the attach machinery is class-generic, so it *could* apply.
  * **Narrowed again 2026-09-03 (`/gr`, folded by `/pd`): the DATA side has now been checked too, and
    the two agree.** The other way an entity could acquire a parent is by being *authored* as a child
    in the level file — and the `.plb` format cannot express that `[reported 2026-09-03]`. `Domain`
    (the scene-graph container) has `Children`, `Bounds`, `Meshes`, `EntityInitDatas`,
    `DomainEntityInfos`, `RuntimeReferences` — **no parent back-reference and no transform of its
    own**; `DomainEntityInfo` (the per-entity placement record) carries `Position`/`Rotation`/`Scale`
    as bare `Vec3`s with **no parent, attach-to-entity or attach-to-bone field**; `EntityInitData`
    holds only line-collision data. Entities are placed **absolutely** within a domain, so **any
    non-null `+0xB8` is assigned at runtime, by code** — exactly the surface the 2026-09-02 `.text`
    scan already swept.
    ⚠️ **A narrowing, not a closure:** that is the on-disk format, not the runtime class layout, and
    a gameplay system (lift, levitation ball, grab) could still reparent through the generic
    machinery. The practical consequence is about *where* the queued `headpos` reading is worth
    taking: standing still on flat ground is now close to a foregone conclusion, and **the
    informative states are on a moving platform, on the levitation ball, and while grabbed** — which
    is what the board already asks for. This says why those three matter more than the baseline.
  Note the direction of the one attach path we fully decoded: `AttachInventoryEntityToPlayer` →
  `0x00421DD0` → `0x00466DE0` reparents **the inventory item, giving it Raz as parent** — Raz does not
  acquire a parent there. **Two false positives worth recording so nobody re-cites them:**
  `0x0050A36B` writes a *texture* pointer at `+0xB8` (its string is
  `workresource/textures/phatline.tga`) and `0x00440EDC` sets `+0xB0`/`+0xB4`/`+0xB8` together —
  both different classes sharing the offset.
* **Settled by one live reading, now wired:** the `headpos` command prints `node+0xB8` beside
  `playerpos`. Zero while riding a moving platform, the levitation ball, or while grabbed ⇒ the
  caveat is empirically dead. Non-zero ⇒ that is the case that would break an FP camera built on
  this chain, and it names itself.

**This retires the recorded blocker** that FP needed the Lua exec primitive to
learn where Raz is (notes/47, notes/48). It never did.

### Raz's rig bone names, straight out of `.rdata` (notes/69)

`headJA_1` `0x00703F94` - `HeadJA_1` `0x00707FA4` - `headJEnd_1` `0x0070D828` -
`spineJC_1` `0x00704E60` - `handJEndLf_2` `0x0071195C` - `handJEndRt_1`
`0x0070E5F0` - `bubbleJC_1` `0x00713D00`.

`handJEndLf_2` is the default attach bone `AttachInventoryEntityToPlayer` falls
back to (`0x005B0D6A`), which is what makes these real rig names rather than
unrelated strings. `[inferred-static 2026-09-01, n=1 per name]` - none resolved
against a live skeleton. **This retires the notes/48 claim that learning Raz's
rig needed a live `DumpSkeletonInfo` session.**

**Both string addresses re-read directly out of `.rdata` 2026-09-02** as a citation check:
`0x00703F94` → `headJA_1`, `0x0071195C` → `handJEndLf_2`. `[inferred-static 2026-09-02, n=1 each]`

### The bone world-position route, decoded (notes/73, 2026-09-02)

`GetBoneWorldPosition`'s shim is `0x005B1690` (`lua-bindings.def` row
`GetBoneWorldPosition 005B1630`). Stripped of Lua marshalling it is **four engine calls**, and it
**returns SIX values — position AND euler**, not three:

```
bone     = FindBoneByName(owner, "headJA_1")     0x00438B70   __thiscall, ret 4
           GetBoneMatrix(bone, boneMtx[16])      0x00492B70   __thiscall, ret 4
ownerMtx = GetOwnerWorldMatrix(owner)            0x00438390   __thiscall, ret 0
           MatMul(ownerMtx, worldMtx[16], boneMtx) 0x00433E50 __thiscall, ret 8
```

`[inferred-static 2026-09-02]` **Every `ret` form above was read off the binary**, so the calling
conventions are checked rather than assumed — a mismatch would corrupt the render thread's stack
rather than fail cleanly. `0x00433E50` is confirmed a 4×4 multiply by its float body
(`fld [this+0x30] * [B+0xc]`, …); `0x00438B70` iterates `count = (owner[0x54] >> 5) & 0x7FFFFFF`
over an array at `owner[0x5C]`, comparing names via `0x00492C10`.

**Translation is floats 12..14** (row 3 of a row-major 4×4) — corroborated twice independently in
this engine: the node transform (`+0x40` = row 3 of the 4×4 at `node+0x10`) and the camera world
matrix (rows `0x90`/`0xA0`/`0xB0`, translation `0xC0`).

⚠️ **What could NOT be settled statically: which object owns the bone array.** The Lua binding gets
its owner from `0x005B01E0`, which returns `luaEntityWrapper->[0xA8]`; the attach path instead uses
`targetEntity->[0x10]` — the same node our position chain walks. Two different accessors, and the
player object reaching `0x00421DD0` is passed as `[entity+0x10]`. Rather than guess, the `headpos`
command **shape-checks four candidates with pure reads** (a sane bone count at `+0x54` and a sane
array pointer at `+0x5C`) and only calls into the engine against one that passes, reporting which
matched. One run settles it.

### Correction to the binding ABI table (notes/69)

`0x006AEF20` was recorded as "get arg 0 / self". **It is `lua_gettop(L)` - the
argument count**: nine instructions computing `(L->top - L->base) >> 3` over an
8-byte Lua 4 TObject. Read as "self", the optional-argument branches in these
bindings (`cmp argc, 3`) are incomprehensible. Also mapped: `0x005B01B0` = arg ->
script object (the layer above `0x005B01E0`'s native node), `0x006AF600` =
`lua_pushnumber`, `0x006AF650` = push bool, `0x006AFB00` = push int/handle. The
**impl is always shim + 0x60** in this build.

**✅ LIVE-VERIFIED 2026-08-27, first try, in a real level.** Reading returns
plausible world coordinates; writing moves the camera; the new position holds;
and the **rendered image visibly changes** (before/after captures). Full chain
proven: text-file command → dispatch → memory write → holds → renders.
Position went 277→577 in menu space, then a held camera in Sasha's Lab was
flown a three-leg path (+400 Y, +500 X, +600 Z), each leg landing exactly.

**⚠️ SUPERSEDED 2026-08-27 — orientation HAS since been exercised, and direct
orientation control is REFUTED.** This paragraph previously read "orientation
was not exercised… only position has been tested". True when written, actively
misleading now: a session reading it would retry work already known to fail.
Position control stands exactly as described above; the orientation half is
closed. Evidence: `psychonauts-vr-modding-notes/2026-08-27-void-camera-facing-hunt.md`.

| camera field | what it is | result |
| --- | --- | --- |
| `+0x08` | position (3 × float) | ✅ **works** — write persists, view moves, renders |
| `+0x20` | a facing vector | ❌ `[verified-live 2026-08-27]` write **persists untouched but is never read by the renderer** — not the view direction, despite being exactly what `SetCameraOrientation`'s impl writes |
| `+0x50`/`+0x60`/`+0x70`, `+0x80` | **view matrix** (world→camera) | — |
| `+0x90`/`+0xA0`/`+0xB0`, `+0xC0` | **camera world matrix** | ❌ `[verified-live 2026-08-27]` write is **overwritten inside the same frame** |
| `+0x154`, `+0x174` | yaw scalars, radians | derived copies — not inputs |
| `+0x530` bit 0 | dirty flag | set after a position write |

**The two failures bracket the problem, and that is the useful part.** `+0x20`
is a *wrong-field* failure — the write survives, nothing reads it. `+0x90` is a
*timing* failure — the right field, clobbered before use: dumped with yaw 0 and
with yaw 90 applied, the rows came back **bit-for-bit identical**
(`3F2438D3 BF44629C…` both times), and an A/B/A with the camera frozen measured
9.95 % → 10.12 % → 10.00 % black. No visible change at all.

**🎯 The live hypothesis, not yet tested.** `CandB` is the engine's own
camera-update tick (notes/59, including its reentrancy guard). The `camyaw`
write was applied from **`BeforeEye1`, which runs *before* `CandB`**, so
`CandB`'s own camera update recomputes the matrix over it. To land, the write
must happen **after the camera update but before the draw traversal — i.e.
*inside* `CandB`, not around it.** `[hypothesis]` This matters more than it
looks: a matrix write landing ahead of traversal makes the engine **cull against
the new orientation**, which is the entire game for the black-void problem, and
it carries **no free-look clamp** — unlike the input-injection route (Candidate
1), which is hard-limited to 87.4°.

**Why the per-draw register-6 route cannot substitute for it.** Patching the MVP
at `SetVertexShaderConstantF` (§6) happens *after* culling, so it rotates the
image but not the culled set — the void would simply rotate with the view. Any
fix for the void must change what the engine decides to draw, not how the result
is transformed.

One nuance worth keeping: after `camhold 0` the camera **stayed where it was
put** rather than snapping back. That does not prove the engine yielded
control — the test was during a dialogue scene whose camera is static anyway.
The camera *was* observed moving under engine control during the level-load
intro, so it does drive it in some states. Whether a held camera fights the
engine during normal gameplay movement is still an open question.

Full derivation with disassembly in dev-archive
`recon/2026-08-27-camera-control-without-lua/`; the harness that drives it is
notes/67 (`PSYVR_AUTOMATION=1`).

## 9b. Camera ORIENTATION control — SOLVED 2026-08-28 (and the void with it)

`[verified-live 2026-08-28]` **The camera's real transform is a full 4x4 at
`camera+0x150`.** Writing it rotates the view *before* the engine culls, so
culling follows — which answers the black-void problem outright and, with the
position write at `+0x08`, gives both halves of what first-person needs.

### Layout

Four rows at stride `0x10`; the **columns** are the interesting vectors:

| | offsets | meaning |
| --- | --- | --- |
| c0 | `+0x150`, `+0x160`, `+0x170` | right axis, **scaled** (|c0| = 1.538) |
| c1 | `+0x154`, `+0x164`, `+0x174` | up axis, **scaled** (|c1| = 2.052) |
| c2 | `+0x158`, `+0x168`, `+0x178` | forward, unit |
| c3 | `+0x15C`, `+0x16C`, `+0x17C` | forward again, unit (paired with c2) |
| row 3 | `+0x180`..`+0x18C` | translation, one entry per column |

`|c1|/|c0| = 1.334` — exactly the 4:3 aspect, so c0/c1 carry the projection
scaling. Row 3 is `dot(O, c_i)` for a single origin **O = −camera position**,
recovered by solving the 3x3 system rather than assumed (solved
`(19853.09, −449.75, −17815.21)` against campos `(−19856.37, 451.40, 17824.51)`).
It is a standard view-matrix translation in a negated space.

### How to rotate it correctly — all three parts matter

1. **Snapshot first, write absolute.** Rotating in place every frame compounds:
   while the camera is stationary the engine does not rewrite this matrix, so a
   15-degree hold became a spin (c0.z drifted 0.5160 → 0.1235 in 1.5 s). Take
   the engine's basis once, then each frame write *snapshot rotated by theta*.
2. **Rotate all four columns.** c2 and c3 are a matched pair; letting them
   diverge breaks the render.
3. **Recompute row 3 to match**: `row3[i] = dot(O, c_i_rotated)`. Leaving the
   translation on the OLD axes is what wrecked every angle above ~5 degrees —
   the inconsistency grows with the angle, which is exactly the symptom that was
   observed and repeatedly misdiagnosed.

Preserving each column's magnitude across the rotation is harmless but was **not**
the fix — tested and made no difference.

### Result

`[measured 2026-08-28, outdoor camp area]` Black fraction by yaw, with the image
visually confirmed clean at 5, 30 and 90 degrees:

| yaw | 0 | 5 | 30 | 60 | 90 | 180 |
| --- | --- | --- | --- | --- | --- | --- |
| near-black | 3.80% | 2.89% | 2.83% | 2.61% | **2.58%** | 2.44% |

**Turning 90 degrees yields LESS black than not turning at all.** Reversible
(3.80 → 2.58 → 3.88) and bit-stable (three dumps 1.5 s apart identical). 180 deg
points into terrain from a low chase-cam and looks degenerate for that reason,
not from a transform error.

Command: `cambasisyaw <deg>` (0 = off, re-snapshots on every re-arm).

### Why every earlier attempt failed

`[disproved 2026-08-28]` **Every matrix on the camera object is a derived output
that nothing reads.** Held writes survive an entire frame — verified by dumping
them back at each hook site — and the image never changes:

- `+0x20` — what `SetCameraOrientation`'s impl writes.
- `+0x50` — the view matrix. A held `0.0` in `[0][0]` read back as `0.0` at
  end of frame with the picture unchanged.
- `+0x90` — the camera world matrix.

This also **corrects the 2026-08-27 diagnosis** that `+0x90` was "overwritten
inside the same frame". It is not: probing at BeforeEye1 / BeforeEye2 /
AfterBoth shows the write surviving all of them. The earlier dump was taken
*before* the write in both the yaw-0 and yaw-90 runs, which shows the engine's
value either way. Wrong field, not wrong timing.

## 10. Autonomous harness recipe (this game)

- **Foreground-focus grab, FIXED session 54 (durable infrastructure, applies to ALL future input
  automation on this project):** `SendInput`-based automation (playbook §2.2's documented-fragile
  approach, used here because it's proven reliable for THIS engine/window - see below) needs
  `SetForegroundWindow` to succeed first. Single-attempt grabs failed repeatedly in this
  environment: `AllowSetForegroundWindow`'s grant is consumed by the very next
  `SetForegroundWindow` call, not durable across a whole key-send sequence, and another process
  (the tool-execution harness's own console) actively contends for foreground. Fix in
  `tools/input/send_key.ps1`: retry loop that re-grants `AllowSetForegroundWindow(pid)` fresh
  immediately before every `SetForegroundWindow` attempt (up to 5x). Confirmed durable: two full
  13-key `enter_gameplay.ps1` sequences completed with zero failures after the fix, vs. 3
  consecutive failures (aborting after 1-3 keys) before it. A longer pre-input delay makes the
  ORIGINAL bug WORSE, not better (lets a temporary post-launch allowance expire before first use) -
  don't retry that "fix".
- **Full orchestration, CONFIRMED working end-to-end** (session 52):
  `tools/input/auto_shader_dump.ps1` chains silence-intro-audio → launch
  off-screen → `enter_gameplay.ps1` → poll the proxy log for a specific
  marker line (defining "done") → kill the process → restore intro audio,
  in a try/finally so cleanup runs even on error. Built on user request
  ("can we automate this") instead of asking for a manual capture each
  time — this pattern (launch, drive, poll-for-log-marker, cleanup) is
  reusable for any future single-shot capture on this game, not just the
  shader dump it was written for.
- **Launch to a known scene**: `tools/input/enter_gameplay.ps1` drives a cold
  title screen → menu → the CONTINUE save slot via synthetic input (see
  next bullet), then verifies arrival by checking the recovered camera
  position jumps into the world-space tens-of-thousands range (real level
  coords vs. menu-space). **Known unreliable, WORSE than previously
  characterized** (notes/43, notes/49, notes/54): session 54 found the miss
  is not random drift — with the focus-grab bug fixed (so timing was finally
  clean), 3 consecutive runs landed at the EXACT SAME wrong coordinates
  every time. It's a deterministic miss, not "usually works" — simple
  retries will never succeed without an actual parameter/approach change.
  Per playbook §2.1, the robust fix is finding an engine-level level/save-
  jump Lua call instead of walking — still not done; this is now the
  higher-priority path given the walk is proven non-random.
- **In-process input drive, CONFIRMED working (playbook §2.2's exact
  recommendation)**: plain `SendInput(KEYEVENTF_SCANCODE, ...)` (arrows need
  `KEYEVENTF_EXTENDEDKEY`) **with the game window made genuinely
  foreground** (`SetForegroundWindow` + verify + abort-on-fail) reliably
  drives the title screen and menus. This is technically external
  (SendInput from our DLL's own thread, not a hooked input-poll), which the
  playbook calls fragile — but it has worked reliably across many sessions
  for this specific engine/window, likely because Psychonauts doesn't pause
  on background focus the way the playbook warns some engines do. Treat as
  a documented exception, not a contradiction: if it ever starts silently
  failing, the playbook's hook-the-poll-function alternative is the fallback.
- **Frame capture**: `PSYVR_DUMP_EYES=1` writes `psyvr_eye1.bmp` /
  `psyvr_eye2.bmp` to `%TEMP%` every ~5s (readback via
  `GetRenderTargetData`+`memcpy`). No automated image-diff harness built yet
  — comparisons have been eyeballed from these dumps, which is the
  playbook's flagged exception case, not yet automated per §2.4.
- **Silent/offscreen operation** (project-specific, not in the general
  playbook): 4 pre-menu splash `.bik` videos carry embedded audio
  independent of in-game volume settings; temporarily renaming them before
  a launch (restored after) silences test sessions in a shared office. A
  window-offscreen mover (`SetWindowPos`, `SWP_NOACTIVATE`) keeps the game
  rendering/receiving input without appearing on the physical screen.

### Engine-side input internals (sessions 16–18, live-traced)

> **Independent corroboration (`/gr`, folded 2026-09-02)** `[reported 2026-09-02]`, from
> Astralathe's GPLv3 source; nothing copied. Our `IsKeyJustPressed 0x00405930` is the same address
> it names `WasInputPressed(input)`, and it hooks the keyboard poll at **`0x00402CF0`**
> (`GetKeyboardInput`), reading the same DIK-indexed array we mapped at `0x00782D78`. Two
> independent derivations landing on the same addresses is the useful part; **none of its addresses
> have been re-verified in our own binary**, so treat the ones we had not already found as leads.
> Other fixed addresses it publishes, unverified here: `InitUIMenu 0x004FA2E0`,
> `UpdateCheatCodes 0x00506CB0`, `EScriptObject::GetName 0x005CAE50`; `GameApp` fields
> `m_cRazInvincible +0x3E`, `pUIMenu +0x8C64`, `m_bStartupComplete +0x9035`. Also
> **`GameApp_CallFunctionf(GameApp*, EScriptVM*, const char* fn, const char* fmt, ...)`**
> (`__fastcall`, edx unused, `55 8B EC 83 EC 08 8D 45 ? 89 45 ? 6A 00`) — the engine's own
> call-any-Lua-binding-by-name bridge, which would be a cleaner route than raw `lua_dobuffer` if the
> Lua path is ever needed. And a precedent worth knowing: Astralathe's console calls
> `lua_dobuffer` **synchronously inside an `EndScene` vtable hook**, no deferral — precedent, not
> proof, and outside a binding callback is the safe case.

Full mechanism map of how this engine consumes keyboard input — useful
context for why the harness drives menus with window-level `SendInput`
rather than memory-forged DirectInput state:

- **Keyboard is polled DirectInput** — `GetDeviceState` on the keyboard
  device only. Buffered `GetDeviceData` belongs to the **mouse exclusively**
  (classified by `this` pointer: 42/42 hits on the mouse device, 0 on the
  keyboard — session 17). Forging buffered keyboard events is therefore a
  dead end by construction.
- Persistent polled keyboard state buffer (DIK-indexed bytes) at
  `0x00782D78`, stable across polls.
- Consumers: a scan of a **3×21 keybinding table** at `0x00782008`
  (3 categories × 21 slots), plus three hardcoded direct reads —
  DIK_RETURN `0x00782D94`, DIK_SPACE `0x00782DB1`, DIK_ESCAPE `0x00782D79`
  (exactly a title screen's confirm/quit keys) — each feeding
  `SetKeyState(dik, pressed)` at `0x00405470`.
- `SetKeyState` is an edge-detection state machine over a per-DIK byte array
  at `0x00782130` (bit0 = held, bit1 = just-pressed, bit2 = just-released;
  a per-frame sweep at `0x00405590` clears the edge bits). It also services
  a **one-shot listener slot** — callback pointer `[0x00782F14]`, userdata
  `[0x00782F18]`; the callback auto-unregisters when it returns 0.
- Above that sits a generic binding-abstraction layer (`0x004055D0`)
  translating keyState bits into per-binding-slot 0x00/0xFF digital-action
  bytes, with query helpers `IsKeyHeld` `0x00405910` / `IsKeyJustPressed`
  `0x00405930` (plus a third, uninspected, ~`0x00405950`).
- **Why buffer-forging never advanced the title screen** (session 17's
  decisive test): a forged DIK_SPACE press verifiably reaches `SetKeyState`
  with correct edge semantics (`0x00`→`0x03` state transition observed), but
  the one-shot listener slot is **unarmed** (`0x00000000`) while idling at
  "Press [] to begin" — the title screen listens via some other, untraced
  path. `SendInput` at the window level (above) sidesteps the question.

## 11. Dead ends & false leads (save future time)

> ### ✅ THE CULL TEST HAS A NAME, AND THE HUNT FOR IT IS CLOSED AS A *LOCATION* (`/gr`, folded 2026-09-02)
>
> Four sessions failed to find "the cull test" and the honest conclusion at the time was that
> there might not be a single 6-plane frustum function to find. Both halves of that turn out to
> be right, and the reason is now known. All `[reported 2026-09-02]` from `/gr` (Astralathe's
> GPLv3 source and PsychoPortal's level-format work; nothing copied, neither is installed):
>
> - **The per-object visibility decision is `ECamera::BoxVisible(EBox3 box /*by value*/,
>   void* pVisCache, bool)`**, `__thiscall`, prologue
>   `55 8B EC 83 EC 28 89 4D ? 8B 45 ? 8A 88 ? ? ? ?`. Sibling:
>   `ECamera::CalculateScreenDiagonal(EBox3*)` (the LOD metric),
>   `55 8B EC 83 EC 44 89 4D ? 8B 45 ? 8B 48 ? 89 4D ? C7 45 ? 00 00 00 00`.
>   **✅ LOCATED 2026-09-02 (`/pd`, static signature scan, NO LAUNCH): `BoxVisible` = `0x004CDC60`
>   (unique match); `CalculateScreenDiagonal` = `0x004D03B0` (unique match).**
>   `[inferred-static 2026-09-02, n=1 each]`. Disassembling `BoxVisible` beyond the prologue found
>   two cross-corroborations for free: it tests **bit 4 of `camera+0x530`**, the same flags byte
>   `SetCameraPosition` sets bit 0 of (dossier §9) — two independent sessions now agree on that
>   field — and it reads **`engine+0xA1`**, the exact "Visibility Tree Culling" byte the 2026-08-24
>   A/B test (notes/63) toggled to a measured null result. The disassembly shows *why* that null was
>   real rather than a broken toggle: the flag gates only one of several branches, and the
>   `camera+0x530` check above it can return early regardless of the flag's state. It is also
>   **self-recursive** (calls itself once per box side via a helper at `0x4130B0`, unexamined) —
>   note this before designing any hook, since a naive counter would double-count per recursion.
>   Full write-up: `modding-notes/74-boxvisible-located-and-three-astralathe-signatures-confirmed.md`.
>
>   **✅ FULLY DISASSEMBLED 2026-09-03 (`/pd`, static, NO LAUNCH) — and the two sentences above are
>   both `[disproved 2026-09-03]`.** `[inferred-static 2026-09-03]`
>   Listings: `dev-archive/recon/2026-09-03-boxvisible-disassembled/`; write-up: `modding-notes/75-…`.
>   - **The helper `0x004130B0` is a plain `Vec3 += Vec3`** (`__thiscall`, `ret 4`, returns `this`,
>     three `fld`/`fadd`/`fstp` pairs). It has nothing to do with recursion: `BoxVisible` calls it
>     twice, once per box **corner** (`0x4CDD11`, `0x4CDD34`), to translate the box by the camera's
>     `Vec3` at `[this+8]`.
>   - **The self-call is a one-shot DELEGATION, not recursion.** `0x4CDCE1` re-pushes the by-value
>     box and calls `0x4CDC60` with `ecx = [0x788CB0]` — **a different camera object** — and only
>     when that global is non-null, `this` is the camera manager's element `[0]`, and `this` is not
>     already that camera. It cannot recurse without bound, so "a naive counter would double-count
>     per recursion" was guarding against the wrong hazard. A hook still has to survive **one**
>     re-entry.
>   - **Signature settled:** `ret 0x20` = 24 bytes of by-value box (`[ebp+8]`…`[ebp+0x1c]`, two
>     `Vec3`s) + `pVisCache` + `bUseCache`.
>   - **⭐ The engine ships its own cull-camera override, with a public setter/getter pair.** Every
>     reference to the global `0x788CB0` resolves: **`0x004D0DA0` = `void __cdecl Set(ECamera*)`**
>     (six instructions, two of substance), **`0x004D0DB0` = the getter**, `0x004CAD80` = a `__thiscall` teardown
>     guard that clears it if it points at `this`, `0x004D36EF` = a level-teardown clear.
>     **Culling from one camera while rendering from another is a built-in facility, reachable by
>     one call and no hook.** ⚠️ Those names are *ours, describing behaviour* — the binary has no
>     symbols — and nothing about the facility has been exercised.
>   - **Two one-bit disables — one shared, one not.** `[cam+0x530]` **bit 4** clear ⇒ *both*
>     `BoxVisible` and the frustum test `0x004CDB70` return "visible" immediately (the identical
>     three instructions open each). `[cam+0x531]` **bit 0** clear ⇒ the PVS stage is skipped while
>     the frustum test still runs — ⚠️ that bit is **not** referenced anywhere in `0x004CDB70`
>     `[measured 2026-09-03]`; it gates only the PVS stage, which lives in `BoxVisible` alone.
>   - **The gate order, fully mapped:** delegation check → (if `bUseCache`) translate both corners →
>     PVS gate, entered only if `[this+0x514] != -1` **and** `[cam+0x531] & 1` **and**
>     `byte [[0x78BC20]+0xA1] != 0`, querying `0x00489A60` and caching via `0x00489C00`/`0x00489BB0`
>     — **all three keyed by `[this+0x514]`, the camera's leaf index** → frustum test `0x004CDB70`
>     last. Nothing in the PVS path reads orientation, which is direct structural support for the
>     "PVS steps with position, frustum varies with yaw" model below. ⚠️ `0x00489A60` is called "the
>     PVS query" on the strength of its arguments and caching key; **its body is not disassembled**.
>   - **⚠️ The cheapest first test is now a flag, not a trampoline:** clear `[cam+0x530]` bit 4 on the
>     active camera and the whole world should draw regardless of aim. **If geometry still vanishes,
>     `BoxVisible` is not the gate producing the void** and the cause is somewhere this analysis has
>     not looked.
>   **Not yet built: any hook, counter, or mitigation** — deliberately deferred, see notes/74's
>   "what is NOT established" section, and notes/75's.
> - **⭐ Culling has TWO gates in series, and only one of them follows the camera matrix.**
>   The `.plb` level format ships a **`VisibilityTree` separate from the `CollisionTree` and the
>   `NavMesh`** — an octree plus one bit-buffer per leaf, sized from `LeavesCount − 1`, i.e.
>   **one bit per other leaf: a from-region PVS**. So the PVS gate keys on **which leaf the camera
>   is in** (position, orientation-independent), while the frustum gate keys on the
>   **`camera+0x150` basis** (which the 2026-08-28 yaw sweep proved).
>   **This partly supersedes the August guess that one octree served both collision and
>   visibility — there are two trees.**
> - **The diagnostic that separates them, and it is cheap:** frustum culling varies *smoothly with
>   yaw*; a PVS shows a *step change with position and none with yaw*. So **a residual void that
>   survives every rotation is the PVS gate's signature, not a transform bug** — which is exactly
>   the shape of the residual left after 2026-08-28 got the void from 92% to 18.5%.
> - **Bears directly on first person vs. the free camera:** moving the eye to Raz's head is a small
>   translation that stays inside the same leaf, so **FP is probably unaffected by the PVS gate**.
>   ⚠️ **Downgraded to `[hypothesis]` 2026-09-03 (`/gr`, folded by `/pd`).** That rests on an
>   unstated assumption — *that leaves are large compared with the eye offset*. Room-sized leaves and
>   it holds; a fine-grained tree and moving the eye to Raz's head could cross a leaf boundary and
>   change the visible set, which would present as geometry popping the instant first person engages
>   and is **very easy to misdiagnose as a transform bug** — the exact failure shape this project has
>   hit before.
>   - **The named static test:** a leaf stores only `PrimitiveIndex`/`PrimitiveCount`, **not its own
>     bounds**, so it is a regular octree and leaf extent is implied by depth — root `SSECube`
>     extent / 2^depth, with mean depth following from `LeavesCount` (8^d leaves at depth d)
>     `[reported 2026-09-03]`. Read those two numbers from one real level and compare the resulting
>     leaf scale against the ~11.6-unit eye-to-Raz offset. **A few hundred leaves per level** ⇒
>     room-scale, the FP reasoning holds. **Many thousands over a small level** ⇒ popping on FP
>     engage is a PVS symptom, not a matrix bug.
>   - ⚠️ **The cost is higher than "a read of a file already in the install"** `[measured 2026-09-03]`:
>     there are **zero loose `.plb` files** in the install. Levels ship as **`PPAK` containers** —
>     `WorkResource/PCLevelPackFiles/*.ppf`, **50** of them (beside 50 `.apf`), up to 33 MB, header magic `50 50 41 4B`,
>     interior interleaving asset paths with compressed data. The item stays `[PD]` (no game needed)
>     but means *parse PPAK → find the level binary → walk to the Octree*, not a two-field read.
>   - ⚠️ **Licensing:** the format is implemented by a GPL-3.0 library we may study but not copy —
>     parse the fields with our own code, or use a third-party tool *as a tool*.
>   A *flown* free camera is affected — outside the level the current leaf's visible set can be
>   empty, blacking the screen for reasons that have nothing to do with the transform. Worth
>   remembering before diagnosing a black free-camera frame as a matrix bug.
> - `pVisCache` is plausibly the decoded current-leaf visible set, cached between per-object calls
>   `[hypothesis]`; cheap check is whether the pointer is stable while the camera is still.

- **Direct camera ORIENTATION control (2026-08-27)** — refuted two different
  ways, and the pair is diagnostic. `camera+0x20` (what `SetCameraOrientation`'s
  impl actually writes) is **never read by the renderer**: the write persists
  untouched and the view does not move. `camera+0x90` (the real camera world
  matrix) is **overwritten inside the same frame**: yaw 0 vs yaw 90 dumps came
  back bit-for-bit identical, A/B/A measured 9.95/10.12/10.00 % black.
  `[verified-live 2026-08-27]` **Position control is unaffected and still
  works.** The remaining orientation route is a *timing* fix, not a field hunt —
  land the write inside `CandB` after the camera update (§9).
- **Patching the per-draw register-6 MVP to fix the void** — cannot work in
  principle, so don't measure it: register 6 is consumed *after* culling, so
  rotating there rotates the image and the void together. `[inferred-static]`
  The void is a culling problem; only something upstream of draw traversal can
  change it.
- **Immediate-mode `GetDeviceState` mouse injection (2026-08-27)** — the hook
  chain installs perfectly (IAT patch → `CreateDevice` → `GetDeviceState` on
  `GUID_SysMouse`, all three confirmed in the log) but injecting
  `mousedx 3000`/`4000` did **not** rotate the camera; the forward vector at
  `+0xB0` showed only low-bit jitter. `[verified-live 2026-08-27]` A later
  session found why: this game's mouse deltas arrive through **buffered
  `GetDeviceData`** (3598 calls observed while the mouse moved, with
  `WM_MOUSEMOVE` and `WM_INPUT` both at zero) — so the hook was on a real but
  *unused* path. Hooking `GetDeviceData` is the untested continuation, not a
  fresh idea. **Note the shape of this mistake:** "the hook installed" was read
  as "the route works", when installation only proves the function exists.

- **Forging keyboard input at the DirectInput-buffer or keyState level**
  (sessions 15–18): refuted twice over — the keyboard never uses buffered
  `GetDeviceData` (mouse only), and a correctly-forged polled-buffer press
  reaches the input state machine but the title screen doesn't consume it
  via the traced one-shot listener slot (see §10's input-internals map).
  Drive menus with window-level `SendInput` instead.
- **UI-shrink-for-comfort via shader classification** (§8): fullscreen
  fade/backdrop overlays are indistinguishable from real HUD elements at
  the shader level. Needs per-draw geometry/texture identity, not shader
  signature.
- **First-person anchor = centroid of "far from eye" skinned origins**
  (session 49/51): backwards on two counts. (1) The near-eye rejection
  filter (`< 0.35×focus`) was tuned to reject "camera junk" but Raz himself
  sits only ~11.6wu from the eye (a rigid chase-cam offset) — the filter
  rejected the very entity it needed. (2) Averaging (centroid) multiple
  accepted origins mixed Raz with unrelated NPC skinned draws, producing
  large frame-to-frame jumps. **Fix**: track the single **nearest**-to-eye
  32-bone (register-96) origin each frame — no averaging, no near-eye
  rejection. Validated stable on the monitor afterward.
- **Hardcoded +Y world-up for the FP height lift** (session 51): this
  engine's levels do **not** share one up-axis — confirmed live, the start
  area reads `baseUp≈(0,-0.01,1.0)` (+Z-up) while a later area reads
  `baseUp≈(0,0.98,0.18)` (+Y-up). A fixed +Y lift moves the eye *sideways*
  in a +Z-up area instead of up. Must use the camera's live up vector,
  recomputed per frame, not a hardcoded axis.
- **Recovering an entity's full world *orientation* by inverting
  `WVP·P⁻¹·V⁻¹`** (session 51, `RAZAXIS` probe): works for the
  **translation** (a point survives the round-trip) but the recovered
  upper-3×3 (meant to be the model's X/Y/Z axes in world space) comes out
  **rank-collapsed** — axes that should be perpendicular measure
  `X·Y ≈ 0.9`. The perspective divide is not invertible for direction
  vectors the way it is for a single point; this whole class of "recover
  full orientation from a render-level matrix inversion" is a dead end for
  this game. An orientation source needs to come from somewhere else (Lua
  `GetBoneWorldPosition` sampled across 2+ bones, or a dedicated
  orientation binding) once Lua exec exists.
- **FP-in-menu phantom lock** (session 51): the nearest-to-eye Raz detector
  also fires on the title/menu/"brain" screens, which have small decorative
  skinned characters — it latches one of those as "Raz" and visibly shakes
  the menu. Fixed with a runtime `F4` toggle so FP only ever engages when
  explicitly turned on in real gameplay, not by default.
- **First-person world-position move producing zero visible eye
  displacement** (session 51, **cause still unknown as of session 53, but
  two more hypotheses ruled out with hard numbers**): a computed
  212-world-unit forward translation produced no visible eye movement — Raz
  stayed in front of the (supposedly relocated) camera. Session 51's
  hypothesis was an unverified row/column-vector convention; **session 52
  disassembled the real shaders and CONFIRMED the convention is correct**
  (§6). Session 53 went further: **two isolated, fully-automated empirical
  tests proved the entire composition pipeline propagates a translation
  with EXACT correct magnitude** — a raw 500wu injection into `T` measured
  back at ~500-540wu, and (more precisely) a 500wu forced `razWorld` offset
  driving the FP-specific `X1` construction measured back at *exactly*
  `500.0wu`, zero variance across 16 samples, using the SAME `T=Identity`
  monitor-preview conditions the real FP tests actually ran under. **The
  composition math is not the bug — it is proven correct end-to-end.**
  New leading suspect: `g_razNearValid` (the nearest-to-eye Raz lock)
  flickering during real-time gameplay — Raz's skinned draw isn't
  guaranteed every frame, and losing the lock falls back to a completely
  different, chase-cam-relative shift, which would produce exactly the
  "camera fighting for a position" / "moves relative to terrain" symptom
  the user described (dynamic instability, not a static wrong offset —
  which is what a math error would look like). Not yet tested — needs a
  real gameplay capture logging the lock's frame-by-frame hit rate, not
  just the title-screen-only tests done so far. See notes/53.

## 12. Open risks toward the North Star

The North Star itself (stereo + head tracking in a real headset) is **already
achieved and confirmed** on a Quest 3 — this section is about the current
Phase 7+ sub-project (first-person) and beyond, not the core conversion:

- ~~Register-6 convention unverified~~ — **CLOSED session 52**, confirmed
  correct via shader disassembly (§6).
- ~~Composition order (X1*T / Transpose(P⁻¹·T·P)) unverified~~ — **CLOSED
  session 53**, two isolated empirical tests measured EXACT correct
  propagation of a known translation through the full pipeline including
  the FP-specific X1 construction. The FP world-space-move bug is real and
  still open, but the render-level transform mechanism itself is now fully
  vindicated end-to-end (shader convention + composition math both
  confirmed correct). New leading suspect: `g_razNearValid` lock
  reliability during real gameplay (§11) — untested, needs a real
  gameplay capture, not more transform-math verification.
- ~~Lua in-process execution is unfinished (multi-session lift)~~ — **MOSTLY
  RESOLVED STATICALLY, session 57, downgrade from "multi-session lift" to
  "one function call, pending live verification."** `0x6B0C00(L, buf, len,
  chunkname_or_NULL)` was fully traced as a complete compile-and-protected-
  call primitive — it takes a raw buffer directly (confirmed via its
  internal structure: compiles via `0x6B0C40`→`0x6BC550`/`0x6B0B30`, then
  calls `0x6B08E0(L,0,-1)`, a confirmed `luaD_pcall`-style protected
  executor, if compilation succeeded). **No `lua_pushstring` is needed at
  all** — notes/50's premise that the push-side had to be found or the
  compile/run pair fully decoded turned out to be satisfiable by the second
  option. Still open before this counts as done: (1) ~~runtime `lua_State*`
  capture (heap address, changes per launch)~~ — **CLOSED STATICALLY
  2026-09-02, see below**, (2) thread/reentrancy safety (call from our own
  frame hook, not from inside a binding callback), (3) a live trivial-payload
  test before anything camera-related. This still gates the clean,
  engine-native fix for first-person (`SetCameraPosition`/`GetBoneWorldPosition`)
  and everything after it (hand IK, hand mesh hiding, shadow verification) —
  but the estimated cost to unblock it dropped sharply. See notes/57.

  > **✅ (1) IS CLOSED: the `lua_State*` does NOT need a runtime capture.**
  > `[inferred-static 2026-09-02, n=1 from our own binary]` `/gr` read Astralathe's
  > source (GPLv3, nothing copied) and reported that `GameApp` embeds its
  > `EScriptVM` at `+0x9A34`, with `lua_State*` the dword at `EScriptVM + 8` — so
  > `L = *(void**)(*(char**)0x0078BC20 + 0x9A3C)`, provided the global really is
  > `GameApp`. **Confirmed against our own exe, not taken on trust:** the
  > `AttachInventoryEntityToPlayer` impl at `0x005B0E0E` does literally
  > `mov eax, [0x78BC20]; add eax, 0x9A34` — our singleton, that exact offset, in a
  > function we decoded independently for other reasons. `/gr` predicted the check
  > would appear as `A1 ?? ?? ?? ?? 05 34 9A 00 00` in `SetTableValue`; it shows up
  > in this function instead, which is a stronger result than the predicted one
  > because it was not the place we went looking.
  > **⬆️ UPGRADED 2026-09-02 (`/pd`, static signature scan, NO LAUNCH): `n=72`, not `n=1`.**
  > `[inferred-static 2026-09-02, n=72]` A whole-binary scan for the same operand pattern found
  > **72 occurrences in `.text`**, and re-running with the operand *fixed* to `0x0078BC20`'s bytes
  > matched all 72 with none eliminated. There is exactly one `GameApp` global in this binary, so
  > every site that fetches `EScriptVM` off it necessarily encodes the same address — this is the
  > strongest corroboration any address in this dossier has. Full scan output:
  > `modding-notes/74-boxvisible-located-and-three-astralathe-signatures-confirmed.md` §4.
  > **Not established:** that `EScriptVM+8` is the `lua_State*` — that half is still
  > `[reported]` from Astralathe alone, and it is the half that would crash if wrong.
  > Read it and print it before passing it to anything.
- **Shadow pass bone-palette assumption is unverified** (§8) — projected
  shadows *may* consume the same bone overrides "for free", but this has
  never been captured live.
- **No automated frame-diff harness** (§10) — visual correctness judgements
  are still manual eyeballing of `.bmp` dumps; slows iteration and risks
  missing regressions the playbook's §2.4 automated-comparison step exists
  to catch.
- **New opportunity, untested live** (session 55/notes/55): a Lua-free level
  loader — `SetPendingLevel` @`0x4FFA40` (`__thiscall`, confirmed via
  prologue/epilogue), reached from the Lua binding `LoadNewLevel`. Calling
  it directly (`(*(void**)0x78BC20, "workresource\\levels\\<CODE>.plb",
  flag)`) could solve the notes/54 automation blocker (deterministic
  door-entry miss) without touching Lua exec or the menu UI at all — worth
  trying before further walk-script tuning. All 49 level pack codes
  catalogued from `WorkResource/PCLevelPackFiles/*.ppf`; `STMU` = the
  menu/brain screen (confirmed — explains every automated capture's landing
  spot this whole project), `CA*` = Campgrounds and `CAJA` = Sasha's Lab
  (both confirmed via embedded strings), others inferred from animation-path
  evidence but unconfirmed. See notes/55 for the full table and caveats.
