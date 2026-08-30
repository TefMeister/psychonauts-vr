# 44 — Feasibility recon: first-person, hand IK, full body rig, shadows, motion controls

**Date:** 2026-08-19, dev machine. **Question from the user:** is "1st person gameplay + hand IK
+ full body rig + full shadow + motion controls" doable in this game at all?
**Short answer: yes for most of it, with one honest "partially" — and the game is dramatically
more cooperative than a 2005 title has any right to be.** Evidence below; verdicts at the end.

## Evidence 1 — The engine has a built-in first-person camera and a fully scriptable camera API

String recon of `Psychonauts.exe` (4MB, Lua bindings are plain C strings):

- `@FirstPersonCamera`, `FirstPerson`, `SetCamFirstPersonInvertPitch`, `ResetCameraMode` —
  the engine's existing hold-to-look mode IS a first-person camera state.
- 120 camera identifiers total: `SetCameraPosition`, `SetCameraOrientation`,
  `SetCameraFieldOfView`, `AttachCameraToEntity`, `CreateNewCamera`, `SetCurrentCamera`,
  `AssignCameraToChannel`, the whole chase-camera parameter set
  (`SetChaseCameraRadius/Azimuth/Altitude/FixedPosition/Free…`), camera paths, and
  `SetEntityCameraAlphaRadius` — the engine's own "fade the player model when the camera is
  close" mechanism, i.e. exactly the tool needed to hide Raz's head/body in first person.
- `GetCameraPosition/Orientation/ViewVector/RightVector` for reading state back.

Independently of all that, we already own the view matrix end-to-end (BuildViewMatrix inline
hook + head tracking), so a first-person camera can be forced at render level even with zero
game cooperation.

## Evidence 2 — Live animated skeleton data flows through our existing hook (bone probe, live-verified)

`PSYVR_BONE_PROBE=1` (new, kept in the DLL) logged bone-palette uploads on the menu while Raz
idles:

- `c96 n=96` per skinned draw = **32 bones x 3 registers = 4x3 matrices** (orthonormal rotation
  3x3 + translation in the 4th column; model space — the c6 WVP we already patch carries
  world/view/proj on top). Smaller palettes upload at `c64 n=24` (8 bones).
- Sampled once per second: translations breathe smoothly (idle animation), e.g. bone 0
  ty 7.339 → 6.342 → 7.369 → 6.783… **This is the live skeleton, per draw, per eye, passing
  through Hook_SetVertexShaderConstantF — readable AND rewritable.**
- Volume: ~14,600 c96 uploads / 5s window on the menu alone (REGHISTO), all through our hook.
- Lua side: `GetBoneWorldPosition`, `GetBoneID`, `EntityHasSkeleton`, `DumpSkeletonInfo`,
  `AttachEntityToEntityBone`, `ShowSkeletons` — the game itself will hand us world-space bone
  positions by name/id if we can call its script API.

Hand IK mechanics that follow: controller pose (OpenVR, already in-process) → model space via
the world transform recoverable from c6·(V·P)⁻¹ (V and P both cached) → 2-bone IK solve for the
arm chain → overwrite those bones' 4x3 blocks in the c96 upload. Purely render-side; the game's
logic never needs to know.

## Evidence 3 — The engine has real projected-texture shadows, scriptable

- Strings: `Projected Shadows To Texture`, `SetLightShadowEntity`, `SetShadowBlendMode`,
  `SetShadowColor`, `SetShadowCullingBias`, `SetShadowFixedDirection`,
  `SetShadowFixedLightSource`, `DebugShadows`, `DebugShadowReceivers`, `ShowShadows`.
- The frame trace shows heavy render-to-texture activity (816 off-target SetRenderTarget
  switches in ~2 min on the menu) — consistent with shadow/channel passes.
- Key implication: projected shadows re-render the skinned mesh; if that pass uploads bones
  through the same constant path (near-certain — one skinning implementation), any bone
  override we do propagates into the shadow for free. Needs one gameplay trace to confirm.

## Evidence 4 — Lua 4.0 VM in-process = a mod control plane waiting to be claimed

- `$Lua: Lua 4.0 Copyright (C) 1994-2000 TeCGraf, PUC-Rio $`, `dostring`, `dofile`,
  `LuaPackFileDoFile`, a thread-pool scheduler (`Lua Threads in …`), and `pBConsole created`
  (there is a console object). Scripts ship packed in `WorkResource/Scripts/packfile/common.lpf`.
- Every API above (camera, bones, visibility, shadows) is a registered Lua binding. If our DLL
  locates the global `lua_State` and calls the statically linked `lua_dostring` (anchorable via
  the version string / LuaPool strings), we can drive ALL of it from inside the process — no
  per-function reverse engineering.
- Input side: `SetActionTable` / `GetActionState` — the game's action abstraction is
  script-visible, which is the clean injection point for motion-control bindings. Fallbacks:
  DirectInput8 hook (`DirectInput8Create` imported) or the proven SendInput path.

## Verdicts (feature by feature)

| Feature | Verdict | Route |
|---|---|---|
| 1st-person gameplay | **Doable** | Engine's own FirstPersonCamera / SetCameraPosition per frame, or render-level eye override at the head bone; hide Raz via SetEntityAlpha / draw-skip |
| Motion controls | **Doable** | OpenVR controllers already in-process; map to game actions (SetActionTable) or SendInput/DI8; gesture→action for melee/powers |
| Hand IK (visual) | **Doable** | Rewrite arm-chain 4x3 blocks in the c96 upload, 2-bone IK from controller poses |
| Full shadow of the VR-driven body | **Probably free** | Projected-shadow pass consumes the same bone palettes; verify with one gameplay trace |
| Full body rig | **Partial — by design** | Layer upper-body IK (head + arms) over the game's own locomotion animation. Replacing the whole skeleton from 3-point tracking would fight the animation-state-driven gameplay for little gain |
| Hands that *physically* interact | Honest no | Combat/interactions are animation-state driven; grabs/hits stay button/action-triggered even with visually-IK'd arms |

## Recommended attack order (each step useful on its own)

1. **In-process Lua execution** — find `lua_State` + `lua_dostring` (force multiplier for
   everything; also unlocks `GetBoneWorldPosition`, entity queries, level scripting).
2. **First-person prototype** — camera at head-bone position + hide Raz + existing head
   tracking. Comfort rails (snap turn, vignette) come with it.
3. **Motion-controller input** — poses/buttons → action injection; gesture bindings.
4. **Visual hand/arm IK** — c96 override with 2-bone solve; bone-index map discovered
   empirically (perturb one bone at a time) or via RayCarrot's format docs.
5. **Shadow verification** — trace a gameplay frame, confirm bone-override propagation.

## Known hard parts / risks (so nobody is surprised later)

- Psychonauts is a 3rd-person acrobatic platformer: cutscenes and scripted sequences force
  cameras constantly; ledges/poles/levitation-ball in first person are a comfort minefield.
  Expect the first-person mode to need a per-situation fallback to 3rd person (the scriptable
  camera API makes that switch cheap, and VR 3rd person with 1:1 head tracking — what we have
  today — is already a good place to fall back to).
- Bone-index → body-part mapping is per-skeleton work (Raz first, done once).
- Lua 4.0 is ancient; its C API differs from 5.x (tag methods, no registry) — budget a day of
  reading the 4.0 manual before touching the VM.

🤖 Session driven autonomously via Claude Code on the dev machine (string recon + live bone
probe + frame traces; no game files modified, no releases).
