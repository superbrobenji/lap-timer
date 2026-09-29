#include "core/drag.h"
#include "core/cfg.h"
#include "core/core.h"
#include <stdio.h>
#include <string.h>

/* Builds the engine's drag_cfg_t from the user's saved cfg_t (spec §6.6/§11.4), and formats §6.6
 * gate names for the DRAG screens. Power of 10 rule 5: per-module assertion code. */
#define DRAGCFG_ASSERT_CODE 0x0AF2

void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out)
{
    CORE_ASSERT_VOID(cfg != NULL, DRAGCFG_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, DRAGCFG_ASSERT_CODE);
    drag_cfg_defaults(out);
    bool            mph = cfg->units == CFG_UNITS_MPH;
    const uint16_t *src = mph ? cfg->drag.benches_mph : cfg->drag.benches_kmh;
    uint8_t         n   = mph ? cfg->drag.n_mph : cfg->drag.n_kmh;
    /* Clamp to the smaller of the two array sizes so a future change to either cannot overrun. */
    uint8_t cfg_cap = (uint8_t)(mph ? sizeof(cfg->drag.benches_mph) / sizeof(cfg->drag.benches_mph[0])
                                     : sizeof(cfg->drag.benches_kmh) / sizeof(cfg->drag.benches_kmh[0]));
    uint8_t out_cap = (uint8_t)(sizeof(out->benches_kmh) / sizeof(out->benches_kmh[0]));
    uint8_t cap     = cfg_cap < out_cap ? cfg_cap : out_cap;
    if (n > cap) n = cap;
    out->units   = mph ? (uint8_t)DRAG_UNITS_MPH : (uint8_t)DRAG_UNITS_KMH;
    out->rollout = cfg->drag.rollout;
    if (n == 0u) return;                          /* no user list: defaults stand */
    out->n_benches = n;
    for (uint8_t i = 0; i < n; i++) {
        out->benches_kmh[i] = src[i];
        if (i < out->n_gates && out->gates[i].kind == DRAG_SPEED_FROM0) out->gates[i].a = src[i];
    }
    CORE_ASSERT_VOID(out->n_gates <= DRAG_MAX_GATES, DRAGCFG_ASSERT_CODE);   /* untouched by the fill above */
}

static int put_num_pair(char *buf, size_t cap, unsigned a, const char *sep, unsigned b)   /* "<a><sep><b>" */
{
    int n = snprintf(buf, cap, "%u%s%u", a, sep, b);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int drag_gate_label(const drag_gate_def_t *g, char *buf, size_t cap)
{
    CORE_ASSERT_RET(g != NULL && buf != NULL, DRAGCFG_ASSERT_CODE, -1);
    CORE_ASSERT_RET(cap >= 8u, DRAGCFG_ASSERT_CODE, -1);
    switch (g->kind) {
    case DRAG_SPEED_FROM0: return put_num_pair(buf, cap, 0u, "-", g->a);
    case DRAG_SPEED_RANGE: return put_num_pair(buf, cap, g->a, "-", g->b);
    case DRAG_BRAKE:       return put_num_pair(buf, cap, g->a, "-", 0u);
    case DRAG_DIST: {
        const char *name = g->a == 1829u ? "60ft" : g->a == 10058u ? "330ft" : g->a == 20117u ? "1/8"
                         : g->a == 30480u ? "1000ft" : g->a == 40234u ? "1/4" : NULL;
        if (name != NULL) { int n = snprintf(buf, cap, "%s", name); return (n < 0 || (size_t)n >= cap) ? -1 : n; }
        int n = snprintf(buf, cap, "%um", (unsigned)(g->a / 100u));
        return (n < 0 || (size_t)n >= cap) ? -1 : n;
    }
    default: buf[0] = '\0'; return -1;
    }
}
