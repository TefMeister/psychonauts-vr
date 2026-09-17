
void __cdecl CandB_AfterBoth_asm(void) asm("CandB_AfterBoth_asm");
void __cdecl CandB_AfterBoth_asm(void)
{
    /* notes/68: third and last candidate site for the camera-matrix write, and
     * the per-frame countdown for the probe. Probed BEFORE AutomationTick so the
     * log shows the engine's own end-of-frame value, not one the harness just
     * poked. */
    CamProbe("AfterBoth");
    ApplyCameraYawAt(3);
    CamProbe("post-write-3");
    if (g_camProbeFrames > 0) {
        if (--g_camProbeFrames == 0) LogLine("CAMPROBE ==== probe window closed ====");
        else LogLine("CAMPROBE ---- end of frame ----");
    }

    /* notes/67: external command queue + camera telemetry. Gated behind
     * PSYVR_AUTOMATION=1 (default off). See the block above for why this is
     * safe from this render-path site and what it deliberately will not do. */
    AutomationTick();

    /* notes/59 part 2: F12 - one-shot Lua-free level jump (notes/55's SetPendingLevel finding,
     * never live-tested before now). Opt-in (PSYVR_LEVEL_JUMP_KEY=1, default off) since this calls
     * real, non-diagnostic engine code with a hardcoded level - not something to leave live by
     * accident. Runs here (unconditionally every real frame) rather than inside the VR-bridge-gated
     * head-tracking block, so it works with no headset/SteamVR involved at all. */
    if (g_levelJumpKeyEnabled) {
        static SHORT s_prevF12 = 0;
        SHORT f12 = GetAsyncKeyState(VK_F12);
        if ((f12 & 0x8000) && !(s_prevF12 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            LogLine("LevelJump: F12 pressed - engine=%p, calling SetPendingLevel(\"%s\")",
                    engine, g_levelJumpCode);
            ((void (__thiscall *)(void *, const char *, BOOL))0x4FFA40)(engine, g_levelJumpCode, TRUE);
            LogLine("LevelJump: SetPendingLevel call returned (no crash) - watch for a world-space "
                    "BVM camera-coord jump in the next few seconds' log lines");
        }
        s_prevF12 = f12;
    }

    /* notes/62: NUMPAD9 - one-shot direct toggle of the "Visibility Tree Culling" debug flag
     * (exe+0x629490 decompile: for a generic-ID item like this one, id=117, the menu's own display
     * sync reads *(BYTE*)(engine_ptr + 44 + id) - writing that same byte directly here sidesteps
     * the debug menu's UI entirely, no navigation/focus needed). Opt-in (PSYVR_CULL_TOGGLE_KEY=1,
     * default off) - this is a real gameplay-affecting engine flag, not a diagnostic no-op. */
    if (g_cullToggleKeyEnabled) {
        static SHORT s_prevNum9 = 0;
        SHORT num9 = GetAsyncKeyState(VK_NUMPAD9);
        if ((num9 & 0x8000) && !(s_prevNum9 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + VISTREE_CULLING_ITEM_ID;
                *flag = !*flag;
                LogLine("CullToggle: NUMPAD9 pressed - Visibility Tree Culling flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + VISTREE_CULLING_ITEM_ID, (int)*flag);
            } else {
                LogLine("CullToggle: NUMPAD9 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum9 = num9;
    }

    /* notes/64: NUMPAD8 - one-shot direct toggle of the "Render Wireframe" debug flag, same
     * direct-byte-write pattern as NUMPAD9 above. Opt-in (PSYVR_WIREFRAME_TOGGLE_KEY=1, default
     * off). */
    if (g_wireframeToggleKeyEnabled) {
        static SHORT s_prevNum8 = 0;
        SHORT num8 = GetAsyncKeyState(VK_NUMPAD8);
        if ((num8 & 0x8000) && !(s_prevNum8 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + RENDER_WIREFRAME_ITEM_ID;
                *flag = !*flag;
                LogLine("WireframeToggle: NUMPAD8 pressed - Render Wireframe flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + RENDER_WIREFRAME_ITEM_ID, (int)*flag);
            } else {
                LogLine("WireframeToggle: NUMPAD8 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum8 = num8;
    }

    /* notes/65: NUMPAD7 - one-shot direct toggle of the "Collision Wireframe" debug flag, same
     * direct-byte-write pattern as NUMPAD8/9 above. Opt-in (PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY=1,
     * default off). This is the theoretically-correct tool for the real-gap-vs-dark-terrain test -
     * see the constant's comment. */
    if (g_collisionWireframeToggleKeyEnabled) {
        static SHORT s_prevNum7 = 0;
        SHORT num7 = GetAsyncKeyState(VK_NUMPAD7);
        if ((num7 & 0x8000) && !(s_prevNum7 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_WIREFRAME_ITEM_ID;
                *flag = !*flag;
                LogLine("CollisionWireframeToggle: NUMPAD7 pressed - Collision Wireframe flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_WIREFRAME_ITEM_ID, (int)*flag);
            } else {
                LogLine("CollisionWireframeToggle: NUMPAD7 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum7 = num7;
    }

    /* notes/66: NUMPAD6 - one-shot direct toggle of the "Collision Spheres" debug flag, same
     * direct-byte-write pattern as NUMPAD7/8/9 above. Opt-in (PSYVR_COLLISION_SPHERES_TOGGLE_KEY=1,
     * default off). Positive-control tool: proves whether debug-menu visual flags render anything
     * at all in this PC build before trusting a negative result from any of them. */
    if (g_collisionSpheresToggleKeyEnabled) {
        static SHORT s_prevNum6 = 0;
        SHORT num6 = GetAsyncKeyState(VK_NUMPAD6);
        if ((num6 & 0x8000) && !(s_prevNum6 & 0x8000)) {
            void *engine = *(void **)0x78BC20;
            if (engine) {
                unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_SPHERES_ITEM_ID;
                *flag = !*flag;
                LogLine("CollisionSpheresToggle: NUMPAD6 pressed - Collision Spheres flag @ %p (engine+%d) now %d",
                        flag, DEBUG_FLAGS_ARRAY_OFFSET + COLLISION_SPHERES_ITEM_ID, (int)*flag);
            } else {
                LogLine("CollisionSpheresToggle: NUMPAD6 pressed but engine=*(void**)0x78BC20 is NULL, skipped");
            }
        }
        s_prevNum6 = num6;
    }

    if (g_traceActive) { TraceFlushDrawsFwd(); LogLine("TRACE: -- AfterBoth (restoring BACKBUF) --"); }
    g_stereoPhase = STEREO_PHASE_IDLE;
    UIShift_ReconcileFwd(); /* notes/36: phase idle - removes any applied UI shift */
    UIVp_PhaseChangedFwd(); /* notes/42: phase idle - drop any applied UI viewport shrink */
    if (!g_stereoReady || !g_pDevice || !g_pRealBackBuffer) return;

    /* notes/28: both eyes' private render targets (g_pEye1Surf/g_pEye2Surf) are now fully
     * rendered for this frame - the natural point to pump the additive VR submission path, BEFORE
     * the render target is switched back to the real backbuffer below. Inert unless
     * PSYVR_ENABLE_SUBMIT=1 (see VRBridge_OnFrameComposited's own guard); never touches
     * g_pRealBackBuffer or anything else the existing monitor composite (Hook_Present, below)
     * depends on. */
    VRBridge_OnFrameComposited();

    g_pDevice->lpVtbl->SetRenderTarget(g_pDevice, 0, g_pRealBackBuffer);
    if (g_pRealDepthStencil) {
        g_pDevice->lpVtbl->SetDepthStencilSurface(g_pDevice, g_pRealDepthStencil);
    }
}

/* ---- naked inline-hook entry points -----------------------------------
 *
 * Both are entered via a JMP planted at the target function's own first
 * INLINE_PATCH_LEN bytes (see PatchJump), so at entry the CPU state is
 * EXACTLY what it would be at the real function's own entry point: no
 * prologue has run yet, ESP points at the return address the real caller's
 * `call` instruction pushed, and any incoming registers the real function
 * itself reads (BuildViewMatrix reads none; CandB reads ECX as a "this"-like
 * context pointer - confirmed by its own first instruction, `mov
 * [ebp-0x20],ecx`, in the raw byte dump in notes/13) are exactly as the game
 * set them up.
 *
 * Hook_BuildViewMatrix: a plain "observe via a C helper, then tail-JMP into
 * the trampoline" hook. Since it ends in a JMP (not a CALL) to the
 * trampoline, the trampoline's own eventual `ret` pops the REAL original
 * return address and resumes the game's own caller directly - transparent,
 * single real invocation, our C helper never mutates the stack region past
 * reading it.
 *
 * Hook_CandB: a "call the real body twice" hook. It uses CALL (not JMP) to
 * invoke the trampoline both times, which means each invocation pushes our
 * own return address on top of whatever's already on the stack; the real
 * function's own standard epilogue (mov esp,ebp; pop ebp; ret) pops exactly
 * that top return address and hands control straight back to us - the real
 * original return address (pushed by the game's own `call CandB`) sits
 * untouched further down the stack the entire time, and our own final `ret`
 * pops it, returning control to the game exactly once, after both eye
 * passes have completed. This two-tier "call resolves inward" trick is what
 * lets the game's own render-dispatch function be invoked twice per frame
 * from a single interception point.
 */
__attribute__((naked)) void Hook_BuildViewMatrix(void)
{
    __asm__ __volatile__(
        "movl 4(%esp), %eax\n\t"   /* pOutMatrix */
        "movl 8(%esp), %ecx\n\t"   /* pEye */
        "movl 12(%esp), %edx\n\t"  /* pAt */
        "pushl 16(%esp)\n\t"       /* pUp (esp unchanged so far, offset still valid) */
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call BVM_OnEntry_asm\n\t"
        "addl $16, %esp\n\t"
        "jmp *BVM_Trampoline_asm\n\t"
    );
}

/* Same tail-jmp shape as Hook_BuildViewMatrix, adapted for
 * BuildProjectionMatrix(pOutMatrix, rawFov, Aspect, zn, zf) - all four raw
 * 4-byte float/stack args are read exactly as __cdecl already represents
 * them on the stack. notes/24 extends this to also read zn/zf (previously
 * only rawFov/Aspect were needed); the stack layout itself was already
 * established in notes/07 and is unchanged here - only which fields this
 * hook happens to read is new. */
__attribute__((naked)) void Hook_BuildProjectionMatrix(void)
{
    __asm__ __volatile__(
        /* notes/37: scale rawFov in place BEFORE anything reads it - the observer below and the
         * real body then both see the same scaled value. x87 stack is empty at a cdecl call
         * boundary; eax/ecx/edx are caller-saved and clobbered below anyway. */
        "flds 8(%esp)\n\t"
        "fmuls g_fovScaleAsm\n\t"
        "fstps 8(%esp)\n\t"
        "movl 8(%esp), %eax\n\t"   /* rawFov (post-scale) */
        "movl 12(%esp), %ecx\n\t"  /* Aspect */
        "movl 16(%esp), %edx\n\t"  /* zn */
        "pushl 20(%esp)\n\t"       /* zf (esp unchanged so far, offset still valid) */
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call BPM_OnEntry_asm\n\t"
        "addl $16, %esp\n\t"
        "jmp *BPM_Trampoline_asm\n\t"
    );
}

__attribute__((naked)) void Hook_CandB(void)
{
    __asm__ __volatile__(
        "movl %ecx, CandB_This_asm\n\t"
        "call CandB_BeforeEye1_asm\n\t"
        "movl CandB_This_asm, %ecx\n\t"
        "call *CandB_Trampoline_asm\n\t"
        "call CandB_BeforeEye2_asm\n\t"
        "movl CandB_This_asm, %ecx\n\t"
        "call *CandB_Trampoline_asm\n\t"
        "call CandB_AfterBoth_asm\n\t"
        "ret\n\t"
    );
}

/* Allocate an executable trampoline stub: a byte-for-byte copy of the
 * original function's first `patchLen` bytes, followed by a JMP back to
 * (originalFuncAddr + patchLen) to resume the function's own unpatched
 * body. Calling this stub (via `call`) behaves exactly like calling the
 * real, never-hooked function - it's the mechanism used both to let each
 * hook's own "real" invocation proceed, and to let arbitrary other code
 * (e.g. SetEyeAndTarget re-invoking BuildViewMatrix) call the pristine
 * original directly. */
static void *MakeTrampoline(BYTE *originalFuncAddr, int patchLen)
{
    BYTE *stub = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    DWORD relTarget;

    if (!stub) return NULL;

    memcpy(stub, originalFuncAddr, (size_t)patchLen);

    stub[patchLen] = 0xE9; /* JMP rel32 */
    relTarget = (DWORD)(originalFuncAddr + patchLen) - (DWORD)(stub + patchLen + 5);
    memcpy(stub + patchLen + 1, &relTarget, 4);

    return stub;
}

/* Overwrite the first `patchLen` (>=5) bytes of originalFuncAddr with
 * E9 <rel32 to hookFuncAddr>, padded with NOPs to patchLen bytes. */
static BOOL PatchJump(BYTE *originalFuncAddr, int patchLen, void *hookFuncAddr)
{
    DWORD oldProtect;
    DWORD relTarget;
    int i;

    if (!VirtualProtect(originalFuncAddr, (SIZE_T)patchLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogLine("ERROR: VirtualProtect failed patching 0x%p, err=%lu", (void *)originalFuncAddr, GetLastError());
        return FALSE;
    }

    originalFuncAddr[0] = 0xE9;
    relTarget = (DWORD)hookFuncAddr - (DWORD)(originalFuncAddr + 5);
    memcpy(originalFuncAddr + 1, &relTarget, 4);
    for (i = 5; i < patchLen; i++) originalFuncAddr[i] = 0x90;

    VirtualProtect(originalFuncAddr, (SIZE_T)patchLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), originalFuncAddr, (SIZE_T)patchLen);
    return TRUE;
}

/* Installs both inline hooks. Pure code-patching, needs no live D3D device,
 * so it's called once from DllMain(DLL_PROCESS_ATTACH) - the target
 * addresses are inside Psychonauts.exe's own already-loaded .text section
 * from the moment the process starts. */
static BOOL InstallInlineHooks(void)
{
    void *bvmTrampoline = MakeTrampoline(ADDR_BUILDVIEWMATRIX, INLINE_PATCH_LEN);
    void *bpmTrampoline = MakeTrampoline(ADDR_BUILDPROJMATRIX, INLINE_PATCH_LEN);
    void *candbTrampoline = MakeTrampoline(ADDR_CANDB, INLINE_PATCH_LEN);

    if (!bvmTrampoline || !bpmTrampoline || !candbTrampoline) {
        LogLine("ERROR: MakeTrampoline failed (bvm=%p bpm=%p candb=%p)", bvmTrampoline, bpmTrampoline, candbTrampoline);
        return FALSE;
    }

    BVM_Trampoline_asm = bvmTrampoline;
    BPM_Trampoline_asm = bpmTrampoline;
    CandB_Trampoline_asm = candbTrampoline;
    g_realBuildViewMatrix = (BuildViewMatrixFn)bvmTrampoline;

    if (!PatchJump(ADDR_BUILDVIEWMATRIX, INLINE_PATCH_LEN, (void *)Hook_BuildViewMatrix)) return FALSE;
    if (!PatchJump(ADDR_BUILDPROJMATRIX, INLINE_PATCH_LEN, (void *)Hook_BuildProjectionMatrix)) return FALSE;
    if (!PatchJump(ADDR_CANDB, INLINE_PATCH_LEN, (void *)Hook_CandB)) return FALSE;

    LogLine("Installed inline hooks: BuildViewMatrix @0x%p (trampoline=0x%p), BuildProjectionMatrix @0x%p (trampoline=0x%p), CandB @0x%p (trampoline=0x%p)",
            (void *)ADDR_BUILDVIEWMATRIX, bvmTrampoline, (void *)ADDR_BUILDPROJMATRIX, bpmTrampoline, (void *)ADDR_CANDB, candbTrampoline);
    return TRUE;
}

/* Creates the two offscreen "per-eye" render targets, matching the real
 * backbuffer's dimensions/format, and grabs+retains a reference to the real
 * backbuffer surface (needed by Hook_Present's composite step and by
 * CandB_AfterBoth_asm to restore the render target after both eye passes). */
static void SetupStereoSurfaces(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pp)
{
    HRESULT hrBB, hrE1, hrE2, hrDS, hrDS1, hrDS2;

    g_pDevice = pDevice;
    g_bbWidth = pp->BackBufferWidth;
    g_bbHeight = pp->BackBufferHeight;

    /* notes/35: eye surfaces at scale x backbuffer (same aspect - projection untouched). If the
     * scaled allocations fail (VRAM/32-bit address space), fall back to 1x rather than losing
     * stereo entirely. */
    g_eyeWidth = g_bbWidth * g_eyeScale;
    g_eyeHeight = g_bbHeight * g_eyeScale;

    hrBB = pDevice->lpVtbl->GetBackBuffer(pDevice, 0, 0, D3DBACKBUFFER_TYPE_MONO, &g_pRealBackBuffer);
    hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
    hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);
    if ((FAILED(hrE1) || FAILED(hrE2)) && g_eyeScale > 1) {
        LogLine("SetupStereoSurfaces: %ux-scaled eye surfaces failed (hrE1=0x%08lX hrE2=0x%08lX) - falling back to 1x",
                g_eyeScale, (unsigned long)hrE1, (unsigned long)hrE2);
        if (g_pEye1Surf) { g_pEye1Surf->lpVtbl->Release(g_pEye1Surf); g_pEye1Surf = NULL; }
        if (g_pEye2Surf) { g_pEye2Surf->lpVtbl->Release(g_pEye2Surf); g_pEye2Surf = NULL; }
        g_eyeScale = 1;
        g_eyeWidth = g_bbWidth;
        g_eyeHeight = g_bbHeight;
        hrE1 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                    D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye1Surf, NULL);
        hrE2 = pDevice->lpVtbl->CreateRenderTarget(pDevice, g_eyeWidth, g_eyeHeight, D3DFMT_A8R8G8B8,
                                                    D3DMULTISAMPLE_NONE, 0, FALSE, &g_pEye2Surf, NULL);
    }

    /* notes/22: capture the device's original auto depth-stencil surface (so it can be
     * restored after both eye passes) and give each eye its OWN private depth-stencil
     * surface matching the real one's format - see the comment on SetEyeAndTarget() for
     * why sharing a single depth buffer between both eyes was the real root cause of the
     * dark-eye/frozen-eye symptoms. Format comes from pp->AutoDepthStencilFormat (live-
     * confirmed 75/D3DFMT_D24S8), not guessed. Discard=TRUE matches typical depth-buffer
     * usage and is safe here since every eye pass unconditionally Clears its own depth
     * surface before drawing. */
    hrDS = pDevice->lpVtbl->GetDepthStencilSurface(pDevice, &g_pRealDepthStencil);
    hrDS1 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_eyeWidth, g_eyeHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye1DepthStencil, NULL);
    hrDS2 = pDevice->lpVtbl->CreateDepthStencilSurface(pDevice, g_eyeWidth, g_eyeHeight,
                                                        pp->AutoDepthStencilFormat,
                                                        D3DMULTISAMPLE_NONE, 0, TRUE,
                                                        &g_pEye2DepthStencil, NULL);

    LogLine("SetupStereoSurfaces: GetBackBuffer hr=0x%08lX ptr=0x%p | Eye1 hr=0x%08lX ptr=0x%p | Eye2 hr=0x%08lX ptr=0x%p (%ux%u, scale=%ux, bb=%ux%u) | "
            "GetDS hr=0x%08lX ptr=0x%p | Eye1DS hr=0x%08lX ptr=0x%p | Eye2DS hr=0x%08lX ptr=0x%p",
            (unsigned long)hrBB, (void *)g_pRealBackBuffer,
            (unsigned long)hrE1, (void *)g_pEye1Surf,
            (unsigned long)hrE2, (void *)g_pEye2Surf,
            g_eyeWidth, g_eyeHeight, g_eyeScale, g_bbWidth, g_bbHeight,
            (unsigned long)hrDS, (void *)g_pRealDepthStencil,
            (unsigned long)hrDS1, (void *)g_pEye1DepthStencil,
            (unsigned long)hrDS2, (void *)g_pEye2DepthStencil);

    g_stereoReady = SUCCEEDED(hrBB) && SUCCEEDED(hrE1) && SUCCEEDED(hrE2) &&
                    SUCCEEDED(hrDS1) && SUCCEEDED(hrDS2) &&
                    g_pRealBackBuffer && g_pEye1Surf && g_pEye2Surf &&
                    g_pEye1DepthStencil && g_pEye2DepthStencil;

    LogLine("Stereo ready = %d", g_stereoReady ? 1 : 0);

    /* notes/28: additive VR submission path - only does anything if PSYVR_ENABLE_SUBMIT=1 was set
     * (default off). Called after the existing monitor-composite surfaces are confirmed ready so
     * the VR bridge's own eye buffers can be sized to match g_bbWidth/g_bbHeight exactly - runs on
     * both initial CreateDevice AND every Reset (this same function handles both, unchanged). */
    if (g_stereoReady) {
        VRBridge_OnStereoSurfacesReady(pDevice, g_eyeWidth, g_eyeHeight);
    }
}

