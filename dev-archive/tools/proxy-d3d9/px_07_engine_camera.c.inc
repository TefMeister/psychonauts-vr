
/* ===================================================================== *
 * notes/67: external automation harness
 *
 * Lets commands be delivered from OUTSIDE the process: append a line to
 * psyvr_automation_cmds.txt next to Psychonauts.exe, and this reads it,
 * runs it, and truncates the file. No synthetic input, no hotkey, no
 * rebuild to change a level code - so an unattended session can drive the
 * game after the user has launched it.
 *
 * This is the XIII harness pattern ported over, including the two lessons
 * that cost that project a crash and a debugging round:
 *
 *  1. NEVER dispatch a real engine COMMAND from a render-path hook. XIII
 *     GPF'd doing exactly that (its queue drained from a camera hook that
 *     turned out to be called from inside the engine's Draw). We are on
 *     the render path here too - CandB_AfterBoth runs mid-frame - so this
 *     harness deliberately only does work that is already proven safe from
 *     this exact site: SetPendingLevel (stages an async request; the F12
 *     path has done this since notes/59), debug-flag byte pokes (notes/62,
 *     64), and plain field writes on the camera object. It does NOT call
 *     into the Lua interpreter or any general command dispatcher. If that
 *     is ever wanted, it needs a game-logic tick site found first.
 *  2. Log BEFORE the call, not after. XIII's first version logged on
 *     completion, so its crash left no record of which command died and it
 *     had to be inferred. LogLine already opens/appends/closes per call,
 *     so every line is on disk before the next one runs.
 *
 * Camera access is the recon/2026-08-27-camera-control-without-lua finding:
 * the Lua bindings are a marshalling shim over plain engine work, so the
 * camera is reachable with a guard + an accessor + three float stores.
 * ===================================================================== */
/* Guard + accessor on the engine singleton, both __thiscall. Returns NULL
 * whenever the engine says there is no usable camera right now (loading,
 * menus), so every caller fails soft rather than writing into nothing. */
static void *AutoGetCamera(void)
{
    void *engine = *(void **)0x78BC20;
    if (!engine) return NULL;
    if (!((BOOL (__thiscall *)(void *))0x504220)(engine)) return NULL;
    return ((void *(__thiscall *)(void *))0x4FA5A0)(engine);
}

/* ===================================================================== *
 * notes/69: RAZ'S WORLD POSITION, WITHOUT LUA AND WITHOUT A SINGLE CALL.
 * [inferred-static 2026-09-01, n=3 corroborating call sites] - NOT yet live.
 *
 * The FP sub-project has been recorded as blocked on "get Raz's position"
 * since notes/47, on the assumption it needed the Lua exec primitive plus
 * GetBoneWorldPosition. Static decode of the binding impls shows it needs
 * neither: GetPlayerPosition_impl (0x005C1CE0) is fifteen instructions and
 * does nothing but walk three pointers off a global the proxy ALREADY
 * dereferences for the camera (0x0078BC20):
 *
 *     engine = *(void **)0x0078BC20            <- same global as AutoGetCamera
 *     player = *(void **)(engine + 0x818C)
 *     obj    = *(void **)(player + 0x10)
 *     pos    = (float *)(obj + 0x40)           <- x, y, z, contiguous
 *
 * The +0x818C player object is corroborated by four independent bindings that
 * all reach it the same way: GetPlayerPosition (0x005C1CE0), GetPlayerLSO
 * (0x005C0BD0), IsRazZLocked (0x005B6CB0) and GetPlayerDist (0x005C0920).
 *
 * The +0x10 -> +0x40 position tail is n=2, not n=1 as first recorded:
 * GetPlayerDist walks the identical chain and then uses those three floats as
 * a POSITION in a distance computation against another entity. That is a
 * second *independent use* consistent with the meaning, which is stronger
 * corroboration than a second identical read would have been.
 *
 * This is READ-ONLY and calls nothing, so it is safe from the render-path
 * hook under the same rule the rest of the harness follows.
 *
 * Head bone, for when a bone-exact eye replaces the fixed height below:
 * Raz's rig names are in the exe - "headJA_1" (0x00703F94, used as a node
 * name via a __thiscall(node, name) at 0x0043CA90), "headJEnd_1", "spineJC_1",
 * "handJEndLf_2" (the default attach bone in AttachInventoryEntityToPlayer).
 * The lookup is __thiscall 0x00438B70(entity, name) -> bone, then
 * __thiscall 0x00492B70(bone, float out[16]) for its matrix. Not wired yet:
 * a fixed eye height is enough to make FP testable, and the bone route can
 * be added once the cheap version is confirmed in a headset. */
#define PSY_PLAYER_OFFSET    0x818C
#define PSY_PLAYEROBJ_OFFSET 0x10
#define PSY_PLAYERPOS_OFFSET 0x40

