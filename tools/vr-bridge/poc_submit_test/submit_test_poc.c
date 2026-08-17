/*
 * End-to-end VR bridge proof-of-concept: D3D9Ex render -> shared D3D11 surface -> OpenVR
 * IVRCompositor::Submit(), against SteamVR's null driver (no physical headset). This is the
 * "does the whole bridge concept actually work" milestone for the project (see
 * notes/27-null-driver-openvr-init-and-compositor-submit.md for full writeup).
 *
 * Combines two previously-separate, independently-proven POCs:
 *   - tools/vr-bridge/poc_shared_surface: D3D9Ex shared render target, readable live from a
 *     separate D3D11 device (proven working, notes/25).
 *   - tools/vr-bridge/poc_openvr_init: vendored OpenVR SDK links and calls into openvr_api.dll
 *     (proven working against SteamVR absent, notes/25; now updated for SteamVR PRESENT, see
 *     below for a real ABI finding made this session).
 *
 * IMPORTANT REAL FINDING (this session, not assumed): openvr_capi.h's "flat FnTable" struct
 * definitions (struct VR_IVRSystem_FnTable, VR_IVRCompositor_FnTable, etc.) do NOT describe what
 * this installed SteamVR build's VR_GetGenericInterface() actually returns. Empirically (via a
 * vectored exception handler + VirtualQuery, see notes/27 for the full diagnostic trail).
 * VR_GetGenericInterface() returns a genuine C++ object pointer (this-ptr into vrclient.dll) --
 * calling through it as a flat struct-of-function-pointers reads the vtable POINTER itself as if
 * it were function pointer slot 0, and jumps into a non-executable .rdata page (confirmed via
 * VirtualQuery: Protect=PAGE_READONLY at the crash address). The fix, confirmed working: treat
 * the returned pointer as a real C++ this-ptr, dereference *(void***)ptr to get the real vtable,
 * and dispatch through THAT using __thiscall (openvr.h's C++ interfaces use unattributed virtual
 * methods => default MSVC x86 ABI => this-ptr in ECX, args on stack, callee-cleans). This same
 * technique is used here for both IVRSystem (informational) and IVRCompositor (load-bearing).
 *
 * Build: see build.ps1. Run directly - no game process involved. Requires the null driver to be
 * enabled (notes/25 section 5 / notes/27 section 1) so SteamVR can run without physical hardware.
 */

#define INITGUID
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../openvr-sdk/headers/openvr_capi.h"
/* Do NOT include <stdbool.h> - see poc_openvr_init/openvr_init_poc.c's header comment for why
 * (openvr_capi.h typedefs its own bool as char on Windows, collides with stdbool.h). */

#define TEX_W 64
#define TEX_H 64

/* ---- OpenVR global entry points (declared directly, not via openvr_capi.h's dead #if 0 block -
 * see poc_openvr_init for the two documented header quirks this works around). ---- */
extern bool VR_IsRuntimeInstalled(void);
extern bool VR_IsHmdPresent(void);
extern uint32_t VR_InitInternal2(EVRInitError *peError, EVRApplicationType eApplicationType, const char *pStartupInfo);
extern void VR_ShutdownInternal(void);
extern void *VR_GetGenericInterface(const char *pchInterfaceVersion, EVRInitError *peError);
extern const char *VR_GetVRInitErrorAsEnglishDescription(EVRInitError error);

#define CHECK_HR(hr, msg) \
    do { \
        if (FAILED(hr)) { \
            printf("FAIL: %s (hr=0x%08lX)\n", msg, (unsigned long)(hr)); \
            return 1; \
        } \
    } while (0)

static HRESULT flush_d3d9(IDirect3DDevice9Ex *pDevice)
{
    IDirect3DQuery9 *pQuery = NULL;
    HRESULT hr = pDevice->lpVtbl->CreateQuery(pDevice, D3DQUERYTYPE_EVENT, &pQuery);
    if (FAILED(hr)) return hr;
    hr = pQuery->lpVtbl->Issue(pQuery, D3DISSUE_END);
    if (FAILED(hr)) { pQuery->lpVtbl->Release(pQuery); return hr; }
    DWORD start = GetTickCount();
    for (;;) {
        hr = pQuery->lpVtbl->GetData(pQuery, NULL, 0, D3DGETDATA_FLUSH);
        if (hr == S_OK) break;
        if (hr != S_FALSE) { pQuery->lpVtbl->Release(pQuery); return hr; }
        if (GetTickCount() - start > 5000) { pQuery->lpVtbl->Release(pQuery); return E_FAIL; }
        Sleep(1);
    }
    pQuery->lpVtbl->Release(pQuery);
    return S_OK;
}

static HRESULT clear_to_color(IDirect3DDevice9Ex *pDevice, IDirect3DSurface9 *pSurf, D3DCOLOR color)
{
    HRESULT hr = pDevice->lpVtbl->SetRenderTarget(pDevice, 0, pSurf);
    if (FAILED(hr)) return hr;
    hr = pDevice->lpVtbl->BeginScene(pDevice);
    if (FAILED(hr)) return hr;
    hr = pDevice->lpVtbl->Clear(pDevice, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0);
    pDevice->lpVtbl->EndScene(pDevice);
    if (FAILED(hr)) return hr;
    return flush_d3d9(pDevice);
}

