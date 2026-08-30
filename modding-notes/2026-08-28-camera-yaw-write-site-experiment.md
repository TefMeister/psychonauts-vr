# Camera-yaw write site — the experiment, written BEFORE running it

**Date:** 2026-08-28 · **Session type:** modding (dev PC, monitor only)
**Status:** built and deployed, **awaiting one live run**

## Why this exists

The black-void problem is a **culling** problem: the engine only draws what its
own frustum accepts, so anything that changes the *image* after culling (the
per-draw register-6 MVP patch, §6) rotates the void along with the view and
cannot help. The only fix is to change what the engine *decides* to draw — which
means the camera has to be turned before the draw traversal.

Two routes exist. The input route (Candidate 1: injected gamepad axis, the
engine turns its own camera) **works** and got the void from 92 % to 18.5 %, but
it is hard-limited by the game's free-look clamp at **87.4°**, which is absolute
rather than a rate — over-driving it does nothing. The direct route — write the
camera matrix ourselves — has no clamp at all, and that is what this tests.

## What was already established

`[verified-live 2026-08-27]` from `2026-08-27-void-camera-facing-hunt.md`:

- `camera+0x20` — what `SetCameraOrientation`'s impl writes — is **never read by
  the renderer**. Write persists untouched, view unchanged. A *wrong-field*
  failure.
- `camera+0x90` (the real camera world matrix) is **overwritten inside the same
  frame**. Yaw 0 and yaw 90 dumps came back bit-for-bit identical
  (`3F2438D3 BF44629C…`); A/B/A measured 9.95 / 10.12 / 10.00 % black. A
  *timing* failure — right field, wrong moment.

The write was applied from **`BeforeEye1` only** — the single call site in the
whole proxy — i.e. *before* the first real `CandB`.

## The hypothesis being tested

`[hypothesis]` The 08-27 conclusion was "the write must go **inside** `CandB`",
which reads as a big RE job. **notes/59 makes it much cheaper:** `CandB` is the
camera's *own update tick* and is **not** the renderer — the real draw chain is a
**sibling** of `CandB` under a higher per-frame dispatcher, **not nested inside
it**.

If that is right, the write never needed to be inside `CandB`. It only needs to
land **after the camera update finishes** and **before the sibling draw chain
runs** — and the existing trampoline already provides two such moments that have
**never been tried**:

| site | when | status |
| --- | --- | --- |
| 1 — `BeforeEye1` | before the first real `CandB` | ❌ refuted 2026-08-27 |
| 2 — `BeforeEye2` | **after the first real `CandB` returns** | **never tried** |
| 3 — `AfterBoth` | after both `CandB` invocations | **never tried** |

## What was built (deployed, not yet run)

- `camyawsite <1|2|3>` — selects where the yaw write is applied, so all three
  are testable in **one launch** instead of three rebuilds.
- `camprobe <n>` — for `n` frames, logs camera world-matrix **row 0** at every
  site, before and after each write, as **floats and raw hex**. Bit-for-bit
  equality is the actual question and printed floats can hide a low-bit change.
- Site 1 behaviour is unchanged, deliberately: the known negative should be
  reproducible **on purpose** rather than assumed.

Build: 2 warnings, both pre-existing (OpenVR `EXTERN_C`, `Direct3DCreate9`
dllexport). Deployed 195,584 → 198,656 bytes.

## The measurement plan, fixed in advance

Run in a **large open outdoor area** (save 3 or 4 — Raz cannot fall off), since
a cramped interior geometry-bounds the void and confounds the reading.

1. `camyaw 0` + `camprobe 20` — **baseline**. Shows what the engine itself does
   to row 0 across the three sites with no write at all. Without this, a later
   "it changed" cannot be attributed.
2. `camyawsite 1`, `camyaw 90`, `camprobe 10` — **reproduce the known negative.**
   Expect: post-write differs, next-site value reverts to the engine's.
3. `camyawsite 2`, `camprobe 10` — **the real test.**
4. `camyawsite 3`, `camprobe 10` — fallback if 2 fails.

At each step: a screen capture, measured with `analyze-capture.ps1 -Black` for
near-black percentage **plus row and column profiles** (the tool reports both
axes precisely because a vertical claim was once wrongly read off column data).

## What counts as which result

- **Survives to the next site AND black % drops** → the void has a clean fix with
  no free-look clamp. This is the win condition.
- **Survives but black % unchanged** → the matrix is not what culling reads;
  the cull frustum is built from something else, and *that* becomes the target.
- **Clobbered at every site** → the camera update is not where I think it is, and
  the sibling-dispatcher model from notes/59 needs revisiting before more code.

**Recording this before the run on purpose.** Today's XIII session had to
withdraw a conclusion that was recorded confidently from a single observation
whose precondition was never checked; the estate-wide fix was to state what
would count as evidence *before* collecting it.
