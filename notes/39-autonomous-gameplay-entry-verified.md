# Session 39 — Autonomous Gameplay Entry: VERIFIED END-TO-END

Date: 2026-08-18 (continuation of session 38's input breakthrough, same afternoon).

## 0. Summary

**A complete, zero-human-input path from cold title screen to live gameplay now exists and has
been executed successfully once**: synthetic SPACE at the title → Raz on the main-menu brain →
walked (UP×3, LEFT×1) to the three menu door-cards → BLUE middle card = CONTINUE (identified
live by the user watching) → entered via **tiny steps with a JUMP after every step** (the door
does not trigger from its edge; a jump while on/over the card enters it) → loading → real
gameplay in Whispering Rock, confirmed two independent ways:

- `BVM cache SET` camera coordinates jumped from menu-space (hundreds) to world-space
  `eye=(52499,-33015,-4389)` — the exact coordinate region notes/22 recorded during real
  user-driven gameplay.
- Eye-surface dump shows Raz mid-level with the gameplay HUD (psi-power icons, Q/E prompts).

Script: `tools/input/enter_gameplay.ps1` (route + door-entry micro-step-jump loop + the
"gameplay reached" detection criterion documented in its header). Save data on the dev machine
is user-declared expendable, so CONTINUE is always safe here.

## 1. What made the door work (for future navigation scripting)

- Walking timings drift; standing at a card's edge and pressing SPACE does nothing.
- The trigger is a JUMP while on/over the card — the reliable pattern is alternating 100ms
  micro-steps toward the card with a SPACE jump after each (8-element loop in the script).
- Menu geometry: after SPACE at title, three door-cards sit in a rough diagonal column
  (yellow top, BLUE middle = CONTINUE, green lower). UP×3 from spawn lands near the yellow;
  CONTINUE is one small step down-left of it.
- The user watched live and course-corrected once ("you have to get to the blue door") —
  future fully-blind runs should verify position via eye dumps at each phase rather than
  trusting timings.

## 2. What this means for the project

The complete test loop — launch, reach gameplay, exercise rendering/tracking under real
gameplay conditions, exit cleanly — is now scriptable with zero user interaction beyond the
standing hands-off-during-input-sends protocol. Combined with the eye-dump/vision loop
(read the actual rendered frame), future sessions can visually regression-test GAMEPLAY, not
just the title screen, entirely autonomously.

## 3. Repo state

- `tools/input/enter_gameplay.ps1` updated with the verified route; this note; status updated;
  synced to dev-archive + modding-notes. No release changes (tooling only).
