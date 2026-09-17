/* lt_nvs.c -- persistent lap-timer state on NVS (spec §15.2).
 *
 * A thin layer over the IDF nvs API. NVS storage primitives only; the policy that consumes
 * them (crash-loop -> safe mode, counter-flush cadence) lives in app_main / the supervisor.
 * Blob layouts are fixed on-flash formats: packed structs so their byte size matches §15.2.
 */
#include "app/lt_nvs.h"
#include "app/lt_consts.h"
#include "app/lt_err.h"

#include <string.h>

#include "core/ses.h"        /* ses_crc16 -- the core CRC-16/CCITT, reused for the cfg blob */
#include "esp_log.h"
#include "esp_system.h"      /* esp_reset_reason_t / ESP_RST_* */
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "lt_nvs";

/* ---- on-flash blob layouts (§15.2). Packed so sizes are exact. ---- */
#define ERR_RING_LEN   32
#define CRASH_LOG_LEN  3
_Static_assert(CRASH_LOG_LEN == CRASH_LOOP_N, "crash-log length must match the §17.5 crash-loop count");

typedef struct __attribute__((packed)) {
    uint16_t code;
    uint32_t uptime_s;
    uint16_t boot;
    uint32_t arg;
} err_entry_t;                                  /* 12 B -> ring = 384 B (§15.2) */

typedef struct __attribute__((packed)) {
    err_entry_t entry[ERR_RING_LEN];
    uint8_t     head;                           /* next write slot */
} err_ring_t;                                   /* 385 B */

typedef struct __attribute__((packed)) {
    uint8_t  reset_reason;
    uint32_t uptime_s;
} crash_entry_t;                                /* 5 B -> log = 15 B (§15.2) */

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

#define COUNTER_FLUSH_US (60 * 1000000LL)       /* >=60 s batching (§15.2) */

/* ---- RAM mirrors ---- */
static nvs_handle_t   s_h_sys, s_h_err, s_h_cfg;
static bool           s_ready;
static uint32_t       s_boot_cnt;
static lt_counters_t  s_counters;
static bool           s_counters_dirty;
static int64_t        s_counters_last_us;
static err_ring_t     s_ring;
static crash_entry_t  s_crash[CRASH_LOG_LEN];

static uint32_t uptime_s_now(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }

static int load_blob(nvs_handle_t h, const char *key, void *dst, size_t expect)
{
    size_t sz = 0;
    if (nvs_get_blob(h, key, NULL, &sz) != ESP_OK || sz != expect) return -1;
    return nvs_get_blob(h, key, dst, &sz) == ESP_OK ? 0 : -1;
}

int lt_nvs_init(void)
{
    if (s_ready) return 0;
    if (nvs_open(NS_SYS, NVS_READWRITE, &s_h_sys) != ESP_OK) return -1;
    if (nvs_open(NS_ERR, NVS_READWRITE, &s_h_err) != ESP_OK) return -1;
    if (nvs_open(NS_CFG, NVS_READWRITE, &s_h_cfg) != ESP_OK) return -1;

    if (nvs_get_u32(s_h_sys, K_BOOT, &s_boot_cnt) != ESP_OK) s_boot_cnt = 0;
    if (load_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) != 0) memset(&s_counters, 0, sizeof(s_counters));
    if (load_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) != 0) memset(&s_ring, 0, sizeof(s_ring));
    if (load_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) != 0) memset(s_crash, 0, sizeof(s_crash));

    s_counters_last_us = esp_timer_get_time();
    s_ready = true;
    return 0;
}

uint32_t lt_nvs_boot_inc(void)
{
    s_boot_cnt++;
    if (nvs_set_u32(s_h_sys, K_BOOT, s_boot_cnt) == ESP_OK) nvs_commit(s_h_sys);
    return s_boot_cnt;
}

uint32_t lt_nvs_boot_get(void) { return s_boot_cnt; }

