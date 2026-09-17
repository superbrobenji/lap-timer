/* logger.c -- logger task (spec §4.3 core 0/prio 8/stack 4096; loop §13.3; files §12.1/§12.5-12.7).
 *
 * Consumes the §4.4 fix/fused rings and the logger's event queue, encoding through core/ses into
 * a 4 KB .log batch; rebuilds the .sum (HDR + VENUE + every LAP + every DRAG_RUN [+ END]) via
 * tmp+fsync+atomic-rename on LAP/DRAG_RUN and at close; fsyncs the .log every 2 s; evicts every
 * 60 s. Session open/close arrives over log_req_q. The task is static; the only dynamic wait is
 * a task notification (ring-notify) with a 1000 ms timeout -- no heap, no queue-set object.
 *
 * Session record fidelity: events drained from evt_q are encoded as generic EVENT records (§12.3
 * 0x09 = {mono,gps,code,arg}, which represents any event verbatim, EV_LAP_COMPLETE/EV_DRAG_DONE
 * included). The authoritative, complete records come from the pipeline over result_q (3.4,
 * resolving the 3.3 deferral): logger_submit_lap writes the full LAP (sectors + per-lap stats §9.4)
 * plus a SECTOR record per gate; logger_submit_drag writes the full DRAG_RUN plus a DRAG_GATE per
 * hit gate; logger_set_venue writes the real VENUE name. .sum stays HDR + VENUE + every LAP + every
 * DRAG_RUN [+ END] (§12.5); SECTOR/DRAG_GATE are .log-only. The 3.3 power-cut exit criterion is
 * unaffected (.sum intact via atomic rename; .log decodable with a resync-past-bad truncated tail).
 */
#include "app/logger.h"
#include "app/lt_ipc.h"
#include "app/lt_sup.h"
#include "app/lt_nvs.h"
#include "app/lt_err.h"
#include "hal/storage.h"

#include "core/event.h"
#include "core/ses.h"
#include "core/types.h"

#include "build_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "log";

/* §4.3 task */
#define LOG_CORE         0
#define LOG_PRIO         8
#define LOG_STACK_BYTES  4096
#define LOG_STACK_WORDS  (LOG_STACK_BYTES / sizeof(StackType_t))
#define LOG_STALL_S      5             /* supervisor heartbeat-stall window (§17.2) */

/* §13.3 cadence + buffers */
#define BATCH_CAP        4096
#define BATCH_FLUSH_B    3584          /* write when the batch reaches this */
#define WRITE_INTERVAL_MS 1000
#define SYNC_INTERVAL_MS  2000
#define EVICT_INTERVAL_MS 60000
#define LOOP_TIMEOUT_MS   1000
#define FRAME_TMP_CAP    256           /* >= max framed record (247 payload + 5) */

/* §12.5 .sum assembly (sized so HDR+VENUE+laps+drags+END always fit BATCH_CAP). */
#define SUM_BUILD_CAP    4096
#define LAP_ACC_CAP      3072
#define DRAG_ACC_CAP     512

static StaticTask_t s_tcb;
static StackType_t  s_stack[LOG_STACK_WORDS];
static TaskHandle_t s_task;

/* open session */
static sto_file_t s_log_fd;
static bool       s_open;
static char       s_id[11];            /* "S%05u_%03u" + NUL */
static uint8_t    s_seq;               /* per-boot session sequence (§12.1) */
static ses_hdr_t  s_hdr;
static uint16_t   s_venue_id, s_layout_id;
static char       s_venue_name[33];

/* codecs + batch */
static ses_fix_state_t   s_fix_st;
static ses_fused_state_t s_fused_st;
static uint8_t s_batch[BATCH_CAP];
static size_t  s_batch_len;

/* .sum accumulators (encoded LAP / DRAG_RUN frames, in emission order) */
static uint8_t s_lap_acc[LAP_ACC_CAP];   static size_t s_lap_len;
static uint8_t s_drag_acc[DRAG_ACC_CAP]; static size_t s_drag_len;
static bool    s_sum_dirty;

