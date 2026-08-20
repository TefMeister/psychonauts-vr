# Psychonauts (2005) — VR Engine Research

Reverse-engineering research behind a VR conversion of the original
**Psychonauts (2005, Double Fine Productions)**, running on a bespoke
in-house engine with no prior VR conversion to build on.

This repository holds two things:

- **[`PLAYBOOK.md`](PLAYBOOK.md)** — a reusable, engine-agnostic, point-by-point
  method for taking *any* game whose engine nobody has converted to VR and
  getting it there. It is oriented around one North Star: **the game rendering
  in a headset with head tracking**, with everything else built on top. The
  same playbook is copied into each of our VR projects' research repos.
- **[`ENGINE-DOSSIER.md`](ENGINE-DOSSIER.md)** — the distilled, current-truth
  reference for *this* game's engine: renderer, frame structure, how the
  camera transform reaches the GPU, the constant-register mechanism, the pass
  inventory, the Lua-binding cheat sheet, and the dead ends that cost us time
  so they don't cost the next engine's.

The blow-by-blow development history lives in the sibling repositories
(`psychonauts-vr-dev-archive` for the messy in-progress record,
`psychonauts-vr-modding-notes` for readable field notes). This repo is the
consolidated engine knowledge, not the diary.

## Status

**The North Star is already reached**: stereo rendering and 6DOF head tracking
are confirmed working in a real Quest 3 headset. Current work is Phase 7+
(first-person view polish) — anchoring the camera to the player character,
Raz, comfortably and stably. See the dossier's status line, §6, and §11/§12
for exactly where that stands and what's still open.

## Scope, ethics, and legality

- This is a **non-commercial fan project**. It requires owning a legitimate
  copy of the game and **redistributes no original game assets** — only files
  we create. See [`.gitignore`](.gitignore).
- The techniques here (DLL proxying, hooking, injection, shader analysis)
  resemble malware only in tooling; the context is personal modding of a game
  we own.
- We **credit everyone** whose work or research this builds on, and we honour
  correction/removal requests from actual rights holders. See
  [`CREDITS.md`](CREDITS.md).
