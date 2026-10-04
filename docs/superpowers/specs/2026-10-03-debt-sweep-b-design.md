# Debt sweep B — pre-bench closure (design)

**Date:** 2026-10-03. **Branch:** `debt-sweep-B`, stacked on `debt-sweep-A` (8e474b5); its PR targets `debt-sweep-A` and retargets as #89/#88 merge. **Bench:** gate `dsB-d1` on the same day as `p07c-d1` and `dsA-d1` (one lap-timer flash, one dev-kit flash).

Closes #86, #87 (lap-timer), #65, #67, #68 (dev-kit) and the debt-sweep-A follow-up "frame the ring mirror in place" (DRAM reclaim). Fact sheet behind every statement: the sweep workspace's `debt-sweep-B-facts.md`.

## 1. Scope and non-goals

In: the six items above. Out: #53 task stacks (bench measurement), #11 (M10), #54/#57 (Plan 6), #61 (sub-project C), #82 (Plan 8), VBO/NMEA black-box export (structurally impossible from fused+event records — `fused_sample_t` carries no position), a lap/split timing table from EVENT records (not asked for). No new subsystems.

## 2. #87 — configuration changes reach the pipeline and the ui

Today `op_config_set` (cmd.c) saves to NVS and acks; the pipeline reads cfg once at boot; the ui reloads only on its own menu actions. `CMD_CONFIG_RELOAD = 5` exists in `lt_ipc.h` and is handled by nobody.

- **Trigger.** `op_config_set`, after a successful `lt_cfg_save`, posts `command_t{ .type = CMD_CONFIG_RELOAD }` to `g_cmd_q` (non-blocking; a full queue is logged, not fatal) and posts `event_t{ .type = EV_CFG_CHANGED }` straight to `g_ui_evt_q` with `xQueueSend(..., 0)` — never through `emit_event()`, so the event is neither logged to the session EVENT record nor streamed to the dev-kit.
- **Event codes.** `core/event.h` gains `EV_CFG_CHANGED = 16` and `EV_LAP_RESET = 17`, commented "ui-only: posted directly to g_ui_evt_q, never emitted through the pipeline's emit_event(), never written to an EVENT record". `link.c`'s stream and the logger never see them by construction (only `emit_event` reaches those sinks).
- **Pipeline.** `handle_cmd` `CMD_CONFIG_RELOAD`: `cfg_defaults` + `lt_cfg_load` (defaults on failure, as at boot) → `drag_cfg_from_user` → `drag_init(&s_drag, &dc)` (a fresh engine: the current run is dropped, session bests are NOT kept — `drag_init` zeroes them; documented, since a units change redefines the mph gates) → `s_mode` re-derived → `publish_drag_snapshot()`. Lap engine untouched.
- **ui.** `handle_event` `EV_CFG_CHANGED`: reload `s_cfg` (defaults on failure), `s_model.units = s_cfg.units`, rebuild `s_drag_cfg`, `s_mode` re-derived — when it differs from the current mode, I2 (final review, ruling B-6) runs the same page-reset/lap-clock-stop `ui_apply_mode()` helper `menu_do_mode()`'s own local toggle uses, not a silent relabel — `drag_rows_refill()` if DRAG rows are showing, `s_dirty = true`. `EV_LAP_RESET`: `s_lap_start_mono_us = 0; s_model.cur_ms = 0; s_model.cur_running = false; s_last_clock_us = 0`; I4 (final review, ruling B-6) additionally clears the ui's PREV/BEST/session-stats/best-sector mirror (`best_ms`, `prev_ms`, `have_best`, `have_prev`, `laps_total`, `laps_valid`, `theo_best_ms`/`have_theo`, `best_n_sectors`, `best_sector_ms[]`/`have_best_sector[]`, the last-sector deltas, `big_kind = BIG_NONE`, `lap_no = 1`, the session-max accumulator) and refills the DRAG rows, mirroring `ui_task()`'s own fresh-session init field-for-field; `s_dirty = true`.
- **Reset.** `dbg reset` (lap-timer console, via `lt shell`) posts `command_t{ .type = CMD_RESET_ENGINE }` to `g_cmd_q` (B-4, final review: the console producer this spec originally assumed existed as a dev-kit "reset-engine" command — no code anywhere implemented one until this fix wave). `handle_cmd` `CMD_RESET_ENGINE` additionally posts `EV_LAP_RESET` to `g_ui_evt_q` (direct send) after its existing work.
- **Ordering.** The pipeline and the ui each reload cfg from NVS independently (no shared copy); both see the saved blob because `lt_cfg_save` committed before either post.

