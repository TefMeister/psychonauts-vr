#pragma once
/* hxr - shows Hard Reset's two eyes in an OpenXR headset (2026-10-07, /lm hard-reset-vr reader).
 * Shape ported from Metro's mxr.c (staging metro-exodus-vr/headset-bridge-2026-10-07) and The Evil Within's tew_xr.c:
 * the headset thread has its OWN D3D11 device and session and is the only thread that calls OpenXR. The handover is
 * on the CPU (hxr_frame.h), because D3D9 (non-Ex) surfaces cannot be shared with D3D11.
 *
 * Psychonauts: on with PSYVR_OPENXR=1 (px_04b_xrbridge.c.inc). Settings in psyvr_xr.ini beside the exe, section [xr]:
 *   runtime_json=<path>   use this OpenXR runtime for this game only (sets XR_RUNTIME_JSON in this process), e.g.
 *                         the simulator's 32-bit json on the dev PC
 *   test_pattern=1        left eye solid red, right eye solid blue, instead of the game picture
 * Needs the 32-bit (x86) openxr_loader.dll beside Psychonauts.exe; without it one log line says so and the game runs on.
 * Each eye goes out as a projection view (the OpenXR simulator ignores quad layers). */

/* GAME THREAD: start the headset thread once (later calls do nothing). */
void hxr_start(void);

/* 1 while the headset session runs: only then is it worth reading frames back. */
int hxr_live(void);

typedef struct {
    long submitted;       /* headset frames ended WITH the game's picture (or the test pattern) */
    long empty;           /* headset frames ended with no picture (nothing handed over yet, or not shown) */
    long uploads;         /* handed-over frames the headset thread took and uploaded */
    long own_pose;        /* headset frames submitted with the pose the picture was drawn with */
    long hands_located[2]; /* frames in which the left / right controller was found (0 = controllers off or absent) */
    long aims_located[2];  /* frames in which its AIM pose was found too (2026-10-16, hr_aim.c) */
    const char *stage;    /* off / loading / running / failed */
} HxrStats;
void hxr_stats(HxrStats *out);   /* running totals; safe from any thread */
