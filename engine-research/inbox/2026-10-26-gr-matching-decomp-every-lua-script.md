# A public matching decompilation of Steam Psychonauts.exe exists (all Lua scripts, ~30 % of engine functions)

From: `/gr`, 2026-10-26 (estate sweep, CHECK-IN). Topic: `external-research/topics/2026-10-26-a-matching-decomp-with-every-lua-script.md`.

**What it touches:** the first-person goal (the unmined built-in first-person camera lead, the `headpos` and
`fpheight` rows on eye height), and dossier sections that name engine code by address only.

`DrDecki/psychonauts-decomp` (MIT, 2026-10-10): 106,607 of 106,607 Lua functions decompile to byte-identical source;
3,252 of 10,930 engine functions match (9.76 % of code bytes), the Lua 4.0 runtime complete; Steam exe only
`[reported 2026-10-26, README]`. Also new: `CosmicMatt/PsychonautsFix`, a d3d9 hook DLL (XInput, borderless window),
which a player could run beside our proxy `[hypothesis: may clash]`.

**Suggested dossier change:** add the decomp to the references for camera / first-person work and for cross-checking
named functions; note PsychonautsFix as a possible conflict in the install notes. Whether to run the author's
decompiler on our own game files (it only reads the install) is Tefa's call; `/gr` does not download it.
