# psychonauts-vr

VR support mod for the original 2005 **Psychonauts** (Double Fine) — a DirectX 9, custom-engine game with no existing VR modding framework.

**Status: v0.1.0-alpha, early/experimental.** Two things confirmed working: (1) real gameplay renders correctly in stereo on both eyes, confirmed by direct play-testing, as a side-by-side monitor view; (2) an experimental bridge that submits real frames to a VR compositor (SteamVR/OpenVR), proven working end-to-end via SteamVR's headset-free null driver — but **no physical VR headset has been used in any test yet**, and performance with that path enabled is currently low. See [proxy-d3d9/USAGE.md](proxy-d3d9/USAGE.md) for exactly what is and isn't confirmed, and known issues. For development history and raw material, see [psychonauts-vr-dev-archive](https://github.com/TefMeister/psychonauts-vr-dev-archive). For field notes and technical write-ups, see [psychonauts-vr-modding-notes](https://github.com/TefMeister/psychonauts-vr-modding-notes).

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.