/* ---- Real-C++-vtable dispatch helper (the confirmed-working fix - see file header). ---- */
static void *real_vtable(void *thisPtr)
{
    return *(void **)thisPtr; /* first 4 bytes of any polymorphic MSVC C++ object = vtable ptr */
}

/* IVRCompositor vtable slot indices, from openvr.h's class IVRCompositor method declaration
 * order (0-based): 0 SetTrackingSpace, 1 GetTrackingSpace, 2 WaitGetPoses, 3 GetLastPoses,
 * 4 GetLastPoseForTrackedDeviceIndex, 5 GetSubmitTexture, 6 Submit. */
typedef EVRCompositorError (__thiscall *WaitGetPoses_t)(void *pThis,
    TrackedDevicePose_t *pRenderPoseArray, uint32_t unRenderPoseArrayCount,
    TrackedDevicePose_t *pGamePoseArray, uint32_t unGamePoseArrayCount);
typedef EVRCompositorError (__thiscall *Submit_t)(void *pThis,
    EVREye eEye, const Texture_t *pTexture, const VRTextureBounds_t *pBounds, EVRSubmitFlags nSubmitFlags);

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== VR bridge end-to-end POC: D3D9Ex -> shared D3D11 -> IVRCompositor::Submit ===\n\n");

    /* ---------- Part A: D3D9Ex shared surface (proven mechanism, poc_shared_surface) ---------- */
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "PsyVR_SubmitTestPOC";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "PsyVR submit-test POC",
                                 WS_OVERLAPPEDWINDOW, 0, 0, TEX_W, TEX_H, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { printf("FAIL: CreateWindowExA\n"); return 1; }

    IDirect3D9Ex *pD3D9Ex = NULL;
    HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D9Ex);
    CHECK_HR(hr, "Direct3DCreate9Ex");

    LUID luid9;
    hr = pD3D9Ex->lpVtbl->GetAdapterLUID(pD3D9Ex, D3DADAPTER_DEFAULT, &luid9);
    CHECK_HR(hr, "IDirect3D9Ex::GetAdapterLUID");

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = TEX_W;
    pp.BackBufferHeight = TEX_H;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    IDirect3DDevice9Ex *pDevice = NULL;
    hr = pD3D9Ex->lpVtbl->CreateDeviceEx(pD3D9Ex, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
        &pp, NULL, &pDevice);
    CHECK_HR(hr, "IDirect3D9Ex::CreateDeviceEx");
    printf("[A1] D3D9Ex device created\n");

    IDirect3DTexture9 *pTex9 = NULL;
    HANDLE sharedHandle = NULL;
    hr = pDevice->lpVtbl->CreateTexture(pDevice, TEX_W, TEX_H, 1, D3DUSAGE_RENDERTARGET,
                                         D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pTex9, &sharedHandle);
    CHECK_HR(hr, "CreateTexture (shared)");
    IDirect3DSurface9 *pSurf9 = NULL;
    hr = pTex9->lpVtbl->GetSurfaceLevel(pTex9, 0, &pSurf9);
    CHECK_HR(hr, "GetSurfaceLevel");

    /* Render a distinctive solid color as the "test frame" content - not game content, per this
     * session's explicit scope, just proof the bridge carries real pixel data through to Submit. */
    D3DCOLOR testColor = D3DCOLOR_ARGB(255, 40, 160, 220); /* a distinct sky-blue */
    hr = clear_to_color(pDevice, pSurf9, testColor);
    CHECK_HR(hr, "clear_to_color (test frame)");
    printf("[A2] D3D9Ex shared surface cleared to test color (R=40,G=160,B=220) and flushed\n\n");

    IDXGIFactory1 *pFactory = NULL;
    hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&pFactory);
    CHECK_HR(hr, "CreateDXGIFactory1");
    IDXGIAdapter1 *pChosenAdapter = NULL;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1 *pAdapter = NULL;
        hr = pFactory->lpVtbl->EnumAdapters1(pFactory, i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->lpVtbl->GetDesc1(pAdapter, &desc);
        if (desc.AdapterLuid.LowPart == luid9.LowPart && desc.AdapterLuid.HighPart == luid9.HighPart) {
            pChosenAdapter = pAdapter;
            wprintf(L"[B1] Matched D3D11 adapter to D3D9Ex's LUID: %s\n", desc.Description);
            break;
        }
        pAdapter->lpVtbl->Release(pAdapter);
    }
    if (!pChosenAdapter) { printf("FAIL: no DXGI adapter matched D3D9Ex LUID\n"); return 1; }

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL gotLevel;
    ID3D11Device *pDevice11 = NULL;
    ID3D11DeviceContext *pContext11 = NULL;
    hr = D3D11CreateDevice((IDXGIAdapter *)pChosenAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
                            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                            D3D11_SDK_VERSION, &pDevice11, &gotLevel, &pContext11);
    CHECK_HR(hr, "D3D11CreateDevice");
    printf("[B2] D3D11CreateDevice OK, feature level=0x%X\n", gotLevel);

    ID3D11Texture2D *pTex11 = NULL;
    hr = pDevice11->lpVtbl->OpenSharedResource(pDevice11, sharedHandle, &IID_ID3D11Texture2D, (void **)&pTex11);
    CHECK_HR(hr, "ID3D11Device::OpenSharedResource");
    printf("[B3] OpenSharedResource OK - D3D11 texture ready to hand to OpenVR\n\n");

    /* ---------- Part C: OpenVR init + IVRCompositor::Submit ---------- */
    printf("VR_IsRuntimeInstalled() = %s\n", VR_IsRuntimeInstalled() ? "true" : "false");
    printf("VR_IsHmdPresent()       = %s\n\n", VR_IsHmdPresent() ? "true" : "false");

    EVRInitError err = EVRInitError_VRInitError_None;
    uint32_t token = VR_InitInternal2(&err, EVRApplicationType_VRApplication_Scene, NULL);
    printf("[C1] VR_InitInternal2(VRApplication_Scene) -> token=%u, error=%d (%s)\n",
           token, (int)err, VR_GetVRInitErrorAsEnglishDescription(err));

    if (!(err == EVRInitError_VRInitError_None && token != 0)) {
        printf("\nFAIL: OpenVR init did not succeed - cannot proceed to Submit.\n");
        return 1;
    }

    EVRInitError compErr = EVRInitError_VRInitError_None;
    void *compPtr = VR_GetGenericInterface(IVRCompositor_Version, &compErr);
    printf("[C2] VR_GetGenericInterface(%s) -> ptr=%p, error=%d\n", IVRCompositor_Version, compPtr, (int)compErr);
    if (!compPtr) {
        printf("\nFAIL: could not get IVRCompositor interface.\n");
        VR_ShutdownInternal();
        return 1;
    }

    void **vtbl = (void **)real_vtable(compPtr);
    printf("[C3] IVRCompositor real vtable = %p (dispatching via __thiscall, this-ptr fix - see file header)\n\n", (void*)vtbl);

    /* WaitGetPoses must be called once per frame before Submit (standard OpenVR usage pattern -
     * establishes frame timing on the compositor side; skipping it risks AlreadySubmitted/timing
     * errors on some builds even though this is our very first frame). */
    TrackedDevicePose_t renderPoses[64];
    TrackedDevicePose_t gamePoses[64];
    ZeroMemory(renderPoses, sizeof(renderPoses));
    ZeroMemory(gamePoses, sizeof(gamePoses));
    WaitGetPoses_t pWaitGetPoses = (WaitGetPoses_t)vtbl[2];
    EVRCompositorError wgpErr = pWaitGetPoses(compPtr, renderPoses, 64, gamePoses, 64);
    printf("[C4] WaitGetPoses -> error=%d\n", (int)wgpErr);

    Texture_t tex;
    tex.handle = (void *)pTex11;
    tex.eType = ETextureType_TextureType_DirectX;
    tex.eColorSpace = EColorSpace_ColorSpace_Auto;

    Submit_t pSubmit = (Submit_t)vtbl[6];
    EVRCompositorError submitErrL = pSubmit(compPtr, EVREye_Eye_Left, &tex, NULL, EVRSubmitFlags_Submit_Default);
    printf("[C5] Submit(Eye_Left)  -> error=%d %s\n", (int)submitErrL,
           submitErrL == EVRCompositorError_VRCompositorError_None ? "(VRCompositorError_None)" : "");
    EVRCompositorError submitErrR = pSubmit(compPtr, EVREye_Eye_Right, &tex, NULL, EVRSubmitFlags_Submit_Default);
    printf("[C6] Submit(Eye_Right) -> error=%d %s\n\n", (int)submitErrR,
           submitErrR == EVRCompositorError_VRCompositorError_None ? "(VRCompositorError_None)" : "");

    int pass = (submitErrL == EVRCompositorError_VRCompositorError_None &&
                submitErrR == EVRCompositorError_VRCompositorError_None);
    printf("=== RESULT: %s ===\n", pass
           ? "PASS - IVRCompositor::Submit accepted the D3D9Ex-bridged D3D11 texture for both eyes"
           : "FAIL - see error codes above");

    VR_ShutdownInternal();
    printf("VR_ShutdownInternal() called.\n");

    pTex11->lpVtbl->Release(pTex11);
    pContext11->lpVtbl->Release(pContext11);
    pDevice11->lpVtbl->Release(pDevice11);
    pChosenAdapter->lpVtbl->Release(pChosenAdapter);
    pFactory->lpVtbl->Release(pFactory);
    pSurf9->lpVtbl->Release(pSurf9);
    pTex9->lpVtbl->Release(pTex9);
    pDevice->lpVtbl->Release(pDevice);
    pD3D9Ex->lpVtbl->Release(pD3D9Ex);
    DestroyWindow(hwnd);

    return pass ? 0 : 1;
}
