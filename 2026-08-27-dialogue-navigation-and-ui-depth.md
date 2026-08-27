# 2026-08-27 (later) — driving dialogue with synthetic input, and UI depth as a VR "stand out" lever

Follow-up to [`2026-08-27-automation-harness-and-free-camera.md`](2026-08-27-automation-harness-and-free-camera.md).
Two questions from the user: get past the dialogue the level jump lands in,
and see whether dialogue options can be made to stand out in VR.

## 1. Dialogue navigation — WORKS, no new code needed

`tools/input/send_key.ps1` (notes/38, foreground-grab fixed in notes/54) drives
the conversation UI directly. Verified by screen capture at every step:

| key | DIK | effect |
| --- | --- | --- |
| DOWN | `0xD0` **extended** | moves the option marker down one |
| ENTER | `0x1C` | selects the highlighted option |
| ESC | `0x01` | opens the Journal (**not** a dialogue dismiss) |

Sequence run: `DOWN` → marker moved from option 1 to option 2 (confirmed on
screen), `DOWN ×3` → landed on the last option, `ENTER` → **conversation
dismissed and gameplay resumed** (camera returned to smooth engine-driven
motion with fractional coordinates, vs. the static integer position it held
during dialogue).

So the full loop now exists: jump to a level → drive its opening conversation →
end up in live gameplay, all without touching the keyboard.

**Caveat, and it is a real one:** `SendInput` needs the game window genuinely
foreground and nobody touching the physical keyboard during the send. Unlike
the camera/level commands (which are focus-independent by design), this part is
not safe to run while the user is using the PC.

## 2. ⚠️ `CAJA` is probably NOT "Sasha's Lab" — the dossier label looks wrong

§9 lists `CAJA` as Sasha's Lab, identified in notes/55 from a loading-screen
string match (`CAJA_sashalab_load.dds`). **The level's actual content
contradicts that.** The NPC there is **Ford Cruller** — confirmed by the
conversation itself:

> "I saw you raking leaves. Was that a disguise?"
> "So what mission are you on down here?"
> "What does all this equipment do?"

Raking leaves in disguise, an underground room full of equipment at Whispering
Rock — that is Ford's sanctuary, not Sasha Nein's lab. A close-up capture after
the conversation shows the old man with the big nose and wild white hair.

The user caught this; the session had asserted "Sasha" from a blurry stereo
screenshot, which was not a defensible read. **The filename evidence and the
content evidence disagree, and content wins.** Worth re-checking the rest of
the 49-code list from notes/55 — if one label is wrong on filename evidence,
others may be too.

## 3. UI depth as the "make it stand out" lever — MEASURED, works

The mod already identifies the 10 pure screen-space UI shaders by signature and
shifts their `c50.x` per eye to place the HUD at `PSYVR_UI_DEPTH` world units
(default 200 ≈ 2 m) instead of infinity. This session made that **runtime
tunable** (`uidepth` command), so the effect could be measured live instead of
costing a relaunch per value.

Measured on the Journal overlay by cross-correlating the two eye views of a
side-by-side capture:

| `uidepth` | measured stereo disparity |
| --- | --- |
| 600 (≈6 m) | **−2 px** |
| 200 (default ≈2 m) | **−7 px** |
| 60 (≈0.6 m) | **−26 px** |

Monotonic and consistent with disparity ∝ 1/depth. **So "pull the UI forward so
it stands out" is a real, working, now-live-tunable lever** — no new rendering
work required to make dialogue *pop* in the headset.

## 4. What is NOT solved: making the OPTIONS specifically stand out

Doing it for the option list *only* (rather than the whole HUD) needs per-draw
identity, which §11 already records as the requirement ("needs per-draw
geometry/texture identity, not shader signature"). Attempted this session and
blocked:

- Armed the notes/35 one-frame trace via the new `trace` command. It fired
  correctly — 1269 lines captured.
- **Zero `TRACE-UI` lines.** Those log statements live only in
  `Hook_DrawPrimitiveUP` / `Hook_DrawIndexedPrimitiveUP`, and Psychonauts'
  UI does **not** draw through the user-pointer path.
- The trace also logs **no draw calls at all** — only `SetRenderTarget`,
  `SetDepthStencilSurface`, `GetRenderTarget`, `StretchRect`, `Present`.

**Next step for this, concretely:** add per-draw logging (shader index + vertex
extents) to the *indexed/vertex-buffer* draw hooks, gated on `g_curShaderIsUI`,
so UI draws can be told apart by screen-space position. The option list should
be separable from the speech bubble and the rest of the HUD by its Y band.

**Fallback if per-draw separation fails:** pull the whole HUD forward while a
conversation is up (drop `uidepth` on dialogue start, restore on exit). Blunter,
but §3 proves it works and it needs no new identification at all.

## Session hygiene notes

- Game state left as found: `uidepth` restored to 200, trace off, closed
  gracefully via `WM_CLOSE`.
- Two self-inflicted tooling bugs worth not repeating: `GetPixel` per pixel in
  PowerShell is unusably slow (use `LockBits`), and **PowerShell variables are
  case-insensitive**, so a loop counter `$r` silently clobbers an array `$R`.
  Both are now called out in the measurement script's header.
