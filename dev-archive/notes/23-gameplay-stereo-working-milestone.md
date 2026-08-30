# Session 23 — MILESTONE: Real-Gameplay Stereo Rendering Confirmed Working (User-Verified)

Date: 2026-08-17. Target: `D:\Program Files (x86)\Steam\steamapps\common\Psychonauts\Psychonauts.exe`
(read-only game install). This is a consolidation/celebration session, not a new investigation — its
job is to record, properly and permanently, the biggest working milestone this project has had, and to
do a bounded look at one small remaining cosmetic issue.

## 1. The headline

**The user played real gameplay with the notes/22 shared-depth-stencil fix deployed and reported, in
their own words: "the game is running absolutely fine on both sides."** This is the first time in the
whole project that real, player-controlled gameplay (not just the title screen's scripted attract-mode
camera) has been confirmed — by the person actually looking at the screen, not just by an agent reading
logs — to render correctly in stereo on both eyes. The frozen-left/dark-right bug that survived two
prior rounds of targeted fixes (notes/20, notes/21) is resolved.

## 2. The arc that got here (notes/19 → notes/22)

There is no `notes/19` — the project's own numbering skips from 18 to 20; nothing was lost, this is
just how the sessions were numbered at the time. The real arc:

1. **notes/20 — first real-gameplay test, two bugs found and fixed.** Until this session, the stereo
   hook had only ever been exercised against the title screen's simple, deterministic attract-mode
   camera. The first live-gameplay test found two symptoms: the left half of the screen appeared
   frozen, the right half appeared dark/corrupted. Two real, well-evidenced bugs were found and fixed:
   a dangling-pointer risk in a redundant CPU-side `BuildViewMatrix` rewrite (disabled outright, not
   load-bearing for the actual visible correction), and a missing guard against `CandB`'s internally-
   nested `Present` call firing more than once during eye 2's pass in gameplay's richer multi-pass
   rendering (fixed with a strict once-per-double-invoke guard). Both fixes were real and necessary,
   but — as the next two sessions found — **not sufficient**: the symptoms persisted after this fix
   shipped.
2. **notes/21 — confirmed notes/20's fix works exactly as designed, but proved a second cause was
   still needed.** A live-log read from the user's own gameplay session (with notes/20's fix already
   deployed) showed the specific bug notes/20 fixed was genuinely gone (zero premature/duplicate
   internal-Present hits reached the real Present passthrough), but the eye1:eye2 draw-call skew
   notes/20 had used as supporting evidence (167:13) was essentially unchanged (109:15) — hard proof
   that skew was never caused by the bug notes/20 fixed. Three more low-risk, well-motivated fixes
   shipped (forcing the real Present to always blit the full backbuffer; a previously entirely-
   unhandled `Reset` hook; exact unthrottled per-frame eye1/eye2 draw-call counters replacing the old
   race-sampled metric) — genuine correctness improvements, but still not the root cause, and still
   not yet live-tested at the time they shipped.
3. **notes/22 — the actual root cause, found by refuting the working hypothesis with hard numbers.**
   Reading the exact per-frame counters notes/21 had shipped (but never actually been read) against a
   full live gameplay session found **206/206 real frames with eye1's draw count EXACTLY equal to
   eye2's** (sum 16960:16960) — proof the long-chased "eye1:eye2 draw-call asymmetry" that had driven
   two full sessions of fixes was **never real**. It was traced to its exact cause: a shared log
   print-throttle (`static DWORD s_lastLog`) that systematically starved eye 2's log line regardless of
   real relative work — a logging artifact, not an engine-state signal. With that hypothesis eliminated,
   the code was re-examined for a structural asymmetry instead of a volume asymmetry, and a real one was
   found: **both eyes had always rendered against the device's single shared auto depth-stencil
   surface** (`SetDepthStencilSurface` was never called anywhere in the file), and only eye 2 ever
   explicitly cleared it. This exactly matched a clue notes/14 had recorded years earlier but never
   fully connected ("clearing BOTH eyes flipped which eye's background was missing"). **Fixed by giving
   each eye its own private depth-stencil surface** and clearing both eyes unconditionally every frame.

The pattern worth naming: two rounds of real, defensible fixes (notes/20, notes/21) targeted genuine
bugs that turned out not to be the actual cause, because the metric being used to validate them (the
throttled phase=1/phase=2 log ratio) was itself broken in a way that made it look unmoved by anything.
notes/22 broke that impasse specifically by going back to hard, unthrottled, per-frame numbers instead
of trusting the existing metric — and that discipline is what surfaced the real bug.

