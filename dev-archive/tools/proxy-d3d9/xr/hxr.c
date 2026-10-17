/* hxr.c (Psychonauts copy, 2026-10-17: from staging hard-reset-vr/proxy-d3d9/src at 9a4dbac; only the ini name,
 * app name and BeG0nE game name differ - keep the two in step) - the headset thread: OpenXR session on its own D3D11 device, fed from the CPU handover (hxr_frame.c).
 * 2026-10-07, /lm hard-reset-vr reader. Shape from Metro's mxr.c (headset-bridge-2026-10-07), simplified: no shared
 * textures, projection layers only, no head tracking (the game's camera does not follow the headset yet).
 *
 * THREADING: the game thread only calls hxr_start(), hxr_live() and hxr_stats(), and publishes frames through
 * hxr_frame; it never calls OpenXR and never waits for this thread. This thread is the only one that calls xr*.
 *
 * HEAD POSE (added later 2026-10-07): every headset frame the head (VIEW space) and both eye views are located in
 * LOCAL space and published (hr_pose.c) for the game's camera (hr_head.c). A picture drawn with a pose carries it
 * back (HxrFrame.pose); the layer is then submitted with THAT pose and field of view, so the runtime corrects only for
 * the time since it was drawn. Pictures without a pose go out with the views located for this frame, as before.
 *
 * OpenXR headers: Khronos, Apache-2.0 OR MIT (third_party/openxr, 1.0.10). The loader is loaded at run time from the
 * game folder, and only once PSYVR_OPENXR=1 is set (Psychonauts). */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdlib.h>
#include <string.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
/* 2026-10-11: BeG0nE Camera Jitter ride-along (copied from BeG0nE camera-jitter/ride-along). Writes only while
 * the headset session runs: P = head pose read here, C = pose the game camera is turned by, F = headset frame out. */
#define JITTERLOG_IMPLEMENTATION
#include "jitterlog.h"

#include "hr_common.h"
#include "hxr.h"
#include "hxr_format.h"
#include "hxr_frame.h"
#include "hr_pose.h"
#include "hxr_buttons.h"

/* ---- Settings ------------------------------------------------------------------------------------- */
#define HXR_INI_NAME        L"psyvr_xr.ini"
#define HXR_INI_SECTION     L"xr"
#define HXR_LOADER_NAME     L"openxr_loader.dll"
#define HXR_APP_NAME        "psychonauts-vr"
#define HXR_MAX_FORMATS     64u
#define HXR_IMAGE_WAIT_NS   100000000   /* 100 ms: longest wait for a swapchain image */
#define HXR_IDLE_SLEEP_MS   10
#define HXR_TP_W            1280u       /* test-pattern size while no game frame has arrived */
#define HXR_TP_H            720u
#define HXR_LOG_EVERY       900l        /* a "headset frames submitted" line every this many frames */

enum { ST_OFF = 0, ST_LOADING, ST_RUNNING, ST_FAILED };
static const char *const g_stage_names[] = { "off", "loading", "running", "failed" };
static volatile LONG g_stage, g_live, g_submitted, g_empty, g_uploads;
static int g_test_pattern;
static const float g_tp_colour[2][4] = { { 0.8f, 0.1f, 0.1f, 1.0f }, { 0.1f, 0.2f, 0.9f, 1.0f } };

static PFN_xrGetInstanceProcAddr p_gipa;
#define XR_FN(name) static PFN_##name p_##name
XR_FN(xrCreateInstance); XR_FN(xrGetSystem); XR_FN(xrCreateSession); XR_FN(xrCreateReferenceSpace);
XR_FN(xrPollEvent); XR_FN(xrBeginSession); XR_FN(xrEndSession); XR_FN(xrEnumerateSwapchainFormats);
XR_FN(xrCreateSwapchain); XR_FN(xrDestroySwapchain); XR_FN(xrEnumerateSwapchainImages); XR_FN(xrWaitFrame);
XR_FN(xrBeginFrame); XR_FN(xrEndFrame); XR_FN(xrAcquireSwapchainImage); XR_FN(xrWaitSwapchainImage);
XR_FN(xrReleaseSwapchainImage); XR_FN(xrGetD3D11GraphicsRequirementsKHR); XR_FN(xrLocateViews);
XR_FN(xrLocateSpace);
/* controllers (2026-10-16, for the hands): one grip-pose action per hand, bound for the common controllers */
XR_FN(xrStringToPath); XR_FN(xrCreateActionSet); XR_FN(xrCreateAction); XR_FN(xrSuggestInteractionProfileBindings);
XR_FN(xrAttachSessionActionSets); XR_FN(xrCreateActionSpace); XR_FN(xrSyncActions);

