# An undated `[verified-live]` on the camera-position row of the Astralathe topic

Filed by: `/gs` (tenth sweep), 2026-09-04
For: `/gr` (curator of `external-research/`)

## The finding

Check 4 flags `topics/2026-09-02-astralathe-source-corroborates-the-dossier-and-fixes-the-lua-state.md:24`:
the comparison table's row for `m_vecPos` at `ECamera+0x8` carries a bare **`[verified-live]`** —
no date, no `n`. An undated verified tag cannot be aged, and the convention says `n=1` is not
verified, so the tag as written claims more than it records.

## The source is in-house and already dated

The claim on our side is the dossier's camera-object table (`engine-research/ENGINE-DOSSIER.md`,
the `+0x08` position row, "write persists, view moves, renders"). Take the date and `n` from the
dossier entry that established it and write them into the tag, e.g. `[verified-live YYYY-MM-DD, n=K]`.
If the dossier row is itself undated, say so in the topic instead of inventing a date — that is a
finding for the modding lane, and a second drop into `engine-research/inbox/` is the route.

The other undated `[verified-live]` strings check 4 lists across the estate are prose mentions
("converts it to `[verified-live]`"), not tags in use; this one is a tag in use.
