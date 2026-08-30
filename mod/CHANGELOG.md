# WIP changelog (private staging)

Newest first. These are untested/experimental builds. "Graduated" means promoted to a public
`psychonauts-vr` release.

## 2026-08-19 (later) — render-level head-locked first person (WIP, not playable yet)

- First person now locks the eye to **Raz's actual body**, recovered from the render stream
  (`World = WVP·P⁻¹·V⁻¹` on his skinned draws), instead of the springy chase camera. It tracks
  him, and the floor-clip-while-walking bug is fixed (lift along world +Y). See dev-archive
  notes/49.
- Anchor is currently Raz's body **centroid** — locked and tracking, but sits at his center (not
  his eyes), still jitters, and occasionally flashes 3rd-person on occlusion. **Next: anchor to
  his HEAD BONE** for a stable, correct, playable eye position (same work hand IK needs).
- Tuning: `PSYVR_FIRST_PERSON=1`, `PSYVR_FP_HEIGHT` (eye up/down), `PSYVR_FP_SMOOTH` (damping),
  `PSYVR_FP_FORWARD` (facing nudge), `PSYVR_FP_PROBE=1` (diagnostics). Live keys: F5/F6 smooth,
  F7/F8 forward, F9/F10 height, F11 recenter. Launcher: `Launch-Psychonauts-VR-FirstPerson.bat`.

## 2026-08-19 — first-person prototype (WIP, not graduated)

- **Experimental first-person mode** (`PSYVR_FIRST_PERSON=1`, default off): earlier version slid
  the eye onto Raz via the look-at point; reached *inside his head* but bounced with the chase
  camera. Superseded by the render-level head-lock above.
- Includes everything from the public **v0.1.7-alpha** base (tangent-matched submit bounds = the
  real zoom fix, restored suggested-FOV log). See the public repo's release notes.

## Baseline

Forked from public **v0.1.7-alpha** (graduated to `psychonauts-vr`, 2026-08-19).
