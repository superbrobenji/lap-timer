#ifndef REPLAY_SYNTH_TRUTH_H
#define REPLAY_SYNTH_TRUTH_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "replay/synth.h"
#include "replay/synth_gps.h"
#include "replay/logio.h"

/* The `synth` tool's library half (spec §22.2): the two side-car writers, the record-generation
 * loop and the command-line parser. Split out of synth_main.c so the end-to-end test drives exactly
 * the code the executable runs. Host only, no global state. */

#define SYNTH_LAYOUT_ID 1        /* the one synthetic layout: header, VENUE record and venue JSON */
#define SYNTH_FUSED_HZ  10       /* default --fused-hz (§12.6 internal profile) */

/* Non-negative return of synth_parse_args: a bit set, 0 when neither flag was given. */
#define SYNTH_ARG_QUIET 0x01
#define SYNTH_ARG_HELP  0x02

/* `<prefix>.truth.json`: generator, configuration, geometry and every analytic crossing time.
 * Returns 0, or -1 on a write error or if the document exceeds the 1 MiB build buffer. */
int synth_truth_write(FILE *f, const synth_run_t *r, const synth_gps_cfg_t *g, uint32_t fixes_written, uint32_t fused_written);
/* `<prefix>.venue.json`: synth_run_venue through trk_to_json (§10.2). 0 / -1. */
int synth_venue_write(FILE *f, const synth_run_t *r);
/* Builds `run` from cfg and writes one whole session into `w` (SESSION_HDR, VENUE, TIME_MAP every
 * 60 s, the time-ordered merge of the FIX_* and FUSED streams, END). fused_hz 0 disables FUSED.
 * Returns 0 and the record counts, or -1 on an invalid configuration or a write error. */
int synth_generate(const synth_cfg_t *cfg, const synth_gps_cfg_t *gcfg, int fused_hz, logw_t *w, synth_run_t *run, uint32_t *n_fix, uint32_t *n_fused);
/* Parses `--name value` options into defaults-filled cfg/gps/fused_hz and copies --out into `out`.
 * Returns a SYNTH_ARG_* bit set, or -1 on an unknown option, a bad value or a missing --out. */
int synth_parse_args(int argc, char **argv, synth_cfg_t *cfg, synth_gps_cfg_t *gps, int *fused_hz, char *out, size_t out_cap);
#endif
