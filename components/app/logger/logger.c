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
#include "app/lt_assert.h"
#include "app/lt_ipc.h"
#include "app/lt_sup.h"
#include "app/lt_nvs.h"
#include "app/lt_err.h"
#include "app/status.h"      /* status_cache_update() -- logger.c owns storage, feeds the cache (Plan 5.6 T1 fix 1) */
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

#define LOG_ASSERT_CODE 0x0B20   /* Power of 10 rule 5 (app/lt_assert.h); logger.c's own code */

static const char *TAG = "log";

/* §4.3 task */
#define LOG_CORE         0
#define LOG_PRIO         8
#define LOG_STACK_BYTES  4096
#define LOG_STACK_WORDS  (LOG_STACK_BYTES / sizeof(StackType_t))
#define LOG_STALL_S      5             /* supervisor heartbeat-stall window (§17.2) */

/* §13.3 cadence + buffers. BATCH_CAP is right-sized to the real worst case: batch_append flushes
 * before the batch would exceed it, and the size-flush fires at BATCH_FLUSH_B, so the most content
 * ever held is BATCH_FLUSH_B + one max real frame (DRAG_RUN, 16 gates = 211 B, §12.3) = 3795 B; the
 * FRAME_TMP_CAP (256) append ceiling keeps this <= 3840 for any frame. .log bytes are unaffected --
 * only the number of sto_write chunks can change, never the concatenated file content. */
#define BATCH_CAP        3840
#define BATCH_FLUSH_B    3584          /* write when the batch reaches this */
#define WRITE_INTERVAL_MS 1000
#define SYNC_INTERVAL_MS  2000
#define EVICT_INTERVAL_MS 60000
#define LOOP_TIMEOUT_MS   1000
#define FRAME_TMP_CAP    256           /* >= max framed record (247 payload + 5) */

/* §12.5 .sum assembly: LAP/DRAG frames accumulate here, then rebuild_sum streams HDR+VENUE+laps+
 * drags+END straight to the .sum fd (A1) -- no assemble-then-emit scratch. */
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

/* status.h cache, maintained incrementally (Plan 5.6 T1 fix 3 -- no periodic storage rescan):
 * s_sessions_cached/s_free_kb_cached are this task's own mirror of what it last pushed via
 * status_cache_update() (status.h has no getter); s_bytes_since_info is .log bytes written
 * (do_write()) since s_free_kb_cached was last a REAL storage_free_kb() reading, so
 * status_cache_estimate() can publish a storage-free estimate between real refreshes. */
static uint16_t s_sessions_cached;
static uint32_t s_free_kb_cached;
static uint32_t s_bytes_since_info;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* storage_free_kb/session_count (Plan 5.6 T1 fix 1, moved verbatim from cmd.c's op_status):
 * hal/storage.h's own contract is "called from one task only (logger, plus cmd for read-only
 * listing/export)" -- this task IS that owner, so these two live here now and feed the
 * status.h cache (status_cache_update, called below) instead of running on whichever task
 * calls status_build(). */
static uint32_t storage_free_kb(void)
{
    sto_info_t si;
    return (sto_info(&si) == 0) ? si.free_kb : 0;
}

/* session_count: number of `.sum` files under /sessions (one per session, §12.1). Name-only
 * listing (sto_list_next_name, no stat()) so this stays O(n) instead of sto_list_next's O(n^2)
 * on LittleFS (Plan 5.6 T1 fix 3); yields every 16 entries so a large /sessions can't starve
 * IDLE0/the task WDT on this task even so. Called once at logger start (the only full listing
 * this file runs -- see status_cache_prime()); every later refresh is incremental. */
static uint16_t session_count(void)
{
    int c = 0;
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") == 0) {
        char name[STO_NAME_MAX];
        int n = 0;
        while (sto_list_next_name(&it, name, sizeof name) == 1) {
            if (++n % 16 == 0) taskYIELD();
            const char *dot = strrchr(name, '.');
            if (dot && strcmp(dot, ".sum") == 0) c++;
        }
        sto_list_close(&it);
    }
    return (c > 0xFFFF) ? 0xFFFF : (uint16_t)c;
}

/* Bounded copy into a fixed on-wire header field (§12): copies at most cap-1 bytes and always
 * NUL-terminates. Truncation of an over-long value (e.g. a git-describe dev version longer than the
 * 16-byte fw field) is intentional and expressed without tripping -Werror=format-truncation. */
