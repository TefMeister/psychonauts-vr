# 55 — Level loader fully mapped (Lua-free!); all 49 level codes catalogued; static-only session

**Date:** 2026-08-20 evening, dev machine. Per the user's request ("learn what you can about the
game without actually running the game for a few hours"), this is a **pure static research
session** — no game launch, no debugger attach to a live process, only offline disassembly
(`llvm-objdump`) of the exe on disk (non-relocatable, so static addresses = live addresses,
per notes/45) plus filesystem/string inspection of the game's own data files. Direct continuation
of the automation blocker from notes/54 ("find an in-engine level-jump instead of walking").

## Headline finding: a direct, Lua-free level-load call

Starting from `LoadNewLevel` in `tools/lua-bindings.def` (a Lua binding nobody had traced before),
fully disassembled the call chain with zero live capture needed:

```
LoadNewLevel(level_name)              [Lua binding shim @0x5BBA90 -> impl @0x5BBAF0]
  name = lua_tostring-equivalent(L, 1)         (0x5B02B0 - see "corrected mapping" below)
  if strrchr(name, '.') == NULL:               (no extension given)
      snprintf(buf, 200, "workresource\levels\%s.plb", name)   [format string @ 0x7119b0]
  else:
      strncpy(buf, name, 200)                  (name already has an extension - used as-is)
  normalize '/' -> '\' in buf
  SetPendingLevel(g_levelMgr, buf, some_flag)  [0x4FFA40, see below]
```

**`SetPendingLevel` @ 0x4FFA40 is a plain `__thiscall` C++ member function** (confirmed from its
prologue/epilogue: `mov [ebp-4],ecx` at entry, `ret 0x8` cleaning 2 stack args at exit —
classic MSVC thiscall). Signature: `void SetPendingLevel(void* this, const char* path, BOOL flag)`.
Body: `strncpy(this+0x9901, path, 300)` (stores the target level path into the object), then sets
three flag bytes (`this+0x9900=1`, `this+0x9034=1`, `this+0x98FE=flag`) — an **async "please
transition to this level" request**, not a synchronous load; presumably consumed by the main loop
on a later tick. `this` = `*(void**)0x78BC20`, a live global singleton pointer (the level/game-state
manager).

**Consequence: a level transition can be requested from our own injected DLL code with a single
direct function call — `((void(__thiscall*)(void*,const char*,BOOL))0x4FFA40)(*(void**)0x78BC20,
"workresource\\levels\\CABH.plb", 0)` — no Lua execution needed at all.** This is a completely
different, much cheaper path than the multi-session Lua primitive (notes/45-50) for the SPECIFIC
problem of "get into a real gameplay level reliably" that's been blocking automation. **Not yet
tested live** — `[0x78BC20]`'s validity at the menu/title screen (vs. only once inside a level) is
unconfirmed, and the exact `flag` semantics and whether any other engine state needs to be primed
first (e.g. is a save/player-state required, or does it always start a level fresh) are unverified.
This is the natural first experiment for the next live session, well ahead of finishing Lua exec.

## Correction to notes/48's Lua arg-reading map

`0x5B02B0(L, index, 0)` — used here to read `LoadNewLevel`'s string argument — is a **generic typed
argument accessor**, not `lua_tonumber` (that's the neighboring `0x5B0230`, correctly mapped in
notes/48). Traced: it calls `0x6AF080(L, index)` to get the argument's Lua type tag, then dispatches
— **tag `1` calls `0x6AF410(L, index)` and returns a `const char*`** (confirmed: `LoadNewLevel`
immediately passes the result straight into `strrchr()`, an unambiguous C-string use). Tag `3`
routes differently (unexplored — likely another value kind, e.g. table or function). **New mapping:
type tag `1` = Lua string in this build's internal tag numbering; `0x6AF080` = get-arg-type-tag;
`0x6AF410` = extract string data given a string-tagged argument.** This incrementally extends the
Lua C-API map (notes/45-50) without needing the push-side primitives those notes flagged as the
real remaining gap for arbitrary script exec — reading args was already mostly mapped; this fills
in one more accessor.

## All 49 level pack codes catalogued (from `WorkResource/PCLevelPackFiles/*.ppf`)

