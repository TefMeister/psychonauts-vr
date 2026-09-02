# 74. `ECamera::BoxVisible` located by signature; three more Astralathe-derived identities confirmed against our own binary (`/pd`, home PC, static only, NO LAUNCH)

Follow-up to the 2026-09-02 `/gr` drop (`external-research/topics/2026-09-02-astralathe-source-corroborates-the-dossier-and-fixes-the-lua-state.md`),
which listed four "concrete next steps, all static, no game running." All four run this session.
**Nothing was launched. Nothing was hooked or patched. Read-only signature scan + static
disassembly of `Psychonauts.exe` on disk**, via a one-off scanner
(`flat-to-vr-RE-toolkit/tools/` does not yet have a signature-scan tool; the byte patterns below were
matched with a throwaway script, not committed, since the toolkit's `static-disasm.py` already covers
`func`/`xrefs`/`at`/`info` and a fifth verb wasn't judged worth adding for a single afternoon's use —
worth revisiting if this account needs it again).

## 1. `ECamera::BoxVisible` — found, unique match, and its own body corroborates two other findings

Signature from Astralathe: `55 8B EC 83 EC 28 89 4D ? 8B 45 ? 8A 88 ? ? ? ?`.

**One hit in the whole `.text` section: `0x004CDC60`.** `[inferred-static 2026-09-02, n=1 static
match, signature independently authored by a second reverser]`

Disassembling it (not just matching the prologue) turned up two things that were not asked for and
are worth more than the address alone:

- **It reads bit 4 of `camera+0x530`** (`mov cl,[eax+0x530]; shr cl,4; and cl,1`) as its very first
  branch. Dossier §9 already has this exact byte, from the *opposite* side: `SetCameraPosition`
  **sets bit 0** of the same byte. So `camera+0x530` is a small flags byte the engine both writes
  (position-changed) and reads (this test) — the two findings now corroborate each other from
  independent sessions, on a field that only had one confirmed writer before today.