static void persist_counters(void)
{
    if (nvs_set_blob(s_h_err, K_CTR, &s_counters, sizeof(s_counters)) == ESP_OK) nvs_commit(s_h_err);
    s_counters_dirty = false;
    s_counters_last_us = esp_timer_get_time();
}

void lt_counters_inc(lt_counter_id_t id, bool persist)
{
    ((uint32_t *)&s_counters)[id]++;   /* lt_counters_t is 9 contiguous u32 in enum order */
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

int errlog_add(uint16_t code, uint32_t arg)
{
    err_entry_t *e = &s_ring.entry[s_ring.head];
    e->code = code;
    e->uptime_s = uptime_s_now();
    e->boot = (uint16_t)s_boot_cnt;
    e->arg = arg;
    s_ring.head = (uint8_t)((s_ring.head + 1) % ERR_RING_LEN);
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) == ESP_OK) nvs_commit(s_h_err);
    ESP_LOGW(TAG, "errlog 0x%04x arg=%u", code, (unsigned)arg);
    return 0;
}

int lt_errlog_snapshot(lt_err_entry_t *out, int cap)
{
    if (!out || cap <= 0) return 0;
    /* head is the next write slot, so slot `head` is the oldest surviving entry once the ring has
     * wrapped; before wrap those slots are still zero. Walking head..head+LEN-1 (mod LEN) yields
     * oldest->newest; a zero `code` marks an untouched slot (real codes are >= 0x0101, §17.7). */
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
    return n;
}

void lt_errlog_clear(void)
{
    memset(&s_ring, 0, sizeof(s_ring));   /* head back to 0; layout unchanged, ring emptied */
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof(s_ring)) == ESP_OK) nvs_commit(s_h_err);
}

void lt_crashlog_push(uint8_t reset_reason, uint32_t prev_uptime_s)
{
    s_crash[2] = s_crash[1];
    s_crash[1] = s_crash[0];
    s_crash[0].reset_reason = reset_reason;
    s_crash[0].uptime_s = prev_uptime_s;
    if (nvs_set_blob(s_h_sys, K_CRASH, s_crash, sizeof(s_crash)) == ESP_OK) nvs_commit(s_h_sys);
}

bool lt_reset_is_abnormal(int r)
{
    switch (r) {
    case ESP_RST_PANIC:
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
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
    nvs_get_u32(s_h_sys, K_SAFE, &v);
    return v;
}

int lt_safe_until_set(uint32_t boot_cnt)
{
    if (nvs_set_u32(s_h_sys, K_SAFE, boot_cnt) != ESP_OK) return -1;
    nvs_commit(s_h_sys);
    return 0;
}

void lt_safe_clear(void)
{
    /* §17.5 uptime-based auto-clear: zero the gate so `boot_cnt <= lt_safe_until_get()` is false
     * on every later boot (boot_cnt is >=1 from the first boot onward). */
    (void)lt_safe_until_set(0);
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
        errlog_add(E_SYS_PANIC, 0);
        break;
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
        lt_counters_inc(LT_CTR_WDT, true);
        errlog_add(E_SYS_WDT_RESET, (uint32_t)reset_reason);
        break;
    case ESP_RST_BROWNOUT:
        lt_counters_inc(LT_CTR_BROWNOUT, true);
        errlog_add(E_SYS_BROWNOUT, 0);
        break;
    default:
        break;
    }
}

/* ---- cfg blob (lt_cfg/cfg): packed cfg_t (leading version, §15.1) + trailing CRC16 (§15.2) ---- */
int lt_cfg_load(cfg_t *c)
{
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
    uint8_t buf[sizeof(cfg_t) + 2];
    memcpy(buf, c, sizeof(cfg_t));
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFF);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);
    if (nvs_set_blob(s_h_cfg, K_CFG, buf, sizeof(buf)) != ESP_OK) return -1;
    nvs_commit(s_h_cfg);
    return 0;
}
