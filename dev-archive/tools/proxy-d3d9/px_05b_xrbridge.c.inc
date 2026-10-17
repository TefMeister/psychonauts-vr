
/* 2026-10-17: the OpenXR headset output, beside the OpenVR one (Tefa: "please start the porting now on all games
 * that use openvr so they could work with openxr, we will finish the mod with openxr first").
 *
 * PSYVR_OPENXR=1 picks it (and switches the VR path on, as PSYVR_ENABLE_SUBMIT=1 does for OpenVR). Everything above
 * the headset is unchanged: the same eye surfaces, the same head tracking (VRBridge_UpdateHeadTracking is fed the
 * OpenXR head pose as the OpenVR 3x4 matrix it already reads), the same real-IPD / off-axis geometry globals. Only
 * the talking to the headset is swapped, for Hard Reset's headset thread (xr/, see xr/README.md):
 *   - xr/hxr.c runs the OpenXR session on its OWN thread and D3D11 device, and is the only code that calls OpenXR;
 *     it also reads both controllers every headset frame (grip + aim poses, buttons, sticks);
 *   - this file reads both eyes back to memory (one frame late, never waiting for the GPU) and hands them over with
 *     the head pose they were drawn with (xr/hxr_frame.c), so the game thread never waits for the headset.
 *
 * THE PICTURE'S FIELD OF VIEW. OpenVR could only be told which part of the eye texture fills the lens (the notes/43
 * crop, VRBridge_SubmitBounds). OpenXR is told the angles the picture was really drawn with, so the whole frame goes
 * out and the runtime lays it 1:1 over the lens: frame tangents x in [k - 1/xScale, k + 1/xScale] (k = the per-eye
 * shear the WVP patch applies, OpenVR sign: left negative), y +/- 1/yScale - the same extents SubmitBounds uses.
 * PSYVR_SUBMIT_BOUNDS=0 sends the headset's own angles instead (the old full-texture stretch). */
#include "xr/hxr.h"
#include "xr/hxr_frame.h"
#include "xr/hr_pose.h"
#include "xr/jitterlog.h"

#define XRB_STATS_EVERY_MS 5000u

static HrPose g_xrPoseApplied;          /* the pose the frame being drawn now was tracked with (valid 0 = none) */

typedef struct {
    IDirect3DSurface9 *mem[2][2];       /* [slot][eye], SYSTEMMEM */
    BOOL pending[2];
    HrPose pose[2];                     /* the pose each slot's picture was drawn with, its fov = the drawn angles */
    UINT w, h;
    D3DFORMAT fmt;
    unsigned frame;
    unsigned handed, not_ready, failed;
} XrReadback;
static XrReadback g_xrRb;

/* ---- the two helpers xr/ needs from its host (Hard Reset has them in d3d9_proxy.c) ------------------------------ */
void hr_log(const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, args);
    msg[sizeof(msg) - 1] = '\0';
    va_end(args);
    LogLine("%s", msg);
}

void hr_beside_exe(const WCHAR *name, WCHAR *out, DWORD room)
{
    DWORD n = GetModuleFileNameW(NULL, out, room);
    WCHAR *slash;
    if (n == 0 || n >= room) { out[0] = 0; return; }
    slash = wcsrchr(out, L'\\');
    if (!slash || (size_t)(slash + 1 - out) + wcslen(name) + 1 > room) { out[0] = 0; return; }
    wcscpy(slash + 1, name);
}

/* ---- pose maths ------------------------------------------------------------------------------------------------- */
/* v rotated by the inverse of unit quaternion q (head frame from LOCAL) */
static HVec3 XRB_RotateInv(HQuat q, HVec3 v)
{
    float x = -q.x, y = -q.y, z = -q.z, w = q.w;          /* conjugate */
    float tx = 2.0f * (y * v.z - z * v.y), ty = 2.0f * (z * v.x - x * v.z), tz = 2.0f * (x * v.y - y * v.x);
    HVec3 r;
    r.x = v.x + w * tx + (y * tz - z * ty);
    r.y = v.y + w * ty + (z * tx - x * tz);
    r.z = v.z + w * tz + (x * ty - y * tx);
    return r;
}

