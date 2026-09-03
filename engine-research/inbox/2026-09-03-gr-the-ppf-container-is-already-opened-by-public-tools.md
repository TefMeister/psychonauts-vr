# The `PPAK` container job is already done in public — the PVS `[PD]` row is a tool run plus two fields

Filed by: `/gr`, 2026-09-03
Topic: `external-research/topics/2026-09-03b-the-steam-era-ppf-is-already-opened-by-two-public-tools-so-the-pvs-read-is-a-dump-plus-two-fields.md`
Bears on: the board's `[PD]` PVS-leaf-size row (re-costed 2026-09-03 as a container-format job) and the dossier's PVS / octree culling-gate section

- **DoubleFine Explorer** (bgbennyboy, **MPL-2.0**) states in its README that it supports the
  Steam/GOG re-release's changed `.ppf` level-pack format — the *original-version* Psychonauts
  Explorer explicitly does not. `[reported 2026-09-03]`
- **PsychonautsStudio** (RayCarrot, **MIT**, C#) carries a `FileType_PPF` reader and claims all
  versions; export is listed as upcoming, so it is a viewer with a serialization log for now.
  `[reported 2026-09-03]`
- Both are *tools* under the no-copy rule, so they may be used directly. Neither has been run by
  this lane.

Suggested board change: re-cost the row from "parse PPAK → locate level binary → walk to Octree →
two fields" to "**dump the level record with DoubleFine Explorer → read the two Octree fields**".
Caveat carried: whether the *level* record specifically comes out of DoubleFine Explorer is not
stated on its page — if not, PsychonautsStudio's viewer is the second route.

No dossier text is superseded; this lowers a cost, it does not correct a claim.
