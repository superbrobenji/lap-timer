#include "core/drag.h"
#include "core/cfg.h"
#include "core/core.h"
#include <stdio.h>

/* Builds the engine's drag_cfg_t from the user's saved cfg_t (spec §6.6/§11.4), and formats §6.6
 * gate names for the DRAG screens. Power of 10 rule 5: per-module assertion code. */
#define DRAGCFG_ASSERT_CODE 0x0AF2

void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out)
{
    CORE_ASSERT_VOID(cfg != NULL, DRAGCFG_ASSERT_CODE);
    CORE_ASSERT_VOID(out != NULL, DRAGCFG_ASSERT_CODE);
    drag_cfg_defaults(out);   /* Ruling R-6 (final review I1/I2): gates[]/n_gates are NEVER touched below */
    out->units   = cfg->units;         /* display only -- core/drag.h's contract keeps gate a/b in km/h */
    out->rollout = cfg->drag.rollout;
    /* Headline bench list is always the km/h list, regardless of cfg->units: SPEED_FROM0 gate
     * thresholds are km/h by core/drag.h's contract, and benches_kmh is what selects which of the
     * default gates are "headline" benches (§11.4) -- it must never be read as mph numbers. Clamp
     * to the smaller of the two array sizes so a future change to either cannot overrun.
     * benches_mph is unused until spec §10's deferred mph-defined gate thresholds (follow-up issue). */
    uint8_t cfg_cap = (uint8_t)(sizeof(cfg->drag.benches_kmh) / sizeof(cfg->drag.benches_kmh[0]));
    uint8_t out_cap = (uint8_t)(sizeof(out->benches_kmh) / sizeof(out->benches_kmh[0]));
    uint8_t cap     = cfg_cap < out_cap ? cfg_cap : out_cap;
    uint8_t n       = cfg->drag.n_kmh;
    if (n > cap) n = cap;
    /* Residual (final review re-review): an empty user bench list must leave drag_cfg_defaults()'s
     * shipped benches (100/200/300) in place, not zero n_benches out from under them. */
    if (n == 0u) return;
    out->n_benches = n;
    for (uint8_t i = 0; i < n; i++) out->benches_kmh[i] = cfg->drag.benches_kmh[i];
    CORE_ASSERT_VOID(out->n_gates <= DRAG_MAX_GATES, DRAGCFG_ASSERT_CODE);   /* untouched by drag_cfg_defaults */
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