## 3. #86 — mph-defined SPEED_FROM0 gates

Ruling R-6 (Plan 7c) rolled back a rewrite of `gates[].a` with mph numbers *as km/h*. The engine's unit is km/h (`drag.h` contract, `kmh_to_mps` at every comparison); the fix is conversion, not a new table.

- `drag_cfg_from_user`: when `cfg->units == CFG_UNITS_MPH` and `n_mph > 0`, the first `min(n_mph, 4)` `DRAG_SPEED_FROM0` gates (ids 1–4, table order) get `a = (uint16_t)lround(mph * 1.609344)` and `out->benches_kmh[i]` gets the same km/h value; `n_benches = n`; ids never change; range/dist/brake gates untouched. km/h mode is unchanged. Empty list → defaults stand (either unit).
- `drag_gate_label(const drag_gate_def_t *g, uint8_t units, char *buf, size_t cap)` gains `units`: for `DRAG_SPEED_FROM0` in mph it prints `0-<round(a / 1.609344)>`; `SPEED_RANGE` and `BRAKE` print their raw km/h numbers in both units (they are not user-defined; documented in §11.1 of the design spec as km/h gates). Round-trip exactness `round(round(m×1.609344)/1.609344) == m` holds for every integer m in 1..300 (verified; no `.5` ties occur), so no per-gate display value is stored. Callers (`ui.c` `row_from_gate`) pass `s_model.units`.
- Tests (`test/test_drag_cfg.c`): mph benches {60,100} → gates 1–2 `a` = 97/161, gates 3–4 default; labels in mph read `0-60`/`0-100`; km/h labels unchanged; empty mph list keeps defaults; a round-trip sweep 1..300.

## 4. Ring mirror framed in place (DRAM reclaim)

`err_ring_t` becomes `{ uint8_t ver; err_entry_t entry[32]; uint8_t head; uint16_t crc; }` packed = 388 B — byte-identical to what `blob_wrap(LT_RING_VER, …)` writes today, so the on-flash `ring` blob needs no migration. Two assert-free helpers under the blob lock: `ring_seal()` sets `ver = LT_RING_VER` and `crc = ses_crc16(&s_ring, 386)`; `ring_check()` verifies size/version/CRC and returns the same −1/−2/−3/−4 codes `load_framed` does. `errlog_persist`/`lt_errlog_clear` write `&s_ring` directly with `nvs_set_blob`; `lt_nvs_init` reads it directly and resets on failure (same reset/report semantics as today). `s_blob_scratch` shrinks to `sizeof(lt_counters_t) + BLOB_OVERHEAD` = 39 B (counters and crash log only). Net ≈ −345 B static; expected `moto_sim` remain ≈ 4.3 KB (reported). The `_Static_assert` on the ring becomes 388 with a comment naming the frame layout.

## 5. #65 — first `/api/sessions` after connect

Facts: the streaming `list` parser (`dl_parse_begin`) requires `---BEGIN` at column 0, while the small-frame parser was made `mem_find`-tolerant in 5c1fdf0 for a prompt glued to the marker with no newline (hardware-observed). A failure detected after the first chunk is already committed closes the HTTP socket mid-body, which app.js renders as "bad response".

