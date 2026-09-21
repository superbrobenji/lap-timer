/* link.c -- peer link: fused-log/event stream fan-out + peer-detect (spec §18, Plan 5 sub-project A).
 *
 * See app/link.h for the full contract. In short: the pipeline (single producer) calls stream_push()
 * per fused sample + per event; records go into a bounded lock-free SPSC ring (drop-newest on full,
 * so the pipeline is never stalled); a low-priority drain task frames each record as an unsolicited
 * §18.1 LINK_STREAM_TAG chunk and fans it out to every attached peer transport. Sinks resolve at
 * LINK time (weak no-op default here; export_serial provides the strong serial sink), so the
 * fan-out stores no function pointer -- Power of 10 rule-9 clean (mirrors core_assert_report).
 *
 * Peer-detect: with a detect pin wired (LINK_DETECT_GPIO >= 0 -- both build envs set it
 * unconditionally today, see components/app/CMakeLists.txt), the GPIO detect line is the
 * DEFINITIVE presence signal; the `cmd` heartbeat (link_note_cmd_activity, called by the serial
 * transport on every request; a STATUS poll is the handshake) is the presence signal ONLY as a
 * fallback on a build with no detect pin (LINK_DETECT_GPIO < 0). With no peer the ring is never
 * fed and the drain task idles -- no hang, no error spam. All state is static (no allocation), so
 * detach leaks nothing.
 */
#include "app/link.h"

#include "app/cmd.h"          /* LINK_STREAM_TAG, CMD_FLAG_LAST, CMD_CHUNK_MAX */
#include "app/lt_assert.h"
#include "app/lt_proto.h"     /* LT_STREAM_TAG -- the shared wire contract a dev-controller peer decodes against */

#include "core/ring.h"
#include "core/event.h"      /* event_t -- sizing the SES_T_EVENT stream record (FIX 3 static assert) */
#include "core/types.h"      /* fused_sample_t -- sizing the SES_T_FUSED stream record (FIX 3 static assert) */
#include "core/ses.h"        /* SES_T_* -- the stream record type byte */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include <stdatomic.h>
#include <string.h>

#define LINK_ASSERT_CODE 0x0B90   /* Power of 10 rule 5 (app/lt_assert.h); link.c's own code */

/* GPIO detect line (§6 connector). Both build envs today set this unconditionally to GPIO4 via
 * components/app/CMakeLists.txt (which also unconditionally REQUIREs `esp_driver_gpio`), so
 * link_serial_present() below always takes the definitive detect-line path. -1 is a defensive
 * default only, for a hypothetical build that does not define LINK_DETECT_GPIO -- on such a build
 * presence falls back to the `cmd` heartbeat alone (see link_serial_present()). */
#ifndef LINK_DETECT_GPIO
#define LINK_DETECT_GPIO (-1)
#endif
#if LINK_DETECT_GPIO >= 0
#include "driver/gpio.h"
#endif

/* §4.3-style task: core 0 (off the pipeline's core 1), low priority (best-effort telemetry, must
 * not preempt logging/ui), NOT supervised or WDT-subscribed -- a stalled/absent peer must never
 * reset the lap-timer. */
#define LINK_CORE         0
#define LINK_PRIO         4
#define LINK_STACK_BYTES  3072
#define LINK_STACK_WORDS  (LINK_STACK_BYTES / sizeof(StackType_t))
#define LINK_POLL_MS      20                 /* drain-wake / detect-poll cadence (also the notify timeout) */
#define LINK_DETECT_STABLE   3               /* consecutive 20ms polls a level must hold before it flips presence (~60 ms) */
#define LINK_DRAIN_BURST  32                 /* rule 2: max records drained per wake (> ring cap) */
#define LINK_PEER_TIMEOUT_MS 3000u           /* mark the peer absent this long after the last heartbeat */
#define LINK_STREAM_CAP   16u                 /* ring depth (power of two); ~1.6 s of buffer at 10 Hz */

_Static_assert((LINK_STREAM_CAP & (LINK_STREAM_CAP - 1u)) == 0u, "LINK_STREAM_CAP must be a power of two");
_Static_assert(5 + LINK_REC_MAX <= CMD_CHUNK_MAX, "a stream frame (5-byte header + payload) must fit one §18.1 data chunk");
_Static_assert((int)LINK_STREAM_TAG == (int)LT_STREAM_TAG, "LINK_STREAM_TAG must mirror app/lt_proto.h's LT_STREAM_TAG");

