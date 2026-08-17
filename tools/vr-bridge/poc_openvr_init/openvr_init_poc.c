/*
 * OpenVR SDK header/import-lib smoke test.
 *
 * Purpose: prove the vendored OpenVR SDK (tools/vr-bridge/openvr-sdk/, headers + win32 import
 * lib + DLL only, see VENDORED.md there) actually compiles and links against this project's
 * existing 32-bit mingw toolchain, and that a real call into openvr_api.dll's VR_InitInternal
 * reaches the runtime and returns a real, meaningful error code - all WITHOUT SteamVR installed.
 * This is intentionally the cheapest possible test of the OpenVR side of the bridge, run before
 * SteamVR itself is available on this machine (see notes/25 for the full picture).
 *
 * Uses the enum/struct definitions from the flat C API (openvr_capi.h) but declares the global
 * entry-point functions itself, matching this project's existing plain-C style
 * (tools/proxy-d3d9/proxy_d3d9.c). Two real header quirks made this necessary, both confirmed
 * live against this exact vendored copy, not assumed:
 *   1. openvr_capi.h's own prototypes for VR_InitInternal/VR_ShutdownInternal/etc. are wrapped in
 *      `#if 0 ... #endif` in the shipped header (dead code, never compiled either way).
 *   2. The still-live, current entry point (confirmed via openvr.h's C++ wrapper, which the
 *      actual openvr_api.dll we vendored implements) is VR_InitInternal2 - taking an extra
 *      `pStartupInfo` string argument and returning a plain uint32_t token - not the older
 *      VR_InitInternal the (dead) C API code above still shows.
 * All the enum/struct/interface-version definitions from openvr_capi.h are used unmodified.
 *
 * EXPECTED RESULT ON THIS MACHINE RIGHT NOW (SteamVR not installed, per notes/25):
 *   VR_InitInternal2 returns error=100 (VRInitError_Init_InstallationNotFound) or 110
 *   (VRInitError_Init_PathRegistryNotFound). That is a PASS for this test's actual purpose (the
 *   header/link path works and the runtime call happens) - it is not expected to fully succeed
 *   until SteamVR is installed. Once it is, rerun this exact .exe (no rebuild needed) and expect
 *   error=0 (VRInitError_None).
 *
 * Build: see build.ps1 in this directory. Requires openvr_api.dll next to the .exe at runtime
 * (build.ps1 copies it from ../openvr-sdk/bin/win32/ automatically).
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../openvr-sdk/headers/openvr_capi.h"
/* NOTE: do NOT also include <stdbool.h> - openvr_capi.h typedefs its own `bool` as `char` on
 * Windows (its `#if defined(__WIN32)` branch, which mingw-w64 does define), which conflicts with
 * stdbool.h's `#define bool _Bool` if both are present. Use 0/1 for its bool type below. */

/* Real, live entry points exported by openvr_api.dll - see the file-header comment above for why
 * these are declared here instead of relying on openvr_capi.h's own (dead, outdated) copies.
 * Default Win32 calling convention (__cdecl) matches openvr.h's VR_CALLTYPE for these globals. */
extern bool VR_IsRuntimeInstalled(void);
extern bool VR_IsHmdPresent(void);
extern uint32_t VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo);
extern void VR_ShutdownInternal(void);
extern void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError);
extern const char *VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

int main(void)
{
    printf("=== OpenVR SDK header/link smoke test (VR_InitInternal2) ===\n\n");

    printf("VR_IsRuntimeInstalled() = %s\n", VR_IsRuntimeInstalled() ? "true" : "false");
    printf("VR_IsHmdPresent()       = %s\n\n", VR_IsHmdPresent() ? "true" : "false"); /* both return openvr's char-typedef'd bool; ?: works fine on any nonzero/zero value */

    EVRInitError err = EVRInitError_VRInitError_None;
    uint32_t token = VR_InitInternal2(&err, EVRApplicationType_VRApplication_Scene, NULL);

    printf("VR_InitInternal2(VRApplication_Scene) -> token=%u, error=%d (%s)\n",
           token, (int)err, VR_GetVRInitErrorAsEnglishDescription(err));

    if (err == EVRInitError_VRInitError_None && token != 0) {
        printf("\nSteamVR IS installed and running - full init succeeded.\n\n");

        EVRInitError sysErr = EVRInitError_VRInitError_None;
        void *sysPtr = VR_GetGenericInterface(IVRSystem_Version, &sysErr);
        printf("VR_GetGenericInterface(%s) -> ptr=%p, error=%d\n", IVRSystem_Version, sysPtr, (int)sysErr);

        if (sysPtr) {
            struct VR_IVRSystem_FnTable *pSystem = (struct VR_IVRSystem_FnTable *)sysPtr;
            uint32_t w = 0, h = 0;
            pSystem->GetRecommendedRenderTargetSize(&w, &h);
            printf("GetRecommendedRenderTargetSize -> %u x %u per eye\n", w, h);
        }

        VR_ShutdownInternal();
        printf("\nVR_ShutdownInternal() called.\n");
    } else {
        printf("\nThis is the EXPECTED result on this machine right now: SteamVR is not installed\n");
        printf("(see notes/25). This still PROVES the vendored OpenVR SDK headers/import-lib link\n");
        printf("and correctly call into openvr_api.dll at runtime - once SteamVR + the null driver\n");
        printf("are set up, rerun this exact .exe (no rebuild needed) and expect error=0.\n");
    }

    return 0;
}
