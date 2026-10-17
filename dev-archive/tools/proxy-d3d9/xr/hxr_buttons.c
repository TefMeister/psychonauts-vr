/* hxr_buttons.c - see hxr_buttons.h */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "hr_common.h"
#include "hxr_buttons.h"

static PFN_xrStringToPath p_s2p;
static PFN_xrCreateAction p_create;
static PFN_xrGetActionStateFloat p_float;
static PFN_xrGetActionStateBoolean p_bool;
static PFN_xrGetActionStateVector2f p_vec2;
static int g_ok;

/* one action per hand and input, so no subaction paths are needed */
enum { A_TRIGGER, A_SQUEEZE, A_STICK, A_STICK_CLICK, A_N_PER_HAND };
static XrAction g_hand_act[2][A_N_PER_HAND];
enum { B_A, B_B, B_X, B_Y, B_MENU, B_N };
static XrAction g_btn[B_N];
static const unsigned g_btn_bit[B_N] = { HRB_A, HRB_B, HRB_X, HRB_Y, HRB_MENU };

static int make(XrActionSet set, XrActionType type, const char *name, const char *shown, XrAction *out) {
    XrActionCreateInfo ai = { XR_TYPE_ACTION_CREATE_INFO };
    ai.actionType = type;
    strncpy(ai.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    strncpy(ai.localizedActionName, shown, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    return XR_SUCCEEDED(p_create(set, &ai, out));
}

int hxb_create(XrInstance inst, XrActionSet set, PFN_xrGetInstanceProcAddr gipa) {
    static const char *const side[2] = { "left", "right" }, *const Side[2] = { "Left", "Right" };
    char n[64], s[64];
    g_ok = 0;
    if (XR_FAILED(gipa(inst, "xrStringToPath", (PFN_xrVoidFunction *)&p_s2p)) ||
        XR_FAILED(gipa(inst, "xrCreateAction", (PFN_xrVoidFunction *)&p_create)) ||
        XR_FAILED(gipa(inst, "xrGetActionStateFloat", (PFN_xrVoidFunction *)&p_float)) ||
        XR_FAILED(gipa(inst, "xrGetActionStateBoolean", (PFN_xrVoidFunction *)&p_bool)) ||
        XR_FAILED(gipa(inst, "xrGetActionStateVector2f", (PFN_xrVoidFunction *)&p_vec2)))
        return 0;
    for (int h = 0; h < 2; ++h) {
        sprintf(n, "%s_trigger", side[h]);      sprintf(s, "%s trigger", Side[h]);
        if (!make(set, XR_ACTION_TYPE_FLOAT_INPUT, n, s, &g_hand_act[h][A_TRIGGER])) return 0;
        sprintf(n, "%s_squeeze", side[h]);      sprintf(s, "%s grip button", Side[h]);
        if (!make(set, XR_ACTION_TYPE_FLOAT_INPUT, n, s, &g_hand_act[h][A_SQUEEZE])) return 0;
        sprintf(n, "%s_stick", side[h]);        sprintf(s, "%s stick", Side[h]);
        if (!make(set, XR_ACTION_TYPE_VECTOR2F_INPUT, n, s, &g_hand_act[h][A_STICK])) return 0;
        sprintf(n, "%s_stick_click", side[h]);  sprintf(s, "%s stick click", Side[h]);
        if (!make(set, XR_ACTION_TYPE_BOOLEAN_INPUT, n, s, &g_hand_act[h][A_STICK_CLICK])) return 0;
    }
    static const char *const bn[B_N] = { "button_a", "button_b", "button_x", "button_y", "button_menu" };
    static const char *const bs[B_N] = { "A button", "B button", "X button", "Y button", "Menu button" };
    for (int b = 0; b < B_N; ++b)
        if (!make(set, XR_ACTION_TYPE_BOOLEAN_INPUT, bn[b], bs[b], &g_btn[b])) return 0;
    g_ok = 1;
    return 1;
}

static XrPath path(XrInstance inst, const char *s) {
    XrPath p = XR_NULL_PATH;
    p_s2p(inst, s, &p);
    return p;
}

/* per family: which input path feeds which action ("" = this family has none) */
typedef struct {
    const char *profile;
    const char *trigger, *squeeze, *stick, *stick_click;   /* both hands, after /user/hand/<side>/input/ */
    const char *a, *b, *x, *y, *menu;                      /* full paths after /user/hand/ */
} Family;
static const Family g_fam[] = {
    { "/interaction_profiles/oculus/touch_controller", "trigger/value", "squeeze/value", "thumbstick", "thumbstick/click",
      "right/input/a/click", "right/input/b/click", "left/input/x/click", "left/input/y/click", "left/input/menu/click" },
    { "/interaction_profiles/valve/index_controller", "trigger/value", "squeeze/value", "thumbstick", "thumbstick/click",
      "right/input/a/click", "right/input/b/click", "left/input/a/click", "left/input/b/click", "" },
    { "/interaction_profiles/htc/vive_controller", "trigger/value", "squeeze/click", "trackpad", "trackpad/click",
      "", "right/input/menu/click", "", "", "left/input/menu/click" },
    { "/interaction_profiles/microsoft/motion_controller", "trigger/value", "squeeze/click", "thumbstick", "thumbstick/click",
      "right/input/trackpad/click", "right/input/menu/click", "left/input/trackpad/click", "", "left/input/menu/click" },
    { "/interaction_profiles/khr/simple_controller", "select/click", "", "", "", "", "", "", "", "left/input/menu/click" },
};

uint32_t hxb_bindings(XrInstance inst, const char *profile, XrActionSuggestedBinding *out, uint32_t n, uint32_t max) {
    static const char *const side[2] = { "left", "right" };
    char p[160];
    if (!g_ok) return n;
    for (size_t f = 0; f < sizeof g_fam / sizeof g_fam[0]; ++f) {
        const Family *fm = &g_fam[f];
        if (strcmp(fm->profile, profile) != 0) continue;
        const char *per_hand[A_N_PER_HAND] = { fm->trigger, fm->squeeze, fm->stick, fm->stick_click };
        for (int h = 0; h < 2; ++h)
            for (int a = 0; a < A_N_PER_HAND; ++a) {
                if (!per_hand[a][0] || n >= max) continue;
                sprintf(p, "/user/hand/%s/input/%s", side[h], per_hand[a]);
                out[n].action = g_hand_act[h][a];
                out[n].binding = path(inst, p);
                ++n;
            }
        const char *btn[B_N] = { fm->a, fm->b, fm->x, fm->y, fm->menu };
        for (int b = 0; b < B_N; ++b) {
            if (!btn[b][0] || n >= max) continue;
            sprintf(p, "/user/hand/%s", btn[b]);
            out[n].action = g_btn[b];
            out[n].binding = path(inst, p);
            ++n;
        }
    }
    return n;
}

static float get_float(XrSession s, XrAction a) {
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
    gi.action = a;
    return XR_SUCCEEDED(p_float(s, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
}
static int get_bool(XrSession s, XrAction a) {
    XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
    XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
    gi.action = a;
    return XR_SUCCEEDED(p_bool(s, &gi, &st)) && st.isActive && st.currentState;
}

void hxb_read(XrSession sess, HrButtons *out) {
    memset(out, 0, sizeof *out);
    if (!g_ok) return;
    for (int h = 0; h < 2; ++h) {
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        XrActionStateVector2f v = { XR_TYPE_ACTION_STATE_VECTOR2F };
        out->trigger[h] = get_float(sess, g_hand_act[h][A_TRIGGER]);
        out->squeeze[h] = get_float(sess, g_hand_act[h][A_SQUEEZE]);
        gi.action = g_hand_act[h][A_STICK];
        if (XR_SUCCEEDED(p_vec2(sess, &gi, &v)) && v.isActive) { out->stick_x[h] = v.currentState.x; out->stick_y[h] = v.currentState.y; }
        if (get_bool(sess, g_hand_act[h][A_STICK_CLICK])) out->buttons |= h ? HRB_RCLICK : HRB_LCLICK;
    }
    for (int b = 0; b < B_N; ++b)
        if (get_bool(sess, g_btn[b])) out->buttons |= g_btn_bit[b];
    out->valid = 1;
}
