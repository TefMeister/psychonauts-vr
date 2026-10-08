/* px_07b_camfollow_restore.c.inc -- head-follow: give the engine its own camera back after each
 * frame, and measure which of two faults made the 2026-09-10 wear "crazy glitchy". (/pd 2026-10-08)
 *
 * WHY. camfollow writes snapshot-rotated-by-head-yaw into camera+0x150 at BeforeEye1 and LEAVES it
 * there. The 2026-08-28 monitor test passed with a still camera, where the engine does not rewrite
 * +0x150 at all. In real play the engine rewrites it whenever the camera moves, and two things can
 * then go wrong (both [hypothesis], neither measured):
 *
 *   FEEDBACK  the engine's next update starts from OUR rotated basis (a smoothed third-person camera
 *             easing from where it "is" toward where it wants to be). PsyBasisFollowHead then sees
 *             "engine moved", snapshots that already-turned basis and turns it again. Each frame adds
 *             the head yaw once more, eased: the view swings far past the head and the camera orbits
 *             the player. That fits "stretches the world", "shaky", and the eye position moving ~470
 *             units in 8 s with nobody walking.
 *   OVERWRITE the engine rewrites +0x150 inside the first CandB, after our BeforeEye1 write, from its
 *             own angles. The head turn is then lost on every frame the camera moves and kept on the
 *             frames it does not: the view flicks between turned and not.
 *
 * WHICH STEP RUNS LAST. Our write is at BeforeEye1 (step 1); the engine's camera update runs inside
 * the real CandB (step 2) and, for the NEXT frame, again after our AfterBoth (step 3) and before the
 * next BeforeEye1. Restoring the engine's own basis at step 3 changes what the next update reads, so it
 * removes FEEDBACK. It cannot help OVERWRITE, which happens at step 2; the counters below tell the two
 * apart, so the run that tests the fix also says whether it was the right one.
 *
 * Commands: camfollowrestore 0|1 (default 1). The stats line prints every CF_STATS_EVERY frames while
 * camfollow is on, with or without the restore, so one run can compare both. */

#define CF_STATS_EVERY      120     /* frames per stats line (about 2 s at 60 fps) */
#define CF_TURNED_DEG       2.0f    /* below this applied yaw, "which basis did the engine follow" is noise */
#define CF_SAME_EPS         1e-5f   /* a float that differs by more than this was rewritten */

static int g_camFollowRestore = 1;  /* 1 = put the engine's own basis back at AfterBoth */

static float g_cfPrevSnapFwd[3], g_cfPrevYaw;
static int   g_cfPrevValid = 0;
static unsigned long g_cfFrames, g_cfRewrites, g_cfLostInPass, g_cfRestored, g_cfLateRewrite;
static double g_cfSumYaw, g_cfTowardDeg, g_cfAwayDeg;

/* Signed turn from a to b, in degrees, about the +X/+Y/+Z axis PsyApplyBasisYaw rotates about (the
 * dominant axis of the snapshot's up column, taken with a POSITIVE sign exactly as that function does,
 * so a positive head yaw reads as a positive turn whatever way this level's up column points). */