/* A pointer we are about to walk came from game memory, so validate it rather
 * than trusting it: a null or obviously bogus value during a level load would
 * otherwise crash the game from the render path. */
static BOOL PsyPtrLooksSane(const void *p)
{
    ULONG_PTR v = (ULONG_PTR)p;
    return v >= 0x00010000 && v < 0x7FFF0000 && (v & 3) == 0;
}

static BOOL PsyGetPlayerPos(float out[3])
{
    char *engine, *player, *obj;

    engine = *(char **)0x78BC20;
    if (!PsyPtrLooksSane(engine)) return FALSE;
    player = *(char **)(engine + PSY_PLAYER_OFFSET);
    if (!PsyPtrLooksSane(player)) return FALSE;
    obj = *(char **)(player + PSY_PLAYEROBJ_OFFSET);
    if (!PsyPtrLooksSane(obj)) return FALSE;

    memcpy(out, obj + PSY_PLAYERPOS_OFFSET, sizeof(float) * 3);
    return TRUE;
}

/* ===================================================================== *
 * notes/73: THE HEAD BONE'S WORLD POSITION - i.e. the MEASURED eye height.
 * [inferred-static 2026-09-02] - built, never run.
 *
 * WHY THIS EXISTS: the board wanted eye height as camera-minus-player, and
 * notes/71 disproved that - both the camera position (+327.8 Y) and the
 * render eye (~149) are the height of a THIRD-PERSON camera, not an eye.
 * Recording either would have baked a wrong constant into the FP work at
 * exactly the spot where fpheight's default of 60 is already a flagged guess.
 * The head bone gives it with no third-person camera in the path and no
 * unit-scale conversion: headWorldPos - playerPos IS the eye height, in the
 * engine's own units.
 *
 * DECODED FROM GetBoneWorldPosition's Lua binding (shim 0x005B1690, the
 * `GetBoneWorldPosition' row of lua-bindings.def). Stripped of marshalling it
 * is four engine calls, and the shim returns SIX values - position AND euler,
 * which is where the "returns 3 floats" reading in earlier notes was wrong:
 *
 *     owner = <the object that owns the bone array>
 *     bone  = FindBoneByName(owner, "headJA_1")      0x00438B70  ret 4
 *     GetBoneMatrix(bone, boneMtx[16])               0x00492B70  ret 4
 *     ownerMtx = GetOwnerWorldMatrix(owner)          0x00438390  ret 0
 *     MatMul(ownerMtx, worldMtx[16], boneMtx)        0x00433E50  ret 8
 *
 * Every one of those four ret forms was read off the binary, so the __thiscall
 * signatures below are checked, not assumed - a stack-convention mismatch here
 * would corrupt the render thread's stack rather than fail cleanly.
 *
 * ⚠️ NOT read-only, unlike playerpos - say so rather than inherit playerpos's
 * safety claim. 0x00438390 branches on flag bits at owner+0x94 across 143
 * instructions, which is the shape of a lazy "recompute the world transform if
 * dirty, then cache it" accessor, so calling it can WRITE engine state. That is
 * what the engine itself does every time a script asks for a bone position, so
 * it is not novel - but it is a different risk class from the pure pointer walk
 * in PsyGetPlayerPos, and it is why this is a MANUAL one-shot command rather
 * than anything that runs per frame.
 *
 * Translation is floats 12..14 (row 3 of a row-major 4x4). That layout is
 * corroborated twice independently in this engine: notes/70's node transform
 * (+0x40 = row 3 of the 4x4 at node+0x10) and the camera world matrix (rows
 * 0x90/0xA0/0xB0, translation 0xC0 = +0x30 from row 0).
 *
 * "headJA_1" is passed as the exe's OWN string constant at 0x00703F94 rather
 * than a literal of ours, so the comparison inside the lookup sees exactly the
 * bytes the engine shipped. Verified by reading .rdata at that address; the
 * sibling default attach bone at 0x0071195C reads "handJEndLf_2", which is the
 * cross-check that these are real rig names and not a coincidental hit.
 *
 * ============================= THE UNKNOWN =============================
 * WHICH object owns the bone array could NOT be settled statically, and this
 * command is built to answer that safely rather than to assume it:
 *   - the Lua binding gets its owner from 0x005B01E0, which returns
 *     luaEntityWrapper->[0xA8];
 *   - the attach path (AttachInventoryEntityToPlayer, whose default target IS
 *     our player at engine+0x818C) instead uses targetEntity->[0x10] - the same
 *     node our position chain walks.
 * Those are two different accessors, so rather than guess, the probe below
 * SHAPE-CHECKS each candidate with pure reads before calling anything into the
 * engine: 0x00438B70 needs count = (owner[0x54] >> 5) & 0x7FFFFFF and an array
 * pointer at owner[0x5C], so a candidate that does not decode to a sane count
 * and a sane pointer is never passed to it. All the guessing is therefore
 * read-only; the engine calls only ever run against a plausible object.
 * The command reports WHICH candidate matched, so one run settles it.
 * ===================================================================== */