/* The pipeline (pipeline.c) pushes each stream record as a type byte + the raw §14 struct:
 * SES_T_FUSED -> 1 + sizeof(fused_sample_t), SES_T_EVENT -> 1 + sizeof(event_t). If either struct
 * grows past LINK_REC_MAX, stream_push()'s `len > LINK_REC_MAX` guard would silently drop the whole
 * record -- catch that at compile time so a future field addition fails the build, not the stream. */
_Static_assert(1 + sizeof(fused_sample_t) <= LINK_REC_MAX, "SES_T_FUSED stream record must fit LINK_REC_MAX");
_Static_assert(1 + sizeof(event_t) <= LINK_REC_MAX, "SES_T_EVENT stream record must fit LINK_REC_MAX");

typedef struct { uint16_t len; uint8_t data[LINK_REC_MAX]; } link_rec_t;

/* --- static state (no heap, §17.9) --- */
static StaticTask_t s_tcb;
static StackType_t  s_stack[LINK_STACK_WORDS];
static TaskHandle_t s_task;
static link_rec_t   s_stream_store[LINK_STREAM_CAP];
static ring_t       g_stream_ring;
static uint16_t     s_seq;                        /* stream chunk sequence (drain task only) */
static bool         s_ready;                      /* published last in link_start() */
static _Atomic bool s_detect_asserted;            /* GPIO detect line state: written by link_task
                                                    * (core 0), read cross-core by stream_push ->
                                                    * link_peer_present() on the pipeline task
                                                    * (core 1) -- must be atomic (I3). */
static _Atomic uint32_t s_last_cmd_ms;            /* last cmd heartbeat (link_now_ms units) */
static _Atomic bool     s_cmd_seen;               /* a cmd request has arrived at least once */

/* --- weak sink defaults (a compiled-in transport overrides the strong symbol at link time) --- */
__attribute__((weak)) int  link_sink_serial_emit(const uint8_t *frame, size_t len) { (void)frame; (void)len; return 0; }
__attribute__((weak)) int  link_sink_ble_emit(const uint8_t *frame, size_t len)    { (void)frame; (void)len; return 0; }
__attribute__((weak)) bool link_sink_ble_present(void)                             { return false; }

static uint32_t link_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Serial peer present. With a detect pin wired (§6 connector), the detect line is DEFINITIVE: the
 * `cmd` heartbeat is NOT OR'd in, so an active console / status poll cannot mask an unplugged
 * dev-kit. The heartbeat is the presence signal ONLY on boards with no detect pin
 * (LINK_DETECT_GPIO < 0). See docs/superpowers/specs/2026-09-21-peer-presence-detect-design.md. */
static bool link_serial_present(void)
{
#if LINK_DETECT_GPIO >= 0
    return atomic_load(&s_detect_asserted);
#else
    if (!atomic_load(&s_cmd_seen)) return false;
    uint32_t elapsed = link_now_ms() - atomic_load(&s_last_cmd_ms);   /* modular; wrap-safe */
    return elapsed < LINK_PEER_TIMEOUT_MS;
#endif
}

void link_note_cmd_activity(void)
{
    atomic_store(&s_last_cmd_ms, link_now_ms());
    atomic_store(&s_cmd_seen, true);
}

bool link_peer_present(void) { return link_serial_present() || link_sink_ble_present(); }

uint32_t link_stream_dropped(void) { return ring_dropped(&g_stream_ring); }

static void link_poll_detect(void)
{
#if LINK_DETECT_GPIO >= 0
    /* active-low: a peer on the connector pulls the detect line to GND; the pin idles high on its
     * internal pull-up, so level 0 = present. Debounce: a level must hold for LINK_DETECT_STABLE
     * consecutive polls before it flips s_detect_asserted, so a bouncy connector/jumper does not
     * flap the stream on/off. detect_run/detect_cand are drain-task-only (link_task) state, so no
     * synchronization on THEM; s_detect_asserted itself is read cross-core (link_peer_present() on
     * the pipeline task) and is `_Atomic` for that reason (I3). */
    static uint8_t detect_run;               /* consecutive reads equal to detect_cand */
    static bool    detect_cand;              /* the candidate level being counted toward */
    bool raw = (gpio_get_level((gpio_num_t)LINK_DETECT_GPIO) == 0);
    if (raw != detect_cand) {
        detect_cand = raw;
        detect_run = 1;
    } else if (detect_run < LINK_DETECT_STABLE) {
        detect_run++;
    }
    if (detect_run >= LINK_DETECT_STABLE) {
        atomic_store(&s_detect_asserted, detect_cand);
    }
#else
    atomic_store(&s_detect_asserted, false);   /* no detect pin assigned yet (Plan 6 hardware); heartbeat only */
#endif
}

