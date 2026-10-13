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
#include "core/trk.h"         /* trk_user_save_venue/trk_user_load_venue/trk_user_id_at/trk_user_count (#97, §10.9) */
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
/* #97 (§10.9): the persisted user track table (tracks_save()/logger_load_tracks() below). Paths
 * are backend-relative (hal/storage.h); storage_internal.c's mount ladder already creates
 * /tracks. Review fix round 1 (T5-R4): the save writes TRACKS_USER_TMP_PATH then sto_rename()s it
 * over TRACKS_USER_PATH (hal/storage.h:49 documents the rename as atomic on LittleFS) so a power
 * cut mid-save leaves the OLD file intact instead of a half-written one. File format:
 * TRACKS_FILE_MAGIC + u8 version + u8 count, then count x length-prefixed, CRC16-trailed
 * trk_user_save_venue() records (core/trk.h) -- see tracks_save()/logger_load_tracks() below. */
#define TRACKS_USER_PATH     "/tracks/user.bin"
#define TRACKS_USER_TMP_PATH "/tracks/user.tmp"
#define TRACKS_FILE_MAGIC    "LTRK"
#define TRACKS_FILE_VERSION  1u
#define WRITE_INTERVAL_MS 1000
#define SYNC_INTERVAL_MS  2000
#define EVICT_INTERVAL_MS 60000
#define LOOP_TIMEOUT_MS   1000
#define FRAME_TMP_CAP    256           /* >= max framed record (247 payload + 5) */

/* §12.7 storage thresholds (Plan 7 T9 fix 3, ruling T9-R3). A bench reboot loop that opened and
 * wrote a new session every boot outran the old single-unlink-per-60s eviction: LittleFS reached
 * 0 free blocks, at which point even unlink() cannot commit its metadata ("lfs.c:704 No more
 * free space", "Unable to split") and the open/write path stalls in compaction past the task
 * WDT. STO_EVICT_PCT/STO_TARGET_PCT bound eviction_check()'s bounded multi-file pass; STO_RESERVE_PCT
 * is the hard floor open_session()'s reserve guard enforces so a session is never opened into it. */
#define STO_EVICT_PCT      10u         /* free/total below this: start evicting (existing behaviour) */
#define STO_TARGET_PCT     15u         /* evict (bounded) until free/total reaches at least this */
#define STO_RESERVE_PCT    5u          /* never open/write below this -- open_session()'s hard floor */
#define EVICT_MAX_PER_PASS 8u          /* rule 2: bound the unlinks any one eviction_check() call performs */

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
 * on LittleFS (Plan 5.6 T1 fix 3). Plan 5.6 T1 fix 4: vTaskDelay(1) every 16 entries blocks this
 * priority-8 task for one tick so IDLE0 (priority 0) runs and the task WDT is fed -- taskYIELD()
 * would not, it never schedules a lower-priority task. Called once at logger start -- the only
 * full listing this file ever runs, via status_cache_prime() (M7: delete_session used to also
 * call status_cache_prime() and so this, on every delete; I2 replaced that with an in-place
 * decrement, so this really is a one-time boot call now). Every later refresh (close_session,
 * eviction_check, delete_session) is incremental. */
