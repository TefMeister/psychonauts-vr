#pragma once
/* hr_pose - one head pose from the headset, as the headset thread located it, and the exchange between threads
 * (2026-10-07, /lm hard-reset-vr reader).
 *
 *   headset thread:  pose_publish()  every headset frame (head from the VIEW space, eyes from xrLocateViews, LOCAL)
 *   game thread:     pose_latest()   at each camera update (hr_head.c), copied under a lock, never waits long
 * The pose a picture was drawn with travels with that picture (hxr_frame.h) so the headset can be told exactly which
 * way the head pointed when it was drawn. Eye 0 = LEFT, eye 1 = RIGHT, as in OpenXR and as the game's eye 0. */
#include "hr_head_math.h"

typedef struct {
    HQuat q;
    HVec3 p;        /* metres, LOCAL space */
    HFov fov;       /* radians */
} HrEyeView;

/* one motion controller, grip pose (OpenXR: origin in the palm, +Z toward the wrist, -Z along the fingers),
 * LOCAL space, metres. Added 2026-10-16 for the hands (hr_hands.c). */
typedef struct {
    int valid;
    HQuat q;
    HVec3 p;
} HrHand;

/* the controllers' triggers, grips, sticks and buttons (2026-10-16, hr_pad.c). Index 0 = left, 1 = right. Sticks
 * -1..1, x right, y up (OpenXR). Vive/WMR trackpads count as sticks. */
enum { HRB_A = 1, HRB_B = 2, HRB_X = 4, HRB_Y = 8, HRB_MENU = 16, HRB_LCLICK = 32, HRB_RCLICK = 64 };
typedef struct {
    int valid;
    float trigger[2], squeeze[2];
    float stick_x[2], stick_y[2];
    unsigned buttons;   /* HRB_* bits held this frame */
} HrButtons;

typedef struct {
    long seq;       /* 0 = none yet */
    int valid;      /* orientation and both views valid */
    HQuat head_q;
    HVec3 head_p;
    HrEyeView eye[2];
    HrHand hand[2]; /* 0 = left, 1 = right; valid 0 when the controller is off or not tracked this frame */
    HrHand aim[2];  /* the same controllers' AIM poses (the runtime's pointing direction), 2026-10-16 for hr_aim.c */
    HrButtons in;   /* buttons and sticks, read in the same headset frame (2026-10-16, hr_pad.c) */
} HrPose;

/* The matrices one render frame's camera was built with (hr_head.c), for drawing our own things into that frame
 * (hr_hands.c): a point in the REFERENCE view frame (the game's un-turned camera frame, head origin subtracted, game
 * units) goes to the eye's clip space as p * ref_to_view * proj[eye]. Added 2026-10-16. */
typedef struct {
    int valid;
    HMat4 ref_to_view;   /* T(-c) * R_head: the head's move and turn */
    HMat4 proj[2];       /* each eye's projection, the eye offset folded in */
    float units;         /* game units per metre */
    HVec3 origin;        /* the headset position (LOCAL, metres) taken as the game camera's own place */
} HrFrameMats;

void pose_publish(const HrPose *p);
int pose_latest(HrPose *out);       /* 1 if a valid pose has arrived */
long pose_count(void);              /* poses published so far */