#define PSY_BONECOUNT_OFFSET 0x54
#define PSY_BONEARRAY_OFFSET 0x5C
#define PSY_NODE_PARENT_OFFSET 0xB8

typedef void  *(__thiscall *PsyFindBone_t)(void *owner, const char *name);
typedef float *(__thiscall *PsyBoneMatrix_t)(void *bone, float *out16);
typedef float *(__thiscall *PsyOwnerWorldMtx_t)(void *owner);
typedef float *(__thiscall *PsyMatMul_t)(void *matA, float *out16, const float *matB);

/* Read-only shape test for "does this object own a bone array?", mirroring
 * exactly what 0x00438B70 will do with it. Pure reads - safe to run on a
 * candidate that turns out to be the wrong class. */
static BOOL PsyLooksLikeBoneOwner(const char *obj, unsigned *outCount)
{
    unsigned count;
    const char *arr;

    if (!PsyPtrLooksSane(obj)) return FALSE;
    count = (*(const unsigned *)(obj + PSY_BONECOUNT_OFFSET) >> 5) & 0x07FFFFFFu;
    if (count == 0 || count > 4096) return FALSE;      /* a rig, not a coincidence */
    arr = *(const char *const *)(obj + PSY_BONEARRAY_OFFSET);
    if (!PsyPtrLooksSane(arr)) return FALSE;

    if (outCount) *outCount = count;
    return TRUE;
}

/* Find the bone-owning object hanging off the player, trying the candidates the
 * static read left open, cheapest and most-likely first. Returns NULL if none
 * shape-checks, which is a clean "not in gameplay / wrong class" answer rather
 * than a crash. `whichOut' names the winner for the log. */
static void *PsyFindPlayerBoneOwner(const char **whichOut, unsigned *countOut)
{
    char *engine, *player, *node;
    struct { const char *name; char *obj; } cand[4];
    int i, n = 0;

    engine = *(char **)0x78BC20;
    if (!PsyPtrLooksSane(engine)) return NULL;
    player = *(char **)(engine + PSY_PLAYER_OFFSET);
    if (!PsyPtrLooksSane(player)) return NULL;

    cand[n].name = "player+0x10 (node)";
    node = *(char **)(player + PSY_PLAYEROBJ_OFFSET);
    cand[n].obj = PsyPtrLooksSane(node) ? node : NULL;
    n++;

    cand[n].name = "player";
    cand[n].obj  = player;
    n++;

    cand[n].name = "player+0xA8";
    cand[n].obj  = PsyPtrLooksSane(player) ? *(char **)(player + 0xA8) : NULL;
    n++;

    cand[n].name = "player+0x10+0xA8";
    cand[n].obj  = (node && PsyPtrLooksSane(node)) ? *(char **)(node + 0xA8) : NULL;
    n++;

    for (i = 0; i < n; ++i) {
        if (cand[i].obj && PsyLooksLikeBoneOwner(cand[i].obj, countOut)) {
            if (whichOut) *whichOut = cand[i].name;
            return cand[i].obj;
        }
    }
    return NULL;
}

/* notes/67: the camera's WORLD matrix, found by dumping the camera object and
 * diffing it across a camera turn (2026-08-27):
 *
 *   +0x50/0x60/0x70 rows, +0x80 translation -> VIEW matrix (world->camera);
 *                                              its translation is the -R*p form
 *   +0x90/0xA0/0xB0 rows, +0xC0 translation -> CAMERA WORLD matrix
 *
 * The two rotations are exact transposes of one another, which is precisely the
 * view/world relationship. Rows are 4 floats (16-byte stride).
 *
 * World up is Z: row 0 of the world matrix (the camera's "right" vector) reads
 * Z = 0.0000 exactly, which only holds for a right vector kept horizontal.
 * So yaw is a rotation about world Z applied to each basis row.
 *
 * NOTE the decoy: camera+0x20 is what SetCameraOrientation writes, and the
 * renderer ignores it - a reversed write there persisted in memory untouched
 * while the view did not move at all. Do not go back to it. */
#define PSY_CAM_WORLD_ROW0 0x90

