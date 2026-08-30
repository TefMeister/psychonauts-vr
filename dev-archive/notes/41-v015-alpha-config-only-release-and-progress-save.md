# 41 — v0.1.5-alpha published (config-only release from the test PC); FOV 1.5 combo still unflown

**Date:** 2026-08-19, test PC.
**Context:** Progress save requested by the project owner, including pushing the mod changes made on the test PC to GitHub with a version bump. Per the new standing policy, every mod update published to GitHub gets its own version number — hence v0.1.5-alpha for what is a launcher/config-only change.

## What shipped in v0.1.5-alpha

Release: https://github.com/TefMeister/psychonauts-vr/releases/tag/v0.1.5-alpha (commits `a45d30f`, `d8cc847`, `2f6f7bd` on main).

- `proxy-d3d9/Launch-Psychonauts-VR.bat` — the generic launcher now lives in the repo instead of being a release-asset orphan; header points Quest 3 owners at the tuned variant.
- `proxy-d3d9/Launch-Psychonauts-VR-Quest3.bat` — **new**: Quest 3 pre-tuned launcher (`PSYVR_FOV_SCALE=1.5`, `PSYVR_RENDER_SCALE=3`), values derived in notes/40 from the real headset's projRaw geometry.
- `proxy-d3d9/USAGE.md` — bumped to v0.1.5-alpha; now documents the missing `suggested PSYVR_FOV_SCALE` log line (the notes/40 Issue 1 regression) instead of advertising it as present, with the manual ÷52° rule of thumb.
- Release zip `psychonauts-vr-0.1.5-alpha.zip` — **DLLs byte-identical to v0.1.4-alpha** (d3d9.dll SHA256 `753D22BC…635BB0`, stated in the release notes). No binaries were built on the test PC; the in-repo `proxy-d3d9/d3d9.dll` / `openvr_api.dll` blobs were deliberately left untouched.

## Test status — important for the work PC

**The FOV 1.5 + render scale 3 combination has still never been run.** The proxy log's last entry is the 2026-08-18 22:50:19 DLL_PROCESS_DETACH from the first head-tracked playtest (which ran FOV 1.00, scale 2x). The owner intended a second session but the log shows none happened. So treat the Quest 3 launcher values as *derived, not flight-tested*; first flight report will follow as its own note.

Final timing block from the 08-18 session, for the record (2x scale): readback ~1.29ms/1.12ms per eye at 72 submits/sec — plenty of headroom for 3x, as predicted in notes/37.

## Open dev-side queue (unchanged from notes/40)

1. Restore/implement the `suggested PSYVR_FOV_SCALE` log line (formula in notes/40 §Issue 2).
2. Frustum-culling pop-out when looking over the shoulder (notes/40 §Issue 3).
3. Center-eye LOD/billboard decisions to fix cross-eyed imposter trees (notes/40 §Issue 4).

## Process changes adopted (2026-08-18/19, owner's standing instructions)

- Version number bumps on **every** mod update published to GitHub, even config-only ones.
- Never any game files in the repos — only files we author (v0.1.5 complies: two `.bat` files, docs, and our own DLL builds).
- Owner approval obtained before this release was published (explicit request in-session).
- Local backups now maintained at `D:\Modding\Psychonauts\{dev-archive,modding-notes,mods}` on the test PC; refreshed with this save.

🤖 Written from the test PC via Claude Code (no git here; pushed with `gh api`).