/* ---- headset-thread state (no other thread touches these) ----------------------------------------- */
typedef struct {
    XrSwapchain sc;
    XrSwapchainImageD3D11KHR *img;
    uint32_t count;
    uint32_t pending;        /* 1 + index of an image acquired but whose wait timed out; 0 = none */
} EyeChain;

static XrInstance g_inst;
static XrSystemId g_sys;
static XrSession g_sess;
static XrSpace g_space, g_view_space;
static XrActionSet g_actions;
static XrAction g_grip[2], g_aim[2];        /* grip (hands) and aim (hr_aim.c) poses per hand */
static XrSpace g_hand_space[2], g_aim_space[2];
static int g_hands_on;               /* the action set is attached: controllers are asked for every frame */
static volatile LONG g_hand_located[2], g_aim_located[2];
static HrPose g_held_pose;           /* the pose the held picture was drawn with (valid 0 = none) */
static volatile LONG g_frames_own_pose;
static LUID g_req_luid;
static D3D_FEATURE_LEVEL g_req_fl;
static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static int g_running;
static EyeChain g_chain[2];
static ID3D11Texture2D *g_held[2];   /* the newest picture per eye, copied into a swapchain image every headset frame */
static uint32_t g_w, g_h;
static int64_t g_fmt;
static int g_have_picture;
static long g_seen;

/* ---- start-up ------------------------------------------------------------------------------------- */
static int load_fn(XrInstance inst, const char *name, PFN_xrVoidFunction *out) {
    return XR_SUCCEEDED(p_gipa(inst, name, out)) && *out;
}
#define LOAD(inst, name) load_fn(inst, #name, (PFN_xrVoidFunction *)&p_##name)

static int load_all(XrInstance i) {
    return LOAD(i, xrGetSystem) && LOAD(i, xrCreateSession) && LOAD(i, xrCreateReferenceSpace) &&
           LOAD(i, xrPollEvent) && LOAD(i, xrBeginSession) && LOAD(i, xrEndSession) &&
           LOAD(i, xrEnumerateSwapchainFormats) && LOAD(i, xrCreateSwapchain) && LOAD(i, xrDestroySwapchain) &&
           LOAD(i, xrEnumerateSwapchainImages) && LOAD(i, xrWaitFrame) && LOAD(i, xrBeginFrame) &&
           LOAD(i, xrEndFrame) && LOAD(i, xrAcquireSwapchainImage) && LOAD(i, xrWaitSwapchainImage) &&
           LOAD(i, xrReleaseSwapchainImage) && LOAD(i, xrGetD3D11GraphicsRequirementsKHR) && LOAD(i, xrLocateViews) &&
           LOAD(i, xrLocateSpace) && LOAD(i, xrStringToPath) && LOAD(i, xrCreateActionSet) &&
           LOAD(i, xrCreateAction) && LOAD(i, xrSuggestInteractionProfileBindings) &&
           LOAD(i, xrAttachSessionActionSets) && LOAD(i, xrCreateActionSpace) && LOAD(i, xrSyncActions);
}

static void read_ini(void) {
    WCHAR ini[MAX_PATH * 2], v[MAX_PATH * 2];
    hr_beside_exe(HXR_INI_NAME, ini, MAX_PATH * 2);
    if (!ini[0]) return;
    g_test_pattern = (int)GetPrivateProfileIntW(HXR_INI_SECTION, L"test_pattern", 0, ini) != 0;
    if (GetPrivateProfileStringW(HXR_INI_SECTION, L"runtime_json", L"", v, MAX_PATH * 2, ini) > 0) {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", v);   /* this process only */
        hr_log("xr: runtime for this game only: %ls", v);
    } else {
        hr_log("xr: no runtime_json in %ls - the system's active OpenXR runtime is used", HXR_INI_NAME);
    }
}

