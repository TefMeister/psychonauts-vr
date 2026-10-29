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

/* 2026-10-29: the REAL block, dumped live in Sasha's Lab (camera +0x150, rows r0..r3). It is a
 * view-projection: c2 = A*c3 and row3[2] = A*row3[3] + B (A 1.0002, B -10.002). The 2026-10-08 model
 * engines above had no such constant, which is why they passed while the game shredded. */
static const float kLab[4][4] = {
    { -1.3317f,    -0.2480f,    -0.4852f,   -0.4851f },
    { -0.0000f,     1.9894f,    -0.2420f,   -0.2419f },
    { -0.7689f,     0.4296f,     0.8405f,    0.8403f },
    { -16.3133f, -1678.7822f,  2995.0203f, 3004.4211f },
};

static float cell(int row, int col)
{
    return *(float *)(g_cam + PSY_CAM_BASIS_OFF + row * PSY_CAM_BASIS_RSTRIDE + col * 4);
}

static float clip(int col, const double p[3])   /* clip coordinate `col` of world point p */
{
    return (float)(p[0] * cell(0, col) + p[1] * cell(1, col) + p[2] * cell(2, col) + cell(3, col));
}

static void real_block(void)
{
    double eye[3], a[3][3], b[3], det;
    const int cols[3] = { 0, 1, 3 };
    float before[4][4], B0;
    int r, c, i;
    memset(g_cam, 0, sizeof g_cam);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 4; c++)
            *(float *)(g_cam + PSY_CAM_BASIS_OFF + r * PSY_CAM_BASIS_RSTRIDE + c * 4) = kLab[r][c];
    /* The eye is where clip x = y = w = 0; solved here by Cramer's rule, independently of the proxy. */
    for (i = 0; i < 3; i++) {
        for (r = 0; r < 3; r++) a[i][r] = kLab[r][cols[i]];
        b[i] = -kLab[3][cols[i]];
    }
    det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
        + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    for (i = 0; i < 3; i++) {
        double m[3][3]; int k;
        for (r = 0; r < 3; r++) for (k = 0; k < 3; k++) m[r][k] = (k == i) ? b[r] : a[r][k];
        eye[i] = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
               + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])) / det;
    }
    for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) before[r][c] = cell(r, c);

    g_basisSnapValid = g_basisLastWriteValid = 0;
    PsyBasisSnapshot(g_cam);
    PsyApplyBasisYaw(g_cam, 0.0f);
    {
        float worst = 0;
        for (r = 0; r < 4; r++) for (c = 0; c < 4; c++) {
            float d = fabsf(cell(r, c) - before[r][c]);
            if (d > worst) worst = d;
        }
        printf("real block, yaw 0: largest change %.5f (w at the eye %.4f)\n", worst, clip(3, eye));
        expect(worst < 0.01f, "real block: zero yaw writes back what the engine had");
    }
    {
        float A0 = sqrtf(before[0][2] * before[0][2] + before[1][2] * before[1][2] + before[2][2] * before[2][2])
                 / sqrtf(before[0][3] * before[0][3] + before[1][3] * before[1][3] + before[2][3] * before[2][3]);
        B0 = before[3][2] - A0 * before[3][3];
    }
    for (i = 0; i < 2; i++) {
        float yaw = i ? -20.0f : 20.0f, A, Bc;
        PsyApplyBasisYaw(g_cam, yaw);
        A = sqrtf(cell(0, 2) * cell(0, 2) + cell(1, 2) * cell(1, 2) + cell(2, 2) * cell(2, 2))
          / sqrtf(cell(0, 3) * cell(0, 3) + cell(1, 3) * cell(1, 3) + cell(2, 3) * cell(2, 3));
        Bc = cell(3, 2) - A * cell(3, 3);
        printf("real block, yaw %+.0f: eye stays at x %.3f y %.3f w %.3f; depth constant %.3f\n",
               yaw, clip(0, eye), clip(1, eye), clip(3, eye), Bc);
        expect(fabsf(clip(0, eye)) < 0.05f && fabsf(clip(1, eye)) < 0.05f && fabsf(clip(3, eye)) < 0.05f,
               "real block: turning keeps the camera where it is");
        /* Measured against the same calculation on the untouched block: the dump has four decimals, so
         * A (and with it B) is only good to about 0.2 here; what matters is that the turn keeps it. */
        expect(fabsf(Bc - B0) < 0.05f, "real block: the depth constant survives the turn");
    }
}

int main(void)
{
    real_block();
    suite(HEAD_YAW);
    suite(-HEAD_YAW);         /* a left turn must read as toward the head too */
    g_upSign = -1.0f;         /* and a level whose up column points down */
    suite(HEAD_YAW);
    suite(-HEAD_YAW);
    if (!g_fail) printf("camfollow_test: all passed\n");
    return g_fail ? 1 : 0;
}