/* ===================================================================== *
 * notes/68: THE FIRST CAMERA ORIENTATION FIELD THE RENDERER ACTUALLY READS
 * (2026-08-28). Read status: PROVEN. Exact meaning: NOT yet established - see
 * "what is NOT proven" at the bottom before building on this.
 *
 * Every previous attempt to turn the camera wrote a MATRIX, and all of them
 * failed, because every matrix on this object is a derived OUTPUT that nothing
 * reads back:
 *
 *   +0x20  facing vector (what SetCameraOrientation writes) - write persists
 *          untouched, renderer ignores it.
 *   +0x50  view matrix   - a held write SURVIVES to end of frame (verified by
 *          dumping it back) and the image still does not change.
 *   +0x90  camera world matrix - same: survives the whole frame, ignored.
 *
 * (The 2026-08-27 note recorded +0x90 as "overwritten inside the same frame".
 * That was wrong: probing at every hook site shows the write surviving from
 * BeforeEye1 through AfterBoth. The earlier dump was almost certainly taken
 * BEFORE the write in both the yaw-0 and yaw-90 runs, which shows the engine's
 * value either way. Wrong-field, not wrong-timing.)
 *
 * The real input is a unit direction vector stored COLUMN-WISE with stride
 * 0x10 - not three contiguous floats:
 *
 *   x at +0x158,  y at +0x168,  z at +0x178
 *
 * A second, near-identical vector sits one float later (+0x15C/+0x16C/+0x17C),
 * which looks like the smoothed "current" against this "target".
 *
 * PROVEN by A/B/A with the camera otherwise untouched: pinning +0x158 to 0.0
 * took the frame from 3.62% -> 0.61% near-black with a completely different
 * view, and releasing it returned it to 3.61%. Reversible, repeatable.
 *
 * WHAT IS NOT PROVEN - do not state these as fact:
 *  - That this is a "look direction". It is A field the renderer reads. A
 *    sweep of the x component (0.724 -> 0.4 -> 0.0 -> -0.4 -> -0.724) did NOT
 *    produce a clean rotation: every value except the original landed in a
 *    similar washed-out, near-clipping view (0.60-0.72% black vs 3.62%), with
 *    only small framing differences between them.
 *  - HOWEVER, that sweep wrote ONE component of a THREE-component vector,
 *    leaving it non-unit (length 0.688 at x=0). A degenerate view is exactly
 *    what a non-unit direction would produce, so the sweep does not
 *    distinguish "wrong field" from "right field, broken normalization".
 *    Writing all three as a unit vector (camlook / lookhold, repointed here)
 *    is the test that separates them, and it has NOT been run yet.
 *  - Whether culling follows it. Black% went DOWN (3.62 -> 0.61), which is
 *    consistent with culling following the camera, but a near-clipping view
 *    fills the frame regardless, so it is not evidence either way yet.
 *
 * Why it is worth pursuing: every other orientation field on this object is a
 * derived output that nothing reads, so this is the only orientation-adjacent
 * input found so far. If it does steer the camera, it does so BEFORE culling -
 * and unlike the gamepad route (Candidate 1) there is no 87.4-degree free-look
 * clamp. */
#define PSY_CAM_LOOKDIR_OFF    0x158
#define PSY_CAM_LOOKDIR_STRIDE 0x10

static void PsySetLookDir(void *cam, float x, float y, float z)
{
    char *b = (char *)cam + PSY_CAM_LOOKDIR_OFF;
    *(float *)(b + 0 * PSY_CAM_LOOKDIR_STRIDE) = x;
    *(float *)(b + 1 * PSY_CAM_LOOKDIR_STRIDE) = y;
    *(float *)(b + 2 * PSY_CAM_LOOKDIR_STRIDE) = z;
    *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
}

static void PsyGetLookDir(void *cam, float *out)
{
    const char *b = (const char *)cam + PSY_CAM_LOOKDIR_OFF;
    out[0] = *(const float *)(b + 0 * PSY_CAM_LOOKDIR_STRIDE);
    out[1] = *(const float *)(b + 1 * PSY_CAM_LOOKDIR_STRIDE);
    out[2] = *(const float *)(b + 2 * PSY_CAM_LOOKDIR_STRIDE);
}

/* ===================================================================== *
 * notes/68 part 2: rotate the WHOLE camera basis, not just forward.
 *
 * The region at +0x150 is a matrix stored as 4 rows of 4 floats, rows at
 * stride 0x10 - so its COLUMNS are the interesting vectors:
 *
 *   c0 = (+0x150, +0x160, +0x170)   right, scaled
 *   c1 = (+0x154, +0x164, +0x174)   up,    scaled
 *   c2 = (+0x158, +0x168, +0x178)   forward, UNIT
 *   c3 = (+0x15C, +0x16C, +0x17C)   forward again, UNIT (smoothed/previous)
 *
 * Evidence it is a camera basis and not a projection: |c1|/|c0| = 2.052/1.538
 * = 1.334, i.e. exactly the 4:3 aspect ratio, and the two scales are UNCHANGED
 * by our own fov command (dumped at fov 1.0 and 2.5 - byte-identical), so they
 * are not the projection scales.
 *
 * Why writing c2 alone produced a washed-out, degenerate image rather than a
 * turn: rotating forward while leaving right and up where they were makes the
 * basis non-orthogonal. That is a skew, not a rotation - so the picture breaks
 * instead of turning. Rotating all three columns together is the actual test.
 *
 * Up axis is Y here: c1 is dominated by its Y component (1.9894 of 2.052).
 * The dossier warns this engine's levels do NOT share one up axis, so the up
 * axis is taken from c1 rather than hardcoded. */
