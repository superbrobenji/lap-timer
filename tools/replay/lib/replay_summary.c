#include "replay/replay.h"
#include "replay/logio.h"
#include "core/jw.h"
#include <stdlib.h>
#include <string.h>

/* Summary of one .log (spec §12, §22.2).
 *
 * JSON contract of replay_print_json — these keys are stable; test_replay_summary.c and, from
 * session 2.7, the test/data/<name>.expected.json fixtures compare against them:
 *
 *   version      string  replay_version() (== core_version())
 *   frames       number  frames the framing layer accepted
 *   bad_frames   number  decoder rejections + unknown record types + framing failures
 *   by_type      object  { "<tt>": count } where <tt> is the record type as two lowercase hex
 *                        digits ("02" = FIX_KEY, "7f" = END); only non-zero types appear
 *   hdr          object  { session_id, mode, variant, venue_id, layout_id, fw, hwid, gps_hz,
 *                          fused_hz, start_gps_us }, or null when the log has no SESSION_HDR
 *   venue        object  { id, layout_id, name }, or null when the log has no VENUE
 *   fix          object  { n, valid, first_gps_us, last_gps_us, max_gspeed_mms }
 *   fused        number  FUSED records decoded
 *   laps         array   the stored laps (the first 64 LAP records), each
 *                        { lap_no, start_gps_us, time_ms, flags, n_sectors, sector_ms: [...] }
 *   n_laps       number  total LAP records, which may exceed the length of "laps"
 *   sectors      number  SECTOR records
 *   drag_runs    number  DRAG_RUN records
 *   drag_gates   number  DRAG_GATE records
 *   events       number  EVENT records
 *   time_maps    number  TIME_MAP records
 *   end          object  { gps_us, reason }, or null when the log has no END
 */

/* One summary object: 64 laps x 9 sector splits plus the scalar fields is well under 16 KB; 64 KB
 * leaves room for the session 2.7 additions without a second pass. */
#define REPLAY_JSON_CAP 65536
/* Capacity of replay_summary_t.laps, taken from the struct so the two can never disagree. */
#define REPLAY_LAPS_CAP ((uint16_t)(sizeof(((replay_summary_t *)0)->laps) / sizeof(lap_result_t)))

/* mm/s -> km/h: x 3600 s/h / 1e6 mm/km. */
static const double MMS_TO_KMH = 0.0036;

static void on_hdr(const ses_hdr_t *h, void *ctx)
{
    replay_summary_t *s = ctx; s->hdr = *h; s->have_hdr = 1;
}

static void on_venue(const ses_venue_t *v, void *ctx)
{
    replay_summary_t *s = ctx; s->venue = *v; s->have_venue = 1;
}

static void on_time_map(const ses_time_map_t *t, void *ctx)
{
    replay_summary_t *s = ctx; (void)t; s->n_time_map++;
}

static void on_fix(const gps_fix_t *f, void *ctx)
{
    replay_summary_t *s = ctx;
    if (s->n_fix == 0) s->first_fix_gps_us = f->gps_us;
    s->last_fix_gps_us = f->gps_us;
    s->n_fix++;
    if (f->valid) s->n_fix_valid++;
    /* max over a set that starts at 0: a log of only negative Doppler speeds reports 0. */
    if (f->gspeed_mms > s->max_gspeed_mms) s->max_gspeed_mms = f->gspeed_mms;
}

static void on_fused(const fused_sample_t *fs, void *ctx)
{
    replay_summary_t *s = ctx; (void)fs; s->n_fused++;
}

static void on_lap(const lap_result_t *l, void *ctx)
{
    replay_summary_t *s = ctx;
    if (s->n_laps_listed < REPLAY_LAPS_CAP) s->laps[s->n_laps_listed++] = *l;
    s->n_lap++;
}

static void on_sector(const ses_sector_t *sec, void *ctx)
{
    replay_summary_t *s = ctx; (void)sec; s->n_sector++;
}

static void on_drag_run(const drag_result_t *run, void *ctx)
{
    replay_summary_t *s = ctx; (void)run; s->n_drag_run++;
}

static void on_drag_gate(const ses_drag_gate_t *g, void *ctx)
{
    replay_summary_t *s = ctx; (void)g; s->n_drag_gate++;
}

static void on_event(const ses_event_t *e, void *ctx)
{
    replay_summary_t *s = ctx; (void)e; s->n_event++;
}

