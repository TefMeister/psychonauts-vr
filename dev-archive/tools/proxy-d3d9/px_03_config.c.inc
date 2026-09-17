/* Reads PSYVR_ENABLE_SUBMIT once. Default OFF - the whole VR path is inert unless explicitly
 * requested, per this session's explicit "don't risk destabilizing the working monitor path"
 * requirement. Set the env var to "1" (e.g. before launching the game) to enable. */
static void VRBridge_ReadEnableFlag(void)
{
    char buf[8];
    DWORD len = GetEnvironmentVariableA("PSYVR_ENABLE_SUBMIT", buf, sizeof(buf));
    g_vrSubmitEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    LogLine("VRBridge: PSYVR_ENABLE_SUBMIT=%s -> g_vrSubmitEnabled=%d",
            (len > 0 && len < sizeof(buf)) ? buf : "(unset)", g_vrSubmitEnabled ? 1 : 0);

    /* notes/31 diagnostic-only A/B toggles - see the comment on g_vrSkipWaitPoses above. */
    len = GetEnvironmentVariableA("PSYVR_SKIP_WAITPOSES", buf, sizeof(buf));
    g_vrSkipWaitPoses = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_SKIP_PUMPEYE", buf, sizeof(buf));
    g_vrSkipPumpEye = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_DISABLE_TRACKING", buf, sizeof(buf));
    g_trackingDisabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_FAKE_POSE", buf, sizeof(buf));
    g_fakePose = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    /* notes/59: fake-pose yaw amplitude override, degrees, 0..175 (stay short of 180 - the sin/cos
     * construction is well-defined there but a full flip is not a meaningful "look behind" test). */
    len = GetEnvironmentVariableA("PSYVR_FAKE_POSE_YAW_DEG", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        float degv = 0.0f;
        if (sscanf(buf, "%f", &degv) == 1 && degv >= 0.0f && degv <= 175.0f)
            g_fakePoseYawAmpRad = degv * 0.017453293f;
    }
    if (g_fakePose)
        LogLine("VRBridge: fake-pose yaw amplitude = %.1f deg (PSYVR_FAKE_POSE_YAW_DEG, default 25.2)",
                g_fakePoseYawAmpRad * 57.29578f);

    /* notes/59 part 2: F12 level-jump opt-in + optional level-code override. */
    len = GetEnvironmentVariableA("PSYVR_LEVEL_JUMP_KEY", buf, sizeof(buf));
    g_levelJumpKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_levelJumpKeyEnabled) {
        char codeBuf[32];
        len = GetEnvironmentVariableA("PSYVR_LEVEL_JUMP_CODE", codeBuf, sizeof(codeBuf));
        if (len > 0 && len < sizeof(codeBuf)) {
            _snprintf(g_levelJumpCode, sizeof(g_levelJumpCode), "workresource\\levels\\%s.plb", codeBuf);
            g_levelJumpCode[sizeof(g_levelJumpCode) - 1] = '\0';
        }
        LogLine("VRBridge: PSYVR_LEVEL_JUMP_KEY=1 - F12 will call SetPendingLevel(\"%s\")", g_levelJumpCode);
    }

    /* notes/67: external automation harness opt-in. Commands arrive in
     * psyvr_automation_cmds.txt next to Psychonauts.exe; results and camera
     * telemetry go to the normal proxy log. Default off - this drives real
     * engine state and is not something to leave live by accident. */
    len = GetEnvironmentVariableA("PSYVR_AUTOMATION", buf, sizeof(buf));
    g_automationEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_automationEnabled) {
        char exePath[MAX_PATH];
        char *slash;
        if (GetModuleFileNameA(NULL, exePath, sizeof(exePath))) {
            slash = strrchr(exePath, '\\');
            if (slash) {
                *(slash + 1) = '\0';
                _snprintf(g_autoCmdPath, sizeof(g_autoCmdPath), "%spsyvr_automation_cmds.txt",
                          exePath);
                g_autoCmdPath[sizeof(g_autoCmdPath) - 1] = '\0';
            }
        }
        if (g_autoCmdPath[0]) {
            FILE *cf = fopen(g_autoCmdPath, "wb");   /* clear stale commands */
            if (cf) fclose(cf);
            /* sscanf, not atoi: stdlib.h is not among this file's includes. */
            len = GetEnvironmentVariableA("PSYVR_AUTOMATION_POLL_MS", buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) {
                int v = 0;
                if (sscanf(buf, "%d", &v) == 1 && v >= 0) g_autoPollMs = (DWORD)v;
            }
            len = GetEnvironmentVariableA("PSYVR_AUTOMATION_TELEM_MS", buf, sizeof(buf));
            if (len > 0 && len < sizeof(buf)) {
                int v = 0;
                if (sscanf(buf, "%d", &v) == 1 && v >= 0) g_autoTelemetryMs = (DWORD)v;
            }
            LogLine("VRBridge: PSYVR_AUTOMATION=1 - commands from \"%s\" (poll=%lums telem=%lums). "
                    "Try: status | level CAJA | campos <x> <y> <z> | cammove <dx> <dy> <dz> | "
                    "camhold 0|1 | flag <id> <0|1>",
                    g_autoCmdPath, g_autoPollMs, g_autoTelemetryMs);
        } else {
            g_automationEnabled = FALSE;
            LogLine("VRBridge: PSYVR_AUTOMATION=1 but the command-file path could not be built - "
                    "automation inert");
        }
    }

    /* notes/67: mouse-delta injection for Candidate 1. Patched unconditionally
     * -- with no delta queued the hook is a pure pass-through, and the import
     * must be patched before the game calls DirectInput8Create (once, early),
     * so there is no later opportunity to enable it on demand. */
    InstallMouseInject();

    /* notes/62: NUMPAD9 Visibility Tree Culling flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_CULL_TOGGLE_KEY", buf, sizeof(buf));
    g_cullToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_cullToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_CULL_TOGGLE_KEY=1 - NUMPAD9 will flip the Visibility Tree "
                "Culling flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, VISTREE_CULLING_ITEM_ID);

    /* notes/64: NUMPAD8 Render Wireframe flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_WIREFRAME_TOGGLE_KEY", buf, sizeof(buf));
    g_wireframeToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_wireframeToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_WIREFRAME_TOGGLE_KEY=1 - NUMPAD8 will flip the Render "
                "Wireframe flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, RENDER_WIREFRAME_ITEM_ID);

    /* notes/65: NUMPAD7 Collision Wireframe flag toggle opt-in. */
    len = GetEnvironmentVariableA("PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY", buf, sizeof(buf));
    g_collisionWireframeToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_collisionWireframeToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_COLLISION_WIREFRAME_TOGGLE_KEY=1 - NUMPAD7 will flip the "
                "Collision Wireframe flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, COLLISION_WIREFRAME_ITEM_ID);

    /* notes/66: NUMPAD6 Collision Spheres flag toggle opt-in (positive-control tool). */
    len = GetEnvironmentVariableA("PSYVR_COLLISION_SPHERES_TOGGLE_KEY", buf, sizeof(buf));
    g_collisionSpheresToggleKeyEnabled = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_collisionSpheresToggleKeyEnabled)
        LogLine("VRBridge: PSYVR_COLLISION_SPHERES_TOGGLE_KEY=1 - NUMPAD6 will flip the "
                "Collision Spheres flag (engine+%d+%d)", DEBUG_FLAGS_ARRAY_OFFSET, COLLISION_SPHERES_ITEM_ID);

    /* notes/65: opt-in auto-pause suppression - see Hook_GameWndProc/PatchIATEntry comments. Does
     * BOTH the WndProc message-swallow (cheap, harmless, catches whatever DOES come through as a
     * message) AND the GetForegroundWindow IAT patch (the fix that actually matters, per this
     * session's live evidence that the pause survived with only one spurious message ever
     * swallowed - so it's very likely a per-tick poll, not message-driven). */
    len = GetEnvironmentVariableA("PSYVR_SUPPRESS_AUTOPAUSE", buf, sizeof(buf));
    g_suppressAutoPause = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_suppressAutoPause) {
        LogLine("VRBridge: PSYVR_SUPPRESS_AUTOPAUSE=1 - will subclass the game window to swallow "
                "focus-loss messages once CreateDevice provides a window handle");
        BOOL iatOk = PatchIATEntry("user32.dll", "GetForegroundWindow", (void *)Hook_GetForegroundWindow);
        LogLine("AutoPauseSuppress: GetForegroundWindow IAT patch %s", iatOk ? "OK" : "FAILED (entry not found)");
    }

    len = GetEnvironmentVariableA("PSYVR_HT_TEST_SHIFT", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) g_htTestShift = (float)atof(buf);
    len = GetEnvironmentVariableA("PSYVR_HT_DEBUG", buf, sizeof(buf));
    g_htDebug = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_htTestShift != 0.0f || g_htDebug)
        LogLine("VRBridge: notes/52 composition-order test - PSYVR_HT_TEST_SHIFT=%.1f PSYVR_HT_DEBUG=%d",
                g_htTestShift, g_htDebug ? 1 : 0);
    len = GetEnvironmentVariableA("PSYVR_FP_FORCE_ACTIVE", buf, sizeof(buf));
    g_fpForceActive = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_fpForceActive) LogLine("VRBridge: PSYVR_FP_FORCE_ACTIVE=1 - FP active from startup (notes/52 X1 isolation test)");
    len = GetEnvironmentVariableA("PSYVR_FP_FORCE_RAZ", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        float dx = 0, dy = 0, dz = 0;
        if (sscanf(buf, "%f,%f,%f", &dx, &dy, &dz) == 3) {
            g_razForceValid = TRUE;
            g_razForceOffX = dx; g_razForceOffY = dy; g_razForceOffZ = dz;
            LogLine("VRBridge: PSYVR_FP_FORCE_RAZ=%.1f,%.1f,%.1f - razWorld forced to baseEye+offset each frame (notes/52)", dx, dy, dz);
        }
    }
    len = GetEnvironmentVariableA("PSYVR_RAZLOCK_STATS", buf, sizeof(buf));
    g_razLockStats = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_razLockStats) LogLine("VRBridge: PSYVR_RAZLOCK_STATS=1 - logging Raz-lock hit rate / loss events each second (notes/53)");

    /* notes/47: first-person prototype. */
    len = GetEnvironmentVariableA("PSYVR_FIRST_PERSON", buf, sizeof(buf));
    g_firstPerson = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    {
        char fb[16];
        len = GetEnvironmentVariableA("PSYVR_FP_FORWARD", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v >= 0.0f && v <= 4.0f) g_fpForward = v;
        }
        len = GetEnvironmentVariableA("PSYVR_FP_HEIGHT", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v > -500.0f && v < 500.0f) g_fpHeight = v;
        }
        len = GetEnvironmentVariableA("PSYVR_FP_SMOOTH", fb, sizeof(fb));
        if (len > 0 && len < sizeof(fb)) {
            float v = 0.0f;
            if (sscanf(fb, "%f", &v) == 1 && v >= 0.02f && v <= 1.0f) g_fpSmooth = v;
        }
    }
    len = GetEnvironmentVariableA("PSYVR_FP_PROBE", buf, sizeof(buf));
    g_fpProbe = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_fpProbe) LogLine("VRBridge: PSYVR_FP_PROBE=1 - logging recovered Raz world origin vs at/eye (notes/49)");
    if (g_firstPerson)
        LogLine("VRBridge: FIRST PERSON on (PSYVR_FIRST_PERSON) - forward=%.2f x eye->at, height=%.1fwu; Raz not hidden yet (notes/47)",
                g_fpForward, g_fpHeight);
    if (g_trackingDisabled || g_fakePose) {
        LogLine("VRBridge: tracking flags - g_trackingDisabled=%d g_fakePose=%d",
                g_trackingDisabled ? 1 : 0, g_fakePose ? 1 : 0);
    }

    len = GetEnvironmentVariableA("PSYVR_DUMP_EYES", buf, sizeof(buf));
    g_dumpEyes = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_dumpEyes) LogLine("VRBridge: PSYVR_DUMP_EYES=1 - eye/backbuffer BMP dumps every ~5s to %%TEMP%%");
    len = GetEnvironmentVariableA("PSYVR_TRACE_FRAME", buf, sizeof(buf));
    g_traceFrames = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_traceFrames) LogLine("VRBridge: PSYVR_TRACE_FRAME=1 - one-frame RT/Clear/draw traces every ~5s");
    len = GetEnvironmentVariableA("PSYVR_REG_HISTO", buf, sizeof(buf));
    g_regHisto = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    len = GetEnvironmentVariableA("PSYVR_SHADER_DUMP", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf) && buf[0] == '1') g_regHisto = TRUE; /* notes/52: needs the dump-to-disk machinery on */
    len = GetEnvironmentVariableA("PSYVR_BONE_PROBE", buf, sizeof(buf));
    g_boneProbe = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_boneProbe) LogLine("VRBridge: PSYVR_BONE_PROBE=1 - throttled bone-palette upload logging (notes/44 recon)");
    len = GetEnvironmentVariableA("PSYVR_BONE_DUMP", buf, sizeof(buf));
    g_boneDump = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_boneDump) LogLine("VRBridge: PSYVR_BONE_DUMP=1 - shoulder-anchor bone hunt: per-draw origins + 32-bone translations (notes/51)");
    len = GetEnvironmentVariableA("PSYVR_SHADER_DUMP", buf, sizeof(buf));
    g_shaderDump = (len > 0 && len < sizeof(buf) && buf[0] == '1');
    if (g_shaderDump) LogLine("VRBridge: PSYVR_SHADER_DUMP=1 - will identify which PSYVR_REG_HISTO .bin dump is the skinned (c96) world shader, for offline disassembly (notes/52, playbook Phase 3.3)");
    if (g_regHisto) LogLine("VRBridge: PSYVR_REG_HISTO=1 - vertex-shader-constant register histogram every ~5s");

    {
        char dbuf[16];
        len = GetEnvironmentVariableA("PSYVR_UI_DEPTH", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1 && v >= 0.0f && v < 100000.0f)
                g_uiDepthWorld = v;
        }
        LogLine("VRBridge: UI depth = %.0f world units (PSYVR_UI_DEPTH; 0 = UI stays at infinity)", g_uiDepthWorld);

        /* notes/37: FOV multiplier, clamped to a sane range. 1.0 = untouched. */
        /* notes/59: ceiling temporarily raised 2.5->4.0 to test candidate-3 (widen the cull/render
         * margin further) against the void-behind-player bug, per notes/40's mitigation #3 - this
         * is exploratory headroom for that specific test, not a claim that values this high are a
         * good shipping default (see notes/59 for the GPU-cost tradeoff and actual measured result). */
        /* notes/67 (2026-08-28): ceiling lowered 4.0 -> 3.4 on measured evidence.
         * A perspective projection is built from tan(fovy/2) and DEGENERATES at
         * fovy = 180 deg, where the half-angle hits 90, tan goes infinite and
         * then NEGATIVE - inverting the projection.
         *
         * This build's base fovy is 0.9076 rad (52 deg), read from the live BPM
         * cache (rawFov=312 -> fovy=2.7227 at scale 3.0). So:
         *     scale 3.0  -> 156 deg   extreme but valid, measured best
         *     scale 3.46 -> 180 deg   SINGULARITY
         *     scale 4.0  -> 208 deg   inverted; measured WORSE than 3.0
         * The old 4.0 ceiling therefore let a user configure a mathematically
         * broken projection, and the void measurement at 4.0 was recording that
         * breakage rather than a real plateau. Capped at 3.4 (175 deg) to stay
         * just under the wall. */
        len = GetEnvironmentVariableA("PSYVR_FOV_SCALE", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1) {
                if (v >= 0.5f && v <= 3.4f) {
                    g_fovScaleAsm = v;
                } else if (v > 3.4f) {
                    g_fovScaleAsm = 3.4f;
                    LogLine("VRBridge: PSYVR_FOV_SCALE=%.2f exceeds the 3.4 ceiling and would push "
                            "fovy past 180 deg (projection inverts) - clamped to 3.4", v);
                }
            }
        }
        LogLine("VRBridge: FOV scale = %.2f (PSYVR_FOV_SCALE, 0.5..4.0 - ceiling raised from 2.5 "
                "for notes/59's void-test headroom; 1.0 = game default)", g_fovScaleAsm);

        /* notes/43: EXPERIMENTAL per-draw UI viewport shrink, OFF by default (1.0). The notes/42
         * auto-shrink idea failed live: the game's fullscreen overlays (fades, pause/menu
         * backdrops, brightness passes) share the exact UI shader signature, so shrinking every
         * UI draw crushed them into the frame center ("everything dark"). Left as an opt-in
         * absolute factor until a per-draw fullscreen-vs-element classifier exists. */
        len = GetEnvironmentVariableA("PSYVR_UI_SCALE", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf)) {
            float v = 0.0f;
            if (sscanf(dbuf, "%f", &v) == 1 && v >= 0.25f && v <= 1.0f)
                g_uiVpScale = v;
        }
        if (g_uiVpScale != 1.0f)
            LogLine("VRBridge: EXPERIMENTAL UI viewport shrink = %.2f (PSYVR_UI_SCALE; known to break fullscreen overlays)", g_uiVpScale);

        /* notes/43: tangent-matched submit crop, ON by default whenever real HMD geometry is
         * available (see VRBridge_SubmitBounds). PSYVR_SUBMIT_BOUNDS=0 restores the old
         * full-texture submit (compositor stretches the frame onto the lens frustum = zoom). */
        len = GetEnvironmentVariableA("PSYVR_SUBMIT_BOUNDS", dbuf, sizeof(dbuf));
        if (len > 0 && len < sizeof(dbuf) && dbuf[0] == '0')
            g_submitBoundsEnabled = FALSE;
        LogLine("VRBridge: submit bounds = %s (PSYVR_SUBMIT_BOUNDS; tangent-matched crop kills the FOV-scale zoom)",
                g_submitBoundsEnabled ? "ON" : "OFF");
    }

    /* notes/35: eye render scale. Default 2x when the VR path is on (the headset wants far more
     * than the game's native resolution), 1x otherwise (monitor-only users keep byte-identical
     * behavior). PSYVR_RENDER_SCALE=1..4 overrides either way. */
    g_eyeScale = g_vrSubmitEnabled ? 2 : 1;
    len = GetEnvironmentVariableA("PSYVR_RENDER_SCALE", buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf) && buf[0] >= '1' && buf[0] <= '4') {
        g_eyeScale = (UINT)(buf[0] - '0');
    }
    LogLine("VRBridge: eye render scale = %ux (PSYVR_RENDER_SCALE%s)", g_eyeScale,
            (len > 0) ? " set" : " defaulted");
    if (g_vrSkipWaitPoses || g_vrSkipPumpEye) {
        LogLine("VRBridge: diagnostic bypass flags - g_vrSkipWaitPoses=%d g_vrSkipPumpEye=%d",
                g_vrSkipWaitPoses ? 1 : 0, g_vrSkipPumpEye ? 1 : 0);
    }
}
