# psychonauts-vr — `mod/`

VR support mod for the original 2005 **Psychonauts** (Double Fine) — a DirectX 9, custom-engine game with no existing VR modding framework.

**Status: v0.1.7-alpha, early/experimental.** Confirmed working: (1) real gameplay renders correctly in stereo on both eyes, confirmed by direct play-testing, as a side-by-side monitor view; (2) a bridge that submits real frames to a VR compositor (SteamVR/OpenVR) — confirmed working with a real physical headset (Quest 3 via Virtual Desktop) at the HMD's native 72Hz; (3) **6DOF head tracking, now confirmed on real hardware** (Quest 3 playtests, 2026-08-18/19) — motion tracks correctly and comfortably. New in v0.1.7: **the zoomed-in picture is fixed at the root** — each eye is submitted with tangent-matched texture bounds, so the compositor's angular mapping is exactly 1:1 on any headset with no per-headset tuning; the earlier "HUD invisible at FOV scale above ~1.2" issue is gone with it, and the suggested-FOV log line is restored. Known remaining issues: an over-the-shoulder culling void (the engine's frustum culling doesn't know about head rotation yet) and doubled-looking distant LOD billboard sprites — fixes queued. See [proxy-d3d9/USAGE.md](proxy-d3d9/USAGE.md) for exactly what is and isn't confirmed. For development history and raw material, see [`dev-archive/`](../dev-archive/). For field notes and technical write-ups, see [`modding-notes/`](../modding-notes/).

## The folders for Psychonauts VR

Everything for this game lives in one repository, one folder per job — so you
always know where to look. You are in **`mod/`**.

| Folder | What lives here |
| --- | --- |
| **`mod/`** ← you are here | The mod itself — the DirectX 9 stereo + head-tracking VR proxy (`d3d9.dll`). |
| [`dev-archive/`](../dev-archive/) | Full development history — snapshots, probes, dead ends, raw recon. |
| [`modding-notes/`](../modding-notes/) | Readable field notes / progress ledger. |
| [staging/psychonauts-vr](https://github.com/TefMeister/staging/tree/main/psychonauts-vr) 🔒 | **Private** — unverified WIP builds, cross-machine handoff. |
| [`engine-research/`](../engine-research/) | Distilled engine reference (dossier) + reusable VR RE playbook. |
| [`external-research/`](../external-research/) | Ongoing public-research leads, gathered separately from hands-on modding work. |

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.

## Contributing & policy

See [CONTRIBUTING.md](CONTRIBUTING.md) — how we credit and link sources, our
**study-everything-public but write-our-own-code** rule (we copy no one else's
source code or files, any license or price), the terms for reusing our work
(free, with credit), and how to request a correction or removal.