- **Parser:** `dl_parse_begin` locates `---BEGIN ` with `mem_find` within the line (bounded by the line length) and parses from there; a host test feeds `"laptimer> ---BEGIN sessions …"` as one line (no newline before it) byte-by-byte and whole, expecting a clean parse (mirrors `test_status_link.c`'s glued case).
- **Early failure → clean error:** `do_sessions_stream` does not commit the chunked response until the first body chunk has been parsed successfully (buffer the first `LH_DL_CHUNK`); a failure before that returns `502 {"error":"<rc name>"}` with the `lh_dl` rc text, so the SPA shows the real reason; a failure after commit still closes the socket (unavoidable) but logs the rc at WARN with "post-commit".
- **SPA:** `loadSessions` retries once after 500 ms on a 502 or a fetch rejection before showing the error.
- **Residual:** the documented two-task UART race (`rx_task` vs the download loop) cannot be host-reproduced; bench item: five reconnects, first `/api/sessions` each time, expect 5/5 OK; a failure prints the rc, which decides a follow-up issue.

## 6. #67 — black-box log: binary on disk, NDJSON on download

Already in place: `.bin` on disk (pinned `logstore_rec_hdr_t`), `GET /api/log/<id>?fmt=jsonl` (default) transcodes via `logstore_rec_to_json`, the Logs tab offers `download (.jsonl)` and `raw`. Closing gaps:

- `Content-Disposition: attachment; filename="<id>.jsonl"` / `"<id>.bin"` so a browser saves a correctly named file.
- The transcoder emits the dev-kit's own append timestamp as `"rx_us"` (today read and dropped) next to the payload's `gps_us`.
- STATUS records (stored, currently skipped) are transcoded (`"rec":"status"` with the `LT_ST_OFF_*` fields the status decoder already produces).
- A format note in the Logs tab (`index.html`): "NDJSON — one JSON object per line; `rec` is `fused`, `event` or `status`; `rx_us` is the dev-kit receive time, `gps_us` the lap-timer's GPS time." and the same paragraph in `logstore_rec.h`.
- Host tests (`test_logstore_json.c`): `rx_us` present and correct; a STATUS record transcodes; existing cases unchanged.

## 7. #68 — live-monitor freshness cue

The header already shows "connected · stream idle" from `stream_age_ms` (Plan 5.6). The monitor pane does not: `appendMonitorRow` stamps `monitor.lastMsgAt`; a 1 s timer (started with the SSE, cleared with it) appends one `— stream stopped —` row when `now - lastMsgAt > 3000` while the EventSource is open, and `— stream resumed —` on the next record; `#monitor-count` reads `N records · last Ns ago`. Pure `app.js` + one CSS class for the marker rows. No C change; browser-only, bench-verified (pull DETECT, watch the pane; reconnect, see "resumed").

## 8. Tests and gates

**Host:** `test_drag_cfg` (§3 cases), `test_dl_leading_noise` (§5 glued-no-newline case), `test_logstore_json` (§6 cases); `test_blob`/`test_cfg_blob` unchanged. Both harnesses warning-free; lint 0; gcc-16 sweep clean; clean `moto_sim`, `moto_neo6m`, `PANEL=ws29v2` and dev-kit builds 0 warnings; `moto_sim` `.bss` delta and DRAM remain reported (expect ≈ +0 for §2/§3, ≈ −345 B for §4).

**Bench `dsB-d1`** (same day as `p07c-d1`/`dsA-d1`, moto_sim + dev-kit):
1. §2: `lt config set {"units":"mph"}` from the dev-kit → the LAP page 2 label flips to `mph` without a reboot; DRAG page 1 labels read `0-60`…; `lt config set {"units":"kmh"}` flips back; `dbg reset` (lap-timer console via `lt shell`) → the CUR cell clears to LAST; toggle units from the DEVICE menu too: DRAG page 1 labels and the engine agree (I1).
2. §3: in mph with benches {60,100}: DRAG page 2 show gates 1–2 as `0-60`, `0-100`; the sim's drag run (if any) fires them at 97/161 km/h (log line).
3. §4: `errlog` lists entries from before a reset after the reset (ring survives in place); boot log shows no `nvs blob ring reset`.
4. §5: disconnect/reconnect the dev-kit link five times; the Sessions tab lists sessions on the first load each time (5/5); any failure's rc goes into a follow-up.
5. §6: download a log as `.jsonl` → the browser saves `log_000000NN.jsonl`; first lines show `rec`, `rx_us`, `gps_us`; a `status` line appears.
6. §7: pull DETECT → the monitor shows `— stream stopped —` within ~3 s; replug → `— stream resumed —`.

Tag `dsB-d1`; close #86 #87 #65 #67 #68; note the DRAM reclaim in the roadmap.

## 9. Out of scope / follow-ups

Dev-kit UART two-task race (bench-decided); VBO/NMEA black-box export (needs FIX records on the stream — a future stream-contract change); mph for range/brake gates (not user-defined today).

## 10. Implementation notes

Written after all seven tasks landed (code head `35b45e1` plus the fix-wave commits), from the execution ledger (`.superpowers/sdd/2026-10-03-debt-sweep-b/progress.md`) — every ruling made along the way, the measured DRAM cost, the minors deferred rather than fixed, and the bench spot-checks that remain.

### 10.1 Spec corrections (amendments to §6)

- The transcoder's type key is `"t"`, not `"rec"` — every bullet in §6 that reads `"rec":"fused"` / `"event"` / `"status"` should read `"t":"fused"` / `"event"` / `"status"`.
- §6's third bullet ("STATUS records (stored, currently skipped) are transcoded") was already satisfied before Task 5 started — the transcoder already emitted `"t":"status"` with the `LT_ST_OFF_*` fields. Task 5 added `rx_us` and the `Content-Disposition` filename/format note; it did not add STATUS transcoding.

### 10.2 Rulings (in ledger order)

- **B-1** (pre-flight, carried by T4): the dev-kit's first-chunk download buffer lives in `dl_sink_t` as a `static` rather than a stack local. One HTTP download at a time is already enforced by the download path's single-reader design; T4 confirmed `linkhost_download_cmd` is serialized by `s_req_mtx` (held `portMAX_DELAY`) plus a single `async_worker`, so the static carries no concurrency risk. Cost if wrong: 512 B of dev-kit DRAM (not constrained like the lap-timer).
- **B-2** (pre-flight, carried by T2): `EV_CFG_CHANGED`/`EV_LAP_RESET` take values 16/17 in the stable `EV_*` enum even though they are never logged or streamed — keeps one enum rather than a parallel ui-only one; the comment marks them ui-only. Cost if wrong: two reserved codes.
- **Task 2 ruling** (post-review): fix all three review findings outright — `stats_reset` fires when the reloaded mode differs, `drag_armed` is cleared in both ui handlers, the third menu label is rebuilt on reload — rather than deferring any of them; bugs are fixed, not parked. Cost if wrong: none.
- **Task 4 ruling** (post-review, accepted): adding `rc`/`where` to the pre-existing REMOTE 502 branch too (not only the new early-failure 502) is accepted as additive, since the SPA only ever reads the `error` field.
- **B-3** (mid-sweep): Tasks 5 and 6 are batched into one dispatch — both are small, disjoint-file dev-kit edits (webapi/logstore/index.html vs app.js/app.css) — for one review surface covering two features. Cost if wrong: a single review covers two features.

### 10.3 DRAM budget

`moto_sim`, apples-to-apples build-pair comparison (base `710bcd8` vs. post-T3): `.bss` delta **−352 B** (105225 → 104873 B); free static DRAM **3960 B → 4312 B**. This is 6 B beyond the brief's naive structural estimate of −346 B (`(388+39) − (385+388) = −346`); the two resized symbols (`s_ring` 388 B, `s_blob_scratch` 39 B) individually measure exactly as specified, so the extra 6 B is ordinary `.bss`-section alignment/layout shift from `s_ring` growing from an odd 385 B to an even 388 B, not a missed or additional reclaim. Tasks 1, 2, and 4–6 all measured `.bss` delta 0 B.

### 10.4 Known, deferred (minors)

Carried forward verbatim from the ledger; none block this sweep or the bench gate.

- Task 1 (pre-existing): `drag_cfg_t.benches_kmh`/`n_benches` ("headline benches", §11.4) has no reader in `components/app` or `core/ui` — a display consumer or removal is a later decision.
- Task 2 (polish): `pipeline_reload_cfg` declares `new_mode` mid-function.
- Task 3 (observation): `ring_seal` writes `ver`/`crc` into the mirror before `nvs_set_blob` succeeds — no RAM consumer reads them.
- Task 3 (follow-up, final review M9): the ring's byte equivalence has no host test (`err_ring_t` is file-static); bench §8.3 is the empirical check.

### 10.5 Bench spot-checks that remain (§8)

None of §8's six bench items have run yet — gate `dsB-d1` is pending (same day as `p07c-d1`/`dsA-d1`). All six remain: (1) §2 mph unit toggle + label flip + `reset-engine` clearing the CUR cell; (2) §3 mph benches firing gates 1–2 at 97/161 km/h; (3) §4 `errlog` surviving a reset with the ring framed in place; (4) §5 five reconnects, first `/api/sessions` 5/5 OK; (5) §6 a `.jsonl` download showing `rx_us`/`gps_us`/a `status` line with the right filename; (6) §7 pulling DETECT shows `— stream stopped —` within ~3 s and replugging shows `— stream resumed —`.
