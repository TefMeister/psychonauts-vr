# Object pop-in is a documented issue in the flat (non-VR) game too — but it's a GPU-vendor-specific driver bug, not obviously the same thing as the void

**Status:** 🆕 new · **Priority:** medium, hedged — a cheap, previously-unexplored angle worth a
30-second check, but flagged honestly as probably a different bug than the one nine sessions of deep
investigation have been chasing.

## What was found

Following up on whether "geometry doesn't render / pops in depending on camera facing" is a known
issue in the original flat game (independent of anything this VR mod does) turned up a real,
documented case — but a narrower one than initially hoped: a Steam Community guide, **"Psi-Epilepsy:
Fixing Object Pop-In and Refresh Rate Bugs,"** documents object pop-in and texture flickering
specifically as an **AMD Radeon driver bug**, not a general engine-level culling defect. The
documented fix: in AMD Radeon Settings (Crimson-era or RadeonPro for persistent cases), set the
game's antialiasing method to **"Adaptive Multisampling"** (may require switching AA mode first to
expose that option, and setting it in Global settings rather than just the per-game profile for some
users). Multiple independent community threads (GOG forums, other Steam discussions, YouTube videos
titled "Psychonauts - AMD glitch") corroborate this as a real, recurring, AMD-specific problem with an
AA-setting-level fix — i.e. this is squarely a GPU-vendor/driver/AA-mode interaction bug, not
something in the game's own CPU-side visibility logic.

## Why this is worth recording despite the hedge

This is explicitly **not** confirmed to be the same phenomenon as the black-void-behind-player bug
this project has spent nine dev-PC sessions investigating — the symptom description here (flickering,
pop-in tied to a specific AA mode on a specific GPU vendor) doesn't obviously match the "large regions
of geometry simply don't render behind the player" behavior documented from the real Quest 3 headset
session (notes/40). But it's worth recording for two reasons:
1. **It's genuinely cheap to rule in or out** — check the dev/home PC's GPU vendor and current AA
   setting; if either machine is running an AMD card without this specific AA-mode workaround applied,
   that's a real, near-zero-effort variable to control for before trusting any further void-related A/B
   test result, regardless of whether it turns out to be *the* cause.
2. **It's independent confirmation that this 2005-era renderer has real, documented GPU-vendor-
   dependent visibility/rendering quirks** — not proof of the specific void mechanism, but a data
   point supporting "this could plausibly be render-pipeline/driver-interaction-shaped, not
   necessarily a discoverable CPU-side frustum test" as one honest possibility alongside the
   already-explored octree/visibility-tree hypotheses (which notes/63's A/B test already disproved as
   *the* mechanism).

## What this research pass did NOT find

No technical postmortem detail on Psychonauts' actual rendering/culling architecture — the one
available developer postmortem (Game Developer/Gamasutra, August 2005 issue, by producer Caroline
Esmurdoc) was checked directly and contains no rendering-engine, occlusion-culling, or camera-system
technical content at all, only production/schedule retrospective material. This avenue is exhausted;
no further postmortem-digging is likely to help.

## Concrete next step

Cheap, low-priority check: confirm the dev/home PC's GPU vendor and current AA setting before the next
void-hunt A/B test session, and apply the Adaptive Multisampling workaround if an AMD card is in play
and it isn't already set — purely to rule out a confounding variable, not as a primary hypothesis for
the void itself. Given the investigation has already concluded the strongest remaining path is a
real in-headset human test, this doesn't change that conclusion — it's a control-for-confounds check
to run alongside it, not instead of it.

## Sources

- https://steamcommunity.com/sharedfiles/filedetails/?id=841015059 (title/topic confirmed via search-engine summary; direct fetch was rate-limited by Steam this pass)
- https://www.gog.com/forum/psychonauts/solution_resolving_flickering_issues_on_amd_cards
- https://www.gamedeveloper.com/audio/classic-postmortem-double-fine-s-i-psychonauts-i- (checked directly — no relevant technical content, noted as an exhausted avenue)
