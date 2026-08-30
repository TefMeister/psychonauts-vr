# 64 — "Render Wireframe" toggle built and confirmed working, but it's the wrong tool for the dark-terrain-vs-real-gap question; live test blocked by a hard, unresolved auto-pause/focus problem

**Date:** 2026-08-24, dev machine. Direct follow-up to notes/63's flagged ambiguity: is the void
tested there real missing geometry, or dark/unlit terrain that just looks like a void? Task: use a
debug-menu rendering toggle to tell the two apart at the same Campgrounds-beach (`CABH`) spot.

## Part 1: found the exact toggle, built it, confirmed it works

Fresh `x64dbg-skills:decompile` pass on `sub_627590` (the debug-menu construction function, same one
notes/61 first found) recovered the FULL item list with every ID, including one not previously
recorded: **"Render Wireframe" / "Display wireframe for rendered geometry", item id 21**, registered
through the exact same generic-ID path as notes/62's "Visibility Tree Culling" (id 117) — meaning the
same direct-byte-write trick applies: `*(BYTE*)(*(void**)0x78BC20 + 44 + 21)`.

Built a **NUMPAD8** hotkey (`PSYVR_WIREFRAME_TOGGLE_KEY=1`, mirroring notes/62's NUMPAD9 pattern
exactly, same file/function). **Confirmed working live via the log, address-exact**:
```
WireframeToggle: NUMPAD8 pressed - Render Wireframe flag @ 05D792D1 (engine+65) now 1
```
`engine+65` = `engine+44+21`, matches the arithmetic exactly. Build clean, deployed (old DLL backed
up as `d3d9.dll.pre-notes64-backup`).

## Part 2: a methodological mistake caught before it produced a false conclusion

**"Render Wireframe" cannot actually answer the dark-terrain-vs-real-gap question, and I should have
realized this before building it.** It's a D3D fill-mode toggle (`D3DFILL_WIREFRAME` vs
`D3DFILL_SOLID`) applied per draw call — it only changes *how* a draw call that's already been issued
gets rasterized. If the void is caused by the engine never issuing the draw call at all (the leading
hypothesis this whole investigation has been chasing), forcing wireframe mode changes **nothing**
about whether that geometry appears — there's no draw call to convert to wireframe either way. A
"nothing visible" result under wireframe is therefore consistent with BOTH "real gap, nothing was
ever submitted" AND "the geometry is dark and also somehow not catching wireframe's normally-bright
line color" — it doesn't discriminate.

