# Cross-project transfer: DirectInput mouse-delta injection (Vireio's VRBoost technique) is a strong fit for "Candidate 1" — arguably a better fit here than where it was first found

**Status:** 🆕 new — a technique lead that directly targets the exact next step notes/60 already
recommended, potentially replacing the harder half of that plan rather than adding a new direction.

This came from a cross-project question (user, 2026-08-24): while researching Far Cry 2's head-tracking
roadmap item, **Vireio Perception**'s `VRBoost` component turned up as prior art — it solves generic
head-tracking-in-arbitrary-games by **masquerading tracked HMD rotation as mouse-look input**,
injected into whatever internal variables the game's own camera system already reads for yaw/pitch,
rather than by finding and mathematically composing a rotation into a render-side matrix. The question:
could the same technique apply to Psychonauts' own "Candidate 1" (feed HMD yaw into the game camera,
one of the two live candidates for the black-void-behind-player fix, per `STATUS.md`)? Read-only pass
over the latest dev-archive session notes (`notes/59` through `notes/66`) to check — no game execution,
per this repo's hard research-only scope.

## Why this is a strong match — Psychonauts already has the exact architecture VRBoost targets

`notes/60` independently discovered, from the user's own gameplay observation, precisely the situation
VRBoost is built for: **"in flat play the mouse-driven camera is already fully decoupled from Raz's
body facing — you can run with Raz facing any direction while the camera looks wherever the mouse
points, and flat rendering follows the CAMERA, not the body... This means the engine already has a
persistent camera-yaw/pitch state driven by mouse input each frame, independent of body/entity logic."**
That session's own framing of Candidate 1 shifted mid-session to exactly this: find where mouse input
drives that persistent camera-yaw/pitch state, and feed HMD yaw into it instead of/alongside real mouse
input.

`notes/60`'s "Part 1" tried to find that persistent state two ways: tracing it from the render/`CandB`
side (dead end — no mouse-input references anywhere in the traced call chain, output looked like a
local throwaway computation, not persistent storage), then pivoting to the input side (catch
`DirectInput8Create`'s return, get the mouse `IDirectInputDevice8*`, breakpoint vtable slot 9 —
`GetDeviceState` — and **walk the caller chain from there** to find where the read deltas get stored).
That second approach ran out of session time before catching the one-shot device-creation call cleanly.
It's listed as the explicit next-session recommendation (`notes/60`, "Recommendation," item 3).

## The technique this unlocks: don't just observe `GetDeviceState` — hook it and inject

The DirectInput mouse-hooking mechanics needed are standard, publicly documented, and match exactly
what `notes/60` was already about to do for observation:

- `IDirectInputDevice8::GetDeviceState` (vtable slot 9, immediate/non-buffered mode — the mode
  `notes/60` already identified as the target) fills a caller-supplied buffer. For a mouse device, that
  buffer is a `DIMOUSESTATE` struct: `lX`/`lY` (relative axis deltas since the last poll — signed
  `LONG`s) plus `lZ` and a button array. (Microsoft Learn reference confirms this layout; a minimal
  public open-source example of hooking exactly this call chain — `DirectInput8Create` →
  `CreateDevice` → the device vtable — exists at `github.com/fiki574/dinput8-hook`, checked as a
  reference for the *shape* of the hook, not copied.)
- The standard injection pattern (general DirectInput-hooking knowledge, confirmed across multiple
  independent sources, not tied to one project): call through to the **real** `GetDeviceState` first
  to get genuine hardware state, then **add synthetic deltas into `lX`/`lY`** before returning the
  struct to the caller. From the game code's point of view, indistinguishable from the player having
  moved the mouse.

**The key advantage over the plan as written in `notes/60`:** that session's plan was to use this hook
point to *observe* the deltas and then trace forward to find where they get stored persistently, so a
separate, more invasive edit could be made at that storage location. **Injecting directly at
`GetDeviceState` skips that whole downstream trace.** The game's own existing code already knows how
to turn a mouse `lX`/`lY` delta into a persistent camera-yaw/pitch update — that's the entire premise
`notes/60` confirmed. So there is no need to ever find that storage location, no need to understand its
smoothing/clamping/sensitivity logic, and no risk of missing a second write site — feed a synthetic
delta in at the one chokepoint every real mouse movement already goes through, and let the game do
100% of the rest exactly as it already does for a human hand on a mouse.

## What "candidate 1" would actually look like with this technique

- Compute the **frame-to-frame delta** of the HMD's yaw (and optionally pitch) from OpenVR's pose —
  a delta, not an absolute value, to match `lX`/`lY`'s own delta semantics.
- Scale that delta to whatever units the game's own mouse-look sensitivity expects — this needs
  empirical calibration (turn the headset a known amount, tune the scale until in-game yaw matches),
  the one piece of this that can't be looked up, only measured live. Conceptually simpler than Far Cry
  2's full 3D basis conversion (see the sibling repo's head-tracking topic) since it's a single scalar,
  not an axis-remapped 3×3 basis — no coordinate-handedness question at all, because the game's own
  code (not this mod) is doing the yaw/pitch-to-camera-orientation math, in its own native convention,
  exactly as it already does for mouse input.
- **Design choice to flag for the modding session:** add the synthetic delta on top of real mouse
  state (both inputs blend) vs. replace it outright while VR is active (HMD is the sole camera-yaw
  input source). Replacing is probably simpler and avoids double-counting, but real mouse might still
  be wanted for menu navigation — worth deciding explicitly rather than defaulting silently.
- **Scope caveat, worth stating plainly:** this only targets the *gameplay-logic* camera-yaw state —
  it does not touch or replace the render-side per-eye stereo head tracking already working (Core VR
  is already "WORKING in a real Quest 3," per `STATUS.md`). The hypothesis this technique tests is
  narrower and specific: that whatever decides what's "in view" for culling purposes (the still-unfound
  cull mechanism) reads *this* persistent camera-yaw state rather than the render matrix directly — so
  making this state track head orientation might make the culling test agree with what the player is
  actually looking at, without ever needing to find the cull function itself. If the cull test turns
  out to read something else entirely, this technique would still correctly fix the camera-yaw/render
  mismatch but might not fix the void — worth setting that expectation before investing a session in it.
- Also worth knowing before starting: `notes/60`'s own "Part 2" already proved a reliable, real-gameplay
  test harness (the `SetPendingLevel` F12 hotkey) exists for testing this live once built — no need to
  solve that separately. `notes/60` and `notes/66` both flag a session-specific window-foreground
  blocker that got in the way of *mouse-based* interaction that session, though — worth checking
  whether that's resolved before relying on any test step that needs real OS focus (this technique
  itself, being a hook rather than requiring window focus to inject, may sidestep that problem
  entirely, similar to how `GetAsyncKeyState`-based hotkeys already worked regardless of focus that
  session).

## Sources (see [CREDITS.md](../CREDITS.md) for the full standing credit)

- cybereality and the Vireio Perception contributors — `Perception` GitHub repo, `VRBoost` head-tracking-via-input-masquerade technique (originally surfaced researching `far-cry-2-vr-external-research`'s head-tracking topic, cross-applied here).
- Microsoft Learn — `IDirectInputDevice8::GetDeviceState` / `DIMOUSESTATE` reference documentation.
- fiki574 — `dinput8-hook` GitHub repo, checked as a minimal reference for the DirectInput hook shape (not copied).
- This project's own `psychonauts-vr-dev-archive/notes/60` (mouse-camera-yaw decoupling observation and the `DirectInput8Create`/`GetDeviceState` next-step recommendation this topic builds on) and `notes/66` (window-foreground blocker context).
