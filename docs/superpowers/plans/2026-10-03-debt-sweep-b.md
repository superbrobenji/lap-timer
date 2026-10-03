# Debt Sweep B — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the remaining pre-bench items: configuration changes reach the pipeline and the ui (#87), mph-defined SPEED_FROM0 gates (#86), the error ring framed in place (−345 B DRAM), and on the dev-kit the first-list failure (#65), log download polish (#67) and a live-monitor freshness cue (#68).

**Architecture:** Lap-timer: two ui-only event codes posted directly to the ui queue (never through `emit_event`), the already-defined `CMD_CONFIG_RELOAD` wired into the pipeline, a unit-aware `drag_cfg_from_user`/`drag_gate_label`, and an `err_ring_t` whose RAM layout is the on-flash frame. Dev-kit: a `mem_find`-tolerant streaming header parse plus a buffered first chunk so early failures return clean errors, `rx_us` + `Content-Disposition` on log downloads, and a pane-local stream-freshness timer in `app.js`.

**Tech Stack:** C11 (Power of 10), ESP-IDF v5.3.2, Unity host tests (lap-timer `test/`, dev-kit `devcontroller/test/`), vanilla JS (`devcontroller/web/app.js`).

**Spec:** `docs/superpowers/specs/2026-10-03-debt-sweep-b-design.md` (binding). Fact sheet: the sweep workspace's `debt-sweep-B-facts.md`. Two spec corrections folded in below: the transcoder's type key is `"t"` (not `"rec"`), and STATUS records already transcode (`"t":"status"`), so §6's third bullet is already satisfied.

## Global Constraints

- Power of 10 on all firmware and `core` code: functions of > 20 code lines carry ≥ 2 `CORE_ASSERT_*`/`LT_ASSERT_*` (never abort) — EXCEPT code that runs under `s_blob_lock` in `lt_nvs.c`, which is assert-free by ruling F-2 (its assert sink takes that lock); every loop bounded; no recursion, heap, goto or function pointers; ≤ 60 code lines per function; `python3 tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation` = 0.
- Zero warnings: both host harnesses (`-Wall -Wextra -Werror -Wshadow -Wconversion`), gcc-16 sweep of the lap-timer `compile_commands.json`, clean ccache-disabled `moto_sim`, `moto_neo6m`, `PANEL=ws29v2 ./build.sh moto_sim build`, and the dev-kit firmware build.
- `EV_CFG_CHANGED`/`EV_LAP_RESET` are never passed to `emit_event()` (they must not reach the session log or the stream).
- Gate thresholds (`drag_gate_def_t.a/b`) stay km/h at all times; only the label converts.
- The on-flash `ring` blob bytes are unchanged (no migration); `cfg` untouched.
- `lt_proto.h` stays IDF-free; the dev-kit's on-disk log format (`logstore_rec_hdr_t`, `.bin`) is unchanged.
- Never flash from a subagent; the controller runs bench gate `dsB-d1` with the user.
- Commit messages end with the session trailers used on this branch (`git log -1`).

---

## File structure

| File | Responsibility | Task |
|---|---|---|
| `components/core/dragengine/drag_cfg.c`, `include/core/drag.h`, `test/test_drag_cfg.c`, `components/app/ui/ui.c` (one call) | mph SPEED_FROM0 thresholds + mph labels | 1 |
| `components/core/include/core/event.h`, `components/app/cmd/cmd.c`, `components/app/pipeline/pipeline.c`, `components/app/ui/ui.c` | cfg-change propagation, lap reset to the ui | 2 |
| `components/app/sys/lt_nvs.c` | ring framed in place, 39 B scratch | 3 |
| `devcontroller/components/linkhost/host/linkhost_proto.c`, `devcontroller/test/test_dl_leading_noise.c`, `devcontroller/components/webapi/webapi.c`, `devcontroller/web/app.js` | first-list robustness | 4 |
| `devcontroller/components/logstore/host/logstore_json.c`, `include/logstore_rec.h`, `devcontroller/test/test_logstore_json.c`, `webapi.c`, `devcontroller/web/index.html` | log download polish | 5 |
| `devcontroller/web/app.js`, `app.css` | monitor freshness cue | 6 |
| roadmap, spec §10 notes, issue comments | docs | 7 |

---