Every level's assets ship as a `<CODE>.ppf` (large, the level content) + `<CODE>.apf` (small,
looks like an animation/asset manifest — confirmed by grep, entries are `anims\...\*.jan` paths).
Identified several by their animation-path contents (character/prop names inside the pack are a
free, no-execution-needed way to fingerprint a level's theme):

| Code(s) | Evidence found (anim paths) | Likely area (confidence: medium-high, from evidence + general game knowledge — NOT independently confirmed by a display-name string yet) |
|---|---|---|
| `STMU` | `menubrain\door1...`, `dartnew\mainmenu_jump.jan` | **The menu/brain screen itself** — HIGH confidence, this is almost certainly what every capture this whole project has been landing on/near |
| `BVES`,`BVMA`,`BVRB`,`BVWC`,`BVWD`,`BVWE`,`BVWT` | `bull\charge.jan` etc. (BVES) | Black Velvetopia (bullfighting theme) — "BV" prefix, 7 variants |
| `MCBB`,`MCTC` | `bunnydemon\`, `butcher\` (MCBB) | Meat Circus — "MC" prefix |
| `THCW`,`THFB`,`THMS` | `becky\getscript.jan`, `becky\talkintoearpiece.jan` (THCW) | Thorney Towers Home for the Disturbed (character "Becky") — "TH" prefix |
| `WWMA` | `censor1l\`, `cantilever\bv\lamp_*.jan` | Waterloo World |
| `MMDM`,`MMI1`,`MMI2` | `boyd\boydchalk_rant01.jan` etc. (MMDM) | The Milkman Conspiracy (character "Boyd") — "MM" prefix |
| `NIBA`,`NIMP` | `breakawaybraintank\` (NIBA) | unidentified theme, "NI" prefix |
| `LLLL` | `clam\`, `crayfish\` (aquatic) | unidentified, possibly water-adjacent |
| `LOCB`,`LOMA` | not sampled this session | "LO" prefix — plausibly Lungfishopolis, unverified |
| `CABH`,`CABH_NIGHT`,`CABU`,`CAGP`,`CAGP_NIGHT`,`CAJA`,`CAKC`,`CAKC_NIGHT`,`CALI`,`CALI_NIGHT`,`CAMA`,`CAMA_NIGHT`,`CARE`,`CARE_NIGHT`,`CASA` | not sampled this session | "CA" prefix, by far the most variants (15) with day/`_NIGHT` pairs — plausibly Whispering Rock Camp's many named sub-areas (the persistent hub), unverified |
| `ASCO`,`ASGR`,`ASLB`,`ASRU`,`ASUP` | `asco_cardtower\cardfall.jan` (ASCO) | card/casino-adjacent theme, "AS" prefix, unverified which character's world |
| `BBA1`,`BBA2`,`BBLT` | not sampled | "BB" prefix — plausibly "Basic Braincase" (the tutorial), unverified |
| `SACU`,`MIFL`,`MILL`,`MMI1`,`MMI2` | not sampled | ungrouped, unsampled |
| `common` | — | shared/cross-level assets, not a level itself |

**Full raw list (verified, exact):** ASCO, ASGR, ASLB, ASRU, ASUP, BBA1, BBA2, BBLT, BVES, BVMA,
BVRB, BVWC, BVWD, BVWE, BVWT, CABH, CABH_NIGHT, CABU, CAGP, CAGP_NIGHT, CAJA, CAKC, CAKC_NIGHT,
CALI, CALI_NIGHT, CAMA, CAMA_NIGHT, CARE, CARE_NIGHT, CASA, LLLL, LOCB, LOMA, MCBB, MCTC, MIFL,
MILL, MIMM, MMDM, MMI1, MMI2, NIBA, NIMP, SACU, STMU, THCW, THFB, THMS, WWMA, common.

**Important caveat on the theme guesses above:** these are pattern-matched from animation-path
character/prop names plus my own general knowledge of the game, cross-checked against the code's
prefix groupings — not confirmed against an in-binary display-name string table (not yet found) or
live gameplay. Treat every "likely area" as a hypothesis to verify, not settled fact, per the
project's own standing discipline about not overstating certainty from partial evidence.

**HARD CONFIRMATIONS found via a direct string sweep of the exe** (not inference — literal embedded
strings), upgrading a few codes from "likely" to "confirmed":
- **`CA` = "Campgrounds", CONFIRMED**: exe contains literal
  `_Campgrounds\CA_MAP_CABHoverlay.plb` / `...CAGPoverlay.plb` / `...CAMAoverlay.plb` /
  `...CAREoverlay.plb` (only these 4 of the 15 `CA*` codes have a minimap overlay asset — the
  others may not have one, not evidence they aren't also Campgrounds sub-areas). Also confirms the
  exact real relative path convention: `WorkResource\Levels\CA...` and (separately, at a different
  offset) the literal string `Levels/CAKC.plb` — direct proof `.plb` files really are named exactly
  as `LoadNewLevel`'s format string predicted.
- **`CAJA` = Sasha's Lab, CONFIRMED**: literal string `CAJA_sashalab_load.dds` (a loading-screen
  texture filename).
- A `total purge Common <CODE>` table at file offset ~0x30FA00-0x30FDFF lists **every** level code
  in sequence — a level-dependency/memory-purge-order table, not display names, but it independently
  cross-validates the full code list already extracted from the `.ppf` filenames (exact match).
- Level-persistence Lua source text is embedded in the exe as literal strings, e.g.
  `self.saved.BVRB = { }` / `self.saved.SACU = { }` / `self.saved.SA = { }` — confirms per-level
  (and per-area-prefix, e.g. `self.saved.BV`) persistent Lua tables are a real, named mechanism;
  useful vocabulary for any future Lua-script reading/writing work.
- Only 2 literal `*_load.dds` loading-screen strings exist IN THE MAIN EXE (`CAJA_sashalab_load.dds`,
  `ca_night_load.dds`) — most per-level loading-screen filenames are presumably referenced from
  inside the level `.ppf`/`.apf` pack files instead, not the exe itself (not yet searched this
  session — a promising next static step for identifying the remaining unconfirmed codes).

## Why this matters for tomorrow

The automation blocker (notes/54: `enter_gameplay.ps1`'s blind walk deterministically misses the
CONTINUE door) has a **completely different, likely-more-robust candidate fix**: skip the walk
entirely, call `SetPendingLevel` directly with a known level code (`STMU` for the menu itself is
already confirmed reachable — that's where every capture has landed; a real gameplay level like one
of the `CA*` camp codes would be the next thing to try) once the game reaches the title/menu. This
needs a small, careful, LIVE-tested addition to `proxy_d3d9.c` (a new hook or on-demand call
wired to a key/env-trigger) — appropriate for tomorrow's session, not attempted tonight per the
"no running the game" instruction.

🤖 Pure static disassembly + filesystem/string inspection this session; zero game execution, zero
debugger attachment, zero files modified. All addresses confirmed via the non-relocatable exe's
on-disk bytes only.
