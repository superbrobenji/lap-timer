/* lt_nvs.c -- persistent lap-timer state on NVS (spec §15.2).
 *
 * A thin layer over the IDF nvs API. NVS storage primitives only; the policy that consumes
 * them (crash-loop -> safe mode, counter-flush cadence) lives in app_main / the supervisor.
 * Blob layouts are fixed on-flash formats: packed structs so their byte size matches §15.2.
 */
#include "app/lt_nvs.h"
#include "app/lt_consts.h"
#include "app/lt_err.h"
#include "app/lt_assert.h"

#include <string.h>

#include "core/ses.h"        /* ses_crc16 -- the core CRC-16/CCITT, reused for the cfg blob */
#include "esp_log.h"
#include "esp_system.h"      /* esp_reset_reason_t / ESP_RST_* */
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   /* error-ring serialisation (F1): static mutex, no ISR use */

static const char *TAG = "lt_nvs";

#define NVS_ASSERT_CODE 0x0B60

/* ---- on-flash blob layouts (§15.2). Packed so sizes are exact. ---- */
#define ERR_RING_LEN   32
#define CRASH_LOG_LEN  3
_Static_assert(CRASH_LOG_LEN == CRASH_LOOP_N, "crash-log length must match the §17.5 crash-loop count");

typedef struct __attribute__((packed)) {
    uint16_t code;
    uint32_t uptime_s;
    uint16_t boot;
    uint32_t arg;
} err_entry_t;                                   /* 12 B -> ring = 384 B (§15.2) */

typedef struct __attribute__((packed)) {
    err_entry_t entry[ERR_RING_LEN];
    uint8_t     head;                           /* next write slot */
} err_ring_t;                                    /* 385 B */

typedef struct __attribute__((packed)) {
    uint8_t  reset_reason;
    uint32_t uptime_s;
} crash_entry_t;                                 /* 5 B -> log = 15 B (§15.2) */

/* ---- namespaces / keys (§15.2) ---- */
#define NS_SYS  "lt_sys"
#define NS_ERR  "lt_err"
#define NS_CFG  "lt_cfg"
#define K_BOOT  "boot_cnt"
#define K_CRASH "crash_log"
#define K_SAFE  "safe_until"
#define K_RING  "ring"
#define K_CTR   "ctr"
#define K_CFG   "cfg"
#define K_STALL "stall_rst"                     /* §17.2/§17.5: supervisor stall-restart marker */
#define K_OTAPEND "ota_pend"                    /* §19.4: OTA-applied pending-validation flag */

#define COUNTER_FLUSH_US (60 * 1000000LL)       /* >=60 s batching (§15.2) */

/* H1: coalesce error-ring flash writes for a REPEATING (deduped) report. A distinct report still
 * persists immediately (crash/stall records stay reliable); a stuck hot-path assert reporting the
 * same (code,arg) every tick can refresh the ring in RAM but only writes flash once per this
 * interval, so a 100 Hz failing assert cannot storm flash or wear the 24 KB NVS partition out. */
#define ERRLOG_MIN_PERSIST_US (5 * 1000000LL)

/* ---- RAM mirrors ---- */
static nvs_handle_t   s_h_sys, s_h_err, s_h_cfg;
static bool           s_ready;
static uint32_t       s_boot_cnt;
static lt_counters_t  s_counters;
static bool           s_counters_dirty;
static int64_t        s_counters_last_us;
static err_ring_t     s_ring;
static crash_entry_t  s_crash[CRASH_LOG_LEN];

/* H1 dedup + rate-limit state (errlog_add). s_last_* is the most recently ADDED (code,arg): an
 * immediate repeat is folded onto the newest ring slot instead of overwriting all 32 with one
 * fault. s_ring_persist_us is the last time the ring was written to flash. */
static uint16_t       s_last_code;
static uint32_t       s_last_arg;
static bool           s_last_valid;
static int64_t        s_ring_persist_us;

/* F1: serialise the shared error-ring RAM mirror (s_ring) + its NVS write. errlog_add is called
 * from the logger/supervisor/console (core 0) AND the pipeline via the core assert hook (core 1),
 * so the read-modify-write of s_ring races without this. Static allocation (no malloc after init),
 * task-context only (the assert hook runs in task context, never an ISR). */
static SemaphoreHandle_t s_ring_lock;
static StaticSemaphore_t s_ring_lock_buf;

