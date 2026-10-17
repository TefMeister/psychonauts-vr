#pragma once
/* hr_head_math - the matrices for the headset's own field of view and head tracking in Hard Reset
 * (2026-10-07, /lm hard-reset-vr reader). Pure C, no Windows: test/hr_head_math_test.c links it as shipped.
 *
 * THE GAME'S CONVENTIONS (static read of the camera update 0x9a9770, 2026-10-07):
 *   - row vectors: clip = p * View * Proj; matrices stored row-major, m[row][col] (helper 0x9a8ab0 computes
 *     out = View * Proj that way);
 *   - view space is right-handed: x right, y up, the camera looks down -z (the projection has m[2][3] = -1, so
 *     clip.w = -z);
 *   - depth 0..1: m[2][2] = f/(n-f), m[3][2] = n*f/(n-f) (D3DX "PerspectiveFovRH");
 *   - the game's own eye shift lives in m[2][0] = -sign*S and m[3][0] = -sign*S*conv.
 * OpenXR view space is the same frame (x right, y up, -z forward, right-handed), so a headset orientation
 * quaternion is used as it is: no axis flips (unlike The Evil Within, whose view frame is left-handed).
 *
 * HEAD ROTATION. A point p in the game's view space (the "reference": where the mouse points) seen by a head turned
 * by R (column-vector rotation of the OpenXR quaternion) and moved to c (reference coordinates) is R^T (p - c); as a
 * row vector that is (p - c) * R. So the turned view is View * T(-c) * R4, where R4 is R in the top-left 3x3.
 *
 * PER EYE. Each eye sits at e (head frame) and may be rotated by Rrel = R_head^T R_eye (canted displays); a point in
 * the turned head frame goes to the eye frame as (p - e) * Rrel. That is folded into the eye's projection:
 * EyeProj = T(-e) * Rrel4 * P(fov), so the game's single view matrix stays the head's (culling follows the head) and
 * its two eye projections carry the rest, exactly where the game already puts its own per-eye shift. */

typedef struct { float m[4][4]; } HMat4;
typedef struct { float x, y, z, w; } HQuat;
typedef struct { float x, y, z; } HVec3;
typedef struct { float left, right, up, down; } HFov;   /* OpenXR angles in radians: left and down are negative */

HMat4 hm_identity(void);
HMat4 hm_mul(const HMat4 *a, const HMat4 *b);                 /* a * b */
int hm_inverse(const HMat4 *a, HMat4 *out);                   /* 0 if singular */
HVec3 hm_transform_point(const HMat4 *m, HVec3 p, float *w);  /* row vector (p,1) * m; returns xyz, *w = w */

/* Column-vector rotation matrix of a unit quaternion, as a 4x4 (normalises; identity for a zero quaternion). */
HMat4 hm_rot_from_quat(HQuat q);
HMat4 hm_translate(HVec3 t);                                  /* row-vector translation: p -> p + t */
HMat4 hm_transpose(const HMat4 *a);

/* The game-style projection for an OpenXR field of view (asymmetric), near n, far f (game units). */
HMat4 hm_proj_fov(HFov fov, float n, float f);

/* The game's own symmetric projection, for comparison: vertical fov (radians), aspect = width / height. */
HMat4 hm_proj_game(float fov_y, float aspect, float n, float f);

/* The smallest fov covering both eyes, widened by margin (radians) on every side (each edge kept under ~85 deg). */
HFov hm_fov_union(HFov a, HFov b, float margin);

/* View * T(-c) * R4  (head_pos c in the reference frame, game units). */
HMat4 hm_turn_view(const HMat4 *view, HQuat head_q, HVec3 head_pos);

/* T(-e) * Rrel4 * P: e = the eye's offset from the head in the HEAD frame (game units), Rrel = R_head^T R_eye. */
HMat4 hm_eye_proj(HQuat head_q, HVec3 head_pos, HQuat eye_q, HVec3 eye_pos, const HMat4 *proj);

/* Yaw (+ = turned left), pitch (+ = looking up), roll (+ = tilted left, right ear up) in degrees, for the log. */
void hm_quat_ypr_deg(HQuat q, float *yaw, float *pitch, float *roll);