static int start_runtime(void) {
    WCHAR path[MAX_PATH * 2];
    XrResult r;
    read_ini();
    hr_beside_exe(HXR_LOADER_NAME, path, MAX_PATH * 2);
    HMODULE lib = path[0] ? LoadLibraryW(path) : NULL;
    if (!lib) { hr_log("xr: %ls (32-bit) not found beside the exe - headset output OFF", HXR_LOADER_NAME); return 0; }
    p_gipa = (PFN_xrGetInstanceProcAddr)(void *)GetProcAddress(lib, "xrGetInstanceProcAddr");
    if (!p_gipa || !LOAD(XR_NULL_HANDLE, xrCreateInstance)) { hr_log("xr: the loader has no entry point"); return 0; }
    const char *exts[1] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ici;
    memset(&ici, 0, sizeof ici);
    ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy(ici.applicationInfo.applicationName, HXR_APP_NAME);
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = exts;
    r = p_xrCreateInstance(&ici, &g_inst);
    if (XR_FAILED(r)) {
        hr_log("xr: xrCreateInstance failed (XrResult %d): no 32-bit D3D11 OpenXR runtime answered", (int)r);
        return 0;
    }
    if (!load_all(g_inst)) { hr_log("xr: a required OpenXR function is missing"); return 0; }
    XrSystemGetInfo gi = { XR_TYPE_SYSTEM_GET_INFO };
    XrGraphicsRequirementsD3D11KHR req = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = p_xrGetSystem(g_inst, &gi, &g_sys);
    if (XR_FAILED(r)) { hr_log("xr: no headset system (XrResult %d)", (int)r); return 0; }
    r = p_xrGetD3D11GraphicsRequirementsKHR(g_inst, g_sys, &req);   /* required before xrCreateSession */
    if (XR_FAILED(r)) { hr_log("xr: graphics requirements failed (XrResult %d)", (int)r); return 0; }
    g_req_luid = req.adapterLuid;
    g_req_fl = req.minFeatureLevel;
    return 1;
}