static inline void ring_lock(void)   { if (s_ring_lock) xSemaphoreTake(s_ring_lock, portMAX_DELAY); }
static inline void ring_unlock(void) { if (s_ring_lock) xSemaphoreGive(s_ring_lock); }

static uint32_t uptime_s_now(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

static int load_blob(nvs_handle_t h, const char *key, void *dst, size_t expect)
{
    /* Parameter validity: key/dst are the caller's own literals/RAM mirrors, never NULL, and
     * expect is always a genuine sizeof(...) > 0. Whether the STORED blob's size matches `expect`
     * is a separate, untrusted-on-flash-data question handled below by the plain `sz != expect`
     * return -- that one stays a return, not an assertion. */
    LT_ASSERT_RET(key != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(dst != NULL, NVS_ASSERT_CODE, -1);
    LT_ASSERT_RET(expect > 0, NVS_ASSERT_CODE, -1);
    size_t sz = 0;
    if (nvs_get_blob(h, key, NULL, &sz) != ESP_OK || sz != expect) return -1;
    return nvs_get_blob(h, key, dst, &sz) == ESP_OK ? 0 : -1;
}

int lt_nvs_init(void)
{
    if (s_ready) return 0;
    if (!s_ring_lock) s_ring_lock = xSemaphoreCreateMutexStatic(&s_ring_lock_buf);   /* F1 */
    /* Postcondition: mutex creation over our own static s_ring_lock_buf must succeed -- without
     * it, ring_lock()/ring_unlock() silently no-op (their own `if (s_ring_lock)` guard) and the
     * F1 serialisation the error ring depends on would be silently absent. */
    LT_ASSERT_RET(s_ring_lock != NULL, NVS_ASSERT_CODE, -1);
    if (nvs_open(NS_SYS, NVS_READWRITE, &s_h_sys) != ESP_OK) return -1;
    if (nvs_open(NS_ERR, NVS_READWRITE, &s_h_err) != ESP_OK) return -1;
    if (nvs_open(NS_CFG, NVS_READWRITE, &s_h_cfg) != ESP_OK) return -1;

    if (nvs_get_u32(s_h_sys, K_BOOT, &s_boot_cnt) != ESP_OK) s_boot_cnt = 0;
    if (load_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) != 0) memset(&s_counters, 0, sizeof(s_counters));
    if (load_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) != 0) memset(&s_ring, 0, sizeof(s_ring));
    /* H2: head comes straight from the NVS blob (load_blob only checks size). A same-size blob from
     * a different firmware/layout could carry head >= ERR_RING_LEN; clamp it here so errlog_add
     * never indexes past s_ring.entry[] (and never needs an assertion to catch it -- see errlog_add). */
    if (s_ring.head >= ERR_RING_LEN) s_ring.head = 0;
    if (load_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) != 0) memset(s_crash, 0, sizeof(s_crash));

    s_counters_last_us = esp_timer_get_time();
    s_ready = true;
    return 0;
}

uint32_t lt_nvs_boot_inc(void)
{
    s_boot_cnt++;
    if (nvs_set_u32(s_h_sys, K_BOOT, s_boot_cnt) == ESP_OK) (void)nvs_commit(s_h_sys);
    return s_boot_cnt;
}

uint32_t lt_nvs_boot_get(void) { return s_boot_cnt; }

static void persist_counters(void)
{
    if (nvs_set_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) == ESP_OK) (void)nvs_commit(s_h_err);
    s_counters_dirty = false;
    s_counters_last_us = esp_timer_get_time();
}

void lt_counters_inc(lt_counter_id_t id, bool persist)
{
    /* id is a raw pointer-cast index into s_counters (lt_counters_t is 9 contiguous u32 in enum
     * order, LT_CTR_BOOTS..LT_CTR_OTA_ROLLBACK) -- an out-of-range id would write past it. */
    LT_ASSERT_VOID(id <= LT_CTR_OTA_ROLLBACK, NVS_ASSERT_CODE);
    ((uint32_t *)&s_counters)[id]++;
    s_counters_dirty = true;
    if (persist) persist_counters();
}

int lt_counters_flush(bool force)
{
    if (!s_counters_dirty) return 0;
    if (force || esp_timer_get_time() - s_counters_last_us >= COUNTER_FLUSH_US) persist_counters();
    return 0;
}

const lt_counters_t *lt_counters(void) { return &s_counters; }

/* Write the RAM ring to NVS and stamp the last-write time. Caller holds ring_lock. The stamp lets
 * the H1 rate-limit cap how often a repeating (deduped) report is allowed to touch flash. */