static float CfSignedTurnDeg(const float a[3], const float b[3])
{
    float ax = fabsf(g_basisSnap[1][0]), ay = fabsf(g_basisSnap[1][1]), az = fabsf(g_basisSnap[1][2]);
    float cross[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
    float c = a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    int up = 1;
    if (ax >= ay && ax >= az) up = 0;
    else if (az >= ay && az >= ax) up = 2;
    return atan2f(cross[up], c) * 57.29578f;
}

static void CfReadColumn(void *cam, int col, float out[3])
{
    const char *B = (const char *)cam + PSY_CAM_BASIS_OFF;
    int row;
    for (row = 0; row < 3; row++) out[row] = *(const float *)(B + row * PSY_CAM_BASIS_RSTRIDE + col * 4);
}

/* 1 if the basis axes in memory still equal what we last wrote (or restored). */
static int CfStillOurs(void *cam)
{
    int col;
    for (col = 0; col < 4; col++) {
        float cur[3];
        int row;
        CfReadColumn(cam, col, cur);
        for (row = 0; row < 3; row++) {
            float d = cur[row] - g_basisLastWrite[col][row];
            if (d < 0) d = -d;
            if (d > CF_SAME_EPS) return 0;
        }
    }
    return 1;
}

/* BeforeEye1, called by PsyBasisFollowHead BEFORE it re-snapshots. When the engine rewrote the basis
 * since last frame, how far did ITS OWN camera turn, and was that the way the head is turned?
 *
 * The tell for FEEDBACK is a drift, not one frame: with the head held turned and the player still, an
 * engine that builds on our write keeps turning its own camera the head's way, frame after frame, until
 * it settles far round (180 deg in tests/camfollow_test.c). At that settled point a single frame looks
 * exactly like an engine keeping its own angle, so only the running sum tells them apart. An engine that
 * keeps its own angles does not drift at all. */
static void PsyCamFollowMeasure(void *cam, int engineMoved)
{
    float fwd[3], turn;
    g_cfFrames++;
    if (!engineMoved || !g_cfPrevValid) return;
    g_cfRewrites++;
    if (fabsf(g_cfPrevYaw) < CF_TURNED_DEG) return;
    CfReadColumn(cam, 2, fwd);
    turn = CfSignedTurnDeg(g_cfPrevSnapFwd, fwd);
    if ((turn > 0) == (g_cfPrevYaw > 0)) g_cfTowardDeg += fabsf(turn);
    else g_cfAwayDeg += fabsf(turn);
}

/* BeforeEye1, called by PsyBasisFollowHead AFTER it wrote: remember the engine's forward for next frame. */
static void PsyCamFollowRemember(float yawDeg)
{
    int i;
    for (i = 0; i < 3; i++) g_cfPrevSnapFwd[i] = g_basisSnap[2][i];
    g_cfPrevYaw = yawDeg;
    g_cfPrevValid = 1;
    g_cfSumYaw += fabsf(yawDeg);
}

/* BeforeEye2: the first real CandB has returned. Was our write replaced during that pass? */
static void PsyCamFollowCheckEye2(void)
{
    void *cam;
    if (!g_camFollowHead || !g_basisLastWriteValid) return;
    cam = AutoGetCamera();
    if (cam && !CfStillOurs(cam)) g_cfLostInPass++;
}

/* AfterBoth: both eyes are drawn. Put the engine's own basis (and translation row) back so its next
 * update starts from its own value, then print the stats line now and then. */
static void PsyCamFollowAfterBoth(void)
{
    void *cam;
    if (!g_camFollowHead) return;
    cam = AutoGetCamera();
    if (cam && g_camFollowRestore && g_basisSnapValid && g_basisLastWriteValid) {
        if (!CfStillOurs(cam)) {
            g_cfLateRewrite++;          /* the engine already replaced it: leave the engine's value */
        } else {
            char *B = (char *)cam + PSY_CAM_BASIS_OFF;
            int col, row;
            for (col = 0; col < 4; col++) {
                for (row = 0; row < 3; row++) {
                    *(float *)(B + row * PSY_CAM_BASIS_RSTRIDE + col * 4) = g_basisSnap[col][row];
                    g_basisLastWrite[col][row] = g_basisSnap[col][row];
                }
                *(float *)(B + 3 * PSY_CAM_BASIS_RSTRIDE + col * 4) = g_basisSnapT[col];
            }
            g_cfRestored++;
        }
    }
    if (g_cfFrames >= CF_STATS_EVERY) {
        LogLine("CAMFOLLOW stats (restore %d): %lu frames, mean |yaw| %.1f deg; engine rewrote the camera "
                "between frames %lu times; its own camera turned %.1f deg TOWARD the head's side, %.1f away; "
                "rewritten during eye 1's pass %lu; restored %lu, engine had already replaced it %lu",
                g_camFollowRestore, g_cfFrames, g_cfSumYaw / (double)g_cfFrames, g_cfRewrites,
                g_cfTowardDeg, g_cfAwayDeg, g_cfLostInPass, g_cfRestored, g_cfLateRewrite);
        g_cfFrames = g_cfRewrites = g_cfLostInPass = g_cfRestored = g_cfLateRewrite = 0;
        g_cfSumYaw = g_cfTowardDeg = g_cfAwayDeg = 0.0;
    }
}

/* camfollow 0: forget the comparison state so the next 'camfollow 1' starts clean. */
static void PsyCamFollowReset(void)
{
    g_cfPrevValid = 0;
}
