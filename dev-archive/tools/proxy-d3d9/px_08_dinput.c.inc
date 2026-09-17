
/* ===================================================================== *
 * notes/67: DirectInput mouse-delta injection — Candidate 1, done the way
 * the engine already supports rather than by fighting it.
 *
 * Why this and not a matrix write: writing the camera world matrix at
 * +0x90 from BeforeEye1 was measured to be OVERWRITTEN inside the same
 * frame (dumps came back bit-for-bit identical with the yaw on and off) —
 * CandB is the engine's own camera-update tick, so anything written around
 * it loses to the update inside it. Rather than find a hook point inside
 * CandB, feed the rotation in as INPUT: notes/60 established that this
 * game's camera yaw is driven by mouse deltas and is decoupled from Raz's
 * body facing, so the engine already knows how to turn a delta into a
 * persistent camera rotation — including its own smoothing and clamping.
 *
 * The decisive advantage for the void: the engine rotates its OWN camera,
 * so culling follows for free. No timing problem exists, because we never
 * race the camera update — we are upstream of it.
 *
 * Chain: DirectInput8Create (exe IAT) -> IDirectInput8::CreateDevice
 * (vtable slot 3) -> IDirectInputDevice8::GetDeviceState (slot 9) on the
 * system mouse. GetDeviceState fills a DIMOUSESTATE (lX, lY, lZ, buttons);
 * we call through to the real one for genuine hardware state, then add our
 * synthetic delta, which is indistinguishable from a real mouse move.
 * Technique credit: Vireio Perception's VRBoost, via the project's own
 * -external-research topic 2026-08-24-directinput-mouse-injection.
 * ===================================================================== */