## 3. This session: the fix is confirmed live, by the user, in real gameplay

notes/22 shipped its fix un-tested (the user's game was already running mid-level and, per this
project's standing safety rule, was never touched). This session started with the user's game already
running again — PID 9188, started 11:39:34 — this time already on the notes/22 (post-fix) binary,
confirmed via the log's `SetupStereoSurfaces` line for this PID showing the new `GetDS`/`Eye1DS`/
`Eye2DS` fields that only exist in the notes/22 build:

```
[2026-08-17 11:39:36.550] SetupStereoSurfaces: GetBackBuffer hr=0x00000000 ptr=0x009E6A40 |
  Eye1 hr=0x00000000 ptr=0x009E6EA0 | Eye2 hr=0x00000000 ptr=0x009E6860 (640x480) |
  GetDS hr=0x00000000 ptr=0x009E6FE0 | Eye1DS hr=0x00000000 ptr=0x009E6C20 | Eye2DS hr=0x00000000 ptr=0x009E6900
[2026-08-17 11:39:36.551] Stereo ready = 1
```

The user played real gameplay against this build and reported directly: **"the game is running
absolutely fine on both sides."** This is the empirical confirmation notes/22 could not get for itself.

**Passive, read-only corroboration gathered this session** (no input sent to the game, no window
focus change — captured via `PrintWindow` against the game's own window handle so as not to interfere
with the user's still-live session): a screenshot of the game's current state, an OS focus-loss
auto-pause dialog rendered as a 3D scene with a 2D card overlay, showing **correct, matching stereo
content on both halves** — same background folds, same card texture, same text, differing only in the
expected horizontal parallax position:

- Left half and right half both show the "While you were away, your game was automatically paused."
  card at matching (not identical — correctly offset) positions against a matching green/teal
  background.
- This screen exercises the exact composite path (`StretchRect` both eyes into the backbuffer, real
  `Present`) with `svscfEye1=0 svscfEye2=0` (this screen doesn't drive the register-6 camera-matrix
  upload — it's a paused/frozen scene) — useful independent evidence that the composite/depth-stencil
  fix holds up even off the main gameplay path, not just under active camera movement.

A full-log pass across every `Present() composite` line ever recorded by this DLL (both the pre-fix
session, PID 2340, and the current post-fix session, PID 9188 — 10,776 log lines total) found **zero
StretchRect failures** (every logged `hrL`/`hrR` is `0x00000000` = `S_OK`) and **zero eye1/eye2
draw-count asymmetry of any kind**, in either direction, across the entire log — reinforcing notes/22's
finding that the draw-count metric itself was always fine; the depth-stencil sharing was the real bug.

## 4. What this milestone means for the project

This project has gone from "can we hook D3D9 at all" (notes/04-06) through "can we move a camera at
all" (notes/07-09) through "can we render two independent passes in one frame at all" (notes/10-13)
through "does a per-eye camera offset actually reach the GPU" (notes/14) through "does any of this
survive contact with real, player-controlled gameplay" (notes/20-22) to: **real gameplay renders
correctly in stereo on both eyes, confirmed by the person playing it.** This is the first time the
project has cleared its own original bar — a working stereo render of the actual game, not just the
title screen — and is the clearest signal yet that the whole approach (inline hooks into the game's own
render-dispatch function, a shader-constant correction on the per-draw WVP upload, per-eye offscreen
render targets with now-private depth-stencil surfaces) is fundamentally sound.

Honest scope of what is and isn't confirmed:
- **Confirmed**: real gameplay, played by the user, on the current build, renders correctly in stereo
  on both eyes ("running absolutely fine on both sides").
- **Not yet confirmed**: this has not been tested with a real VR headset or SteamVR/OpenXR runtime —
  it is still a side-by-side window render, viewed on a monitor. No head tracking exists yet (see
  notes/13 §7 / USAGE.md "Known limitations" — still accurate). The per-eye offset is a rigid
  parallel-axis offset (correct for comfort per notes/15's IPD cross-check), not a converged/toe-in
  stereo pair.
- **New minor issue found in the same round of testing**: the main pause/menu UI screen's left eye
  was reported completely black — see §5 below. Explicitly out of scope for blocking this milestone;
  documented and bounded rather than chased.

## 5. Minor remaining issue: main-menu left-eye-black (diagnosed, not fixed — bounded effort)

The user separately reported that the **main pause/menu UI screen** (not the title screen — that has
been extensively tested and works; not real gameplay — that's the milestone above) shows a completely
black left eye. This section documents what was investigated this session and why it was not blindly
fixed.

**What was ruled out, with hard evidence from the full live log (10,776 lines, both sessions):**
- **Not a draw-call omission.** Every single logged `svscfEye1`/`svscfEye2` pair across the entire log
  is either `0:0` or an exactly-matched positive pair (e.g. `101:101`, `162:162`, ...) — there is no
  frame anywhere in the log where one eye has draws and the other has zero. If the main menu's left
  eye were rendering nothing at all, that would show up as an asymmetric pair (`0:N` or `N:0`) — none
  exist. Whatever is happening, both eyes are receiving the same number of register-6 corrected draw
  calls.
- **Not a `StretchRect` failure.** Every logged composite line has `hrL=0x00000000 hrR=0x00000000`
  (both `S_OK`) — the copy from each eye's offscreen surface into its half of the backbuffer always
  succeeds at the API level.
- **Not the composite mechanism itself being broken for low-activity screens.** The passively-captured
  auto-pause dialog screenshot (§3) proves the exact same composite path renders correctly even at
  `svscfEye1=0 svscfEye2=0` (a static/frozen scene with no active camera-matrix uploads) — so "the menu
  doesn't drive register 6" alone cannot explain a black eye; the auto-pause dialog also doesn't drive
  register 6 and both its eyes render fine.

**Leading hypothesis, not confirmed (needs a live reproduction, not code speculation):** the per-draw
correction (`patched[3] += (-d) * xScale`, where `d = ±3.25` world units) is a fixed-world-unit offset
calibrated against gameplay/title-screen camera distances (tens to thousands of world units away). If
the specific main-menu screen uses its own distinct close-up camera framing (common for pause/main
menus — a tightly-framed character/icon shot, unlike the title screen's wider attract-mode shot or
gameplay's larger distances), a fixed 3.25-unit lateral shift could push that close geometry outside
one eye's view frustum (near-clip or simply off to the side) while leaving the other eye's
opposite-signed shift within bounds — a real, geometry-position bug, not a missing-draw or
failed-API-call bug, which is exactly why the log's draw-count parity looks perfectly healthy while the
visual result is asymmetric.

**Why this was not blind-fixed this session**: the game was already running the user's own live session
throughout (see §3) — reproducing the exact main-menu screen would require sending input into their
active session, which this project's standing safety rule (and this session's explicit instructions)
rules out. Without a live reproduction, there is no way to read the actual `xScale`/`d`/`delta` values
or `BVM cache SET` eye/at data at the moment the bug occurs, and shipping a code change against a
hypothesis with no confirming measurement risks regressing the now-confirmed-working gameplay fix for
an unconfirmed guess. Per this project's own repeated practice (e.g. notes/18's Lead 1, notes/16's
false-positive catch), an untested guess is worse than an honestly-flagged open item.

**Concrete next step** (cheap, no new investigation needed): next time the user is at the main menu with
this build running, read `%TEMP%\psychonautsvr_proxy.log` for the `SVSCF stereo-correct` lines logged
in that window (`reg=... phase=... xScale=... d=... delta=...`) and the `BVM cache SET` line immediately
before it (`eye=(...) at=(...) right=(...)`) — that's enough to check the near-clip-frustum hypothesis
directly (does the eye/at distance at the menu look unusually small compared to gameplay's, and does the
patched W-row shift plausibly exceed it) without any further code changes or live debugging session.

## 6. Disposition

- **Milestone (§1-4)**: fully confirmed — user-verified, in their own words, playing real gameplay
  against the notes/22 build. This is the strongest evidence bar the project has ever cleared.
- **Menu black-left-eye (§5)**: diagnosed as far as passive log/screenshot evidence allows (ruled out
  draw-call omission and StretchRect failure; leading hypothesis is a near-clip/frustum interaction
  specific to a close-framed menu camera), not fixed — correctly bounded per this session's own
  explicit "don't chase this, it's not worth extensive time" instruction. No code changes made for
  this issue.
- **No code changes this session** — `proxy_d3d9.c` is unchanged from notes/22's fix. The DLL currently
  in the game directory (running as PID 9188) is already the notes/22 build.
- **Mod repo**: this is the first session where the public mod repo's README/USAGE.md get to say
  "gameplay stereo rendering works, user-confirmed" instead of "title-screen-only, experimental." See
  the updated `USAGE.md` for the exact new framing, including the still-open menu issue and the
  still-pending real-headset validation.