/* Frame one record as a §18.1 unsolicited stream chunk (tag|seq_lo|seq_hi|flags|len, see
 * app/lt_proto.h's lt_stream_hdr_t) and fan out to each attached sink. */
static void link_deliver(const link_rec_t *r)
{
    LT_ASSERT_VOID(r != NULL, LINK_ASSERT_CODE);                         /* drain popped a real slot */
    LT_ASSERT_VOID(r->len > 0 && r->len <= LINK_REC_MAX, LINK_ASSERT_CODE);   /* len set by stream_push's bound */
    uint8_t frame[5 + LINK_REC_MAX];
    uint16_t seq = s_seq++;
    frame[0] = LINK_STREAM_TAG;                 /* 0xFF: an unsolicited stream frame, not a cmd response */
    frame[1] = (uint8_t)seq;
    frame[2] = (uint8_t)(seq >> 8);
    frame[3] = CMD_FLAG_LAST;                   /* each record is one self-contained chunk */
    frame[4] = (uint8_t)r->len;                 /* len: payload byte count that follows (lt_stream_hdr_t) */
    memcpy(frame + 5, r->data, r->len);
    size_t flen = (size_t)5 + r->len;
    if (link_serial_present())   (void)link_sink_serial_emit(frame, flen);
    if (link_sink_ble_present()) (void)link_sink_ble_emit(frame, flen);
    /* A THIRD peer transport would fan out with one more `if (...present) sink_emit(...)` line. */
}

static void link_task(void *arg)
{
    (void)arg;
    LT_ASSERT_VOID(g_stream_ring.buf != NULL, LINK_ASSERT_CODE);          /* link_start init'd the ring first */
    LT_ASSERT_VOID(g_stream_ring.item_size == sizeof(link_rec_t), LINK_ASSERT_CODE);
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LINK_POLL_MS));      /* wake on push, else poll cadence */
        link_poll_detect();
        link_rec_t r;
        int n = 0;
        while (n < LINK_DRAIN_BURST && ring_pop(&g_stream_ring, &r)) {    /* rule 2: bounded burst */
            link_deliver(&r);
            n++;
        }
        LT_ASSERT_VOID(n <= LINK_DRAIN_BURST, LINK_ASSERT_CODE);          /* burst cap held */
    }
}

void stream_push(const uint8_t *rec, size_t len)
{
    if (!s_ready) return;                        /* before link_start(): drop */
    if (!link_peer_present()) return;            /* no peer: never feed the ring (cheapest path) */
    LT_ASSERT_VOID(rec != NULL, LINK_ASSERT_CODE);
    if (len == 0 || len > LINK_REC_MAX) return;  /* records are always in range; guard defensively */
    link_rec_t r;
    r.len = (uint16_t)len;
    memcpy(r.data, rec, len);
    (void)ring_push(&g_stream_ring, &r);         /* drop-newest on full; link_stream_dropped() counts it */
    if (s_task) xTaskNotifyGive(s_task);         /* wake the drain (best-effort) */
}

void link_start(void)
{
    if (s_ready) return;                         /* idempotent */
    ring_init(&g_stream_ring, s_stream_store, sizeof(link_rec_t), LINK_STREAM_CAP, false /*drop-newest*/);
#if LINK_DETECT_GPIO >= 0
    gpio_config_t io = { .pin_bit_mask = 1ULL << LINK_DETECT_GPIO, .mode = GPIO_MODE_INPUT,
                         .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
                         .intr_type = GPIO_INTR_DISABLE };
    (void)gpio_config(&io);
#endif
    s_task = xTaskCreateStaticPinnedToCore(link_task, "link", LINK_STACK_WORDS, NULL,
                                           LINK_PRIO, s_stack, &s_tcb, LINK_CORE);
    LT_ASSERT_VOID(s_task != NULL, LINK_ASSERT_CODE);   /* static task creation over our own storage must succeed */
    s_ready = true;                              /* published last: stream_push is a no-op until now */
}
