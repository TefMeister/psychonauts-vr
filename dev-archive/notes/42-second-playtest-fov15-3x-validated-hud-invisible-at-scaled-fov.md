# 42 — Second head-tracked playtest (FOV 1.5 + 3x first flight): zoom improved, 72Hz holds, NEW: HUD invisible at scaled FOV

**Date:** 2026-08-19, 00:09–00:15 session on the test PC (Quest 3, RTX 5080), v0.1.4-alpha DLLs with the v0.1.5-alpha Quest 3 launcher values (`PSYVR_FOV_SCALE=1.5`, `PSYVR_RENDER_SCALE=3`).

## Results

### Confirmed improvements
- **Zoom: better.** Player-confirmed the magnification is clearly reduced at FOV 1.5 ("it was better zoom wise"). The notes/40 derivation holds up in practice.
- **3x render scale validated on real hardware.** After load-in, the session sustained ~69–72 frames/sec at 2400×1800 per eye. Readbacks grew as expected but stayed in budget:
  - `ReadbackChain[0] avg≈5.1ms`, `ReadbackChain[1] avg≈4.2ms` (vs ~1.3/1.1ms at 2x) — both eyes ≈9ms against the 13.9ms 72Hz budget.
  - Early startup windows showed n=30/sec with near-zero WaitGetPoses; that was load-in/menu, not steady state (late-session windows: n=69, WaitGetPoses avg 1.27ms).
  - Log confirms `2400x1800, scale=3x` eye buffers and `BPM rawFov=156.000 fovy=1.3614` (=52°·1.5=78° vertical).
- **Clean exit again** (DETACH 00:15:28, no hang) — two-for-two since the notes/34 fix.

### NEW ISSUE — HUD invisible when FOV scale > 1 (screen-space UI inherits the widened frustum)
Player report: HUD not visible at all during play at FOV 1.5.

Mechanism: the game draws HUD/UI in screen space spanning the full frame. With the projection widened 1.5×, the frame now covers ≈94°×78°, so UI anchored at frame corners/edges lands at ≈±47° horizontal from view center. The Quest 3 per-eye frustum crops at 40° nasal (projRaw r=0.8391) and ~50° temporal — so each HUD corner survives in at most ONE eye, at the far edge of lens clarity. Net effect: HUD unreadable/invisible. (The v0.1.3 UI-depth shift itself worked in the 08-18 session at FOV 1.0, where the HUD sat at its native ±33°/±26°.)

**Proposed fix (cheap, uses existing machinery):** the 10 UI-signature vertex shaders are already intercepted for the per-eye depth shift. In that same constant patch, also scale UI position x/y by `1/PSYVR_FOV_SCALE`, so screen-space UI keeps its original angular footprint regardless of the 3D FOV widening. Optional knob `PSYVR_UI_SCALE` to override.

Test-side stopgap until then: run `PSYVR_FOV_SCALE≈1.2` when HUD visibility matters more than zoom.

### Culling void: confirmed still present at FOV 1.5 (expected)
Looking over the shoulder still hits pure blackness from some angle on — FOV 1.5 widens the cull frustum but cannot cover behind the game camera. Notably the player describes the effect as genuinely unsettling ("empty blackness... really scary... staring into nothingness"), which upgrades this from cosmetic to a comfort issue: an unrendered void in a headset has real psychological weight that the same bug on a monitor does not. Worth prioritizing the head-yaw-feedback fix (notes/40 §Issue 3) accordingly.

## Updated dev-side queue (priority order)
1. **UI x/y rescale for FOV scale** (this note) — HUD is currently unusable at the recommended Quest 3 FOV setting.
2. **Head-yaw feedback into game camera / cull-frustum widening** (notes/40 §3) — now a comfort issue, see above.
3. Center-eye LOD/billboard decisions (notes/40 §4).
4. Restore the `suggested PSYVR_FOV_SCALE` log line (notes/40 §1).

🤖 Written from the test PC via Claude Code (no git here; pushed with `gh api`).
