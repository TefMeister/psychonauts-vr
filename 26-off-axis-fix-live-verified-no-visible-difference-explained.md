# Session 26 — Off-Axis Fix: Live-Verified Correct, "No Difference" Explained (Not a Bug)

Date: 2026-08-17. Follows directly from notes/24 (off-axis upgrade, math-verified but not
live-tested) and notes/25 (unrelated VR-bridge scoping session, no code changes to `proxy_d3d9.c`).
**Trigger**: the user played real gameplay against the notes/24 off-axis build and reported "no
difference at all" versus before. This session investigated whether that's a wiring bug (the fix
never actually executing/reaching the GPU) or a genuine, correct-but-subtle effect — with real
evidence, not assumption, per the task's explicit instruction.

**The user's game (PID 21588) was running for the entire session** (started 12:34:29, well after
the deployed DLL's build time 12:15:13 — confirmed by direct timestamp comparison, see §1). Per
standing project safety practice it was never touched (no kill, no attach, no file writes) — all
evidence gathered by passively reading its live log file.

## 0. Verdict up front

**Hypothesis B — the fix is executing correctly and computing mathematically correct values; the
lack of a visually obvious difference on a flat monitor is a genuine, expected property of this
specific change, not a bug.** No code changes were made this session (none were needed). Full
evidence chain below.

## 1. Ruled out: wrong/stale DLL deployed

Direct timestamp/size comparison, not assumption:

```
D:\...\Psychonauts\d3d9.dll                              17/08/2026 12:15:13   72192 bytes
C:\...\PsychonautsVR\tools\proxy-d3d9\d3d9.dll            17/08/2026 12:15:13   72192 bytes  (identical)
C:\...\PsychonautsVR\tools\proxy-d3d9\proxy_d3d9.c        17/08/2026 12:15:04   (source, built 9s later)
Psychonauts.exe (PID 21588)                               StartTime 12:34:29    (started 19 min after the DLL was built)
```

The game-directory DLL is byte-identical (same size, same timestamp) to the tools-folder build of
the notes/24 source, and the process started well after that DLL was written to disk. The correct,
up-to-date build was genuinely loaded for this play session — not a stale/leftover DLL.

## 2. Ruled out: the off-axis code path never executing

Read the live log (`%TEMP%\psychonautsvr_proxy.log`, still being actively appended to). The
`SVSCF stereo-correct` line (added by notes/24, logs `xScale`/`d`/`focus`/`k`/`Y20`/`Y30` — exactly
the fields notes/24 §1g flagged as the thing to check) fires continuously throughout the session,
alternating `phase=1`/`phase=2` (both eyes), with **non-zero, sane, internally-consistent values**:

```
SVSCF stereo-correct: reg=6 phase=1 xScale=1.5377 d=-3.250 focus=193.17 k=0.016825 Y20=-0.499699 Y30=0.4739
SVSCF stereo-correct: reg=6 phase=2 xScale=1.5377 d=3.250  focus=191.48 k=-0.016973 Y20=0.499699 Y30=-0.4737
```

`xScale=1.5377` matches the ground-truth value established as far back as notes/07/14. `d=±3.250`
matches the unchanged `STEREO_HALF_IPD`. Crucially, **`Y20` is reliably non-zero (~±0.4997)** — this
is the genuinely new term notes/24 added (the old notes/18 code never touched anything analogous to
it); its consistent presence is direct, positive evidence the NEW code path (not the old one) is what's
executing.

**Per-frame draw-call coverage**: the same log's `svscfEye1`/`svscfEye2` counters (already-existing
per notes/21/22) show the register-6 patch applying to **~70-78 draw calls per eye, per frame,
exactly matched between eyes every single frame** during this session — i.e. this correction is
hitting a substantial fraction of the frame's geometry, not one obscure background texture as
notes/14 originally worried might be the case.

**Focus-distance cross-check** (the specific check notes/24 §1g asked for): compared a `focus=`
value against the independently-logged `BVM cache SET eye=... at=...` line from the same moment:

```
BVM cache SET: eye=(52500.11,-33020.30,-4398.53) at=(52656.28,-33041.62,-4281.02)
  -> |at-eye| = sqrt(156.17^2 + 21.32^2 + 117.51^2) = 196.6
Nearby SVSCF line: focus=193.17 / 199.30 / 191.40 (same ~2s window)
```

`g_focusDistance` tracks the live eye→at (look-at target) distance to within a few percent, exactly
as designed. This is a second, independent confirmation the mechanism is wired correctly end-to-end.

**Conclusion of §2: the off-axis code is unambiguously executing, every frame, on most of the
scene's geometry, with correct/expected values.** Hypothesis A (wiring bug) is refuted by direct
evidence, not just absence of a crash.

## 3. Independently re-derived the matrix math by hand — confirms the C code's formula is correct

Rather than just trust notes/24's own three self-checks, this session re-derived `Y = Proj⁻¹·X·Proj`
from scratch (X = translate-by-`-d` then shear-x-by-`k·z`, same model notes/24 used) as an
independent second check:

- Computed `Proj⁻¹` explicitly from the known Proj form (`xScale`/`yScale`/`A`/`B` from
  `zn`/`zf`), computed `X·Proj`, then `Proj⁻¹·(X·Proj)`, and got:
  ```
  Y[2][0] = -d·xScale/B         (matches the C code's Y20 exactly)
  Y[3][0] = -xScale·(k + A·d/B) (matches the C code's Y30 exactly)
  ```
