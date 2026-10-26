# A public matching decompilation of Psychonauts: every Lua script, and a third of the engine's functions

**Status:** 🆕 new · **Priority:** medium-high. New since the last pass (repo created 2026-10-10). It targets the
same Steam `Psychonauts.exe` we mod, and turns the game's scripts (Raz, cameras, cutscenes, level logic) into
readable source with tools that check every function.

## What is public

Read online from the README and file list (GitHub viewer and API), 2026-10-26. Nothing was downloaded.
All `[reported 2026-10-26]`.

- **`DrDecki/psychonauts-decomp`** (MIT, created 2026-10-10, last push 2026-10-17, 18 stars): "a work-in-progress
  decompilation of Psychonauts (2005, PC)". No game assets or code in the repo.
  - **Scripts: 106,607 of 106,607 Lua functions match** (9,955 bytecode chunks from `.lub`, `.lpf`, `.ppf` and
    `.pkg`). Lua 4.0 with Double Fine changes; a patched compiler (`psyluac`) proves byte-identical output.
  - **Engine: 3,252 of 10,930 functions match (29.75 %, but 9.76 % of the code bytes)**; the Lua 4.0 runtime in
    `src/lua` is complete, the script-function glue in `src/game` is in progress. Built with the original compiler,
    Visual C++ 2008 RTM (15.00.21022.08).
  - Tested only against the **Steam** `Psychonauts.exe` (SHA-1 given in the README); GOG "probably has the same
    scripts" (unchecked by the author).
  - Experimental repacking: edited scripts go back into the game files with a backup/restore step. Text changes
    showed in game; a gravity change in `Dart.lua` showed no effect in the main menu (the README notes three copies
    of `Dart`).
- **`CosmicMatt/PsychonautsFix`** (2026-10-02, C++, a DirectX hook DLL + INI next to the exe): XInput and rumble,
  a fix for endless spinning on non-360 pads, Xbox prompts, **borderless window**, a fix for slow high resolutions
  with VSync. Small, quiet since 2026-10-03.
- `Fancy2209/Open-Li-Po` (C, active 2026-10-21) describes itself as "a reverse-engineering effort of Psychonauts
  (Li-Po)"; no README could be read, so nothing more is claimed.
- No other Psychonauts VR mod was found `[reported 2026-10-26, n=1 look]`.

## Why it matters here

1. **The first-person goal.** The scripts that drive Raz, cameras and cutscenes become readable text. That is where
   to look for the built-in first-person camera lead, for which script switches the camera in cutscenes and
   dialogue (the "third person for dialogue" half of the goal), and for Raz's eye and head setup that the
   `headpos` / `fpheight` rows are trying to measure `[hypothesis]`.
2. **Engine names.** Matched engine functions and the script-function table name code our dossier reached by
   address only; a cross-check is cheap. Our mod stays native C++ (standing rule); the scripts are a map, not the
   place to build.
3. **Lua runtime.** A complete, matching Lua 4.0 runtime corroborates the `lua_State` facts already in the dossier
   (the Astralathe topic of 2026-09-02).
4. PsychonautsFix touches the same `d3d9` hook point and window as our proxy; if a player runs both, they may clash
   `[hypothesis]`.

## Next step it unlocks

Read the engine and script-function names in the repo online (`src/game`, `engine/`) for camera and first-person
entries, and list what they name. The decompiled scripts themselves are generated from each player's own game
files, not published; producing them would mean running the author's tools, which is a decision for Tefa, since
this lane never downloads other people's code to study.

## Sources

- DrDecki, psychonauts-decomp: <https://github.com/DrDecki/psychonauts-decomp>
- CosmicMatt, PsychonautsFix: <https://github.com/CosmicMatt/PsychonautsFix>
- Fancy2209, Open-Li-Po: <https://github.com/Fancy2209/Open-Li-Po>