static int make_own_device(void) {
    IDXGIFactory1 *fac = NULL;
    IDXGIAdapter1 *ad = NULL, *chosen = NULL;
    D3D_FEATURE_LEVEL fl = g_req_fl > D3D_FEATURE_LEVEL_11_0 ? g_req_fl : D3D_FEATURE_LEVEL_11_0;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll"), d3d = LoadLibraryW(L"d3d11.dll");
    HRESULT (WINAPI *p_factory)(REFIID, void **) =
        dxgi ? (HRESULT (WINAPI *)(REFIID, void **))(void *)GetProcAddress(dxgi, "CreateDXGIFactory1") : NULL;
    PFN_D3D11_CREATE_DEVICE p_dev = d3d ? (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(d3d, "D3D11CreateDevice") : NULL;
    if (!p_dev) { hr_log("xr: d3d11.dll has no D3D11CreateDevice"); return 0; }
    if (p_factory && SUCCEEDED(p_factory(&IID_IDXGIFactory1, (void **)&fac))) {
        for (UINT i = 0; IDXGIFactory1_EnumAdapters1(fac, i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d;
            IDXGIAdapter1_GetDesc1(ad, &d);
            if (!chosen && memcmp(&d.AdapterLuid, &g_req_luid, sizeof(LUID)) == 0) chosen = ad;
            else IDXGIAdapter1_Release(ad);
        }
        IDXGIFactory1_Release(fac);
    }
    HRESULT hr = p_dev((IDXGIAdapter *)chosen, chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                       &fl, 1, D3D11_SDK_VERSION, &g_dev, NULL, &g_ctx);
    if (chosen) IDXGIAdapter1_Release(chosen);
    if (FAILED(hr)) hr_log("xr: could not make the headset thread's D3D11 device (hr 0x%08lx)", (unsigned long)hr);
    return SUCCEEDED(hr);
}

/* ---- controllers: one grip pose per hand (2026-10-16, for the hands) ------------------------------- */
static XrPath path(const char *s) {
    XrPath p = XR_NULL_PATH;
    p_xrStringToPath(g_inst, s, &p);
    return p;
}
/* bind both grips and aims for one controller family, plus its buttons and sticks (hxr_buttons.c); a runtime that
 * does not know the family just says no. If it refuses the buttons, the poses alone are tried again, so the hands
 * never stop working because of a button path. Returns 2 = with buttons, 1 = poses only, 0 = refused. */
static int suggest(const char *profile) {
    XrActionSuggestedBinding b[40] = { { g_grip[0], path("/user/hand/left/input/grip/pose") },
                                       { g_grip[1], path("/user/hand/right/input/grip/pose") },
                                       { g_aim[0], path("/user/hand/left/input/aim/pose") },
                                       { g_aim[1], path("/user/hand/right/input/aim/pose") } };
    uint32_t n = hxb_bindings(g_inst, profile, b, 4, 40);
    XrInteractionProfileSuggestedBinding sb = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    sb.interactionProfile = path(profile);
    sb.countSuggestedBindings = n;
    sb.suggestedBindings = b;
    if (n > 4 && XR_SUCCEEDED(p_xrSuggestInteractionProfileBindings(g_inst, &sb))) return 2;
    if (n > 4) hr_log("xr: %s refused its button bindings; poses only for it", profile);
    sb.countSuggestedBindings = 4;
    return XR_SUCCEEDED(p_xrSuggestInteractionProfileBindings(g_inst, &sb)) ? 1 : 0;
}
static void setup_controllers(void) {
    static const char *const profiles[] = {
        "/interaction_profiles/khr/simple_controller", "/interaction_profiles/oculus/touch_controller",
        "/interaction_profiles/valve/index_controller", "/interaction_profiles/htc/vive_controller",
        "/interaction_profiles/microsoft/motion_controller" };
    static const char *const names[2] = { "left_grip", "right_grip" }, *const shown[2] = { "Left hand", "Right hand" };
    static const char *const anames[2] = { "left_aim", "right_aim" }, *const ashown[2] = { "Left aim", "Right aim" };
    XrActionSetCreateInfo asi = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(asi.actionSetName, "hands");
    strcpy(asi.localizedActionSetName, "Hands");
    if (XR_FAILED(p_xrCreateActionSet(g_inst, &asi, &g_actions))) { hr_log("xr: no action set - controllers OFF"); return; }
    for (int h = 0; h < 2; ++h) {
        XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
        ai.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy(ai.actionName, names[h]);
        strcpy(ai.localizedActionName, shown[h]);
        if (XR_FAILED(p_xrCreateAction(g_actions, &ai, &g_grip[h]))) { hr_log("xr: no grip action - controllers OFF"); return; }
        XrActionCreateInfo bi = { XR_TYPE_ACTION_CREATE_INFO };
        bi.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy(bi.actionName, anames[h]);
        strcpy(bi.localizedActionName, ashown[h]);
        if (XR_FAILED(p_xrCreateAction(g_actions, &bi, &g_aim[h]))) { hr_log("xr: no aim action - controllers OFF"); return; }
    }
    int buttons = hxb_create(g_inst, g_actions, p_gipa);
    if (!buttons) hr_log("xr: button actions could not be made - poses only");
    int bound = 0, with_buttons = 0;
    for (size_t i = 0; i < sizeof profiles / sizeof profiles[0]; ++i) {
        int r = suggest(profiles[i]);
        bound += r > 0;
        with_buttons += r == 2;
    }
    XrSessionActionSetsAttachInfo at = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    at.countActionSets = 1;
    at.actionSets = &g_actions;
    if (XR_FAILED(p_xrAttachSessionActionSets(g_sess, &at))) { hr_log("xr: action set not attached - controllers OFF"); return; }
    for (int h = 0; h < 2; ++h) {
        XrActionSpaceCreateInfo si = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        si.action = g_grip[h];
        si.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(p_xrCreateActionSpace(g_sess, &si, &g_hand_space[h]))) { hr_log("xr: no hand space - controllers OFF"); return; }
        XrActionSpaceCreateInfo ai2 = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        ai2.action = g_aim[h];
        ai2.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(p_xrCreateActionSpace(g_sess, &ai2, &g_aim_space[h]))) { hr_log("xr: no aim space - controllers OFF"); return; }
    }
    g_hands_on = 1;
    hr_log("xr: controllers ON: grip and aim poses asked for every frame (%d of %d controller families accepted, %d "
           "with buttons and sticks)", bound, (int)(sizeof profiles / sizeof profiles[0]), with_buttons);
}
/* each headset frame: let the runtime update the actions, then find each controller in LOCAL space */
static void locate_hands(XrTime t, HrPose *hp) {
    if (!g_hands_on) return;
    XrActiveActionSet active = { g_actions, XR_NULL_PATH };
    XrActionsSyncInfo si = { XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    if (XR_FAILED(p_xrSyncActions(g_sess, &si))) return;    /* not focused yet: nothing to read */
    hxb_read(g_sess, &hp->in);
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    for (int h = 0; h < 2; ++h) {
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        XrResult lr = p_xrLocateSpace(g_hand_space[h], g_space, t, &loc);
        /* BeG0nE Occlusion Drift rides on the jitter log: one line per controller read, tracked or lost */
        jl_hand_q(h ? 'R' : 'L', XR_SUCCEEDED(lr) && (loc.locationFlags & need) == need, loc.pose.orientation.x,
                  loc.pose.orientation.y, loc.pose.orientation.z, loc.pose.orientation.w, loc.pose.position.x,
                  loc.pose.position.y, loc.pose.position.z);
        if (XR_FAILED(lr) || (loc.locationFlags & need) != need) continue;
        hp->hand[h].valid = 1;
        hp->hand[h].q.x = loc.pose.orientation.x; hp->hand[h].q.y = loc.pose.orientation.y;
        hp->hand[h].q.z = loc.pose.orientation.z; hp->hand[h].q.w = loc.pose.orientation.w;
        hp->hand[h].p.x = loc.pose.position.x; hp->hand[h].p.y = loc.pose.position.y; hp->hand[h].p.z = loc.pose.position.z;
        InterlockedIncrement(&g_hand_located[h]);
    }
    for (int h = 0; h < 2; ++h) {   /* the aim poses (2026-10-16, hr_aim.c); a runtime without them leaves valid 0 */
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(p_xrLocateSpace(g_aim_space[h], g_space, t, &loc)) || (loc.locationFlags & need) != need) continue;
        hp->aim[h].valid = 1;
        hp->aim[h].q.x = loc.pose.orientation.x; hp->aim[h].q.y = loc.pose.orientation.y;
        hp->aim[h].q.z = loc.pose.orientation.z; hp->aim[h].q.w = loc.pose.orientation.w;
        hp->aim[h].p.x = loc.pose.position.x; hp->aim[h].p.y = loc.pose.position.y; hp->aim[h].p.z = loc.pose.position.z;
        InterlockedIncrement(&g_aim_located[h]);
    }
}

