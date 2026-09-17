# Power of 10 deviation register

Design authority: `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` §2 (the
adversarial deviation model) and §4 (this table's format). Plan:
`docs/superpowers/plans/2026-09-17-plan-4.5-power-of-10.md`.

Every row here survived the §2 burden of proof: a spelled-out compliant alternative was
considered and concretely shown to fail (infeasible, or not worth its cost) before the site was
accepted as a deviation. "It's how it's written now" or "it's convenient" are never sufficient
justification on their own. `tools/lint/power_of_10.py` reads this table and skips a listed
`file:symbol` site for the matching rule; every other site is a finding.

This register is seeded (Session 4.5.1) with ONLY the genuinely-unavoidable IDF/FreeRTOS API
boundary sites found in the tree today, per design §3 rule 9's first bucket: the FreeRTOS/IDF
function signature itself is a function pointer, and there is no way to call that API at all
without supplying one short of not using the SDK. The codebase's OWN callbacks (the lap/drag
engine event emit, the `exp`/`ses`/`sto_list`/`cmd` callbacks, and `core_set_assert_hook`) are
deliberately NOT seeded here — design §3 rule 9's second bucket presumes those are removable and
reworks them in Session 4.5.5, registering only what survives that rework.

Rule 9 (`--enforce-fnptr`) is inert until Session 4.5.5 (plan §"Global Constraints"); this table
exists now so the linter and its register-parsing path can be exercised report-only ahead of
that.

| id | rule | file:symbol | compliant alternative considered | why rejected | benefit | reviewer |
|----|------|--------------|-----------------------------------|--------------|---------|----------|
| PD-1 | 9 | components/app/pipeline/pipeline.c:pipeline_task | Don't create the pipeline as its own pinned FreeRTOS task (e.g. run its work from an existing task's loop, or hand-roll cooperative scheduling without FreeRTOS tasks). | `xTaskCreateStaticPinnedToCore`'s `pxTaskCode` parameter (`TaskFunction_t`) is a function pointer by the FreeRTOS/IDF API contract; sharing another task's context would break the §9 core/priority pinning the pipeline needs (steady sample-rate GPS/IMU fusion on its own core), and there is no way to obtain a statically-allocated, pinned FreeRTOS task without passing IDF a task-entry function pointer. | Static (no-heap-after-init), pinned-core, prioritized task for the §9 sensor pipeline, as the task architecture requires. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
| PD-2 | 9 | components/app/logger/logger.c:logger_task | Same as PD-1: avoid a dedicated FreeRTOS task for the §13 logger (fold its work into another task's loop instead). | Same IDF API-contract reason as PD-1 (`xTaskCreateStaticPinnedToCore`'s task-entry parameter); folding logging into another task would couple its stall/backpressure behavior to that task's own timing and defeat the §17.2 supervisor's per-task stall detection. | Static, independently-supervised task for §13 session/log writes, isolated from other tasks' timing. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
| PD-3 | 9 | components/app/supervisor/sup.c:sup_task | Same as PD-1: avoid a dedicated FreeRTOS task for the §17.2 supervisor. | Same IDF API-contract reason as PD-1; the supervisor watchdog-feeds and health-checks the OTHER tasks, so it structurally cannot be folded into one of them without losing independence from the very tasks it supervises. | Static, independent watchdog/health-check task per §17.2, structurally separate from the tasks it supervises. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
| PD-4 | 9 | components/app/ui/ui.c:ui_task | Same as PD-1: avoid a dedicated FreeRTOS task for the §4.3 UI (drive rendering from another task's loop). | Same IDF API-contract reason as PD-1; the UI blocks on its input queues at its own cadence (§4.3/§20) and driving it from, say, the pipeline's loop would tie screen refresh to sensor timing and reintroduce priority coupling the task split exists to avoid. | Static, independently-prioritized task for the §4.3 UI (menu, screens, button debounce) decoupled from sensor/logging timing. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
| PD-5 | 9 | components/drivers/export_serial/export_serial.c:register_cmd | Don't use IDF's `esp_console` component for the §18 serial REPL; hand-roll a UART line reader and command dispatcher instead. | `esp_console_cmd_t.func` is `esp_console_cmd_func_t` (a function pointer) by the IDF `esp_console` API contract -- `register_cmd`'s own `func` parameter carries that same type to reach it. Hand-rolling a line-editing, help-text, argv-parsing REPL to avoid one struct field of function-pointer type would duplicate a substantial, already-vetted IDF component for no compliance benefit. | Reuses IDF's vetted `esp_console` REPL (dumb-mode line handling, per-command help, argv parsing) for the §18.1/§18.4 status/list/open/read/config/errlog/diag/delete/close/dbg commands instead of a hand-rolled parser. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
| PD-6 | 9 | components/drivers/board_devkit_v1/board.c:board_buttons_enable_isr | Don't install a GPIO edge interrupt for the buttons; poll `board_buttons_read()` from a task loop instead. | `gpio_isr_handler_add`'s `isr_handler` parameter (`gpio_isr_t`) is a function pointer by the IDF `driver/gpio` API contract; `board_buttons_enable_isr`'s own `cb` parameter exists to forward the ISR-context button edge up to `ui_buttons.c` (§4.4). Polling would add up to a full poll-period of debounce/latency jitter to every button press and cost a busy-poll task tick; there is no way to register an IDF GPIO ISR handler without supplying a function pointer. | Low-latency, debounced (§17.9 ISR-safe, `*FromISR` only) button edge detection per §4.4, without a dedicated busy-polling task. | s4.5.1 (tooling stand-up; IDF/FreeRTOS boundary, not an app-code judgment call) |