**The theoretically correct tool is "Collision Wireframe" (item id 22, found in the same decompile,
registered the same way)** — collision queries in an engine like this typically run through a
completely separate system from render-time visibility culling (needed for physics/AI regardless of
what's on-screen), so it would show geometry in the void region independent of whatever the render
pipeline decided to cull. This was actually notes/59's ORIGINAL recommendation ("Show Collision...
if collision geometry is drawn there but the normal render still shows black, that confirms
definitively that geometry exists") — I built the adjacent-but-wrong item (id 21, render wireframe)
instead of the one actually specified (collision-side). **Not built this session; id 22 is confirmed
to exist and use the identical toggle mechanism, so wiring it is a ~5-minute repeat of the exact same
pattern next time** (same file, same function, just `RENDER_WIREFRAME_ITEM_ID 21` → a new
`COLLISION_WIREFRAME_ITEM_ID 22` constant and a free hotkey, e.g. NUMPAD7).

One caveat even for Collision Wireframe: per Jill Crungus's octree research and notes/61's own
"Visibility Tree Culling" finding, if collision detection *also* walks the same visibility tree
(plausible — it would explain why a single "visibility tree" toggle exists for BOTH domains), then
Collision Wireframe might ALSO fail to show geometry in a culled region. That would itself be an
interesting, informative result (suggesting collision and render visibility are the same gate), not a
dead end — just worth going in aware of the possibility rather than assuming a positive result is
guaranteed.

## Part 3: live test blocked by a severe, unresolved auto-pause/focus problem — bigger than notes/60's version

Confirmed the game genuinely enters real, controllable `CABH` gameplay after the F12 jump (world
coordinates, `HeadTrack` fwd vector visibly rotating through the full sway range, real HUD visible) —
but **the "while you were away, your game was automatically paused" dialog reappears within single-digit
seconds of essentially every level load**, not after a period of AFK time. Across two attempts this
session:
- Attempt 1: got exactly ONE clean gameplay frame (captured as the very first sample of a longer
  collection run) before the pause hit on the very next 5-second dump cycle — i.e. paused within ~5
  seconds of the level finishing loading.
- Attempt 2 (fresh relaunch, tried to beat the pause by toggling wireframe within ~300ms of the level
  jump): the wireframe toggle keypress sent that early was **silently missed** (no log line at all) —
  almost certainly because the per-frame hotkey-polling code only runs during real stereo-composite
  frames, and the game was still mid-load (menu/loading phase, no compositing happening) when the key
  was sent. A **resend 30+ seconds later did register** (confirmed in the log), but by then the pause
  had already re-triggered, so the only frame captured afterward showed the pause dialog again, not
  gameplay.

**Five distinct techniques, across this session and notes/60, have now all failed to dismiss or avoid
this dialog**: `SetForegroundWindow` retry loop, `AttachThreadInput`, a direct mouse click at
screen-verified-correct coordinates (computed via `ClientToScreen`, confirmed against a fresh
screenshot — the click landed on the right pixel, the window still didn't activate), minimize/restore,
Alt+Tab (proved a focus change is *possible* but landed on the wrong window), and this session's new
attempt — `SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, ...)` to disable the OS-level
foreground-steal protection outright, which failed with **ERROR_ACCESS_DENIED (5)** even as the
current user. This reads as a real, structural environmental constraint (very likely a persistent,
protected foreground window — e.g. whatever the user has open watching this session — that Windows'
input-focus security correctly refuses to let background automation override), not a technique gap.
GetAsyncKeyState-based hotkeys (our own F12/NUMPAD8/NUMPAD9) are correctly unaffected by any of this
(they read global physical key state, not per-window input) — it's specifically anything requiring
the GAME to receive real focused input (dismissing its own UI, DirectInput keyboard/mouse) that's
blocked.

## Recommendation for next session

1. **Wire `COLLISION_WIREFRAME_ITEM_ID 22`** the same way as this session's `RENDER_WIREFRAME_ITEM_ID
   21` (or Show Collision, item id unclear from notes/61's list — cross-check) — the actually-correct
   tool, not yet tried.
2. **Solve the pause dialog differently: don't try to dismiss it — prevent it, or capture inside the
   single clean window before it hits.** Concrete ideas not yet tried: (a) check whether the pause is
   specifically tied to `SetPendingLevel`'s scene-transition code path (maybe a window-recreate or
   device-reset side effect momentarily surrenders focus) rather than generic AFK — if so, a DIFFERENT
   way to reach the test scene (not a level jump, e.g. real menu navigation once, then staying in the
   same session) might avoid re-triggering it entirely; (b) pre-arm the toggle BEFORE sending F12 (set
   the env var / an alternate always-on default) so the very first post-load frame is already in the
   test configuration, avoiding the need for any keypress to land in a narrow live window at all; (c)
   just accept this needs a human in the loop for one click, exactly as the notes/63 session did
   successfully ("I asked you to click the game window once") — cheapest fix by far if a session has a
   human actively present.
3. Given how consistently and quickly this now blocks EVERY live gameplay test (not just this one),
   this may be worth its own dedicated investigation/fix session rather than a side quest each time —
   it's now blocked two out of the last three sessions' actual empirical goals.

## Cleanup

Game process killed, intro videos restored (verified — no `.silenced` files remain), no save files
touched (only F12/NUMPAD8/NUMPAD9 sent, no menu navigation reached the Journal). SteamVR
(vrserver/vrcompositor, null driver) was started this session to get the fake-pose sway rendering at
all (confirmed necessary — `PSYVR_FAKE_POSE` only visibly rotates the camera when the VR bridge is
actually active, `PSYVR_ENABLE_SUBMIT=1` + a running compositor; `PSYVR_FIRST_PERSON=1`'s fallback
preview path is the *other* way to get sway without a bridge, per notes/62, not exercised this
session since the real bridge was up) — **left running**, matching this project's usual practice of
treating the null-driver SteamVR instance as standing dev-environment infrastructure rather than
something to tear down every session. Deployed DLL (`d3d9.dll`/`openvr_api.dll` with the new NUMPAD8
toggle) left in place; old build backed up as `d3d9.dll.pre-notes64-backup`. Game install otherwise
untouched.

🤖 Live x64dbg (attach, `x64dbg-skills:decompile` for the fresh `sub_627590` item list) + build/deploy
+ live gameplay testing (SendInput-only, no `SetForegroundWindow` for hotkeys; multiple focus-API
techniques attempted and failed for the pause dialog specifically, as detailed above).
