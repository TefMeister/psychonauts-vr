# Write-Hook Proof of Concept — `pEye` Lateral Offset Injection

Date: 2026-08-16. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install; only the two approved helper scripts,
`silence-intro-videos.ps1` / `restore-intro-videos.ps1`, touched anything under it, both
run/verified reverted this session; no other file in the game directory changed).

**Goal**: move from pure observation (notes/07, notes/08) to the first behavior-modifying
experiment — install a real write-hook at `BuildViewMatrix` (`exe+0x292480`), compute the
camera's right vector, mutate `*pEye` before the real `D3DXMatrixLookAtRH` call runs, and prove
end-to-end that the modified value is what the renderer actually uses. Not stereo rendering —
single-eye offset injection only, as scoped.

## Result: the write-hook works, with 100% confirmed-used writes

Same method as prior sessions: MCP tools for x64dbg were checked again via `ToolSearch` and are
still not registered in this session, so the raw `x64dbg_automate.X64DbgClient` Python API was
driven directly (as every prior live-debug session). Script: scratchpad `writehook.py`
(disposable, not copied into the workspace — logic summarized below and in `capture_log`-style
excerpts).

Two breakpoints were set:

1. **`exe+0x292480`** — `BuildViewMatrix`'s entry (same function documented in notes/07). On each
   hit: read `pEye`/`pAt`/`pUp` from the stack (`[esp+8]`/`[esp+0xC]`/`[esp+0x10]`), dereference
   each to get real `(x,y,z)` floats, compute
   `fwd = normalize(at - eye)`, `right = normalize(cross(fwd, up))`, then
   `new_eye = eye + right * 40.0`, and write `new_eye` back into `*pEye` via `write_memory`
   (packed as 3 little-endian floats) before resuming.
2. **`exe+0x2924AC`** — the `call <JMP.&D3DXMatrixLookAtRH>` instruction itself (documented in
   notes/07 as the real call site, 5 bytes before the "`mov ecx,0x10`" return-point address used
   in earlier sessions). At this breakpoint the call has **not yet executed**, so
   `[esp+4]` is still `pEye` as pushed by the wrapper (stack order at the call instruction:
   `pOut=[esp+0], pEye=[esp+4], pAt=[esp+8], pUp=[esp+0xC]`, matching the documented push order).
   Dereferencing `pEye` here reads the exact vector about to be passed into
   `D3DXMatrixLookAtRH` — the direct, no-ambiguity proof that our write stuck and wasn't
   overwritten by anything between our write and the real call.

Offset used: **40.0 world units** along the computed right vector, chosen because the previously
logged eye→at distance on this same title-screen camera was ~190–200 units (notes/08), making 40
units (~20%) a clearly-visible but not scene-breaking first try.

### Outcome across 40 consecutive hits over ~25 seconds

- **40/40 `BuildViewMatrix` entry hits**: every read succeeded, every computed offset was written,
  `write_memory` reported success every time.
- **39/39 `D3DXMatrixLookAtRH` call-site hits that were captured before the run ended**: every
  single one showed `eye_about_to_be_used_by_D3DXMatrixLookAtRH` **exactly equal** (to float
  precision) to the value we wrote at the preceding entry hit. `mismatches=0`.