static void errlog_persist(void)
{
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) == ESP_OK) (void)nvs_commit(s_h_err);
    s_ring_persist_us = esp_timer_get_time();
}

int errlog_add(uint16_t code, uint32_t arg)
{
    /* Assert-FREE (H2): errlog_add IS the assertion sink (core_assert_report -> here). An
     * LT_ASSERT here would re-enter core_assert_fail -> core_assert_report -> errlog_add and recurse
     * to a boot-time stack overflow, so both guards are plain, self-recovering checks: code 0 is the
     * empty-slot sentinel (§17.7 real codes >= 0x0101), and a corrupt head is clamped, never asserted.
     * H1: a stuck hot-path assert reports the same (code,arg) every tick -- dedup it onto the newest
     * slot and rate-limit the flash write so it cannot storm NVS; a distinct report persists at once
     * so genuine crash/stall records stay reliable. */
    if (code == 0) return -1;
    ring_lock();                                /* F1: RMW of s_ring + its NVS write is not atomic */
    if (s_ring.head >= ERR_RING_LEN) s_ring.head = 0;
    bool repeat = s_last_valid && code == s_last_code && arg == s_last_arg;
    if (repeat) {
        uint8_t newest = (uint8_t)((s_ring.head + ERR_RING_LEN - 1) % ERR_RING_LEN);
        s_ring.entry[newest].uptime_s = uptime_s_now();
        if (esp_timer_get_time() - s_ring_persist_us >= ERRLOG_MIN_PERSIST_US) errlog_persist();
        ring_unlock();
        return 0;
    }
    err_entry_t *e = &s_ring.entry[s_ring.head];
    e->code = code; e->uptime_s = uptime_s_now(); e->boot = (uint16_t)s_boot_cnt; e->arg = arg;
    s_ring.head = (uint8_t)((s_ring.head + 1) % ERR_RING_LEN);
    s_last_code = code; s_last_arg = arg; s_last_valid = true;
    errlog_persist();
    ring_unlock();
    ESP_LOGW(TAG, "errlog 0x%04x arg=%u", code, (unsigned)arg);
    return 0;
}

/* The STRONG core_assert_report (Power of 10 rule 9, spec §17.9): overrides core.c's weak no-op
 * default so a failing CORE_ASSERT_ or LT_ASSERT_ anywhere in the firmware -- which never aborts
 * on target -- is recorded into the §17.7 error ring. It lives HERE, next to errlog_add and in a
 * TU the firmware always links (errlog_add is referenced from boot), so the strong definition
 * reliably wins over the weak default: a strong def parked in a TU nothing else references would
 * not be pulled from the component archive, silently leaving the no-op default in the image.
 * It MUST NOT call a CORE_ASSERT_/LT_ASSERT_ macro itself (that would recurse back through
 * core_assert_fail); errlog_add's own asserts only fire on a zero code or a corrupt ring head,
 * neither of which a real assertion report produces. */
void core_assert_report(uint16_t code, const char *file, int line)
{
    ESP_LOGE(TAG, "core assert 0x%04x at %s:%d", code, file ? file : "?", line);
    (void)errlog_add(code, (uint32_t)line);
}

int lt_errlog_snapshot(lt_err_entry_t *out, int cap)
{
    /* Every real caller (cmd.c ERRLOG_GET/DIAG_GET) passes its own fixed local array and its
     * compile-time sizeof -- out==NULL or cap<=0 here would be a caller bug, not routine input. */
    LT_ASSERT_RET(out != NULL, NVS_ASSERT_CODE, 0);
    LT_ASSERT_RET(cap > 0, NVS_ASSERT_CODE, 0);
    /* head is the next write slot, so slot `head` is the oldest surviving entry once the ring has
     * wrapped; before wrap those slots are still zero. Walking head..head+LEN-1 (mod LEN) yields
     * oldest->newest; a zero `code` marks an untouched slot (real codes are >= 0x0101, §17.7). */
    ring_lock();                                /* F1: consistent copy vs. a concurrent errlog_add */
    int n = 0;
    for (int i = 0; i < ERR_RING_LEN && n < cap; i++) {
        const err_entry_t *e = &s_ring.entry[(s_ring.head + i) % ERR_RING_LEN];
        if (e->code == 0) continue;
        out[n].code     = e->code;
        out[n].arg      = e->arg;
        out[n].uptime_s = e->uptime_s;
        out[n].boot     = e->boot;
        n++;
    }
    ring_unlock();
    return n;
}