static uint16_t session_count(void)
{
    int c = 0;
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") == 0) {
        char name[STO_NAME_MAX];
        int n = 0;
        while (sto_list_next_name(&it, name, sizeof name) == 1) {
            if (++n % 16 == 0) vTaskDelay(1);   /* one tick: lets IDLE0 run (see above) */
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
 * every section was, and still is, emitted in the same order.
 * Plan 5.6 T1 fix 4: returns true only when the .sum was fully written, synced and atomically
 * renamed into place; every failure path returns false (each one still recorded by errlog_add), so
 * a caller that counts sessions cannot count a .sum that never landed. */
static bool rebuild_sum(bool closing, int64_t end_gps_us, uint8_t end_reason)
{
    if (s_id[0] == 0) return false;
    uint8_t frame[FRAME_TMP_CAP];
    int n = ses_encode_hdr(&s_hdr, frame, sizeof frame);
    if (n < 0) return false;                                       /* HDR is mandatory (matches pre-A1) */
    /* Plan 5.6 T1 fix 4: LT_ASSERT_RET(..., false), not LT_ASSERT_VOID -- a reported, recovered
     * assertion leaves no .sum on disk, so it must report "did not land" like every other failure. */
    LT_ASSERT_RET((size_t)n <= sizeof frame, LOG_ASSERT_CODE, false);   /* encoded frame within the stack buffer */
    LT_ASSERT_RET(s_lap_len <= LAP_ACC_CAP, LOG_ASSERT_CODE, false);    /* accumulator invariant (acc_append) */
    LT_ASSERT_RET(s_drag_len <= DRAG_ACC_CAP, LOG_ASSERT_CODE, false);  /* accumulator invariant (acc_append) */

    char tmp_path[40], sum_path[40];
    log_path(tmp_path, sizeof tmp_path, s_id, ".sum.tmp");
    log_path(sum_path, sizeof sum_path, s_id, ".sum");
    sto_file_t f;
    if (sto_open(tmp_path, STO_WR | STO_CREATE, &f) != 0) { (void)errlog_add(E_STO_WRITE, 0); return false; }

    int rc = sum_write(f, frame, (size_t)n);                       /* HDR */
    if (rc == 0) { n = ses_encode_venue(s_venue_id, s_layout_id, s_venue_name, frame, sizeof frame);
                   if (n > 0) rc = sum_write(f, frame, (size_t)n); }
    if (rc == 0) rc = sum_write(f, s_lap_acc, s_lap_len);
    if (rc == 0) rc = sum_write(f, s_drag_acc, s_drag_len);
    if (rc == 0 && closing) { n = ses_encode_end(end_gps_us, end_reason, frame, sizeof frame);
                              if (n > 0) rc = sum_write(f, frame, (size_t)n); }

    if (rc == 0) { rc = sto_sync(f); if (rc != 0) (void)errlog_add(E_STO_WRITE, 0); }
    (void)sto_close(f);
    if (rc != 0) return false;
    if (sto_rename(tmp_path, sum_path) != 0) { (void)errlog_add(E_STO_WRITE, 0); return false; }   /* atomic (§13.1) */
    return true;
}

static void eviction_check(void);   /* forward decl: called from open_session (§12.7 "at session start") and the main loop */
static void status_cache_prime(void);   /* forward decl: called from logger_task at boot only -- I2 made
                                          * delete_session's cache update incremental, not a rescan */

static void open_session(const log_request_t *req)
{
    LT_ASSERT_VOID(req != NULL, LOG_ASSERT_CODE);
    if (s_open) return;                            /* one open .log at a time */

    /* Reserve guard (§12.7, Plan 7 T9 fix 3 / ruling T9-R3): before creating any file, refuse to
     * open below STO_RESERVE_PCT free. Try one eviction pass first; if that still leaves free
     * space below the reserve, do not open -- there is no file yet to leave half-created, and the
     * next open request (or the 60 s eviction pass) retries naturally once space recovers. */
    sto_info_t si;
    if (sto_info(&si) == 0 && si.total_kb != 0 &&
        si.free_kb < (si.total_kb * STO_RESERVE_PCT) / 100u) {
        eviction_check();
        if (sto_info(&si) == 0 && si.total_kb != 0 &&
            si.free_kb < (si.total_kb * STO_RESERVE_PCT) / 100u) {
            if (!s_samples_full) {
                s_samples_full = true;
                sys_flags_set(SYS_STORAGE_FULL);
                (void)errlog_add(E_STO_FULL, 0);
            }
            ESP_LOGE(TAG, "storage below reserve (%u/%u KB): session not opened",
                     (unsigned)si.free_kb, (unsigned)si.total_kb);
            return;
        }
    }

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
    /* latch and sys flag move together (Plan 7 T9 fix 3b): only reached once the reserve guard
     * above has already confirmed storage is genuinely above STO_RESERVE_PCT, so a latch that was
     * set stays consistent with SYS_STORAGE_FULL instead of going stale. */
    if (s_samples_full) { s_samples_full = false; sys_flags_clear(SYS_STORAGE_FULL); }

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
    /* initial .sum: HDR + VENUE. Failure already recorded by errlog_add inside; the .sum is
     * rebuilt again at the next LAP/close. */
    (void)rebuild_sum(false, 0, 0);
    eviction_check();                                /* §12.7: eviction runs at session start, not only every 60 s;
                                                        * Plan 5.6 T1 fix 3: also refreshes the status.h cache when
                                                        * it actually evicts. sessions is NOT bumped here -- only
                                                        * close_session's .sum counts a session (see status_cache_prime()
                                                        * for the only full recount, at boot). */
    ESP_LOGI(TAG, "session %s open", s_id);
}

/* Returns 0 when nothing was open (the routine "no-op" case) or once the close has completed;
 * -1 if req was NULL or this task's own open-session state was invalid (M1: an asserted anomaly
 * now reports failure to a synchronous caller, logger_request_sync, instead of a silent 0/ok). */
static int close_session(const log_request_t *req)
{
    LT_ASSERT_RET(req != NULL, LOG_ASSERT_CODE, -1);
    if (!s_open) return 0;
    LT_ASSERT_RET(s_id[0] != '\0', LOG_ASSERT_CODE, -1);   /* valid session state: s_open implies a set id */
    LT_ASSERT_RET(s_log_fd >= 0, LOG_ASSERT_CODE, -1);     /* valid session state: s_open implies an open fd */
    do_write();
    uint8_t tmp[FRAME_TMP_CAP];
    int n = ses_encode_end(req->gps_us, req->reason, tmp, sizeof tmp);
    batch_append(tmp, n);
    do_write();
    (void)sto_sync(s_log_fd);
    (void)sto_close(s_log_fd);
    s_open = false;
    /* Finalise .sum with END. Plan 5.6 T1 fix 3: the count is incremental, not a rescan -- closing
     * is the one definitive "a session is now counted" boundary this file uses (see
     * status_cache_prime()'s doc comment for why open_session does not also bump this).
     * Plan 5.6 T1 fix 4: and only a .sum that actually landed counts -- rebuild_sum returns false
     * on any write/sync/rename failure (recorded by errlog_add inside), leaving the count put. */
    if (rebuild_sum(true, req->gps_us, req->reason)) s_sessions_cached++;
    s_free_kb_cached = storage_free_kb();
    s_bytes_since_info = 0;
    status_cache_update(s_free_kb_cached, s_sessions_cached);
    ESP_LOGI(TAG, "session %s closed (reason %u)", s_id, (unsigned)req->reason);
    return 0;
}

/* T4 fix 2: sto_exists() (hal/storage.h) is the quiet existence probe -- unlike the T4 fix 1
 * version of this helper (sto_open+sto_close), it never ESP_LOGE's on a missing path, so a
 * `delete` of an already-gone id no longer prints anything (bench dsA-d1 item 2's "no E ( lines").
 * A probe result of 1 (exists) or <0 (stat() itself failed -- exists unknown) both count as "try
 * the unlink"; only an unambiguous 0 (definitely does not exist) counts as "skip it" -- see
 * delete_session's own comment for how that feeds the -3 decision and, for the <0 case, how the
 * eventual unlink's own rc becomes this request's rc. */
static void session_files_exist(const char *id, bool *has_log, bool *has_sum)
{
    LT_ASSERT_VOID(id != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(has_log != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(has_sum != NULL, LOG_ASSERT_CODE);
    char path[48];
    (void)snprintf(path, sizeof path, "/sessions/%s.log", id);
    *has_log = (sto_exists(path) != 0);   /* 1 exists, or <0 unknown -> attempt the unlink either way */
    (void)snprintf(path, sizeof path, "/sessions/%s.sum", id);
    *has_sum = (sto_exists(path) != 0);
}

/* DELETE_SESSION (#73): the only unlink path -- runs on this task (the storage owner), so the
 * eviction listing's iterator (evict_scan_oldest) is never crossed by a foreign mutation. Refuses
 * the currently open session (its fd would be orphaned and the in-flight session's data lost) --
 * the sender's own fast-path check (cmd.c's old logger_open_session_id() guard) is gone; this is
 * now the sole authority. 0 ok; -3 neither file existed (T4 fix 2: both sto_exists probes returned
 * a definite 0, checked BEFORE unlinking -- sto_unlink's own rc cannot report this on its own,
 * since it returns 0 on ENOENT too); -4 the session is open; else the first non-zero unlink rc
 * (an sto_exists probe that itself failed, <0, is treated as "try the unlink anyway" -- its rc,
 * not the probe's, is what this returns). */
static int delete_session(const log_request_t *req)
{
    LT_ASSERT_RET(req != NULL, LOG_ASSERT_CODE, -1);
    LT_ASSERT_RET(memchr(req->id, '\0', sizeof req->id) != NULL, LOG_ASSERT_CODE, -1);   /* id is NUL-terminated within its field */
    if (s_open && strcmp(s_id, req->id) == 0) return -4;   /* session is open */
    bool has_log, has_sum;
    session_files_exist(req->id, &has_log, &has_sum);
    if (!has_log && !has_sum) return -3;                    /* neither file existed */
    char path[48];
    int rc = 0;
    int rc_sum = 0;
    if (has_log) {
        (void)snprintf(path, sizeof path, "/sessions/%s.log", req->id);
        rc = sto_unlink(path);
    }
    if (has_sum) {
        (void)snprintf(path, sizeof path, "/sessions/%s.sum", req->id);
        rc_sum = sto_unlink(path);
        if (rc == 0) rc = rc_sum;
    }
    /* I2 (Ruling F-3): incremental update instead of a status_cache_prime() rescan -- a removed
     * .sum is the only thing that changes the session count (a .log-only delete never counted
     * toward it, see session_count()'s doc comment), so decrement in place and mirror
     * close_session's cache-publish tail rather than re-listing /sessions. Gated on rc_sum == 0
     * (final residual, re-review): has_sum alone only means "a .sum existed and an unlink was
     * attempted" -- a failed unlink leaves the file on disk but would still decrement the count,
     * wrong until the next full rescan (boot only, now that this path is incremental). has_sum
     * stays in the condition too, so a .sum that never existed (sto_unlink returns 0 on ENOENT)
     * does not decrement either. */
    if (has_sum && rc_sum == 0 && s_sessions_cached > 0) s_sessions_cached--;
    s_free_kb_cached = storage_free_kb();
    s_bytes_since_info = 0;
    status_cache_update(s_free_kb_cached, s_sessions_cached);
    return rc;
}

/* #97 (§10.9) blob-size proof (review fix round 1, T5-R4/M9): TRK_USER_REC_MAX (core/trk.h) is
 * trk_user_save_venue()'s worst-case single-record payload -- every slot full, TRK_MAX_LAYOUTS
 * layouts x LAP_MAX_SECTORS sectors each -- and this file's own per-record wrapper (u16 len +
 * u16 crc16 = 4 B) sits around it. The logger streams ONE record at a time through s_batch, never
 * the whole table at once, so this is the real invariant the 3840 B scratch batch must satisfy --
 * and, unlike the whole-table blob this replaces (2 + user_n*sizeof(trk_venue_t) + 2, which
 * overflowed BATCH_CAP at 2 venues already), it holds for every one of TRK_MAX_USER venues,
 * independent of how many are actually stored. */
_Static_assert(TRK_USER_REC_MAX + 4u <= BATCH_CAP,
               "the largest single user-track record (+ its length/crc wrapper) must fit the logger's scratch batch");

/* One record (length prefix + trk_user_save_venue()'s payload + crc16 trailer) for user-table
 * index i, written to f through s_batch. False on any write failure -- tracks_save() aborts the
 * whole save rather than leave a partial file (the caller never sto_rename()s a .tmp this
 * returned false on, so the OLD /tracks/user.bin is untouched by a failed save). */
static bool tracks_save_record(sto_file_t f, uint8_t i)
{
    LT_ASSERT_RET(sizeof s_batch == BATCH_CAP, LOG_ASSERT_CODE, false);   /* the scratch buffer this streams through */
    size_t n = 0;
    if (trk_user_save_venue(i, s_batch, sizeof s_batch, &n) != 0) return false;
    LT_ASSERT_RET(n <= TRK_USER_REC_MAX, LOG_ASSERT_CODE, false);   /* within the proven bound (_Static_assert above) */
    uint8_t  lenbuf[2] = { (uint8_t)n, (uint8_t)(n >> 8) };
    uint16_t crc       = ses_crc16(s_batch, n);
    uint8_t  crcbuf[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };
    return sto_write(f, lenbuf, sizeof lenbuf) == 0 &&
           sto_write(f, s_batch, n) == 0 &&
           sto_write(f, crcbuf, sizeof crcbuf) == 0;
}

/* #97 (§10.9), review fix round 1 (T5-R4): serialise the whole user track table into
 * /tracks/user.tmp (one record per venue, tracks_save_record() above, streamed through s_batch --
 * no new buffer), then sto_rename() it over /tracks/user.bin only once every record landed
 * without error (atomic on LittleFS, hal/storage.h:49) -- a power cut mid-save leaves the OLD file
 * intact. Any pending session-log batch is flushed first (do_write()) so this never clobbers
 * unwritten .log bytes; s_batch_len stays logically empty afterward, same as right after a flush
 * (tracks_save_record() never touches it). M2: every failure path below is errlog_add()'d --
 * with a fire-and-forget requester (the pipeline's own call, create_finish_if_active()) the rc
 * itself goes nowhere, so this is the only on-device trace a save failed.
 *
 * Final review M-2: a failed save used to leave /tracks/user.tmp behind -- harmless
 * (STO_WR|STO_CREATE self-heals it on the next save) but it sits on a filesystem with a hard 5%
 * reserve (STO_RESERVE_PCT) until then, so every failure return below now unlinks it first.
 *
 * Final review F-5/I-5: the sim capture's own venue (TRK_SIM_VENUE_ID, reserved -- core/trk.h) is
 * never written here -- it is re-registered fresh from gps_sim_venue_json() every sim boot
 * (pipeline_init()), so persisting it would be redundant at best and, the bug this closes, at
 * worst silently overwrite a real-build venue that happens to land in the same table slot across
 * a sim/real image swap. write_count (and so the header's own count byte) reflects only what is
 * actually written, computed via trk_user_id_at() before the header is written, since the header
 * precedes the records it describes. */
static int tracks_save(void)
{
    if (s_batch_len != 0) {
        LT_ASSERT_RET(s_open, LOG_ASSERT_CODE, -1);   /* invariant: a non-empty batch implies an open session */
        do_write();
    }
    LT_ASSERT_RET(s_batch_len == 0, LOG_ASSERT_CODE, -1);   /* postcondition: s_batch is free to reuse */

    int count = trk_user_count();
    LT_ASSERT_RET(count >= 0 && count <= TRK_MAX_USER, LOG_ASSERT_CODE, -1);
    uint8_t write_count = 0;
    for (int i = 0; i < count; i++) if (trk_user_id_at((uint8_t)i) != TRK_SIM_VENUE_ID) write_count++;

    sto_file_t f;
    if (sto_open(TRACKS_USER_TMP_PATH, STO_WR | STO_CREATE, &f) != 0) {
        (void)errlog_add(E_STO_WRITE, 0);
        return -1;
    }
    uint8_t hdr[6];
    memcpy(hdr, TRACKS_FILE_MAGIC, 4);
    hdr[4] = (uint8_t)TRACKS_FILE_VERSION;
    hdr[5] = write_count;
    bool ok = sto_write(f, hdr, sizeof hdr) == 0;
    for (int i = 0; ok && i < count; i++) {
        if (trk_user_id_at((uint8_t)i) == TRK_SIM_VENUE_ID) continue;   /* F-5: never persisted */
        ok = tracks_save_record(f, (uint8_t)i);
    }
    (void)sto_close(f);
    if (!ok) {
        (void)errlog_add(E_STO_WRITE, 0);
        (void)sto_unlink(TRACKS_USER_TMP_PATH);   /* M-2: do not leave a half-written tmp behind */
        return -1;
    }
    if (sto_rename(TRACKS_USER_TMP_PATH, TRACKS_USER_PATH) != 0) {
        (void)errlog_add(E_STO_WRITE, 0);
        (void)sto_unlink(TRACKS_USER_TMP_PATH);   /* M-2: rename failed -- the tmp is still ours to clean up */
        return -1;
    }
    return 0;
}

/* Reads one length-prefixed, CRC16-trailed record from f into s_batch and loads it. False on a
 * short/truncated read (the rest of the file, if any, is unreachable without it -- the caller
 * stops the loop there). A CRC mismatch is NOT a false here: it is reported true (keep reading)
 * with just that record skipped, so one corrupt record never costs every record after it (review
 * fix round 1, T5-R4 refinement 3) -- a sharp contrast with a whole-table load (the earlier
 * trk_user_load(), deleted in the final review fix wave, M-3, since #97 made it dead code) which
 * would wipe the entire table on any single defect. trk_user_load_venue() itself additionally
 * refuses (final review F-5/I-5) a record carrying the sim's reserved id -- never installed even
 * if one somehow made it into a committed file. */
static bool tracks_load_record(sto_file_t f)
{
    LT_ASSERT_RET(sizeof s_batch == BATCH_CAP, LOG_ASSERT_CODE, false);   /* the scratch buffer this streams through */
    uint8_t lenbuf[2];
    size_t  got = 0;
    if (sto_read(f, lenbuf, sizeof lenbuf, &got) != 0 || got != sizeof lenbuf) return false;
    uint16_t len = (uint16_t)(lenbuf[0] | ((uint16_t)lenbuf[1] << 8));
    if (len == 0 || len > TRK_USER_REC_MAX || len > sizeof s_batch) return false;
    if (sto_read(f, s_batch, len, &got) != 0 || got != len) return false;
    uint8_t crcbuf[2];
    if (sto_read(f, crcbuf, sizeof crcbuf, &got) != 0 || got != sizeof crcbuf) return false;
    uint16_t want = (uint16_t)(crcbuf[0] | ((uint16_t)crcbuf[1] << 8));
    if (ses_crc16(s_batch, len) == want) (void)trk_user_load_venue(s_batch, len);
    return true;
}

/* #97 (§10.9), review fix round 1 (T5-R2) + final review (F-4/I-4): load /tracks/user.bin into
 * the user track table. Declared in app/logger.h and called ONCE, directly (no request/reply, no
 * queue), from main/app_main.c's boot_subsystems() -- right after trk_init() and strictly BEFORE
 * logger_start() creates the logger task a few lines later -- see core/trk.h's ownership comment
 * for why that placement needs no concurrency premise at all (previously this ran inside
 * logger_task()'s own boot init instead, on a safety argument that rested on two unstated
 * premises -- same-core task affinity, and this function never yielding -- the final review
 * moved it here rather than shoring up that proof). Stays in THIS file (not app_main.c) because
 * it only reads the format logger.c owns writing (tracks_save() above); it is simply no longer
 * called from the task that owns writing it. s_batch is asserted empty (nothing has run yet at
 * this point in boot, not just assumed). A missing file (sto_exists() != 1: absent, or the probe
 * itself failed) or a header that fails its magic/version check is not treated as a crash -- a
 * fresh device, or a foreign/corrupt file, just loads nothing, same as before #97. */
int logger_load_tracks(void)
{
    LT_ASSERT_RET(s_batch_len == 0, LOG_ASSERT_CODE, -1);   /* boot: nothing buffered yet */
    if (sto_exists(TRACKS_USER_PATH) != 1) return 0;        /* no file: nothing to load */
    sto_file_t f;
    if (sto_open(TRACKS_USER_PATH, STO_RD, &f) != 0) return -1;
    LT_ASSERT_RET(f >= 0, LOG_ASSERT_CODE, -1);   /* valid state: a successful sto_open yields fd >= 0 */
    uint8_t hdr[6];
    size_t  got = 0;
    bool ok = sto_read(f, hdr, sizeof hdr, &got) == 0 && got == sizeof hdr &&
              memcmp(hdr, TRACKS_FILE_MAGIC, 4) == 0 && hdr[4] == (uint8_t)TRACKS_FILE_VERSION;
    if (ok) {
        uint8_t count = hdr[5];
        for (uint8_t i = 0; i < count && i < TRK_MAX_USER; i++) {
            if (!tracks_load_record(f)) break;
        }
    }
    (void)sto_close(f);
    return ok ? 0 : -1;
}

/* Returns the rc this request's handling produced -- 0 ok, <0 an error -- so drain_requests can
 * notify a synchronous caller (logger_request_sync, debt sweep A #59/#73). OPEN/REBUILD/EVICT
 * have no failure surface of their own yet (their own I/O failures are already recorded by
 * errlog_add inside), so they always return 0. DELETE_SESSION returns delete_session()'s rc. */
static int handle_request(const log_request_t *req)
{
    LT_ASSERT_RET(req != NULL, LOG_ASSERT_CODE, -1);   /* M1: an asserted anomaly reports failure, not 0/ok */
    switch (req->type) {
    case LOGGER_OPEN_SESSION:    open_session(req); return 0;
    case LOGGER_CLOSE_SESSION:   return close_session(req);
    /* failure already recorded by errlog_add inside; the .sum is rebuilt again at the next LAP/close */
    case LOGGER_REBUILD_SUMMARY: if (s_open) (void)rebuild_sum(false, 0, 0); return 0;
    case LOGGER_EVICT:           s_last_evict_ms = now_ms() - EVICT_INTERVAL_MS; return 0;   /* force an eviction pass this loop */
    case LOGGER_DELETE_SESSION:  return delete_session(req);
    case LOGGER_SAVE_TRACKS:     return tracks_save();
    /* LOGGER_LOAD_TRACKS retired (review fix round 1, T5-R2) -- logger_load_tracks() is called
     * once from main/app_main.c's boot sequence (final review F-4/I-4), before this task even
     * exists, never via a request. */
    default: return 0;
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

/* Plan 5.6 final-review A I2: publish a free_kb reading the caller already has in hand --
 * s_free_kb_cached, s_bytes_since_info (re-baselined so status_cache_estimate() starts fresh
 * from here) and the status.h cache itself. Shared by eviction_check's early-return path
 * (plentiful space: si.free_kb is already a real reading, zero extra I/O to publish it) and its
 * tight-storage refresh (a fresh storage_free_kb() reading) so free_kb stops going stale while
 * space stays plentiful, without duplicating the three-line publish or growing this file's
 * statics. */
static void cache_publish_free(uint32_t free_kb)
{
    s_free_kb_cached = free_kb;
    s_bytes_since_info = 0;
    status_cache_update(s_free_kb_cached, s_sessions_cached);
}

/* Name-only scan (Plan 5.6 T1 fix 3 / Plan 7 T9 fix 2: sto_list_next_name, no per-entry stat(),
 * O(n) instead of the O(n^2) sto_list_next walk) for the oldest .log under /sessions that is not
 * the current open session (the oldest session is the smallest .log name, §12.7). Fills e->oldest
 * and returns true when a candidate exists; returns false (e->oldest left empty) when there is
 * nothing left to delete. The vTaskDelay(1) every 16 entries (Plan 5.6 T1 fix 4) is what lets
 * IDLE0 (priority 0) -- and the ui task -- actually get scheduled ahead of this priority-8 task;
 * taskYIELD() would not, it never schedules a lower-priority task. */
static bool evict_scan_oldest(evict_ctx_t *e)
{
    LT_ASSERT_RET(e != NULL, LOG_ASSERT_CODE, false);
    memset(e, 0, sizeof *e);
    if (s_id[0]) (void)snprintf(e->curlog, sizeof e->curlog, "%s.log", s_id);
    sto_iter_t it;
    if (sto_list_open(&it, "/sessions") == 0) {
        char name[STO_NAME_MAX];
        int n = 0;
        while (sto_list_next_name(&it, name, sizeof name) == 1) {
            if (++n % 16 == 0) vTaskDelay(1);   /* one tick: lets IDLE0 run (see above) */
            size_t len = strlen(name);
            if (len < 4 || strcmp(name + len - 4, ".log") != 0) continue;   /* only .log (never .sum) */
            if (strcmp(name, e->curlog) == 0) continue;                     /* never the current session */
            if (e->oldest[0] == 0 || strcmp(name, e->oldest) < 0)
                (void)snprintf(e->oldest, sizeof e->oldest, "%s", name);    /* smallest id == oldest (§12.7) */
        }
        sto_list_close(&it);
    }
    LT_ASSERT_RET(strlen(e->oldest) < STO_NAME_MAX, LOG_ASSERT_CODE, false);   /* postcondition: name copy in bounds */
    return e->oldest[0] != 0;
}

/* Unlink one evicted .log (the .sum is kept, as today) and record it: one E_STO_EVICT errlog
 * entry plus one ESP_LOGW line per file. Fix round 2 (minor #13): `si` (the caller's loop-scoped
 * reading) is from BEFORE this unlink ran, so the line now re-reads sto_info() after the unlink
 * to report the true post-eviction free/total figure, not the stale pre-unlink one -- best-effort
 * (falls back to the pre-unlink `*si` reading on failure; the caller's own loop re-reads sto_info()
 * again anyway for its loop condition, so this extra read costs nothing beyond a clearer log line). */
static void evict_one(const char *name, const sto_info_t *si)
{
    LT_ASSERT_VOID(name != NULL, LOG_ASSERT_CODE);
    LT_ASSERT_VOID(si != NULL, LOG_ASSERT_CODE);
    char p[STO_NAME_MAX + 12];   /* "/sessions/" (10) + a full oldest name + NUL */
    (void)snprintf(p, sizeof p, "/sessions/%s", name);
    (void)sto_unlink(p);
    (void)errlog_add(E_STO_EVICT, 0);
    sto_info_t after = *si;
    (void)sto_info(&after);
    ESP_LOGW(TAG, "evicted %s (free %u/%u KB)", name, (unsigned)after.free_kb, (unsigned)after.total_kb);
}

/* Invariant: the filesystem must never reach 0 free blocks -- LittleFS cannot even unlink then
 * ("lfs.c:704 No more free space", "Unable to split"); this bounded multi-file pass (run every
 * 60 s and at session start) plus open_session()'s reserve guard are what keep the bench's
 * reboot-loop deadlock (Plan 7 T9 fix 3, ruling T9-R3) from recurring. */
static void eviction_check(void)
{
    LT_ASSERT_VOID(!s_open || s_id[0] != '\0', LOG_ASSERT_CODE);   /* valid session state */
    sto_info_t si;
    if (sto_info(&si) != 0 || si.total_kb == 0) return;
    LT_ASSERT_VOID(si.free_kb <= si.total_kb, LOG_ASSERT_CODE);   /* HAL report sanity before the % math below */
    if (si.free_kb >= (si.total_kb * STO_EVICT_PCT) / 100u) {
        if (s_samples_full) { s_samples_full = false; sys_flags_clear(SYS_STORAGE_FULL); }
        /* Plan 5.6 final-review A I2: the common path -- publish the si.free_kb already in hand
         * instead of returning before the cache is ever refreshed while space stays plentiful
         * (only prime/close/tight-eviction used to touch it). Zero extra I/O. */
        cache_publish_free(si.free_kb);
        return;
    }

    /* free < STO_EVICT_PCT: evict oldest-first in a bounded multi-file pass, until free reaches
     * STO_TARGET_PCT or EVICT_MAX_PER_PASS unlinks have run (rule 2: bounded loop) -- a single
     * unlink per 60 s pass could not keep up with a bench reboot loop that opened and wrote a
     * new session every boot (Plan 7 T9 fix 3). Each iteration re-scans (evict_scan_oldest, still
     * O(n)/name-only) and re-reads sto_info(); the vTaskDelay(1) between passes, plus the one
     * inside the scan, both still let IDLE0 -- and the ui task -- run ahead of this task. */
    uint32_t pass = 0;
    bool nothing_left = false;
    while (si.free_kb < (si.total_kb * STO_TARGET_PCT) / 100u && pass < EVICT_MAX_PER_PASS) {
        evict_ctx_t e;
        if (!evict_scan_oldest(&e)) { nothing_left = true; break; }
        evict_one(e.oldest, &si);
        pass++;
        if (sto_info(&si) != 0 || si.total_kb == 0) break;   /* re-read for the loop condition */
        vTaskDelay(1);                                        /* one tick between passes */
    }
    LT_ASSERT_VOID(pass <= EVICT_MAX_PER_PASS, LOG_ASSERT_CODE);   /* rule 2: bounded loop */

    if (nothing_left && si.free_kb < (si.total_kb * STO_RESERVE_PCT) / 100u) {
        /* nothing left to delete and still below the reserve: pause samples */
        if (!s_samples_full) { s_samples_full = true; sys_flags_set(SYS_STORAGE_FULL); (void)errlog_add(E_STO_FULL, 0); }
    }
    /* Plan 5.6 T1 fix 3: an evicted name is always a .log (never a .sum -- evict_scan_oldest's
     * filter only ever candidates .log names), so this pass never changes the session count;
     * refresh the cheap free_kb reading (one sto_info(), not a re-listing) and re-baseline the
     * between-refresh byte estimate either way. */
    cache_publish_free(storage_free_kb());
}

/* T3 fix 1 (ruling P-7) + M2: pack rc into the notification the same way on every path -- req.seq
 * into the top byte, rc into the low 24 bits -- so a waiter (logger_request_sync) can tell this
 * reply apart from a stale one of its own. A NULL requester (no synchronous caller) is a no-op. */
static void notify_requester(const log_request_t *req, int rc)
{
    if (req->requester == NULL) return;
    uint32_t val = ((uint32_t)req->seq << 24) | ((uint32_t)rc & 0x00FFFFFFu);
    (void)xTaskNotify(req->requester, val, eSetValueWithOverwrite);
}

/* Drain the logger's control queue (open/close/rebuild/evict/delete commands, §4.4). Pulled out
 * of logger_task's own loop (rather than inlined there, as it used to be) so its Rule 2 drain-
 * bound trip can safely `return` out of a plain helper, instead of out of the task body.
 * debt sweep A #59/#73: a non-NULL requester (only logger_request_sync sets one) gets notified
 * with this request's rc once handle_request() has finished it -- the logger task never waits on
 * anything the requester holds, so this notify can never deadlock. */
static void drain_requests(void)
{
    LT_ASSERT_VOID(g_log_req_q != NULL, LOG_ASSERT_CODE);   /* valid state: created by lt_ipc_init() at boot */
    log_request_t req;
    uint32_t n = 0;
    while (xQueueReceive(g_log_req_q, &req, 0) == pdTRUE) {
        /* M2: doubled from LOG_REQ_Q_DEPTH -- a handler can yield mid-drain (e.g. storage I/O
         * inside delete_session/eviction_check), letting producers refill the queue within this
         * same call; still a bounded loop (rule 2), just against a less pessimistic worst case. */
        if (n++ >= 2 * LOG_REQ_Q_DEPTH) {
            core_assert_fail(LOG_ASSERT_CODE, __FILE__, __LINE__);
            /* M2: the trip must not silently swallow this request's reply -- notify its requester
             * (if any) with rc -1 so a synchronous caller times out cleanly instead of waiting out
             * its full timeout for a reply that was never coming. */
            notify_requester(&req, -1);
            return;
        }
        int rc = handle_request(&req);
        notify_requester(&req, rc);
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
 * every later refresh is incremental (close_session/eviction_check/delete_session) or a
 * storage-free estimate (status_cache_estimate). This is why open_session does NOT also call
 * this: it neither closes a session (no new .sum counted) nor is the storage owner's only chance
 * to see one. Resets s_bytes_since_info too: the only caller (logger_task, at boot) just took a
 * real storage_free_kb() reading, so status_cache_estimate() must restart its between-refresh
 * estimate from here, not from bytes appended before this priming pass.
 *
 * M7 (final review): this used to also be delete_session's (LOGGER_DELETE_SESSION, debt sweep A
 * #73) only way to see a session count that just went DOWN (close_session only ever increments
 * it) -- I2 replaced that full rescan with an in-place decrement (delete_session, above), since a
 * removed .sum is the one thing this file already knows changed the count by exactly one. */
static void status_cache_prime(void)
{
    s_sessions_cached = session_count();
    s_free_kb_cached = storage_free_kb();
    s_bytes_since_info = 0;
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
    (void)arg; sup_register_task(HB_LOGGER, xTaskGetCurrentTaskHandle(), LOG_STALL_S);
    /* #97 (§10.9): the user track table is no longer loaded here (final review F-4/I-4) --
     * logger_load_tracks() now runs on app_main's own task, before this task is even created (see
     * that function's doc comment and core/trk.h's ownership comment for why). */
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
        /* failure already recorded by errlog_add inside; the .sum is rebuilt again at the next LAP/close */
        if (s_sum_dirty && s_open) { (void)rebuild_sum(false, 0, 0); s_sum_dirty = false; }
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
