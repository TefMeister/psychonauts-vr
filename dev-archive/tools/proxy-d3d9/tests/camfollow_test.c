/* camfollow_test.c - runs the SHIPPED head-follow code (px_07's basis snapshot/rotate/follow, cut out
 * of the file by build_camfollow_test.sh, plus px_07b) against three pretend engines (2026-10-08, /pd).
 * Exit code 0 = all passed.
 *
 *   FEEDBACK   each frame the engine eases its camera from whatever is in +0x150 toward where it wants
 *   OWN        each frame the engine rewrites +0x150 from its own angle
 *   OVERWRITE  like OWN, but the rewrite lands inside the first CandB, after our BeforeEye1 write
 *
 * It checks two things: that the restore stops FEEDBACK from swinging the view past the head, and that
 * the counters name the right engine, so the next live run can say which one Psychonauts is. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static char  g_cam[0x200];
static int   g_camFollowHead = 1;
static float g_camFollowScale = 1.0f;
static float g_headYawDeg = 0.0f;
static void *AutoGetCamera(void) { return g_cam; }
static void LogLine(const char *fmt, ...) { (void)fmt; }

#include "../build/tests/basis_follow_extract.inc"
#include "../px_07b_camfollow_restore.c.inc"

#define PI_F 3.14159265f
#define FRAMES 100
#define HEAD_YAW 20.0f
#define EASE 0.1f

static int g_fail;
static void expect(int ok, const char *what) { if (!ok) { printf("FAIL %s\n", what); g_fail++; } }

static void put_col(int col, float x, float y, float z)
{
    char *B = g_cam + PSY_CAM_BASIS_OFF;
    *(float *)(B + 0 * PSY_CAM_BASIS_RSTRIDE + col * 4) = x;
    *(float *)(B + 1 * PSY_CAM_BASIS_RSTRIDE + col * 4) = y;
    *(float *)(B + 2 * PSY_CAM_BASIS_RSTRIDE + col * 4) = z;
}

/* An engine-style basis at angle a (degrees) about Y, with the measured column scales, and the
 * translation row for origin O = (ox, 0, 0). */
static float g_upSign = 1.0f;   /* -1: the level's up column points down (the game's Y-flipped space) */

static void engine_write(float a, float ox)
{
    float r = a * PI_F / 180.0f, s = sinf(r), c = cosf(r);
    char *B = g_cam + PSY_CAM_BASIS_OFF;
    float cols[4][3] = { { 1.538f * c, 0, -1.538f * s }, { 0, 2.052f * g_upSign, 0 }, { s, 0, c }, { s, 0, c } };
    int i;
    for (i = 0; i < 4; i++) {
        put_col(i, cols[i][0], cols[i][1], cols[i][2]);
        *(float *)(B + 3 * PSY_CAM_BASIS_RSTRIDE + i * 4) = ox * cols[i][0];
    }
}

static float view_angle(void)   /* the forward column's angle about Y, in degrees */
{
    char *B = g_cam + PSY_CAM_BASIS_OFF;
    float x = *(float *)(B + 0 * PSY_CAM_BASIS_RSTRIDE + 2 * 4);
    float z = *(float *)(B + 2 * PSY_CAM_BASIS_RSTRIDE + 2 * 4);
    return atan2f(x, z) * 180.0f / PI_F;
}

enum { FEEDBACK, OWN, OVERWRITE };
static const char *g_name[] = { "FEEDBACK", "OWN", "OVERWRITE" };

typedef struct { float drawn; double toward, away; unsigned long lost, restored, late; } Run;


static Run run(int engine, int restore, float yaw)
{
    Run out;
    float own = 0.0f;
    int f;
    memset(g_cam, 0, sizeof g_cam);
    g_basisSnapValid = g_basisLastWriteValid = 0;
    PsyCamFollowReset();
    g_cfFrames = g_cfRewrites = g_cfLostInPass = g_cfRestored = g_cfLateRewrite = 0;
    g_cfSumYaw = g_cfTowardDeg = g_cfAwayDeg = 0;
    g_camFollowRestore = restore;
    g_headYawDeg = yaw;
    engine_write(0.0f, 100.0f);
    out.drawn = 0;
    for (f = 0; f < FRAMES; ++f) {
        float ox = 100.0f + (float)f;            /* the camera is moving: the engine rewrites every frame */
        if (engine == FEEDBACK) engine_write(view_angle() + EASE * (0.0f - view_angle()), ox);
        else if (engine == OWN) engine_write(own, ox);
        PsyBasisFollowHead(g_cam);                /* BeforeEye1 */
        if (engine == OVERWRITE) engine_write(own, ox);   /* inside the first CandB */
        out.drawn = view_angle();                 /* what eye 2 is drawn with */
        PsyCamFollowCheckEye2();                  /* BeforeEye2 */
        PsyCamFollowAfterBoth();                  /* AfterBoth */
    }
    out.toward = g_cfTowardDeg;
    out.away = g_cfAwayDeg;
    out.lost = g_cfLostInPass;
    out.restored = g_cfRestored;
    out.late = g_cfLateRewrite;
    printf("%-9s restore %d, head %+.0f, up %+.0f: view drawn at %7.1f deg; engine turned toward the head %6.1f,"
           " away %6.1f; lost in pass %3lu, restored %3lu, late %3lu\n", g_name[engine], restore, yaw, g_upSign,
           out.drawn, out.toward, out.away, out.lost, out.restored, out.late);
    return out;
}

static void suite(float yaw)
{
    Run r;
    r = run(FEEDBACK, 0, yaw);
    expect(fabsf(r.drawn - yaw) > 45.0f, "FEEDBACK without restore swings the view far past the head (the fault reproduces)");
    expect(r.toward > 90.0 && r.away < 1.0, "FEEDBACK without restore: the engine's own camera drifts toward the head");
    r = run(FEEDBACK, 1, yaw);
    expect(fabsf(r.drawn - yaw) < 0.5f, "FEEDBACK with restore draws the head yaw, no more");
    expect(r.toward < 1.0 && r.restored > FRAMES / 2, "FEEDBACK with restore: no drift, restores done");
    r = run(OWN, 0, yaw);
    expect(fabsf(r.drawn - yaw) < 0.5f, "OWN draws the head yaw");
    expect(r.toward < 1.0 && r.away < 1.0, "OWN: the engine's own camera does not drift");
    r = run(OVERWRITE, 1, yaw);
    expect(fabsf(r.drawn) < 0.5f, "OVERWRITE loses the head yaw (restore cannot help)");
    expect(r.lost > FRAMES / 2 && r.late > FRAMES / 2, "OVERWRITE is counted as lost in the pass");
}

int main(void)
{
    suite(HEAD_YAW);
    suite(-HEAD_YAW);         /* a left turn must read as toward the head too */
    g_upSign = -1.0f;         /* and a level whose up column points down */
    suite(HEAD_YAW);
    suite(-HEAD_YAW);
    if (!g_fail) printf("camfollow_test: all passed\n");
    return g_fail ? 1 : 0;
}