static volatile LONG g_injectMouseDX = 0;  /* one-shot, consumed on next poll */
static volatile LONG g_injectMouseDY = 0;
static const GUID kGuidSysMouse =
    { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

typedef HRESULT (WINAPI *DIGetDeviceState_t)(void *dev, DWORD cb, void *data);
typedef HRESULT (WINAPI *DICreateDevice_t)(void *di, const GUID *rguid, void **dev, void *outer);
typedef HRESULT (WINAPI *DI8Create_t)(HINSTANCE h, DWORD ver, const GUID *riid, void **out, void *outer);
static DIGetDeviceState_t g_pRealGetDeviceState = NULL;
static DICreateDevice_t   g_pRealDICreateDevice = NULL;
static DI8Create_t        g_pRealDI8Create      = NULL;

static void *g_diMouseDevice = NULL;   /* the ONE device we may touch */

/* notes/67: per-axis override of the gamepad's DIJOYSTATE. Index order matches
 * the struct: 0=lX 1=lY 2=lZ 3=lRx 4=lRy 5=lRz. Off by default, so real stick
 * input passes through untouched. */
static int  g_padOverrideOn[6]  = { 0, 0, 0, 0, 0, 0 };
static LONG g_padOverrideVal[6] = { 0, 0, 0, 0, 0, 0 };

/* notes/67: counters for the OTHER input paths a game can read a mouse through.
 * GetDeviceState (immediate DirectInput) is already covered and was proven not
 * to be used for the mouse here, so the remaining candidates are:
 *   - DirectInput BUFFERED reads .......... GetDeviceData, vtable slot 10
 *   - Win32 window messages ............... WM_MOUSEMOVE
 *   - Raw Input ........................... WM_INPUT
 * Counted only while the probe window is open. Whichever is non-zero while the
 * user moves the mouse is the path that must be injected into.
 * (The counters themselves are declared near the top of this file, because the
 * WndProc hook that increments two of them sits earlier than this code.) */

/* notes/67: GetDeviceState probe. Answers, with data instead of inference:
 *   - does the game poll DirectInput at all, and for WHICH devices
 *   - do real mouse movements actually arrive as non-zero lX/lY
 *   - what magnitudes look like, so injected deltas can be scaled sensibly
 * Every distinct device pointer is tracked separately, which also reveals a
 * gamepad if one is being polled - relevant when a controller is plugged in.
 * Armed for a fixed window by the `mouseprobe <seconds>` command. */
#define DIPROBE_MAX_DEV 6
typedef struct {
    void  *dev;
    LONG   calls;
    LONG   nonzero;
    DWORD  lastCb;
    LONG   maxAbsX, maxAbsY;
    LONG   lastX, lastY;
    int    axisSeen;
    LONG   axMin[6], axMax[6], axLast[6];
} DIProbeDev;
static DIProbeDev  g_diProbe[DIPROBE_MAX_DEV];
/* g_diProbeActive is declared near the top of the file - the WndProc hook needs
 * it and sits earlier than this. */
static DWORD        g_diProbeUntil   = 0;

static void DIProbeRecord(void *dev, DWORD cb, const void *data)
{
    int i, slot = -1;
    LONG x = 0, y = 0;
    for (i = 0; i < DIPROBE_MAX_DEV; i++) {
        if (g_diProbe[i].dev == dev) { slot = i; break; }
        if (g_diProbe[i].dev == NULL) { g_diProbe[i].dev = dev; slot = i; break; }
    }
    if (slot < 0) return;
    g_diProbe[slot].calls++;
    g_diProbe[slot].lastCb = cb;
    /* notes/67: a DIJOYSTATE is exactly 80 bytes and starts with six LONG axes
     * (lX lY lZ lRx lRy lRz). Log their range so the resting centre and the
     * axis scale are known BEFORE anything is injected - guessing a magnitude
     * is how the mouse attempt wasted a session. */
    if (cb == 80 && data) {
        const LONG *ax = (const LONG *)data;
        int a;
        for (a = 0; a < 6; a++) {
            if (!g_diProbe[slot].axisSeen) {
                g_diProbe[slot].axMin[a] = ax[a]; g_diProbe[slot].axMax[a] = ax[a];
            } else {
                if (ax[a] < g_diProbe[slot].axMin[a]) g_diProbe[slot].axMin[a] = ax[a];
                if (ax[a] > g_diProbe[slot].axMax[a]) g_diProbe[slot].axMax[a] = ax[a];
            }
            g_diProbe[slot].axLast[a] = ax[a];
        }
        g_diProbe[slot].axisSeen = 1;
    }
    /* Only interpret the first two LONGs as axes for the known mouse device -
     * for anything else (a keyboard's byte array) that would be nonsense. */
    if (dev == g_diMouseDevice && data && cb >= 3 * sizeof(LONG)) {
        x = ((const LONG *)data)[0];
        y = ((const LONG *)data)[1];
        if (x || y) {
            g_diProbe[slot].nonzero++;
            g_diProbe[slot].lastX = x; g_diProbe[slot].lastY = y;
            if (x < 0 ? -x : x > g_diProbe[slot].maxAbsX) g_diProbe[slot].maxAbsX = (x < 0 ? -x : x);
            if (y < 0 ? -y : y > g_diProbe[slot].maxAbsY) g_diProbe[slot].maxAbsY = (y < 0 ? -y : y);
        }
    }
}

static void DIProbeReport(void)
{
    int i;
    LogLine("MouseProbe: === results ===");
    for (i = 0; i < DIPROBE_MAX_DEV; i++) {
        if (!g_diProbe[i].dev) continue;
        LogLine("MouseProbe: dev=%p %s calls=%ld cb=%lu nonzeroMotion=%ld maxAbs=(%ld,%ld) last=(%ld,%ld)",
                g_diProbe[i].dev,
                (g_diProbe[i].dev == g_diMouseDevice) ? "[MOUSE]" : "[other]",
                g_diProbe[i].calls, g_diProbe[i].lastCb, g_diProbe[i].nonzero,
                g_diProbe[i].maxAbsX, g_diProbe[i].maxAbsY,
                g_diProbe[i].lastX, g_diProbe[i].lastY);
        if (g_diProbe[i].axisSeen) {
            LogLine("MouseProbe:   axes min  X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld",
                    g_diProbe[i].axMin[0], g_diProbe[i].axMin[1], g_diProbe[i].axMin[2],
                    g_diProbe[i].axMin[3], g_diProbe[i].axMin[4], g_diProbe[i].axMin[5]);
            LogLine("MouseProbe:   axes max  X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld",
                    g_diProbe[i].axMax[0], g_diProbe[i].axMax[1], g_diProbe[i].axMax[2],
                    g_diProbe[i].axMax[3], g_diProbe[i].axMax[4], g_diProbe[i].axMax[5]);
            LogLine("MouseProbe:   axes rest X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld  "
                    "(an axis whose min!=max is one you MOVED)",
                    g_diProbe[i].axLast[0], g_diProbe[i].axLast[1], g_diProbe[i].axLast[2],
                    g_diProbe[i].axLast[3], g_diProbe[i].axLast[4], g_diProbe[i].axLast[5]);
        }
    }
    LogLine("MouseProbe: (no [MOUSE] row at all = the game never polled the mouse "
            "through GetDeviceState during the window)");
    LogLine("MouseProbe: other input paths -> GetDeviceData(buffered)=%ld  "
            "WM_MOUSEMOVE=%ld  WM_INPUT(raw)=%ld",
            g_cntGetDeviceData, g_cntWmMouseMove, g_cntWmInput);
    LogLine("MouseProbe: whichever of those is non-zero while the mouse was moving "
            "is the path that must be injected into.");
}

static HRESULT WINAPI Hook_DIGetDeviceState(void *dev, DWORD cb, void *data)
{
    HRESULT hr = g_pRealGetDeviceState(dev, cb, data);
    /* MUST match the exact device instance, not just the buffer size.
     *
     * Bug this guard exists for (found live 2026-08-27): DirectInput device
     * objects of the same class SHARE ONE VTABLE, so patching slot 9 after
     * checking the GUID at CreateDevice patches it for EVERY device - keyboard
     * included. The first version only checked `cb >= 12`, which a 256-byte
     * keyboard state buffer passes happily, so injected deltas were being
     * added to the first bytes of the KEY-STATE array. DIK_ESCAPE is index 1,
     * so the harness was synthesising Escape presses: the pause Journal kept
     * opening by itself and silently invalidated three camera tests, while the
     * mouse never saw the delta at all because the keyboard poll consumed it
     * first.
     *
     * DIMOUSESTATE also starts with three LONG axes, so the size check stays
     * as a second line of defence. */
    /* Probe first: record what the game ACTUALLY receives, before we add
     * anything, so the numbers describe the game's own input not ours. */
    if (g_diProbeActive) {
        if ((LONG)(GetTickCount() - g_diProbeUntil) >= 0) {
            InterlockedExchange(&g_diProbeActive, 0);
            DIProbeReport();
        } else if (SUCCEEDED(hr)) {
            DIProbeRecord(dev, cb, data);
        }
    }

    /* notes/67: GAMEPAD axis override. The probe proved the game polls a
     * DIJOYSTATE (cb == 80) every frame while never polling the mouse through
     * this API at all - so this, not the mouse, is the injection path that can
     * actually reach the camera. Identified by buffer size rather than by a
     * stored device pointer, so it keeps working if the pad is re-created.
     *
     * Absolute, not additive: an analog axis is a position, so we SET it while
     * an override is active and leave it alone otherwise (real stick input
     * passes through untouched). */
    if (SUCCEEDED(hr) && data && cb == 80) {
        int a;
        LONG *ax = (LONG *)data;
        for (a = 0; a < 6; a++) {
            if (g_padOverrideOn[a]) ax[a] = g_padOverrideVal[a];
        }
    }

    if (SUCCEEDED(hr) && data && dev == g_diMouseDevice && cb >= 3 * sizeof(LONG)) {
        LONG dx = InterlockedExchange(&g_injectMouseDX, 0);
        LONG dy = InterlockedExchange(&g_injectMouseDY, 0);
        if (dx || dy) {
            LONG *axes = (LONG *)data;
            axes[0] += dx;
            axes[1] += dy;
        }
    }
    return hr;
}

/* IDirectInputDevice8::GetDeviceData - the BUFFERED read path, vtable slot 10.
 * Counted only; the same shared-vtable caveat applies, so this filters on the
 * known mouse instance rather than trusting that only the mouse arrives here. */
typedef HRESULT (WINAPI *DIGetDeviceData_t)(void *dev, DWORD cbObjData, void *rgdod,
                                            DWORD *pdwInOut, DWORD dwFlags);
static DIGetDeviceData_t g_pRealGetDeviceData = NULL;

static HRESULT WINAPI Hook_DIGetDeviceData(void *dev, DWORD cbObjData, void *rgdod,
                                           DWORD *pdwInOut, DWORD dwFlags)
{
    HRESULT hr = g_pRealGetDeviceData(dev, cbObjData, rgdod, pdwInOut, dwFlags);
    if (g_diProbeActive && dev == g_diMouseDevice) InterlockedIncrement(&g_cntGetDeviceData);
    return hr;
}

static HRESULT WINAPI Hook_DICreateDevice(void *di, const GUID *rguid, void **dev, void *outer)
{
    HRESULT hr = g_pRealDICreateDevice(di, rguid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && rguid &&
        memcmp(rguid, &kGuidSysMouse, sizeof(GUID)) == 0) {
        /* Remember WHICH instance is the mouse. The vtable patch below affects
         * every device sharing this vtable (keyboard included), so the hook
         * body filters on this pointer - see the comment there. */
        g_diMouseDevice = *dev;
        if (!g_pRealGetDeviceState) {
            void **vtbl = *(void ***)(*dev);
            DWORD oldProt;
            g_pRealGetDeviceState = (DIGetDeviceState_t)vtbl[9];
            if (VirtualProtect(&vtbl[9], sizeof(void *), PAGE_READWRITE, &oldProt)) {
                vtbl[9] = (void *)Hook_DIGetDeviceState;
                VirtualProtect(&vtbl[9], sizeof(void *), oldProt, &oldProt);
                LogLine("MouseInject: hooked GetDeviceState; mouse device = %p "
                        "(vtable is shared, so the hook filters on this pointer)", *dev);
                /* Also take slot 10 (GetDeviceData) so the buffered path can be
                 * counted - see the probe report. */
                g_pRealGetDeviceData = (DIGetDeviceData_t)vtbl[10];
                if (VirtualProtect(&vtbl[10], sizeof(void *), PAGE_READWRITE, &oldProt)) {
                    vtbl[10] = (void *)Hook_DIGetDeviceData;
                    VirtualProtect(&vtbl[10], sizeof(void *), oldProt, &oldProt);
                    LogLine("MouseInject: also hooked GetDeviceData (buffered path, counted only)");
                } else {
                    g_pRealGetDeviceData = NULL;
                }
            } else {
                g_pRealGetDeviceState = NULL;
                LogLine("MouseInject: VirtualProtect failed on the device vtable");
            }
        }
    }
    return hr;
}

static HRESULT WINAPI Hook_DirectInput8Create(HINSTANCE h, DWORD ver, const GUID *riid,
                                              void **out, void *outer)
{
    HRESULT hr = g_pRealDI8Create(h, ver, riid, out, outer);
    if (SUCCEEDED(hr) && out && *out && !g_pRealDICreateDevice) {
        void **vtbl = *(void ***)(*out);
        DWORD oldProt;
        g_pRealDICreateDevice = (DICreateDevice_t)vtbl[3];
        if (VirtualProtect(&vtbl[3], sizeof(void *), PAGE_READWRITE, &oldProt)) {
            vtbl[3] = (void *)Hook_DICreateDevice;
            VirtualProtect(&vtbl[3], sizeof(void *), oldProt, &oldProt);
            LogLine("MouseInject: hooked IDirectInput8::CreateDevice");
        } else {
            g_pRealDICreateDevice = NULL;
        }
    }
    return hr;
}

static void InstallMouseInject(void)
{
    HMODULE hDI = GetModuleHandleA("dinput8.dll");
    if (!hDI) hDI = LoadLibraryA("dinput8.dll");
    if (!hDI) { LogLine("MouseInject: dinput8.dll not loadable - injection unavailable"); return; }
    g_pRealDI8Create = (DI8Create_t)GetProcAddress(hDI, "DirectInput8Create");
    if (g_pRealDI8Create &&
        PatchIATEntry("dinput8.dll", "DirectInput8Create", (void *)Hook_DirectInput8Create))
        LogLine("MouseInject: DirectInput8Create IAT-patched (mouse yaw injection armed)");
    else
        LogLine("MouseInject: could not patch DirectInput8Create - injection unavailable");
}