#define PSY_CAM_BASIS_OFF    0x150
#define PSY_CAM_BASIS_RSTRIDE 0x10

static int   g_camBasisYawOn  = 0;
static float g_camBasisYawDeg = 0.0f;

/* ⚠️ WHY THIS SNAPSHOTS INSTEAD OF ROTATING IN PLACE - a real bug, measured.
 *
 * The first version rotated whatever was in the basis, every frame, at
 * BeforeEye1. While the camera is stationary the engine does NOT rewrite this
 * matrix, so a 15-degree hold COMPOUNDED: dumps 1.5s apart showed c0.z drifting
 * 0.5160 -> 0.1235 and c2.z -0.9292 -> -0.9832, while c3.z stayed at its
 * original -0.9291 because it was deliberately left alone. So c2 and c3 - a
 * matched pair - diverged, and the render broke.
 *
 * That invalidated the experiment: the washed-out frames were a runaway of our
 * own making, NOT evidence about whether the basis is rotatable.
 *
 * Fix: snapshot the engine's own basis the first time the yaw is armed, then
 * each frame WRITE snapshot-rotated-by-theta. Absolute, idempotent, cannot
 * accumulate. Re-snapshot on re-arm so the camera can be moved between tests. */
static float g_basisSnap[4][3];   /* [column][row] - c0,c1,c2,c3 */
static float g_basisSnapT[4];     /* row 3 (+0x180): the translation row */
static float g_basisOrigin[3];    /* the point row 3 is measured from */
static int   g_basisOriginOk = 0;
static int   g_basisSnapValid = 0;

/* notes/68 part 3: drive the camera transform from LIVE HEAD YAW.
 *
 * This is the void fix as a player experiences it: turn your head and the game
 * turns its own camera, so it culls and renders where you are looking. Unlike
 * the gamepad route there is no 87.4-degree free-look clamp.
 *
 * Head yaw relative to the tracking reference, in degrees, published by
 * VRBridge_UpdateHeadTracking. Sign and gain are runtime-tunable because the
 * correct sign is a convention question best settled in a headset, not guessed
 * (XIII needed its HMD yaw negated for the same reason). */

/* What we wrote last frame, so a re-snapshot can tell "the engine updated the
 * camera" from "this is still our own value". Without this the follow mode
 * would read back its own write and compound it into a spin - the same bug that
 * wrecked the first manual attempt. */
static float g_basisLastWrite[4][3];
static int   g_basisLastWriteValid = 0;

/* ⚠️ THE TRANSLATION ROW MUST FOLLOW THE ROTATION - this is what broke every
 * larger angle.
 *
 * The block at +0x150 is a full 4x4: rows 0-2 hold the axes as COLUMNS, and row
 * 3 (+0x180) is a translation. Measured live, row3[i] = dot(O, c_i) for a single
 * origin O, recovered by solving the 3x3 system:
 *
 *     solved O   = (-19852.63, -449.93, 17814.91)
 *     camera pos = (-19856.27,  451.41, 17824.46)
 *
 * X and Z agree to a few units (the camera drifts between the dump and the
 * telemetry sample); Y comes back as the exact NEGATIVE. So this is a view-style
 * transform in a Y-flipped space.
 *
 * Rotating the axes while leaving row 3 pinned to the OLD axes leaves the
 * translation inconsistent, and the inconsistency grows with the rotation angle
 * - which is exactly the symptom: clean at 2-5 degrees, sheared by 8-10,
 * wrecked by 15+, and completely unaffected by preserving column magnitudes.
 *
 * O is SOLVED from the engine's own basis and row 3 rather than assumed from
 * camera position, so no handedness or Y-flip convention has to be guessed. */
static int PsySolveOrigin(void)
{
    /* Solve M O = t, where the rows of M are c0, c1, c2. */
    double m[3][4], f;
    int i, j, r, p;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) m[i][j] = g_basisSnap[i][j];
        m[i][3] = g_basisSnapT[i];
    }
    for (i = 0; i < 3; i++) {
        p = i;
        for (r = i + 1; r < 3; r++)
            if ((m[r][i] < 0 ? -m[r][i] : m[r][i]) > (m[p][i] < 0 ? -m[p][i] : m[p][i])) p = r;
        if ((m[p][i] < 0 ? -m[p][i] : m[p][i]) < 1e-9) return 0;   /* singular */
        if (p != i) for (j = 0; j < 4; j++) { double t = m[i][j]; m[i][j] = m[p][j]; m[p][j] = t; }
        for (r = 0; r < 3; r++) {
            if (r == i) continue;
            f = m[r][i] / m[i][i];
            for (j = i; j < 4; j++) m[r][j] -= f * m[i][j];
        }
    }
    for (i = 0; i < 3; i++) g_basisOrigin[i] = (float)(m[i][3] / m[i][i]);
    return 1;
}

