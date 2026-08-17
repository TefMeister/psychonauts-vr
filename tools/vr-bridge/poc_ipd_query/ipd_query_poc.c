/*
 * Real OpenVR per-eye geometry query POC (session 32, Task 1).
 *
 * Purpose: query REAL values from the already-proven-working null-driver OpenVR runtime
 * (notes/27) for the four properties notes/24's off-axis stereo correction currently
 * estimates/hardcodes:
 *   1. IVRSystem::GetFloatTrackedDeviceProperty(Hmd, Prop_UserIpdMeters_Float) - real IPD.
 *   2. IVRSystem::GetEyeToHeadTransform(eye) - real per-eye offset from head origin (may be
 *      asymmetric, not just +/-half-IPD).
 *   3. IVRSystem::GetProjectionRaw(eye, &l,&r,&t,&b) - real per-eye off-axis frustum tangents.
 *   4. IVRSystem::GetRecommendedRenderTargetSize(&w,&h) - real recommended eye-buffer size.
 *
 * Deliberately standalone (no game, no proxy_d3d9.c changes) - proves the real numbers this
 * session's proxy_d3d9.c integration will consume, before touching the live-tested VR bridge
 * code, per this project's established practice (see notes/25/27/28's own standalone-POC-first
 * approach). Reuses the notes/27 vtable-dereference + __thiscall dispatch fix (this SteamVR
 * build's VR_GetGenericInterface returns a real C++ this-ptr, not the flat FnTable struct
 * openvr_capi.h documents) verbatim - already proven for IVRSystem::GetRecommendedRenderTargetSize
 * in notes/27's own openvr_init_poc.c.
 *
 * One new ABI wrinkle handled here, not needed by any prior POC: GetEyeToHeadTransform returns
 * HmdMatrix34_t (48 bytes) BY VALUE. The MSVC x86 ABI (which this SteamVR build's vrclient.dll
 * was compiled with) passes a hidden pointer to caller-allocated return storage as the FIRST
 * explicit parameter (immediately after `this`) for any large-struct-returning method - NOT
 * something a naive `struct HmdMatrix34_t (__thiscall*)(void*, EVREye)` function-pointer
 * declaration can be trusted to reproduce correctly across compilers. Declared explicitly here as
 * `void (__thiscall*)(void *pThis, HmdMatrix34_t *pOut, EVREye eEye)` instead - manually
 * implementing the same ABI contract rather than relying on the compiler's own (unverified, for
 * this cross-compiler this-> MSVC-callee case) struct-return code generation. GetProjectionRaw and
 * GetFloatTrackedDeviceProperty both avoid this issue entirely (out-params / plain float return),
 * which is part of why the task specifically calls out GetProjectionRaw over GetProjectionMatrix.
 *
 * Build: see build.ps1 in this directory (same LLVM-MinGW i686 toolchain as every other POC here).
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../openvr-sdk/headers/openvr_capi.h"
/* Do NOT include <stdbool.h> - see every other POC's header comment in this project for why. */

extern bool VR_IsRuntimeInstalled(void);
extern bool VR_IsHmdPresent(void);
extern uint32_t VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo);
extern void VR_ShutdownInternal(void);
extern void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError);
extern const char *VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

static void *RealVtable(void *thisPtr) { return *(void **)thisPtr; }