/* the head pose as OpenVR's device-to-tracking 3x4 (column-vector rotation, metres): both are right-handed, y up,
 * -z forward, so nothing is flipped */
static void XRB_PoseTo34(const HrPose *p, HmdMatrix34_t *m)
{
    float x = p->head_q.x, y = p->head_q.y, z = p->head_q.z, w = p->head_q.w;
    float n = sqrtf(x * x + y * y + z * z + w * w);
    if (n < 1e-6f) { x = y = z = 0.0f; w = 1.0f; } else { x /= n; y /= n; z /= n; w /= n; }
    m->m[0][0] = 1 - 2 * (y * y + z * z); m->m[0][1] = 2 * (x * y - z * w);     m->m[0][2] = 2 * (x * z + y * w);
    m->m[1][0] = 2 * (x * y + z * w);     m->m[1][1] = 1 - 2 * (x * x + z * z); m->m[1][2] = 2 * (y * z - x * w);
    m->m[2][0] = 2 * (x * z - y * w);     m->m[2][1] = 2 * (y * z + x * w);     m->m[2][2] = 1 - 2 * (x * x + y * y);
    m->m[0][3] = p->head_p.x; m->m[1][3] = p->head_p.y; m->m[2][3] = p->head_p.z;
}

/* the same globals VRBridge_QueryRealGeometry fills from IVRSystem, from the OpenXR views (OpenVR's tangent signs:
 * left and top negative). Re-run every pose, so a changed IPD is picked up; logged once. */
static void XRB_UpdateGeometry(const HrPose *p)
{
    static BOOL s_logged = FALSE;
    int e;
    for (e = 0; e < 2; e++) {
        HVec3 d;
        float l = tanf(p->eye[e].fov.left), r = tanf(p->eye[e].fov.right);
        float t = -tanf(p->eye[e].fov.up), b = -tanf(p->eye[e].fov.down);
        d.x = p->eye[e].p.x - p->head_p.x; d.y = p->eye[e].p.y - p->head_p.y; d.z = p->eye[e].p.z - p->head_p.z;
        d = XRB_RotateInv(p->head_q, d);
        g_realHalfIPD[e] = d.x * WORLD_UNITS_PER_METER;
        g_realShearK[e] = (l + r) * 0.5f;
        g_realShearValid[e] = (fabsf(g_realShearK[e]) > 1e-4f);
        g_realProjRaw[e][0] = l; g_realProjRaw[e][1] = r; g_realProjRaw[e][2] = t; g_realProjRaw[e][3] = b;
        if (e == 0) g_hmdFovyRad = atanf(fabsf(t)) + atanf(fabsf(b));
    }
    g_realProjRawValid = TRUE;
    g_vrGeomValid = TRUE;
    if (!s_logged) {
        s_logged = TRUE;
        for (e = 0; e < 2; e++)
            LogLine("XRBridge: eye=%d offset=%.4fm (%.3f world units) projRaw l=%.4f r=%.4f t=%.4f b=%.4f centre=%.5f",
                    e, g_realHalfIPD[e] / WORLD_UNITS_PER_METER, g_realHalfIPD[e], g_realProjRaw[e][0],
                    g_realProjRaw[e][1], g_realProjRaw[e][2], g_realProjRaw[e][3], g_realShearK[e]);
        LogLine("XRBridge: g_vrGeomValid = TRUE from the OpenXR views (HMD vertical FOV %.1f deg)",
                g_hmdFovyRad * 57.29578f);
    }
}

