static BOOL AutoReadCameraPos(float out[3])
{
    void *cam = AutoGetCamera();
    if (!cam) return FALSE;
    memcpy(out, (char *)cam + PSY_CAM_POS_OFFSET, sizeof(float) * 3);
    return TRUE;
}

static BOOL AutoWriteCameraPos(const float p[3])
{
    void *cam = AutoGetCamera();
    if (!cam) return FALSE;
    memcpy((char *)cam + PSY_CAM_POS_OFFSET, p, sizeof(float) * 3);
    *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
    return TRUE;
}

static void AutoRunCommand(const char *cmd)
{
    float pos[3];
    char  codeBuf[64];
    float a = 0.0f, b = 0.0f, c = 0.0f;
    int   id = 0, val = 0;

    LogLine("Auto: BEGIN \"%s\"", cmd);

    if (_strnicmp(cmd, "level ", 6) == 0) {
        void *engine = *(void **)0x78BC20;
        if (sscanf(cmd + 6, "%31s", codeBuf) == 1 && engine) {
            char path[128];
            _snprintf(path, sizeof(path), "workresource\\levels\\%s.plb", codeBuf);
            path[sizeof(path) - 1] = '\0';
            ((void (__thiscall *)(void *, const char *, BOOL))0x4FFA40)(engine, path, TRUE);
            LogLine("Auto: END   \"%s\" -> SetPendingLevel(\"%s\")", cmd, path);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (bad code or engine NULL)", cmd);
        }

    } else if (_strnicmp(cmd, "campos ", 7) == 0) {
        if (sscanf(cmd + 7, "%f %f %f", &a, &b, &c) == 3) {
            pos[0] = a; pos[1] = b; pos[2] = c;
            g_camHoldPos[0] = a; g_camHoldPos[1] = b; g_camHoldPos[2] = c;
            LogLine("Auto: END   \"%s\" -> %s", cmd,
                    AutoWriteCameraPos(pos) ? "written" : "FAILED (no camera)");
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: campos <x> <y> <z>)", cmd);
        }

    } else if (_strnicmp(cmd, "cammove ", 8) == 0) {
        if (sscanf(cmd + 8, "%f %f %f", &a, &b, &c) == 3 && AutoReadCameraPos(pos)) {
            pos[0] += a; pos[1] += b; pos[2] += c;
            g_camHoldPos[0] = pos[0]; g_camHoldPos[1] = pos[1]; g_camHoldPos[2] = pos[2];
            LogLine("Auto: END   \"%s\" -> %s, now %.1f %.1f %.1f", cmd,
                    AutoWriteCameraPos(pos) ? "written" : "FAILED",
                    pos[0], pos[1], pos[2]);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (bad args or no camera)", cmd);
        }

    /* notes/67: THE void test. Yaws the game camera about world Z, applied
     * before the engine renders, so its culling follows. If geometry appears
     * when we look behind the player, Candidate 1 works and the void is
     * curable rather than merely mitigable. */
    /* notes/67: queue a synthetic mouse delta. Deltas are RELATIVE, so this is
     * one-shot: the next GetDeviceState poll consumes it and the camera turns
     * by that much, rather than spinning continuously. */
    /* notes/67: arm the DirectInput probe for N seconds. Move the mouse (and
     * the controller sticks) during the window; the report names every device
     * the game polled, how often, and whether real motion arrived. */
    } else if (_strnicmp(cmd, "mouseprobe ", 11) == 0) {
        int secs = 0;
        if (sscanf(cmd + 11, "%d", &secs) == 1 && secs > 0 && secs <= 120) {
            memset(g_diProbe, 0, sizeof(g_diProbe));
            g_diProbeUntil = GetTickCount() + (DWORD)(secs * 1000);
            InterlockedExchange(&g_diProbeActive, 1);
            LogLine("Auto: END   \"%s\" -> probe armed for %d s. MOVE THE MOUSE NOW. "
                    "Hook installed=%d, known mouse device=%p",
                    cmd, secs, g_pRealGetDeviceState != NULL, g_diMouseDevice);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: mouseprobe <1..120 seconds>)", cmd);
        }

    /* notes/67: drive a gamepad axis. THIS is the working injection path -
     * the game polls the pad every frame and never polls the mouse through
     * DirectInput. Index: 0=lX 1=lY 2=lZ 3=lRx 4=lRy 5=lRz.
     *   padaxis 3 40000   hold axis lRx at 40000
     *   padaxis 3 off     release it back to the real stick */
    } else if (_strnicmp(cmd, "padaxis ", 8) == 0) {
        int idx = 0; char valBuf[32];
        if (sscanf(cmd + 8, "%d %31s", &idx, valBuf) == 2 && idx >= 0 && idx < 6) {
            if (_stricmp(valBuf, "off") == 0) {
                g_padOverrideOn[idx] = 0;
                LogLine("Auto: END   \"%s\" -> axis %d released to real hardware", cmd, idx);
            } else {
                int v = 0;
                if (sscanf(valBuf, "%d", &v) == 1) {
                    g_padOverrideVal[idx] = (LONG)v;
                    g_padOverrideOn[idx]  = 1;
                    LogLine("Auto: END   \"%s\" -> axis %d held at %d", cmd, idx, v);
                } else {
                    LogLine("Auto: END   \"%s\" -> FAILED (value must be a number or 'off')", cmd);
                }
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: padaxis <0..5> <value|off>)", cmd);
        }

    } else if (_stricmp(cmd, "padrelease") == 0) {
        int a; for (a = 0; a < 6; a++) g_padOverrideOn[a] = 0;
        LogLine("Auto: END   \"padrelease\" -> all gamepad axes released to real hardware");

    } else if (_strnicmp(cmd, "mousedx ", 8) == 0) {
        int v = 0;
        if (sscanf(cmd + 8, "%d", &v) == 1) {
            InterlockedExchange(&g_injectMouseDX, (LONG)v);
            LogLine("Auto: END   \"%s\" -> queued mouse dX=%d (hooked=%d)", cmd, v,
                    g_pRealGetDeviceState != NULL);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: mousedx <delta>)", cmd);
        }

    } else if (_strnicmp(cmd, "mousedy ", 8) == 0) {
        int v = 0;
        if (sscanf(cmd + 8, "%d", &v) == 1) {
            InterlockedExchange(&g_injectMouseDY, (LONG)v);
            LogLine("Auto: END   \"%s\" -> queued mouse dY=%d (hooked=%d)", cmd, v,
                    g_pRealGetDeviceState != NULL);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: mousedy <delta>)", cmd);
        }

    } else if (_strnicmp(cmd, "camyaw ", 7) == 0) {
        if (sscanf(cmd + 7, "%f", &a) == 1) {
            g_camYawDeg = a;
            LogLine("Auto: END   \"%s\" -> camera yaw = %.1f deg (0 = off), site %d", cmd, a, g_camYawSite);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camyaw <degrees>)", cmd);
        }

    } else if (_strnicmp(cmd, "playerpos", 9) == 0) {
        /* notes/69: report Raz's world position straight off the pointer chain.
         * Run this FIRST on the next launch - it is the one-command check that
         * the statically-derived chain is real. Sanity test: the numbers must
         * move when Raz walks and must NOT move when only the camera turns. */
        float pp[3];
        if (PsyGetPlayerPos(pp))
            LogLine("Auto: END   \"%s\" -> player %.2f %.2f %.2f", cmd, pp[0], pp[1], pp[2]);
        else
            LogLine("Auto: END   \"%s\" -> FAILED (no player object; loading or menu?)", cmd);

    } else if (_strnicmp(cmd, "headpos", 7) == 0) {
        /* notes/73: the MEASURED eye height. Prints, in one line each:
         *   - which candidate object owned the bone array (the one thing the
         *     static read could not settle - see PsyFindPlayerBoneOwner);
         *   - the head bone's world position;
         *   - playerpos beside it, and the difference, which IS the eye height
         *     in engine units. Feed the up-axis figure to `fpheight'.
         *   - node+0xB8, the parent pointer. Zero here while riding a moving
         *     platform / the levitation ball / while grabbed is what retires
         *     the last playerpos caveat; non-zero names the case that breaks
         *     an FP camera built on this chain.
         * Each stage logs BEFORE it calls into the engine, so if the game dies
         * the log names the exact stage rather than leaving a mystery. */
        const char *which = NULL;
        unsigned boneCount = 0;
        void *owner = PsyFindPlayerBoneOwner(&which, &boneCount);
        char *engine = *(char **)0x78BC20;
        char *player = PsyPtrLooksSane(engine) ? *(char **)(engine + PSY_PLAYER_OFFSET) : NULL;
        char *node   = PsyPtrLooksSane(player) ? *(char **)(player + PSY_PLAYEROBJ_OFFSET) : NULL;

        if (PsyPtrLooksSane(node)) {
            void *parent = *(void **)(node + PSY_NODE_PARENT_OFFSET);
            LogLine("Auto:       node+0xB8 (parent) = %p  %s", parent,
                    parent ? "NON-NULL - playerpos is LOCAL to this parent, see notes/73"
                           : "NULL - playerpos is world space here");
        }

        if (!owner) {
            LogLine("Auto: END   \"%s\" -> FAILED (no bone-owning object off the player; "
                    "loading, a menu, or none of the four candidates shape-checked)", cmd);
        } else {
            float boneMtx[16], worldMtx[16], pp[3];
            float *ownerMtx, *world;
            void *bone;

            LogLine("Auto:       bone owner = %s (%p), %u bones", which, owner, boneCount);
            LogLine("Auto:       calling FindBoneByName(owner, \"headJA_1\") @0x00438B70");
            bone = ((PsyFindBone_t)0x00438B70)(owner, (const char *)0x00703F94);
            if (!bone) {
                LogLine("Auto: END   \"%s\" -> FAILED (owner shape-checked but has no "
                        "\"headJA_1\" bone - wrong object, or a rig without that name)", cmd);
            } else {
                LogLine("Auto:       bone = %p; calling GetBoneMatrix @0x00492B70", bone);
                memset(boneMtx, 0, sizeof(boneMtx));
                ((PsyBoneMatrix_t)0x00492B70)(bone, boneMtx);

                LogLine("Auto:       calling GetOwnerWorldMatrix @0x00438390");
                ownerMtx = ((PsyOwnerWorldMtx_t)0x00438390)(owner);

                if (!PsyPtrLooksSane(ownerMtx)) {
                    /* No owner transform: the bone matrix is the best we have.
                     * Say so rather than silently reporting local coordinates
                     * as if they were world space. */
                    LogLine("Auto: END   \"%s\" -> bone-LOCAL only (owner world matrix "
                            "unavailable) trans %.2f %.2f %.2f - NOT an eye height",
                            cmd, boneMtx[12], boneMtx[13], boneMtx[14]);
                } else {
                    LogLine("Auto:       calling MatMul @0x00433E50");
                    memset(worldMtx, 0, sizeof(worldMtx));
                    world = ((PsyMatMul_t)0x00433E50)(ownerMtx, worldMtx, boneMtx);
                    if (!PsyPtrLooksSane(world)) world = worldMtx;

                    LogLine("Auto:       head world %.2f %.2f %.2f", world[12], world[13], world[14]);
                    if (PsyGetPlayerPos(pp)) {
                        LogLine("Auto: END   \"%s\" -> head %.2f %.2f %.2f | player %.2f %.2f %.2f "
                                "| EYE HEIGHT dx %.2f dy %.2f dz %.2f (up axis is Y per notes/71; "
                                "feed that to fpheight)", cmd,
                                world[12], world[13], world[14], pp[0], pp[1], pp[2],
                                world[12] - pp[0], world[13] - pp[1], world[14] - pp[2]);
                    } else {
                        LogLine("Auto: END   \"%s\" -> head %.2f %.2f %.2f (no playerpos to "
                                "difference against)", cmd, world[12], world[13], world[14]);
                    }
                }
            }
        }

    } else if (_strnicmp(cmd, "fpcam ", 6) == 0) {
        /* notes/69: first person by moving the CAMERA (pre-culling), not the
         * render transform (post-culling, which notes/68 refuted). */
        g_fpCamOn = (cmd[6] == '1');
        LogLine("Auto: END   \"%s\" -> fpcam = %d (height %.1f on axis %d, forward %.1f)",
                cmd, g_fpCamOn, g_fpCamHeight, g_fpCamUpAxis, g_fpCamForward);
        /* fpcam and camhold both own camera+0x08 and would silently fight:
         * camhold writes from the automation tick (AfterBoth) while fpcam
         * writes at BeforeEye1, so fpcam always wins and camhold/campos/cammove
         * would appear broken rather than overridden. Say so instead. */
        if (g_fpCamOn && g_camHold)
            LogLine("Auto:       NOTE fpcam overrides camhold - camhold, campos and cammove "
                    "cannot move the camera while fpcam is on. Use 'camhold 0' + 'fpcam 0' "
                    "to get the free camera back.");

    } else if (_strnicmp(cmd, "fpheight ", 9) == 0) {
        if (sscanf(cmd + 9, "%f", &a) == 1 && a >= -500.0f && a <= 500.0f) {
            g_fpCamHeight = a;
            LogLine("Auto: END   \"%s\" -> fp eye height = %.1f wu", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: fpheight <-500..500>)", cmd);
        }

    } else if (_strnicmp(cmd, "fpforward ", 10) == 0) {
        if (sscanf(cmd + 10, "%f", &a) == 1 && a >= -500.0f && a <= 500.0f) {
            g_fpCamForward = a;
            LogLine("Auto: END   \"%s\" -> fp forward offset = %.1f wu", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: fpforward <-500..500>)", cmd);
        }

    } else if (_strnicmp(cmd, "fpaxis ", 7) == 0) {
        /* The dossier warns this engine's levels do not share one up axis, so
         * the height axis is a runtime choice rather than a build-time guess. */
        int ax = cmd[7] - '0';
        if (ax >= 0 && ax <= 2) {
            g_fpCamUpAxis = ax;
            LogLine("Auto: END   \"%s\" -> fp up axis = %d (0=X 1=Y 2=Z)", cmd, ax);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: fpaxis <0|1|2>)", cmd);
        }

    } else if (_strnicmp(cmd, "camfollow ", 10) == 0) {
        /* notes/68 part 3: drive the game's own camera transform from live head
         * yaw, before culling. This is the void fix as a player experiences it. */
        g_camFollowHead = (cmd[10] == '1');
        if (!g_camFollowHead) { g_basisSnapValid = 0; g_basisLastWriteValid = 0; }
        LogLine("Auto: END   \"%s\" -> camera follows head = %d (scale %.2f, head yaw now %.1f deg)",
                cmd, g_camFollowHead, g_camFollowScale, g_headYawDeg);

    } else if (_strnicmp(cmd, "camfollowscale ", 15) == 0) {
        /* Sign/gain, tunable live: the correct sign is a convention question and
         * is best settled by looking through the headset rather than guessed.
         * -1 mirrors, values <1 damp the turn, >1 amplify it. */
        if (sscanf(cmd + 15, "%f", &a) == 1 && a >= -4.0f && a <= 4.0f) {
            g_camFollowScale = a;
            LogLine("Auto: END   \"%s\" -> follow scale = %.2f", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camfollowscale <-4.0..4.0>)", cmd);
        }

    } else if (_strnicmp(cmd, "cambasisyaw ", 12) == 0) {
        /* notes/68 part 2: rotate the whole camera basis (right/up/forward)
         * about world up, held every frame at BeforeEye1 so it lands before the
         * engine culls. 0 = off. This is the orthogonal-preserving version of
         * the c2-only write, which skewed the basis and broke the image. */
        if (sscanf(cmd + 12, "%f", &a) == 1) {
            g_camBasisYawDeg = a;
            g_camBasisYawOn = (a != 0.0f);
            /* Re-snapshot on every re-arm: the reference must be the engine's
             * CURRENT basis, or a yaw issued after the player moved would be
             * measured from a stale orientation. Clearing it on disarm also
             * lets the engine's own value come back. */
            g_basisSnapValid = 0;
            LogLine("Auto: END   \"%s\" -> basis yaw = %.1f deg (%s, absolute from a fresh snapshot)", cmd, a,
                    g_camBasisYawOn ? "ON" : "OFF");
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: cambasisyaw <degrees>)", cmd);
        }

    } else if (_strnicmp(cmd, "camyawsite ", 11) == 0) {
        /* notes/68: WHERE the write lands is the open question, not the maths.
         * 1 = BeforeEye1 (already refuted 2026-08-27 - kept so the negative can
         *     be reproduced deliberately rather than assumed)
         * 2 = BeforeEye2 - after the first real CandB RETURNS
         * 3 = AfterBoth  - after both CandB invocations */
        int v = 0;
        if (sscanf(cmd + 11, "%d", &v) == 1 && v >= 1 && v <= 3) {
            g_camYawSite = v;
            LogLine("Auto: END   \"%s\" -> yaw write site = %d (%s)", cmd, v,
                    v == 1 ? "BeforeEye1 (known-refuted)" :
                    v == 2 ? "BeforeEye2 (after 1st CandB returns)" :
                             "AfterBoth (after both CandB calls)");
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camyawsite <1|2|3>)", cmd);
        }

    } else if (_strnicmp(cmd, "camprobe ", 9) == 0) {
        /* Log camera world-matrix row 0 at every site for N frames. This is the
         * measurement that makes the result unambiguous: it shows WHEN the
         * engine rewrites the matrix, instead of leaving us to infer it from
         * whether the picture changed. */
        int v = 0;
        if (sscanf(cmd + 9, "%d", &v) == 1 && v > 0 && v <= 240) {
            g_camProbeFrames = v;
            LogLine("Auto: END   \"%s\" -> camera probe armed for %d frames (yaw %.1f, site %d)",
                    cmd, v, g_camYawDeg, g_camYawSite);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camprobe <1..240>)", cmd);
        }

    } else if (_strnicmp(cmd, "lookhold ", 9) == 0) {
        g_lookHold = (cmd[9] == '1');
        LogLine("Auto: END   \"%s\" -> lookhold=%d dir %.4f %.4f %.4f", cmd, (int)g_lookHold,
                g_lookHoldDir[0], g_lookHoldDir[1], g_lookHoldDir[2]);

    } else if (_strnicmp(cmd, "camhold ", 8) == 0) {
        g_camHold = (cmd[8] == '1');
        if (g_camHold && !AutoReadCameraPos(g_camHoldPos))
            LogLine("Auto: camhold on, but no camera yet - target stays as last set");
        LogLine("Auto: END   \"%s\" -> camhold=%d target %.1f %.1f %.1f", cmd, (int)g_camHold,
                g_camHoldPos[0], g_camHoldPos[1], g_camHoldPos[2]);
        /* See the matching note on fpcam: both own camera+0x08, and fpcam wins
         * because it writes later in the frame. Warn rather than let camhold
         * look broken. */
        if (g_camHold && g_fpCamOn)
            LogLine("Auto:       NOTE fpcam is ON and overrides camhold every frame - "
                    "run 'fpcam 0' first if you want the free camera back.");

    } else if (_strnicmp(cmd, "flag ", 5) == 0) {
        void *engine = *(void **)0x78BC20;
        if (sscanf(cmd + 5, "%d %d", &id, &val) == 2 && engine && id >= 0 && id < 256) {
            unsigned char *flag = (unsigned char *)engine + DEBUG_FLAGS_ARRAY_OFFSET + id;
            *flag = (unsigned char)(val ? 1 : 0);
            LogLine("Auto: END   \"%s\" -> debug flag %d = %d", cmd, id, (int)*flag);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: flag <id 0-255> <0|1>)", cmd);
        }

    /* notes/67: live-tunable UI knobs. These were env-var-only, so every
     * experiment cost a relaunch (and a relaunch costs the user, since only
     * they may start the game). They are plain floats consumed by existing
     * per-draw code, so changing them mid-session is safe and takes effect on
     * the next frame. */
    } else if (_strnicmp(cmd, "uidepth ", 8) == 0) {
        if (sscanf(cmd + 8, "%f", &a) == 1 && a >= 0.0f) {
            g_uiDepthWorld = a;
            LogLine("Auto: END   \"%s\" -> UI depth = %.0f world units (0 = infinity)", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: uidepth <world units, 0 = off>)", cmd);
        }

    } else if (_strnicmp(cmd, "dlgdepth ", 9) == 0) {
        if (sscanf(cmd + 9, "%f", &a) == 1 && a >= 0.0f) {
            g_dlgDepthWorld = a;
            LogLine("Auto: END   \"%s\" -> dialogue UI depth = %.0f world units "
                    "(0 = share the normal UI depth of %.0f)", cmd, a, g_uiDepthWorld);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: dlgdepth <world units, 0 = off>)", cmd);
        }

    } else if (_strnicmp(cmd, "uiscale ", 8) == 0) {
        if (sscanf(cmd + 8, "%f", &a) == 1 && a > 0.0f) {
            g_uiVpScale = a;
            LogLine("Auto: END   \"%s\" -> UI viewport scale = %.2f", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: uiscale <factor > 0>)", cmd);
        }

    } else if (_strnicmp(cmd, "trace ", 6) == 0) {
        /* Arms the notes/35 one-frame trace, which includes the TRACE-UI lines
         * carrying each UI draw's shader index and vertex positions - the
         * per-draw identity needed to tell one HUD element from another. */
        g_traceFrames = (cmd[6] == '1');
        LogLine("Auto: END   \"%s\" -> one-frame trace %s", cmd,
                g_traceFrames ? "ARMED (one frame every ~5s)" : "off");

    /* notes/67: FOV scale, live. g_fovScaleAsm is read per call by the injected
     * stub, so a change lands on the next frame. Runtime control turns the
     * void A/B that 00-status lists as the top open item from "one relaunch per
     * value" (and only the user can relaunch) into one session. */
    } else if (_strnicmp(cmd, "fov ", 4) == 0) {
        /* Same 3.4 ceiling as the env var - past ~3.46 the projection inverts. */
        if (sscanf(cmd + 4, "%f", &a) == 1 && a >= 0.5f && a <= 3.4f) {
            g_fovScaleAsm = a;
            LogLine("Auto: END   \"%s\" -> FOV scale = %.2f", cmd, a);
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: fov <0.5..4.0>)", cmd);
        }

    /* notes/67: READ-ONLY dump of the camera's orientation matrix at
     * camera+0x20, decoded statically in recon/2026-08-27 but never yet
     * exercised. Reading first: the matrix convention (row vs column major,
     * axis order, handedness) is NOT known, and writing a rotation built on a
     * guessed convention would just produce a confusing wrong result. Sampling
     * it while the game turns the camera is how the convention gets settled. */
    /* notes/67: WRITE the camera's facing direction. camera+0x20 is three
     * floats, not the 3x3 matrix the static read first assumed - the live
     * dump settled that (m[0..2] came back unit-length, m[3..8] were
     * unrelated neighbouring fields including a stray 104.0). So turning the
     * camera is writing a normalized direction; there is no matrix
     * convention, handedness or axis order left to guess.
     *
     * Why this matters beyond a free camera: 00-status lists "feed HMD yaw
     * into the game camera" as Candidate 1 for the black void, marked
     * unimplementable - which looks like it was judged when no way to set the
     * camera's facing was known. Pulling the camera 4000 units back showed the
     * engine culls and renders relative to the camera object we write, so if
     * facing is writable too, the game should cull toward wherever the head is
     * pointing and leave no unrendered region to fall into. Untested. */
    } else if (_strnicmp(cmd, "camlook ", 8) == 0) {
        if (sscanf(cmd + 8, "%f %f %f", &a, &b, &c) == 3) {
            float len = (float)sqrt(a*a + b*b + c*c);
            if (len < 1e-6f) {
                LogLine("Auto: END   \"%s\" -> FAILED (zero-length direction)", cmd);
            } else {
                void *cam = AutoGetCamera();
                if (cam) {
                    PsySetLookDir(cam, a / len, b / len, c / len);
                    g_lookHoldDir[0] = a / len;
                    g_lookHoldDir[1] = b / len;
                    g_lookHoldDir[2] = c / len;
                    LogLine("Auto: END   \"%s\" -> dir = %.4f %.4f %.4f (at +0x%03X stride 0x%X)",
                            cmd, g_lookHoldDir[0], g_lookHoldDir[1], g_lookHoldDir[2],
                            PSY_CAM_LOOKDIR_OFF, PSY_CAM_LOOKDIR_STRIDE);
                } else {
                    LogLine("Auto: END   \"%s\" -> FAILED (no camera)", cmd);
                }
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: camlook <x> <y> <z>)", cmd);
        }

    /* notes/67: dump raw floats from the camera object, to FIND the field the
     * renderer actually uses for view direction.
     *
     * Measured 2026-08-27, and it rules out the obvious candidates:
     *  - camera+0x08 (position) IS used: writes visibly move the view.
     *  - camera+0x20 is NOT: a write survives in memory unread and unchanged
     *    by the game, yet the rendered view does not move at all. It is what
     *    SetCameraOrientation writes, but not what the renderer consumes.
     *  - the camera is NOT a look-at rig: sliding it 2500 units sideways
     *    translated the view and let Raz leave the frame entirely, instead of
     *    rotating to keep a target centred.
     * So facing lives in some other field. This dumps a window of the object
     * as float and hex together, to spot another unit-length vec3 or a
     * rotation matrix. Read-only.
     *
     * Usage: dump 0x0 64   (offset is bytes from the camera pointer) */
    } else if (_strnicmp(cmd, "dump ", 5) == 0) {
        unsigned int off = 0; int cnt = 0;
        if (sscanf(cmd + 5, "%x %d", &off, &cnt) == 2 && cnt > 0 && cnt <= 64 &&
            off < 0x2000) {
            void *cam = AutoGetCamera();
            if (cam) {
                int i;
                char line[512];
                for (i = 0; i < cnt; i += 4) {
                    const unsigned char *base = (const unsigned char *)cam + off + i * 4;
                    const float *f = (const float *)base;
                    const unsigned int *u = (const unsigned int *)base;
                    int n = (cnt - i) < 4 ? (cnt - i) : 4;
                    if (n == 4)
                        _snprintf(line, sizeof(line),
                                  "+0x%03X: %12.4f %12.4f %12.4f %12.4f | %08X %08X %08X %08X",
                                  off + i * 4, f[0], f[1], f[2], f[3], u[0], u[1], u[2], u[3]);
                    else if (n == 3)
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f %12.4f %12.4f | %08X %08X %08X",
                                  off + i * 4, f[0], f[1], f[2], u[0], u[1], u[2]);
                    else if (n == 2)
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f %12.4f | %08X %08X",
                                  off + i * 4, f[0], f[1], u[0], u[1]);
                    else
                        _snprintf(line, sizeof(line), "+0x%03X: %12.4f | %08X", off + i * 4, f[0], u[0]);
                    line[sizeof(line) - 1] = '\0';
                    LogLine("Auto: dump %s", line);
                }
                LogLine("Auto: END   \"%s\" -> dumped %d floats from camera+0x%X", cmd, cnt, off);
            } else {
                LogLine("Auto: END   \"%s\" -> FAILED (no camera)", cmd);
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: dump <hexOffset < 0x2000> <count 1..64>)", cmd);
        }

    /* notes/67: write a single float into the camera object, and optionally
     * HOLD it every frame.
     *
     * Why: with a verified 34.4 deg camera swing, +0x154 and +0x174 moved by
     * ~30 deg worth of RADIANS (-1.6869 -> -1.1550 and -0.9721 -> -1.5318),
     * which makes them the camera's yaw as a scalar angle - plausibly a
     * current/target pair. The stick is an ABSOLUTE CLAMPED offset, so it
     * cannot reach behind the player; writing the angle directly is the way to
     * test whether the clamp is in the input mapping or in the camera itself.
     *
     * Hold matters: the engine recomputes camera state every frame, and a
     * one-shot write to camera+0x90 was measured coming back bit-for-bit
     * identical. If a one-shot poke does nothing, try holding it. */
    } else if (_strnicmp(cmd, "pokef ", 6) == 0) {
        unsigned int off = 0; float fv = 0.0f; char holdBuf[16] = {0};
        int n = sscanf(cmd + 6, "%x %f %15s", &off, &fv, holdBuf);
        if (n >= 2 && off < 0x2000 && (off & 3) == 0) {
            void *cam = AutoGetCamera();
            if (cam) {
                float *slot = (float *)((char *)cam + off);
                float old = *slot;
                *slot = fv;
                if (n >= 3 && _stricmp(holdBuf, "hold") == 0) {
                    g_pokeHoldOff = off; g_pokeHoldVal = fv; g_pokeHoldOn = 1;
                    LogLine("Auto: END   \"%s\" -> +0x%X was %.4f now %.4f (HELD every frame)",
                            cmd, off, old, fv);
                } else {
                    LogLine("Auto: END   \"%s\" -> +0x%X was %.4f now %.4f (one-shot)",
                            cmd, off, old, fv);
                }
            } else {
                LogLine("Auto: END   \"%s\" -> FAILED (no camera)", cmd);
            }
        } else {
            LogLine("Auto: END   \"%s\" -> FAILED (expected: pokef <hexOffset, 4-aligned> <float> [hold])", cmd);
        }

    } else if (_stricmp(cmd, "pokerelease") == 0) {
        g_pokeHoldOn = 0;
        LogLine("Auto: END   \"pokerelease\" -> float hold released");

    } else if (_stricmp(cmd, "orient") == 0) {
        void *cam = AutoGetCamera();
        if (cam) {
            const float *m = (const float *)((char *)cam + 0x20);
            LogLine("Auto: END   \"orient\" -> m[0..8] = "
                    "%.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f",
                    m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        } else {
            LogLine("Auto: END   \"orient\" -> no camera available right now");
        }

    } else if (_stricmp(cmd, "status") == 0) {
        if (AutoReadCameraPos(pos))
            LogLine("Auto: END   \"status\" -> campos %.2f %.2f %.2f camhold=%d uidepth=%.0f "
                    "uiscale=%.2f trace=%d",
                    pos[0], pos[1], pos[2], (int)g_camHold, g_uiDepthWorld, g_uiVpScale,
                    (int)g_traceFrames);
        else
            LogLine("Auto: END   \"status\" -> no camera available right now");

    } else {
        LogLine("Auto: END   \"%s\" -> UNKNOWN command", cmd);
    }
}