typedef void (__thiscall *GetRecommendedRenderTargetSize_t)(void *pThis, uint32_t *pW, uint32_t *pH);
typedef void (__thiscall *GetProjectionRaw_t)(void *pThis, EVREye eEye, float *pL, float *pR, float *pT, float *pB);
typedef void (__thiscall *GetEyeToHeadTransform_t)(void *pThis, struct HmdMatrix34_t *pOut, EVREye eEye);
typedef float (__thiscall *GetFloatTrackedDeviceProperty_t)(void *pThis, TrackedDeviceIndex_t unDeviceIndex, ETrackedDeviceProperty prop, ETrackedPropertyError *pError);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Real OpenVR per-eye geometry query (session 32) ===\n\n");

    printf("VR_IsRuntimeInstalled() = %s\n", VR_IsRuntimeInstalled() ? "true" : "false");
    printf("VR_IsHmdPresent()       = %s\n\n", VR_IsHmdPresent() ? "true" : "false");

    EVRInitError err = EVRInitError_VRInitError_None;
    uint32_t token = VR_InitInternal2(&err, EVRApplicationType_VRApplication_Scene, NULL);
    printf("VR_InitInternal2 -> token=%u error=%d (%s)\n", token, (int)err, VR_GetVRInitErrorAsEnglishDescription(err));
    if (!(err == EVRInitError_VRInitError_None && token != 0)) {
        printf("FAILED to init - is SteamVR/null-driver running? (notes/27 already confirmed this works)\n");
        return 1;
    }

    EVRInitError sysErr = EVRInitError_VRInitError_None;
    void *sysPtr = VR_GetGenericInterface(IVRSystem_Version, &sysErr);
    printf("VR_GetGenericInterface(%s) -> ptr=%p error=%d\n", IVRSystem_Version, sysPtr, (int)sysErr);
    if (!sysPtr) { VR_ShutdownInternal(); return 1; }

    void **vtbl = (void **)RealVtable(sysPtr);
    printf("IVRSystem real vtable = %p\n\n", (void *)vtbl);

    GetRecommendedRenderTargetSize_t pGetRT = (GetRecommendedRenderTargetSize_t)vtbl[0];
    GetProjectionRaw_t pGetProjRaw = (GetProjectionRaw_t)vtbl[2];
    GetEyeToHeadTransform_t pGetEyeToHead = (GetEyeToHeadTransform_t)vtbl[5];
    GetFloatTrackedDeviceProperty_t pGetFloatProp = (GetFloatTrackedDeviceProperty_t)vtbl[23];

    /* --- 1. Real recommended render target size --- */
    uint32_t w = 0, h = 0;
    pGetRT(sysPtr, &w, &h);
    printf("[1] GetRecommendedRenderTargetSize -> %u x %u per eye\n\n", w, h);

    /* --- 2. Real IPD --- */
    ETrackedPropertyError propErr = ETrackedPropertyError_TrackedProp_Success;
    float ipdMeters = pGetFloatProp(sysPtr, k_unTrackedDeviceIndex_Hmd, ETrackedDeviceProperty_Prop_UserIpdMeters_Float, &propErr);
    printf("[2] GetFloatTrackedDeviceProperty(Hmd, Prop_UserIpdMeters_Float) -> %.6f meters (err=%d)\n",
           ipdMeters, (int)propErr);
    printf("    = %.4f cm, half-IPD = %.4f cm\n\n", ipdMeters * 100.0f, ipdMeters * 50.0f);

    /* --- 3. Real per-eye head-to-eye transform --- */
    for (int e = 0; e < 2; e++) {
        EVREye eye = (e == 0) ? EVREye_Eye_Left : EVREye_Eye_Right;
        struct HmdMatrix34_t m;
        ZeroMemory(&m, sizeof(m));
        pGetEyeToHead(sysPtr, &m, eye);
        printf("[3] GetEyeToHeadTransform(%s):\n", e == 0 ? "Left" : "Right");
        printf("    [ %8.5f %8.5f %8.5f %8.5f ]\n", m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3]);
        printf("    [ %8.5f %8.5f %8.5f %8.5f ]\n", m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3]);
        printf("    [ %8.5f %8.5f %8.5f %8.5f ]\n", m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3]);
        printf("    translation (x,y,z) = (%.6f, %.6f, %.6f) meters -> x = %.4f cm\n\n",
               m.m[0][3], m.m[1][3], m.m[2][3], m.m[0][3] * 100.0f);
    }

    /* --- 4. Real per-eye raw projection tangents --- */
    for (int e = 0; e < 2; e++) {
        EVREye eye = (e == 0) ? EVREye_Eye_Left : EVREye_Eye_Right;
        float l = 0, r = 0, t = 0, b = 0;
        pGetProjRaw(sysPtr, eye, &l, &r, &t, &b);
        printf("[4] GetProjectionRaw(%s) -> left=%.6f right=%.6f top=%.6f bottom=%.6f\n",
               e == 0 ? "Left" : "Right", l, r, t, b);
        printf("    center-offset (l+r)/2 = %.6f  (0 = symmetric/no real off-axis data)\n\n", (l + r) * 0.5f);
    }

    VR_ShutdownInternal();
    printf("VR_ShutdownInternal() called. Done.\n");
    return 0;
}