static void on_end(const ses_end_t *e, void *ctx)
{
    replay_summary_t *s = ctx; s->end = *e; s->have_end = 1;
}

int replay_summarize_file(const char *path, replay_summary_t *out)
{
    static const logr_cb_t cb = {
        .on_hdr = on_hdr, .on_venue = on_venue, .on_time_map = on_time_map, .on_fix = on_fix,
        .on_fused = on_fused, .on_lap = on_lap, .on_sector = on_sector, .on_drag_run = on_drag_run,
        .on_drag_gate = on_drag_gate, .on_event = on_event, .on_calib = NULL, .on_mark = NULL,
        .on_power = NULL, .on_end = on_end, .on_bad = NULL,
    };
    memset(out, 0, sizeof *out);
    logr_t r;
    logr_init(&r, &cb, out);
    if (logr_read_file(&r, path) != 0) return -1;
    out->n_frames = r.n_frames;
    /* Both kinds of loss are reported together: a decoder rejection (framed but malformed) and a
     * framing failure (bad CRC, impossible length, truncated tail at EOF). */
    out->n_bad = r.n_bad + r.rd.frames_bad;
    memcpy(out->n_by_type, r.n_by_type, sizeof out->n_by_type);
    return 0;
}

void replay_print_text(const replay_summary_t *s, FILE *f)
{
    fprintf(f, "frames %u (bad %u)\n", (unsigned)s->n_frames, (unsigned)s->n_bad);
    if (s->have_hdr)
        fprintf(f, "hdr %s mode %u venue %u/%u gps %u Hz fused %u Hz\n",
                s->hdr.session_id, (unsigned)s->hdr.mode, (unsigned)s->hdr.venue_id,
                (unsigned)s->hdr.layout_id, (unsigned)s->hdr.gps_hz, (unsigned)s->hdr.fused_hz);
    if (s->have_venue)
        fprintf(f, "venue %u/%u %s\n", (unsigned)s->venue.venue_id, (unsigned)s->venue.layout_id, s->venue.name);
    if (s->n_time_map) fprintf(f, "time_map %u\n", (unsigned)s->n_time_map);
    if (s->n_fix)
        fprintf(f, "fix %u valid %u span %.3f s max %.1f km/h\n", (unsigned)s->n_fix, (unsigned)s->n_fix_valid,
                (double)(s->last_fix_gps_us - s->first_fix_gps_us) / 1e6, (double)s->max_gspeed_mms * MMS_TO_KMH);
    if (s->n_fused) fprintf(f, "fused %u\n", (unsigned)s->n_fused);
    for (uint16_t i = 0; i < s->n_laps_listed; i++) {
        const lap_result_t *l = &s->laps[i];
        fprintf(f, "lap %u %.3f s flags 0x%02X", (unsigned)l->lap_no, (double)l->time_ms / 1000.0, (unsigned)l->flags);
        if (l->n_sectors) {
            fputs(" sectors", f);
            for (uint8_t k = 0; k < l->n_sectors; k++) fprintf(f, " %.1f", (double)l->sector_ms[k] / 1000.0);
        }
        fputc('\n', f);
    }
    if (s->n_lap > s->n_laps_listed) fprintf(f, "laps %u (%u listed)\n", (unsigned)s->n_lap, (unsigned)s->n_laps_listed);
    if (s->n_sector) fprintf(f, "sector %u\n", (unsigned)s->n_sector);
    if (s->n_drag_run) fprintf(f, "drag_run %u\n", (unsigned)s->n_drag_run);
    if (s->n_drag_gate) fprintf(f, "drag_gate %u\n", (unsigned)s->n_drag_gate);
    if (s->n_event) fprintf(f, "event %u\n", (unsigned)s->n_event);
    if (s->have_end) fprintf(f, "end reason %u at %lld\n", (unsigned)s->end.reason, (long long)s->end.gps_us);
}