static int create_session(void) {
    XrGraphicsBindingD3D11KHR bind = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    bind.device = g_dev;
    sci.next = &bind;
    sci.systemId = g_sys;
    XrResult r = p_xrCreateSession(g_inst, &sci, &g_sess);
    if (XR_FAILED(r)) { hr_log("xr: xrCreateSession failed (XrResult %d)", (int)r); return 0; }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    r = p_xrCreateReferenceSpace(g_sess, &rs, &g_space);
    if (XR_FAILED(r)) { hr_log("xr: no LOCAL reference space (XrResult %d)", (int)r); return 0; }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;      /* the head itself, for the head pose */
    if (XR_FAILED(p_xrCreateReferenceSpace(g_sess, &rs, &g_view_space))) g_view_space = XR_NULL_HANDLE;
    setup_controllers();
    hr_log("xr: session created on the headset thread's own D3D11 device; waiting for the runtime to start it");
    return 1;
}

static void set_live(int on) {
    g_running = on;
    InterlockedExchange(&g_live, on);
    if (on) jl_vr_on(); else jl_vr_off();
}

static void poll_events(void) {
    XrEventDataBuffer ev;
    memset(&ev, 0, sizeof ev);
    ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    while (p_xrPollEvent(g_inst, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *sc = (const XrEventDataSessionStateChanged *)&ev;
            if (sc->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(p_xrBeginSession(g_sess, &bi))) { set_live(1); hr_log("xr: session running"); }
            } else if (sc->state == XR_SESSION_STATE_STOPPING) {
                p_xrEndSession(g_sess);
                set_live(0);
                hr_log("xr: session stopped by the runtime");
            } else if (sc->state == XR_SESSION_STATE_EXITING || sc->state == XR_SESSION_STATE_LOSS_PENDING) {
                set_live(0);
                InterlockedExchange(&g_stage, ST_FAILED);
                hr_log("xr: the runtime ended the session - headset output stopped");
            }
        }
        memset(&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

/* ---- swapchains + held textures ------------------------------------------------------------------- */
static void destroy_chains(void) {
    for (int e = 0; e < 2; ++e) {
        if (g_chain[e].sc) p_xrDestroySwapchain(g_chain[e].sc);
        free(g_chain[e].img);
        memset(&g_chain[e], 0, sizeof g_chain[e]);
        if (g_held[e]) { ID3D11Texture2D_Release(g_held[e]); g_held[e] = NULL; }
    }
    g_have_picture = 0;
}

static int make_chain(UINT w, UINT h, EyeChain *c) {
    XrSwapchainCreateInfo ci;
    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = g_fmt; ci.width = w; ci.height = h;
    ci.sampleCount = 1; ci.faceCount = 1; ci.arraySize = 1; ci.mipCount = 1;
    if (XR_FAILED(p_xrCreateSwapchain(g_sess, &ci, &c->sc))) return 0;
    p_xrEnumerateSwapchainImages(c->sc, 0, &c->count, NULL);
    c->img = (XrSwapchainImageD3D11KHR *)calloc(c->count ? c->count : 1, sizeof *c->img);
    if (!c->img) return 0;
    for (uint32_t i = 0; i < c->count; ++i) c->img[i].type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
    return XR_SUCCEEDED(p_xrEnumerateSwapchainImages(c->sc, c->count, &c->count, (XrSwapchainImageBaseHeader *)c->img));
}

static int build_chains(uint32_t w, uint32_t h) {
    int64_t fmts[HXR_MAX_FORMATS];
    uint32_t n = 0;
    destroy_chains();
    if (XR_FAILED(p_xrEnumerateSwapchainFormats(g_sess, HXR_MAX_FORMATS, &n, fmts))) return 0;
    g_fmt = hxr_pick_format(fmts, n);
    if (!g_fmt) { hr_log("xr: the runtime offers no 8-bit RGBA/BGRA swapchain format - headset output stopped"); return 0; }
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = (DXGI_FORMAT)g_fmt;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (int e = 0; e < 2; ++e) {
        if (!make_chain(w, h, &g_chain[e]) || FAILED(ID3D11Device_CreateTexture2D(g_dev, &td, NULL, &g_held[e]))) {
            hr_log("xr: swapchain or held texture setup failed (%ux%u format %d) - headset output stopped", w, h,
                   (int)g_fmt);
            destroy_chains();
            return 0;
        }
    }
    g_w = w; g_h = h;
    hr_log("xr: two %ux%u eye swapchains, format %d%s", w, h, (int)g_fmt,
           hxr_needs_swizzle(g_fmt) ? " (red/blue swapped on upload)" : "");
    return 1;
}

/* the newest handed-over frame, if any: fix the bytes in place, upload both eyes into the held textures */
static int take_new_frame(void) {
    HxrFrame *f = hxr_frame_take(g_seen);
    if (!f) return 1;
    int ok = 1;
    if (!g_chain[0].sc || f->w != g_w || f->h != g_h) ok = build_chains(f->w, f->h);
    if (ok) {
        int sw = hxr_needs_swizzle(g_fmt);
        for (int e = 0; e < 2; ++e) {
            hxr_fix_pixels((uint32_t *)f->eye[e], (size_t)f->w * f->h, sw);
            ID3D11DeviceContext_UpdateSubresource(g_ctx, (ID3D11Resource *)g_held[e], 0, NULL, f->eye[e], f->w * 4u, 0);
        }
        g_have_picture = 1;
        g_held_pose = f->pose;
        InterlockedIncrement(&g_uploads);
    }
    g_seen = f->serial;
    hxr_frame_release(f);
    return ok;
}

/* 1 when the eye's swapchain image got its picture and was released. A timed-out image stays acquired (the spec
 * forbids releasing it unwaited) and is waited on again next frame. */
static int fill_eye(int e) {
    XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    uint32_t idx = 0;
    if (g_chain[e].pending) idx = g_chain[e].pending - 1;
    else if (XR_FAILED(p_xrAcquireSwapchainImage(g_chain[e].sc, &ai, &idx)) || idx >= g_chain[e].count) return 0;
    wi.timeout = HXR_IMAGE_WAIT_NS;
    XrResult r = p_xrWaitSwapchainImage(g_chain[e].sc, &wi);
    if (r == XR_TIMEOUT_EXPIRED) { g_chain[e].pending = idx + 1; return 0; }
    g_chain[e].pending = 0;
    if (XR_FAILED(r)) return 0;
    ID3D11Resource *dst = (ID3D11Resource *)g_chain[e].img[idx].texture;
    if (g_test_pattern) {
        D3D11_RENDER_TARGET_VIEW_DESC rd;
        ID3D11RenderTargetView *rtv = NULL;
        memset(&rd, 0, sizeof rd);
        rd.Format = (DXGI_FORMAT)g_fmt;              /* swapchain images may be typeless: name the format */
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(g_dev, dst, &rd, &rtv))) {
            ID3D11DeviceContext_ClearRenderTargetView(g_ctx, rtv, g_tp_colour[e]);
            ID3D11RenderTargetView_Release(rtv);
        }
    } else {
        ID3D11DeviceContext_CopyResource(g_ctx, dst, (ID3D11Resource *)g_held[e]);
    }
    return XR_SUCCEEDED(p_xrReleaseSwapchainImage(g_chain[e].sc, &ri));
}