/* timing + state */
static uint32_t s_last_write_ms, s_last_sync_ms, s_last_evict_ms;
static bool     s_samples_full;        /* SYS_STORAGE_FULL: sample logging paused, summaries continue */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Bounded copy into a fixed on-wire header field (§12): copies at most cap-1 bytes and always
 * NUL-terminates. Truncation of an over-long value (e.g. a git-describe dev version longer than the
 * 16-byte fw field) is intentional and expressed without tripping -Werror=format-truncation. */
static void hdr_set(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void log_path(char *out, size_t cap, const char *id, const char *ext)
{
    (void)snprintf(out, cap, "/sessions/%s%s", id, ext);
}

/* Write the accumulated batch to the open .log. */
static void do_write(void)
{
    if (!s_open || s_batch_len == 0) return;
    if (sto_write(s_log_fd, s_batch, s_batch_len) != 0) errlog_add(E_STO_WRITE, (uint32_t)s_batch_len);
    s_batch_len = 0;
    s_last_write_ms = now_ms();
}

/* Append one framed record to the batch, flushing first if it would not fit. */
static void batch_append(const uint8_t *frame, int n)
{
    if (n <= 0) return;
    if (s_batch_len + (size_t)n > BATCH_CAP) do_write();
    memcpy(s_batch + s_batch_len, frame, (size_t)n);
    s_batch_len += (size_t)n;
}

static void acc_append(uint8_t *acc, size_t *len, size_t cap, const uint8_t *frame, int n)
{
    if (n <= 0 || *len + (size_t)n > cap) return;   /* beyond cap: kept in .log only (§12.5) */
    memcpy(acc + *len, frame, (size_t)n);
    *len += (size_t)n;
}

/* §12.5: rebuild .sum = HDR + VENUE + every LAP + every DRAG_RUN [+ END] via tmp+sync+rename. */
static void rebuild_sum(bool closing, int64_t end_gps_us, uint8_t end_reason)
{
    if (s_id[0] == 0) return;
    static uint8_t buf[SUM_BUILD_CAP];
    size_t off = 0;
    int n;

    n = ses_encode_hdr(&s_hdr, buf + off, SUM_BUILD_CAP - off);
    if (n < 0) return;
    off += (size_t)n;
    n = ses_encode_venue(s_venue_id, s_layout_id, s_venue_name, buf + off, SUM_BUILD_CAP - off);
    if (n > 0) off += (size_t)n;
    if (off + s_lap_len <= SUM_BUILD_CAP)  { memcpy(buf + off, s_lap_acc, s_lap_len);   off += s_lap_len; }
    if (off + s_drag_len <= SUM_BUILD_CAP) { memcpy(buf + off, s_drag_acc, s_drag_len); off += s_drag_len; }
    if (closing) {
        n = ses_encode_end(end_gps_us, end_reason, buf + off, SUM_BUILD_CAP - off);
        if (n > 0) off += (size_t)n;
    }

    char tmp_path[40], sum_path[40];
    log_path(tmp_path, sizeof tmp_path, s_id, ".sum.tmp");
    log_path(sum_path, sizeof sum_path, s_id, ".sum");
    sto_file_t f;
    if (sto_open(tmp_path, STO_WR | STO_CREATE, &f) != 0) { errlog_add(E_STO_WRITE, 0); return; }
    int rc = sto_write(f, buf, off);
    if (rc != 0) errlog_add(E_STO_WRITE, (uint32_t)off);
    if (rc == 0) {
        rc = sto_sync(f);
        if (rc != 0) errlog_add(E_STO_WRITE, 0);
    }
    sto_close(f);
    if (rc == 0 && sto_rename(tmp_path, sum_path) != 0) errlog_add(E_STO_WRITE, 0);   /* atomic (§13.1) */
}

static void eviction_check(void);   /* forward decl: called from open_session (§12.7 "at session start") and the main loop */

static void open_session(const log_request_t *req)
{
    if (s_open) return;                            /* one open .log at a time */
    if (s_seq < 0xFF) s_seq++;
    (void)snprintf(s_id, sizeof s_id, "S%05u_%03u",
                   (unsigned)(lt_nvs_boot_get() & 0xFFFFu), (unsigned)s_seq);

    memset(&s_hdr, 0, sizeof s_hdr);
    hdr_set(s_hdr.session_id, sizeof s_hdr.session_id, s_id);
    s_hdr.mode      = req->mode;
    s_hdr.variant   = (uint8_t)(CFG_VARIANT_MOTO ? 0 : 1);
    s_hdr.venue_id  = req->venue_id;
    s_hdr.layout_id = req->layout_id;
    hdr_set(s_hdr.fw,   sizeof s_hdr.fw,   CFG_FW_VERSION);
    hdr_set(s_hdr.hwid, sizeof s_hdr.hwid, CFG_HWID);
    s_hdr.log_profile  = 0;
    s_hdr.fused_hz     = (uint8_t)CFG_FUSED_LOG_HZ;
    s_hdr.gps_hz       = 0;                         /* real rate filled by the pipeline (3.4) */
    s_hdr.start_gps_us = req->gps_us;               /* calib block left zero until 3.4 */

    s_venue_id = req->venue_id;
    s_layout_id = req->layout_id;
    s_venue_name[0] = 0;

    ses_fix_state_init(&s_fix_st);
    ses_fused_state_init(&s_fused_st);
    s_batch_len = 0;
    s_lap_len = 0;
    s_drag_len = 0;
    s_sum_dirty = false;
    s_samples_full = false;

    char path[40];
    log_path(path, sizeof path, s_id, ".log");
    if (sto_open(path, STO_WR | STO_APPEND | STO_CREATE, &s_log_fd) != 0) {
        errlog_add(E_STO_WRITE, 0);
        s_id[0] = 0;
        return;
    }
    s_open = true;
    s_last_write_ms = s_last_sync_ms = now_ms();

    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_hdr(&s_hdr, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();                                     /* flush HDR now: a cut right after open still yields a valid .log */
    sto_sync(s_log_fd);                              /* and sync it: a cut right after open must not lose the .log HDR either */
    rebuild_sum(false, 0, 0);                        /* initial .sum: HDR + VENUE */
    eviction_check();                                /* §12.7: eviction runs at session start, not only every 60 s */
    ESP_LOGI(TAG, "session %s open", s_id);
}

static void close_session(const log_request_t *req)
{
    if (!s_open) return;
    do_write();
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_end(req->gps_us, req->reason, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();
    sto_sync(s_log_fd);
    sto_close(s_log_fd);
    s_open = false;
    rebuild_sum(true, req->gps_us, req->reason);     /* finalise .sum with END */
    ESP_LOGI(TAG, "session %s closed (reason %u)", s_id, (unsigned)req->reason);
}

static void handle_request(const log_request_t *req)
{
    switch (req->type) {
    case LOGGER_OPEN_SESSION:    open_session(req); break;
    case LOGGER_CLOSE_SESSION:   close_session(req); break;
    case LOGGER_REBUILD_SUMMARY: if (s_open) rebuild_sum(false, 0, 0); break;
    case LOGGER_EVICT:           s_last_evict_ms = now_ms() - EVICT_INTERVAL_MS; break;   /* force an eviction pass this loop */
    default: break;
    }
}

static void handle_event(const event_t *ev)
{
    /* Every event -- EV_LAP_COMPLETE / EV_DRAG_DONE included -- is logged verbatim as a generic
     * EVENT record (§12.3). The authoritative full LAP / DRAG_RUN records now arrive over result_q
     * (handle_result), so the logger no longer synthesises a minimal one from the event payload. */
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_event(ev->mono_us, ev->gps_us, ev->type, ev->arg32, tmp, sizeof tmp);
    batch_append(tmp, n);
}

/* §12.3 SECTOR / DRAG_GATE / real VENUE from the pipeline's full engine result (result_q). */
static void handle_result(const log_result_t *res)
{
    if (!s_open) return;                             /* nothing to write without an open .log */
    uint8_t tmp[FRAME_TMP_CAP];
    int n;

    if (res->kind == LOG_RES_LAP) {
        const lap_result_t *lap = &res->u.lap;
        n = ses_encode_lap(lap, tmp, sizeof tmp);    /* full record: sectors + per-lap stats §9.4 */
        batch_append(tmp, n);
        acc_append(s_lap_acc, &s_lap_len, LAP_ACC_CAP, tmp, n);   /* .sum carries every LAP (§12.5) */
        /* One SECTOR record per sector gate: crossing gps_us reconstructed from the cumulative
         * splits (exact -- the splits are the crossing differences), delta left 0. */
        int64_t cum_us = lap->start_gps_us;
        uint8_t gates = (lap->n_sectors > 0) ? (uint8_t)(lap->n_sectors - 1u) : 0u;
        for (uint8_t j = 0; j < gates && j < LAP_MAX_SECTORS; j++) {
            cum_us += (int64_t)lap->sector_ms[j] * 1000;
            n = ses_encode_sector(lap->lap_no, (uint8_t)(j + 1u), cum_us, lap->sector_ms[j], 0,
                                  tmp, sizeof tmp);
            batch_append(tmp, n);                    /* SECTOR is .log-only */
        }
        s_sum_dirty = true;
    } else if (res->kind == LOG_RES_DRAG) {
        const drag_result_t *run = &res->u.drag;
        n = ses_encode_drag_run(run, tmp, sizeof tmp);
        batch_append(tmp, n);
        acc_append(s_drag_acc, &s_drag_len, DRAG_ACC_CAP, tmp, n);
        for (uint8_t i = 0; i < run->n_gates && i < DRAG_MAX_GATES; i++) {
            const drag_gate_res_t *g = &run->gates[i];
            if (!g->hit) continue;
            int64_t g_gps_us = run->t0_gps_us + (int64_t)g->time_ms * 1000;
            n = ses_encode_drag_gate(run->run_no, g->gate_id, g_gps_us, g->time_ms,
                                     g->speed_cms, g->dist_cm, tmp, sizeof tmp);
            batch_append(tmp, n);                    /* DRAG_GATE is .log-only */
        }
        s_sum_dirty = true;
    } else if (res->kind == LOG_RES_VENUE) {
        s_venue_id  = res->u.venue.venue_id;
        s_layout_id = res->u.venue.layout_id;
        (void)snprintf(s_venue_name, sizeof s_venue_name, "%s", res->u.venue.name);
        s_hdr.venue_id  = s_venue_id;                /* keep the .sum HDR consistent */
        s_hdr.layout_id = s_layout_id;
        s_sum_dirty = true;                          /* rebuild .sum with the real VENUE record */
    }
}

static void drain_results(void)
{
    log_result_t res;
    while (xQueueReceive(g_result_q, &res, 0) == pdTRUE) handle_result(&res);
}

static void drain_rings(void)
{
    /* §17.5 safe mode: FIX/FUSED sample records are dropped (still popped off the ring so it
     * does not back up) while SESSION_HDR/VENUE/LAP/DRAG_RUN/END keep flowing (handle_result,
     * open_session, close_session) so summaries and the .sum still form. Same gate as the
     * SYS_STORAGE_FULL sample pause below. */
    bool suppress = s_samples_full || (sys_flags_get() & (1u << SYS_SAFE_MODE)) != 0;

    gps_fix_t fix;
    while (ring_pop(&g_fix_ring, &fix)) {
        if (s_open && !suppress) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fix(&s_fix_st, &fix, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
        ses_fused_state_on_fix(&s_fused_st, fix.gps_us);   /* keep fused deltas referenced to fixes */
    }
    fused_sample_t fs;
    while (ring_pop(&g_fused_ring, &fs)) {
        if (s_open && !suppress) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fused(&s_fused_st, &fs, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
    }
}

static void drain_events(void)
{
    event_t ev;
    while (xQueueReceive(g_evt_q, &ev, 0) == pdTRUE) {
        if (s_open) handle_event(&ev);
    }
}

typedef struct { char oldest[24]; char curlog[24]; } evict_ctx_t;
static void evict_cb(const char *name, uint32_t size, void *ctx)
{
    (void)size;
    evict_ctx_t *e = (evict_ctx_t *)ctx;
    size_t len = strlen(name);
    if (len < 4 || strcmp(name + len - 4, ".log") != 0) return;   /* only .log (never .sum) */
    if (strcmp(name, e->curlog) == 0) return;                     /* never the current session */
    if (e->oldest[0] == 0 || strcmp(name, e->oldest) < 0)
        (void)snprintf(e->oldest, sizeof e->oldest, "%s", name);  /* smallest id == oldest (§12.7) */
}

static void eviction_check(void)
{
    sto_info_t si;
    if (sto_info(&si) != 0 || si.total_kb == 0) return;
    if (si.free_kb >= si.total_kb / 10u) {
        if (s_samples_full) { s_samples_full = false; sys_flags_clear(SYS_STORAGE_FULL); }
        return;
    }
    /* free < 10 %: delete the oldest .log that is not the current session. */
    evict_ctx_t e;
    memset(&e, 0, sizeof e);
    if (s_id[0]) (void)snprintf(e.curlog, sizeof e.curlog, "%s.log", s_id);
    sto_list("/sessions", evict_cb, &e);
    if (e.oldest[0]) {
        char p[40];
        (void)snprintf(p, sizeof p, "/sessions/%s", e.oldest);
        sto_unlink(p);
        errlog_add(E_STO_EVICT, 0);
        ESP_LOGW(TAG, "evicted %s (free %u/%u KB)", e.oldest, (unsigned)si.free_kb, (unsigned)si.total_kb);
    } else if (si.free_kb < si.total_kb / 20u) {    /* nothing to delete and < 5 %: pause samples */
        if (!s_samples_full) { s_samples_full = true; sys_flags_set(SYS_STORAGE_FULL); errlog_add(E_STO_FULL, 0); }
    }
}

static void logger_task(void *arg)
{
    (void)arg;
    sup_register_task(HB_LOGGER, xTaskGetCurrentTaskHandle(), LOG_STALL_S);
    s_last_evict_ms = now_ms();
    ESP_LOGI(TAG, "logger up (core %d prio %d)", LOG_CORE, LOG_PRIO);

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOOP_TIMEOUT_MS));   /* ring-notify or 1000 ms */

        log_request_t req;
        while (xQueueReceive(g_log_req_q, &req, 0) == pdTRUE) handle_request(&req);

        drain_rings();
        drain_events();
        drain_results();

        uint32_t now = now_ms();
        if (s_open && s_batch_len &&
            (s_batch_len >= BATCH_FLUSH_B || (now - s_last_write_ms) >= WRITE_INTERVAL_MS))
            do_write();
        if (s_open && (now - s_last_sync_ms) >= SYNC_INTERVAL_MS) {
            sto_sync(s_log_fd);
            s_last_sync_ms = now;
        }
        if (s_sum_dirty && s_open) { rebuild_sum(false, 0, 0); s_sum_dirty = false; }
        if ((now - s_last_evict_ms) >= EVICT_INTERVAL_MS) { eviction_check(); s_last_evict_ms = now_ms(); }

        g_hb[HB_LOGGER]++;
    }
}

void logger_start(void)
{
    if (s_task) return;
    s_task = xTaskCreateStaticPinnedToCore(logger_task, "logger", LOG_STACK_WORDS, NULL,
                                           LOG_PRIO, s_stack, &s_tcb, LOG_CORE);
}

void logger_notify(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}

const char *logger_open_session_id(void)
{
    /* F5: the id the logger currently holds open for writing, or NULL if none. Read cross-task by
     * the console's DELETE guard -- a benign race (the id only changes on open/close), enough to
     * refuse unlinking a live session's .log/.sum. */
    return s_open ? s_id : NULL;
}

/* Pipeline -> logger full results. Copy-by-value onto result_q + wake the logger; a momentarily
 * full queue drops the result (the .log still carries the EVENT record). Safe from the pipeline. */
static void submit(const log_result_t *r)
{
    if (!g_result_q) return;
    if (xQueueSend(g_result_q, r, 0) == pdTRUE) logger_notify();
}

void logger_submit_lap(const lap_result_t *lap)
{
    if (!lap) return;
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_LAP;
    r.u.lap = *lap;
    submit(&r);
}

void logger_submit_drag(const drag_result_t *run)
{
    if (!run) return;
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_DRAG;
    r.u.drag = *run;
    submit(&r);
}

void logger_set_venue(uint16_t venue_id, uint16_t layout_id, const char *name)
{
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_VENUE;
    r.u.venue.venue_id = venue_id;
    r.u.venue.layout_id = layout_id;
    if (name) (void)snprintf(r.u.venue.name, sizeof r.u.venue.name, "%s", name);
    submit(&r);
}
