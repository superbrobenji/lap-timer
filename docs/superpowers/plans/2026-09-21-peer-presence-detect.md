# Peer Presence Detect (GPIO detect line) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the lap-timer sense the dev controller via a hardware GPIO detect line and drive its `0xFF` stream on presence, and remove the dev-kit's polling heartbeat bandage — so black-box logging runs autonomously and the stream is continuous.

**Architecture:** Enable the already-written (but disabled) GPIO detect-line path in the lap-timer's `components/app/link/link.c` on `GPIO4` (active-low, internal pull-up), add a small debounce, and remove the dev-controller's `link_heartbeat` task. The lap-timer's `link_serial_present()` already gates the stream on `detect OR recent-cmd`; this just supplies a real, debounced detect signal.

**Tech Stack:** ESP-IDF v5.3.2 (C); FreeRTOS; `driver/gpio.h`. Two firmwares: lap-timer (`components/app`, strict Power-of-10) and dev controller (`devcontroller/`, pragmatic P10, no blocking linter).

**Spec:** `docs/superpowers/specs/2026-09-21-peer-presence-detect-design.md`

## Global Constraints

- **Detect pin:** lap-timer **GPIO4**, **active-low** (low = dev-kit present), **internal pull-up** enabled, polled every `LINK_POLL_MS` (20 ms) in `link_task`.
- **Debounce:** require **`LINK_DETECT_STABLE` = 3** consecutive identical reads (~60 ms) before flipping `s_detect_asserted`.
- **Keep** `link_serial_present()`'s existing `detect OR recent-cmd` fallback unchanged.
- **Pin is defined via the build system, not hardcoded in `link.c`** — `link.c` keeps its `#ifndef LINK_DETECT_GPIO / #define (-1)` default; the value `4` is supplied by `components/app/CMakeLists.txt` compile definition (board_devkit_v1's §6 connector detect pin).
- **Lap-timer changes gate on the strict P10 linter** (`tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation`) + both firmware envs (`moto_sim`, `moto_neo6m`) + `core_selftest` building clean. Dev-controller change gates on its host tests + build (no blocking linter).
- **Do NOT flash** any board without the user's "ready" (they hold BOOT). On-target verification is a flash gate run with the user present.
- Wiring (bench, interim): one jumper, lap-timer **GPIO4 → dev-controller GND** (dedicated detect wire); becomes the Plan 6 connector's detect pin.

## File Structure

- `components/app/link/link.c` — MODIFY: add debounce state + logic to `link_poll_detect()`. (The `gpio_config` in `link_start()` and the `s_detect_asserted` branch of `link_serial_present()` already exist and activate once `LINK_DETECT_GPIO >= 0`.)
- `components/app/CMakeLists.txt` — MODIFY: add `esp_driver_gpio` to `REQUIRES` and define `LINK_DETECT_GPIO=4` for the component.
- `devcontroller/main/main.c` — MODIFY: delete the `link_heartbeat` task, `LINK_HEARTBEAT_MS`, and its `xTaskCreate`.

No host-test file: `link.c` is FreeRTOS/GPIO glue not in the host harness, and the debounce is a 3-count state machine; its behavior is verified on-device in Task 3 (per spec §7). No new files.

---

### Task 1: Lap-timer — enable the debounced GPIO detect line

**Files:**
- Modify: `components/app/link/link.c` (`link_poll_detect`, ~line 108; add a `LINK_DETECT_STABLE` define near the other `LINK_*` defines ~line 53)
- Modify: `components/app/CMakeLists.txt` (`REQUIRES` list + a `target_compile_definitions`)

**Interfaces:**
- Consumes: existing `s_detect_asserted` (file-scope bool, drain-task-only), `LINK_DETECT_GPIO` (macro), `LINK_POLL_MS`, `gpio_get_level` (`driver/gpio.h`, already `#include`d under `#if LINK_DETECT_GPIO >= 0`), and `link_start()`'s existing `gpio_config` (input + `GPIO_PULLUP_ENABLE`).
- Produces: no new external symbols; `s_detect_asserted` now reflects a debounced GPIO4 read.

- [ ] **Step 1: Add the debounce interval define.** In `components/app/link/link.c`, next to `#define LINK_POLL_MS 20` (~line 53), add:

```c
#define LINK_DETECT_STABLE   3               /* consecutive 20ms polls a level must hold before it flips presence (~60 ms) */
```

- [ ] **Step 2: Replace `link_poll_detect()` with the debounced version.** Change the function (currently ~line 108) to:

```c
static void link_poll_detect(void)
{
#if LINK_DETECT_GPIO >= 0
    /* active-low: a peer on the connector pulls the detect line to GND; the pin idles high on its
     * internal pull-up, so level 0 = present. Debounce: a level must hold for LINK_DETECT_STABLE
     * consecutive polls before it flips s_detect_asserted, so a bouncy connector/jumper does not
     * flap the stream on/off. State is drain-task-only (link_task), so no synchronization. */
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
        s_detect_asserted = detect_cand;
    }
#else
    s_detect_asserted = false;   /* no detect pin assigned yet (Plan 6 hardware); heartbeat only */
#endif
}
```

- [ ] **Step 3: Wire the pin + gpio dependency in the app CMakeLists.** In `components/app/CMakeLists.txt`, add `esp_driver_gpio` to the component's `REQUIRES` (the `link.c` comment at the `LINK_DETECT_GPIO` define calls for this), and add after the `idf_component_register(...)` call:

```cmake
# §6 peer connector detect line (board_devkit_v1): GPIO4, free + internal-pull-up capable +
# non-strapping. link.c enables its (active-low) detect path once this is >= 0. See
# docs/superpowers/specs/2026-09-21-peer-presence-detect-design.md.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_DETECT_GPIO=4)
```

- [ ] **Step 4: Run the strict Power-of-10 linter.**

Run: `source tools/idf-env.sh && python tools/lint/power_of_10.py --enforce-fnptr --fail-on-violation components/app/link/link.c`
Expected: rc=0 (no new violations; `link_poll_detect` stays small, <20 lines, no new function pointers).

- [ ] **Step 5: Build both firmware envs + the self-test.**

Run: `source tools/idf-env.sh && ./build.sh moto_sim build && ./build.sh moto_neo6m build`
Expected: both reach "Project build complete" with no errors and no new warnings (confirms `LINK_DETECT_GPIO=4` compiles the detect path: `gpio_config`, `gpio_get_level`, and `esp_driver_gpio` linked). Also build `core_selftest` if the harness includes `link.c`; otherwise skip.

- [ ] **Step 6: Commit.**

```bash
git add components/app/link/link.c components/app/CMakeLists.txt
git commit -m "feat(link): enable debounced GPIO4 detect line for peer presence"
```

---

### Task 2: Dev controller — remove the heartbeat bandage

**Files:**
- Modify: `devcontroller/main/main.c` (delete `LINK_HEARTBEAT_MS`, `link_heartbeat()`, and its `xTaskCreate`; all added in commit `23cbac9`)

**Interfaces:**
- Consumes: nothing new. `stream_consumer` (drains `linkhost_stream_pop` → `logstore_append` + `webapi_stream_push`) is unchanged and keeps running; it now receives a continuous stream whenever the lap-timer detects the dev-kit.
- Produces: no external symbol changes.

- [ ] **Step 1: Delete the heartbeat task + its define.** In `devcontroller/main/main.c`, remove the block:

```c
#define LINK_HEARTBEAT_MS 1500
static void link_heartbeat(void *arg)
{
    (void)arg;
    lt_status_t st;
    for (;;) {
        (void)linkhost_status(&st);          /* result ignored: this is a keepalive */
        vTaskDelay(pdMS_TO_TICKS(LINK_HEARTBEAT_MS));
    }
}
```

- [ ] **Step 2: Delete the heartbeat task creation.** In `app_main`, remove:

```c
    if (xTaskCreate(link_heartbeat, "link_heartbeat", 4096, NULL, 4, NULL) != pdPASS)
        ESP_LOGW(TAG, "link_heartbeat task create failed");
```

Leave the `stream_consumer` task creation immediately above it intact. If `lt_status_t` / `linkhost_status` / `freertos/task.h` are now unused elsewhere in `main.c`, leave their includes (they are still used by `stream_consumer` and other code — do not prune includes without checking).

- [ ] **Step 3: Build + host tests.**

Run: `cd devcontroller && source ../tools/idf-env.sh && idf.py build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: "Project build complete" (no errors, no new warnings), and 8/8 host tests pass (unchanged — `main.c` is not in the host target).

- [ ] **Step 4: Commit.**

```bash
git add devcontroller/main/main.c
git commit -m "refactor(devcontroller): drop link_heartbeat bandage (LT drives presence now)"
```

---

### Task 3: Flash gate — on-device verification (user present, both boards)

**Files:** none (hardware verification).

**Interfaces:** consumes the Task 1 (lap-timer) + Task 2 (dev-controller) firmware.

- [ ] **Step 1: Add the detect jumper.** With power off / before flashing, wire lap-timer **GPIO4 → dev-controller GND** (a dedicated jumper, separate from the power/UART ground). Keep the existing UART1↔UART0 + power + GND wiring.

- [ ] **Step 2: Flash the lap-timer** (moto_sim). Announce the command, wait for the user's "ready" (they hold BOOT), then flash and release BOOT the instant esptool finishes.

Run: `source tools/idf-env.sh && ./build.sh moto_sim flash --yes` (port `/dev/cu.usbserial-0001`)
Expected: clean boot; `link` task up.

- [ ] **Step 3: Flash the dev controller** (one USB port — swap after the lap-timer). Announce, wait for "ready", flash, release BOOT.

Run: `cd devcontroller && idf.py -p /dev/cu.usbserial-0001 flash`
Expected: clean boot, `dev controller ready`, no `link_heartbeat` task.

- [ ] **Step 4: Verify presence-driven streaming.** With the detect jumper connected and **no browser open**, wait ~30 s, then (join AP only if the user approves, or have the user use the SPA) download a black-box log via the Logs tab and decode it (`scratchpad/decode_log.py`): it must contain records (autonomous logging works without a browser). Open the live monitor: the stream must be **steady** (no multi-second gaps).

- [ ] **Step 5: Verify unplug/replug.** Remove the detect jumper: the live monitor stops receiving new rows within ~60 ms + no new log records accrue (stream gated off). Reconnect: streaming resumes. This confirms the debounced GPIO4 detect drives the stream.

- [ ] **Step 6: Settle the events question.** In a captured log (decoder output), note whether any EVENT (`0x09`) records exist. Events now deliver continuously if the lap-timer fires them; if the decode shows fused-only, the sim isn't producing events (a separate matter, not this change).

---

## Self-Review

**Spec coverage:** §2 presence contract → Task 1 (GPIO4, active-low, pull-up) + Task 3 Step 1 (wiring). §3 lap-timer changes → Task 1 (pin define, debounce, kept fallback). §4 dev-controller changes → Task 2 (remove heartbeat; consumer unchanged). §5 behavior + §7 testing → Task 3. §6 edge cases → covered by the design (no code); §8 out-of-scope (sim events, #67 format) → Task 3 Step 6 notes events as separate. All spec sections map to a task.

**Placeholder scan:** no TBD/TODO; every code step has concrete code; the debounce, the CMake def, and the deletions are shown verbatim.

**Type consistency:** `s_detect_asserted` (bool), `LINK_DETECT_STABLE` (int macro), `detect_run`/`detect_cand` (function-static) are consistent; `LINK_DETECT_GPIO=4` matches `link.c`'s `#if LINK_DETECT_GPIO >= 0` guard; Task 2 removes exactly the symbols Task from commit `23cbac9` added.
