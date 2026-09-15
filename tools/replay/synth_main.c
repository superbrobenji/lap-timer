#include "replay/synth_truth.h"
#include "replay/replay.h"
#include <stdio.h>
#include <stdlib.h>

#define PREFIX_CAP 480
#define PATH_CAP   512           /* PREFIX_CAP plus the longest suffix, ".truth.json" */
#define ERR_CAP    160           /* synth_run_build messages are one short sentence */

static void usage(FILE *f)
{
    fprintf(f,
        "usage: synth --out PREFIX [options]\n"
        "Writes PREFIX.log, PREFIX.truth.json and PREFIX.venue.json (spec 22.2).\n"
        "\n"
        "circuit:\n"
        "  --vertices N          polygon vertices                       (12)\n"
        "  --length M            lap length along the driven path, m    (2500)\n"
        "  --radius M            corner arc radius, m                   (40)\n"
        "  --irregularity F      vertex radius jitter, 0..0.9           (0)\n"
        "  --anticlockwise       drive anticlockwise                    (clockwise)\n"
        "  --seed S              seed for the circuit and the noise     (1)\n"
        "run:\n"
        "  --v-corner MPS        constant speed through every arc       (15)\n"
        "  --v-max MPS           straight cruise cap                    (50)\n"
        "  --a-acc MPS2          acceleration on straights              (3)\n"
        "  --a-brk MPS2          braking on straights                   (6)\n"
        "  --lap-var F           per-lap v_max scale 1 +/- F            (0.03)\n"
        "  --laps N              full laps after the first S/F          (10)\n"
        "  --sectors N           sector gates, 0..8                     (2)\n"
        "  --start-before M      out-lap length before S/F, m           (300)\n"
        "  --stop-after M        run-out past the last S/F, m           (200)\n"
        "  --sf-frac F           S/F position as a fraction of length   (0.041667)\n"
        "gps:\n"
        "  --rate HZ             fix rate, 5 or 10                      (5)\n"
        "  --pos-sigma M         Gauss-Markov position sigma, m         (1.5)\n"
        "  --pos-tau S           Gauss-Markov correlation time, s       (20)\n"
        "  --speed-sigma MPS     white speed noise sigma                (0.05)\n"
        "  --head-sigma DEG      white heading noise sigma              (0.5)\n"
        "  --latency MS          mean arrival latency                   (80)\n"
        "  --jitter MS           uniform arrival jitter, +/-            (20)\n"
        "  --dropout T0:T1       drop fixes for T0 <= t < T1, s         (none)\n"
        "  --fused-hz N          FUSED rate, 0 disables                 (10)\n"
        "  --start-utc TS        run time 0 as YYYY-MM-DDTHH:MM:SS      (2026-09-15T10:00:00)\n"
        "output:\n"
        "  --out PREFIX          output path prefix                     (required)\n"
        "  --quiet               no summary line on stdout\n"
        "  --help                this text\n");
}

int main(int argc, char **argv)
{
    synth_cfg_t cfg;
    synth_gps_cfg_t gcfg;
    int fused_hz = 0;
    char prefix[PREFIX_CAP];

    int flags = synth_parse_args(argc, argv, &cfg, &gcfg, &fused_hz, prefix, sizeof prefix);
    if (flags < 0) { usage(stderr); return 2; }
    if (flags & SYNTH_ARG_HELP) { usage(stdout); return 0; }

    char log_path[PATH_CAP], truth_path[PATH_CAP], venue_path[PATH_CAP];
    if (snprintf(log_path, sizeof log_path, "%s.log", prefix) >= (int)sizeof log_path ||
        snprintf(truth_path, sizeof truth_path, "%s.truth.json", prefix) >= (int)sizeof truth_path ||
        snprintf(venue_path, sizeof venue_path, "%s.venue.json", prefix) >= (int)sizeof venue_path) {
        fprintf(stderr, "synth: --out prefix is too long\n");
        return 2;
    }

    /* synth_run_t holds every lap table (a few megabytes), far past any sane stack. */
    synth_run_t *run = (synth_run_t *)malloc(sizeof *run);
    if (!run) { fprintf(stderr, "synth: out of memory\n"); return 1; }

    /* Validate before creating any file, so an impossible circuit reports its own message and
     * leaves no half-written output behind. synth_generate builds the same run again. */
    char err[ERR_CAP];
    err[0] = '\0';
    if (synth_run_build(run, &cfg, err, sizeof err) != 0) {
        fprintf(stderr, "synth: %s\n", err);
        free(run);
        return 1;
    }

    logw_t w;
    if (logw_open_file(&w, log_path) != 0) {
        fprintf(stderr, "synth: cannot write %s\n", log_path);
        free(run);
        return 1;
    }
    uint32_t n_fix = 0, n_fused = 0;
    int rc = synth_generate(&cfg, &gcfg, fused_hz, &w, run, &n_fix, &n_fused);
    if (logw_close(&w) != 0) rc = -1;
    if (rc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", log_path);
        free(run);
        return 1;
    }

    FILE *tf = fopen(truth_path, "w");
    int trc = tf ? synth_truth_write(tf, run, &gcfg, n_fix, n_fused) : -1;
    if (tf && fclose(tf) != 0) trc = -1;
    if (trc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", truth_path);
        free(run);
        return 1;
    }

    FILE *vf = fopen(venue_path, "w");
    int vrc = vf ? synth_venue_write(vf, run) : -1;
    if (vf && fclose(vf) != 0) vrc = -1;
    if (vrc != 0) {
        fprintf(stderr, "synth: cannot write %s\n", venue_path);
        free(run);
        return 1;
    }

    if (!(flags & SYNTH_ARG_QUIET))
        printf("wrote %s: %u fixes, %u fused, %d laps, %.3f s\n",
               log_path, n_fix, n_fused, cfg.laps, run->duration_s);
    free(run);
    return 0;
}
