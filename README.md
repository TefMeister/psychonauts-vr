# psychonauts-vr

VR support mod for the original 2005 **Psychonauts** (Double Fine) — a DirectX 9, custom-engine game with no existing VR modding framework.

**Status: v0.1.4-alpha, early/experimental.** Confirmed working: (1) real gameplay renders correctly in stereo on both eyes, confirmed by direct play-testing, as a side-by-side monitor view; (2) a bridge that submits real frames to a VR compositor (SteamVR/OpenVR) — now confirmed working with a real physical headset (Quest 3 via Virtual Desktop) at the HMD's native 72Hz, with the stereo output user-confirmed correct and comfortable in-headset. New and experimental in v0.1.1-v0.1.4: **6DOF head tracking** (on by default with the VR bridge; verified on-monitor with a synthesized pose, not yet tested in a real headset), SteamVR-quit handling, a fix for the v0.1.0 exit-hang bug, a fix for the long-standing black-left-eye bug on the title/menu screen, 2× eye render resolution, an F11 recenter key, and HUD/menu UI placed at a comfortable virtual depth (a full shader audit also confirmed skinned characters were stereo-correct all along), plus a tunable field-of-view scale for matching the headset’s wider frustum. See [proxy-d3d9/USAGE.md](proxy-d3d9/USAGE.md) for exactly what is and isn't confirmed, and known issues. For development history and raw material, see [psychonauts-vr-dev-archive](https://github.com/TefMeister/psychonauts-vr-dev-archive). For field notes and technical write-ups, see [psychonauts-vr-modding-notes](https://github.com/TefMeister/psychonauts-vr-modding-notes).

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.
