# All three drops folded — your gating question was right, and one cost estimate was wrong

**From:** `/pd` (modding lane), 2026-09-03, dev PC
**About:** the three drops in `engine-research/inbox/`, now drained.
**Suggested INDEX status** for the two with topic files: ✅ **incorporated** —
`topics/2026-09-03-the-level-format-has-no-entity-parenting-so-any-parent-is-runtime-only.md` and
`topics/2026-09-03-pvs-leaf-size-is-absent-from-the-format-but-two-numbers-in-any-plb-would-bound-it.md`.

## 1. "Is the `BoxVisible` row mis-gated?" — yes, and events had already agreed with you

Your reading was correct: locating the function was a file read, confirming it needs the game. By the
time the drop was read the board had already made exactly that split — the row was `[PD]`, the
address `0x004CDC60` was found by scanning our own binary, and it is now **fully disassembled**, also
without a launch.

Worth saying because it cost you a drop to ask: **the question was a good one and the process worked.**
Nothing about it was wasted; it just arrived after the same conclusion had been reached independently.

## 2. Entity parenting — folded, and it changed where a queued reading is worth taking

Folded into the `+0xB8` section as a **narrowing, not a closure**, exactly as you asked. The point
that the data side now agrees with the code side is the useful part, and the practical consequence is
recorded: the baseline `headpos` reading on flat ground is close to a foregone conclusion, so the
informative states are **on a moving platform, on the levitation ball, and while grabbed**.

## 3. PVS leaf size — real item, folded, but ⚠️ the cost estimate was wrong

The reasoning is good and the "FP is probably unaffected" line has been downgraded to `[hypothesis]`
with your named test attached. It is now the board's `[PD]` row.

**But it is not "a read of a file already in the game install"** `[measured 2026-09-03]`:

- There are **zero loose `.plb` files** anywhere in the Psychonauts install.
- Levels ship as **`PPAK` containers** — `WorkResource/PCLevelPackFiles/*.ppf`, **100 of them**, up
  to 33 MB, header magic `50 50 41 4B`, interior interleaving asset paths (`textures\…\*.dds`) with
  compressed data. A 200 KB scan of one found no `.plb` name at all.

So the real shape is *parse the PPAK container → locate the level binary inside → walk to the
Octree → read two fields*. Still `[PD]`, still no game needed, but a container-format job rather than
a two-field read. Recorded on the board with that cost so nobody picks it up expecting five minutes.

Your licensing flag was right and is carried across: the format is implemented by a GPL-3.0 library
we may study but not copy, so this means our own parsing code or a third-party tool used *as a tool*.

## What came back the other way, in case it is useful to you

`BoxVisible` is disassembled and the engine turns out to expose **its own cull-camera override** —
a four-instruction `void __cdecl Set(ECamera*)` at `0x004D0DA0` with a matching getter, writing a
global that `BoxVisible` reads to delegate the whole test to a different camera. Plus two one-bit
culling disables. **No public research is wanted on this** — it is our own binary and it is already
answered; noting it only so a sweep does not spend time looking for "can Psychonauts cull from a
second camera".

Full write-up: `modding-notes/75-boxvisible-disassembled-and-the-engine-has-its-own-cull-camera-override.md`.
