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

static LONG WINAPI CrashDiag(EXCEPTION_POINTERS *ep)
{
    printf("\n!!! VECTORED EXCEPTION: code=0x%08lX at address=%p !!!\n",
           ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        printf("    access type=%lu (0=read,1=write) target address=0x%p\n",
               (unsigned long)ep->ExceptionRecord->ExceptionInformation[0],
               (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    }
#if defined(__i386__)
    printf("    EIP=%p ESP=%p EBP=%p EAX=%p ECX=%p\n",
           (void*)ep->ContextRecord->Eip, (void*)ep->ContextRecord->Esp,
           (void*)ep->ContextRecord->Ebp, (void*)ep->ContextRecord->Eax,
           (void*)ep->ContextRecord->Ecx);
#endif
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* unbuffered - so output survives if VR_InitInternal2 crashes */
    AddVectoredExceptionHandler(1, CrashDiag);
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

            /* Diagnostic: dump the raw first few pointer-sized slots of the FnTable and query
             * their containing module, to check whether sysPtr really points at an array of
             * valid code pointers before calling through slot 0. */
            void **slots = (void **)sysPtr;
            for (int i = 0; i < 6; i++) {
                HMODULE hMod = NULL;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)slots[i], &hMod);
                char modName[MAX_PATH] = "?";
                if (hMod) GetModuleFileNameA(hMod, modName, MAX_PATH);
                printf("  slot[%d] = %p  (module: %s)\n", i, slots[i], hMod ? modName : "(none - not in any loaded module)");
            }

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(slots[0], &mbi, sizeof(mbi))) {
                printf("  VirtualQuery(slot[0]): BaseAddress=%p RegionSize=0x%zx State=0x%lx Protect=0x%lx AllocProtect=0x%lx\n",
                       mbi.BaseAddress, mbi.RegionSize, mbi.State, mbi.Protect, mbi.AllocationProtect);
            } else {
                printf("  VirtualQuery(slot[0]) FAILED, err=%lu\n", GetLastError());
            }
            /* Dump the first 16 raw bytes at slot[0] - if it's real x86 code it should look like a
             * plausible prologue (push ebp / mov / sub esp, etc). If it's all zero or garbage, the
             * pointer itself is wrong despite being "in range" of vrclient.dll. */
            unsigned char *codeBytes = (unsigned char *)slots[0];
            printf("  bytes @ slot[0]:");
            for (int i = 0; i < 16; i++) printf(" %02X", codeBytes[i]);
            printf("\n");

            /* HYPOTHESIS: sysPtr is a real C++ object (this-ptr to a vtable), not a flat FnTable -
             * slot[0]'s bytes decoded as more in-range pointers (a vtable stored in .rdata), and
             * VirtualQuery confirms slot[0] itself sits on a PAGE_READONLY (non-executable) page,
             * i.e. it cannot be code. Test: treat sysPtr as this-ptr, *(void***)sysPtr as the real
             * vtable, and dispatch slot 0 of THAT via __thiscall (openvr.h's IVRSystem methods are
             * plain unattributed C++ virtuals => default MSVC-ABI __thiscall on x86: this in ECX). */
            void **vtbl = *(void ***)sysPtr;
            printf("  (vtable hypothesis) *(void***)sysPtr = %p\n", (void*)vtbl);
            if (vtbl) {
                MEMORY_BASIC_INFORMATION mbi2;
                if (VirtualQuery(vtbl[0], &mbi2, sizeof(mbi2))) {
                    printf("  VirtualQuery(vtbl[0]=%p): Protect=0x%lx AllocProtect=0x%lx\n",
                           vtbl[0], mbi2.Protect, mbi2.AllocationProtect);
                }
                typedef void (__thiscall *GetRecommendedRenderTargetSize_thiscall_t)(void *pThis, uint32_t *pW, uint32_t *pH);
                GetRecommendedRenderTargetSize_thiscall_t fn = (GetRecommendedRenderTargetSize_thiscall_t)vtbl[0];
                printf("Calling vtbl[0] via __thiscall with this=sysPtr now...\n");
                fflush(stdout);
                fn(sysPtr, &w, &h);
                printf("GetRecommendedRenderTargetSize (thiscall vtable) -> %u x %u per eye\n", w, h);
            }
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
