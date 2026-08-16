# psychonauts-vr-modding-notes

Trial-and-error field notes from AI-assisted reverse engineering of the original **Psychonauts** (2005, Double Fine) — a project to add VR support to a DirectX 9, custom-engine game that has no existing modding framework.

This repo holds the readable, curated write-ups: what was tried, what was learned, confirmed technical facts (hook addresses, offsets, engine behavior), and the reasoning behind each decision. For the raw, messy, everything-including-failed-attempts material, see the companion [psychonauts-vr-dev-archive](https://github.com/TefMeister/psychonauts-vr-dev-archive) repo. For actual working mod releases, see [psychonauts-vr](https://github.com/TefMeister/psychonauts-vr).

## Status

See `00-status.md` for the current state of the project. As of the latest update: a first working side-by-side stereo render of the title screen, with a per-eye camera correction confirmed (via a controlled offset comparison) to reach the GPU and visibly change what's rendered — see `14-shader-constant-stereo-hook.md` for the full writeup and honest caveats.

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.
