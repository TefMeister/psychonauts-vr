# psychonauts-vr

VR support mod for the original 2005 **Psychonauts** (Double Fine) — a DirectX 9, custom-engine game with no existing VR modding framework.

**Status: v0.1.7-alpha, early/experimental.** Confirmed working: (1) real gameplay renders correctly in stereo on both eyes, confirmed by direct play-testing, as a side-by-side monitor view; (2) a bridge that submits real frames to a VR compositor (SteamVR/OpenVR) — confirmed working with a real physical headset (Quest 3 via Virtual Desktop) at the HMD's native 72Hz; (3) **6DOF head tracking, now confirmed on real hardware** (Quest 3 playtests, 2026-08-18/19) — motion tracks correctly and comfortably. New in v0.1.7: **the zoomed-in picture is fixed at the root** — each eye is submitted with tangent-matched texture bounds, so the compositor's angular mapping is exactly 1:1 on any headset with no per-headset tuning; the earlier "HUD invisible at FOV scale above ~1.2" issue is gone with it, and the suggested-FOV log line is restored. Known remaining issues: an over-the-shoulder culling void (the engine's frustum culling doesn't know about head rotation yet) and doubled-looking distant LOD billboard sprites — fixes queued. See [proxy-d3d9/USAGE.md](proxy-d3d9/USAGE.md) for exactly what is and isn't confirmed. For development history and raw material, see [psychonauts-vr-dev-archive](https://github.com/TefMeister/psychonauts-vr-dev-archive). For field notes and technical write-ups, see [psychonauts-vr-modding-notes](https://github.com/TefMeister/psychonauts-vr-modding-notes).

## Credits

See [CREDITS.md](CREDITS.md) — every source, tool, and prior research this project builds on, credited by name, plus a standing notice on respecting creators' wishes.