/* the angles eye e of the frame being drawn now covers (see the file header); 0 = not known, keep the headset's */
static BOOL XRB_DrawnFov(int e, HFov *out)
{
    float xHalf, yHalf, k;
    if (!g_submitBoundsEnabled || !g_projXScaleValid || g_projXScale <= 0.0f || g_projYScale <= 0.0f) return FALSE;
    xHalf = 1.0f / g_projXScale;
    yHalf = 1.0f / g_projYScale;
    k = g_realShearValid[e] ? g_realShearK[e]
                            : (-g_realHalfIPD[e]) / ((g_focusDistance > 1.0f) ? g_focusDistance : 1.0f);
    out->left = atanf(k - xHalf);
    out->right = atanf(k + xHalf);
    out->up = atanf(yHalf);
    out->down = -atanf(yHalf);
    return TRUE;
}

/* ---- the bridge entry points (px_04 hands over to these when g_vrUseXR) ----------------------------------------- */
static void XRBridge_Init(UINT w, UINT h)
{
    hxr_start();
    g_vrBufWidth = w;
    g_vrBufHeight = h;
    g_vrBridgeReady = TRUE;   /* the readback waits for hxr_live(); nothing here can fail the game */
    LogLine("XRBridge_Init: OpenXR headset thread asked to start (%ux%u per eye); PSYVR_SUBMIT_BOUNDS=%d -> %s",
            w, h, g_submitBoundsEnabled ? 1 : 0,
            g_submitBoundsEnabled ? "each eye goes out with the angles it was drawn with"
                                  : "each eye goes out with the headset's own angles");
}

static void XRBridge_ReleaseReadback(void)
{
    int s, e;
    for (s = 0; s < 2; s++) {
        for (e = 0; e < 2; e++)
            if (g_xrRb.mem[s][e]) { g_xrRb.mem[s][e]->lpVtbl->Release(g_xrRb.mem[s][e]); g_xrRb.mem[s][e] = NULL; }
        g_xrRb.pending[s] = FALSE;
    }
    g_xrRb.w = g_xrRb.h = 0;
}

static void XRBridge_LogStats(void)
{
    static DWORD s_last = 0;
    DWORD now = GetTickCount();
    HxrStats st;
    if (s_last != 0 && (DWORD)(now - s_last) < XRB_STATS_EVERY_MS) return;
    s_last = now;
    hxr_stats(&st);
    LogLine("XRBridge: headset %s | frames submitted %ld, empty %ld, pictures uploaded %ld, with own pose %ld | "
            "readback handed %u, not ready %u, failed %u | controllers found L %ld R %ld, aim L %ld R %ld | "
            "poses %ld", st.stage, st.submitted, st.empty, st.uploads, st.own_pose, g_xrRb.handed,
            g_xrRb.not_ready, g_xrRb.failed, st.hands_located[0], st.hands_located[1], st.aims_located[0],
            st.aims_located[1], pose_count());
}

/* after each real Present (where OpenVR's WaitGetPoses ran): the newest head pose drives the camera. Never waits. */
static void XRBridge_PumpPoses(void)
{
    HrPose p;
    XRBridge_LogStats();
    if (!pose_latest(&p)) {
        g_xrPoseApplied.valid = 0;
        VRBridge_UpdateHeadTracking(NULL);
        return;
    }
    XRB_UpdateGeometry(&p);
    {
        HmdMatrix34_t m;
        XRB_PoseTo34(&p, &m);
        VRBridge_UpdateHeadTracking(&m);
    }
    g_xrPoseApplied = p;
    jl_camera_q(p.head_q.x, p.head_q.y, p.head_q.z, p.head_q.w, p.head_p.x, p.head_p.y, p.head_p.z);
}

