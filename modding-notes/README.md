# psychonauts-vr — `modding-notes/`

Trial-and-error field notes from AI-assisted reverse engineering of the original **Psychonauts** (2005, Double Fine) — a project to add VR support to a DirectX 9, custom-engine game that has no existing modding framework.

This folder holds the readable, curated write-ups: what was tried, what was learned, confirmed technical facts (hook addresses, offsets, engine behavior), and the reasoning behind each decision. For the raw, messy, everything-including-failed-attempts material, see the companion [`dev-archive/`](../dev-archive/) repo. For actual working mod releases, see [`mod/`](../mod/).

## The folders for Psychonauts VR

Everything for this game lives in one repository, one folder per job — so you
always know where to look. You are in **`modding-notes/`**.

| Folder | What lives here |
| --- | --- |
| [`mod/`](../mod/) | The mod itself — the DirectX 9 stereo + head-tracking VR proxy (`d3d9.dll`). |
| [`dev-archive/`](../dev-archive/) | Full development history — snapshots, probes, dead ends, raw recon. |
| **`modding-notes/`** ← you are here | Readable field notes / progress ledger. |
| [staging/psychonauts-vr](https://github.com/TefMeister/staging/tree/main/psychonauts-vr) 🔒 | **Private** — unverified WIP builds, cross-machine handoff. |
| [`engine-research/`](../engine-research/) | Distilled engine reference (dossier) + reusable VR RE playbook. |
| [`external-research/`](../external-research/) | Ongoing public-research leads, gathered separately from hands-on modding work. |

## Status

See `00-status.md` for the current state of the project. As of the latest update (2026-08-18, v0.1.4-alpha): real gameplay renders correctly in stereo on both eyes (confirmed by direct play-testing); frames are submitted to SteamVR/OpenVR and have been confirmed working inside a real headset (Quest 3 via Virtual Desktop) at its native 72Hz; 6DOF head tracking, 2× eye render resolution, UI depth placement, and a tunable FOV scale are all implemented; and the full test loop (launch → menu → real gameplay) can now run autonomously via synthetic input — see `39-autonomous-gameplay-entry-verified.md` for the latest session.

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.

## Contributing & policy

See [CONTRIBUTING.md](CONTRIBUTING.md) — how we credit and link sources, our
**study-everything-public but write-our-own-code** rule (we copy no one else's
source code or files, any license or price), the terms for reusing our work
(free, with credit), and how to request a correction or removal.