static void PsyBasisSnapshot(void *cam)
{
    const char *B = (const char *)cam + PSY_CAM_BASIS_OFF;
    int col, row;
    for (col = 0; col < 4; col++)
        for (row = 0; row < 3; row++)
            g_basisSnap[col][row] =
                *(const float *)(B + row * PSY_CAM_BASIS_RSTRIDE + col * 4);
    for (col = 0; col < 4; col++)
        g_basisSnapT[col] = *(const float *)(B + 3 * PSY_CAM_BASIS_RSTRIDE + col * 4);
    g_basisOriginOk = PsySolveOrigin();
    g_basisSnapValid = 1;
    /* Log only when something a reader would care about changes. In follow mode
     * the engine legitimately updates the camera EVERY frame, so an unconditional
     * log here wrote ~60 lines/sec and put 13.6 MB into the proxy log during one
     * short test - which also buries the lines that matter. */
    {
        static int  s_logged = 0;
        static int  s_lastOk = -1;
        static float s_lastO[3] = {0,0,0};
        float d = 0.0f; int i;
        for (i = 0; i < 3; i++) {
            float e = g_basisOrigin[i] - s_lastO[i];
            d += e < 0 ? -e : e;
        }
        if (!s_logged || g_basisOriginOk != s_lastOk || d > 500.0f) {
            LogLine("CAMBASIS snapshot: origin %s (%.2f %.2f %.2f)",
                    g_basisOriginOk ? "solved" : "SINGULAR - translation will not be corrected",
                    g_basisOrigin[0], g_basisOrigin[1], g_basisOrigin[2]);
            s_logged = 1; s_lastOk = g_basisOriginOk;
            for (i = 0; i < 3; i++) s_lastO[i] = g_basisOrigin[i];
        }
    }
}

/* Write snapshot-rotated-by-deg. ALL FOUR columns, including c3: c2 and c3 are
 * a matched pair and letting them diverge was what broke the picture. */
static void PsyApplyBasisYaw(void *cam, float deg)
{
    char *B = (char *)cam + PSY_CAM_BASIS_OFF;
    float s = (float)sin(deg * 3.14159265358979f / 180.0f);
    float c = (float)cos(deg * 3.14159265358979f / 180.0f);
    int col, up = 1;
    /* Up axis from the SNAPSHOT's c1 (the up column), not hardcoded - this
     * engine's levels do not share one up axis. */
    float ax = g_basisSnap[1][0] < 0 ? -g_basisSnap[1][0] : g_basisSnap[1][0];
    float ay = g_basisSnap[1][1] < 0 ? -g_basisSnap[1][1] : g_basisSnap[1][1];
    float az = g_basisSnap[1][2] < 0 ? -g_basisSnap[1][2] : g_basisSnap[1][2];
    if (ax >= ay && ax >= az) up = 0;
    else if (az >= ay && az >= ax) up = 2;

    for (col = 0; col < 4; col++) {
        float x = g_basisSnap[col][0], y = g_basisSnap[col][1], z = g_basisSnap[col][2];
        float nx, ny, nz, mag;
        /* ⚠️ PRESERVE EACH COLUMN'S MAGNITUDE.
         * c0 and c1 are NOT unit - they carry the projection scaling: measured
         * |c0| = 1.538, |c1| = 2.052, ratio 1.334 = the 4:3 aspect. Rotating
         * them as though they were orthonormal axes MIXES those two scales,
         * which shears the frustum instead of turning it. Measured live:
         * clean at 2-5 degrees, visibly sheared by 8-10, wrecked by 15 - a
         * progressive breakdown, which is the signature of a shear rather than
         * a wrong field. So rotate the DIRECTION and restore the length. */
        mag = (float)sqrt(x * x + y * y + z * z);
        if (mag > 1e-8f) { x /= mag; y /= mag; z /= mag; } else { mag = 0.0f; }
        if (up == 1) {        /* rotate in X-Z */
            nx = x * c + z * s; ny = y; nz = -x * s + z * c;
        } else if (up == 2) { /* rotate in X-Y */
            nx = x * c - y * s; ny = x * s + y * c; nz = z;
        } else {              /* up == 0: rotate in Y-Z */
            nx = x; ny = y * c - z * s; nz = y * s + z * c;
        }
        nx *= mag; ny *= mag; nz *= mag;
        *(float *)(B + 0 * PSY_CAM_BASIS_RSTRIDE + col * 4) = nx;
        *(float *)(B + 1 * PSY_CAM_BASIS_RSTRIDE + col * 4) = ny;
        *(float *)(B + 2 * PSY_CAM_BASIS_RSTRIDE + col * 4) = nz;
        /* Row 3 must follow: row3[col] = dot(origin, rotated column). Leaving it
         * on the OLD axes is what wrecked every angle above ~5 degrees. */
        if (g_basisOriginOk)
            *(float *)(B + 3 * PSY_CAM_BASIS_RSTRIDE + col * 4) =
                g_basisOrigin[0] * nx + g_basisOrigin[1] * ny + g_basisOrigin[2] * nz;
        g_basisLastWrite[col][0] = nx;
        g_basisLastWrite[col][1] = ny;
        g_basisLastWrite[col][2] = nz;
    }
    g_basisLastWriteValid = 1;
}

