# 58 — DumpSkeletonInfo partially traced: entity bone-count/array offsets found

**Date:** 2026-08-20 evening, dev machine. Static-only continuation (notes/55-57). Partial trace of
`DumpSkeletonInfo` (`0x00571F10`), the Lua binding flagged since notes/44 as the intended way to
identify Raz's head-bone index — relevant now that notes/57 substantially de-risked the Lua exec
primitive this same session.

## Confirmed

- Takes **1 argument**, read via `0x5B01E0(L, 1, 0)` (a different accessor than the string-getter
  found in notes/55 — likely an entity/userdata-pointer accessor; not further decoded this session).
- Resets a global counter at `0x78C9FC` (probably an output-index for wherever the dump gets
  written — not traced to its destination this session).
- **Entity struct offset `+0x54`**: a packed DWORD; bone count = `(dword >> 5) & 0x7FFFFFF`. If this
  read holds for Raz's entity, it directly gives bone count without any Lua call.
- **Entity struct offset `+0x5C`**: pointer to an array of per-bone pointers (`bone[i]` = the
  DWORD at `entity[0x5C] + 4*i`).
- Each `bone[i]` structure has a sub-pointer at `+0x8`, which itself has a pointer at `+0xA8` and a
  count byte at `+0xAC`; walking further reaches an array of **0x60 (96)-byte per-record
  structures** — plausibly per-bone metadata (name, parent index, bind transform, etc.), not
  decoded field-by-field this session — that's real further work, not attempted tonight given the
  risk of mis-reading struct layout from pure static disassembly without a live instance to
  cross-check offsets against.

## Not attempted / explicitly out of scope tonight

Fully decoding the 96-byte bone-record layout (which fields are name/parent/transform and at what
offsets) needs either much more careful static tracing or — more reliably — a live capture (dump
the raw 96-byte records for a known entity and eyeball/diff them against known bone counts/names).
Flagged for a future LIVE session, not pursued further under tonight's "no game execution"
constraint.

## Why this still matters

Even without finishing the bone-record layout, the `+0x54`/`+0x5C` entity offsets are independently
useful: if Raz's entity pointer can be identified by other means (e.g. correlating the render-level
c96-nearest-to-eye detection from notes/51 with the *entity* it belongs to, not just its render
draw), these offsets would let render-level code read his real bone COUNT and pointer array
directly — another potential Lua-free shortcut, in the same spirit as notes/55's level-loader
finding. Not yet connected to anything live; a lead for a future session, not a working mechanism.

🤖 Pure static disassembly; zero game execution this session.