### Task 1: mph-defined SPEED_FROM0 gates (#86)

**Files:**
- Modify: `components/core/dragengine/drag_cfg.c` (`drag_cfg_from_user`, `drag_gate_label`), `components/core/include/core/drag.h` (prototype + doc), `test/test_drag_cfg.c` (two existing cases updated, three added), `components/app/ui/ui.c:771` (`row_from_gate`'s label call passes `s_model.units`)

**Interfaces (produced):**

```c
/* core/drag.h */
void drag_cfg_from_user(const cfg_t *cfg, drag_cfg_t *out);   /* unchanged signature; mph benches now define gates 1..4 */
int  drag_gate_label(const drag_gate_def_t *g, uint8_t units, char *buf, size_t cap);   /* units: 0 km/h, 1 mph */
#define DRAG_MPH_PER_KMH 1.609344
```

Rules: in mph mode (`cfg->units == CFG_UNITS_MPH`, `n_mph > 0`) the first `min(n_mph, 4)` gates of kind `DRAG_SPEED_FROM0` in table order (ids 1–4) get `a = (uint16_t)lround(mph * DRAG_MPH_PER_KMH)` and `out->benches_kmh[i]` the same km/h value, `n_benches = n`; other kinds untouched; ids untouched. km/h mode: unchanged (benches copied, gates untouched). Empty list in either unit → defaults stand. Label: `DRAG_SPEED_FROM0` in mph prints `0-<lround(a / DRAG_MPH_PER_KMH)>`; every other kind prints exactly as today in both units.

- [ ] **Step 1: Failing tests** — `test/test_drag_cfg.c`: update `test_units_never_touch_gates` → rename to `test_mph_benches_define_speed_gates`:

```c
static void test_mph_benches_define_speed_gates(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH;
    c.drag.n_mph = 2; c.drag.benches_mph[0] = 60; c.drag.benches_mph[1] = 100;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_UINT8(1, d.units);
    TEST_ASSERT_EQUAL_UINT8(want.n_gates, d.n_gates);
    TEST_ASSERT_EQUAL_UINT8(1, d.gates[0].id);   TEST_ASSERT_EQUAL_UINT16(97, d.gates[0].a);    /* 60 mph */
    TEST_ASSERT_EQUAL_UINT8(2, d.gates[1].id);   TEST_ASSERT_EQUAL_UINT16(161, d.gates[1].a);   /* 100 mph */
    TEST_ASSERT_EQUAL_UINT16(want.gates[2].a, d.gates[2].a);                                    /* defaults kept */
    TEST_ASSERT_EQUAL_UINT16(want.gates[3].a, d.gates[3].a);
    for (uint8_t i = 4; i < want.n_gates; i++) TEST_ASSERT_EQUAL_MEMORY(&want.gates[i], &d.gates[i], sizeof(drag_gate_def_t));
    TEST_ASSERT_EQUAL_UINT8(2, d.n_benches);
    TEST_ASSERT_EQUAL_UINT16(97, d.benches_kmh[0]); TEST_ASSERT_EQUAL_UINT16(161, d.benches_kmh[1]);
    char buf[8];
    TEST_ASSERT_EQUAL_INT(4, drag_gate_label(&d.gates[0], 1, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("0-60", buf);
    TEST_ASSERT_EQUAL_INT(5, drag_gate_label(&d.gates[1], 1, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("0-100", buf);
    TEST_ASSERT_EQUAL_INT(7, drag_gate_label(&d.gates[4], 1, buf, sizeof buf)); TEST_ASSERT_EQUAL_STRING("100-200", buf);   /* range: raw km/h */
}
static void test_mph_empty_list_keeps_defaults(void)
{
    cfg_t c; cfg_defaults(&c); c.units = CFG_UNITS_MPH; c.drag.n_mph = 0;
    drag_cfg_t d; drag_cfg_from_user(&c, &d);
    drag_cfg_t want; drag_cfg_defaults(&want);
    TEST_ASSERT_EQUAL_MEMORY(want.gates, d.gates, sizeof want.gates);
    TEST_ASSERT_EQUAL_UINT8(3, d.n_benches);
}
static void test_mph_round_trip_is_exact(void)
{
    for (unsigned m = 1; m <= 300; m++) {
        drag_gate_def_t g = { 1, DRAG_SPEED_FROM0, (uint16_t)lround((double)m * DRAG_MPH_PER_KMH), 0 };
        char buf[8], want[8];
        (void)snprintf(want, sizeof want, "0-%u", m);
        TEST_ASSERT_TRUE(drag_gate_label(&g, 1, buf, sizeof buf) > 0);
        TEST_ASSERT_EQUAL_STRING(want, buf);
    }
}
```

Every existing `drag_gate_label(x, buf, cap)` call in the file becomes `drag_gate_label(x, 0, buf, cap)`. Add `#include <math.h>` and `<stdio.h>`. Register the three in `main`. (`test/CMakeLists.txt` already links `m` for the core tests — confirm; add `m` to `add_core_test`'s link line if `lround` fails to link.)

- [ ] **Step 2: Run to fail** (compile error on the new signature).

- [ ] **Step 3: Implement** in `drag_cfg.c`:

```c
#include <math.h>
static uint16_t mph_to_kmh(uint16_t mph) { return (uint16_t)lround((double)mph * DRAG_MPH_PER_KMH); }
static unsigned kmh_to_mph(uint16_t kmh) { return (unsigned)lround((double)kmh / DRAG_MPH_PER_KMH); }

/* mph mode: the first n SPEED_FROM0 gates (table order, ids 1..4) take the mph bench list,
 * converted to the engine's km/h (#86). Range/dist/brake gates and all ids are untouched. */
static void apply_mph_benches(const cfg_t *cfg, drag_cfg_t *out, uint8_t n)
{
    CORE_ASSERT_VOID(cfg != NULL && out != NULL, DRAGCFG_ASSERT_CODE);
    CORE_ASSERT_VOID(n <= 4u && out->n_gates <= DRAG_MAX_GATES, DRAGCFG_ASSERT_CODE);
    uint8_t k = 0;
    for (uint8_t i = 0; i < out->n_gates && k < n; i++) {
        if (out->gates[i].kind != DRAG_SPEED_FROM0) continue;
        out->gates[i].a     = mph_to_kmh(cfg->drag.benches_mph[k]);
        out->benches_kmh[k] = out->gates[i].a;
        k++;
    }
    out->n_benches = k;
}
```

`drag_cfg_from_user`: after `out->rollout = ...`, branch: `if (cfg->units == CFG_UNITS_MPH) { uint8_t n = cfg->drag.n_mph; if (n > cap) n = cap; if (n == 0u) return; apply_mph_benches(cfg, out, n); return; }` then the existing km/h path. Rewrite the R-6 comment: "R-6's bug was writing mph numbers as km/h; mph benches now convert (spec dsB §3)". `drag_gate_label(g, units, buf, cap)`: `CORE_ASSERT_RET(units <= 1u, ..., -1)`; `case DRAG_SPEED_FROM0: return put_num_pair(buf, cap, 0u, "-", units ? kmh_to_mph(g->a) : g->a);`. Header doc updated. `ui.c:771`: `drag_gate_label(g, s_model.units, r->label, sizeof r->label)`.

- [ ] **Step 4: Run to pass** — `./test/build/test_drag_cfg` 7/7; full `ctest` green.

- [ ] **Step 5: Gate + commit** — lint 0; host 0 warnings; gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings.

```
feat(drag): mph bench list defines the SPEED_FROM0 thresholds (converted to km/h) and labels print mph (debt sweep B T1, #86)
```

---

### Task 2: Configuration changes reach the pipeline and the ui (#87)

**Files:**
- Modify: `components/core/include/core/event.h` (two codes), `components/app/cmd/cmd.c` (`op_config_set`, includes), `components/app/pipeline/pipeline.c` (`handle_cmd`: `CMD_CONFIG_RELOAD`, `CMD_RESET_ENGINE`), `components/app/ui/ui.c` (`handle_event`, two helpers)

**Interfaces (produced):**

```c
/* core/event.h — append after EV_FAULT */
    EV_CFG_CHANGED   = 16,  /* ui-only: posted straight to g_ui_evt_q by cmd.c, never via emit_event(), never logged/streamed */
    EV_LAP_RESET     = 17   /* ui-only: posted straight to g_ui_evt_q by the pipeline on CMD_RESET_ENGINE */
```

- [ ] **Step 1: cmd.c** — in `op_config_set` after the successful `lt_cfg_save`, before the ack:

```c
    cfg_change_notify();                                    /* #87: pipeline + ui reload */
```

with

```c
/* #87: after a persisted CONFIG_SET, tell the pipeline (CMD_CONFIG_RELOAD) and the ui
 * (EV_CFG_CHANGED, posted directly -- never emit_event(), so it is not logged or streamed).
 * Non-blocking: a full queue is reported, not fatal (the next reload or reboot catches up). */
static void cfg_change_notify(void)
{
    LT_ASSERT_VOID(g_cmd_q != NULL, CMD_ASSERT_CODE);
    LT_ASSERT_VOID(g_ui_evt_q != NULL, CMD_ASSERT_CODE);
    command_t c = { .type = CMD_CONFIG_RELOAD };
    if (xQueueSend(g_cmd_q, &c, 0) != pdTRUE) ESP_LOGW(TAG, "config reload: cmd queue full");
    event_t ev = { .type = EV_CFG_CHANGED, .mono_us = esp_timer_get_time() };
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "config changed: ui queue full");
}
```

(`cmd.c` already includes `app/lt_ipc.h` and `esp_timer.h`; add `core/event.h` if not transitively present. `cmd.c` has no `TAG`: add `static const char *TAG = "cmd";` next to its other statics, or use the file's existing log idiom if one exists.)

- [ ] **Step 2: pipeline.c** — `handle_cmd`:

```c
    case CMD_CONFIG_RELOAD:
        pipeline_reload_cfg();
        break;
```

```c
/* #87: re-read the persisted cfg and rebuild what depends on it -- the drag engine's gate table
 * (a units change redefines the mph gates: drag_init() is a fresh engine, session bests are
 * dropped, documented in spec dsB §2) and the riding mode. The lap engine is untouched. */
static void pipeline_reload_cfg(void)
{
    cfg_t      cfg;
    drag_cfg_t dc;
    cfg_defaults(&cfg);
    (void)lt_cfg_load(&cfg);
    drag_cfg_from_user(&cfg, &dc);
    drag_init(&s_drag, &dc);
    s_mode = (cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    publish_drag_snapshot();
    LT_ASSERT_VOID(s_drag.cfg.n_gates <= DRAG_MAX_GATES, PIPE_ASSERT_CODE);
    LT_ASSERT_VOID(s_mode == MODE_LAP || s_mode == MODE_DRAG, PIPE_ASSERT_CODE);
    ESP_LOGI(TAG, "config reloaded (units %u, mode %u)", (unsigned)cfg.units, (unsigned)s_mode);
}
```

`CMD_RESET_ENGINE`: after `publish_drag_snapshot();` add `ui_post_lap_reset();`:

```c
static void ui_post_lap_reset(void)
{
    LT_ASSERT_VOID(g_ui_evt_q != NULL, PIPE_ASSERT_CODE);
    event_t ev = { .type = EV_LAP_RESET, .mono_us = esp_timer_get_time() };
    LT_ASSERT_VOID(ev.type == EV_LAP_RESET, PIPE_ASSERT_CODE);
    if (xQueueSend(g_ui_evt_q, &ev, 0) != pdTRUE) ESP_LOGW(TAG, "lap reset: ui queue full");
}
```

Update the `default:` comment (CONFIG_RELOAD no longer "later"). `cfg_t` + `drag_cfg_t` on the pipeline stack ≈ 230 B (the boot path already does this).

- [ ] **Step 3: ui.c** — `handle_event`:

```c
    case EV_CFG_CHANGED: ui_reload_cfg(); break;
    case EV_LAP_RESET:   ui_lap_reset();  break;
```

```c
/* #87: a peer CONFIG_SET persisted a new cfg -- reload the ui's working copy and everything
 * derived from it (units suffixes, DRAG gate labels/benches, riding mode). */
static void ui_reload_cfg(void)
{
    cfg_defaults(&s_cfg);
    (void)lt_cfg_load(&s_cfg);
    s_model.units = s_cfg.units;
    s_mode        = (s_cfg.mode == CFG_MODE_DRAG) ? (uint8_t)MODE_DRAG : (uint8_t)MODE_LAP;
    s_model.mode  = s_mode;
    drag_cfg_from_user(&s_cfg, &s_drag_cfg);
    snprintf(s_lbl_units, sizeof s_lbl_units, "Units: %s", s_cfg.units == CFG_UNITS_MPH ? "mph" : "km/h");
    snprintf(s_lbl_mode, sizeof s_lbl_mode, "Mode: %s", s_mode == MODE_DRAG ? "Drag" : "Lap");
    if (s_model.mode == SCR_MODE_DRAG) drag_rows_refill();
    s_dirty = true;
    LT_ASSERT_VOID(s_drag_cfg.n_gates <= DRAG_MAX_GATES, UI_APP_ASSERT_CODE);
    LT_ASSERT_VOID(s_model.units <= 1u, UI_APP_ASSERT_CODE);
}
/* #87: the pipeline's engines were reset remotely -- the running-lap clock must not keep counting. */
static void ui_lap_reset(void)
{
    s_lap_start_mono_us = 0;
    s_last_clock_us     = 0;
    s_model.cur_ms      = 0;
    s_model.cur_running = false;
    s_dirty             = true;
}
```

(The ui has only `s_lbl_mode`/`s_lbl_units` labels; `menu_do_display` reads `s_cfg.display.live_clock` live, so the reload above is enough for the live-clock toggle.) `ui_lap_reset` is ≤ 20 lines (no asserts required).

- [ ] **Step 4: Verify** — `ctest` green (event.h compiles into core tests); lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings; `grep -n "EV_CFG_CHANGED\|EV_LAP_RESET" components` shows no `emit_event` call site with either code.

- [ ] **Step 5: Commit**

```
feat(cfg): a persisted CONFIG_SET reloads the pipeline (CMD_CONFIG_RELOAD) and the ui (EV_CFG_CHANGED); CMD_RESET_ENGINE clears the ui lap clock (debt sweep B T2, #87)
```

---

### Task 3: Error ring framed in place (DRAM reclaim)

**Files:**
- Modify: `components/app/sys/lt_nvs.c` (`err_ring_t`, `BLOB_SCRATCH_MAX`, `lt_nvs_init`, `errlog_persist`, `lt_errlog_clear`, new `ring_seal`/`ring_check`)

Everything here runs under `s_blob_lock` → assert-free (ruling F-2).

- [ ] **Step 1: Layout**

```c
typedef struct __attribute__((packed)) {
    uint8_t     ver;                            /* frame version byte == LT_RING_VER (blob.h layout) */
    err_entry_t entry[ERR_RING_LEN];
    uint8_t     head;                           /* next write slot */
    uint16_t    crc;                            /* LE crc16 over ver..head (blob.h layout) */
} err_ring_t;                                   /* 388 B == the on-flash `ring` blob, byte for byte */
_Static_assert(sizeof(err_ring_t) == 388, "error ring IS its on-flash frame: [ver][32 entries][head][crc16]");
#define RING_CRC_SPAN (sizeof(err_ring_t) - 2u)
#define BLOB_SCRATCH_MAX (sizeof(lt_counters_t) + BLOB_OVERHEAD)   /* counters (36) and crash log (15) only: 39 B */
_Static_assert(sizeof(crash_entry_t) * CRASH_LOG_LEN + BLOB_OVERHEAD <= BLOB_SCRATCH_MAX, "crash log fits the scratch");
```

- [ ] **Step 2: Helpers** (assert-free, caller holds the lock):

```c
/* Seal the ring mirror in place: version byte + crc16 over everything before the crc field. */
static void ring_seal(void)
{
    s_ring.ver = (uint8_t)LT_RING_VER;
    uint16_t c = ses_crc16((const uint8_t *)&s_ring, RING_CRC_SPAN);
    s_ring.crc = c;                              /* packed struct: little-endian store on this target */
}
/* Verify a ring read straight from NVS. 0 ok; -1 size; -2 crc; -3 version. */
static int ring_check(size_t sz)
{
    if (sz != sizeof(s_ring)) return -1;
    if (ses_crc16((const uint8_t *)&s_ring, RING_CRC_SPAN) != s_ring.crc) return -2;
    if (s_ring.ver != (uint8_t)LT_RING_VER) return -3;
    return 0;
}
/* Write the sealed mirror directly (no scratch). 0 ok. */
static int ring_save(void)
{
    ring_seal();
    if (nvs_set_blob(s_h_err, K_RING, &s_ring, sizeof s_ring) != ESP_OK) return -1;
    (void)nvs_commit(s_h_err);
    return 0;
}
/* Boot: read the ring directly; on absent → zero quietly; on size/crc/version failure → zero + mask. */
static int ring_load(void)
{
    size_t sz = 0;
    esp_err_t probe = nvs_get_blob(s_h_err, K_RING, NULL, &sz);
    int rc;
    if (probe == ESP_ERR_NVS_NOT_FOUND) rc = -4;
    else if (probe != ESP_OK || sz != sizeof s_ring) rc = -1;
    else if (nvs_get_blob(s_h_err, K_RING, &s_ring, &sz) != ESP_OK) rc = -1;
    else rc = ring_check(sz);
    if (rc != 0) {
        memset(&s_ring, 0, sizeof s_ring);
        if (rc == -4) ESP_LOGI(TAG, "nvs blob ring absent (fresh)");
        else { ESP_LOGW(TAG, "nvs blob ring reset (rc %d)", rc); s_blob_reset_mask |= (uint8_t)(1u << BLOB_TAG_RING); }
    }
    return rc;
}
```

`lt_nvs_init`: replace the ring `load_or_reset` call with `(void)ring_load();` (keep the head clamp after it). `errlog_persist` and `lt_errlog_clear`: replace `save_framed(... K_RING ...)` with `(void)ring_save();`. `s_ring.crc` is little-endian in memory on the ESP32 (and on the x86 host) — identical to `blob_wrap`'s two-byte LE store, so the on-flash bytes do not change; state this in a comment and cross-check against `blob.c`'s store order. The `errlog_add` fast path never touches `ver`/`crc` (only `ring_seal` does, at save).

- [ ] **Step 3: Verify** — lint 0; gcc-16 clean; clean `moto_sim` + `moto_neo6m` 0 warnings; `nm --size-sort` shows `s_blob_scratch` = 39 B and `s_ring` = 388 B; `.bss` delta ≈ −346 B; `moto_sim` DRAM remain (expect ≈ 4300 B) reported; `test_blob` unchanged.

- [ ] **Step 4: Commit**

```
perf(nvs): error ring framed in place (mirror == on-flash blob); blob scratch shrinks to 39 B (debt sweep B T3, −346 B DRAM)
```

---

### Task 4: Dev-kit first `/api/sessions` robustness (#65)

**Files:**
- Modify: `devcontroller/components/linkhost/host/linkhost_proto.c` (`dl_parse_begin`), `devcontroller/test/test_dl_leading_noise.c` (one case), `devcontroller/components/webapi/webapi.c` (`sessions_chunk_cb`, `do_sessions_stream`, `dl_sink_t`), `devcontroller/web/app.js` (`loadSessions`)

- [ ] **Step 1: Failing host test** — `test_dl_leading_noise.c` (its helpers: `build_frame(buf, name, wire, wire_len, ...)` builds a framed `list` reply with a real crc32, `build_stream(...)` assembles the noise + frame stream and returns the expected JSON, `sink_cb`/the sink struct collect decoded bytes, `lh_dl_init(&c, false, sink_cb, &s)` / `lh_dl_feed` / `lh_dl_result` drive the parser — read the three existing cases and mirror their shape):

```c
/* A prompt glued to the header with NO newline at all ("laptimer> ---BEGIN list 42---\r\n…"),
 * hardware-observed in dumb mode; the buffered small-frame parser tolerates it (5c1fdf0) and so
 * must the streaming one. Differs from the three cases above: no '\n' precedes ---BEGIN. */
static void test_prompt_glued_to_begin_no_newline(void)
{
    const char *json; size_t json_len;
    uint8_t frame[STREAM_MAX];
    size_t fl = build_frame(frame, "list", LIST_JSON, strlen(LIST_JSON), &json, &json_len);   /* adapt to build_frame's real signature */
    uint8_t in[STREAM_MAX + 16]; size_t n = 0;
    memcpy(in, "laptimer> ", 10); n += 10;
    memcpy(in + n, frame, fl); n += fl;
    sink_t s; memset(&s, 0, sizeof s);
    lh_dl_t c; lh_dl_init(&c, /*is_binary*/false, sink_cb, &s);
    for (size_t i = 0; i < n; i++) lh_dl_feed(&c, in + i, 1);            /* byte by byte */
    TEST_ASSERT_EQUAL_INT(0, lh_dl_result(&c));
    TEST_ASSERT_EQUAL_UINT(json_len, s.len);
    TEST_ASSERT_EQUAL_MEMORY(json, s.buf, json_len);
}
```

(`LIST_JSON`/`STREAM_MAX`/`sink_t` are the file's existing constants/types — use whatever the three cases use; the point is the glued prompt with no `\n` before `---BEGIN`.)

- [ ] **Step 2: Run to fail** — `cmake --build devcontroller/test/build && ./devcontroller/test/build/test_dl_leading_noise` → the new case fails (timeout/not found).

- [ ] **Step 3: Parser** — `dl_parse_begin`: replace the `memcmp(line, PFX, pl) != 0` gate with `const uint8_t *b = mem_find((const uint8_t *)line, len, PFX, pl); if (b == NULL) return false; size_t off = (size_t)(b - (const uint8_t *)line); line += off; len -= off;` then proceed as before. Comment: "mirrors linkhost_parse_frame's 5c1fdf0 tolerance for a prompt glued to the marker".

- [ ] **Step 4: Clean early error** — `dl_sink_t` gains `uint8_t first[LH_DL_CHUNK]; size_t first_len; bool committed;`. `sessions_chunk_cb`: while `!committed`, copy into `first` (bounded by `LH_DL_CHUNK`); when `first_len == LH_DL_CHUNK` or on the next call, set headers, send `first`, `committed = true`, then send the current chunk. `do_sessions_stream`: on `rc == 0` with `!committed` → set headers, send `first` (may be empty), terminate; on `rc != 0` with `!committed` → `send_stream_error` (the 502 path — `send_stream_error`'s JSON gains `"rc":<rc>` and `"where":"pre-commit"`); on `rc != 0` with `committed` → log `WARN "sessions rc=%d post-commit -> abort socket"` and close as today. Bound the copy loop by `LH_DL_CHUNK`.

- [ ] **Step 5: SPA** — `loadSessions`: on `res.status === 502` or `res.networkError` the first time, retry once after 500 ms (`setTimeout`), then show the error with `res.data && res.data.error ? res.data.error : "bad response"` in the message.

- [ ] **Step 6: Verify** — dev-kit `ctest` green (new case passes; existing 3 unchanged); dev-kit firmware build 0 warnings; lap-timer untouched.

- [ ] **Step 7: Commit**

```
fix(devcontroller): streaming list parser tolerates a glued prompt; first-chunk buffering gives a clean 502 on early failure; SPA retries once (debt sweep B T4, #65)
```

---

### Task 5: Log download polish (#67)

**Files:**
- Modify: `devcontroller/components/logstore/host/logstore_json.c`, `devcontroller/components/logstore/include/logstore_rec.h` (format note), `devcontroller/test/test_logstore_json.c`, `devcontroller/components/webapi/webapi.c` (`do_log_download`: `Content-Disposition`), `devcontroller/web/index.html` (Logs tab note)

- [ ] **Step 1: Failing tests** — `test_logstore_json.c`: in `test_fused_record_transcodes` assert the JSON contains `"rx_us":1234567890` (the header's `ts_us` the helper writes); add `test_status_record_transcodes` (a `LT_REC_STATUS` payload of `LT_STATUS_LEN` bytes built at the `LT_ST_OFF_*` offsets, expect `"t":"status"` and `"rx_us"`). Register.

- [ ] **Step 2: Run to fail.**

- [ ] **Step 3: Implement** — `logstore_rec_to_json`: after `linkhost_stream_to_json` returns `n > 0`, insert `"rx_us":<hdr.ts_us>` after the opening `{`: build into a local `char tmp[LT_JSON_MAX]`? No — avoid a second buffer: call `linkhost_stream_to_json(&rec, out + k, out_cap - k)` where `k = snprintf(out, out_cap, "{\"rx_us\":%llu,", (unsigned long long)hdr.ts_us)` and then overwrite the inner object's leading `{` by shifting: simplest correct form — emit the prefix with `snprintf`, call the inner transcoder into `out + k`, then `memmove(out + k, out + k + 1, (size_t)n)` to drop the inner `{` (bounded by `n`), return `k + n - 1`. Guard every length against `out_cap`. (Document that the inner JSON always starts with `{`.) `webapi.c` `do_log_download`: `httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"<id>.jsonl\"")` for jsonl and `.bin` for bin — the `dl_sink_t`'s `disp` pattern at `webapi.c:371-377` is the model (a function-local `char disp[64]` is fine: this is the HTTP task). `logstore_rec.h` + `index.html` Logs tab: the one-paragraph format note from spec §6 with `t` (not `rec`).

- [ ] **Step 4: Verify** — dev-kit `ctest` green (8/8 in the file); dev-kit firmware build 0 warnings.

- [ ] **Step 5: Commit**

```
feat(devcontroller): log download carries rx_us per record and a filename; NDJSON format note (debt sweep B T5, #67)
```

---

### Task 6: Live-monitor freshness cue (#68)

**Files:**
- Modify: `devcontroller/web/app.js` (monitor section: `startMonitor`, `stopMonitor`, `appendMonitorRow`), `devcontroller/web/app.css` (`.monitor-pane .marker`)

- [ ] **Step 1:** In the monitor closure: `var lastMsgAt = 0, stalled = false, freshTimer = null;`. `appendMonitorRow` sets `lastMsgAt = Date.now()`; if `stalled` → append `'{"info":"stream resumed"}'`-style marker row (`el("div", { className: "marker", text: "— stream resumed —" })`) and `stalled = false`. `startMonitor` after creating the EventSource: `freshTimer = setInterval(tick, 1000)` where `tick`: if `monitorSource && lastMsgAt && !stalled && Date.now() - lastMsgAt > 3000` → append `— stream stopped —` marker, `stalled = true`; always update `#monitor-count` to `monitorRowCount + " records · last " + (lastMsgAt ? Math.round((Date.now() - lastMsgAt) / 1000) + "s ago" : "—")`. `stopMonitor`: `clearInterval(freshTimer); freshTimer = null; lastMsgAt = 0; stalled = false`. Marker rows do not count as records. CSS: `.monitor-pane .marker { color: var(--warn); font-style: italic; }`.

- [ ] **Step 2: Verify** — `node --check devcontroller/web/app.js` (syntax); open `index.html` statically if a browser is available — otherwise the bench verifies; dev-kit firmware build (the web assets are embedded — confirm the build still passes).

- [ ] **Step 3: Commit**

```
feat(devcontroller/web): live monitor shows stream stopped/resumed markers and record freshness (debt sweep B T6, #68)
```

---

### Task 7: Docs, bench, PR

**Files:**
- Modify: `docs/superpowers/plans/2026-09-14-roadmap.md` (entry after Debt sweep A), `docs/superpowers/specs/2026-10-03-debt-sweep-b-design.md` (§10 implementation notes: rulings, measured DRAM, the two spec corrections), `docs/superpowers/specs/2026-09-29-plan-7c-display-closure-design.md` §10 (mph gates no longer deferred — pointer to dsB §3), `docs/superpowers/specs/2026-09-30-debt-sweep-a-design.md` §9 (ring reclaim done — pointer)

- [ ] **Step 1:** Write the edits; `git diff --check`; commit `docs(debt-sweep-b): roadmap entry, spec pointers, implementation notes`.
- [ ] **Step 2 (controller):** bench `dsB-d1` per spec §8 with the user (same day as the other two gates); PR against `debt-sweep-A`; close #86 #87 #65 #67 #68.

---

## Self-review

- **Spec coverage:** §2 → T2; §3 → T1; §4 → T3; §5 → T4; §6 → T5 (STATUS transcode already present — recorded as a correction); §7 → T6; §8 → each task's verify + T7 Step 2; §9 untouched.
- **Placeholders:** none; the dev-kit test in T4 names the file's helpers generically with an explicit instruction to adapt to the real names (the file's builder/sink exist; their names are read first).
- **Type consistency:** `drag_gate_label(const drag_gate_def_t*, uint8_t, char*, size_t)` (T1) used in ui.c and tests; `EV_CFG_CHANGED 16`/`EV_LAP_RESET 17` (T2) only posted via `xQueueSend(g_ui_evt_q, ...)`; `ring_seal/ring_check/ring_save/ring_load` (T3) private to lt_nvs.c; `dl_sink_t.first/first_len/committed` (T4) private to webapi.c.