/* Follow live head yaw. Re-snapshots only when the engine itself moved the
 * camera, detected by comparing against our own last write - so a stationary
 * camera does not compound, and a moving one still tracks. */
static void PsyBasisFollowHead(void *cam)
{
    const char *B = (const char *)cam + PSY_CAM_BASIS_OFF;
    int col, row, engineMoved = 0;
    if (!g_basisLastWriteValid) {
        engineMoved = 1;
    } else {
        for (col = 0; col < 4 && !engineMoved; col++)
            for (row = 0; row < 3; row++) {
                float cur = *(const float *)(B + row * PSY_CAM_BASIS_RSTRIDE + col * 4);
                float d = cur - g_basisLastWrite[col][row];
                if (d < 0) d = -d;
                if (d > 1e-5f) { engineMoved = 1; break; }
            }
    }
    if (engineMoved) PsyBasisSnapshot(cam);
    PsyApplyBasisYaw(cam, g_headYawDeg * g_camFollowScale);
}

/* ===================================================================== *
 * notes/69: FIRST PERSON ON THE CAMERA OBJECT (fpcam), not on the render
 * transform. UNTESTED - built on the dev PC, never yet run.
 *
 * The existing PSYVR_FIRST_PERSON path composes a translation into the
 * register-6 WVP upload. notes/68 proved that layer is POST-culling: it moves
 * the picture, not the camera, so the engine still culls for wherever the
 * chase camera was. That is almost certainly why notes/51-53 recorded "zero
 * visible eye movement" from an FP path that looked correct on paper, and it
 * is why the black void survives it.
 *
 * This is the rebuild §1 of the status board asks for: put the eye where Raz
 * is by writing the two fields that ARE pre-culling inputs and are both now
 * known-writable -
 *
 *   position    camera+0x08   (already exercised by campos/camhold)
 *   orientation camera+0x150  (the basis, already exercised by camfollow)
 *
 * so head yaw and the eye position both land before the engine culls.
 *
 * Deliberately kept to a FIXED eye height rather than the head bone. The bone
 * route is fully mapped above and costs one more call chain, but a fixed
 * height is enough to answer the only question that matters first: does the
 * engine cull correctly for a camera parked inside the player? Adding the
 * bone before knowing that would risk attributing a bone bug to the camera.
 *
 * Height/forward defaults are GUESSES in world units, hence both are live
 * commands: Raz is a child, the game's unit scale is not established anywhere
 * in the notes, and a respawn is ~26000 units of travel (notes/67), so the
 * scale is large. Tune in the headset, do not trust these numbers. */
static int   g_fpCamOn      = 0;
static float g_fpCamHeight  = 60.0f;  /* along the world up axis, from Raz's origin */
static float g_fpCamForward = 0.0f;   /* along camera forward, to clear the head mesh */
static int   g_fpCamUpAxis  = 2;      /* 0=X 1=Y 2=Z. notes/68: the camera world
                                       * matrix's right vector reads Z=0 exactly,
                                       * which puts world up on Z - but the dossier
                                       * warns levels do not all share an up axis,
                                       * so this stays selectable. */

static void PsyFirstPersonCam(void *cam)
{
    float p[3], fwd[3];

    if (!PsyGetPlayerPos(p)) return;   /* loading / menu: leave the camera alone */

    if (g_fpCamUpAxis >= 0 && g_fpCamUpAxis <= 2)
        p[g_fpCamUpAxis] += g_fpCamHeight;

    if (g_fpCamForward != 0.0f) {
        PsyGetLookDir(cam, fwd);
        p[0] += fwd[0] * g_fpCamForward;
        p[1] += fwd[1] * g_fpCamForward;
        p[2] += fwd[2] * g_fpCamForward;
    }

    memcpy((char *)cam + PSY_CAM_POS_OFFSET, p, sizeof(float) * 3);
    *((unsigned char *)cam + PSY_CAM_DIRTY_OFFSET) |= 1;
}

static float g_camYawDeg = 0.0f;   /* 0 = off */

/* notes/68: WHERE the yaw write lands, which is the whole ballgame.
 *
 * 2026-08-27 applied it only at BeforeEye1 and it did nothing: dumps with yaw 0
 * and yaw 90 came back bit-for-bit identical, so the write was being recomputed
 * over, inside the same frame. That was read as "CandB overwrites it, so the
 * write must go INSIDE CandB" - but notes/59 established something that makes
 * this much cheaper: CandB is the camera's own update tick and is NOT the
 * renderer; the real draw chain is a SIBLING of CandB under a higher per-frame
 * dispatcher, not nested inside it.
 *
 * If that is right, the write does not need to be inside CandB at all - it only
 * needs to land AFTER the camera update finishes and BEFORE the sibling draw
 * chain runs. The trampoline already provides two such moments that have never
 * been tried:
 *
 *   1 = BeforeEye1  - before the first real CandB   (the one already refuted)
 *   2 = BeforeEye2  - AFTER the first real CandB returns
 *   3 = AfterBoth   - after both CandB invocations
 *
 * Selectable at runtime so all three can be tested in ONE launch instead of
 * three rebuilds. */
