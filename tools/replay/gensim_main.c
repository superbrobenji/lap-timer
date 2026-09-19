/* gensim -- generate the committed gps_sim capture header + its replay-expected lap times.
 *
 * usage: gensim <in.log> <in.venue.json> <out sim_capture.h> <out expected.json>
 *
 * Reads a synthetic session .log (produced by `synth --pos-sigma 0`, so the fixes carry no
 * position noise and the on-device lap engine reproduces `replay` exactly) plus its venue side-car.
 * It emits two committed artifacts:
 *
 *   sim_capture.h       the FIX records decoded byte-for-byte the way `replay` decodes them
 *                       (logio on_fix = the reconstructed absolute fix), as a compact sim_fix_t[]
 *                       the gps_sim driver replays at real time, plus the venue JSON string the
 *                       pipeline feeds to trk_from_json (identical code path to replay's
 *                       --venue-json, so the device venue == the replay venue by construction).
 *   expected.json       `replay --mode lap` over the SAME .log + venue: the reference lap times
 *                       the orchestrator compares the on-device console laps against (+/-30 ms).
 *
 * Deterministic: same synth seed + same build => identical bytes. The device feeds the SAME
 * decoded fixes and the SAME venue through the SAME core engine, so device laps == replay laps.
 */
#include "replay/replay.h"
#include "replay/logio.h"
#include "core/trk.h"
#include "core/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FIX 8192

static gps_fix_t g_fix[MAX_FIX];
static uint32_t  g_nfix;

static void on_fix(const gps_fix_t *fix, void *ctx)
{
    (void)ctx;
    if (g_nfix < MAX_FIX) g_fix[g_nfix++] = *fix;
}

/* Read a whole text file into a heap buffer (NUL-terminated). */
static char *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