/* both eyes finished (CandB AfterBoth): queue this frame's readback, hand over last frame's if it has landed */
static void XRBridge_OnFrameComposited(void)
{
    D3DSURFACE_DESC desc;
    unsigned cur, prev;
    int e;
    HRESULT hr;

    if (!g_pEye1Surf || !g_pEye2Surf || !hxr_live()) return;
    if (FAILED(g_pEye1Surf->lpVtbl->GetDesc(g_pEye1Surf, &desc))) return;
    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8) {
        static BOOL s_said = FALSE;   /* the handover and the headset swapchain take 8-bit BGRA only */
        if (!s_said) { s_said = TRUE; LogLine("XRBridge: eye surface format %d is not 8-bit BGRA - nothing sent", (int)desc.Format); }
        return;
    }
    if (desc.Width != g_xrRb.w || desc.Height != g_xrRb.h || desc.Format != g_xrRb.fmt) {
        int s;
        XRBridge_ReleaseReadback();
        for (s = 0; s < 2; s++)
            for (e = 0; e < 2; e++) {
                hr = g_pDevice->lpVtbl->CreateOffscreenPlainSurface(g_pDevice, desc.Width, desc.Height, desc.Format,
                                                                     D3DPOOL_SYSTEMMEM, &g_xrRb.mem[s][e], NULL);
                if (FAILED(hr)) {
                    LogLine("XRBridge: readback surface %ux%u format %d failed hr=0x%08lX", desc.Width, desc.Height,
                            (int)desc.Format, (unsigned long)hr);
                    XRBridge_ReleaseReadback();
                    return;
                }
            }
        g_xrRb.w = desc.Width; g_xrRb.h = desc.Height; g_xrRb.fmt = desc.Format;
        LogLine("XRBridge: readback ring made, 2 x 2 eyes of %ux%u format %d", desc.Width, desc.Height, (int)desc.Format);
    }

    cur = g_xrRb.frame % 2u;
    prev = cur ^ 1u;
    g_xrRb.frame++;

    /* this frame: copy both eyes to memory (queued; locked a frame later) */
    if (SUCCEEDED(g_pDevice->lpVtbl->GetRenderTargetData(g_pDevice, g_pEye1Surf, g_xrRb.mem[cur][0])) &&
        SUCCEEDED(g_pDevice->lpVtbl->GetRenderTargetData(g_pDevice, g_pEye2Surf, g_xrRb.mem[cur][1]))) {
        g_xrRb.pending[cur] = TRUE;
        g_xrRb.pose[cur] = g_xrPoseApplied;
        for (e = 0; e < 2; e++) {
            HFov f;
            if (g_xrRb.pose[cur].valid && XRB_DrawnFov(e, &f)) g_xrRb.pose[cur].eye[e].fov = f;
        }
    } else {
        g_xrRb.pending[cur] = FALSE;
        g_xrRb.failed++;
    }

    /* last frame: hand it over if both copies have landed */
    if (g_xrRb.pending[prev]) {
        D3DLOCKED_RECT lr[2];
        BOOL locked[2] = { FALSE, FALSE };
        for (e = 0; e < 2; e++)
            locked[e] = g_xrRb.mem[prev][e]->lpVtbl->LockRect(g_xrRb.mem[prev][e], &lr[e], NULL,
                                                              D3DLOCK_READONLY | D3DLOCK_DONOTWAIT) == S_OK;
        if (locked[0] && locked[1]) {
            HxrFrame *f = hxr_frame_begin_write(g_xrRb.w, g_xrRb.h, (uint32_t)g_xrRb.fmt);
            if (f) {
                UINT y;
                for (e = 0; e < 2; e++)
                    for (y = 0; y < g_xrRb.h; y++)
                        memcpy(f->eye[e] + (size_t)y * g_xrRb.w * 4u, (BYTE *)lr[e].pBits + (size_t)y * lr[e].Pitch,
                               (size_t)g_xrRb.w * 4u);
                f->pose = g_xrRb.pose[prev];
                hxr_frame_publish(f);
                g_xrRb.handed++;
            }
            g_xrRb.pending[prev] = FALSE;
        } else {
            g_xrRb.not_ready++;
        }
        for (e = 0; e < 2; e++)
            if (locked[e]) g_xrRb.mem[prev][e]->lpVtbl->UnlockRect(g_xrRb.mem[prev][e]);
    }
}

/* the headset thread has no stop; it ends with the process (as in Hard Reset) */
static void XRBridge_Shutdown(void)
{
    XRBridge_ReleaseReadback();
    g_xrPoseApplied.valid = 0;
}