/* Read the whole drop-file, truncate it, then run each line. Truncation
 * happens BEFORE execution deliberately: a line still in the file proves it
 * never ran, which is how XIII's crash was pinned down. */
static void AutoDrainCommands(void)
{
    char  contents[4096];
    size_t n = 0;
    FILE *f;
    char *line, *next;

    f = fopen(g_autoCmdPath, "rb");
    if (!f) return;
    n = fread(contents, 1, sizeof(contents) - 1, f);
    fclose(f);
    if (n == 0) return;
    contents[n] = '\0';

    f = fopen(g_autoCmdPath, "wb");
    if (f) fclose(f);

    line = contents;
    while (line && *line) {
        char *end;
        next = strchr(line, '\n');
        if (next) *next++ = '\0';
        /* trim */
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        end = line + strlen(line);
        while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = '\0';
        if (*line && *line != '#' && !(line[0] == '/' && line[1] == '/'))
            AutoRunCommand(line);
        line = next;
    }
}

static void AutomationTick(void)
{
    DWORD now;
    float pos[3];

    if (!g_automationEnabled) return;
    now = GetTickCount();

    /* Re-apply the held position every frame so the game's own camera update
     * cannot walk it back - this is what makes a free camera actually hold. */
    if (g_camHold) AutoWriteCameraPos(g_camHoldPos);
    if (g_lookHold) {
        void *cam = AutoGetCamera();
        /* notes/68: repointed from the dead +0x20 to the real look-direction
         * input (+0x158, stride 0x10). Writing +0x20 held a value the renderer
         * never read, which is why lookhold appeared to do nothing. */
        if (cam) PsySetLookDir(cam, g_lookHoldDir[0], g_lookHoldDir[1], g_lookHoldDir[2]);
    }

    if (now - g_autoLastPoll >= g_autoPollMs) {
        g_autoLastPoll = now;
        if (InterlockedCompareExchange(&g_autoInCommand, 1, 0) == 0) {
            AutoDrainCommands();
            InterlockedExchange(&g_autoInCommand, 0);
        }
    }

    if (g_autoTelemetryMs && now - g_autoLastTelemetry >= g_autoTelemetryMs) {
        g_autoLastTelemetry = now;
        if (AutoReadCameraPos(pos))
            LogLine("AutoTelem: campos %.2f %.2f %.2f camhold=%d", pos[0], pos[1], pos[2],
                    (int)g_camHold);
        else
            LogLine("AutoTelem: no camera (loading/menu?)");
    }
}