/* Same oldest->newest, empty-slots-skipped walk as lt_errlog_snapshot, exposed by count/index so
 * callers (cmd.c ERRLOG_GET/DIAG_GET) can stream entries into JSON without a second full-size
 * snapshot array. lt_errlog_at(i) returns byte-for-byte the entry lt_errlog_snapshot would place
 * at out[i] for the same ring state. */
int lt_errlog_count(void)
{
    ring_lock();
    int n = 0;
    for (int i = 0; i < ERR_RING_LEN; i++)
        if (s_ring.entry[(s_ring.head + i) % ERR_RING_LEN].code != 0) n++;
    ring_unlock();
    return n;
}

int lt_errlog_at(int index, lt_err_entry_t *out)
{
    LT_ASSERT_RET(out != NULL, NVS_ASSERT_CODE, -1);
    if (index < 0) return -1;                       /* out-of-range is routine input, not an anomaly */
    ring_lock();                                    /* F1: consistent single-entry view vs. errlog_add */
    int n = 0, rc = -1;
    for (int i = 0; i < ERR_RING_LEN; i++) {
        const err_entry_t *e = &s_ring.entry[(s_ring.head + i) % ERR_RING_LEN];
        if (e->code == 0) continue;                 /* untouched slot (real codes >= 0x0101, §17.7) */
        if (n == index) {
            out->code = e->code; out->arg = e->arg; out->uptime_s = e->uptime_s; out->boot = e->boot;
            rc = 0;
            break;
        }
        n++;
    }
    ring_unlock();
    return rc;
}

void lt_errlog_clear(void)
{
    ring_lock();                                /* F1: clear vs. a concurrent errlog_add */
    memset(&s_ring, 0, sizeof(s_ring));   /* head back to 0; layout unchanged, ring emptied */
    s_last_valid = false;                       /* H1: drop dedup state so a post-clear repeat re-appends */
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) == ESP_OK) (void)nvs_commit(s_h_err);
    s_ring_persist_us = esp_timer_get_time();
    ring_unlock();
}

void lt_crashlog_push(uint8_t reset_reason, uint32_t prev_uptime_s)
{
    s_crash[2] = s_crash[1];
    s_crash[1] = s_crash[0];
    s_crash[0].reset_reason = reset_reason;
    s_crash[0].uptime_s = prev_uptime_s;
    if (nvs_set_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) == ESP_OK) (void)nvs_commit(s_h_sys);
}

bool lt_reset_is_abnormal(int r)
{
    switch (r) {
    case ESP_RST_PANIC:
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
    case LT_RST_STALL:      /* F3: a supervisor-forced pipeline-stall restart counts as abnormal */
        return true;
    default:
        return false;
    }
}

bool lt_crashlog_is_loop(void)
{
    for (int i = 0; i < CRASH_LOG_LEN; i++) {
        if (!lt_reset_is_abnormal(s_crash[i].reset_reason)) return false;
        if (s_crash[i].uptime_s >= CRASH_LOOP_WINDOW_S) return false;   /* uptime < window each (§17.5) */
    }
    return true;
}

uint32_t lt_safe_until_get(void)
{
    uint32_t v = 0;
    (void)nvs_get_u32(s_h_sys, K_SAFE, &v);
    return v;
}

int lt_safe_until_set(uint32_t boot_cnt)
{
    if (nvs_set_u32(s_h_sys, K_SAFE, boot_cnt) != ESP_OK) return -1;
    (void)nvs_commit(s_h_sys);
    return 0;
}

void lt_safe_clear(void)
{
    /* §17.5 uptime-based auto-clear: zero the gate so `boot_cnt <= lt_safe_until_get()` is false
     * on every later boot (boot_cnt is >=1 from the first boot onward). */
    (void)lt_safe_until_set(0);
}

void lt_stall_flag_set(void)
{
    /* F3: the supervisor sets this immediately before esp_restart() on a wedged pipeline. Its HW
     * reset reason is ESP_RST_SW (§17.5 normal), so without this marker the crash-loop detector
     * would never escalate a chronically stalled pipeline to safe mode. Persist now -- it must
     * survive the restart it precedes. */
    if (nvs_set_u8(s_h_sys, K_STALL, 1) == ESP_OK) (void)nvs_commit(s_h_sys);
}