static int   g_camYawSite = 1;     /* 1=BeforeEye1 (legacy), 2=BeforeEye2, 3=AfterBoth */

/* Diagnostic: log camera world-matrix row 0 at every site in the same frame.
 * This answers "WHEN does the engine rewrite the matrix" directly, instead of
 * inferring it from whether the picture changed - the inference that made the
 * 2026-08-27 result ambiguous for a whole session. Counts down in frames. */
static int   g_camProbeFrames = 0;

static void CamProbe(const char *where)
{
    void *cam;
    float *m;
    if (g_camProbeFrames <= 0) return;
    cam = AutoGetCamera();
    if (!cam) { LogLine("CAMPROBE %-11s (no camera)", where); return; }
    m = (float *)((char *)cam + PSY_CAM_WORLD_ROW0);
    /* Raw hex as well as float: bit-for-bit equality is the actual question,
     * and printed floats can hide a low-bit difference. */
    LogLine("CAMPROBE %-11s row0 = %.4f %.4f %.4f   [%08X %08X %08X]",
            where, m[0], m[1], m[2],
            *(unsigned *)&m[0], *(unsigned *)&m[1], *(unsigned *)&m[2]);
}

/* site: which hook point is calling. Applies the yaw only if it matches the
 * selected site, so one launch can test all three. */
static void ApplyCameraYawAt(int site)
{
    void *cam;
    float *m;
    float s, c, x, y;
    int r;

    if (g_camYawDeg == 0.0f) return;
    if (site != g_camYawSite) return;
    cam = AutoGetCamera();
    if (!cam) return;

    s = (float)sin(g_camYawDeg * 3.14159265358979f / 180.0f);
    c = (float)cos(g_camYawDeg * 3.14159265358979f / 180.0f);
    for (r = 0; r < 3; r++) {
        m = (float *)((char *)cam + PSY_CAM_WORLD_ROW0 + r * 0x10);
        x = m[0]; y = m[1];
        m[0] = x * c - y * s;
        m[1] = x * s + y * c;
    }
    if (g_camProbeFrames > 0) LogLine("CAMPROBE   applied yaw %.1f at site %d", g_camYawDeg, site);
}

/* BeforeEye1 entry: the held-scalar pin, plus the yaw if site 1 is selected. */
static void ApplyCameraYaw(void)
{
    /* notes/67: pin a scalar before the engine renders, if one is held. Done
     * here (BeforeEye1) as well as in the automation tick, so a value the
     * engine recomputes per frame still stands a chance of being seen. */
    if (g_pokeHoldOn) {
        void *pc = AutoGetCamera();
        if (pc) *(float *)((char *)pc + g_pokeHoldOff) = g_pokeHoldVal;
    }

    /* notes/68: the look-direction hold must be applied HERE, not only in the
     * automation tick. AutomationTick runs from AfterBoth - after the frame has
     * already been culled and drawn - so a direction written there only affects
     * the next frame, and the engine recomputes it first. Applying at
     * BeforeEye1 is what made the +0x158 experiment work, and turning the camera
     * before culling is the entire point for the black void. */
    if (g_lookHold) {
        void *lc = AutoGetCamera();
        if (lc) PsySetLookDir(lc, g_lookHoldDir[0], g_lookHoldDir[1], g_lookHoldDir[2]);
    }

    /* notes/69: first person on the camera object. Applied HERE, at BeforeEye1,
     * for exactly the reason the look-direction hold is: the automation tick
     * runs from AfterBoth, one frame too late and after culling. Runs before
     * the yaw block so head-follow rotates the camera we just moved. */
    if (g_fpCamOn) {
        void *fc = AutoGetCamera();
        if (fc) PsyFirstPersonCam(fc);
    }

    /* notes/68 part 3: head-follow takes precedence over the manual test yaw. */
    if (g_camFollowHead) {
        void *hc = AutoGetCamera();
        if (hc) PsyBasisFollowHead(hc);
    } else
    /* notes/68 part 2: rotate the whole basis, every frame, before culling. */
    if (g_camBasisYawOn && g_camBasisYawDeg != 0.0f) {
        void *bc = AutoGetCamera();
        if (bc) {
            if (!g_basisSnapValid) PsyBasisSnapshot(bc);
            PsyApplyBasisYaw(bc, g_camBasisYawDeg);
        }
    }

    CamProbe("BeforeEye1");
    ApplyCameraYawAt(1);
    CamProbe("post-write-1");
}
