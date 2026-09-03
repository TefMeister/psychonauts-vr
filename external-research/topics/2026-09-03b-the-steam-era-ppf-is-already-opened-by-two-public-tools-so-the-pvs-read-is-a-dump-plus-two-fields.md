# The Steam-era `.ppf` is already opened by two public tools — so the PVS read is a dump plus two fields, not a container-format job

**Date:** 2026-09-03 · **Status:** 🆕 new · **Priority:** medium · **Corrects the cost of:** the
board's `[PD]` PVS-leaf-size row, which the modding side re-estimated on 2026-09-03 as *"parse the
PPAK container → locate the level binary inside → walk to the Octree → read two fields"* after finding
zero loose `.plb` files and 50 `PPAK` (`.ppf`) containers in the install.

## The finding

The container half of that job is **already done in public, twice**, by tools whose licences let us
use them *as tools* (the standing rule: modding toolsets may be used directly; only non-tool code is
study-and-reimplement):

| Tool | Author | Licence | Steam/GOG `.ppf`? | What it does | Tag |
| --- | --- | --- | --- | --- | --- |
| **DoubleFine Explorer** (v1.4.1) | bgbennyboy (Quick and Easy Software) | **MPL-2.0**, Pascal | **Yes — explicitly.** README: the re-release "changed the format of the level pack files (.ppf). DoubleFine Explorer only supports this newer version" | "view, extract and convert resources" from Double Fine games; Psychonauts 1 re-release listed as supported | `[reported]` |
| **PsychonautsStudio** | RayCarrot | **MIT**, C# | "supports all versions" incl. PS2 and Xbox prototypes; source has `src/FileType/Binary/FileType_PPF.cs` | Currently "open and view" + serialization log; **export/batch listed as *upcoming***, so treat as a viewer for now | `[reported]` |
| Psychonauts Explorer | bgbennyboy | none stated, Pascal | **No** — "designed for the original version… the re-released versions on GOG and Steam changed the file formats so some files (like the level .ppf files) won't work" | original-2005 archives only | `[reported]` — listed so nobody tries it |
| psychoportal (`Packs/PackPack/PackPack.cs`) | Jill Nesbit | GPL-3.0 + exception | Yes (the library already known to this project) | a *library*, study-only under our rules | `[reported]` |

**Cross-check that matters:** the modding side's own read of the `.ppf` files — `PPAK` magic, asset
paths interleaved with compressed payloads, no `.plb` name visible in the first 200 KB — is consistent
with a *pack* whose contents are typed records rather than a directory of named files, which is
exactly why a bespoke "find the `.plb` inside" scan found nothing and a format-aware tool is the right
instrument.

## What it changes

The `[PD]` row shrinks from "write a PPAK parser" to:

1. Run DoubleFine Explorer against one `WorkResource/PCLevelPackFiles/*.ppf` and dump the level
   binary (the `.plb`-equivalent record) — a tool run, no code.
2. Read the two Octree fields the 2026-09-03 PVS topic named (total leaf count and root bounds), with
   our own two-field reader or by eye in a hex view.

⚠️ **Two honest caveats.** (1) That DoubleFine Explorer dumps the *level* record specifically — as
opposed to textures, scripts and audio — is not stated on its page; the README claims re-release
`.ppf` support in general. If the level record does not come out, PsychonautsStudio's `FileType_PPF`
viewer plus its serialization log is the second route. (2) Neither tool has been run by this lane
(research-only); this is a lead, tagged as such.

## Also recorded so no sweep re-finds it

The modding side reports `ECamera::BoxVisible` fully disassembled and **the engine exposes its own
cull-camera override** — a six-instruction `Set(ECamera*)` at `0x004D0DA0` with a getter, plus two
one-bit culling disables. **No public research is wanted on "can Psychonauts cull from a second
camera"** — it is our own binary and already answered (`modding-notes/75-…`).

## Sources

- https://github.com/bgbennyboy/DoubleFine-Explorer — README "Limitations" section (re-release `.ppf`
  support statement), licence MPL-2.0 (via the GitHub API), last push 2023-11-05
- https://quickandeasysoftware.net/software/doublefine-explorer — v1.4.1, "view, extract and convert
  resources", Psychonauts 1 listed, source on GitHub
- https://github.com/RayCarrot/PsychonautsStudio — README (features vs upcoming), MIT, C#, last push
  2024-04-30; `src/FileType/Binary/FileType_PPF.cs` located via GitHub code search
- https://github.com/bgbennyboy/Psychonauts-Explorer — README: original-version-only statement
- https://quickandeasysoftware.net/readmes/PsychonautsExplorerHelp/psychonautsfiles.htm — `.ppf` =
  "level pack files"
- https://gitlab.com/scrunguscrungus/psychoportal — `PsychoPortal/Psychonauts/Packs/PackPack/`
  (tree listing only, via the GitLab API; nothing read or copied)