bool lt_stall_flag_take(void)
{
    uint8_t v = 0;
    if (nvs_get_u8(s_h_sys, K_STALL, &v) != ESP_OK || v == 0) return false;
    if (nvs_set_u8(s_h_sys, K_STALL, 0) == ESP_OK) (void)nvs_commit(s_h_sys);   /* consume once */
    return true;
}

int lt_ota_pending_set(void)
{
    /* §19.4: ota_end() persists this before esp_ota_set_boot_partition + reboot. Persist now -- it
     * must survive the reboot it precedes. */
    if (nvs_set_u8(s_h_sys, K_OTAPEND, 1) != ESP_OK) return -1;
    (void)nvs_commit(s_h_sys);
    return 0;
}

bool lt_ota_pending_get(void)
{
    uint8_t v = 0;
    return nvs_get_u8(s_h_sys, K_OTAPEND, &v) == ESP_OK && v != 0;
}

void lt_ota_pending_clear(void)
{
    /* §19.4: the supervisor clears the flag once it has marked the image valid or logged the
     * rollback, so a later boot is not treated as OTA-related. */
    if (nvs_set_u8(s_h_sys, K_OTAPEND, 0) == ESP_OK) (void)nvs_commit(s_h_sys);
}

const char *lt_reset_reason_str(int r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt-WDT";
    case ESP_RST_TASK_WDT:  return "task-WDT";
    case ESP_RST_WDT:       return "other-WDT";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "SDIO";
    case LT_RST_STALL:      return "pipeline-stall";
    default:                return "unknown";
    }
}

void lt_boot_record_reset(int reset_reason, uint32_t prev_uptime_s)
{
    /* §4.7 step 1 / §17.5: shift the crash log every boot; count + log abnormal resets on the
     * crash path (persist immediately -- these must survive the next reset). */
    lt_crashlog_push((uint8_t)reset_reason, prev_uptime_s);
    switch (reset_reason) {
    case ESP_RST_PANIC:
        lt_counters_inc(LT_CTR_CRASHES, true);
        (void)errlog_add(E_SYS_PANIC, 0);
        break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        lt_counters_inc(LT_CTR_WDT, true);
        (void)errlog_add(E_SYS_WDT_RESET, (uint32_t)reset_reason);
        break;
    case ESP_RST_BROWNOUT:
        lt_counters_inc(LT_CTR_BROWNOUT, true);
        (void)errlog_add(E_SYS_BROWNOUT, 0);
        break;
    default:
        break;
    }
}

/* ---- cfg blob (lt_cfg/cfg): packed cfg_t (leading version, §15.1) + trailing CRC16 (§15.2) ---- */
int lt_cfg_load(cfg_t *c)
{
    /* c is the caller's own cfg_t (ui.c/cmd.c/app_main.c each pass &local_var) -- always non-NULL
     * before we memcpy into it. The size/CRC/version checks below validate the STORED blob, which
     * is untrusted on-flash data the caller-visible cfg_t is not: those stay plain returns. */
    LT_ASSERT_RET(c != NULL, NVS_ASSERT_CODE, -1);
    uint8_t buf[sizeof(cfg_t) + 2];
    size_t sz = 0;
    if (nvs_get_blob(s_h_cfg, K_CFG, NULL, &sz) != ESP_OK || sz != sizeof(buf)) return -1;
    if (nvs_get_blob(s_h_cfg, K_CFG, buf, &sz) != ESP_OK) return -1;

    uint16_t want = ses_crc16(buf, sizeof(cfg_t));
    uint16_t got  = (uint16_t)(buf[sizeof(cfg_t)] | (buf[sizeof(cfg_t) + 1] << 8));
    if (want != got) return -1;
    if (buf[0] != CFG_VERSION) return -1;     /* leading version byte == cfg_t.version */

    memcpy(c, buf, sizeof(cfg_t));
    return cfg_validate(c);                   /* >=0 corrections; stored user settings win (§15.1) */
}

int lt_cfg_save(const cfg_t *c)
{
    /* c is the caller's own cfg_t -- memcpy'd from unconditionally below, so NULL would crash. */
    LT_ASSERT_RET(c != NULL, NVS_ASSERT_CODE, -1);
    uint8_t buf[sizeof(cfg_t) + 2];
    memcpy(buf, c, sizeof(cfg_t));
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFF);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);
    if (nvs_set_blob(s_h_cfg, K_CFG, buf, sizeof(buf)) != ESP_OK) return -1;
    (void)nvs_commit(s_h_cfg);
    return 0;
}