- **It reads `engine+0xA1`** (`mov eax,[0x78bc20]; movzx ecx,[eax+0xa1]; test ecx,ecx; je …`) —
  **the exact byte the 2026-08-24 A/B test toggled as "Visibility Tree Culling"** (notes/62/63). That
  A/B test found **zero visual difference** when flipping it and the hypothesis was written off. This
  session's disassembly shows why without needing to guess: the flag only gates **one branch inside a
  function with several other early-outs** (the `camera+0x530` bit check above returns early
  regardless of the flag's state), so toggling it can legitimately change nothing for a given test
  scene while the function and the flag are both exactly what they were said to be. **This does not
  reopen notes/63's negative result — it explains why a real toggle produced a real null**, which is a
  different thing from the toggle not working.
- **It is self-recursive**: `0x004CDCE1  call 0x4CDC60` (itself), preceded by an `EBox3` build on the
  stack from `[ebp+8..+0x1c]` and a helper call to `0x4130B0` (uninspected — plausibly a coordinate
  transform, called twice, once per box side, before the recursive call). Worth knowing before anyone
  designs a hook: naive entry/exit counting here would double-count once per recursive invocation.
- Confirms `0x004FA5A0` (called at `0x004CDC95` right after loading the `0x78bc20` singleton) as the
  "get the current camera" accessor **from a second, independent call site** — already known from
  dossier §9 as `GetCamera`; see §3 below for what Astralathe calls it.

**`ECamera::CalculateScreenDiagonal(EBox3*)`** — signature `55 8B EC 83 EC 44 89 4D ? 8B 45 ? 8B 48
? 89 4D ? C7 45 ? 00 00 00 00`, **one hit: `0x004D03B0`**. Not disassembled beyond the prologue this
session; recorded for completeness since it was scanned in the same pass.

## 2. `lua_dobuffer` at `0x6B0C00` and `GetMainChannel`/`GetCamera` at `0x4FA5A0` — both re-verified byte-for-byte

Astralathe's signatures for `lua_dobuffer` (`55 8B EC 51 8B 45 ? 50 8B 4D ? 51 8B 55 ? 52 8B 45 ?`)
and `GameApp_GetMainChannel` (`55 8B EC 51 89 4D ? 6A 00 8B 4D ? E8 ? ? ? ? 8B 00`) each produced
**exactly one match, and both land on the addresses our own dossier already had** (`0x006B0C00` from
notes/57's independent static trace; `0x004FA5A0` from dossier §9's `GetCamera`).
`[inferred-static 2026-09-02, n=1 signature match each, corroborating a pre-existing independent
reading]` — two readings, two methods, same answer. §12's item (2) in the `/gr` topic is closed: the
signatures are not just plausible, they land exactly where our own static trace already put them.

## 3. `GameApp_RenderFrame` signature lands ON `CandB`, not its caller

`/gr`'s note left this as a one-line byte check: "whether it is `CandB` (`0x004FEDA0`) or its caller."
Signature `55 8B EC 83 EC 20 89 4D ? 68 ? ? ? ? FF 15` — **two hits: `0x004FEDA0` and `0x00683C90`.**
The first is byte-for-byte our own `CandB`, already independently identified (dossier §9/§13,
multiple live sessions) as the once-per-frame, reentrancy-guarded camera-update tick this mod already
hooks. **So Astralathe's `GameApp_RenderFrame` and our `CandB` are the same function**
`[inferred-static 2026-09-02, n=1 signature match at a previously-independently-identified address]`
— which gives the existing hook a real engine-vocabulary name, and confirms (rather than merely
assumes) that the per-frame tick this mod's whole camera/void/automation stack is built on really is
the engine's own top-level per-frame entry point, not an inner helper of it. The second hit
(`0x683C90`) is unexamined — plausibly a second caller of the same prologue shape; not chased this
session since it doesn't bear on anything open.

## 4. The `lua_State` identity — closed, at `n=72`, not `n=1`

`/gr`'s check was: does the `A1`-operand in `EScriptVM`-address computations read our known singleton
`0x0078BC20`? The loose signature (`A1 ? ? ? ? 05 34 9A 00 00`, wildcarding the operand) found **72
occurrences across `.text`**. Re-running with the operand **fixed** to `0x0078BC20`'s little-endian
bytes (`A1 20 BC 78 00 05 34 9A 00 00`) matched **all 72, with zero eliminated.**
`[inferred-static 2026-09-02, n=72]` — not a coincidence: there is exactly one `GameApp` global in
this binary, so every one of the 72 sites in the executable that fetches `EScriptVM` off `GameApp`
necessarily encodes the same address, and all 72 agree with the address this dossier already uses for
the player chain, the camera chain and everything else built on `0x0078BC20`. This is the strongest
single corroboration any address in this dossier has, and it closes `/gr`'s item (1) outright:

```
lua_State* L = *(void**)( *(char**)0x0078BC20 + 0x9A3C );
```

`GameApp_CallFunctionf` (item 4's signature, `55 8B EC 83 EC 08 8D 45 ? 89 45 ? 6A 00`) was also
scanned while set up for this: **one hit, `0x005CEC10`** — a genuinely new address, not previously in
this dossier. Not exercised or corroborated beyond the signature match this session.

## What is NOT established, on purpose

- **No hook, patch, or probe was built.** The `/gr` topic's suggestion to "add a call counter next to
  the existing camera probes" is understood but deliberately not attempted this session:
  `BoxVisible` is `__thiscall` with a 24-byte struct passed **by value** and is **self-recursive**, and
  this project's existing per-frame hook (`CandB`) is a hand-built inline trampoline specific to one
  call site (`dev-archive/tools/proxy-d3d9/proxy_d3d9.c`, ~6,550 lines, no generic call-hooking
  helper). A rushed trampoline on a recursive by-value-struct thiscall is exactly the shape of bug
  this account's own method lessons warn about (see the cross-engine library's "the instrument can be
  the bug"); it deserves its own session with room to verify the calling convention by disassembling
  the two calls at `0x4CDCE1`/`0x4CDD11`/`0x4CDD34` in full, not a bolt-on at the end of a scan pass.
- **The `pVisCache` argument's role is still `[hypothesis]`** — plausibly the current leaf's decoded
  visible-set pointer, per the `/gr` topic's read of the PVS structure, but not traced here.
- **`0x4130B0`, `0x683C90` and `0x5CEC10` are unexamined** beyond what is stated above.
- **A "force `BoxVisible` to return true" mitigation is understood in principle but not built**: the
  function already contains a return-true path with a correct, compiler-generated epilogue
  (`0x004CDC7F: mov al,1; jmp 0x004CDE6C`, taken when the `camera+0x530` bit-4 check is false) — so a
  future mitigation could jump straight into that existing path rather than write a new one, which
  removes the stack-cleanup-convention risk that made the counter idea unattractive this session. Not
  attempted; recorded because it changes the risk calculus for whoever picks this up next.

## Next (either machine, either mode)

- **[PD]** Disassemble `0x4130B0` and read the two `0x4CDCE1`/recursive-call arguments fully, so a
  future hook (counter or return-true patch) is built against a known calling convention rather than
  an inferred one.
- **[FLAT]** Once a probe exists: confirm call frequency and the `pVisCache` behaviour live, and check
  whether jumping to the existing `mov al,1` path measurably changes the void's residual (the `/gr`
  topic's "second void mitigation").
- No live test needed to get value from this session — the four identities above are usable
  immediately for anyone reading the dossier, independent of when the mitigation gets built.
