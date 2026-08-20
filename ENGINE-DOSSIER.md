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
real gameplay (untested) — see §6, §11, §12.

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
- **UI/HUD**: drawn through a small, distinct family of shaders (10 of 455
  total vertex shaders) that bypass the register-6 world-transform path
  entirely (screen-space, "HUD at infinity"). Given explicit VR depth via a
  separate mechanism (`PSYVR_UI_DEPTH`, a per-eye `c50.x` shift) rather than
  the world-transform correction. **Dead end**: attempting to *shrink* the
  UI viewport for comfort (notes/42) failed live because the game's
  fullscreen fade/backdrop overlays share the exact same UI shader
  signature as real HUD elements — shrinking one shrinks/darkens the other;
  there is no shader-identity-level way to tell them apart, would need
  per-draw texture/geometry classification instead. Demoted to an opt-in,
  off-by-default knob (`PSYVR_UI_SCALE`).
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
(§10/§6's open item). None of these are wired up yet — table lists what's
**known to exist** in the binary, not what we've exercised:

| Lua binding | VA (this build) | Effect / use |
|---|---|---|
| `SetCameraPosition` | `0x00568FA0` (shim) / `0x00569000` (impl) | Direct camera position control |
| `SetCameraOrientation` | `0x005691C0` | Direct camera orientation control |
| `AttachCameraToEntity` | (in the 1129-binding table, `tools/lua-bindings.def`) | Camera-follows-entity mode |
| `SetEntityCameraAlphaRadius` | ″ | Fade an entity near-camera (candidate for hiding Raz's head mesh in FP) |
| `GetBoneWorldPosition` | `0x005B1630` (shim) / `0x005B1690` (impl) | Query a bone's world position — the clean source of Raz's head/shoulder position for FP, once Lua exec works |
| `DumpSkeletonInfo` | `0x00571F10` | Dumps an entity's bone map — the intended way to identify Raz's head-bone index |
| `SetEntityAlpha` / `SetEntityVisible` | `0x00593DA0` / `0x00593AA0` | Hide/fade an entity |
| `SetShadowFixedDirection` etc. | (see §8) | Shadow control, untested |

Non-Lua, engine-native (already in use): `FirstPersonCamera` mode is
reported to exist in the engine (notes/44, from string/function evidence);
not yet exercised.

## 10. Autonomous harness recipe (this game)

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
  coords vs. menu-space). **Known unreliable** (notes/43, notes/49): the
  blind walk-to-the-door portion has drifted and missed on at least two
  occasions; treat as "usually works", not deterministic yet. Per playbook
  §2.1, the more robust fix would be finding an engine-level level/save-jump
  Lua call instead of walking — not yet done.
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

## 11. Dead ends & false leads (save future time)

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
- **Lua in-process execution is unfinished** (multi-session lift, notes/50):
  `luaB_dostring` is an inlined compile-then-run, not a callable
  `lua_dobuffer(L, char*, len, name)` — needs `lua_pushstring` located (or
  the compile/run pair fully decoded to accept a raw buffer), a runtime
  `lua_State*` capture (heap address, changes per launch), and a
  non-reentrant game-thread command pump. This gates the clean, engine-
  native fix for first-person (`SetCameraPosition`/`GetBoneWorldPosition`)
  and everything after it (hand IK, mesh hiding, shadow verification).
- **Shadow pass bone-palette assumption is unverified** (§8) — projected
  shadows *may* consume the same bone overrides "for free", but this has
  never been captured live.
- **No automated frame-diff harness** (§10) — visual correctness judgements
  are still manual eyeballing of `.bmp` dumps; slows iteration and risks
  missing regressions the playbook's §2.4 automated-comparison step exists
  to catch.
