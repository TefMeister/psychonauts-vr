# Psychonauts VR — Status

Last updated: 2026-08-16 (double-call safety test session)

## Latest status (read this first)

**GO: the double-call safety test passed cleanly — green light to attempt the real stereo
matrix injection / dual-render hook next session, no further investigation recommended first.**
This closes the last open empirical question from notes/11 by actually doing the thing (call
`exe+0xFEDA0` twice per frame with unmodified state) instead of continuing to reason about it
statically. Full detail, methodology, and two reusable x64dbg-automate tooling gotchas found
along the way are in `notes/12-double-call-safety-test.md`:

- **15/15 consecutive double-invokes of CandB (`exe+0xFEDA0`) succeeded cleanly** over a
  sustained real-frame window: every one landed at the correct return address with the stack
  pointer byte-identical before/after both calls, entry register state was bit-identical across
  all 15 real hits, and the second call's return value (`eax=1`) exactly matched the first's on
  every hit (no sign of an "already rendered this frame" reentrancy guard silently short-circuiting
  the second call).
- **Baseline frame cadence immediately before and after the double-call window is statistically
  identical** (~0.204s/hit both times) — no lasting corruption, no growing lag, no delayed crash
  once double-invoking stopped.
- **Zero crashes, hangs, or unresolved exceptions** across the whole test. (Two earlier attempts
  this session failed for tooling reasons unrelated to CandB itself — an event-queue
  desynchronization bug, and x64dbg's `rtr`/"run to return" command not being reliably
  call-depth-aware across CandB's ~90-deep nested call tree, landing short and cascading into a
  self-inflicted bad state after 4 iterations — both diagnosed, fixed by switching to a targeted
  single-shot breakpoint at the known return address, and confirmed fixed by the clean 15/15 run
  that followed. Neither failure mode implicated CandB's own safety.)
- **Honest limitation carried forward**: no visual/screenshot confirmation of animation speed was
  performed (verdict rests on stack/register/return-value/timing consistency, not a direct look
  at the screen); six brief, non-repeating, unexplained debugger stops in unrelated-looking DLL
  address space were observed once mid-test and are noted but not folded into the verdict either
  way (didn't correlate with any double-invoke failure, never recurred). Neither limitation was
  judged to block proceeding.
- **Next session**: attempt the actual dual-render hook (per the plan already laid out in
  notes/10 §6 / notes/11 §4) — hook `exe+0xFEDA0`, call it twice per real frame (once per eye,
  proven per-eye `BuildViewMatrix` offset from notes/09) into two render targets stood up via the
  already-hooked `CreateDevice` (notes/06), compositing before the real `Present`. No remaining
  *unscoped* unknowns block this.

## Prior milestone (render-function classification session, still valid)

**The dual-render open question was resolved via static + empirical analysis: the candidate
"render one eye's scene" wrapper function was classified as pure-render-only, converging with
this session's actual double-call test above.** Two launch-then-attach x64dbg captures (see
`notes/11-render-function-classification.md` for full detail):

- **Corrected notes/10's two candidate addresses**: `exe+0x115F36`/`exe+0xFEFEE` were return
  addresses (mid-function), not entry points. Their real entry points were found by backward
  prologue scan + forward-alignment verification: **`exe+0x115610`** (695-instruction body) and
  **`exe+0xFEDA0`** (282-instruction body). **`exe+0xFEDA0` directly calls `exe+0x115610`**
  (confirmed via the exact call/return-address byte offset) — this is the true, fully-confirmed
  call hierarchy, not just an EBP-depth guess. Both fire **exactly once per real Present frame**.
- **Full disassembly of both function bodies (695 + 282 instructions) found zero D3D API calls**
  (all drawing is delegated to nested helpers, matching notes/10's deeper EBP frames) and only 21
  non-stack memory writes total, **none floating-point, none increment/accumulate** — all
  single-shot literal-constant resets or transient-pointer set/clear pairs.
- **Empirical live-memory watch across 8 real frames** (register-resolved effective addresses,
  fixing a first attempt's broken address-parsing) confirmed every write site fires **at most
  once per frame** (not a loop) and targets a **different memory address almost every frame** —
  the opposite of what a persistent game-state variable (timer, position) would look like; that
  would live at one stable address.
- **Verdict: leans safe to call twice**, not airtight (89 nested helper calls were identified by
  address but not individually disassembled — judged not worth a full manual audit this
  session). **Recommended next step, not another investigation**: hook `exe+0xFEDA0` and do the
  actual double-call experiment (call it twice with unchanged matrices first, watch for
  double-simulation symptoms over a sustained window) as the cheapest way to close the remaining
  gap, then proceed to the real stereo hook (per-eye offset + second render target + composite)
  using every already-proven primitive from notes 06/07/09/10.

## Prior milestone (render-loop structure session, still valid)

**The frame's render-loop structure was mapped, closing out the "how does the game
structure a frame" unknown flagged at the end of the write-hook session.** Live x64dbg capture
(three failed/diagnostic attempts plus one fully successful one — see
`notes/10-render-loop-structure.md` §1 for two reusable debugging-harness bugs found and fixed
along the way: launching directly under x64dbg hit a persistent access-violation retry loop this
session, worked around by launching the game normally and attaching after ~15s; and the
automation library's event-queue helper pops newest-first (LIFO) which silently corrupted a live
pointer read until fixed to drain strictly oldest-first) confirmed:

- **Frame order**: `[BuildProjectionMatrix x8, BuildViewMatrix x3] → [~89 draw calls across
  multiple SetRenderTarget/Clear/BeginScene/EndScene brackets] → Present`, every frame, camera
  matrices always built before any draw calls, `Present` always last.
- **The engine already calls `SetRenderTarget` (8x) and `Clear` (3x) multiple times per single
  frame**, with a `SetRenderTarget` burst immediately followed by `EndScene`→`BeginScene`→`Clear`
  — strong evidence of an existing multi-pass structure (most plausibly a shadow/render-to-texture
  pass) that a stereo second-eye pass could piggyback on the same mechanism.
- **A candidate "draw the whole scene" wrapper was identified via an 8-level EBP frame-pointer
  walk from the first live `DrawIndexedPrimitive` hit**: `exe+0x115F36` and `exe+0xFEFEE` are the
  two outermost/best candidates (no symbols, not yet disassembled).
- **Open risk, not yet resolved**: whether that candidate wrapper's call chain does *only*
  rendering or also touches per-frame game-logic/animation state that would double-advance if
  called twice — this is the concrete next step (disassemble those two addresses and check),
  not a new open-ended unknown. Full plan in `notes/10-render-loop-structure.md` §6.

**Proposed next milestone**: disassemble `exe+0x115F36`/`exe+0xFEFEE` to confirm one is a clean
render-only entry point, then attempt the actual dual-render hook: call it twice per frame (once
per eye, using the already-proven per-eye matrix offset from notes/09) into two render targets
stood up via the already-hooked `CreateDevice` (notes/06), compositing before the real `Present`.
If the split turns out not to be clean, fall back to single-render + post-process
reprojection/warp instead of fighting a tangled call graph.

## Prior milestone (write-hook proof-of-concept session, still valid)

**The write-hook mechanism is proven end-to-end — this is the first behavior-modifying
experiment, and it worked.** A real live write was installed at `BuildViewMatrix`
(`exe+0x292480`): on each hit, `pEye`/`pAt`/`pUp` were read, a right vector was computed
(`normalize(cross(normalize(at-eye), up))`), and `*pEye` was overwritten with `eye + right*40.0`
before letting the real function proceed. A second breakpoint directly on the
`call D3DXMatrixLookAtRH` instruction (`exe+0x2924AC`) re-read `*pEye` immediately before the
call executed, to prove the write wasn't overwritten before use. Result: **40/40 writes
succeeded, 39/39 call-site checks matched the written value exactly, 0 mismatches**, sustained
across ~25 seconds / ~40 frames on the title screen's live camera — not a one-frame flicker. No
crash, no anti-tamper reaction. Full detail, exact values, and one important surprise (below) in
`notes/09-write-hook-proof-of-concept.md`.

**Important surprise**: `*pEye` is a live pointer into the camera's own persistent state, not a
fresh copy each call — writes on one hit partially carry forward into what the next hit on the
same pointer reads as its "original" value. This produced **bounded compounding**: the first few
writes on a given pointer stack close to additively, then the game's own camera smoothing visibly
damps it and the value converges to a stable offset rather than diverging unboundedly or being
cleanly reset. This is a concrete, actionable finding for the stereo work, not just a curiosity —
see the recommendation below.

**Proposed next milestone (the real stereo-rendering step)**: this is the big one and will likely
need its own dedicated deep-dive session. Two parts:
1. **Per-eye matrix math**: duplicate the offset logic proven this session for two eyes with
   opposite-sign offsets, but **cache/derive a fresh unmodified base eye position each frame**
   rather than reading back an already-offset `*pEye` (to sidestep the compounding behavior found
   above) — either by capturing the pre-write value once per real frame and computing both eyes
   from that cached base, or by not mutating the shared pointer in place at all and instead
   calling `D3DXMatrixLookAtRH` (or the `BuildViewMatrix` wrapper) twice into two separate output
   buffers.
2. **Actual dual rendering** (the hard, unscoped part): the game's main render loop needs to run
   *twice* per frame — once per eye — into two separate render targets, then composite. This
   needs its own investigation into how the game's frame/draw-call loop is structured (where
   `Present` is called relative to the full scene draw, whether the render path can be
   re-entered cleanly a second time with a different view/projection matrix and target surface,
   whether any per-frame state would need resetting between the two passes). The already-hooked
   `CreateDevice`/`Present` proxy (`notes/06-createdevice-present-hooks.md`) is the natural place
   to stand up a second render target, but nothing about invoking the render path twice has been
   explored yet.

Revisit the gameplay-input blocker (`notes/08`) in parallel/afterward — real player camera
movement will eventually be needed to validate the stereo hook, not just the title screen's
attract-mode animation.

## Prior milestone (live-camera-data session, still valid)

Both camera-matrix hook points were confirmed carrying real, live, changing data. Breakpoints on
`BuildViewMatrix` (`exe+0x292480`) and `BuildProjectionMatrix` (`exe+0x2924D0`) were hit
repeatedly (15 + 45 hits over ~15s) with real `pEye`/`pAt`/`pUp` vectors that drift smoothly
frame-to-frame (proving a live, moving camera, not a cached one-shot value) and a stable,
plausible `rawFov=104.0` / `aspect=1.3333` (4:3) / `zn=10.0` / `zf=50000.0`. This used the
title/attract screen's own animated 3D camera (a real `D3DXMatrixLookAtRH` scene, unlike the
static post-title menu observed previously) rather than player-controlled gameplay — **reaching
actual gameplay was blocked this session by a simulated-input problem**: `SendInput` (VK and
scan-code), legacy `keybd_event`, and `PostMessage` all failed to dismiss the title screen's
"press any key" prompt, despite confirmed-correct OS-level window focus and a validated-working
injection mechanism (control-tested against Notepad). Root cause narrowed to the game's `DIEmWin`
DirectInput hook-based input path not reacting to synthetic input in this environment — not a
debugger, hook, or config problem (full diagnostic trail in
`notes/08-live-camera-data-gameplay.md`).

## Prior milestone (camera-matrix injection-point session, still valid)

**The camera-matrix injection point is identified with concrete, live-confirmed addresses.**
Two small wrapper functions were fully disassembled — `exe+0x292480` (builds the view matrix,
`BuildViewMatrix(pOut, pEye, pAt, pUp)`, all three vector args passed as pointers straight
through to `D3DXMatrixLookAtRH`) and `exe+0x2924D0` (builds the projection matrix,
`BuildProjectionMatrix(pOut, rawFov, aspect, zn, zf)`, feeding a FOV unit conversion into
`D3DXMatrixPerspectiveFovRH`) — both are the actual hook points for per-eye view/projection
matrix injection. Also resolved, with live evidence: **the game uses the shader-constant
pipeline, not the fixed-function pipeline** — `IDirect3DDevice9::SetTransform` was never called
by the game's own code across ~75 seconds of live observation (zero hits on a real breakpoint at
its resolved address, versus 300+ hits on `SetVertexShaderConstantF` and 40+ `Present` frames in
the same window); the `SetTransform` hits that did occur came from the D3D9 runtime's own
`CreateDevice` bootstrap, not the game. The specific `SetVertexShaderConstantF` call/register
range carrying the actual camera matrix wasn't pinned down yet — the observation window was the
main menu (no active 3D camera), so `D3DXMatrixLookAtRH` never fired live and the
`SetVertexShaderConstantF` hits observed all looked like 2D UI/screen-space constants from one
call site (`exe+0x27EF03`). Full detail, full disassembly listings, and the concrete next step
(reach real gameplay, watch for a `Vector4fCount=4` constant upload) are in
`notes/07-camera-matrix-injection-point.md`.

**Proposed next milestone**: get the debugger past the main menu into actual gameplay (simulated
input or attach-after-manual-launch) and repeat the `SetVertexShaderConstantF` observation to
find the exact call site/register range for the camera matrix upload — that's the second,
possibly primary, injection point alongside the two wrapper functions already found. In
parallel/afterward, use the already-hooked `CreateDevice` (`notes/06-createdevice-present-hooks.md`)
to create a second render target, purely to prove a second surface can be stood up against this
device/driver — still no compositing/stereo logic yet, just infrastructure.

## Prior milestone (CreateDevice/Present vtable-hook session, still valid)

The proxy `d3d9.dll` (`tools/proxy-d3d9/`) **vtable-hooks `IDirect3D9::CreateDevice` (slot 16)
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

See "Latest status" at the top of this file for the current one — this section is left as
historical context from the first live-debug session; each subsequent session's "Latest status"
supersedes it.