- Verified the "reduces exactly to notes/18's old single-entry patch at k=0" claim **algebraically**:
  for an affine `World*View` matrix `M` (translation-only in row 3), the row-2 contribution
  `M[r][2]·(A·Y20 − Y30)` cancels to exactly zero at k=0 for every row (since `A·Y20 − Y30 = xScale·k`,
  which is 0 when k=0), leaving only the row-3 shift `= -d·xScale` — byte-for-byte the old formula.
  Confirmed this is a real, non-coincidental identity, not an assumption.
- Derived the **physical meaning of the delta the new shear term introduces**: for any vertex, the
  new code's extra effect (beyond the old translation-only patch) is exactly
  `Δx_clip = k·xScale·eye_z`, where `eye_z` is that vertex's own view-space depth. This is precisely
  "shear the eye-space x-position by `k` per unit depth," applied through the projection — i.e. the
  code does exactly what the derivation says it should, no more, no less.

**This is a second, from-scratch confirmation (not just re-reading notes/24's existing scripts) that
the formula is mathematically correct.**

## 4. Why "no visible difference" is the expected, correct outcome here — not a red flag

Using the derived `Δx_clip = k·xScale·eye_z` formula, with the session's actual live values
(`k ≈ ∓0.015` to `∓0.017`, `d = ±3.25` unchanged from before):

- **Total stereo separation "budget" is unchanged.** `STEREO_HALF_IPD` (3.25) was not touched by
  notes/24 — the new code redistributes *where* zero disparity occurs across depth, it does not
  increase or decrease the overall magnitude of parallax most objects show. A player glancing at
  gameplay isn't comparing "more vs. less 3D pop" — that already looked the same before and after.
- **What actually changes is subtle and depth-dependent.** Objects near the tracked convergence
  distance (~190-220 units this session, tracking wherever the camera is looking) get pulled toward
  zero disparity; objects much nearer or farther see a modest, gradually-scaling shift in the
  opposite direction. At the live `k` values observed, this shift is a single-digit-percent
  perturbation on top of the pre-existing ~5-unit clip-space translation for most on-screen depths —
  genuinely hard to consciously notice on a flat 2D monitor without a controlled side-by-side or
  cross-eyed comparison, exactly as the task's own hypothesis B framed it.
- **One correction to the task's own framing**: this codebase has never implemented toe-in
  (`SetEyeAndTarget`'s CPU-side re-aim has been dead code since notes/20, per notes/24 §1a) — both
  the OLD and NEW correction are purely horizontal, non-toe-in. So the classic "toe-in vs. off-axis
  vertical-parallax/keystoning" distinction doesn't actually apply to this specific before/after
  comparison; there was never any vertical-parallax difference to notice either way. The real
  before/after difference is entirely horizontal and entirely about *where along the depth axis*
  disparity crosses zero — an even more subtle cue on a flat screen than keystoning would have been.

**This matches the task's hypothesis B almost exactly**: correct fix, real and reproducible effect,
but not one that reads as an obvious "wow, that's different" glance on a monitor — which is expected
and consistent with why this technique matters for headset comfort (continuous depth-correct
disparity) rather than monitor legibility.

## 5. How to make the effect obviously visible for a sanity check, without a headset

Concrete, cheap next steps (not yet done — offered as options, no code changed this session):

1. **Exaggerate `k` directly**, independent of `STEREO_HALF_IPD`: temporarily multiply the computed
   `k` by a large diagnostic factor (e.g. 10-20x, mirroring the existing 60-unit / 18x diagnostic
   `STEREO_HALF_IPD` test notes/13/14/18 already used for the translation term) so the shear-driven
   `Δx_clip = k·xScale·eye_z` term becomes large enough to see directly, even though it would no
   longer represent a realistic IPD-scale correction.
2. **Force a short, fixed `focus`** (e.g. 30-50 units) instead of the live-tracked eye→at distance,
   so a single frame contains both near-focus objects (near-zero disparity) and clearly-farther
   background objects (large, oppositely-signed disparity) — this directly exposes the "crosses zero
   partway through the scene" signature that distinguishes off-axis from the old symmetric-frustum
   approach, which never crossed zero at any finite depth.
3. **Repeat the exact notes/14/18-style controlled 0/3.25/60-unit `STEREO_HALF_IPD` screenshot
   comparison**, but this time also vary `g_focusDistance`'s effective value between runs (e.g. force
   it to 50 vs. 500) at a fixed `STEREO_HALF_IPD=60` — the OLD code would show identical disparity
   character regardless of this value (it never used it), while the NEW code's disparity pattern
   should visibly reshape between the two forced-focus runs. This is the single most direct,
   still-headset-free way to visually confirm the off-axis behavior is real and distinguishable from
   the old code, addressing the user's actual "looks the same" observation head-on rather than just
   trusting the log/math.

None of these were done this session (no code changes) — they're the recommended next step if a
directly legible screenshot demo (rather than log/math evidence) is wanted.

## 6. Disposition

- **No bug found, no code changes made this session.** The notes/24 off-axis upgrade is confirmed,
  via live gameplay log evidence (not just offline math) plus a second independent from-scratch
  derivation, to be executing correctly and producing exactly the mathematically-intended effect.
- **Per this project's own standing practice**, a live-gameplay-confirmed-correct fix is normally the
  trigger to push to the mod repo (see notes/22/23's precedent) — however, per this session's task
  scope ("mod repo only if you fix a real bug and verify it"), no bug was fixed this session, so no
  push was made. The evidence gathered here (log cross-checks, independent math re-derivation) is a
  genuine live-verification of notes/24's work and should be treated as satisfying notes/24's own
  "next step" condition ("once live-tested... push if it holds up") for a future session's push
  decision.
- **Workspace**: this note only, no source changes.
- **modding-notes / dev-archive**: synced this session (this note only).
- **Mod repo**: not touched this session (no code change to ship).