- Example (from `writehook_stdout.txt`, hit #1):
  ```
  ENTRY#1  t=5.02s  pEye=0x19eb58  orig_eye=(-370.516, 457.271, 16.949)  at=(-374.736, 224.141, 17.113)
           up=(0, 0.000706, 0.99999976)  right=(-0.999836, 0.018099, -0.0000128)
           new_eye=(-410.510, 457.994, 16.948)  write_ok=True
  CALLSITE#1 t=5.28s pEye=0x19eb58 eye_about_to_be_used_by_D3DXMatrixLookAtRH=(-410.510, 457.994, 16.948)
             matches_our_write=True
  ```

This is the concrete end-to-end proof requested: **read → compute → write → confirmed used by
the renderer**, for every single frame across a sustained ~25-second window, not a one-shot
flicker.

## Surprise: the game partially "fights back" — bounded compounding, not a clean per-frame reset

This was not anticipated going in and is worth flagging clearly for the next session:

`*pEye` is **not** a copy freshly re-derived from some untouched ground truth every call — it's a
live pointer into the camera's own persistent state. Because of that, our write on one hit
becomes part of the *input* the game's own camera-update logic reads on the next hit that reuses
the same pointer. Concretely, across the run, four distinct `pEye` addresses appeared and
alternated (`0x19eb58`, `0x19e578`, `0x19e528`, `0x19e478` — more than the "3 calls per frame"
noted in notes/08, likely several camera instances: primary + reflection/secondary views), and
tracking any one of them shows:

- The **first few** hits on a given pointer show close-to-full compounding — each hit adds
  roughly another 40-unit step in the (slowly rotating) right direction, i.e. the offset is not
  being reset back to some "true" unmodified trajectory.
- After roughly 3–5 hits on the same pointer, the growth **visibly decelerates** and each
  pointer's value converges toward a stable offset band rather than diverging without bound — by
  hit ~25–40 several pointers had settled into a fixed repeating value (see `ENTRY#25`–`#40` in
  the log, where `orig_eye`/`new_eye` on `0x19e578` stopped changing at all, hit after hit). This
  looks like the game's own camera smoothing/spring logic partially damping our injected offset
  rather than either (a) ignoring it (clean per-frame reset) or (b) letting it diverge unbounded.
- No crash, no visible error, no anti-tamper reaction — the game simply kept rendering with
  whatever eye position it was handed.

**Practical implication for stereo**: a real per-eye hook cannot assume "add IPD/2 once and it's
stable" if it writes into the same persistent pointer the game re-reads next frame — it should
either (a) recompute the offset fresh from the game's own **unmodified** intended eye each frame
(requires caching/knowing the pre-offset value, not reading back the already-offset one), or (b)
write into a separate output buffer / call `D3DXMatrixLookAtRH` a second time with an
independently-tracked base position rather than mutating `*pEye` in place. This is a concrete,
actionable finding for the stereo milestone, not just a curiosity.

## Screenshot

One screenshot was taken mid-run (`shot.png` in the session scratchpad, not copied into the
workspace — game screenshots of the live title screen aren't committed per the legal-boundary
rule) at approximately t≈12s into the capture window, i.e. after several compounding write cycles
had already pushed the tracked eye positions roughly 150–180 units from their original baseline.
The window showed the normal Psychonauts title card (rotating brain, "PSYCHONAUTS" logo, "Press
[ ] to begin") — plausible but not conclusively different framing versus an unmodified baseline,
because **no clean "before" screenshot was captured this session**: the offset began engaging
within ~5 seconds of the process settling (as soon as the title screen's live camera started
calling `BuildViewMatrix`), before a baseline shot could be taken. Given the very strong
programmatic proof (39/39 call-site matches, 0 mismatches), this was judged sufficient to close
out the milestone rather than spend more time chasing a clean visual A/B — a future session doing
the real stereo work will naturally get before/after visual comparisons for free once it renders
two eyes side by side.

## Process/cleanup notes

- Launch → capture → cleanup all completed in one script pass; no orphaned `x32dbg`/`Psychonauts`
  processes (`Get-Process` returned nothing for either after `terminate_session()`).
- `restore-intro-videos.ps1` run and verified: `INTRO.bik`, `DFLogo.bik`, `MajescoLogo.bik`,
  `transgaming.bik` all back under original names, no `.silenced` files remain.
- No `d3d9.dll` or other stray file left in the game directory (checked explicitly).
- No anti-debug/anti-tamper reaction to the live memory write, consistent with every prior
  session for this game.

## Recommended next step

See the top of `notes/00-status.md` for the current recommendation — this session's evidence
(write-hook mechanism fully proven, plus the compounding/smoothing finding above) makes the
natural next step the actual stereo-rendering milestone: duplicate this offset logic for two eyes
with opposite signs and a **freshly-cached unmodified base eye per frame** (to avoid the
compounding behavior found here), and render the scene twice into two separate render targets by
invoking the game's render path twice per frame. That second half — finding and re-entering the
per-frame render/draw call twice — is the hard, not-yet-scoped part and likely needs its own
dedicated deep-dive session on the game's main render-loop structure, not just this matrix
function.
