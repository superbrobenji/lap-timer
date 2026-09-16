#include "replay/replay.h"
#include "core/trk.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* replay <file.log> [--json]                              — summary of the decoded records (§22.2)
 * replay --mode lap  [--venue-json P | --venue ID] [--json] <file.log>  — run the lap engine
 * replay --mode drag [--json] <file.log>                                — run the drag engine
 *
 * All logic lives in replaylib. --mode lap resolves the venue from --venue-json P, else --venue ID
 * (a bundled venue), else the <logbase>.venue.json side-car next to the .log (§10.2). --imu (raw IMU
 * through fus_step) is future work (§22.2). */

#define VENUE_JSON_CAP (64 * 1024)

static int usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [--mode lap|drag] [--venue ID | --venue-json PATH] [--json] <file.log>\n"
        "       %s <file.log> [--json]   (record summary; the default when --mode is omitted)\n",
        argv0, argv0);
    return 2;
}

/* Reads the whole file into a NUL-terminated malloc buffer. Returns the buffer (free it) or NULL. */
static char *read_text(const char *path, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = (char *)malloc(cap);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, cap - 1, f);
    int err = ferror(f) || !feof(f);        /* !feof => file bigger than cap */
    fclose(f);
    if (err) { free(buf); return NULL; }
    buf[n] = '\0';
    return buf;
}

/* Fills *out from --venue-json, else --venue id, else <logbase>.venue.json. Returns 0 / -1. */
static int resolve_venue(const char *log_path, const char *venue_json, long venue_id,
                         trk_venue_t *out, const char *self)
{
    if (venue_json) {
        char *js = read_text(venue_json, VENUE_JSON_CAP);
        if (!js) { fprintf(stderr, "%s: cannot read %s\n", self, venue_json); return -1; }
        char err[128];
        int rc = trk_from_json(out, js, strlen(js), err, sizeof err);
        free(js);
        if (rc != 0) { fprintf(stderr, "%s: %s: %s\n", self, venue_json, err); return -1; }
        return 0;
    }
    if (venue_id >= 0) {
        const trk_venue_t *v = trk_get((uint16_t)venue_id);
        if (!v) { fprintf(stderr, "%s: no bundled venue %ld\n", self, venue_id); return -1; }
        *out = *v;
        return 0;
    }
    /* default side-car: <logbase>.venue.json (strip a trailing ".log") */
    size_t n = strlen(log_path);
    const char *ext = (n >= 4 && strcmp(log_path + n - 4, ".log") == 0) ? log_path + n - 4 : log_path + n;
    char side[1024];
    if ((size_t)snprintf(side, sizeof side, "%.*s.venue.json", (int)(ext - log_path), log_path) >= sizeof side) {
        fprintf(stderr, "%s: path too long\n", self);
        return -1;
    }
    char *js = read_text(side, VENUE_JSON_CAP);
    if (!js) { fprintf(stderr, "%s: lap mode needs a venue (--venue-json/--venue or %s)\n", self, side); return -1; }
    char err[128];
    int rc = trk_from_json(out, js, strlen(js), err, sizeof err);
    free(js);
    if (rc != 0) { fprintf(stderr, "%s: %s: %s\n", self, side, err); return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    const char *self = (argc > 0 && argv[0]) ? argv[0] : "replay";
    const char *path = NULL, *venue_json = NULL;
    int mode = REPLAY_MODE_SUMMARY, json = 0;
    long venue_id = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--json") == 0) { if (json) return usage(self); json = 1; }
        else if (strcmp(a, "--mode") == 0) {
            if (++i >= argc) return usage(self);
            if (strcmp(argv[i], "lap") == 0) mode = REPLAY_MODE_LAP;
            else if (strcmp(argv[i], "drag") == 0) mode = REPLAY_MODE_DRAG;
            else return usage(self);
        }
        else if (strcmp(a, "--venue") == 0) {
            if (++i >= argc) return usage(self);
            char *end = NULL;
            venue_id = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0' || venue_id < 0 || venue_id > 65535) return usage(self);
        }
        else if (strcmp(a, "--venue-json") == 0) { if (++i >= argc) return usage(self); venue_json = argv[i]; }
        else if (a[0] == '-' || path) return usage(self);
        else path = a;
    }
    if (!path) return usage(self);

    if (mode == REPLAY_MODE_SUMMARY) {
        replay_summary_t s;
        errno = 0;
        if (replay_summarize_file(path, &s) != 0) {
            fprintf(stderr, "%s: %s: %s\n", self, path, strerror(errno));
            return 1;
        }
        if (json) replay_print_json(&s, stdout);
        else replay_print_text(&s, stdout);
        return 0;
    }

    trk_venue_t venue;
    const trk_venue_t *vp = NULL;
    if (mode == REPLAY_MODE_LAP) {
        if (resolve_venue(path, venue_json, venue_id, &venue, self) != 0) return 1;
        vp = &venue;
    }

    replay_run_t *r = (replay_run_t *)malloc(sizeof *r);
    if (!r) { fprintf(stderr, "%s: out of memory\n", self); return 1; }
    int rc = replay_run(path, mode, vp, r);
    if (rc != 0) {
        fprintf(stderr, "%s: %s: %s\n", self, path, rc == -1 ? strerror(errno) : "replay error");
        free(r);
        return 1;
    }
    if (json) replay_print_run_json(r, stdout);
    else {
        if (mode == REPLAY_MODE_LAP)
            for (uint16_t i = 0; i < r->n_laps; i++)
                printf("lap %u %.3f s flags 0x%02X\n", (unsigned)r->laps[i].lap_no,
                       (double)r->laps[i].time_ms / 1000.0, (unsigned)r->laps[i].flags);
        else
            for (uint16_t i = 0; i < r->n_runs; i++)
                printf("run %u t0 %lld flags 0x%02X trap %u cm/s\n", (unsigned)r->runs[i].run_no,
                       (long long)r->runs[i].t0_gps_us, (unsigned)r->runs[i].flags, (unsigned)r->runs[i].trap_cms);
    }
    free(r);
    return 0;
}
