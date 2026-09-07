# 2026-09-07 — The `PPAK` packs open statically, and the PVS row has no level binary to find

*Session: `/pd psychonauts`, dev PC, **static only, NO LAUNCH**. The game was not launched and
nothing here has been run. Lane claim taken and released.*

The project's single `[PD]` row — bound the PVS leaf size — was costed as *"parse PPAK → locate the
level binary → walk to Octree → read two fields"*, and a `/gr` drop re-costed it to *"dump the level
record with a public tool"*. **Both assume a level record exists as a discrete file inside the pack.
It does not.** The row is not blocked on container parsing at all; it is blocked one layer deeper.

---

## 1. Enumerating a pack needs no third-party tool

`WorkResource/PCLevelPackFiles/` holds **50 `.ppf` + 50 `.apf`** `[measured 2026-09-07]`. Names are
stored as `u16 length` + NUL-terminated ASCII, so a byte-resyncing scan enumerates a pack in about
ten lines — no dependency, no download.

| | |
| --- | --- |
| `.ppf` magic | `PPAK`; per-asset FourCC stored little-endian as `CYSP` (= `'PSYC'`), ×91 in `ASCO.ppf`, `MPAK` ×1 |
| `.apf` magic | `KAPA` (= `'APAK'`) — an **animation** pack; `ASCO.apf` is 155 records, **all `.jan`** |
| `ASCO.ppf` (16.7 MB) | **556 name records — 279 `.dds`, 89 `.plb`, 30 `.lua`**, plus entity-type-suffixed names |

Tool: `dev-archive/tools/ppak_scan.py` (`<pack>`, `--names`, `--census`). Read-only; it prints names
and offsets, which are interface metadata, and never writes to the game folder.

---

## 2. ⚠️ THE CORRECTION: no `.plb` is a level scene

`[measured 2026-09-07, n=5 packs, 579 records]`

Tested by the naming convention itself — a level scene would be `levels\<level>\<level>.plb`:

| pack | `.plb` records | level scenes |
| --- | --- | --- |
| `ASCO` | 89 | 0 |
| `MMI1` | 170 | 0 |
| `NIMP` | 100 | 0 |
| `WWMA` | 213 | 0 |
| `common` | 7 | 0 |

**Zero matches in any pack.** What the `.plb` records actually are: props, vehicles, overlays, held
objects, characters and globalmodels. The `MPAK` magic agrees — it is a model pack.

The `.apf` sibling is animations only, so the level is not there either. And the remaining names in
the `.ppf` end in **entity type** suffixes rather than file extensions — `.domaincontroller`,
`.splineobject`, `.tightrope`, `.brainjar`, `.teleporter`, `.animator`, `.figment`, `.ladder`,
`.emotionalbaggage`, `.psichallengecard`.

⇒ **The level is a serialized entity graph inside the `.ppf`, not a file you can carve out.** So
"locate the level binary" is not a step that exists, and the Octree's root `SSECube` extent and
`LeavesCount` have to be recovered from that serialization.

### ⚠️ A false positive I generated and then killed

My first classifier just excluded anything under `props/`, `globalmodels/` or `characters/`. It
reported "NONE" for `ASCO` — the right answer — and then, run across other packs, flagged
`levels\mm_milkmanconspiracy\vehicles\blacksedan.plb` and
`levels\ww_waterlooworld\overlay\ww_overlaydefault.plb` as level scenes. **They are models that
simply do not live under `props/`.** The heuristic was agreeing with me for the wrong reason on a
sample of one. Replaced with the naming test above, the tool re-run, and the false-positive case
recorded in the tool itself so it is not reintroduced.

This is the whole reason `n=1` is not verified: the first pack gave the right answer *and* would
have shipped a broken classifier.

---

## 3. What this does to the `[PD]` row

**It does not close it, and it is no longer honestly a `[PD]` row.** The cheap half — enumerate the
container — is done and needs nothing. The remaining half is understanding the entity-graph
serialization well enough to find an Octree node inside it, which is a substantial format job, and
is exactly what the two public tools already implement:

- **DoubleFine Explorer** (bgbennyboy, **MPL-2.0**) — README claims support for the Steam/GOG
  re-release's changed `.ppf`; the original-version Psychonauts Explorer explicitly does not.
  `[reported 2026-09-03]`
- **PsychonautsStudio** (RayCarrot, **MIT**, C#) — `FileType_PPF` reader, claims all versions;
  export listed as upcoming, so a viewer with a serialization log for now. `[reported 2026-09-03]`

Both are *tools* under the no-copy rule and may be used directly. **Neither has been run by this
lane**, and running one needs a download — which is the user's call, not `/pd`'s to do unasked. So
the row is re-tagged `[USER]`: one decision, then the read is minutes.

**What is NOT established:** that either tool actually exposes the Octree fields. DoubleFine
Explorer's page does not say whether the *level* record specifically comes out; PsychonautsStudio is
a viewer. If neither exposes it, the fallback is reading the serialization from the exe's own
loader, which is `[PD]` work but a much bigger one than the row ever described.

---

## 4. Why any of this matters to the North Star

The row exists to answer one thing: **is first person safe from the PVS gate?** Dossier §9b
establishes that the PVS keys on *position*, not orientation, and that the void's residual is a
culling-gate signature rather than a matrix bug. The leaf scale decides it — a few hundred leaves
over a level means room-scale leaves and the ~11.6-unit eye-to-Raz offset is irrelevant; many
thousands means the offset can cross a leaf boundary and popping on FP engage is expected.

Nothing here changes that reasoning. It changes only what it costs to get the number.

---

## 5. Also folded this session

The 2026-09-07 `/gr` drop on the **`GetDeviceData` continuation** is now in dossier §9c: a sibling
project has a working DirectInput injector, host-tested (36 checks, 0 failures), at
`staging/prince-of-persia-2008-vr/proxy-dinput8/` with harness `tools/pop_input.py`.

⚠️ **Not a drop-in — it hooks `GetDeviceState`; we need `GetDeviceData`,** and that difference *is*
our dead end. What transfers is the apply logic as a host-testable pure function, and its four
behaviours are recorded as requirements for our version. The one that matters most here: **one-shot
relative mouse motion**, because we measured **3598 `GetDeviceData` calls while the mouse moved**, and
`GetDeviceData` is buffered — a queue of events, not a state snapshot — so it needs its own
equivalent of that rule.

The filer also disproved the attractive wrong idea before filing it: their shared-vtable fix does
**not** explain our 2026-08-27 negative, because our record already establishes the mouse arrives via
buffered `GetDeviceData` with `WM_MOUSEMOVE` and `WM_INPUT` both at zero `[verified-live]`.