/* the head (VIEW space) and both eye views in LOCAL space at this frame's display time, for the game's camera */
static void publish_pose(XrTime t) {
    HrPose hp;
    XrView v[2];
    XrViewLocateInfo vi = { XR_TYPE_VIEW_LOCATE_INFO };
    XrViewState vst = { XR_TYPE_VIEW_STATE };
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    uint32_t nv = 0;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    memset(&hp, 0, sizeof hp);
    memset(v, 0, sizeof v);
    v[0].type = v[1].type = XR_TYPE_VIEW;
    vi.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vi.displayTime = t;
    vi.space = g_space;
    if (!g_view_space || XR_FAILED(p_xrLocateSpace(g_view_space, g_space, t, &loc)) ||
        (loc.locationFlags & need) != need)
        return;
    if (XR_FAILED(p_xrLocateViews(g_sess, &vi, &vst, 2, &nv, v)) || nv != 2 || (vst.viewStateFlags & need) != need)
        return;
    hp.head_q.x = loc.pose.orientation.x; hp.head_q.y = loc.pose.orientation.y;
    hp.head_q.z = loc.pose.orientation.z; hp.head_q.w = loc.pose.orientation.w;
    hp.head_p.x = loc.pose.position.x; hp.head_p.y = loc.pose.position.y; hp.head_p.z = loc.pose.position.z;
    for (int e = 0; e < 2; ++e) {
        hp.eye[e].q.x = v[e].pose.orientation.x; hp.eye[e].q.y = v[e].pose.orientation.y;
        hp.eye[e].q.z = v[e].pose.orientation.z; hp.eye[e].q.w = v[e].pose.orientation.w;
        hp.eye[e].p.x = v[e].pose.position.x; hp.eye[e].p.y = v[e].pose.position.y; hp.eye[e].p.z = v[e].pose.position.z;
        hp.eye[e].fov.left = v[e].fov.angleLeft; hp.eye[e].fov.right = v[e].fov.angleRight;
        hp.eye[e].fov.up = v[e].fov.angleUp; hp.eye[e].fov.down = v[e].fov.angleDown;
    }
    hp.valid = 1;
    locate_hands(t, &hp);
    pose_publish(&hp);
    jl_pose_q(hp.head_q.x, hp.head_q.y, hp.head_q.z, hp.head_q.w, hp.head_p.x, hp.head_p.y, hp.head_p.z);
}

