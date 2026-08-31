# psychonauts-vr — `dev-archive/`

Full development archive for the Psychonauts VR mod — snapshots, probes, raw recon data, build scripts, transfer sessions, and the messy in-progress history behind the mod. Nothing here is curated; if it was created or used during development, it lives here.

For the readable field notes, see [`modding-notes/`](../modding-notes/). For actual working mod releases, see [`mod/`](../mod/).

This project adds VR support to the original 2005 **Psychonauts** (Double Fine), a DirectX 9 title on a custom in-house engine with no existing VR modding framework — everything here is built from scratch via reverse engineering.

## The folders for Psychonauts VR

Everything for this game lives in one repository, one folder per job — so you
always know where to look. You are in **`dev-archive/`**.

| Folder | What lives here |
| --- | --- |
| [`mod/`](../mod/) | The mod itself — the DirectX 9 stereo + head-tracking VR proxy (`d3d9.dll`). |
| **`dev-archive/`** ← you are here | Full development history — snapshots, probes, dead ends, raw recon. |
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