static void hdr_set(char *dst, size_t cap, const char *src)
{
    LT_ASSERT_VOID(dst != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(src != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(cap > 0, LOG_ASSERT_CODE);     /* cap - 1 below would underflow at cap == 0 */
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void log_path(char *out, size_t cap, const char *id, const char *ext)
{
    LT_ASSERT_VOID(out != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(id != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(ext != NULL, LOG_ASSERT_CODE);
    (void)snprintf(out, cap, "/sessions/%s%s", id, ext);
}

/* Write the accumulated batch to the open .log. */
static void do_write(void)
{
    if (!s_open || s_batch_len == 0) return;
    LT_ASSERT_VOID(s_batch_len <= BATCH_CAP, LOG_ASSERT_CODE);   /* buffer capacity before the write */
    LT_ASSERT_VOID(s_log_fd >= 0, LOG_ASSERT_CODE);              /* valid session state: s_open implies an open fd */
    if (sto_write(s_log_fd, s_batch, s_batch_len) != 0) (void)errlog_add(E_STO_WRITE, (uint32_t)s_batch_len);
    s_bytes_since_info += (uint32_t)s_batch_len;   /* Plan 5.6 T1 fix 3: status_cache_estimate()'s input */
    s_batch_len = 0;
    s_last_write_ms = now_ms();
}

/* Append one framed record to the batch, flushing first if it would not fit. */
static void batch_append(const uint8_t *frame, int n)
{
    if (n <= 0) return;
    LT_ASSERT_VOID(frame != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID((size_t)n <= FRAME_TMP_CAP, LOG_ASSERT_CODE);        /* record length within bounds */
    LT_ASSERT_VOID(s_batch_len <= BATCH_CAP, LOG_ASSERT_CODE);          /* precondition: batch already valid */
    if (s_batch_len + (size_t)n > BATCH_CAP) do_write();
    LT_ASSERT_VOID(s_batch_len + (size_t)n <= BATCH_CAP, LOG_ASSERT_CODE);   /* capacity before this write */
    memcpy(s_batch + s_batch_len, frame, (size_t)n);
    s_batch_len += (size_t)n;
}

static void acc_append(uint8_t *acc, size_t *len, size_t cap, const uint8_t *frame, int n)
{
    LT_ASSERT_VOID(acc != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(len != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(*len <= cap, LOG_ASSERT_CODE);   /* precondition: accumulator already within its cap */
    if (n <= 0 || *len + (size_t)n > cap) return;   /* beyond cap: kept in .log only (§12.5) */
    LT_ASSERT_VOID(frame != NULL, LOG_ASSERT_CODE);
    memcpy(acc + *len, frame, (size_t)n);
    *len += (size_t)n;
}

/* Stream one .sum section to the open tmp fd, in emission order (§12.5). Skips a zero-length
 * section (an empty accumulator, exactly as the old off+len memcpy did nothing); reports and
 * propagates an I/O error so the caller skips sync+rename, leaving an incomplete .sum.tmp with no
 * rename -- the same power-cut failure mode as the previous single sto_write (§13.1). */
static int sum_write(sto_file_t f, const uint8_t *p, size_t n)
{
    LT_ASSERT_RET(p != NULL, LOG_ASSERT_CODE, -1);
    LT_ASSERT_RET(n <= LAP_ACC_CAP, LOG_ASSERT_CODE, -1);   /* largest section is a full lap accumulator */
    if (n == 0) return 0;
    if (sto_write(f, p, n) != 0) { (void)errlog_add(E_STO_WRITE, (uint32_t)n); return -1; }
    return 0;
}

/* §12.5: rebuild .sum = HDR + VENUE + every LAP + every DRAG_RUN [+ END] via tmp+sync+rename.
 * A1: each HDR/VENUE/END frame is encoded into one small stack buffer and streamed to the open fd;
 * the accumulators stream straight from their own storage -- no 4 KB assemble-then-emit scratch.
 * The bytes are identical to the old single-buffer build: its fit-guards were always satisfied
 * (HDR 99 + VENUE 41 + LAP_ACC_CAP 3072 + DRAG_ACC_CAP 512 + END 14 = 3738 < the old 4096 cap), so
 * every section was, and still is, emitted in the same order. */
static void rebuild_sum(bool closing, int64_t end_gps_us, uint8_t end_reason)
{
    if (s_id[0] == 0) return;
    uint8_t frame[FRAME_TMP_CAP];
    int n = ses_encode_hdr(&s_hdr, frame, sizeof frame);
    if (n < 0) return;                                             /* HDR is mandatory (matches pre-A1) */
    LT_ASSERT_VOID((size_t)n <= sizeof frame, LOG_ASSERT_CODE);    /* encoded frame within the stack buffer */
    LT_ASSERT_VOID(s_lap_len <= LAP_ACC_CAP, LOG_ASSERT_CODE);     /* accumulator invariant (acc_append) */
    LT_ASSERT_VOID(s_drag_len <= DRAG_ACC_CAP, LOG_ASSERT_CODE);   /* accumulator invariant (acc_append) */

    char tmp_path[40], sum_path[40];
    log_path(tmp_path, sizeof tmp_path, s_id, ".sum.tmp");
    log_path(sum_path, sizeof sum_path, s_id, ".sum");
    sto_file_t f;
    if (sto_open(tmp_path, STO_WR | STO_CREATE, &f) != 0) { (void)errlog_add(E_STO_WRITE, 0); return; }

    int rc = sum_write(f, frame, (size_t)n);                       /* HDR */
    if (rc == 0) { n = ses_encode_venue(s_venue_id, s_layout_id, s_venue_name, frame, sizeof frame);
                   if (n > 0) rc = sum_write(f, frame, (size_t)n); }
    if (rc == 0) rc = sum_write(f, s_lap_acc, s_lap_len);
    if (rc == 0) rc = sum_write(f, s_drag_acc, s_drag_len);
    if (rc == 0 && closing) { n = ses_encode_end(end_gps_us, end_reason, frame, sizeof frame);
                              if (n > 0) rc = sum_write(f, frame, (size_t)n); }

    if (rc == 0) { rc = sto_sync(f); if (rc != 0) (void)errlog_add(E_STO_WRITE, 0); }
    (void)sto_close(f);
    if (rc == 0 && sto_rename(tmp_path, sum_path) != 0) (void)errlog_add(E_STO_WRITE, 0);   /* atomic (§13.1) */
}

static void eviction_check(void);   /* forward decl: called from open_session (§12.7 "at session start") and the main loop */

static void open_session(const log_request_t *req)
{
    LT_ASSERT_VOID(req != NULL, LOG_ASSERT_CODE);
    if (s_open) return;                            /* one open .log at a time */
    if (s_seq < 0xFF) s_seq++;
    (void)snprintf(s_id, sizeof s_id, "S%05u_%03u",
                   (unsigned)(lt_nvs_boot_get() & 0xFFFFu), (unsigned)s_seq);
    LT_ASSERT_VOID(s_id[0] != '\0', LOG_ASSERT_CODE);   /* valid session state before opening a file for it */

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
        (void)errlog_add(E_STO_WRITE, 0);
        s_id[0] = 0;
        return;
    }
    s_open = true;
    LT_ASSERT_VOID(s_log_fd >= 0, LOG_ASSERT_CODE);   /* valid session state: a successful sto_open yields fd >= 0 */
    s_last_write_ms = s_last_sync_ms = now_ms();

    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_hdr(&s_hdr, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();                                     /* flush HDR now: a cut right after open still yields a valid .log */
    (void)sto_sync(s_log_fd);                        /* and sync it: a cut right after open must not lose the .log HDR either */
    rebuild_sum(false, 0, 0);                        /* initial .sum: HDR + VENUE */
    eviction_check();                                /* §12.7: eviction runs at session start, not only every 60 s;
                                                        * Plan 5.6 T1 fix 3: also refreshes the status.h cache when
                                                        * it actually evicts. sessions is NOT bumped here -- only
                                                        * close_session's .sum counts a session (see status_cache_prime()
                                                        * for the only full recount, at boot). */
    ESP_LOGI(TAG, "session %s open", s_id);
}

static void close_session(const log_request_t *req)
{
    LT_ASSERT_VOID(req != NULL, LOG_ASSERT_CODE);
    if (!s_open) return;
    LT_ASSERT_VOID(s_id[0] != '\0', LOG_ASSERT_CODE);   /* valid session state: s_open implies a set id */
    LT_ASSERT_VOID(s_log_fd >= 0, LOG_ASSERT_CODE);     /* valid session state: s_open implies an open fd */
    do_write();
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_end(req->gps_us, req->reason, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();
    (void)sto_sync(s_log_fd);
    (void)sto_close(s_log_fd);
    s_open = false;
    rebuild_sum(true, req->gps_us, req->reason);     /* finalise .sum with END */
    /* Plan 5.6 T1 fix 3: incremental, not a rescan -- a .sum now exists for this session, and
     * closing is the one definitive "a session is now counted" boundary this file uses (see
     * status_cache_prime()'s doc comment for why open_session does not also bump this). */
    s_sessions_cached++;
    s_free_kb_cached = storage_free_kb();
    s_bytes_since_info = 0;
    status_cache_update(s_free_kb_cached, s_sessions_cached);
    ESP_LOGI(TAG, "session %s closed (reason %u)", s_id, (unsigned)req->reason);
}

static void handle_request(const log_request_t *req)
{
    LT_ASSERT_VOID(req != NULL, LOG_ASSERT_CODE);
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
    LT_ASSERT_VOID(ev != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(s_open, LOG_ASSERT_CODE);   /* valid session state: callers only forward events while open */
    LT_ASSERT_VOID(ev->type <= EV_FAULT, LOG_ASSERT_CODE);   /* the EV_* set is closed and stable (core/event.h) */
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
    LT_ASSERT_VOID(res != NULL, LOG_ASSERT_CODE);
    if (!s_open) return;                             /* nothing to write without an open .log */
    uint8_t tmp[FRAME_TMP_CAP];
    int n;

    if (res->kind == LOG_RES_LAP) {
        const lap_result_t *lap = &res->u.lap;
        LT_ASSERT_VOID(lap->n_sectors <= LAP_MAX_SECTORS + 1u, LOG_ASSERT_CODE);   /* within sector_ms[] */
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
        LT_ASSERT_VOID(run->n_gates <= DRAG_MAX_GATES, LOG_ASSERT_CODE);   /* within gates[] */
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
    LT_ASSERT_VOID(g_result_q != NULL, LOG_ASSERT_CODE);   /* valid state: created by lt_ipc_init() at boot */
    log_result_t res;
    uint32_t n = 0;
    while (xQueueReceive(g_result_q, &res, 0) == pdTRUE) {
        LT_ASSERT_VOID(n++ < RESULT_Q_DEPTH, LOG_ASSERT_CODE);   /* rule 2: drain bounded by queue depth */
        handle_result(&res);
    }
}

static void drain_rings(void)
{
    /* §17.5 safe mode: FIX/FUSED sample records are dropped (still popped off the ring so it
     * does not back up) while SESSION_HDR/VENUE/LAP/DRAG_RUN/END keep flowing (handle_result,
     * open_session, close_session) so summaries and the .sum still form. Same gate as the
     * SYS_STORAGE_FULL sample pause below. */
    bool suppress = s_samples_full || (sys_flags_get() & (1u << SYS_SAFE_MODE)) != 0;

    gps_fix_t fix;
    uint32_t fix_n = 0;
    while (ring_pop(&g_fix_ring, &fix)) {
        LT_ASSERT_VOID(fix_n++ < FIX_RING_CAP, LOG_ASSERT_CODE);   /* rule 2: ring drain bounded by ring depth */
        if (s_open && !suppress) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fix(&s_fix_st, &fix, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
        ses_fused_state_on_fix(&s_fused_st, fix.gps_us);   /* keep fused deltas referenced to fixes */
    }
    fused_sample_t fs;
    uint32_t fused_n = 0;
    while (ring_pop(&g_fused_ring, &fs)) {
        LT_ASSERT_VOID(fused_n++ < FUSED_RING_CAP, LOG_ASSERT_CODE);   /* rule 2: ring drain bounded by ring depth */
        if (s_open && !suppress) {
            uint8_t tmp[FRAME_TMP_CAP];
            int n = ses_encode_fused(&s_fused_st, &fs, tmp, sizeof tmp);
            batch_append(tmp, n);
        }
    }
}

static void drain_events(void)
{
    LT_ASSERT_VOID(g_evt_q != NULL, LOG_ASSERT_CODE);   /* valid state: created by lt_ipc_init() at boot */
    event_t ev;
    uint32_t n = 0;
    while (xQueueReceive(g_evt_q, &ev, 0) == pdTRUE) {
        LT_ASSERT_VOID(n++ < EVT_Q_DEPTH, LOG_ASSERT_CODE);   /* rule 2: drain bounded by queue depth */
        if (s_open) handle_event(&ev);
    }
}

typedef struct { char oldest[STO_NAME_MAX]; char curlog[STO_NAME_MAX]; } evict_ctx_t;

static void eviction_check(void)
{
    LT_ASSERT_VOID(!s_open || s_id[0] != '\0', LOG_ASSERT_CODE);   /* valid session state */
    sto_info_t si;
    if (sto_info(&si) != 0 || si.total_kb == 0) return;
    LT_ASSERT_VOID(si.free_kb <= si.total_kb, LOG_ASSERT_CODE);   /* HAL report sanity before the % math below */
    if (si.free_kb >= si.total_kb / 10u) {
        if (s_samples_full) { s_samples_full = false; sys_flags_clear(SYS_STORAGE_FULL); }
        return;
    }
    /* free < 10 %: delete the oldest .log that is not the current session. Per-entry stat() (via
     * sto_list_next, not the name-only sto_list_next_name) is kept -- eviction wants sizes/ages
     * available to it (a T12 latency-work candidate, §4). This loop stays O(n^2) on LittleFS; the
     * taskYIELD() every 16 entries (Plan 5.6 T1 fix 3) only feeds IDLE0/the task WDT through it,
     * it does not fix the complexity. */
    evict_ctx_t e;
    memset(&e, 0, sizeof e);
    if (s_id[0]) (void)snprintf(e.curlog, sizeof e.curlog, "%s.log", s_id);
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") == 0) {
        sto_entry_t ent;
        int n = 0;
        while (sto_list_next(&it, &ent) == 1) {
            if (++n % 16 == 0) taskYIELD();
            const char *name = ent.name;
            size_t len = strlen(name);
            if (len < 4 || strcmp(name + len - 4, ".log") != 0) continue;   /* only .log (never .sum) */
            if (strcmp(name, e.curlog) == 0) continue;                      /* never the current session */
            if (e.oldest[0] == 0 || strcmp(name, e.oldest) < 0)
                (void)snprintf(e.oldest, sizeof e.oldest, "%s", name);      /* smallest id == oldest (§12.7) */
        }
        sto_list_close(&it);
    }
    if (e.oldest[0]) {
        char p[STO_NAME_MAX + 12];   /* "/sessions/" (10) + a full oldest name + NUL */
        (void)snprintf(p, sizeof p, "/sessions/%s", e.oldest);
        (void)sto_unlink(p);
        (void)errlog_add(E_STO_EVICT, 0);
        ESP_LOGW(TAG, "evicted %s (free %u/%u KB)", e.oldest, (unsigned)si.free_kb, (unsigned)si.total_kb);
    } else if (si.free_kb < si.total_kb / 20u) {    /* nothing to delete and < 5 %: pause samples */
        if (!s_samples_full) { s_samples_full = true; sys_flags_set(SYS_STORAGE_FULL); (void)errlog_add(E_STO_FULL, 0); }
    }
    /* Plan 5.6 T1 fix 3: e.oldest, when set, is always a .log (never a .sum -- the filter above
     * only ever candidates .log names), so an eviction pass never changes the session count;
     * refresh the cheap free_kb reading (one sto_info(), not a re-listing) and re-baseline the
     * between-refresh byte estimate either way (an unlink, or the samples-paused branch, both
     * reached only because free space was already tight). */
    s_free_kb_cached = storage_free_kb();
    s_bytes_since_info = 0;
    status_cache_update(s_free_kb_cached, s_sessions_cached);
}

/* Drain the logger's control queue (open/close/rebuild/evict commands, §4.4). Pulled out of
 * logger_task's own loop (rather than inlined there, as it used to be) so its Rule 2 drain-bound
 * assertion can safely `return` out of a plain helper on trip, instead of out of the task body. */
static void drain_requests(void)
{
    LT_ASSERT_VOID(g_log_req_q != NULL, LOG_ASSERT_CODE);   /* valid state: created by lt_ipc_init() at boot */
    log_request_t req;
    uint32_t n = 0;
    while (xQueueReceive(g_log_req_q, &req, 0) == pdTRUE) {
        LT_ASSERT_VOID(n++ < LOG_REQ_Q_DEPTH, LOG_ASSERT_CODE);   /* rule 2: drain bounded by queue depth */
        handle_request(&req);
    }
}

/* Pulled out of logger_task's own body (a task entry that must never assert-return, see
 * sup_task's note) to keep that loop short and this ordinary helper free to assert normally in
 * the future. eviction_check() itself refreshes the status.h cache when it actually evicts
 * (Plan 5.6 T1 fix 3) -- no separate refresh needed here. */
static void evict_if_due(uint32_t now)
{
    if ((now - s_last_evict_ms) < EVICT_INTERVAL_MS) return;
    eviction_check();
    s_last_evict_ms = now_ms();
}

/* Plan 5.6 T1 fix 3: the only full session_count() scan this file ever runs (name-only, O(n),
 * yields every 16 entries -- see session_count()) is this ONE priming pass at logger start;
 * every later refresh is incremental (close_session/eviction_check) or a storage-free estimate
 * (status_cache_estimate). This is why open_session does NOT also call this: it neither closes a
 * session (no new .sum counted) nor is the storage owner's only chance to see one. */
static void status_cache_prime(void)
{
    s_sessions_cached = session_count();
    s_free_kb_cached = storage_free_kb();
    status_cache_update(s_free_kb_cached, s_sessions_cached);
}

/* Plan 5.6 T1 fix 3: cheap re-publish between real refreshes (open/close/evict/priming) --
 * estimates free_kb by subtracting .log bytes appended since the cache was last a real
 * storage_free_kb() reading (s_bytes_since_info, updated in do_write()) from the cached value,
 * clamped at 0. No sto_info()/listing call: pure arithmetic, safe to run every loop tick. */
static void status_cache_estimate(void)
{
    uint32_t used_kb = s_bytes_since_info >> 10;
    uint32_t free_kb = (s_free_kb_cached > used_kb) ? s_free_kb_cached - used_kb : 0;
    status_cache_update(free_kb, s_sessions_cached);
}

static void logger_task(void *arg)
{
    (void)arg;
    sup_register_task(HB_LOGGER, xTaskGetCurrentTaskHandle(), LOG_STALL_S);
    /* Prime the status.h cache HERE (logger task, storage already mounted by boot_storage()
     * before boot_subsystems()'s logger_start(), app_main.c) so a STATUS built before any
     * session ever opens or closes reports the real free_kb/sessions, not a cold 0/0 (Plan 5.6
     * T1 fix 2/3). Crammed onto one line to stay within the P10 rule-5 line budget for a task
     * entry, which must never itself LT_ASSERT_*-return (see sup_task's note). */
    s_last_evict_ms = now_ms(); status_cache_prime();
    ESP_LOGI(TAG, "logger up (core %d prio %d)", LOG_CORE, LOG_PRIO);

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOOP_TIMEOUT_MS));   /* ring-notify or 1000 ms */

        drain_requests();
        drain_rings();
        drain_events();
        drain_results();

        uint32_t now = now_ms();
        if (s_open && s_batch_len &&
            (s_batch_len >= BATCH_FLUSH_B || (now - s_last_write_ms) >= WRITE_INTERVAL_MS))
            do_write();
        if (s_open && (now - s_last_sync_ms) >= SYNC_INTERVAL_MS) {
            (void)sto_sync(s_log_fd);
            s_last_sync_ms = now;
        }
        if (s_sum_dirty && s_open) { rebuild_sum(false, 0, 0); s_sum_dirty = false; }
        evict_if_due(now); status_cache_estimate();   /* Plan 5.6 T1 fix 3: no periodic storage scan */

        g_hb[HB_LOGGER]++;
    }
}

void logger_start(void)
{
    if (s_task) return;
    s_task = xTaskCreateStaticPinnedToCore(logger_task, "logger", LOG_STACK_WORDS, NULL,
                                           LOG_PRIO, s_stack, &s_tcb, LOG_CORE);
    LT_ASSERT_VOID(s_task != NULL, LOG_ASSERT_CODE);   /* postcondition: static task creation must succeed */
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
    LT_ASSERT_VOID(r != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(r->kind <= LOG_RES_VENUE, LOG_ASSERT_CODE);   /* only 3 kinds ever set, all in this file */
    if (!g_result_q) return;
    if (xQueueSend(g_result_q, r, 0) == pdTRUE) logger_notify();
}

void logger_submit_lap(const lap_result_t *lap)
{
    LT_ASSERT_VOID(lap != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(lap->n_sectors <= LAP_MAX_SECTORS + 1u, LOG_ASSERT_CODE);   /* within sector_ms[] */
    log_result_t r;
    memset(&r, 0, sizeof r);
    r.kind = LOG_RES_LAP;
    r.u.lap = *lap;
    submit(&r);
}

void logger_submit_drag(const drag_result_t *run)
{
    LT_ASSERT_VOID(run != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(run->n_gates <= DRAG_MAX_GATES, LOG_ASSERT_CODE);   /* within gates[] */
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