void replay_print_json(const replay_summary_t *s, FILE *f)
{
    static const char hexdig[] = "0123456789abcdef";
    char *buf = malloc(REPLAY_JSON_CAP);
    if (!buf) { fputs("{\"error\":\"overflow\"}\n", f); return; }
    jw_t w;
    jw_init(&w, buf, REPLAY_JSON_CAP);
    jw_obj_open(&w);

    jw_key(&w, "version"); jw_str(&w, replay_version());
    jw_key(&w, "frames"); jw_uint(&w, s->n_frames);
    jw_key(&w, "bad_frames"); jw_uint(&w, s->n_bad);

    jw_key(&w, "by_type"); jw_obj_open(&w);
    for (unsigned t = 0; t < sizeof s->n_by_type / sizeof s->n_by_type[0]; t++) {
        if (s->n_by_type[t] == 0) continue;
        char key[3] = { hexdig[(t >> 4) & 0xFu], hexdig[t & 0xFu], '\0' };
        jw_key(&w, key); jw_uint(&w, s->n_by_type[t]);
    }
    jw_obj_close(&w);

    jw_key(&w, "hdr");
    if (s->have_hdr) {
        jw_obj_open(&w);
        jw_key(&w, "session_id"); jw_str(&w, s->hdr.session_id);
        jw_key(&w, "mode"); jw_uint(&w, s->hdr.mode);
        jw_key(&w, "variant"); jw_uint(&w, s->hdr.variant);
        jw_key(&w, "venue_id"); jw_uint(&w, s->hdr.venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, s->hdr.layout_id);
        jw_key(&w, "fw"); jw_str(&w, s->hdr.fw);
        jw_key(&w, "hwid"); jw_str(&w, s->hdr.hwid);
        jw_key(&w, "gps_hz"); jw_uint(&w, s->hdr.gps_hz);
        jw_key(&w, "fused_hz"); jw_uint(&w, s->hdr.fused_hz);
        jw_key(&w, "start_gps_us"); jw_int(&w, s->hdr.start_gps_us);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_key(&w, "venue");
    if (s->have_venue) {
        jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, s->venue.venue_id);
        jw_key(&w, "layout_id"); jw_uint(&w, s->venue.layout_id);
        jw_key(&w, "name"); jw_str(&w, s->venue.name);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_key(&w, "fix"); jw_obj_open(&w);
    jw_key(&w, "n"); jw_uint(&w, s->n_fix);
    jw_key(&w, "valid"); jw_uint(&w, s->n_fix_valid);
    jw_key(&w, "first_gps_us"); jw_int(&w, s->first_fix_gps_us);
    jw_key(&w, "last_gps_us"); jw_int(&w, s->last_fix_gps_us);
    jw_key(&w, "max_gspeed_mms"); jw_int(&w, s->max_gspeed_mms);
    jw_obj_close(&w);

    jw_key(&w, "fused"); jw_uint(&w, s->n_fused);

    jw_key(&w, "laps"); jw_arr_open(&w);
    for (uint16_t i = 0; i < s->n_laps_listed; i++) {
        const lap_result_t *l = &s->laps[i];
        jw_obj_open(&w);
        jw_key(&w, "lap_no"); jw_uint(&w, l->lap_no);
        jw_key(&w, "start_gps_us"); jw_int(&w, l->start_gps_us);
        jw_key(&w, "time_ms"); jw_uint(&w, l->time_ms);
        jw_key(&w, "flags"); jw_uint(&w, l->flags);
        jw_key(&w, "n_sectors"); jw_uint(&w, l->n_sectors);
        jw_key(&w, "sector_ms"); jw_arr_open(&w);
        for (uint8_t k = 0; k < l->n_sectors; k++) jw_uint(&w, l->sector_ms[k]);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_key(&w, "n_laps"); jw_uint(&w, s->n_lap);

    jw_key(&w, "sectors"); jw_uint(&w, s->n_sector);
    jw_key(&w, "drag_runs"); jw_uint(&w, s->n_drag_run);
    jw_key(&w, "drag_gates"); jw_uint(&w, s->n_drag_gate);
    jw_key(&w, "events"); jw_uint(&w, s->n_event);
    jw_key(&w, "time_maps"); jw_uint(&w, s->n_time_map);

    jw_key(&w, "end");
    if (s->have_end) {
        jw_obj_open(&w);
        jw_key(&w, "gps_us"); jw_int(&w, s->end.gps_us);
        jw_key(&w, "reason"); jw_uint(&w, s->end.reason);
        jw_obj_close(&w);
    } else {
        jw_null(&w);
    }

    jw_obj_close(&w);
    if (jw_overflow(&w)) fputs("{\"error\":\"overflow\"}\n", f);
    else { fputs(buf, f); fputc('\n', f); }
    free(buf);
}