/* one headset frame; with a picture, both eyes go out as one projection layer at the runtime's own eye poses */
static void submit_frame(void) {
    XrFrameWaitInfo fwi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState fs = { XR_TYPE_FRAME_STATE };
    XrFrameBeginInfo fbi = { XR_TYPE_FRAME_BEGIN_INFO };
    XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
    XrCompositionLayerProjection proj;
    XrCompositionLayerProjectionView pv[2];
    XrView views[2];
    const XrCompositionLayerBaseHeader *layers[1];
    int have = 0;

    if (XR_FAILED(p_xrWaitFrame(g_sess, &fwi, &fs))) { Sleep(HXR_IDLE_SLEEP_MS); return; }
    publish_pose(fs.predictedDisplayTime);
    if (!take_new_frame()) { InterlockedExchange(&g_stage, ST_FAILED); }
    if (XR_FAILED(p_xrBeginFrame(g_sess, &fbi))) return;
    if (fs.shouldRender && g_chain[0].sc && (g_test_pattern || g_have_picture)) {
        XrViewLocateInfo vi = { XR_TYPE_VIEW_LOCATE_INFO };
        XrViewState vst = { XR_TYPE_VIEW_STATE };
        uint32_t nv = 0;
        memset(views, 0, sizeof views);
        views[0].type = views[1].type = XR_TYPE_VIEW;
        vi.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vi.displayTime = fs.predictedDisplayTime;
        vi.space = g_space;
        int views_ok = XR_SUCCEEDED(p_xrLocateViews(g_sess, &vi, &vst, 2, &nv, views)) && nv == 2;
        int filled = fill_eye(0) + fill_eye(1);
        int own = g_held_pose.valid && !g_test_pattern;   /* the pose the picture was drawn with */
        if ((views_ok || own) && filled == 2) {
            if (own) InterlockedIncrement(&g_frames_own_pose);
            for (int e = 0; e < 2; ++e) {
                memset(&pv[e], 0, sizeof pv[e]);
                pv[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                if (own) {
                    const HrEyeView *v = &g_held_pose.eye[e];
                    pv[e].pose.orientation.x = v->q.x; pv[e].pose.orientation.y = v->q.y;
                    pv[e].pose.orientation.z = v->q.z; pv[e].pose.orientation.w = v->q.w;
                    pv[e].pose.position.x = v->p.x; pv[e].pose.position.y = v->p.y; pv[e].pose.position.z = v->p.z;
                    pv[e].fov.angleLeft = v->fov.left; pv[e].fov.angleRight = v->fov.right;
                    pv[e].fov.angleUp = v->fov.up; pv[e].fov.angleDown = v->fov.down;
                } else {
                    pv[e].pose = views[e].pose;
                    pv[e].fov = views[e].fov;
                }
                pv[e].subImage.swapchain = g_chain[e].sc;
                pv[e].subImage.imageRect.extent.width = (int32_t)g_w;
                pv[e].subImage.imageRect.extent.height = (int32_t)g_h;
            }
            memset(&proj, 0, sizeof proj);
            proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            proj.space = g_space;
            proj.viewCount = 2;
            proj.views = pv;
            layers[0] = (const XrCompositionLayerBaseHeader *)&proj;
            have = 1;
        }
    }
    fei.displayTime = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount = (uint32_t)have;
    fei.layers = have ? layers : NULL;
    XrResult er = p_xrEndFrame(g_sess, &fei);
    jl_present();
    if (XR_FAILED(er)) { hr_log("xr: xrEndFrame failed (XrResult %d) with %d layer(s)", (int)er, have); have = 0; }
    if (have) {
        LONG n = InterlockedIncrement(&g_submitted);
        if (n == 1 || n % HXR_LOG_EVERY == 0) hr_log("xr: %ld headset frames submitted", (long)n);
    } else {
        InterlockedIncrement(&g_empty);
    }
}

static DWORD WINAPI xr_thread(LPVOID unused) {
    (void)unused;
    jl_init("psychonauts-vr", "openxr");
    if (!start_runtime() || !make_own_device() || !create_session()) {
        InterlockedExchange(&g_stage, ST_FAILED);
        hr_log("xr: headset output could not start (see the lines above); the game runs on");
        return 0;
    }
    InterlockedExchange(&g_stage, ST_RUNNING);
    while (g_stage == ST_RUNNING) {
        poll_events();
        if (!g_running) { Sleep(HXR_IDLE_SLEEP_MS); continue; }
        if (g_test_pattern && !g_chain[0].sc && hxr_frame_serial() == 0 && !build_chains(HXR_TP_W, HXR_TP_H)) {
            InterlockedExchange(&g_stage, ST_FAILED);
            break;
        }
        submit_frame();
    }
    set_live(0);
    hr_log("xr: headset thread stopped");
    return 0;   /* resources are left to process exit */
}

/* ---- the game thread's side ----------------------------------------------------------------------- */
void hxr_start(void) {
    if (InterlockedCompareExchange(&g_stage, ST_LOADING, ST_OFF) != ST_OFF) return;
    hxr_frame_init();
    HANDLE h = CreateThread(NULL, 0, xr_thread, NULL, 0, NULL);
    if (!h) { InterlockedExchange(&g_stage, ST_FAILED); hr_log("xr: could not start the headset thread"); return; }
    CloseHandle(h);
    hr_log("xr: ON - headset thread started (own D3D11 device, projection layers, CPU handover)");
}

int hxr_live(void) { return g_live != 0; }

void hxr_stats(HxrStats *out) {
    LONG st = g_stage;
    out->submitted = g_submitted;
    out->empty = g_empty;
    out->uploads = g_uploads;
    out->own_pose = g_frames_own_pose;
    out->hands_located[0] = g_hand_located[0];
    out->hands_located[1] = g_hand_located[1];
    out->aims_located[0] = g_aim_located[0];
    out->aims_located[1] = g_aim_located[1];
    out->stage = st >= ST_OFF && st <= ST_FAILED ? g_stage_names[st] : "?";
}
