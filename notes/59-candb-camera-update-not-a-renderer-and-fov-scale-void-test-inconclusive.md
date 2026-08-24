# 59 — CandB is the camera's own update tick (not a renderer); FOV_SCALE void test built but inconclusive on this monitor

**Date:** 2026-08-24, dev machine. Two threads: (1) closing out the multi-session hunt for the
void-behind-player CPU-side cull test (notes/40's open bug, top priority per the user 2026-08-23),
and (2) implementing + monitor-testing candidate 3 (widen `PSYVR_FOV_SCALE`) once the cull-test hunt
didn't converge after several live x64dbg + angr-decompile passes.

## 1. Cull-test hunt: closing finding — CandB is not the renderer

Several live sessions (x64dbg attach + breakpoints, EBP-chain walks, and `x64dbg-skills:decompile`
angr pseudocode on the candidate functions) traced both `BuildViewMatrix` call sites
(`exe+0x00692480`) that fire per real frame:

- **"Site B"** (`exe+0x67F296`, fires ~2x/frame): its downstream chain
  (`exe+0x520400` → `0x51D080`/`0x51D200`/`0x51D3A0`) decompiles as a **world-space icon/HUD-marker
  overlay renderer** — opcode-tagged command-queue pushes (`sub_67ee30(4,...)`, `sub_67ee30(0x33,...)`
  etc.), a UV-rect-shaped `(0,1,0,1)` default, and a big material/texture-handle selection table keyed
  off small state flags. Not the main-scene culler. Two full sessions were spent on this chain before
  this was recognized — flagging clearly so a future session doesn't re-walk it expecting a frustum
  test.
- **"Site A"** (`exe+0x512C9B`, fires exactly once/frame): its caller is inside **`CandB`
  (`exe+0x4FEDA0`)** — the exact function this mod's own `Hook_CandB` already inline-hooks and calls
  TWICE per real frame (once per eye) for stereo. Decompiling `CandB` in full shows it is **not a
  renderer at all**:

  ```c
  char sub_4feda0(struct_3 *idx)   // CandB
  {
      EnterCriticalSection(&g_791490.DebugInfo);
      if (idx->field_9036 & 1) {              // reentrancy guard
          v8 = idx->field_0->field_18();       // short-circuit: vtable dispatch only, skip everything below
          LeaveCriticalSection(&g_791490.DebugInfo);
          return v8;
      }
      // ... a long, branchy sequence of per-entity subsystem tick calls (physics/audio/animation-
      // shaped helper calls gated on small per-entity flag bytes) - BuildViewMatrix (Site A) is
      // nested inside one of these, consistent with this being the CAMERA entity's own Tick() ...
      LeaveCriticalSection(&g_791490.DebugInfo);
      return 1;
  }
  ```

  Nothing in `CandB`'s body calls `DrawIndexedPrimitive`, `SetVertexShaderConstantF`, or any of the
  WVP-composition functions found on the Site B side. **This resolves the open question from
  notes/10 §5** ("not yet confirmed whether calling the render wrapper twice would double-advance
  game logic if the outer function were naively re-invoked") — for `CandB` specifically, it can't:
  the reentrancy guard (`idx->field_9036` bit 0) makes the mod's own second per-eye invocation take
  the short-circuit vtable-dispatch path instead of re-running entity updates. Worth keeping in mind
  for any FUTURE double-call hook of a *different* function, since this guard is local to `CandB`,
  not a general engine property.

**Net result:** camera-update (Site A/`CandB`) and the real draw-call chain are siblings under some
higher per-frame dispatcher, not nested — walked one level above `CandB` (through what looks like an
SEH "safe call" wrapper around `exe+0x4659C0`) without reaching that common ancestor or a confirmed
frustum/cull test. `DrawIndexedPrimitive` was also never cleanly resolved live off the real device
vtable (two attempts at catching the proxy's `Direct3DCreate9` call at the right moment both stalled
for reasons not fully diagnosed — possibly just slow asset-loading on this dev PC, not investigated
further). **The actual cull mechanism (geometric frustum test vs. a simpler room/portal-active flag)
remains unidentified after four sessions of static+live RE effort.** Per the user's direction, this
thread is paused in favor of candidate 3 below rather than continuing to spend RE budget on it.

## 2. Candidate 3: widen `PSYVR_FOV_SCALE` — implementation

Two small, additive changes to `proxy_d3d9.c` (dev-archive working copy at
`tools/proxy-d3d9/proxy_d3d9.c` — **note:** this is the actively-built copy; the public `-mod` repo's
`proxy-d3d9/proxy_d3d9.c` is a stripped release snapshot without the FP/diagnostic globals and is
NOT what `build.ps1` in that repo can build against without the vendored `vr-bridge/openvr-sdk`
sibling folder, which only exists under dev-archive):

1. **`PSYVR_FOV_SCALE` ceiling raised 2.5 → 4.0** (clamp only; default unchanged, still read from the
   launcher's `PSYVR_FOV_SCALE=1.8`). Exploratory headroom for this specific test, not a claim that
   values this high belong in a shipping default.
2. **New `PSYVR_FAKE_POSE_YAW_DEG` env var** overriding the existing `PSYVR_FAKE_POSE` sway's yaw
   amplitude (was hardcoded `0.44rad`/~25°; default behavior unchanged when unset). Lets a monitor
   test sweep well past the gentle default sway without a new test harness — reuses the exact
   existing sway mechanism/script pattern (`auto_ht_test.ps1`'s silence→launch→poll→kill→restore
   shape).

Built clean via `tools/proxy-d3d9/build.ps1` (same two pre-existing, unrelated warnings as every
prior session — `EXTERN_C` redefinition, `Direct3DCreate9` dllexport-on-redeclaration). Deployed
build's `d3d9.dll`/`openvr_api.dll` are now live in the game directory; **the previously-deployed
Aug-20 build is backed up as `d3d9.dll.pre-notes59-backup` / `openvr_api.dll.pre-notes59-backup`** in
the same game folder — restore by copying those back over the live names if this build needs to be
rolled back.

## 3. Test result: inconclusive — wrong scene, not a wrong fix

Test script (`fov_void_test.ps1`, scratch-only, not committed — trivial to recreate from
`auto_ht_test.ps1`'s pattern if wanted): silence intro videos → launch with
`PSYVR_FAKE_POSE=1 PSYVR_FAKE_POSE_YAW_DEG=95 PSYVR_DUMP_EYES=1 PSYVR_FOV_SCALE=<value>` → move
offscreen → poll-copy `%TEMP%\psyvr_eye1/2.bmp` every 5s for 30s → kill → restore. Ran at
`FOV_SCALE=1.8` (today's shipped default) and `2.5` (the old max), 12 screenshots total.

**Both settings show the same result: no visible void, at any sampled phase of the ±95° sway.** The
title/menu screen's "brain vault" background is a **densely enclosing decorative pattern that wraps
close around the camera on every side** — visually nothing like the sparse outdoor gameplay geometry
(terrain, sky, distant structures) where the user and notes/40's original playtest actually observed
the void. This is a **real methodology finding, not a negative result on the fix**: this dev PC's
monitor-only test setup, as currently built, **cannot reproduce the void bug at all**, so it cannot
yet demonstrate whether raising `PSYVR_FOV_SCALE` helps, at either the old or new ceiling.

Reaching real outdoor gameplay to test properly was considered and deliberately not attempted this
session: `enter_gameplay.ps1`'s blind door-entry timing was already flagged unreliable as of session
54 (2026-08-20) and never confirmed fixed; combining it with `PSYVR_FAKE_POSE` active from process
start (required, since the pose mode is a startup-only env read, not toggleable mid-session) adds a
second failure mode on top — Psychonauts' movement reads camera-relative, so a ±95°-swaying fake
camera during the timed walk-to-door sequence would very plausibly send the blind micro-steps in the
wrong direction, on top of a script that was already unreliable without that interference. Judged not
worth the time this session; flagged as the concrete blocker for a real monitor-only test.

## 4. Recommendation

**Not raising the shipped default from 1.8 based on this session** — there is no real screenshot
evidence either way yet, and guessing a higher default without evidence would be exactly the kind of
unverified change this project's own practice (dev-archive/modding-notes only until verified) exists
to avoid. The raised code ceiling (4.0) and the new `PSYVR_FAKE_POSE_YAW_DEG` knob are left in place
as working, tested-to-build infrastructure for whichever comes first:

1. A future session fixes/replaces gameplay entry (the untested `SetPendingLevel` Lua-free level-jump
   from notes/55/00-status.md is the leading candidate — bypasses the door-timing problem entirely)
   and reruns this same FOV_SCALE A/B in a real outdoor level, or
2. The user tests it directly on the home PC in a real headset, where reproducing the bug is trivial
   (just turn your head) and doesn't depend on any of this session's synthetic-pose machinery.

Either way, **the architectural ceiling from notes/40 still applies regardless of what FOV_SCALE value
gets picked**: this is a symmetric-frustum widen, so it can shrink the pop-out for a partial turn but
can never cover a true ~180° look-directly-behind, and every increment trades real per-frame GPU cost
(more geometry submitted and rendered, most of it off-screen) for a fixed, non-full-coverage gain. If
the eventual real-scene test still leaves an unacceptable void at the practical distortion ceiling
(likely somewhere in the 3.0-ish region before the perspective math gets too extreme to be usable —
not measured this session), the real fix is still one of candidates 1/2 (feed head yaw into the
actual camera, or pad only the CPU-side cull test) — this stopgap does not replace that RE work, it
buys time.

🤖 Live x64dbg (attach + breakpoints + EBP-chain walks) and `x64dbg-skills:decompile` (angr) sessions
for §1; a real build + monitor-only screenshot test for §2-3. Game install itself untouched throughout
(read-only except the mod's own `d3d9.dll`/`openvr_api.dll`, both backed up before replacing).
