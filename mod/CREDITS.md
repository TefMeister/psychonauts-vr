# Credits & Attribution

This project is a reverse-engineering and modding effort built on the public
research, tools, and documentation of many people who came before us. None of
this would be possible without their work. We list every source we've drawn
on below — including work that helped only as inspiration — by name or
handle, as accurately as we could verify it.

## The game itself

This mod modifies, at runtime, the original **Psychonauts** (2005) by
**Double Fine Productions** (https://www.doublefine.com). The game, its
engine, and all of its assets are theirs, and the game is the entire reason
this project exists. **No game files, code, or assets are distributed in any
of this project's repositories** — only code, notes, and tools we wrote
ourselves, plus third-party components whose licenses permit redistribution
(noted below).

## Prior art, tools, and libraries

| Source / Work | Creator(s) | Link |
|---|---|---|
| Psychonauts D3D9 stereo-depth fix (Helix Mod) | bo3b (post author); earlier fix by eqzitara | https://helixmod.blogspot.com/2013/05/psychonauts.html |
| PsychonautsStudio (file tools) | RayCarrot | https://github.com/RayCarrot/PsychonautsStudio |
| Psychonauts Reverse Engineering Blog | Jill (JillCrungus) | https://jillcrungus.com/projects/psychonauts/blog/ |
| vorpX (VR injection driver) | Ralf Ostertag / Animation Labs | https://www.vorpx.com |
| Astralathe (mod loader / debugger) | Jill (JillCrungus) — *GameBanana submitter handle not independently re-confirmed by us; see link* | https://gamebanana.com/tools/12094 |
| Cobweb Duster (companion setup tool) | Jill (JillCrungus) — *handle unconfirmed on GameBanana page itself, corroborated via jillcrungus.com* | https://gamebanana.com/tools/12094 |
| OpenVR SDK / SteamVR (headers, import lib, and `openvr_api.dll`, vendored and redistributed under its BSD-3-Clause license) | Valve Corporation | https://github.com/ValveSoftware/openvr |
| dxwrapper | elishacloud | https://github.com/elishacloud/dxwrapper |
| x64dbg (debugger) | mrexodia, Sigma, tr4ceflow, Dreg, Nukem, Herz3h, torusrxxx, and the x64dbg contributor community | https://github.com/x64dbg/x64dbg |
| x64dbg-automate (debugger automation our live RE sessions ran on) | dariushoule | https://github.com/dariushoule/x64dbg-automate |
| x64dbg-skills (guide) | dariushoule | https://github.com/dariushoule/x64dbg-skills |
| Superpowers | Jesse Vincent (GitHub: obra) and contributors at Prime Radiant | https://github.com/obra/superpowers |
| LLVM-MinGW toolchain (used to build the mod) | Martin Storsjö and the LLVM / MinGW-w64 communities | https://github.com/mstorsjo/llvm-mingw |
| Virtual Desktop (used for all physical-headset testing) | Guy Godin / Virtual Desktop, Inc. | https://www.vrdesktop.net |
| SoundVolumeView (development utility, used locally during testing; not redistributed in these repos) | Nir Sofer (NirSoft) | https://www.nirsoft.net/utils/sound_volume_view.html |
| guides | Brobert-in-aus | https://github.com/Brobert-in-aus/guides |

Development on this project is AI-assisted: much of the reverse engineering,
code, and documentation was produced with **Claude (Anthropic)**
(https://claude.com) working alongside the project owner.

Where a handle above is marked "unconfirmed," it means we could not
independently verify that exact string on the original source page at the
time of writing, even though we're confident in the underlying identity from
corroborating sources. If you are one of the people above and can correct or
confirm a detail, please open an issue — we'd rather fix it than leave it
wrong.

## Missing from this list?

If you — or someone whose work you know — contributed to, influenced, or
even just inspired anything used in this project and you aren't credited
here, please **open a GitHub issue on this repo** and we'll correct it as
soon as possible. We would much rather over-credit than leave anyone out.

## Respecting creators

This project exists because other people generously shared their
reverse-engineering research, tools, and modding know-how in public — we've
tried to credit every one of them by name or handle above, as accurately as
we could verify. If you are the creator or rightful owner of anything
credited or used here and you'd rather your work not be referenced in this
repo, or you want specific content removed or no longer used by the mod,
please tell us: **open a GitHub issue on this repo**. We'll act on that
request promptly — no argument, no delay — and we'll find another way to get
the job done that doesn't rely on your material. This is your work; we're
just grateful to have learned from it.