/* Emit `s` as a C string literal, minifying JSON whitespace so the embedded venue is compact. */
static void emit_json_string(FILE *h, const char *s)
{
    int in_str = 0;
    fputc('"', h);
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (!in_str && (c == ' ' || c == '\n' || c == '\r' || c == '\t')) continue;
        if (c == '"') { in_str = !in_str; fputs("\\\"", h); continue; }
        if (c == '\\') { fputs("\\\\", h); continue; }
        fputc(c, h);
    }
    fputc('"', h);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s <in.log> <in.venue.json> <out.h> <out.expected.json>\n", argv[0]);
        return 2;
    }
    const char *log_path = argv[1], *venue_path = argv[2], *h_path = argv[3], *exp_path = argv[4];

    /* venue: parse exactly as replay does (trk_from_json), and keep the raw JSON to embed. */
    size_t vlen = 0;
    char *vjson = slurp(venue_path, &vlen);
    if (!vjson) { fprintf(stderr, "gensim: cannot read %s\n", venue_path); return 1; }
    trk_venue_t venue;
    char verr[160] = {0};
    if (trk_from_json(&venue, vjson, vlen, verr, sizeof verr) != 0) {
        fprintf(stderr, "gensim: bad venue json: %s\n", verr);
        free(vjson);
        return 1;
    }

    /* fixes: decode the .log exactly as replay does (reconstructed absolute fixes). */
    g_nfix = 0;
    static const logr_cb_t cb = { .on_fix = on_fix };   /* all other members NULL */
    logr_t r;
    logr_init(&r, &cb, NULL);
    if (logr_read_file(&r, log_path) != 0) {
        fprintf(stderr, "gensim: cannot read %s\n", log_path);
        free(vjson);
        return 1;
    }
    if (g_nfix < 2) { fprintf(stderr, "gensim: %s has too few fixes\n", log_path); free(vjson); return 1; }

    /* fix rate from the median-ish first spacing (uniform for a synth capture). */
    int64_t dt_us = g_fix[1].gps_us - g_fix[0].gps_us;
    int rate_hz = (dt_us > 0) ? (int)((1000000 + dt_us / 2) / dt_us) : 5;

    /* expected lap times: replay the SAME .log + venue through the core lap engine. */
    replay_run_t *rr = (replay_run_t *)malloc(sizeof *rr);
    if (!rr) { free(vjson); return 1; }
    if (replay_run(log_path, REPLAY_MODE_LAP, &venue, rr) != 0) {
        fprintf(stderr, "gensim: replay_run failed\n");
        free(rr); free(vjson);
        return 1;
    }
    FILE *ef = fopen(exp_path, "w");
    if (!ef) { fprintf(stderr, "gensim: cannot write %s\n", exp_path); free(rr); free(vjson); return 1; }
    replay_print_run_json(rr, ef);
    fclose(ef);

    /* header */
    FILE *h = fopen(h_path, "w");
    if (!h) { fprintf(stderr, "gensim: cannot write %s\n", h_path); free(rr); free(vjson); return 1; }
    fprintf(h,
        "/* sim_capture.h -- GENERATED, DO NOT EDIT. Committed synthetic GPS capture for gps_sim.\n"
        " *\n"
        " * Regenerate with tools/sim/gen_sim_capture.sh (see that script for the exact synth\n"
        " * command and seed). %u fixes decoded byte-for-byte from the synth .log the way replay\n"
        " * decodes them; the pipeline feeds SIM_VENUE_JSON to trk_from_json (identical to replay's\n"
        " * --venue-json path), so the on-device laps reproduce test/data/sim_capture.expected.json.\n"
        " */\n"
        "#ifndef GPS_SIM_CAPTURE_H\n"
        "#define GPS_SIM_CAPTURE_H\n"
        "#include <stdint.h>\n\n"
        "#define SIM_FIX_COUNT   %uu\n"
        "#define SIM_FIX_RATE_HZ %d\n\n"
        "/* Compact fix: everything the pipeline validity rule (§6.5) and the engines read. mono_us is\n"
        " * assigned by the driver at delivery (real device time), so it is not stored here. */\n"
        "typedef struct {\n"
        "    int64_t  gps_us;\n"
        "    int32_t  lat_e7, lon_e7, alt_mm, gspeed_mms, head_e5;\n"
        "    uint32_t hacc_mm, sacc_mms;\n"
        "    uint16_t pdop_e2;\n"
        "    uint8_t  fix_type, sats, flags;\n"
        "} sim_fix_t;\n\n"
        "static const sim_fix_t SIM_FIXES[SIM_FIX_COUNT] = {\n",
        g_nfix, g_nfix, rate_hz);

    for (uint32_t i = 0; i < g_nfix; i++) {
        const gps_fix_t *f = &g_fix[i];
        fprintf(h,
            "    { %lldLL, %d, %d, %d, %d, %d, %uu, %uu, %uu, %u, %u, 0x%02Xu },\n",
            (long long)f->gps_us, f->lat_e7, f->lon_e7, f->alt_mm, f->gspeed_mms, f->head_e5,
            f->hacc_mm, f->sacc_mms, (unsigned)f->pdop_e2, (unsigned)f->fix_type,
            (unsigned)f->sats, (unsigned)f->flags);
    }
    fputs("};\n\n", h);

    fputs("/* Venue as JSON; the pipeline parses it with trk_from_json (same as replay --venue-json). */\n", h);
    fputs("static const char SIM_VENUE_JSON[] =\n    ", h);
    emit_json_string(h, vjson);
    fputs(";\n\n", h);
    fputs(
        "/* Defined in gps_sim.c (returns SIM_VENUE_JSON above); declared here -- the one header\n"
        " * gps_sim.c and its cross-component caller (pipeline.c) can both see -- so the definition\n"
        " * has a visible prototype (rule 6: -Wmissing-prototypes). Not file-local: pipeline.c calls\n"
        " * it when CFG_GPS_SIM. */\n"
        "const char *gps_sim_venue_json(void);\n\n"
        "#endif /* GPS_SIM_CAPTURE_H */\n", h);
    fclose(h);

    printf("gensim: wrote %s (%u fixes) and %s\n", h_path, g_nfix, exp_path);
    free(rr);
    free(vjson);
    return 0;
}
