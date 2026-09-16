# Plan 01: Core Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and test, entirely on the host, the pure-C `core/` foundation: shared types, byte writer, SPSC ring, geodesy, time base, session frame format and record codecs, JSON writer, configuration, track database, and the VBO/NMEA/JSON exporters.

**Architecture:** Every file in `components/core/` is C11 with no ESP-IDF, FreeRTOS, or `malloc`. A plain CMake project in `test/` compiles `core/` plus Unity and runs one test executable per component under `ctest` with ASan/UBSan. The same sources later compile unchanged as an ESP-IDF component.

**Tech Stack:** C11, CMake ≥ 3.16, Unity 2.6.0 (git submodule), jsmn (vendored single header), Python 3 for the track table generator. macOS host (Apple clang) or Linux (gcc).

**Spec:** `docs/superpowers/specs/2026-09-14-lap-timer-design.md` — sections referenced per task. Read §0 (conventions), §4.1 (layering rules), §5.2 (core API), §6, §12, §14, §15, §10.1–10.2, Appendix A.

**Roadmap:** `docs/superpowers/plans/2026-09-14-roadmap.md` (this is plan 1 of window A).

## Global Constraints

- `components/core/**` MUST NOT include any ESP-IDF, FreeRTOS, or driver header, and MUST NOT call `malloc`/`free`. State lives in caller-provided structs. (§4.1)
- C11. Core compiled with `-Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion`. Vendored third-party files compiled with warnings relaxed. (§17.9)
- All time values are `int64_t` microseconds unless the field name ends in `_ms` or `_s`. (§0)
- All on-flash records are little-endian, packed, versioned, CRC-protected. (§0, §12)
- Every numeric threshold is a named constant from Appendix A, defined in `components/core/include/core/consts.h`; no bare literals in logic.
- Symbol prefixes: `geo_`, `tb_`, `ses_`, `cfg_`, `trk_`, `exp_`, `ring_`, `bw_`/`br_`, `jw_`. (§0)
- Commit after every task with the message shown; run `ctest --test-dir test/build --output-on-failure` before every commit and only commit on a green run.
- Working directory for all commands: repository root `/Users/benji/projects/personal/lap-timer`.

## File structure produced by this plan

```
components/core/
  CMakeLists.txt                     ESP-IDF component registration (used by plan 03)
  include/core/core.h                core_version(), CORE_ASSERT_RET / CORE_ASSERT_VOID
  include/core/consts.h              Appendix A constants used by core
  include/core/types.h               gps_fix_t, imu_raw_t, fused_sample_t, lap_stats_t, lap_result_t, drag_*_t
  include/core/bw.h                  byte writer / reader (header-only)
  include/core/ring.h                SPSC ring (header-only)
  include/core/geo.h  geo/geo.c
  include/core/tb.h   timebase/tb.c
  include/core/ses.h  session/ses_frame.c  session/ses_records.c
  include/core/jw.h   util/jw.c           minimal JSON writer
  include/core/jsmn.h util/jsmn.c         vendored JSON tokenizer
  include/core/cfg.h  config/cfg.c  config/cfg_json.c
  include/core/trk.h  tracks/trk.c  tracks/trk_json.c  tracks/trk_bundled.c (generated)
  include/core/exp.h  export/exp.c  export/exp_vbo.c  export/exp_nmea.c  export/exp_json.c
  util/core.c
test/
  CMakeLists.txt
  unity/                              git submodule, tag v2.6.0
  test_smoke.c test_bw.c test_ring.c test_geo.c test_tb.c test_ses_frame.c test_ses_records.c
  test_jw.c test_cfg.c test_trk.c test_exp_vbo.c test_exp_nmea_json.c
tools/tracks/gen_tracks.py  tools/tracks/killarney.json  tools/tracks/zwartkops.json
test_apps/core_selftest/                on-target self-test project (Task 14)
docs/hardware/bom.md                    prototype bill of materials (Task 15)
```

---

### Task 1: Host test harness

**Files:**
- Create: `test/CMakeLists.txt`, `test/test_smoke.c`
- Create: `components/core/include/core/core.h`, `components/core/util/core.c`, `components/core/CMakeLists.txt`
- Create: `test/unity` (git submodule)
- Modify: `.gitignore` (add `test/build/`)

**Interfaces:**
- Produces: CMake function `add_core_test(<name>)` that builds `test/<name>.c` against `core` + `unity` and registers it with ctest. Every later task adds one line `add_core_test(test_x)`.
- Produces: `const char *core_version(void);`

- [ ] **Step 1: Add Unity as a submodule pinned to v2.6.0**

```bash
git submodule add https://github.com/ThrowTheSwitch/Unity.git test/unity
git -C test/unity checkout v2.6.0
git add .gitmodules test/unity
```

- [ ] **Step 2: Write the smoke test**

`test/test_smoke.c`:
```c
#include "unity.h"
#include "core/core.h"
#include <stddef.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_version_string(void)
{
    TEST_ASSERT_EQUAL_STRING("0.0.1", core_version());
}

/* ---- core assertions (spec §17.9) ---- */

static uint16_t    seen_code;
static const char *seen_file;
static int         seen_line, seen_calls;
static void record_hook(uint16_t code, const char *file, int line)
{
    seen_code = code; seen_file = file; seen_line = line; seen_calls++;
}

static int guarded_ret(int ok)   { CORE_ASSERT_RET(ok, 0x0A99, -7); return 0; }
static int void_calls;
static void guarded_void(int ok) { CORE_ASSERT_VOID(ok, 0x0A98); void_calls++; }

static void test_assert_hook_records_the_code_on_a_forced_failure(void)
{
    seen_calls = 0; void_calls = 0;
    core_set_assert_hook(record_hook);

    TEST_ASSERT_EQUAL_INT(0, guarded_ret(1));            /* a passing check stays silent */
    TEST_ASSERT_EQUAL_INT(0, seen_calls);

    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));           /* failing: reports and returns the value */
    TEST_ASSERT_EQUAL_INT(1, seen_calls);
    TEST_ASSERT_EQUAL_HEX16(0x0A99, seen_code);
    TEST_ASSERT_NOT_NULL(seen_file);
    TEST_ASSERT_GREATER_THAN(0, seen_line);

    guarded_void(1); TEST_ASSERT_EQUAL_INT(1, void_calls);
    guarded_void(0); TEST_ASSERT_EQUAL_INT(1, void_calls);   /* returned before the body */
    TEST_ASSERT_EQUAL_INT(2, seen_calls);
    TEST_ASSERT_EQUAL_HEX16(0x0A98, seen_code);

    core_set_assert_hook(NULL);                          /* NULL = silent, never a null call */
    TEST_ASSERT_EQUAL_INT(-7, guarded_ret(0));
    TEST_ASSERT_EQUAL_INT(2, seen_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string);
    RUN_TEST(test_assert_hook_records_the_code_on_a_forced_failure);
    return UNITY_END();
}
```

- [ ] **Step 3: Write the CMake harness**

`test/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
project(laptimer_host C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# One strict flag set for core, tools and tests (spec §17.9, §21.4).
set(LAPTIMER_STRICT_FLAGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/core)
file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)

add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC ${CORE_DIR}/include)
target_compile_options(core PRIVATE ${LAPTIMER_STRICT_FLAGS})
# vendored third-party sources get relaxed warnings (file added in Task 8)
set_source_files_properties(${CORE_DIR}/util/jsmn.c PROPERTIES COMPILE_OPTIONS "-Wno-conversion;-Wno-sign-conversion;-Wno-unused-function")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # -fno-sanitize-recover makes a UBSan finding abort the test run instead of printing and continuing,
  # so a ctest pass really means no undefined behaviour was executed.
  target_compile_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  target_link_options(core PUBLIC -fsanitize=address,undefined -fno-sanitize-recover=undefined)
endif()

add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)

enable_testing()
find_package(Threads REQUIRED)
function(add_core_test name)
  add_executable(${name} ${name}.c)
  target_compile_options(${name} PRIVATE ${LAPTIMER_STRICT_FLAGS})
  target_link_libraries(${name} PRIVATE core unity m Threads::Threads)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# One executable per test/test_*.c (CONFIGURE_DEPENDS re-globs on every build, so a new suite needs no
# edit here). test_lap is held back: its exit-criterion case drives the host-only synth fixture, so it
# links the replay library instead of core/unity/m directly (replaylib already carries those).
file(GLOB CORE_TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${CORE_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  if(NOT name STREQUAL "test_lap")
    add_core_test(${name})
  endif()
endforeach()

# Host-only tools and their tests (spec §21.4, §22.2)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tools/replay ${CMAKE_CURRENT_BINARY_DIR}/tools/replay)

# test_lap links replaylib (which brings in core and libm), plus unity. The synth-driven case is guarded
# by #ifndef ESP_PLATFORM, so the same file still builds and runs on the ESP32 (core_selftest), where
# the replay tools do not exist.
add_executable(test_lap test_lap.c)
target_compile_options(test_lap PRIVATE ${LAPTIMER_STRICT_FLAGS})
target_link_libraries(test_lap PRIVATE replaylib unity Threads::Threads)
add_test(NAME test_lap COMMAND test_lap)
```

- [ ] **Step 4: Run the build to verify it fails (no core sources yet)**

Run: `cmake -S test -B test/build && cmake --build test/build`
Expected: FAIL — `No SOURCES given to target: core` or `core/core.h: No such file`.

- [ ] **Step 5: Write the minimal core**

`components/core/include/core/core.h`:
```c
#ifndef CORE_CORE_H
#define CORE_CORE_H
#include <stdint.h>

const char *core_version(void);

/* Core assertions (spec §17.9). A failing check reports `code` through the hook and returns an
 * error to the caller; it never aborts on target. The app installs a hook that logs the code into
 * the error ring; host tests install one that records it. NULL (the default) is silent. */
typedef void (*core_assert_hook_t)(uint16_t code, const char *file, int line);
void core_set_assert_hook(core_assert_hook_t hook);       /* NULL = silent */
void core_assert_fail(uint16_t code, const char *file, int line);

#define CORE_ASSERT_RET(cond, code, ret) do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return (ret); } } while (0)
#define CORE_ASSERT_VOID(cond, code)     do { if (!(cond)) { core_assert_fail((code), __FILE__, __LINE__); return; } } while (0)
#endif
```

`components/core/util/core.c`:
```c
#include "core/core.h"
#include <stddef.h>
#include <stdatomic.h>

const char *core_version(void) { return "0.0.1"; }

/* _Atomic with explicit acquire/release (as ring.h already does for the SPSC ring) so installing
 * the hook from core 0 while core 1 is mid-read of it is defined behaviour, not a data race. */
static _Atomic core_assert_hook_t assert_hook;

void core_set_assert_hook(core_assert_hook_t hook) { atomic_store_explicit(&assert_hook, hook, memory_order_release); }

void core_assert_fail(uint16_t code, const char *file, int line)
{
    core_assert_hook_t hook = atomic_load_explicit(&assert_hook, memory_order_acquire);
    if (hook) hook(code, file, line);
}
```

`components/core/CMakeLists.txt` (ESP-IDF component; unused by the host build but kept next to the sources):
```cmake
if(CMAKE_BUILD_EARLY_EXPANSION)
    # CONFIGURE_DEPENDS is invalid in the script-mode early-expansion pass IDF uses to pull
    # each component's REQUIRES (§ build-system "early expansion"); SRCS is unused there anyway.
    file(GLOB_RECURSE CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/*.c)
else()
    file(GLOB_RECURSE CORE_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
endif()
idf_component_register(SRCS ${CORE_SRCS} INCLUDE_DIRS include)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=float-conversion)
```

Append to `.gitignore`:
```
test/build/
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`

- [ ] **Step 7: Commit**

```bash
git add test/CMakeLists.txt test/test_smoke.c components/core .gitignore .gitmodules test/unity
git commit -m "build: host test harness with Unity and core library skeleton"
```

---

### Task 2: Shared types, constants, byte writer/reader

**Files:**
- Create: `components/core/include/core/types.h`, `components/core/include/core/consts.h`, `components/core/include/core/bw.h`
- Create: `test/test_bw.c`
- Modify: `test/CMakeLists.txt` (add `add_core_test(test_bw)`)

**Interfaces:**
- Produces: all structs in `types.h` below (used by every later task and by the HAL in plan 03).
- Produces: `bw_t`/`br_t` with `bw_init, bw_u8, bw_u16, bw_u32, bw_i16, bw_i32, bw_i64, bw_bytes, bw_len, bw_overflow` and `br_init, br_u8, br_u16, br_u32, br_i16, br_i32, br_i64, br_bytes, br_remaining, br_underflow`.
- Produces: every constant name in Appendix A that core uses.

- [ ] **Step 1: Write the failing test**

`test/test_bw.c`:
```c
#include "unity.h"
#include "core/bw.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_write_then_read_round_trip(void)
{
    uint8_t buf[32];
    bw_t w; bw_init(&w, buf, sizeof buf);
    bw_u8(&w, 0xA5); bw_u16(&w, 0x1234); bw_u32(&w, 0xDEADBEEF);
    bw_i16(&w, -2); bw_i32(&w, -100000); bw_i64(&w, -1234567890123LL);
    TEST_ASSERT_FALSE(bw_overflow(&w));
    TEST_ASSERT_EQUAL_UINT(1 + 2 + 4 + 2 + 4 + 8, bw_len(&w));
    /* little-endian on the wire */
    TEST_ASSERT_EQUAL_HEX8(0x34, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, buf[3]);

    br_t r; br_init(&r, buf, bw_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0xA5, br_u8(&r));
    TEST_ASSERT_EQUAL_HEX16(0x1234, br_u16(&r));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, br_u32(&r));
    TEST_ASSERT_EQUAL_INT16(-2, br_i16(&r));
    TEST_ASSERT_EQUAL_INT32(-100000, br_i32(&r));
    TEST_ASSERT_EQUAL_INT64(-1234567890123LL, br_i64(&r));
    TEST_ASSERT_EQUAL_UINT(0, br_remaining(&r));
    TEST_ASSERT_FALSE(br_underflow(&r));
}

static void test_overflow_and_underflow_are_flagged_not_fatal(void)
{
    uint8_t buf[3];
    bw_t w; bw_init(&w, buf, sizeof buf);
    bw_u32(&w, 1);
    TEST_ASSERT_TRUE(bw_overflow(&w));
    TEST_ASSERT_EQUAL_UINT(0, bw_len(&w));   /* nothing partially written */

    br_t r; br_init(&r, buf, 2);
    (void)br_u32(&r);
    TEST_ASSERT_TRUE(br_underflow(&r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_round_trip);
    RUN_TEST(test_overflow_and_underflow_are_flagged_not_fatal);
    return UNITY_END();
}
```

Add `add_core_test(test_bw)` to `test/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build && cmake --build test/build`
Expected: FAIL — `core/bw.h: No such file or directory`.

- [ ] **Step 3: Write the headers**

`components/core/include/core/bw.h`:
```c
#ifndef CORE_BW_H
#define CORE_BW_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Little-endian byte writer. On overflow nothing is written and the flag is set. */
typedef struct { uint8_t *p; size_t cap; size_t len; bool overflow; } bw_t;

static inline void bw_init(bw_t *w, uint8_t *buf, size_t cap) { w->p = buf; w->cap = cap; w->len = 0; w->overflow = false; }
static inline size_t bw_len(const bw_t *w) { return w->len; }
static inline bool bw_overflow(const bw_t *w) { return w->overflow; }
static inline void bw_bytes(bw_t *w, const void *src, size_t n)
{
    if (w->len + n > w->cap) { w->overflow = true; return; }
    memcpy(w->p + w->len, src, n); w->len += n;
}
static inline void bw_u8(bw_t *w, uint8_t v) { bw_bytes(w, &v, 1); }
static inline void bw_u16(bw_t *w, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bw_bytes(w, b, 2); }
static inline void bw_u32(bw_t *w, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; bw_bytes(w, b, 4); }
static inline void bw_u64(bw_t *w, uint64_t v) { bw_u32(w, (uint32_t)v); bw_u32(w, (uint32_t)(v >> 32)); }
static inline void bw_i16(bw_t *w, int16_t v) { bw_u16(w, (uint16_t)v); }
static inline void bw_i32(bw_t *w, int32_t v) { bw_u32(w, (uint32_t)v); }
static inline void bw_i64(bw_t *w, int64_t v) { bw_u64(w, (uint64_t)v); }

/* Little-endian byte reader. On underflow returns 0 and sets the flag. */
typedef struct { const uint8_t *p; size_t len; size_t pos; bool underflow; } br_t;

static inline void br_init(br_t *r, const uint8_t *buf, size_t len) { r->p = buf; r->len = len; r->pos = 0; r->underflow = false; }
static inline size_t br_remaining(const br_t *r) { return r->len - r->pos; }
static inline bool br_underflow(const br_t *r) { return r->underflow; }
static inline bool br_bytes(br_t *r, void *dst, size_t n)
{
    if (r->pos + n > r->len) { r->underflow = true; memset(dst, 0, n); return false; }
    memcpy(dst, r->p + r->pos, n); r->pos += n; return true;
}
static inline uint8_t br_u8(br_t *r) { uint8_t v; br_bytes(r, &v, 1); return v; }
static inline uint16_t br_u16(br_t *r) { uint8_t b[2]; br_bytes(r, b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }
static inline uint32_t br_u32(br_t *r) { uint8_t b[4]; br_bytes(r, b, 4); return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }
static inline uint64_t br_u64(br_t *r) { uint64_t lo = br_u32(r); uint64_t hi = br_u32(r); return lo | (hi << 32); }
static inline int16_t br_i16(br_t *r) { return (int16_t)br_u16(r); }
static inline int32_t br_i32(br_t *r) { return (int32_t)br_u32(r); }
static inline int64_t br_i64(br_t *r) { return (int64_t)br_u64(r); }
#endif
```

`components/core/include/core/types.h`:
```c
#ifndef CORE_TYPES_H
#define CORE_TYPES_H
#include <stdint.h>
#include <stdbool.h>

#define LAP_MAX_SECTORS 8
#define DRAG_MAX_GATES  16

/* GPS fix as delivered by the driver (spec §5.1) */
typedef struct {
    int64_t  gps_us;      /* UTC microseconds since Unix epoch; 0 if time invalid */
    int64_t  mono_us;     /* arrival time of the last byte of the message */
    int32_t  lat_e7;      /* degrees * 1e7 */
    int32_t  lon_e7;
    int32_t  alt_mm;      /* height above MSL */
    int32_t  gspeed_mms;  /* Doppler ground speed, mm/s */
    int32_t  head_e5;     /* heading of motion, degrees * 1e5, 0..36e6 */
    uint32_t hacc_mm;
    uint32_t sacc_mms;
    uint16_t pdop_e2;
    uint8_t  fix_type;    /* 0 none, 2 2D, 3 3D */
    uint8_t  sats;
    uint8_t  flags;       /* GPS_FLAG_* */
    uint8_t  valid;       /* set by the pipeline validity rule (§6.5) */
} gps_fix_t;
#define GPS_FLAG_FIXOK 0x01
#define GPS_FLAG_TIME  0x02
#define GPS_FLAG_DATE  0x04

typedef struct {
    int64_t mono_us;
    int16_t ax, ay, az;   /* raw LSB, ±16 g  → 2048 LSB/g */
    int16_t gx, gy, gz;   /* raw LSB, ±2000 dps → 16.4 LSB/dps */
} imu_raw_t;

typedef struct {
    int64_t mono_us;
    int64_t gps_us;       /* tb_mono_to_gps(mono_us) */
    float   g_lon, g_lat, g_comb;   /* g; +lat = right */
    float   lean_deg;               /* + = right */
    float   yaw_dps;                /* earth frame, + = left turn */
    uint8_t flags;                  /* FUS_* */
} fused_sample_t;
#define FUS_LEAN_VALID 0x01
#define FUS_ORIENT_OK  0x02
#define FUS_STILL      0x04
#define FUS_DISAGREE   0x08
#define FUS_CLAMPED    0x10
#define FUS_BIAS_STALE 0x20
#define FUS_SUSPECT    0x40

typedef struct {
    uint16_t max_speed_cms, min_speed_cms;
    int16_t  max_lean_l_cdeg, max_lean_r_cdeg;
    int16_t  max_glat_e3, max_gacc_e3, max_gbrake_e3;
} lap_stats_t;                       /* 14 bytes packed on the wire */

typedef struct {
    uint16_t    lap_no;
    int64_t     start_gps_us;
    uint32_t    time_ms;
    uint8_t     flags;               /* LAP_F_* */
    uint8_t     n_sectors;           /* number of splits = sector gates + 1 */
    uint32_t    sector_ms[LAP_MAX_SECTORS + 1];
    lap_stats_t stats;
} lap_result_t;
#define LAP_F_GPS_LOST    0x01
#define LAP_F_PIT         0x02
#define LAP_F_INCOMPLETE  0x04
#define LAP_F_OUT_LAP     0x08
#define LAP_F_INTERRUPTED 0x10
#define LAP_F_TOO_LONG    0x20
#define LAP_F_VALID       0x40

typedef struct { uint8_t gate_id; uint32_t time_ms; uint16_t speed_cms; uint32_t dist_cm; uint8_t hit; } drag_gate_res_t;
typedef struct {
    uint16_t        run_no;
    int64_t         t0_gps_us;
    uint8_t         flags;           /* DRAG_F_* */
    uint8_t         n_gates;
    uint16_t        trap_cms;
    drag_gate_res_t gates[DRAG_MAX_GATES];
} drag_result_t;
#define DRAG_F_ROLLOUT 0x01
#define DRAG_F_QUARTER 0x02          /* 1/4 mile reached */
#endif
```

`components/core/include/core/consts.h` (Appendix A subset used by core; the app adds its own):
```c
#ifndef CORE_CONSTS_H
#define CORE_CONSTS_H
#define EARTH_R_M              6371008.8
#define G_MPS2                 9.80665
#define FUSION_HZ              100
#define LEAN_ALPHA             0.98f
#define LEAN_MAX_DEG           70.0f
#define G_MAX                  3.0f
#define LEAN_DISAGREE_DPS      10.0f
#define LEAN_DISAGREE_S        5
#define LEAN_REF_MIN_SPEED_MPS 3.0f
#define LEAN_REF_TIMEOUT_S     5
#define STILL_ACC_VAR          (0.02f * 0.02f)
#define STILL_GYRO_VAR         (2.0f * 2.0f)
#define STILL_WINDOW_S         2
#define BIAS_TEMP_STALE_C      15
#define FWD_LEARN_ACC_MPS2     1.5f
#define FWD_LEARN_MIN_S        1
#define FWD_LEARN_WINDOWS      3
#define TB_WINDOW_S            30
#define TB_LOCK_FIXES          10
#define TB_PPS_DISAGREE_US     50000LL
#define TB_PPS_STALE_US        5000000LL
#define FIX_HACC_MAX_M         15
#define FIX_MIN_SATS           5
#define FIX_MAX_SPEED_MPS      139
#define FIX_MAX_JUMP_MPS       250
#define FIX_LOST_COUNT         3
#define GATE_REARM_DIST_M      50.0
#define GATE_REARM_MIN_S       2
#define GATE_HALF_WIDTH_M      15.0
#define MIN_LAP_S              20
#define MAX_LAP_S              1800
#define VENUE_RADIUS_DEFAULT_M 2000
#define VENUE_LEAVE_FACTOR     1.5
#define VENUE_LEAVE_S          60
#define VENUE_SCAN_S           5
#define LAYOUT_LEN_TOL         0.15
#define PIT_SPEED_KMH          5
#define PIT_TIME_S             10
#define PRED_TABLE_MAX         600
#define DRAG_ARM_SPEED_KMH     0.5f
#define DRAG_ARM_STILL_S       2
#define DRAG_LAUNCH_G          0.15f
#define DRAG_LAUNCH_HOLD_MS    100
#define DRAG_LAUNCH_SCAN_G     0.05f
#define DRAG_ROLLOUT_M         0.3048
#define DRAG_TIMEOUT_S         60
#define DRAG_FALSE_START_S     2
#define DRAG_BRAKE_STOP_KMH    0.5f
#define DRAG_FALSE_START_KMH   1.0f
#define DRAG_DONE_SETTLE_S     5
#define TRAP_DIST_M            20.117
#define FIX_KEYFRAME_S         5
#define SES_SYNC               0xA5
#define SES_MAX_PAYLOAD        247
#define TRK_MAX_LAYOUTS        8
#define TRK_MAX_USER           4
#define MOVING_SPEED_KMH       3
#define IMU_ACC_LSB_PER_G      2048.0f
#define IMU_GYR_LSB_PER_DPS    16.4f
#define FWD_LEARN_MAX_YAW_DPS  2.0f
#define FUS_ORIENT_MIN_G       0.5f
#define FUS_ORIENT_MAX_G       1.5f
#define FUS_REF_MAX_AGE_US     1000000LL
#endif
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: `100% tests passed` (2 tests).

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/types.h components/core/include/core/consts.h components/core/include/core/bw.h test/test_bw.c test/CMakeLists.txt
git commit -m "feat(core): shared types, constants, byte writer/reader"
```

---

### Task 3: SPSC ring buffer

**Files:**
- Create: `components/core/include/core/ring.h`, `test/test_ring.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `ring_t`; `void ring_init(ring_t*, void *storage, uint32_t item_size, uint32_t cap_pow2, bool overwrite_oldest)`; `bool ring_push(ring_t*, const void *item)` (false = dropped, only in drop-newest mode); `bool ring_pop(ring_t*, void *out)`; `uint32_t ring_count(const ring_t*)`; `uint32_t ring_dropped(const ring_t*)`.

- [ ] **Step 1: Write the failing test**

`test/test_ring.c`:
```c
#include "unity.h"
#include "core/ring.h"
#include <pthread.h>

#ifndef RING_STRESS_N
#define RING_STRESS_N 2000000ULL          /* target build overrides with -DRING_STRESS_N=20000ULL */
#endif

void setUp(void) {}
void tearDown(void) {}

typedef struct { int a; int b; } item_t;

static void test_fifo_order_and_wraparound(void)
{
    item_t storage[4]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 4, false);
    for (int i = 0; i < 3; i++) { item_t it = { i, i * 10 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }
    TEST_ASSERT_EQUAL_UINT32(3, ring_count(&r));
    item_t out;
    TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(0, out.a);
    TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(1, out.a);
    for (int i = 3; i < 6; i++) { item_t it = { i, 0 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }   /* wraps */
    TEST_ASSERT_EQUAL_UINT32(4, ring_count(&r));
    for (int i = 2; i < 6; i++) { TEST_ASSERT_TRUE(ring_pop(&r, &out)); TEST_ASSERT_EQUAL_INT(i, out.a); }
    TEST_ASSERT_FALSE(ring_pop(&r, &out));
}

static void test_drop_newest_policy_counts_drops(void)
{
    item_t storage[2]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 2, false);
    item_t it = { 1, 0 };
    TEST_ASSERT_TRUE(ring_push(&r, &it));
    it.a = 2; TEST_ASSERT_TRUE(ring_push(&r, &it));
    it.a = 3; TEST_ASSERT_FALSE(ring_push(&r, &it));
    TEST_ASSERT_EQUAL_UINT32(1, ring_dropped(&r));
    item_t out; ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(1, out.a);
}

static void test_overwrite_oldest_policy_keeps_newest(void)
{
    item_t storage[2]; ring_t r;
    ring_init(&r, storage, sizeof(item_t), 2, true);
    for (int i = 1; i <= 3; i++) { item_t it = { i, 0 }; TEST_ASSERT_TRUE(ring_push(&r, &it)); }
    TEST_ASSERT_EQUAL_UINT32(2, ring_count(&r));
    TEST_ASSERT_EQUAL_UINT32(1, ring_dropped(&r));
    item_t out;
    ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(2, out.a);
    ring_pop(&r, &out); TEST_ASSERT_EQUAL_INT(3, out.a);
}

typedef struct { uint64_t tag; uint64_t check; } stress_item_t;   /* invariant: check == ~tag */
typedef struct { ring_t *r; uint64_t n; } stress_arg_t;

static void *stress_producer(void *p)
{
    stress_arg_t *a = p;
    for (uint64_t i = 1; i <= a->n; i++) {
        stress_item_t it = { i, ~i };
        while (!ring_push(a->r, &it)) { /* drop-newest: spin until there is room */ }
    }
    return NULL;
}

/* Pops until it has seen the final tag; counts torn items and order violations. */
typedef struct { ring_t *r; uint64_t last_tag; uint64_t torn; uint64_t out_of_order; uint64_t received; uint64_t last_expected; } stress_res_t;
static void *stress_consumer(void *p)
{
    stress_res_t *res = p;
    stress_item_t it;
    for (;;) {
        if (!ring_pop(res->r, &it)) continue;
        res->received++;
        if (it.check != ~it.tag) res->torn++;
        if (it.tag <= res->last_tag) res->out_of_order++;
        res->last_tag = it.tag;
        if (it.tag == res->last_expected) break;
    }
    return NULL;
}

/* The eviction race is a data race by the letter of C11 (§ring.h), so ThreadSanitizer cannot be used
 * to police it: on a 2-slot ring TSan serialises the two threads enough that the window is never
 * entered, and it would report the deliberate seqlock-style read as a bug. The invariant is guarded
 * here instead: a non-zero drop count proves the producer really did evict under the consumer. */
static void test_concurrent_overwrite_oldest_never_returns_torn_items(void)
{
    static stress_item_t storage[2]; static ring_t r;
    ring_init(&r, storage, sizeof(stress_item_t), 2, true);
    const uint64_t N = RING_STRESS_N;
    stress_arg_t pa = { &r, N };
    stress_res_t cr = { &r, 0, 0, 0, 0, N };
    pthread_t pt, ct;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&ct, NULL, stress_consumer, &cr));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&pt, NULL, stress_producer, &pa));
    pthread_join(pt, NULL); pthread_join(ct, NULL);
    TEST_ASSERT_EQUAL_UINT64(0, cr.torn);
    TEST_ASSERT_EQUAL_UINT64(0, cr.out_of_order);
    TEST_ASSERT_EQUAL_UINT64(N, cr.last_tag);
    TEST_ASSERT_EQUAL_UINT64(N, cr.received + ring_dropped(&r));   /* every item was delivered or counted dropped */
    TEST_ASSERT_GREATER_THAN(0, ring_dropped(&r));                 /* the race window really was entered */
}

static void test_concurrent_drop_newest_delivers_everything_in_order(void)
{
    static stress_item_t storage[4]; static ring_t r;
    ring_init(&r, storage, sizeof(stress_item_t), 4, false);
    const uint64_t N = RING_STRESS_N;
    stress_arg_t pa = { &r, N };
    stress_res_t cr = { &r, 0, 0, 0, 0, N };
    pthread_t pt, ct;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&ct, NULL, stress_consumer, &cr));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&pt, NULL, stress_producer, &pa));
    pthread_join(pt, NULL); pthread_join(ct, NULL);
    TEST_ASSERT_EQUAL_UINT64(0, cr.torn);
    TEST_ASSERT_EQUAL_UINT64(0, cr.out_of_order);
    TEST_ASSERT_EQUAL_UINT64(N, cr.received);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fifo_order_and_wraparound);
    RUN_TEST(test_drop_newest_policy_counts_drops);
    RUN_TEST(test_overwrite_oldest_policy_keeps_newest);
    RUN_TEST(test_concurrent_overwrite_oldest_never_returns_torn_items);
    RUN_TEST(test_concurrent_drop_newest_delivers_everything_in_order);
    return UNITY_END();
}
```

Add `add_core_test(test_ring)` to `test/CMakeLists.txt`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/ring.h: No such file`.

- [ ] **Step 3: Implement**

`components/core/include/core/ring.h`:
```c
#ifndef CORE_RING_H
#define CORE_RING_H
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Single-producer single-consumer ring. Capacity must be a power of two.
 * Indices grow monotonically; the mask maps them to slots.
 *
 * Policies: drop-newest (push returns false when full) or overwrite-oldest
 * (push evicts the oldest item). With overwrite-oldest the producer may evict
 * the very slot the consumer is copying; the consumer detects that by publishing
 * its consumption with a compare-and-swap on tail and retries when it lost the
 * race, so a torn copy is never returned.
 * The discarded copy is a formal C11 data race (seqlock-style read of a slot being
 * overwritten); it is never observed, and the two-thread stress tests guard the invariant.
 *
 * A ring_t must not be copied or moved once either side has started using it.
 * ring_count() and ring_dropped() are approximate snapshots for diagnostics. */
typedef struct {
    uint8_t         *buf;
    uint32_t         item_size;
    uint32_t         mask;
    _Atomic uint32_t head;      /* next write index (producer) */
    _Atomic uint32_t tail;      /* next read index (consumer; producer advances it on eviction) */
    bool             overwrite;
    _Atomic uint32_t dropped;
} ring_t;

static inline void ring_init(ring_t *r, void *storage, uint32_t item_size, uint32_t cap_pow2, bool overwrite_oldest)
{
    r->buf = (uint8_t *)storage; r->item_size = item_size; r->mask = cap_pow2 - 1u;
    atomic_store(&r->head, 0u); atomic_store(&r->tail, 0u);
    r->overwrite = overwrite_oldest; atomic_store(&r->dropped, 0u);
}

static inline uint32_t ring_count(const ring_t *r)
{
    return atomic_load(&r->head) - atomic_load(&r->tail);
}

static inline uint32_t ring_dropped(const ring_t *r) { return atomic_load(&r->dropped); }

static inline bool ring_push(ring_t *r, const void *item)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t > r->mask) {                              /* full */
        if (!r->overwrite) { atomic_fetch_add(&r->dropped, 1u); return false; }
        uint32_t expected = t;
        /* Evict the oldest slot. If the consumer publishes its pop of that slot first,
         * our CAS fails, the item was delivered (not dropped), and the slot is free anyway. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &expected, t + 1u, memory_order_acq_rel, memory_order_acquire))
            atomic_fetch_add(&r->dropped, 1u);
    }
    memcpy(r->buf + (size_t)(h & r->mask) * r->item_size, item, r->item_size);
    atomic_store_explicit(&r->head, h + 1u, memory_order_release);
    return true;
}

static inline bool ring_pop(ring_t *r, void *out)
{
    for (;;) {
        uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
        uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
        if (t == h) return false;
        memcpy(out, r->buf + (size_t)(t & r->mask) * r->item_size, r->item_size);
        /* Publish only if the producer did not evict this slot while we copied it. */
        if (atomic_compare_exchange_strong_explicit(&r->tail, &t, t + 1u, memory_order_acq_rel, memory_order_acquire))
            return true;
        /* Lost the race: the copy may be torn. Retry from the new tail. */
    }
}
#endif
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: `100% tests passed` (3 test executables; `test_ring` itself runs 5 cases).

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/ring.h test/test_ring.c test/CMakeLists.txt
git commit -m "feat(core): lock-free SPSC ring with drop-newest and overwrite-oldest policies"
```

---

### Task 4: Geodesy (`core/geo`)

**Files:**
- Create: `components/core/include/core/geo.h`, `components/core/geo/geo.c`, `test/test_geo.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces (spec §5.2, §6.3, §6.4): `geo_enu_t {double x, y}`, `geo_origin_t`, `geo_origin_set`, `geo_to_enu`, `geo_dist_m`, `geo_segment_cross(a, b, p, q, double *t_out, int *dir_sign_out) -> int`, `geo_dist_point_segment`, `geo_interp_time(d, v0, v1, dt) -> tau`.

- [ ] **Step 1: Write the failing test**

`test/test_geo.c`:
```c
#include "unity.h"
#include "core/geo.h"
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

static void test_enu_100m_east_at_lat_minus34(void)
{
    geo_origin_t o; geo_origin_set(&o, -34.0, 18.5);
    double dlon_deg = 100.0 / (GEO_EARTH_R_M * cos(-34.0 * GEO_PI / 180.0)) * 180.0 / GEO_PI;
    geo_enu_t e = geo_to_enu(&o, -34.0, 18.5 + dlon_deg);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 100.0, e.x);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, e.y);
    geo_enu_t n = geo_to_enu(&o, -34.0 + 100.0 / GEO_EARTH_R_M * 180.0 / GEO_PI, 18.5);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 100.0, n.y);
}

static void test_haversine_known_distance(void)
{
    /* one degree of latitude at the equator ≈ 111.195 km */
    TEST_ASSERT_DOUBLE_WITHIN(5.0, 111195.0, geo_dist_m(0.0, 0.0, 1.0, 0.0));
}

static void test_segment_cross_forward_gives_plus_one(void)
{
    /* gate p1 = left end (west), p2 = right end (east); motion northbound */
    geo_enu_t p = { -10, 0 }, q = { 10, 0 }, a = { 1, -5 }, b = { 1, 5 };
    double t; int dir;
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a, b, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, t);
    TEST_ASSERT_EQUAL_INT(1, dir);
}

static void test_segment_cross_reverse_gives_minus_one(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 }, a = { 1, 5 }, b = { 1, -5 };
    double t; int dir;
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a, b, p, q, &t, &dir));
    TEST_ASSERT_EQUAL_INT(-1, dir);
}

static void test_segment_miss_parallel_and_touching(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 };
    double t; int dir;
    geo_enu_t a1 = { 20, -5 }, b1 = { 20, 5 };                 /* passes beside the gate */
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a1, b1, p, q, &t, &dir));
    geo_enu_t a2 = { -5, 1 }, b2 = { 5, 1 };                   /* parallel */
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a2, b2, p, q, &t, &dir));
    geo_enu_t a3 = { 0, -5 }, b3 = { 0, 0 };                   /* ends exactly on the gate: counts (t = 1) */
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a3, b3, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.0, t);
}

static void test_dist_point_segment(void)
{
    geo_enu_t p = { 0, 0 }, q = { 10, 0 };
    geo_enu_t on = { 5, 3 }, off = { 14, 3 };
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 3.0, geo_dist_point_segment(on, p, q));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(off, p, q));
}

static void test_interp_constant_speed_is_linear(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.1, geo_interp_time(2.0, 20.0, 20.0, 0.2));
}

static void test_interp_decelerating_matches_closed_form(void)
{
    /* v0 = 30, v1 = 10 over 0.2 s → a = -100. At τ = 0.1: d = 30*0.1 - 0.5*100*0.01 = 2.5 */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.1, geo_interp_time(2.5, 30.0, 10.0, 0.2));
}

static void test_interp_clamps_to_segment(void)
{
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.2, geo_interp_time(100.0, 20.0, 20.0, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, geo_interp_time(-1.0, 20.0, 20.0, 0.2));
}

static void test_segment_cross_rejects_each_out_of_range_parameter(void)
{
    geo_enu_t p = { -10, 0 }, q = { 10, 0 };
    double t = -99.0; int dir = -99;
    /* t > 1: the gate line is reached only past the end of the motion segment */
    geo_enu_t a1 = { 0, -10 }, b1 = { 0, -5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a1, b1, p, q, &t, &dir));
    /* t < 0: the gate line was already behind the start of the motion segment */
    geo_enu_t a2 = { 0, 5 }, b2 = { 0, 10 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a2, b2, p, q, &t, &dir));
    /* u < 0: the motion crosses the gate's line beyond its left end */
    geo_enu_t a3 = { -20, -5 }, b3 = { -20, 5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a3, b3, p, q, &t, &dir));
    /* u > 1: beyond the right end (also covered by the miss test above) */
    geo_enu_t a4 = { 20, -5 }, b4 = { 20, 5 };
    TEST_ASSERT_EQUAL_INT(0, geo_segment_cross(a4, b4, p, q, &t, &dir));
    TEST_ASSERT_EQUAL_DOUBLE(-99.0, t);          /* outputs untouched on a rejection */
    TEST_ASSERT_EQUAL_INT(-99, dir);
    /* and the same geometry does cross when both parameters are in range */
    geo_enu_t a5 = { 0, -5 }, b5 = { 0, 5 };
    TEST_ASSERT_EQUAL_INT(1, geo_segment_cross(a5, b5, p, q, &t, &dir));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.5, t);
}

static void test_dist_point_segment_degenerate_and_before_start(void)
{
    geo_enu_t degenerate = { 3, 4 };
    geo_enu_t origin = { 0, 0 };
    /* p == q: no direction to project onto, the distance is to the point itself */
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(origin, degenerate, degenerate));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, geo_dist_point_segment(degenerate, degenerate, degenerate));
    /* projection before p: the t < 0 clamp pins it to p */
    geo_enu_t p = { 0, 0 }, q = { 10, 0 }, before = { -4, 3 };
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 5.0, geo_dist_point_segment(before, p, q));
}

static void test_interp_time_degenerate_speeds(void)
{
    /* v0 ≈ 0 and acc ≈ 0: the vehicle is not moving, so the distance is reached no sooner than the
     * end of the interval; the fallback returns dt instead of dividing by zero (§6.4 step 4). */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, 0.0, 0.0, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, 1e-9, 1e-9, 0.2));
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.2, geo_interp_time(2.0, -1.0, -1.0, 0.2));   /* v0 not positive */
    /* a distance the deceleration can never cover makes the discriminant negative; the guard keeps
     * the result finite and clamped to dt rather than NaN */
    double tau = geo_interp_time(30.0, 10.0, 8.0, 1.0);
    TEST_ASSERT_TRUE(tau == tau);                                  /* not NaN */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.0, tau);
    tau = geo_interp_time(1.0, 1.0, 0.0, 0.1);
    TEST_ASSERT_TRUE(tau == tau);
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.1, tau);
    /* degenerate interval */
    TEST_ASSERT_DOUBLE_WITHIN(1e-12, 0.0, geo_interp_time(2.0, 20.0, 20.0, 0.0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enu_100m_east_at_lat_minus34);
    RUN_TEST(test_haversine_known_distance);
    RUN_TEST(test_segment_cross_forward_gives_plus_one);
    RUN_TEST(test_segment_cross_reverse_gives_minus_one);
    RUN_TEST(test_segment_miss_parallel_and_touching);
    RUN_TEST(test_dist_point_segment);
    RUN_TEST(test_interp_constant_speed_is_linear);
    RUN_TEST(test_interp_decelerating_matches_closed_form);
    RUN_TEST(test_interp_clamps_to_segment);
    RUN_TEST(test_segment_cross_rejects_each_out_of_range_parameter);
    RUN_TEST(test_dist_point_segment_degenerate_and_before_start);
    RUN_TEST(test_interp_time_degenerate_speeds);
    return UNITY_END();
}
```

Add `add_core_test(test_geo)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/geo.h: No such file`.

- [ ] **Step 3: Implement**

`components/core/include/core/geo.h`:
```c
#ifndef CORE_GEO_H
#define CORE_GEO_H
#include "core/consts.h"

#define GEO_PI        3.14159265358979323846
#define GEO_EARTH_R_M EARTH_R_M

typedef struct { double x, y; } geo_enu_t;                       /* metres east, north */
typedef struct { double lat0_rad, lon0_rad, cos_lat0; } geo_origin_t;

void      geo_origin_set(geo_origin_t *o, double lat_deg, double lon_deg);
geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg);
double    geo_dist_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
/* Returns 1 if segment a→b properly crosses gate p→q. t_out = fraction along a→b (0..1).
 * dir_sign_out = sign(cross(q−p, b−a)): +1 when p is the left end of the gate seen from the motion. */
int       geo_segment_cross(geo_enu_t a, geo_enu_t b, geo_enu_t p, geo_enu_t q, double *t_out, int *dir_sign_out);
double    geo_dist_point_segment(geo_enu_t x, geo_enu_t p, geo_enu_t q);
/* Time τ (0..dt) to travel distance d along a segment entered at speed v0 and left at v1 after dt,
 * assuming constant acceleration. (§6.4 step 4) */
double    geo_interp_time(double d, double v0, double v1, double dt);
#endif
```

`components/core/geo/geo.c`:
```c
#include "core/geo.h"
#include <math.h>

#define DEG2RAD (GEO_PI / 180.0)

void geo_origin_set(geo_origin_t *o, double lat_deg, double lon_deg)
{
    o->lat0_rad = lat_deg * DEG2RAD;
    o->lon0_rad = lon_deg * DEG2RAD;
    o->cos_lat0 = cos(o->lat0_rad);
}

geo_enu_t geo_to_enu(const geo_origin_t *o, double lat_deg, double lon_deg)
{
    geo_enu_t e;
    e.x = (lon_deg * DEG2RAD - o->lon0_rad) * o->cos_lat0 * GEO_EARTH_R_M;
    e.y = (lat_deg * DEG2RAD - o->lat0_rad) * GEO_EARTH_R_M;
    return e;
}

double geo_dist_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg)
{
    double p1 = lat1_deg * DEG2RAD, p2 = lat2_deg * DEG2RAD;
    double dp = p2 - p1, dl = (lon2_deg - lon1_deg) * DEG2RAD;
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2.0 * GEO_EARTH_R_M * atan2(sqrt(a), sqrt(1.0 - a));
}

static double cross2(geo_enu_t u, geo_enu_t v) { return u.x * v.y - u.y * v.x; }

int geo_segment_cross(geo_enu_t a, geo_enu_t b, geo_enu_t p, geo_enu_t q, double *t_out, int *dir_sign_out)
{
    geo_enu_t r = { b.x - a.x, b.y - a.y };
    geo_enu_t s = { q.x - p.x, q.y - p.y };
    double den = cross2(r, s);
    if (fabs(den) < 1e-9) return 0;
    geo_enu_t qp = { p.x - a.x, p.y - a.y };
    double t = cross2(qp, s) / den;
    double u = cross2(qp, r) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    *t_out = t;
    *dir_sign_out = (cross2(s, r) > 0.0) ? 1 : -1;
    return 1;
}

double geo_dist_point_segment(geo_enu_t x, geo_enu_t p, geo_enu_t q)
{
    double vx = q.x - p.x, vy = q.y - p.y;
    double wx = x.x - p.x, wy = x.y - p.y;
    double len2 = vx * vx + vy * vy;
    double t = len2 > 0.0 ? (wx * vx + wy * vy) / len2 : 0.0;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    double cx = p.x + t * vx - x.x, cy = p.y + t * vy - x.y;
    return sqrt(cx * cx + cy * cy);
}

double geo_interp_time(double d, double v0, double v1, double dt)
{
    if (dt <= 0.0) return 0.0;
    if (d <= 0.0) return 0.0;
    double acc = (v1 - v0) / dt;
    double tau;
    if (fabs(acc) < 0.01) {
        tau = (v0 > 1e-6) ? d / v0 : dt;
    } else {
        double disc = v0 * v0 + 2.0 * acc * d;
        if (disc < 0.0) disc = 0.0;
        tau = (-v0 + sqrt(disc)) / acc;
    }
    if (tau < 0.0) tau = 0.0;
    if (tau > dt) tau = dt;
    return tau;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass (4 test executables).

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/geo.h components/core/geo/geo.c test/test_geo.c test/CMakeLists.txt
git commit -m "feat(core): geodesy — ENU projection, haversine, gate crossing, constant-accel interpolation"
```

---

### Task 5: Time base (`core/tb`)

**Files:**
- Create: `components/core/include/core/tb.h`, `components/core/timebase/tb.c`, `test/test_tb.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces (spec §6.1–6.2): `int64_t tb_days_from_civil(int y, unsigned m, unsigned d)`; `int64_t tb_gps_us_from_utc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, int32_t nano)`; `tb_t`; `tb_init`; `tb_on_fix(tb_t*, int64_t fix_gps_us, int64_t arrival_mono_us, int64_t serial_time_us)`; `tb_on_pps(tb_t*, int64_t edge_mono_us, int64_t top_gps_us)`; `int64_t tb_mono_to_gps(const tb_t*, int64_t mono_us)`; `bool tb_locked(const tb_t*)`; `uint8_t tb_quality(const tb_t*)` (0 unlocked, 1 min-filter, 2 PPS).

- [ ] **Step 1: Write the failing test**

`test/test_tb.c`:
```c
#include "unity.h"
#include "core/tb.h"
#include "core/consts.h"
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

static void test_days_from_civil_known_dates(void)
{
    TEST_ASSERT_EQUAL_INT64(0, tb_days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(11017, tb_days_from_civil(2000, 3, 1));     /* leap day 2000 counted */
    TEST_ASSERT_EQUAL_INT64(20710, tb_days_from_civil(2026, 9, 14));
    TEST_ASSERT_EQUAL_INT64(19782, tb_days_from_civil(2024, 2, 29));
}

static void test_gps_us_from_utc_with_negative_nano(void)
{
    /* 2026-09-14 10:15:00 with nano = -500 → half a microsecond before the second */
    int64_t expect = (20710LL * 86400 + 10 * 3600 + 15 * 60) * 1000000LL;
    TEST_ASSERT_EQUAL_INT64(expect, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, 0));
    TEST_ASSERT_EQUAL_INT64(expect - 1, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, -500));   /* truncates toward -inf */
    TEST_ASSERT_EQUAL_INT64(expect + 1500, tb_gps_us_from_utc(2026, 9, 14, 10, 15, 0, 1500000));
}

/* deterministic LCG so the test is reproducible */
static uint32_t lcg = 12345u;
static int64_t rnd_range(int64_t lo, int64_t hi) { lcg = lcg * 1103515245u + 12345u; return lo + (int64_t)((lcg >> 8) % (uint32_t)(hi - lo + 1)); }

static void test_min_filter_rejects_one_sided_jitter(void)
{
    tb_t t; tb_init(&t);
    const int64_t true_offset = 1000000;         /* mono = gps + 1 s */
    const int64_t serial_us = 25000;             /* transmit time of the message, known */
    int64_t gps = 1789380900LL * 1000000LL;
    for (int i = 0; i < 200; i++) {              /* 40 s of fixes at 5 Hz; the 30 s window sees ~150 */
        int64_t latency = serial_us + rnd_range(5000, 95000);   /* 30..120 ms total, one-sided */
        tb_on_fix(&t, gps, gps + true_offset + latency, serial_us);
        gps += 200000;
    }
    TEST_ASSERT_TRUE(tb_locked(&t));
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
    int64_t est = (gps + true_offset) - tb_mono_to_gps(&t, gps + true_offset);
    TEST_ASSERT_INT64_WITHIN(10000, true_offset, est);   /* residual = smallest latency drawn above the serial time */
}

static void test_not_locked_before_ten_fixes(void)
{
    tb_t t; tb_init(&t);
    for (int i = 0; i < TB_LOCK_FIXES - 1; i++) tb_on_fix(&t, 1000000LL * i, 1000000LL * i + 500000, 0);
    TEST_ASSERT_FALSE(tb_locked(&t));
    tb_on_fix(&t, 1000000LL * TB_LOCK_FIXES, 1000000LL * TB_LOCK_FIXES + 500000, 0);
    TEST_ASSERT_TRUE(tb_locked(&t));
}

static void test_window_rollover_forgets_old_minimum(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 0;
    /* first 10 s: offset 1.000 s exactly */
    for (int i = 0; i < 50; i++) { tb_on_fix(&t, gps, gps + 1000000, 0); gps += 200000; }
    /* next 40 s: the clock drifted, true offset now 1.020 s */
    for (int i = 0; i < 200; i++) { tb_on_fix(&t, gps, gps + 1020000, 0); gps += 200000; }
    int64_t est = (gps + 1020000) - tb_mono_to_gps(&t, gps + 1020000);
    TEST_ASSERT_INT64_WITHIN(1000, 1020000, est);
}

static void test_pps_takes_precedence_and_falls_back_on_disagreement(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 5000000000LL;
    for (int i = 0; i < 20; i++) { tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); gps += 200000; }
    tb_on_pps(&t, gps + 1000000, gps);                 /* exact edge: offset 1.000 s */
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    TEST_ASSERT_EQUAL_INT64(gps, tb_mono_to_gps(&t, gps + 1000000));
    tb_on_pps(&t, gps + 1000000 + 200000, gps);        /* 200 ms disagreement → rejected */
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
}

static void test_pps_expires_without_edges_but_survives_filter_glitch(void)
{
    tb_t t; tb_init(&t);
    int64_t gps = 7000000000LL;
    for (int i = 0; i < 20; i++) { tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); gps += 200000; }
    tb_on_pps(&t, gps + 1000000, gps);
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* a glitched fix with an absurdly early arrival drags the min-filter 200 ms away; PPS must survive */
    tb_on_fix(&t, gps, gps + 1000000 - 160000, 0);
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* edges keep coming for 4 s: still PPS */
    for (int s = 1; s <= 4; s++) { gps += 1000000; tb_on_pps(&t, gps + 1000000, gps); tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); }
    TEST_ASSERT_EQUAL_UINT8(2, tb_quality(&t));
    /* no edge for 5.2 s of fixes: PPS expires, filter takes over */
    for (int i = 0; i < 26; i++) { gps += 200000; tb_on_fix(&t, gps, gps + 1000000 + 40000, 0); }
    TEST_ASSERT_EQUAL_UINT8(1, tb_quality(&t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil_known_dates);
    RUN_TEST(test_gps_us_from_utc_with_negative_nano);
    RUN_TEST(test_min_filter_rejects_one_sided_jitter);
    RUN_TEST(test_not_locked_before_ten_fixes);
    RUN_TEST(test_window_rollover_forgets_old_minimum);
    RUN_TEST(test_pps_takes_precedence_and_falls_back_on_disagreement);
    RUN_TEST(test_pps_expires_without_edges_but_survives_filter_glitch);
    return UNITY_END();
}
```

Add `add_core_test(test_tb)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/tb.h: No such file`.

- [ ] **Step 3: Implement**

`components/core/include/core/tb.h`:
```c
#ifndef CORE_TB_H
#define CORE_TB_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int64_t  half_min[2];          /* minimum offset seen in each half window */
    int64_t  half_start_mono[2];   /* mono_us when each half started */
    bool     half_valid[2];
    int      cur;                  /* index of the half currently being filled */
    uint32_t fixes;
    int64_t  filt_offset_us;       /* min over valid halves */
    int64_t  pps_offset_us;
    int64_t  pps_edge_mono_us;     /* mono time of the last accepted PPS edge */
    bool     pps_valid;
} tb_t;

int64_t tb_days_from_civil(int y, unsigned m, unsigned d);
int64_t tb_gps_us_from_utc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, int32_t nano);

void    tb_init(tb_t *t);
/* serial_time_us = len*10/baud of the message just received; subtracted from the arrival stamp. Also expires a PPS lock whose last edge is older than TB_PPS_STALE_US. */
void    tb_on_fix(tb_t *t, int64_t fix_gps_us, int64_t arrival_mono_us, int64_t serial_time_us);
void    tb_on_pps(tb_t *t, int64_t edge_mono_us, int64_t top_of_second_gps_us);
int64_t tb_mono_to_gps(const tb_t *t, int64_t mono_us);
bool    tb_locked(const tb_t *t);
uint8_t tb_quality(const tb_t *t);     /* 0 unlocked, 1 min-filter, 2 PPS */
#endif
```

`components/core/timebase/tb.c`:
```c
#include "core/tb.h"
#include "core/consts.h"
#include <string.h>

#define HALF_WINDOW_US ((int64_t)TB_WINDOW_S * 1000000LL / 2)

int64_t tb_days_from_civil(int y, unsigned m, unsigned d)
{
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? 0u - 3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097LL + (int64_t)doe - 719468LL;
}

int64_t tb_gps_us_from_utc(int y, unsigned m, unsigned d, unsigned hh, unsigned mm, unsigned ss, int32_t nano)
{
    int64_t secs = tb_days_from_civil(y, m, d) * 86400LL + (int64_t)hh * 3600 + (int64_t)mm * 60 + (int64_t)ss;
    int64_t us = secs * 1000000LL;
    /* floor division of nano by 1000 so negative nano rounds toward -inf */
    int64_t q = nano / 1000;
    if ((nano % 1000) < 0) q -= 1;
    return us + q;
}

void tb_init(tb_t *t) { memset(t, 0, sizeof *t); }

static void recompute(tb_t *t)
{
    int64_t m = 0; bool any = false;
    for (int i = 0; i < 2; i++) {
        if (!t->half_valid[i]) continue;
        if (!any || t->half_min[i] < m) { m = t->half_min[i]; any = true; }
    }
    if (any) t->filt_offset_us = m;
}

void tb_on_fix(tb_t *t, int64_t fix_gps_us, int64_t arrival_mono_us, int64_t serial_time_us)
{
    int64_t o = (arrival_mono_us - serial_time_us) - fix_gps_us;
    int c = t->cur;
    if (!t->half_valid[c]) {
        t->half_min[c] = o; t->half_start_mono[c] = arrival_mono_us; t->half_valid[c] = true;
    } else if (arrival_mono_us - t->half_start_mono[c] >= HALF_WINDOW_US) {
        c = 1 - c; t->cur = c;
        t->half_min[c] = o; t->half_start_mono[c] = arrival_mono_us; t->half_valid[c] = true;
    } else if (o < t->half_min[c]) {
        t->half_min[c] = o;
    }
    t->fixes++;
    recompute(t);
    /* PPS staleness: without edges the PPS offset cannot track crystal drift; fall back to the filter */
    if (t->pps_valid && arrival_mono_us - t->pps_edge_mono_us > TB_PPS_STALE_US) t->pps_valid = false;
}

void tb_on_pps(tb_t *t, int64_t edge_mono_us, int64_t top_of_second_gps_us)
{
    int64_t o = edge_mono_us - top_of_second_gps_us;
    int64_t ref = t->pps_valid ? t->pps_offset_us : t->filt_offset_us;
    bool must_check = t->pps_valid || tb_locked(t);
    if (must_check) {
        int64_t diff = o - ref;
        if (diff > TB_PPS_DISAGREE_US || diff < -TB_PPS_DISAGREE_US) { t->pps_valid = false; return; }
    }
    t->pps_offset_us = o; t->pps_valid = true; t->pps_edge_mono_us = edge_mono_us;
}

int64_t tb_mono_to_gps(const tb_t *t, int64_t mono_us)
{
    return mono_us - (t->pps_valid ? t->pps_offset_us : t->filt_offset_us);
}

bool tb_locked(const tb_t *t) { return t->fixes >= TB_LOCK_FIXES; }

uint8_t tb_quality(const tb_t *t)
{
    if (t->pps_valid) return 2;
    return tb_locked(t) ? 1 : 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/tb.h components/core/timebase/tb.c test/test_tb.c test/CMakeLists.txt
git commit -m "feat(core): time base — civil date math, min-filter mono→gps mapping, PPS lock"
```

---

### Task 6: Session frame format and resyncing reader (`core/ses` part 1)

**Files:**
- Create: `components/core/include/core/ses.h`, `components/core/session/ses_frame.c`, `test/test_ses_frame.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces (spec §12.2): `uint16_t ses_crc16(const uint8_t*, size_t)` (CRC-16/CCITT-FALSE); `int ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap)` → total bytes or −1; `ses_reader_t`, `ses_reader_init`, `ses_reader_feed(r, buf, n, cb, ctx)`, `ses_reader_flush(r, cb, ctx)` with `typedef void (*ses_frame_cb_t)(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)`; counters `frames_ok`, `frames_bad`.
- Produces: the `SES_T_*` type enum (§12.3). Task 7 adds record codecs to the same header.

- [ ] **Step 1: Write the failing test**

`test/test_ses_frame.c`:
```c
#include "unity.h"
#include "core/ses.h"
#include "core/core.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_crc16_ccitt_false_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1, ses_crc16((const uint8_t *)"123456789", 9));
}

static void test_frame_layout(void)
{
    uint8_t out[16];
    uint8_t payload[3] = { 1, 2, 3 };
    int n = ses_frame_encode(0x09, payload, 3, out, sizeof out);
    TEST_ASSERT_EQUAL_INT(3 + SES_FRAME_OVERHEAD, n);
    TEST_ASSERT_EQUAL_HEX8(SES_SYNC, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x09, out[1]);
    TEST_ASSERT_EQUAL_HEX8(3, out[2]);
    uint16_t crc = ses_crc16(out + 1, 2 + 3);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)crc, out[6]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8), out[7]);
    TEST_ASSERT_EQUAL_INT(-1, ses_frame_encode(0x09, payload, 3, out, 7));   /* too small */
}

typedef struct { int calls; uint8_t types[8]; uint8_t lens[8]; uint8_t last_payload[SES_MAX_PAYLOAD]; } cap_t;
static void cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    cap_t *c = ctx; c->types[c->calls] = type; c->lens[c->calls] = len; memcpy(c->last_payload, payload, len); c->calls++;
}

static void test_reader_decodes_two_frames_fed_byte_by_byte(void)
{
    uint8_t stream[64]; int n = 0;
    uint8_t p1[2] = { 0xA5, 0xA5 };                    /* sync byte inside payload must not confuse the reader */
    uint8_t p2[1] = { 7 };
    n += ses_frame_encode(0x02, p1, 2, stream + n, sizeof stream - (size_t)n);
    n += ses_frame_encode(0x04, p2, 1, stream + n, sizeof stream - (size_t)n);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    for (int i = 0; i < n; i++) ses_reader_feed(&r, stream + i, 1, cb, &c);
    TEST_ASSERT_EQUAL_INT(2, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x02, c.types[0]); TEST_ASSERT_EQUAL_UINT8(2, c.lens[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, c.types[1]); TEST_ASSERT_EQUAL_UINT8(1, c.lens[1]);
    TEST_ASSERT_EQUAL_UINT8(7, c.last_payload[0]);
    TEST_ASSERT_EQUAL_UINT32(2, r.frames_ok); TEST_ASSERT_EQUAL_UINT32(0, r.frames_bad);
}

static void test_reader_resyncs_after_corruption(void)
{
    uint8_t stream[64]; int n = 0;
    uint8_t p1[4] = { 1, 2, 3, 4 }; uint8_t p2[1] = { 9 };
    n += ses_frame_encode(0x03, p1, 4, stream + n, sizeof stream - (size_t)n);
    int second = n;
    n += ses_frame_encode(0x05, p2, 1, stream + n, sizeof stream - (size_t)n);
    stream[4] ^= 0xFF;                                  /* corrupt a payload byte of frame 1 */
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x05, c.types[0]);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
    (void)second;
}

static void test_reader_resync_finds_frame_starting_inside_bad_frame(void)
{
    /* garbage that looks like a frame header with a large len, immediately followed by a real frame */
    uint8_t stream[64]; int n = 0;
    stream[n++] = SES_SYNC; stream[n++] = 0x02; stream[n++] = 40;    /* claims 40 bytes; only a few follow */
    uint8_t p[1] = { 42 };
    n += ses_frame_encode(0x0B, p, 1, stream + n, sizeof stream - (size_t)n);
    /* pad so the bogus frame "completes" with wrong CRC */
    while (n < 3 + 40 + 2) stream[n++] = 0;
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
}

static void test_reader_rejects_oversize_len_without_stalling(void)
{
    uint8_t stream[8] = { SES_SYNC, 0x02, 255, 0, 0, 0, 0, 0 };
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, stream, sizeof stream, cb, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static void test_flush_recovers_frame_hidden_behind_spurious_sync_at_eof(void)
{
    uint8_t stream[16]; int n = 0;
    stream[n++] = SES_SYNC; stream[n++] = 0x99; stream[n++] = 200;    /* spurious header claiming 200 bytes */
    uint8_t p[1] = { 42 };
    n += ses_frame_encode(0x0B, p, 1, stream + n, sizeof stream - (size_t)n);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);                                  /* stuck waiting for 200 bytes */
    ses_reader_flush(&r, cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
    TEST_ASSERT_EQUAL_UINT8(0, r.state);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static void test_flush_on_truncated_frame_counts_bad_and_is_idempotent(void)
{
    uint8_t payload[4] = { 1, 2, 3, 4 };
    uint8_t frame[16]; int n = ses_frame_encode(0x03, payload, 4, frame, sizeof frame);
    ses_reader_t r; ses_reader_init(&r); cap_t c = { 0 };
    ses_reader_feed(&r, frame, (size_t)(n - 2), cb, &c);               /* CRC bytes missing */
    ses_reader_flush(&r, cb, &c);
    TEST_ASSERT_EQUAL_INT(0, c.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
    TEST_ASSERT_EQUAL_UINT8(0, r.state);
    ses_reader_flush(&r, cb, &c);                                       /* no-op when idle */
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);
}

static int      assert_calls;
static uint16_t assert_code;
static void record_assert(uint16_t code, const char *file, int line) { (void)file; (void)line; assert_calls++; assert_code = code; }

static void test_on_bad_guard_resets_the_reader_before_returning(void)
{
    /* Force the internal bounds guard in on_bad(): collected + pending must exceed sizeof(r.replay),
     * a combination normal traffic cannot reach (idx caps at 251; a prior on_bad's own output is
     * itself bounded). ses_reader_t's fields are public precisely so a whitebox test can build this
     * otherwise-unreachable state directly, the same way the surrounding "resync" tests read them. */
    ses_reader_t r; ses_reader_init(&r);
    r.state = 1;
    r.buf[0] = 0x02;
    r.buf[1] = SES_MAX_PAYLOAD;                                 /* len byte: claims the largest payload */
    r.idx = (uint16_t)(2 + SES_MAX_PAYLOAD);                    /* type+len+payload already collected: 2 CRC bytes short */
    r.need = (uint16_t)(2 + SES_MAX_PAYLOAD + 2);
    uint16_t crc = ses_crc16(r.buf, (size_t)2 + SES_MAX_PAYLOAD);
    r.replay[0] = (uint8_t)~crc; r.replay[1] = (uint8_t)(~(crc >> 8));   /* guaranteed CRC mismatch: bad frame */
    r.replay_len = sizeof r.replay;                             /* pending alone already exceeds sizeof(replay) - idx: forces the guard */
    r.replay_pos = 0;

    assert_calls = 0;
    core_set_assert_hook(record_assert);
    cap_t c = { 0 };
    ses_reader_feed(&r, NULL, 0, cb, &c);
    core_set_assert_hook(NULL);

    TEST_ASSERT_EQUAL_INT(1, assert_calls);
    TEST_ASSERT_EQUAL_HEX16(0x0A02, assert_code);
    TEST_ASSERT_EQUAL_UINT8(0, r.state);              /* the guard did not leave the reader mid-frame */
    TEST_ASSERT_EQUAL_UINT16(0, r.idx);
    TEST_ASSERT_EQUAL_UINT32(1, r.frames_bad);

    /* the reader must still recognise a normal frame after the guard trips */
    uint8_t stream[16]; uint8_t p[1] = { 42 };
    int n = ses_frame_encode(0x0B, p, 1, stream, sizeof stream);
    ses_reader_feed(&r, stream, (size_t)n, cb, &c);
    TEST_ASSERT_EQUAL_INT(1, c.calls);
    TEST_ASSERT_EQUAL_HEX8(0x0B, c.types[0]);
    TEST_ASSERT_EQUAL_UINT8(42, c.last_payload[0]);
}

/* deterministic LCG, same pattern as the other suites */
static uint32_t lcg = 22695477u;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

#define FUZZ_MAX_FRAMES 6
#define FUZZ_MAX_PAYLOAD 24
typedef struct { uint8_t type, len, first, last; } frame_sig_t;
typedef struct { int n; frame_sig_t sig[32]; } fuzz_cap_t;

static void fuzz_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    fuzz_cap_t *c = ctx;
    if (c->n < (int)(sizeof c->sig / sizeof c->sig[0])) {
        c->sig[c->n].type = type; c->sig[c->n].len = len;
        c->sig[c->n].first = len ? payload[0] : 0;
        c->sig[c->n].last = len ? payload[len - 1] : 0;
    }
    c->n++;
}

static void test_fuzz_frames_buried_in_garbage_are_all_recovered(void)
{
    for (int it = 0; it < 1000; it++) {
        uint8_t stream[512]; size_t sn = 0;
        frame_sig_t want[FUZZ_MAX_FRAMES]; int nwant = 0;
        int nframes = 1 + (int)(rnd() % FUZZ_MAX_FRAMES);
        for (int f = 0; f < nframes; f++) {
            int gap = (int)(rnd() % 13u);                       /* garbage before each frame */
            for (int g = 0; g < gap; g++) stream[sn++] = (uint8_t)(rnd() % 256u);
            uint8_t payload[FUZZ_MAX_PAYLOAD];
            uint8_t len = (uint8_t)(rnd() % (FUZZ_MAX_PAYLOAD + 1u));
            for (uint8_t i = 0; i < len; i++) payload[i] = (uint8_t)(rnd() % 256u);
            uint8_t type = (uint8_t)(1u + rnd() % 0x7Fu);
            int w = ses_frame_encode(type, payload, len, stream + sn, sizeof stream - sn);
            TEST_ASSERT_GREATER_THAN(0, w);
            sn += (size_t)w;
            want[nwant].type = type; want[nwant].len = len;
            want[nwant].first = len ? payload[0] : 0;
            want[nwant].last = len ? payload[len - 1] : 0;
            nwant++;
        }
        int tail = (int)(rnd() % 13u);                          /* trailing garbage */
        for (int g = 0; g < tail; g++) stream[sn++] = (uint8_t)(rnd() % 256u);

        ses_reader_t r; ses_reader_init(&r);
        fuzz_cap_t c; memset(&c, 0, sizeof c);
        size_t pos = 0;
        while (pos < sn) {                                      /* random chunk sizes */
            size_t chunk = 1u + rnd() % 17u;
            if (pos + chunk > sn) chunk = sn - pos;
            ses_reader_feed(&r, stream + pos, chunk, fuzz_cb, &c);
            pos += chunk;
        }
        ses_reader_flush(&r, fuzz_cb, &c);
        TEST_ASSERT_EQUAL_INT(nwant, c.n);
        for (int i = 0; i < nwant; i++) {
            TEST_ASSERT_EQUAL_HEX8(want[i].type, c.sig[i].type);
            TEST_ASSERT_EQUAL_UINT8(want[i].len, c.sig[i].len);
            TEST_ASSERT_EQUAL_HEX8(want[i].first, c.sig[i].first);
            TEST_ASSERT_EQUAL_HEX8(want[i].last, c.sig[i].last);
        }
        TEST_ASSERT_EQUAL_UINT32((uint32_t)nwant, r.frames_ok);
        TEST_ASSERT_EQUAL_UINT8(0, r.state);                    /* flush always leaves the reader idle */
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_ccitt_false_check_value);
    RUN_TEST(test_frame_layout);
    RUN_TEST(test_reader_decodes_two_frames_fed_byte_by_byte);
    RUN_TEST(test_reader_resyncs_after_corruption);
    RUN_TEST(test_reader_resync_finds_frame_starting_inside_bad_frame);
    RUN_TEST(test_reader_rejects_oversize_len_without_stalling);
    RUN_TEST(test_flush_recovers_frame_hidden_behind_spurious_sync_at_eof);
    RUN_TEST(test_flush_on_truncated_frame_counts_bad_and_is_idempotent);
    RUN_TEST(test_on_bad_guard_resets_the_reader_before_returning);
    RUN_TEST(test_fuzz_frames_buried_in_garbage_are_all_recovered);
    return UNITY_END();
}
```

Add `add_core_test(test_ses_frame)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/ses.h: No such file`.

- [ ] **Step 3: Implement**

`components/core/include/core/ses.h`:
```c
#ifndef CORE_SES_H
#define CORE_SES_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "core/types.h"
#include "core/consts.h"

/* Record types (spec §12.3) */
enum {
    SES_T_SESSION_HDR = 0x01, SES_T_FIX_KEY = 0x02, SES_T_FIX_DELTA = 0x03, SES_T_FUSED = 0x04,
    SES_T_LAP = 0x05, SES_T_SECTOR = 0x06, SES_T_DRAG_RUN = 0x07, SES_T_DRAG_GATE = 0x08,
    SES_T_EVENT = 0x09, SES_T_CALIB = 0x0A, SES_T_MARK = 0x0B, SES_T_TIME_MAP = 0x0C,
    SES_T_VENUE = 0x0D, SES_T_POWER = 0x0E, SES_T_END = 0x7F
};

#define SES_FRAME_OVERHEAD 5      /* sync + type + len + crc16 */

uint16_t ses_crc16(const uint8_t *buf, size_t n);
/* Writes sync|type|len|payload|crc16 into out. Returns bytes written, or -1 if cap is too small or len > SES_MAX_PAYLOAD. */
int      ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap);

/* Invoked once per valid frame. `payload` points into the reader's internal buffer and is valid
 * only for the duration of the callback: copy what you need before returning. */
typedef void (*ses_frame_cb_t)(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx);

typedef struct {
    uint8_t  state;                           /* 0 = hunting sync, 1 = collecting */
    uint16_t idx;                             /* bytes collected into buf */
    uint16_t need;                            /* total bytes expected in buf once len is known */
    uint8_t  buf[2 + SES_MAX_PAYLOAD + 2];    /* type, len, payload, crc */
    uint8_t  replay[2 * (2 + SES_MAX_PAYLOAD + 2)];    /* rescan buffer; proven bound is 2+247+2 bytes, kept at 2x for headroom */
    uint16_t replay_len, replay_pos;
    uint32_t frames_ok, frames_bad;
} ses_reader_t;

void ses_reader_init(ses_reader_t *r);
/* Feed any number of bytes; cb is invoked once per valid frame. Resynchronises after corruption. */
void ses_reader_feed(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx);
/* Call once at the end of a bounded input (a file). A frame that can never complete is treated as
 * bad and the bytes after its sync are rescanned, so a valid frame hidden behind a spurious sync
 * near EOF is still recovered. Idempotent when the reader is idle. */
void ses_reader_flush(ses_reader_t *r, ses_frame_cb_t cb, void *ctx);

/* Record codecs are declared in Task 7 below this line. */
#endif
```

`components/core/session/ses_frame.c`:
```c
#include "core/ses.h"
#include "core/core.h"
#include <string.h>

uint16_t ses_crc16(const uint8_t *buf, size_t n)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc = (uint16_t)(crc ^ ((uint16_t)buf[i] << 8));      /* explicit: `^=` widens to int, gcc -Wconversion */
        for (int b = 0; b < 8; b++)
            crc = (uint16_t)((crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1));   /* ?: promotes both arms to int */
    }
    return crc;
}

int ses_frame_encode(uint8_t type, const void *payload, uint8_t len, uint8_t *out, size_t cap)
{
    if (len > SES_MAX_PAYLOAD) return -1;
    size_t total = (size_t)len + SES_FRAME_OVERHEAD;
    if (cap < total) return -1;
    out[0] = SES_SYNC; out[1] = type; out[2] = len;
    if (len) memcpy(out + 3, payload, len);
    uint16_t crc = ses_crc16(out + 1, (size_t)2 + len);
    out[3 + len] = (uint8_t)crc;
    out[4 + len] = (uint8_t)(crc >> 8);
    return (int)total;
}

void ses_reader_init(ses_reader_t *r) { memset(r, 0, sizeof *r); }

/* returns 0 = continue, 1 = frame complete and valid, 2 = frame bad */
static int step(ses_reader_t *r, uint8_t b)
{
    if (r->state == 0) {
        if (b == SES_SYNC) { r->state = 1; r->idx = 0; r->need = 0; }
        return 0;
    }
    r->buf[r->idx++] = b;
    if (r->idx == 2) {
        uint8_t len = r->buf[1];
        if (len > SES_MAX_PAYLOAD) return 2;
        r->need = (uint16_t)(2 + len + 2);
    }
    if (r->need && r->idx == r->need) {
        uint8_t len = r->buf[1];
        uint16_t crc = ses_crc16(r->buf, (size_t)2 + len);
        uint16_t got = (uint16_t)(r->buf[2 + len] | (r->buf[3 + len] << 8));
        return (crc == got) ? 1 : 2;
    }
    return 0;
}

static void on_bad(ses_reader_t *r)
{
    /* Re-scan everything collected after the sync byte, plus whatever replay input was still pending. */
    uint16_t collected = r->idx;
    uint16_t pending = (uint16_t)(r->replay_len - r->replay_pos);
    /* Reset the reader state first: whether or not the bounds guard below trips, a caller must be
     * able to keep feeding bytes afterwards without the reader staying wedged mid-frame. */
    r->frames_bad++;
    r->state = 0; r->idx = 0; r->need = 0;
    /* collected <= 2+SES_MAX_PAYLOAD+2 and pending is what is left of an equally bounded replay,
     * so the sum fits the 2x-sized replay buffer. Checked rather than assumed: a corrupted reader
     * struct must not turn into a memcpy past the end. */
    CORE_ASSERT_VOID((size_t)collected + pending <= sizeof r->replay, 0x0A02);
    uint8_t tmp[sizeof r->replay];
    memcpy(tmp, r->buf, collected);
    memcpy(tmp + collected, r->replay + r->replay_pos, pending);
    r->replay_len = (uint16_t)(collected + pending);
    r->replay_pos = 0;
    memcpy(r->replay, tmp, r->replay_len);
}

void ses_reader_feed(ses_reader_t *r, const uint8_t *buf, size_t n, ses_frame_cb_t cb, void *ctx)
{
    size_t in_pos = 0;
    for (;;) {
        uint8_t b;
        if (r->replay_pos < r->replay_len) b = r->replay[r->replay_pos++];
        else if (in_pos < n) b = buf[in_pos++];
        else break;
        int st = step(r, b);
        if (st == 1) {
            r->frames_ok++;
            cb(r->buf[0], r->buf + 2, r->buf[1], ctx);
            r->state = 0; r->idx = 0; r->need = 0;
        } else if (st == 2) {
            on_bad(r);
        }
    }
    /* The loop exits only once the replay is drained and the input consumed; reset for the next call. */
    r->replay_pos = 0; r->replay_len = 0;
}

void ses_reader_flush(ses_reader_t *r, ses_frame_cb_t cb, void *ctx)
{
    /* A partial frame at EOF can never complete: discard its sync byte and rescan the rest.
     * Each round consumes at least one byte, so this terminates. */
    while (r->state == 1) {
        on_bad(r);
        ses_reader_feed(r, NULL, 0, cb, ctx);
    }
}
```

Note on `on_bad`: the first byte of `buf` (the type byte after the sync) is re-scanned along with the rest, so a real frame whose sync byte was consumed as payload of a bogus frame is found. Every restart consumes at least the sync byte, so the loop terminates.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add components/core/include/core/ses.h components/core/session/ses_frame.c test/test_ses_frame.c test/CMakeLists.txt
git commit -m "feat(core): session frame format with CRC-16 and resynchronising reader"
```

---

### Task 7: Session record codecs (`core/ses` part 2)

**Files:**
- Create: `components/core/session/ses_records.c`, `test/test_ses_records.c`
- Modify: `components/core/include/core/ses.h` (append declarations), `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ses_frame_encode`, `bw_t`/`br_t`, `types.h`.
- Produces (spec §12.3–12.4): the codecs listed in the header below. Every `ses_encode_*` returns the full frame length written into `out` (or −1); every `ses_decode_*` returns 1 on success, 0 if the type does not apply, −1 on malformed payload.

- [ ] **Step 1: Write the failing test**

`test/test_ses_records.c`:
```c
#include "unity.h"
#include "core/ses.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static gps_fix_t mk_fix(int64_t gps_us, int32_t lat, int32_t lon, int32_t alt_mm, int32_t v_mms, int32_t head_e5, uint8_t sats, uint8_t valid)
{
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = gps_us; f.lat_e7 = lat; f.lon_e7 = lon; f.alt_mm = alt_mm; f.gspeed_mms = v_mms; f.head_e5 = head_e5;
    f.hacc_mm = 2500; f.sacc_mms = 300; f.pdop_e2 = 150; f.fix_type = 3; f.sats = sats; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = valid;
    return f;
}

/* decoder side: collect fixes through the frame reader */
typedef struct { ses_fix_state_t st; gps_fix_t out[64]; int n; uint8_t types[64]; } dec_t;
static void dec_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    dec_t *d = ctx; d->types[d->n] = type;
    if (ses_decode_fix(&d->st, type, payload, len, &d->out[d->n]) == 1) d->n++;
}

static void test_fix_key_then_deltas_then_key_after_5s(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    uint8_t stream[2048]; size_t n = 0;
    int64_t t0 = 1789380900LL * 1000000LL;
    /* 5 Hz for 5.2 s = 27 fixes; moving 6 m north per fix at ~30 m/s */
    for (int i = 0; i < 27; i++) {
        gps_fix_t f = mk_fix(t0 + i * 200000LL, -338567000 + i * 540, 185170000, 45000 + i * 10, 30000, 1000, 8, 1);
        int w = ses_encode_fix(&enc, &f, stream + n, sizeof stream - n);
        TEST_ASSERT_GREATER_THAN(0, w); n += (size_t)w;
    }
    dec_t d; memset(&d, 0, sizeof d); ses_fix_state_init(&d.st);
    ses_reader_t r; ses_reader_init(&r);
    ses_reader_feed(&r, stream, n, dec_cb, &d);
    TEST_ASSERT_EQUAL_INT(27, d.n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, d.types[0]);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, d.types[1]);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, d.types[25]);       /* t = 5.0 s → keyframe */
    for (int i = 0; i < 27; i++) {
        TEST_ASSERT_EQUAL_INT64(t0 + i * 200000LL, d.out[i].gps_us);
        TEST_ASSERT_EQUAL_INT32(-338567000 + i * 540, d.out[i].lat_e7);
        TEST_ASSERT_EQUAL_INT32(185170000, d.out[i].lon_e7);
        TEST_ASSERT_INT32_WITHIN(100, 45000 + i * 10, d.out[i].alt_mm);     /* dm resolution in deltas */
        TEST_ASSERT_EQUAL_INT32(30000, d.out[i].gspeed_mms);
        TEST_ASSERT_EQUAL_INT32(1000, d.out[i].head_e5);
        TEST_ASSERT_EQUAL_UINT8(8, d.out[i].sats);
        TEST_ASSERT_EQUAL_UINT8(1, d.out[i].valid);
        TEST_ASSERT_EQUAL_UINT8(3, d.out[i].fix_type);
    }
}

static void test_fix_key_forced_on_overflow_and_after_invalid(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    uint8_t buf[64];
    gps_fix_t a = mk_fix(1000000, 0, 0, 0, 1000, 0, 6, 1);
    ses_encode_fix(&enc, &a, buf, sizeof buf);
    gps_fix_t b = mk_fix(1200000, 40000, 0, 0, 1000, 0, 6, 1);          /* dlat 40000 > int16 → KEY */
    ses_encode_fix(&enc, &b, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    gps_fix_t c = mk_fix(1400000, 40100, 0, 0, 1000, 0, 6, 0);          /* invalid fix, still logged as delta */
    ses_encode_fix(&enc, &c, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_DELTA, buf[1]);
    gps_fix_t dfix = mk_fix(1600000, 40200, 0, 0, 1000, 0, 6, 1);       /* first valid after invalid → KEY */
    ses_encode_fix(&enc, &dfix, buf, sizeof buf);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
}

static void test_fused_dt_relative_to_last_fix_then_previous_fused(void)
{
    ses_fused_state_t enc; ses_fused_state_init(&enc);
    ses_fused_state_t dec; ses_fused_state_init(&dec);
    uint8_t buf[64];
    ses_fused_state_on_fix(&enc, 5000000); ses_fused_state_on_fix(&dec, 5000000);
    fused_sample_t s1; memset(&s1, 0, sizeof s1);
    s1.gps_us = 5040000; s1.g_lat = 0.123f; s1.g_lon = -0.5f; s1.lean_deg = 12.34f; s1.yaw_dps = -5.5f; s1.flags = FUS_LEAN_VALID;
    int w = ses_encode_fused(&enc, &s1, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(11 + SES_FRAME_OVERHEAD, w);
    fused_sample_t o1; TEST_ASSERT_EQUAL_INT(1, ses_decode_fused(&dec, buf + 3, 11, &o1));
    TEST_ASSERT_EQUAL_INT64(5040000, o1.gps_us);
    TEST_ASSERT_FLOAT_WITHIN(0.0006f, 0.123f, o1.g_lat);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, 12.34f, o1.lean_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.006f, -5.5f, o1.yaw_dps);
    TEST_ASSERT_EQUAL_UINT8(FUS_LEAN_VALID, o1.flags);
    fused_sample_t s2 = s1; s2.gps_us = 5140000;
    ses_encode_fused(&enc, &s2, buf, sizeof buf);
    fused_sample_t o2; ses_decode_fused(&dec, buf + 3, 11, &o2);
    TEST_ASSERT_EQUAL_INT64(5140000, o2.gps_us);
}

static void test_lap_round_trip(void)
{
    lap_result_t lap; memset(&lap, 0, sizeof lap);
    lap.lap_no = 7; lap.start_gps_us = 123456789012LL; lap.time_ms = 112340; lap.flags = LAP_F_VALID; lap.n_sectors = 3;
    lap.sector_ms[0] = 32100; lap.sector_ms[1] = 41000; lap.sector_ms[2] = 39240;
    lap.stats.max_speed_cms = 6000; lap.stats.min_speed_cms = 1800; lap.stats.max_lean_l_cdeg = -5200; lap.stats.max_lean_r_cdeg = 5500;
    lap.stats.max_glat_e3 = 1320; lap.stats.max_gacc_e3 = 610; lap.stats.max_gbrake_e3 = -1050;
    uint8_t buf[128];
    int w = ses_encode_lap(&lap, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(30 + 4 * 3 + SES_FRAME_OVERHEAD, w);
    lap_result_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_lap(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &out));
    TEST_ASSERT_EQUAL_MEMORY(&lap, &out, sizeof lap);
}

static void test_drag_run_round_trip(void)
{
    drag_result_t run; memset(&run, 0, sizeof run);
    run.run_no = 2; run.t0_gps_us = 99; run.flags = DRAG_F_QUARTER; run.trap_cms = 8472; run.n_gates = 2;
    run.gates[0] = (drag_gate_res_t){ 2, 5910, 2778, 9800, 1 };
    run.gates[1] = (drag_gate_res_t){ 10, 14200, 8472, 40234, 1 };
    uint8_t buf[256];
    int w = ses_encode_drag_run(&run, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(14 + 12 * 2 + SES_FRAME_OVERHEAD, w);
    drag_result_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_run(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &out));
    TEST_ASSERT_EQUAL_MEMORY(&run, &out, sizeof run);
}

static void test_hdr_round_trip_and_size(void)
{
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10); h.mode = 0; h.variant = 0; h.venue_id = 6; h.layout_id = 1;
    strcpy(h.fw, "v0.1.0"); strcpy(h.hwid, "moto_neo6m_epaper"); h.log_profile = 0; h.fused_hz = 10; h.gps_hz = 5;
    h.start_gps_us = 1789380900000000LL; h.r_e4[0] = 10000; h.r_e4[4] = 10000; h.r_e4[8] = 10000; h.gbias[1] = -12; h.calib_flags = 3;
    uint8_t buf[128];
    int w = ses_encode_hdr(&h, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(94 + SES_FRAME_OVERHEAD, w);
    ses_hdr_t out; TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(buf + 3, 94, &out));
    TEST_ASSERT_EQUAL_MEMORY(&h, &out, sizeof h);
}

static void test_small_records_sizes(void)
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_INT(19 + SES_FRAME_OVERHEAD, ses_encode_sector(3, 1, 123, 32100, -210, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(21 + SES_FRAME_OVERHEAD, ses_encode_drag_gate(1, 2, 123, 5910, 2778, 9800, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(22 + SES_FRAME_OVERHEAD, ses_encode_event(1, 2, 0x0101, 7, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(17 + SES_FRAME_OVERHEAD, ses_encode_time_map(1, 2, 1, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(36 + SES_FRAME_OVERHEAD, ses_encode_venue(6, 1, "Killarney", buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(11 + SES_FRAME_OVERHEAD, ses_encode_power(1, 2, 3900, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(9 + SES_FRAME_OVERHEAD, ses_encode_end(1, 0, buf, sizeof buf));
    TEST_ASSERT_EQUAL_INT(9 + SES_FRAME_OVERHEAD, ses_encode_mark(1, 0, buf, sizeof buf));
}

static void test_negative_ground_speed_forces_a_keyframe(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    ses_fix_state_t dec; ses_fix_state_init(&dec);
    uint8_t buf[64]; gps_fix_t out;
    gps_fix_t a = mk_fix(1000000, -338567000, 185170000, 45000, 1000, 9000000, 9, 1);
    int n = ses_encode_fix(&enc, &a, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&dec, buf[1], buf + 3, buf[2], &out));

    /* a negative Doppler speed cannot be represented in the unsigned delta field */
    gps_fix_t b = a; b.gps_us += 200000; b.gspeed_mms = -1500;
    n = ses_encode_fix(&enc, &b, buf, sizeof buf);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(SES_T_FIX_KEY, buf[1]);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&dec, buf[1], buf + 3, buf[2], &out));
    TEST_ASSERT_EQUAL_INT32(-1500, out.gspeed_mms);              /* decoded exactly */
    TEST_ASSERT_EQUAL_INT32(enc.prev.gspeed_mms, out.gspeed_mms);   /* encoder and decoder agree */
    TEST_ASSERT_EQUAL_INT64(b.gps_us, out.gps_us);
    TEST_ASSERT_EQUAL_INT32(b.lat_e7, out.lat_e7);
}

static void test_hdr_strings_using_every_wire_byte_round_trip_nul_terminated(void)
{
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10);
    memcpy(h.fw, "v0.3.1-abcdefghi", 16);          /* exactly 16 bytes, no NUL on the wire */
    memcpy(h.hwid, "moto_neo6m_epaper_int_bl", 24);
    h.venue_id = 6; h.layout_id = 1; h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = 1789640100000000LL;
    uint8_t buf[128];
    int w = ses_encode_hdr(&h, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(94 + SES_FRAME_OVERHEAD, w);            /* wire payload unchanged */
    ses_hdr_t out;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(buf + 3, 94, &out));
    TEST_ASSERT_EQUAL_STRING("S00042_001", out.session_id);
    TEST_ASSERT_EQUAL_UINT(10, strlen(out.session_id));
    TEST_ASSERT_EQUAL_INT('\0', out.session_id[10]);
    TEST_ASSERT_EQUAL_STRING("v0.3.1-abcdefghi", out.fw);
    TEST_ASSERT_EQUAL_UINT(16, strlen(out.fw));
    TEST_ASSERT_EQUAL_STRING("moto_neo6m_epaper_int_bl", out.hwid);
    TEST_ASSERT_EQUAL_UINT(24, strlen(out.hwid));
    TEST_ASSERT_EQUAL_MEMORY(&h, &out, sizeof h);
}

static void test_small_record_round_trips(void)
{
    uint8_t buf[64];
    int w;

    w = ses_encode_sector(3, 1, 123456789LL, 32100, -210, buf, sizeof buf);
    ses_sector_t sec;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_sector(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &sec));
    TEST_ASSERT_EQUAL_UINT16(3, sec.lap_no); TEST_ASSERT_EQUAL_UINT8(1, sec.idx);
    TEST_ASSERT_EQUAL_INT64(123456789LL, sec.gps_us); TEST_ASSERT_EQUAL_UINT32(32100, sec.split_ms);
    TEST_ASSERT_EQUAL_INT32(-210, sec.delta_ms);

    w = ses_encode_drag_gate(1, 2, 987654321LL, 5910, 2778, 9800, buf, sizeof buf);
    ses_drag_gate_t dg;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_gate(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &dg));
    TEST_ASSERT_EQUAL_UINT16(1, dg.run_no); TEST_ASSERT_EQUAL_UINT8(2, dg.gate_id);
    TEST_ASSERT_EQUAL_INT64(987654321LL, dg.gps_us); TEST_ASSERT_EQUAL_UINT32(5910, dg.time_ms);
    TEST_ASSERT_EQUAL_UINT16(2778, dg.speed_cms); TEST_ASSERT_EQUAL_UINT32(9800, dg.dist_cm);

    w = ses_encode_event(-5, 1789380900000000LL, 0x0101, 0xDEADBEEF, buf, sizeof buf);
    ses_event_t ev;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_event(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &ev));
    TEST_ASSERT_EQUAL_INT64(-5, ev.mono_us); TEST_ASSERT_EQUAL_INT64(1789380900000000LL, ev.gps_us);
    TEST_ASSERT_EQUAL_HEX16(0x0101, ev.code); TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, ev.arg);

    w = ses_encode_time_map(4242, 1789380900000000LL, 2, buf, sizeof buf);
    ses_time_map_t tm;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_time_map(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &tm));
    TEST_ASSERT_EQUAL_INT64(4242, tm.mono_us); TEST_ASSERT_EQUAL_INT64(1789380900000000LL, tm.gps_us);
    TEST_ASSERT_EQUAL_UINT8(2, tm.quality);

    w = ses_encode_venue(6, 1, "Killarney", buf, sizeof buf);
    ses_venue_t vn;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &vn));
    TEST_ASSERT_EQUAL_UINT16(6, vn.venue_id); TEST_ASSERT_EQUAL_UINT16(1, vn.layout_id);
    TEST_ASSERT_EQUAL_STRING("Killarney", vn.name);
    /* a name filling all 32 wire bytes still decodes NUL-terminated */
    w = ses_encode_venue(9, 2, "0123456789012345678901234567890123", buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_venue(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &vn));
    TEST_ASSERT_EQUAL_UINT(31, strlen(vn.name));    /* encoder keeps a NUL inside the 32-byte field */

    w = ses_encode_power(77, 3, 3900, buf, sizeof buf);
    ses_power_t pw;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_power(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &pw));
    TEST_ASSERT_EQUAL_INT64(77, pw.mono_us); TEST_ASSERT_EQUAL_UINT8(3, pw.state); TEST_ASSERT_EQUAL_UINT16(3900, pw.batt_mv);

    w = ses_encode_end(1789380900000000LL, 2, buf, sizeof buf);
    ses_end_t en;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_end(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &en));
    TEST_ASSERT_EQUAL_INT64(1789380900000000LL, en.gps_us); TEST_ASSERT_EQUAL_UINT8(2, en.reason);

    w = ses_encode_mark(1789380900000001LL, 1, buf, sizeof buf);
    ses_mark_t mk;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_mark(buf + 3, (uint8_t)(w - SES_FRAME_OVERHEAD), &mk));
    TEST_ASSERT_EQUAL_INT64(1789380900000001LL, mk.gps_us); TEST_ASSERT_EQUAL_UINT8(1, mk.kind);
}

static void test_calib_round_trip(void)
{
    ses_calib_t c; memset(&c, 0, sizeof c);
    for (int i = 0; i < 9; i++) c.r_e4[i] = (int16_t)(i * 1000 - 4000);
    c.gbias[0] = -12; c.gbias[1] = 340; c.gbias[2] = 0;
    c.calib_flags = 0x03;
    uint8_t buf[64];
    int w = ses_encode_calib(&c, buf, sizeof buf);
    TEST_ASSERT_EQUAL_INT(25 + SES_FRAME_OVERHEAD, w);
    TEST_ASSERT_EQUAL_HEX8(SES_T_CALIB, buf[1]);
    ses_calib_t out; memset(&out, 0xAA, sizeof out);
    TEST_ASSERT_EQUAL_INT(1, ses_decode_calib(buf + 3, 25, &out));
    for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(c.r_e4[i], out.r_e4[i]);
    for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT16(c.gbias[i], out.gbias[i]);
    TEST_ASSERT_EQUAL_HEX8(c.calib_flags, out.calib_flags);
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(buf + 3, 24, &out));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(buf + 3, 26, &out));
}

static void test_decoders_reject_malformed_payloads(void)
{
    uint8_t p[256]; memset(p, 0, sizeof p);
    gps_fix_t f; lap_result_t lap; drag_result_t run; ses_hdr_t hdr; fused_sample_t fs;
    ses_fix_state_t fst; ses_fix_state_init(&fst);
    ses_fused_state_t ust; ses_fused_state_init(&ust);

    /* FIX_KEY / FIX_DELTA: exact lengths only */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 38, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 40, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 15, &f));   /* DELTA before any KEY */
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fix(&fst, SES_T_FIX_KEY, p, 39, &f));      /* now a reference exists */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 14, &f));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fix(&fst, SES_T_FIX_DELTA, p, 16, &f));
    TEST_ASSERT_EQUAL_INT(0, ses_decode_fix(&fst, SES_T_LAP, p, 39, &f));          /* not a fix record */

    /* FUSED: exact length and a reference */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 11, &fs));                 /* no reference yet */
    ses_fused_state_on_fix(&ust, 1000000);
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 10, &fs));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_fused(&ust, p, 12, &fs));
    TEST_ASSERT_EQUAL_INT(1, ses_decode_fused(&ust, p, 11, &fs));

    /* LAP: short header, declared sector count, declared-vs-actual length */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, 29, &lap));
    p[15] = LAP_MAX_SECTORS + 2;                                                   /* n_sectors field */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, (uint8_t)(30 + 4 * (LAP_MAX_SECTORS + 2)), &lap));
    p[15] = 3;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_lap(p, 30, &lap));                        /* length does not match n_sectors */
    TEST_ASSERT_EQUAL_INT(1, ses_decode_lap(p, 42, &lap));
    p[15] = 0;

    /* DRAG_RUN: short header, declared gate count, declared-vs-actual length */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, 13, &run));
    p[13] = DRAG_MAX_GATES + 1;                                                    /* n_gates field */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, (uint8_t)(14 + 12 * 1), &run));
    p[13] = 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_run(p, 14, &run));
    TEST_ASSERT_EQUAL_INT(1, ses_decode_drag_run(p, 38, &run));
    p[13] = 0;

    /* SESSION_HDR: exact length and version 1 */
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 93, &hdr));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 95, &hdr));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 94, &hdr));                        /* version byte is 0 */
    p[0] = 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_hdr(p, 94, &hdr));
    p[0] = 1;
    TEST_ASSERT_EQUAL_INT(1, ses_decode_hdr(p, 94, &hdr));

    /* encoders refuse counts they cannot frame */
    uint8_t out[512];
    memset(&lap, 0, sizeof lap); lap.n_sectors = LAP_MAX_SECTORS + 2;
    TEST_ASSERT_EQUAL_INT(-1, ses_encode_lap(&lap, out, sizeof out));
    lap.n_sectors = LAP_MAX_SECTORS + 1;
    TEST_ASSERT_GREATER_THAN(0, ses_encode_lap(&lap, out, sizeof out));
    memset(&run, 0, sizeof run); run.n_gates = DRAG_MAX_GATES + 1;
    TEST_ASSERT_EQUAL_INT(-1, ses_encode_drag_run(&run, out, sizeof out));
    run.n_gates = DRAG_MAX_GATES;
    TEST_ASSERT_GREATER_THAN(0, ses_encode_drag_run(&run, out, sizeof out));

    /* the small-record decoders all validate their length exactly */
    ses_sector_t sec; ses_drag_gate_t dg; ses_event_t ev; ses_time_map_t tm;
    ses_venue_t vn; ses_power_t pw; ses_end_t en; ses_mark_t mk; ses_calib_t cal;
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_sector(p, 18, &sec));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_drag_gate(p, 20, &dg));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_event(p, 21, &ev));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_time_map(p, 18, &tm));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_venue(p, 35, &vn));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_power(p, 12, &pw));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_end(p, 8, &en));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_mark(p, 10, &mk));
    TEST_ASSERT_EQUAL_INT(-1, ses_decode_calib(p, 24, &cal));
}

/* deterministic LCG, same pattern as the other suites */
static uint32_t lcg = 1664525u;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }
static int32_t rnd_span(int32_t span) { return (int32_t)(rnd() % (uint32_t)(2 * span + 1)) - span; }

typedef struct { ses_fix_state_t st; gps_fix_t last; int n; } walk_dec_t;
static void walk_cb(uint8_t type, const uint8_t *payload, uint8_t len, void *ctx)
{
    walk_dec_t *d = ctx;
    if (ses_decode_fix(&d->st, type, payload, len, &d->last) == 1) d->n++;
}

static void test_fuzz_ten_thousand_fix_random_walk_round_trips(void)
{
    ses_fix_state_t enc; ses_fix_state_init(&enc);
    walk_dec_t d; memset(&d, 0, sizeof d); ses_fix_state_init(&d.st);
    ses_reader_t r; ses_reader_init(&r);

    gps_fix_t f = mk_fix(1789380900LL * 1000000LL, -338567000, 185170000, 45000, 30000, 9012000, 9, 1);
    for (int i = 0; i < 10000; i++) {
        f.gps_us += (int64_t)(100 + rnd() % 200u) * 1000;       /* 100..299 ms, whole milliseconds */
        f.lat_e7 += rnd_span(3000);
        f.lon_e7 += rnd_span(3000);
        f.alt_mm += rnd_span(2000);
        f.gspeed_mms = (int32_t)(rnd() % 60001u);
        f.head_e5 = (int32_t)(rnd() % 36000001u);
        f.hacc_mm = 1000 + rnd() % 5000u;
        f.sats = (uint8_t)(4 + rnd() % 16u);
        f.valid = (uint8_t)((rnd() % 32u) != 0);                 /* the occasional invalid fix */
        uint8_t frame[64];
        int w = ses_encode_fix(&enc, &f, frame, sizeof frame);
        TEST_ASSERT_GREATER_THAN(0, w);
        int before = d.n;
        ses_reader_feed(&r, frame, (size_t)w, walk_cb, &d);
        TEST_ASSERT_EQUAL_INT(before + 1, d.n);
        TEST_ASSERT_EQUAL_INT64(f.gps_us, d.last.gps_us);
        TEST_ASSERT_EQUAL_INT32(f.lat_e7, d.last.lat_e7);
        TEST_ASSERT_EQUAL_INT32(f.lon_e7, d.last.lon_e7);
        TEST_ASSERT_EQUAL_UINT8(f.sats, d.last.sats);
        TEST_ASSERT_EQUAL_UINT8(f.valid, d.last.valid);
        TEST_ASSERT_INT32_WITHIN(50, f.alt_mm, d.last.alt_mm);          /* decimetre field */
        TEST_ASSERT_INT32_WITHIN(5, f.gspeed_mms, d.last.gspeed_mms);   /* cm/s field */
    }
    TEST_ASSERT_EQUAL_INT(10000, d.n);
    TEST_ASSERT_EQUAL_UINT32(10000, r.frames_ok);
    TEST_ASSERT_EQUAL_UINT32(0, r.frames_bad);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fix_key_then_deltas_then_key_after_5s);
    RUN_TEST(test_fix_key_forced_on_overflow_and_after_invalid);
    RUN_TEST(test_fused_dt_relative_to_last_fix_then_previous_fused);
    RUN_TEST(test_lap_round_trip);
    RUN_TEST(test_drag_run_round_trip);
    RUN_TEST(test_hdr_round_trip_and_size);
    RUN_TEST(test_small_records_sizes);
    RUN_TEST(test_negative_ground_speed_forces_a_keyframe);
    RUN_TEST(test_hdr_strings_using_every_wire_byte_round_trip_nul_terminated);
    RUN_TEST(test_small_record_round_trips);
    RUN_TEST(test_calib_round_trip);
    RUN_TEST(test_decoders_reject_malformed_payloads);
    RUN_TEST(test_fuzz_ten_thousand_fix_random_walk_round_trips);
    return UNITY_END();
}
```

Add `add_core_test(test_ses_records)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — implicit declaration of `ses_fix_state_init` etc.

- [ ] **Step 3: Append declarations to `core/ses.h`**

Insert before the final `#endif` of `components/core/include/core/ses.h`:
```c
/* ---- Record codecs (spec §12.3, §12.4) ---- */

typedef struct {
    bool      have_prev;
    gps_fix_t prev;               /* reconstructed previous fix (what a decoder holds) */
    int64_t   last_key_gps_us;
    bool      prev_valid;
} ses_fix_state_t;
void ses_fix_state_init(ses_fix_state_t *st);
/* Chooses FIX_KEY or FIX_DELTA. Returns frame length or -1. */
int  ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *fix, uint8_t *out, size_t cap);
/* Returns 1 and fills out for FIX_KEY/FIX_DELTA; 0 for other types; -1 on malformed. */
int  ses_decode_fix(ses_fix_state_t *st, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out);

typedef struct { int64_t ref_gps_us; bool have_ref; } ses_fused_state_t;
void ses_fused_state_init(ses_fused_state_t *st);
void ses_fused_state_on_fix(ses_fused_state_t *st, int64_t fix_gps_us);   /* call on both sides when a FIX_* passes */
int  ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap);
int  ses_decode_fused(ses_fused_state_t *st, const uint8_t *payload, uint8_t len, fused_sample_t *out);

int  ses_encode_lap(const lap_result_t *lap, uint8_t *out, size_t cap);
int  ses_decode_lap(const uint8_t *payload, uint8_t len, lap_result_t *out);

/* Every decoder below validates `len` exactly and returns 1 on success, -1 on a malformed payload.
 * The structs mirror the wire payloads of §12.3; char arrays carry one extra byte so the decoded
 * value is always NUL-terminated (the wire size is unchanged). */
typedef struct { uint16_t lap_no; uint8_t idx; int64_t gps_us; uint32_t split_ms; int32_t delta_ms; } ses_sector_t;
typedef struct { uint16_t run_no; uint8_t gate_id; int64_t gps_us; uint32_t time_ms; uint16_t speed_cms; uint32_t dist_cm; } ses_drag_gate_t;
typedef struct { int64_t mono_us, gps_us; uint16_t code; uint32_t arg; } ses_event_t;
typedef struct { int64_t mono_us, gps_us; uint8_t quality; } ses_time_map_t;
typedef struct { uint16_t venue_id, layout_id; char name[33]; } ses_venue_t;     /* name is char[32] on the wire */
typedef struct { int64_t mono_us; uint8_t state; uint16_t batt_mv; } ses_power_t;
typedef struct { int64_t gps_us; uint8_t reason; } ses_end_t;
typedef struct { int64_t gps_us; uint8_t kind; } ses_mark_t;
typedef struct { int16_t r_e4[9]; int16_t gbias[3]; uint8_t calib_flags; } ses_calib_t;   /* 25 B payload */

int  ses_encode_sector(uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms, uint8_t *out, size_t cap);
int  ses_decode_sector(const uint8_t *payload, uint8_t len, ses_sector_t *out);
int  ses_encode_drag_run(const drag_result_t *run, uint8_t *out, size_t cap);
int  ses_decode_drag_run(const uint8_t *payload, uint8_t len, drag_result_t *out);
int  ses_encode_drag_gate(uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm, uint8_t *out, size_t cap);
int  ses_decode_drag_gate(const uint8_t *payload, uint8_t len, ses_drag_gate_t *out);
int  ses_encode_event(int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg, uint8_t *out, size_t cap);
int  ses_decode_event(const uint8_t *payload, uint8_t len, ses_event_t *out);
int  ses_encode_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality, uint8_t *out, size_t cap);
int  ses_decode_time_map(const uint8_t *payload, uint8_t len, ses_time_map_t *out);
int  ses_encode_venue(uint16_t venue_id, uint16_t layout_id, const char *name, uint8_t *out, size_t cap);
int  ses_decode_venue(const uint8_t *payload, uint8_t len, ses_venue_t *out);
int  ses_encode_power(int64_t mono_us, uint8_t state, uint16_t batt_mv, uint8_t *out, size_t cap);
int  ses_decode_power(const uint8_t *payload, uint8_t len, ses_power_t *out);
int  ses_encode_end(int64_t gps_us, uint8_t reason, uint8_t *out, size_t cap);
int  ses_decode_end(const uint8_t *payload, uint8_t len, ses_end_t *out);
int  ses_encode_mark(int64_t gps_us, uint8_t kind, uint8_t *out, size_t cap);
int  ses_decode_mark(const uint8_t *payload, uint8_t len, ses_mark_t *out);
int  ses_encode_calib(const ses_calib_t *c, uint8_t *out, size_t cap);
int  ses_decode_calib(const uint8_t *payload, uint8_t len, ses_calib_t *out);

typedef struct {
    char     session_id[11];      /* char[10] on the wire + NUL */
    uint8_t  mode, variant;
    uint16_t venue_id, layout_id;
    char     fw[17];              /* char[16] on the wire + NUL */
    char     hwid[25];            /* char[24] on the wire + NUL */
    uint8_t  log_profile, fused_hz, gps_hz;
    int64_t  start_gps_us;
    int16_t  r_e4[9];             /* rotation matrix × 1e4, row-major */
    int16_t  gbias[3];
    uint8_t  calib_flags;
} ses_hdr_t;
int  ses_encode_hdr(const ses_hdr_t *h, uint8_t *out, size_t cap);
int  ses_decode_hdr(const uint8_t *payload, uint8_t len, ses_hdr_t *out);
```

- [ ] **Step 4: Implement `ses_records.c`**

`components/core/session/ses_records.c`:
```c
#include "core/ses.h"
#include "core/bw.h"
#include <string.h>
#include <math.h>

#define KEYFRAME_US ((int64_t)FIX_KEYFRAME_S * 1000000LL)

static int finish(uint8_t type, bw_t *w, uint8_t *out, size_t cap)
{
    if (bw_overflow(w)) return -1;
    return ses_frame_encode(type, w->p, (uint8_t)bw_len(w), out, cap);
}

static int64_t round_div(int64_t a, int64_t b)          /* round-to-nearest for positive b */
{
    return (a >= 0) ? (a + b / 2) / b : -((-a + b / 2) / b);
}

static int16_t clamp_i16(int64_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static uint16_t clamp_u16(int64_t v)
{
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return (uint16_t)v;
}

static uint8_t clamp_u8(int64_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* ---------------- fix ---------------- */

void ses_fix_state_init(ses_fix_state_t *st) { memset(st, 0, sizeof *st); }

static uint8_t fix_flags(const gps_fix_t *f)
{
    uint8_t fl = 0;
    if (f->valid) fl |= 0x01;
    if (f->flags & GPS_FLAG_FIXOK) fl |= 0x02;
    if (f->fix_type == 3) fl |= 0x04;
    return fl;
}

static int encode_key(ses_fix_state_t *st, const gps_fix_t *f, uint8_t *out, size_t cap)
{
    uint8_t p[39]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, f->gps_us); bw_i32(&w, f->lat_e7); bw_i32(&w, f->lon_e7); bw_i32(&w, f->alt_mm);
    bw_i32(&w, f->gspeed_mms); bw_i32(&w, f->head_e5); bw_u32(&w, f->hacc_mm); bw_u16(&w, clamp_u16(f->sacc_mms));
    bw_u16(&w, f->pdop_e2); bw_u8(&w, f->fix_type); bw_u8(&w, f->sats); bw_u8(&w, fix_flags(f));
    st->prev = *f; st->have_prev = true; st->last_key_gps_us = f->gps_us; st->prev_valid = f->valid != 0;
    return finish(SES_T_FIX_KEY, &w, out, cap);
}

int ses_encode_fix(ses_fix_state_t *st, const gps_fix_t *f, uint8_t *out, size_t cap)
{
    if (!st->have_prev || !st->prev_valid || f->gps_us - st->last_key_gps_us >= KEYFRAME_US)
        return encode_key(st, f, out, cap);
    int64_t dt = round_div(f->gps_us - st->prev.gps_us, 1000);
    int64_t dlat = (int64_t)f->lat_e7 - st->prev.lat_e7;
    int64_t dlon = (int64_t)f->lon_e7 - st->prev.lon_e7;
    int64_t dalt = round_div((int64_t)f->alt_mm - st->prev.alt_mm, 100);
    int64_t v_cms = round_div(f->gspeed_mms, 10);
    if (dt < 0 || dt > 65535 || dlat > 32767 || dlat < -32768 || dlon > 32767 || dlon < -32768 ||
        dalt > 32767 || dalt < -32768 || v_cms > 65535 || v_cms < 0)
        return encode_key(st, f, out, cap);
    uint8_t p[15]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, (uint16_t)dt); bw_i16(&w, (int16_t)dlat); bw_i16(&w, (int16_t)dlon); bw_i16(&w, (int16_t)dalt);
    bw_u16(&w, (uint16_t)v_cms); bw_u16(&w, clamp_u16(round_div(f->head_e5, 1000)));
    bw_u8(&w, clamp_u8(round_div(f->hacc_mm, 100))); bw_u8(&w, f->sats); bw_u8(&w, fix_flags(f));
    /* advance the reconstructed previous exactly as the decoder will */
    st->prev.gps_us += dt * 1000; st->prev.lat_e7 = f->lat_e7; st->prev.lon_e7 = f->lon_e7;
    st->prev.alt_mm += (int32_t)(dalt * 100); st->prev.gspeed_mms = (int32_t)(v_cms * 10);
    st->prev.head_e5 = (int32_t)(round_div(f->head_e5, 1000) * 1000); st->prev.sats = f->sats;
    st->prev.hacc_mm = (uint32_t)(clamp_u8(round_div(f->hacc_mm, 100)) * 100u);
    st->prev.valid = f->valid; st->prev_valid = f->valid != 0;
    return finish(SES_T_FIX_DELTA, &w, out, cap);
}

int ses_decode_fix(ses_fix_state_t *st, uint8_t type, const uint8_t *payload, uint8_t len, gps_fix_t *out)
{
    br_t r; br_init(&r, payload, len);
    if (type == SES_T_FIX_KEY) {
        if (len != 39) return -1;
        memset(out, 0, sizeof *out);
        out->gps_us = br_i64(&r); out->lat_e7 = br_i32(&r); out->lon_e7 = br_i32(&r); out->alt_mm = br_i32(&r);
        out->gspeed_mms = br_i32(&r); out->head_e5 = br_i32(&r); out->hacc_mm = br_u32(&r); out->sacc_mms = br_u16(&r);
        out->pdop_e2 = br_u16(&r); out->fix_type = br_u8(&r); out->sats = br_u8(&r);
        uint8_t fl = br_u8(&r);
        out->valid = fl & 0x01; out->flags = (uint8_t)(((fl & 0x02) ? GPS_FLAG_FIXOK : 0) | GPS_FLAG_TIME | GPS_FLAG_DATE);
        st->prev = *out; st->have_prev = true; st->last_key_gps_us = out->gps_us; st->prev_valid = out->valid != 0;
        return 1;
    }
    if (type == SES_T_FIX_DELTA) {
        if (len != 15 || !st->have_prev) return -1;
        uint16_t dt = br_u16(&r); int16_t dlat = br_i16(&r); int16_t dlon = br_i16(&r); int16_t dalt = br_i16(&r);
        uint16_t v = br_u16(&r); uint16_t head = br_u16(&r); uint8_t hacc = br_u8(&r); uint8_t sats = br_u8(&r); uint8_t fl = br_u8(&r);
        gps_fix_t *p = &st->prev;
        p->gps_us += (int64_t)dt * 1000; p->lat_e7 += dlat; p->lon_e7 += dlon; p->alt_mm += (int32_t)dalt * 100;
        p->gspeed_mms = (int32_t)v * 10; p->head_e5 = (int32_t)head * 1000; p->hacc_mm = (uint32_t)hacc * 100u; p->sats = sats;
        p->sacc_mms = 0; p->pdop_e2 = 0;
        p->fix_type = (fl & 0x04) ? 3 : 2; p->valid = fl & 0x01;
        p->flags = (uint8_t)(((fl & 0x02) ? GPS_FLAG_FIXOK : 0) | GPS_FLAG_TIME | GPS_FLAG_DATE);
        st->prev_valid = p->valid != 0;
        *out = *p;
        return 1;
    }
    return 0;
}

/* ---------------- fused ---------------- */

void ses_fused_state_init(ses_fused_state_t *st) { memset(st, 0, sizeof *st); }
void ses_fused_state_on_fix(ses_fused_state_t *st, int64_t fix_gps_us) { st->ref_gps_us = fix_gps_us; st->have_ref = true; }

int ses_encode_fused(ses_fused_state_t *st, const fused_sample_t *fs, uint8_t *out, size_t cap)
{
    if (!st->have_ref) return -1;
    int64_t dt = round_div(fs->gps_us - st->ref_gps_us, 1000);
    /* dt is unsigned on the wire (§12.3). A fused sample stamped before its reference (a fix that
     * arrived late, or a time-base step) is clamped to 0 rather than dropped: the sample still
     * carries usable lean/g values, and both sides advance ref_gps_us by the same clamped dt, so
     * encoder and decoder stay in step. The cost is that such a sample is timed at the reference. */
    if (dt < 0) dt = 0;
    if (dt > 65535) dt = 65535;
    uint8_t p[11]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, (uint16_t)dt);
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->g_lat * 1000.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->g_lon * 1000.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->lean_deg * 100.0f)));
    bw_i16(&w, clamp_i16((int64_t)lroundf(fs->yaw_dps * 100.0f)));
    bw_u8(&w, fs->flags);
    st->ref_gps_us += dt * 1000;
    return finish(SES_T_FUSED, &w, out, cap);
}

int ses_decode_fused(ses_fused_state_t *st, const uint8_t *payload, uint8_t len, fused_sample_t *out)
{
    if (len != 11 || !st->have_ref) return -1;
    br_t r; br_init(&r, payload, len);
    uint16_t dt = br_u16(&r);
    memset(out, 0, sizeof *out);
    st->ref_gps_us += (int64_t)dt * 1000;
    out->gps_us = st->ref_gps_us;
    out->g_lat = (float)br_i16(&r) / 1000.0f;
    out->g_lon = (float)br_i16(&r) / 1000.0f;
    out->lean_deg = (float)br_i16(&r) / 100.0f;
    out->yaw_dps = (float)br_i16(&r) / 100.0f;
    out->flags = br_u8(&r);
    out->g_comb = sqrtf(out->g_lat * out->g_lat + out->g_lon * out->g_lon);
    return 1;
}

/* ---------------- lap / sector ---------------- */

static void put_stats(bw_t *w, const lap_stats_t *s)
{
    bw_u16(w, s->max_speed_cms); bw_u16(w, s->min_speed_cms); bw_i16(w, s->max_lean_l_cdeg); bw_i16(w, s->max_lean_r_cdeg);
    bw_i16(w, s->max_glat_e3); bw_i16(w, s->max_gacc_e3); bw_i16(w, s->max_gbrake_e3);
}
static void get_stats(br_t *r, lap_stats_t *s)
{
    s->max_speed_cms = br_u16(r); s->min_speed_cms = br_u16(r); s->max_lean_l_cdeg = br_i16(r); s->max_lean_r_cdeg = br_i16(r);
    s->max_glat_e3 = br_i16(r); s->max_gacc_e3 = br_i16(r); s->max_gbrake_e3 = br_i16(r);
}

int ses_encode_lap(const lap_result_t *lap, uint8_t *out, size_t cap)
{
    if (lap->n_sectors > LAP_MAX_SECTORS + 1) return -1;
    uint8_t p[30 + 4 * (LAP_MAX_SECTORS + 1)]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, lap->lap_no); bw_i64(&w, lap->start_gps_us); bw_u32(&w, lap->time_ms); bw_u8(&w, lap->flags); bw_u8(&w, lap->n_sectors);
    for (uint8_t i = 0; i < lap->n_sectors; i++) bw_u32(&w, lap->sector_ms[i]);
    put_stats(&w, &lap->stats);
    return finish(SES_T_LAP, &w, out, cap);
}

int ses_decode_lap(const uint8_t *payload, uint8_t len, lap_result_t *out)
{
    if (len < 30) return -1;
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->lap_no = br_u16(&r); out->start_gps_us = br_i64(&r); out->time_ms = br_u32(&r); out->flags = br_u8(&r); out->n_sectors = br_u8(&r);
    if (out->n_sectors > LAP_MAX_SECTORS + 1 || len != 30 + 4 * out->n_sectors) return -1;
    for (uint8_t i = 0; i < out->n_sectors; i++) out->sector_ms[i] = br_u32(&r);
    get_stats(&r, &out->stats);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_sector(uint16_t lap_no, uint8_t idx, int64_t gps_us, uint32_t split_ms, int32_t delta_ms, uint8_t *out, size_t cap)
{
    uint8_t p[19]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, lap_no); bw_u8(&w, idx); bw_i64(&w, gps_us); bw_u32(&w, split_ms); bw_i32(&w, delta_ms);
    return finish(SES_T_SECTOR, &w, out, cap);
}

int ses_decode_sector(const uint8_t *payload, uint8_t len, ses_sector_t *out)
{
    if (len != 19) return -1;
    br_t r; br_init(&r, payload, len);
    out->lap_no = br_u16(&r); out->idx = br_u8(&r); out->gps_us = br_i64(&r);
    out->split_ms = br_u32(&r); out->delta_ms = br_i32(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- drag ---------------- */

int ses_encode_drag_run(const drag_result_t *run, uint8_t *out, size_t cap)
{
    if (run->n_gates > DRAG_MAX_GATES) return -1;
    uint8_t p[14 + 12 * DRAG_MAX_GATES]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, run->run_no); bw_i64(&w, run->t0_gps_us); bw_u8(&w, run->flags); bw_u16(&w, run->trap_cms); bw_u8(&w, run->n_gates);
    for (uint8_t i = 0; i < run->n_gates; i++) {
        const drag_gate_res_t *g = &run->gates[i];
        bw_u8(&w, g->gate_id); bw_u32(&w, g->time_ms); bw_u16(&w, g->speed_cms); bw_u32(&w, g->dist_cm); bw_u8(&w, g->hit);
    }
    return finish(SES_T_DRAG_RUN, &w, out, cap);
}

int ses_decode_drag_run(const uint8_t *payload, uint8_t len, drag_result_t *out)
{
    if (len < 14) return -1;
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->run_no = br_u16(&r); out->t0_gps_us = br_i64(&r); out->flags = br_u8(&r); out->trap_cms = br_u16(&r); out->n_gates = br_u8(&r);
    if (out->n_gates > DRAG_MAX_GATES || len != 14 + 12 * out->n_gates) return -1;
    for (uint8_t i = 0; i < out->n_gates; i++) {
        drag_gate_res_t *g = &out->gates[i];
        g->gate_id = br_u8(&r); g->time_ms = br_u32(&r); g->speed_cms = br_u16(&r); g->dist_cm = br_u32(&r); g->hit = br_u8(&r);
    }
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_drag_gate(uint16_t run_no, uint8_t gate_id, int64_t gps_us, uint32_t time_ms, uint16_t speed_cms, uint32_t dist_cm, uint8_t *out, size_t cap)
{
    uint8_t p[21]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u16(&w, run_no); bw_u8(&w, gate_id); bw_i64(&w, gps_us); bw_u32(&w, time_ms); bw_u16(&w, speed_cms); bw_u32(&w, dist_cm);
    return finish(SES_T_DRAG_GATE, &w, out, cap);
}

int ses_decode_drag_gate(const uint8_t *payload, uint8_t len, ses_drag_gate_t *out)
{
    if (len != 21) return -1;
    br_t r; br_init(&r, payload, len);
    out->run_no = br_u16(&r); out->gate_id = br_u8(&r); out->gps_us = br_i64(&r);
    out->time_ms = br_u32(&r); out->speed_cms = br_u16(&r); out->dist_cm = br_u32(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- misc ---------------- */

int ses_encode_event(int64_t mono_us, int64_t gps_us, uint16_t code, uint32_t arg, uint8_t *out, size_t cap)
{
    uint8_t p[22]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_i64(&w, gps_us); bw_u16(&w, code); bw_u32(&w, arg);
    return finish(SES_T_EVENT, &w, out, cap);
}

int ses_decode_event(const uint8_t *payload, uint8_t len, ses_event_t *out)
{
    if (len != 22) return -1;
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->gps_us = br_i64(&r); out->code = br_u16(&r); out->arg = br_u32(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_time_map(int64_t mono_us, int64_t gps_us, uint8_t quality, uint8_t *out, size_t cap)
{
    uint8_t p[17]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_i64(&w, gps_us); bw_u8(&w, quality);
    return finish(SES_T_TIME_MAP, &w, out, cap);
}

int ses_decode_time_map(const uint8_t *payload, uint8_t len, ses_time_map_t *out)
{
    if (len != 17) return -1;
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->gps_us = br_i64(&r); out->quality = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_venue(uint16_t venue_id, uint16_t layout_id, const char *name, uint8_t *out, size_t cap)
{
    uint8_t p[36]; bw_t w; bw_init(&w, p, sizeof p);
    char nm[32]; memset(nm, 0, sizeof nm); strncpy(nm, name, sizeof nm - 1);
    bw_u16(&w, venue_id); bw_u16(&w, layout_id); bw_bytes(&w, nm, sizeof nm);
    return finish(SES_T_VENUE, &w, out, cap);
}

int ses_decode_venue(const uint8_t *payload, uint8_t len, ses_venue_t *out)
{
    if (len != 36) return -1;
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    out->venue_id = br_u16(&r); out->layout_id = br_u16(&r);
    br_bytes(&r, out->name, 32); out->name[32] = '\0';
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_power(int64_t mono_us, uint8_t state, uint16_t batt_mv, uint8_t *out, size_t cap)
{
    uint8_t p[11]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, mono_us); bw_u8(&w, state); bw_u16(&w, batt_mv);
    return finish(SES_T_POWER, &w, out, cap);
}

int ses_decode_power(const uint8_t *payload, uint8_t len, ses_power_t *out)
{
    if (len != 11) return -1;
    br_t r; br_init(&r, payload, len);
    out->mono_us = br_i64(&r); out->state = br_u8(&r); out->batt_mv = br_u16(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_end(int64_t gps_us, uint8_t reason, uint8_t *out, size_t cap)
{
    uint8_t p[9]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, gps_us); bw_u8(&w, reason);
    return finish(SES_T_END, &w, out, cap);
}

int ses_decode_end(const uint8_t *payload, uint8_t len, ses_end_t *out)
{
    if (len != 9) return -1;
    br_t r; br_init(&r, payload, len);
    out->gps_us = br_i64(&r); out->reason = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

int ses_encode_mark(int64_t gps_us, uint8_t kind, uint8_t *out, size_t cap)
{
    uint8_t p[9]; bw_t w; bw_init(&w, p, sizeof p);
    bw_i64(&w, gps_us); bw_u8(&w, kind);
    return finish(SES_T_MARK, &w, out, cap);
}

int ses_decode_mark(const uint8_t *payload, uint8_t len, ses_mark_t *out)
{
    if (len != 9) return -1;
    br_t r; br_init(&r, payload, len);
    out->gps_us = br_i64(&r); out->kind = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- calibration ---------------- */

int ses_encode_calib(const ses_calib_t *c, uint8_t *out, size_t cap)
{
    uint8_t p[25]; bw_t w; bw_init(&w, p, sizeof p);
    for (int i = 0; i < 9; i++) bw_i16(&w, c->r_e4[i]);
    for (int i = 0; i < 3; i++) bw_i16(&w, c->gbias[i]);
    bw_u8(&w, c->calib_flags);
    return finish(SES_T_CALIB, &w, out, cap);
}

int ses_decode_calib(const uint8_t *payload, uint8_t len, ses_calib_t *out)
{
    if (len != 25) return -1;
    br_t r; br_init(&r, payload, len);
    for (int i = 0; i < 9; i++) out->r_e4[i] = br_i16(&r);
    for (int i = 0; i < 3; i++) out->gbias[i] = br_i16(&r);
    out->calib_flags = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}

/* ---------------- header ---------------- */

int ses_encode_hdr(const ses_hdr_t *h, uint8_t *out, size_t cap)
{
    uint8_t p[94]; bw_t w; bw_init(&w, p, sizeof p);
    bw_u8(&w, 1);                                   /* ver */
    bw_u8(&w, 0);                                   /* reserved, keeps payload at the documented 94 bytes */
    bw_bytes(&w, h->session_id, 10); bw_u8(&w, h->mode); bw_u8(&w, h->variant);
    bw_u16(&w, h->venue_id); bw_u16(&w, h->layout_id); bw_bytes(&w, h->fw, 16); bw_bytes(&w, h->hwid, 24);
    bw_u8(&w, h->log_profile); bw_u8(&w, h->fused_hz); bw_u8(&w, h->gps_hz); bw_i64(&w, h->start_gps_us);
    for (int i = 0; i < 9; i++) bw_i16(&w, h->r_e4[i]);
    for (int i = 0; i < 3; i++) bw_i16(&w, h->gbias[i]);
    bw_u8(&w, h->calib_flags);
    return finish(SES_T_SESSION_HDR, &w, out, cap);
}

int ses_decode_hdr(const uint8_t *payload, uint8_t len, ses_hdr_t *out)
{
    if (len != 94) return -1;
    br_t r; br_init(&r, payload, len);
    memset(out, 0, sizeof *out);
    if (br_u8(&r) != 1) return -1;
    br_u8(&r);                                      /* reserved, currently unused */
    br_bytes(&r, out->session_id, 10); out->session_id[10] = '\0'; out->mode = br_u8(&r); out->variant = br_u8(&r);
    out->venue_id = br_u16(&r); out->layout_id = br_u16(&r);
    br_bytes(&r, out->fw, 16); out->fw[16] = '\0';          /* the wire field may use all 16 bytes */
    br_bytes(&r, out->hwid, 24); out->hwid[24] = '\0';
    out->log_profile = br_u8(&r); out->fused_hz = br_u8(&r); out->gps_hz = br_u8(&r); out->start_gps_us = br_i64(&r);
    for (int i = 0; i < 9; i++) out->r_e4[i] = br_i16(&r);
    for (int i = 0; i < 3; i++) out->gbias[i] = br_i16(&r);
    out->calib_flags = br_u8(&r);
    return br_underflow(&r) ? -1 : 1;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass. If `TEST_ASSERT_EQUAL_MEMORY` on `lap_result_t` fails because of struct padding garbage, the test already `memset`s both sides; keep the `memset` and re-check field order.

- [ ] **Step 6: Commit**

```bash
git add components/core/include/core/ses.h components/core/session/ses_records.c test/test_ses_records.c test/CMakeLists.txt
git commit -m "feat(core): session record codecs — fix key/delta, fused, lap, drag, header, misc"
```

---

### Task 8: JSON writer and vendored tokenizer (`core/jw`, `core/jsmn`)

**Files:**
- Create: `components/core/include/core/jw.h`, `components/core/util/jw.c`
- Create: `components/core/include/core/jsmn.h` (vendored), `components/core/include/core/json.h`, `components/core/util/jsmn.c`, `components/core/util/json.c`
- Create: `test/test_jw.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `jw_t` writer: `jw_init(jw_t*, char *buf, size_t cap)`, `jw_obj_open/close`, `jw_arr_open/close`, `jw_key(const char*)`, `jw_int(int64_t)`, `jw_uint(uint64_t)`, `jw_bool(bool)`, `jw_null()`, `jw_str(const char*)` (escapes `"` `\` and control chars), `jw_double(double, int decimals)`, `jw_len()`, `jw_overflow()`. Output is always NUL-terminated while it fits.
- Produces: `json.h` helpers over jsmn: `int json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max)` (returns token count, or −1 on a jsmn error or nesting deeper than `JSON_MAX_DEPTH`), `bool json_tok_eq(js, tok, "literal")`, `int json_skip(const jsmntok_t *toks, int ntoks, int i)` (index after the subtree, iterative), `bool json_tok_int(js, tok, int64_t *out)`, `bool json_tok_double(js, tok, double *out)`, `bool json_tok_bool(js, tok, bool *out)`, `size_t json_tok_str(js, tok, char *out, size_t cap)`, `int json_obj_get(js, toks, ntoks, obj, "key")` (index of the value token or −1).

- [ ] **Step 1: Vendor jsmn**

```bash
curl -fL https://raw.githubusercontent.com/zserge/jsmn/25647e692c7906b96ffd2b05ca54c097948e879c/jsmn.h -o components/core/include/core/jsmn.h
# if that commit is unavailable, use master:
# curl -fL https://raw.githubusercontent.com/zserge/jsmn/master/jsmn.h -o components/core/include/core/jsmn.h
head -5 components/core/include/core/jsmn.h   # must show the MIT licence header
```

- [ ] **Step 2: Write the failing test**

`test/test_jw.c`:
```c
#include "unity.h"
#include "core/jw.h"
#include "core/json.h"
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_writer_produces_expected_document(void)
{
    char buf[128]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_obj_open(&w);
    jw_key(&w, "a"); jw_int(&w, 1);
    jw_key(&w, "b"); jw_arr_open(&w); jw_int(&w, 1); jw_int(&w, 2); jw_arr_close(&w);
    jw_key(&w, "c"); jw_obj_open(&w); jw_key(&w, "d"); jw_str(&w, "x\"y\\z\n"); jw_obj_close(&w);
    jw_key(&w, "e"); jw_bool(&w, true);
    jw_key(&w, "f"); jw_double(&w, -1.25, 2);
    jw_key(&w, "g"); jw_null(&w);
    jw_obj_close(&w);
    TEST_ASSERT_FALSE(jw_overflow(&w));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"b\":[1,2],\"c\":{\"d\":\"x\\\"y\\\\z\\n\"},\"e\":true,\"f\":-1.25,\"g\":null}", buf);
}

static void test_writer_overflow_is_flagged_and_terminated(void)
{
    char buf[8]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_obj_open(&w); jw_key(&w, "abcdef"); jw_int(&w, 1); jw_obj_close(&w);
    TEST_ASSERT_TRUE(jw_overflow(&w));
    TEST_ASSERT_EQUAL_UINT(0, buf[7]);
}

static void test_tokenizer_helpers(void)
{
    const char *js = "{\"n\":-42,\"s\":\"hi\",\"arr\":[1,{\"k\":2},3],\"f\":1.5,\"t\":true,\"after\":7}";
    jsmntok_t toks[32];
    int n = json_parse(js, strlen(js), toks, 32);
    TEST_ASSERT_GREATER_THAN(0, n);
    int vn = json_obj_get(js, toks, n, 0, "n"); int64_t iv; TEST_ASSERT_TRUE(json_tok_int(js, &toks[vn], &iv)); TEST_ASSERT_EQUAL_INT64(-42, iv);
    int vs = json_obj_get(js, toks, n, 0, "s"); char s[8]; json_tok_str(js, &toks[vs], s, sizeof s); TEST_ASSERT_EQUAL_STRING("hi", s);
    int va = json_obj_get(js, toks, n, 0, "arr"); TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[va].type); TEST_ASSERT_EQUAL_INT(3, toks[va].size);
    int after = json_skip(toks, n, va);
    TEST_ASSERT_TRUE(json_tok_eq(js, &toks[after], "f"));
    int vf = json_obj_get(js, toks, n, 0, "f"); double dv; TEST_ASSERT_TRUE(json_tok_double(js, &toks[vf], &dv)); TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.5, dv);
    int vt = json_obj_get(js, toks, n, 0, "t"); bool bv; TEST_ASSERT_TRUE(json_tok_bool(js, &toks[vt], &bv)); TEST_ASSERT_TRUE(bv);
    TEST_ASSERT_EQUAL_INT(-1, json_obj_get(js, toks, n, 0, "missing"));
}

static void test_double_guard_clamps_decimals_and_flags_unfittable_values(void)
{
    char buf[128]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_arr_open(&w); jw_double(&w, 1.0, 100); jw_arr_close(&w);
    TEST_ASSERT_FALSE(jw_overflow(&w));
    TEST_ASSERT_EQUAL_STRING("[1.00000000000000000]", buf);      /* 17 decimals */
    jw_init(&w, buf, sizeof buf);
    jw_arr_open(&w); jw_double(&w, 1e300, 3); jw_arr_close(&w);
    TEST_ASSERT_TRUE(jw_overflow(&w));
}

static void test_skip_over_nested_object_values(void)
{
    const char *js = "{\"a\":[1,{\"x\":[1,2,3],\"y\":{\"z\":1}},2,3],\"b\":99}";
    jsmntok_t toks[32];
    int n = json_parse(js, strlen(js), toks, 32);
    TEST_ASSERT_GREATER_THAN(0, n);
    int vb = json_obj_get(js, toks, n, 0, "b"); int64_t v;
    TEST_ASSERT_TRUE(json_tok_int(js, &toks[vb], &v)); TEST_ASSERT_EQUAL_INT64(99, v);
    int va = json_obj_get(js, toks, n, 0, "a");
    TEST_ASSERT_EQUAL_INT(vb - 1, json_skip(toks, n, va));          /* skipping the array lands on key "b" */
}

static void test_skip_out_of_range_index_returns_ntoks_without_reading(void)
{
    const char *js = "{\"a\":1}";
    jsmntok_t toks[4];
    int n = json_parse(js, strlen(js), toks, 4);
    TEST_ASSERT_GREATER_THAN(0, n);
    /* A truncated token array (a stale index past the real count) must not dereference toks[i]. */
    TEST_ASSERT_EQUAL_INT(n, json_skip(toks, n, n));         /* i == ntoks */
    TEST_ASSERT_EQUAL_INT(n, json_skip(toks, n, n + 100));   /* i far past ntoks: would be OOB on toks[4] */
    TEST_ASSERT_EQUAL_INT(n, json_skip(toks, n, -1));        /* i < 0 */
}

static void test_parse_rejects_documents_deeper_than_the_cap(void)
{
    static char js[1024];
    static jsmntok_t toks[1024];
    int p = 0;
    for (int i = 0; i < 460; i++) js[p++] = '[';
    for (int i = 0; i < 460; i++) js[p++] = ']';
    TEST_ASSERT_EQUAL_INT(-1, json_parse(js, (size_t)p, toks, 1024));   /* depth, not token count */

    p = 0;                                                              /* 15 levels: still accepted */
    for (int i = 0; i < 15; i++) js[p++] = '[';
    js[p++] = '1';
    for (int i = 0; i < 15; i++) js[p++] = ']';
    int n = json_parse(js, (size_t)p, toks, 1024);
    TEST_ASSERT_EQUAL_INT(16, n);
    TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[0].type);
    TEST_ASSERT_EQUAL_INT(JSMN_PRIMITIVE, toks[15].type);
}

static void test_tok_str_with_zero_capacity_writes_nothing(void)
{
    const char *js = "{\"s\":\"hi\"}";
    jsmntok_t toks[8];
    int n = json_parse(js, strlen(js), toks, 8);
    TEST_ASSERT_GREATER_THAN(0, n);
    int vs = json_obj_get(js, toks, n, 0, "s");
    char guard[4] = { 'A', 'B', 'C', 'D' };
    TEST_ASSERT_EQUAL_UINT(0, json_tok_str(js, &toks[vs], guard, 0));
    TEST_ASSERT_EQUAL_MEMORY("ABCD", guard, 4);
}

/* deterministic LCG, same pattern as the other suites */
static uint32_t lcg = 2463534242u;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

static void test_fuzz_jw_str_always_emits_a_parsable_string(void)
{
    for (int it = 0; it < 2000; it++) {
        char raw[33];
        int len = 1 + (int)(rnd() % 32u);
        for (int i = 0; i < len; i++) raw[i] = (char)(1u + rnd() % 255u);   /* any byte but NUL */
        raw[len] = '\0';
        char buf[512]; jw_t w; jw_init(&w, buf, sizeof buf);
        jw_arr_open(&w); jw_str(&w, raw); jw_arr_close(&w);
        TEST_ASSERT_FALSE(jw_overflow(&w));
        jsmntok_t toks[8];
        int n = json_parse(buf, jw_len(&w), toks, 8);
        TEST_ASSERT_EQUAL_INT(2, n);                                        /* array + one string token */
        TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[0].type);
        TEST_ASSERT_EQUAL_INT(JSMN_STRING, toks[1].type);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_produces_expected_document);
    RUN_TEST(test_writer_overflow_is_flagged_and_terminated);
    RUN_TEST(test_tokenizer_helpers);
    RUN_TEST(test_double_guard_clamps_decimals_and_flags_unfittable_values);
    RUN_TEST(test_skip_over_nested_object_values);
    RUN_TEST(test_skip_out_of_range_index_returns_ntoks_without_reading);
    RUN_TEST(test_parse_rejects_documents_deeper_than_the_cap);
    RUN_TEST(test_tok_str_with_zero_capacity_writes_nothing);
    RUN_TEST(test_fuzz_jw_str_always_emits_a_parsable_string);
    return UNITY_END();
}
```

Add `add_core_test(test_jw)`.

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S test -B test/build && cmake --build test/build`
Expected: FAIL — `core/jw.h: No such file`.

- [ ] **Step 4: Implement the writer**

`components/core/include/core/jw.h`:
```c
#ifndef CORE_JW_H
#define CORE_JW_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define JW_MAX_DEPTH 12
#define JW_MAX_DECIMALS 17

typedef struct {
    char   *buf; size_t cap; size_t len; bool overflow;
    uint8_t depth; bool first[JW_MAX_DEPTH]; bool after_key;
} jw_t;

void   jw_init(jw_t *w, char *buf, size_t cap);
void   jw_obj_open(jw_t *w);  void jw_obj_close(jw_t *w);
void   jw_arr_open(jw_t *w);  void jw_arr_close(jw_t *w);
void   jw_key(jw_t *w, const char *key);
void   jw_int(jw_t *w, int64_t v);
void   jw_uint(jw_t *w, uint64_t v);
void   jw_bool(jw_t *w, bool v);
void   jw_null(jw_t *w);
void   jw_str(jw_t *w, const char *s);
/* decimals clamped to 0..JW_MAX_DECIMALS; a value whose text exceeds 47 chars sets overflow and writes
 * nothing (|v| ≳ 1e29 at 17 decimals, ~1e28 for negative values, whose sign costs one more char) */
void   jw_double(jw_t *w, double v, int decimals);
size_t jw_len(const jw_t *w);
bool   jw_overflow(const jw_t *w);
#endif
```

`components/core/util/jw.c`:
```c
#include "core/jw.h"
#include <stdio.h>
#include <string.h>

void jw_init(jw_t *w, char *buf, size_t cap)
{
    memset(w, 0, sizeof *w); w->buf = buf; w->cap = cap;
    if (cap) buf[0] = '\0';
    w->first[0] = true;
}

static void put(jw_t *w, const char *s, size_t n)
{
    if (w->overflow) return;
    if (w->len + n + 1 > w->cap) { w->overflow = true; if (w->cap) w->buf[w->cap - 1] = '\0'; return; }
    memcpy(w->buf + w->len, s, n); w->len += n; w->buf[w->len] = '\0';
}
static void putc_(jw_t *w, char c) { put(w, &c, 1); }

static void sep(jw_t *w)
{
    if (w->after_key) { w->after_key = false; return; }
    if (!w->first[w->depth]) putc_(w, ',');
    w->first[w->depth] = false;
}

static void open_(jw_t *w, char c)
{
    sep(w); putc_(w, c);
    if (w->depth + 1 < JW_MAX_DEPTH) w->depth++; else w->overflow = true;
    w->first[w->depth] = true;
}
static void close_(jw_t *w, char c) { if (w->depth) w->depth--; putc_(w, c); }

void jw_obj_open(jw_t *w) { open_(w, '{'); }
void jw_obj_close(jw_t *w) { close_(w, '}'); }
void jw_arr_open(jw_t *w) { open_(w, '['); }
void jw_arr_close(jw_t *w) { close_(w, ']'); }

void jw_str(jw_t *w, const char *s)
{
    sep(w); putc_(w, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  put(w, "\\\"", 2); break;
        case '\\': put(w, "\\\\", 2); break;
        case '\n': put(w, "\\n", 2); break;
        case '\r': put(w, "\\r", 2); break;
        case '\t': put(w, "\\t", 2); break;
        default:
            if (c < 0x20) { char tmp[7]; snprintf(tmp, sizeof tmp, "\\u%04x", c); put(w, tmp, 6); }
            else putc_(w, (char)c);
        }
    }
    putc_(w, '"');
}

void jw_key(jw_t *w, const char *key) { jw_str(w, key); putc_(w, ':'); w->after_key = true; }

/* Formats into a scratch buffer; a value that does not fit is treated as overflow (nothing written). */
static void put_fmt(jw_t *w, const char *t, int n, size_t cap)
{
    if (n < 0 || (size_t)n >= cap) { w->overflow = true; if (w->cap) w->buf[w->len] = '\0'; return; }
    sep(w); put(w, t, (size_t)n);
}

void jw_int(jw_t *w, int64_t v) { char t[24]; int n = snprintf(t, sizeof t, "%lld", (long long)v); put_fmt(w, t, n, sizeof t); }
void jw_uint(jw_t *w, uint64_t v) { char t[24]; int n = snprintf(t, sizeof t, "%llu", (unsigned long long)v); put_fmt(w, t, n, sizeof t); }
void jw_bool(jw_t *w, bool v) { sep(w); if (v) put(w, "true", 4); else put(w, "false", 5); }
void jw_null(jw_t *w) { sep(w); put(w, "null", 4); }
void jw_double(jw_t *w, double v, int decimals)
{
    if (decimals < 0) decimals = 0;
    if (decimals > JW_MAX_DECIMALS) decimals = JW_MAX_DECIMALS;
    char t[48]; int n = snprintf(t, sizeof t, "%.*f", decimals, v);
    put_fmt(w, t, n, sizeof t);
}
size_t jw_len(const jw_t *w) { return w->len; }
bool jw_overflow(const jw_t *w) { return w->overflow; }
```

- [ ] **Step 5: Implement the tokenizer wrapper and helpers**

`components/core/include/core/json.h`:
```c
#ifndef CORE_JSON_H
#define CORE_JSON_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define JSMN_HEADER
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "core/jsmn.h"

/* Maximum nesting depth accepted by json_parse: a token with more than JSON_MAX_DEPTH
 * ancestors makes the whole document invalid. Config and track documents nest 6 deep. */
#define JSON_MAX_DEPTH 16

/* Parses and enforces JSON_MAX_DEPTH. Returns the token count, or -1 on a jsmn error or too deep. */
int    json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks);   /* token count or -1 */
bool   json_tok_eq(const char *js, const jsmntok_t *t, const char *s);
/* Iterative (no recursion): the subtree of token i is the run of following tokens that start
 * before toks[i].end, which JSMN_PARENT_LINKS guarantees is contiguous. Depth is capped by
 * json_parse, so neither helper can be driven to unbounded stack use. */
int    json_skip(const jsmntok_t *toks, int ntoks, int i);                          /* index of the token after subtree i */
bool   json_tok_int(const char *js, const jsmntok_t *t, int64_t *out);
bool   json_tok_double(const char *js, const jsmntok_t *t, double *out);
bool   json_tok_bool(const char *js, const jsmntok_t *t, bool *out);
/* Raw copy of the token's source bytes, NUL-terminated, truncated to cap-1; does NOT unescape JSON escapes. Suitable for the ASCII keys and short values this project exchanges (config, track names). Returns the copied length. */
size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap);     /* copies, NUL-terminates, returns length */
int    json_obj_get(const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *key);   /* value token index or -1 */
#endif
```

`components/core/util/jsmn.c`:
```c
#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "core/jsmn.h"
```

`components/core/util/json.c`:
```c
#include "core/json.h"
#include <string.h>
#include <stdlib.h>

int json_parse(const char *js, size_t n, jsmntok_t *toks, unsigned max_toks)
{
    jsmn_parser p; jsmn_init(&p);
    int r = jsmn_parse(&p, js, n, toks, max_toks);
    if (r < 0) return -1;
    /* Depth from the parent links jsmn already maintains; bail out as soon as one chain is too long. */
    for (int i = 0; i < r; i++) {
        int d = 0;
        for (int par = toks[i].parent; par >= 0; par = toks[par].parent)
            if (++d > JSON_MAX_DEPTH) return -1;
    }
    return r;
}

bool json_tok_eq(const char *js, const jsmntok_t *t, const char *s)
{
    size_t len = (size_t)(t->end - t->start);
    return t->type == JSMN_STRING && strlen(s) == len && memcmp(js + t->start, s, len) == 0;
}

int json_skip(const jsmntok_t *toks, int ntoks, int i)
{
    /* Every descendant of i starts before i ends and jsmn emits them contiguously, so a forward
     * scan finds the end of the subtree without recursion. Primitives and strings have no
     * descendants and the loop exits on the first test. */
    if (i < 0 || i >= ntoks) return ntoks;
    int j = i + 1;
    while (j < ntoks && toks[j].start < toks[i].end) j++;
    return j;
}

static size_t tok_copy(const char *js, const jsmntok_t *t, char *tmp, size_t cap)
{
    if (cap == 0) return 0;
    size_t len = (size_t)(t->end - t->start);
    if (len >= cap) len = cap - 1;
    memcpy(tmp, js + t->start, len); tmp[len] = '\0';
    return len;
}

bool json_tok_int(const char *js, const jsmntok_t *t, int64_t *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[32]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; long long v = strtoll(tmp, &end, 10);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_double(const char *js, const jsmntok_t *t, double *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    char tmp[48]; tok_copy(js, t, tmp, sizeof tmp);
    char *end; double v = strtod(tmp, &end);
    if (*end != '\0') return false;
    *out = v; return true;
}

bool json_tok_bool(const char *js, const jsmntok_t *t, bool *out)
{
    if (t->type != JSMN_PRIMITIVE) return false;
    if (js[t->start] == 't') { *out = true; return true; }
    if (js[t->start] == 'f') { *out = false; return true; }
    return false;
}

size_t json_tok_str(const char *js, const jsmntok_t *t, char *out, size_t cap)
{
    /* jsmn leaves escapes in place; the config/track keys never contain escapes, so a plain copy suffices */
    return tok_copy(js, t, out, cap);
}

int json_obj_get(const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *key)
{
    if (obj < 0 || obj >= ntoks || toks[obj].type != JSMN_OBJECT) return -1;
    int i = obj + 1;                            /* first key token */
    for (int k = 0; k < toks[obj].size && i + 1 < ntoks; k++) {
        if (json_tok_eq(js, &toks[i], key)) return i + 1;
        i = json_skip(toks, ntoks, i + 1);      /* past this key's value subtree */
    }
    return -1;
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass. (The `set_source_files_properties` line from Task 1 relaxes warnings for `jsmn.c`.)

- [ ] **Step 7: Commit**

```bash
git add components/core/include/core/jw.h components/core/util/jw.c components/core/include/core/jsmn.h components/core/include/core/json.h components/core/util/jsmn.c components/core/util/json.c test/test_jw.c test/CMakeLists.txt
git commit -m "feat(core): minimal JSON writer and jsmn tokenizer helpers"
```

---

### Task 9: Configuration (`core/cfg`)

**Files:**
- Create: `components/core/include/core/cfg.h`, `components/core/config/cfg.c`, `components/core/config/cfg_json.c`, `test/test_cfg.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `jw`, `json`.
- Produces (spec §15.1, §5.2): `cfg_t` (fields below), `CFG_VERSION 1`, `cfg_profile_t` (hardware-profile defaults), `int cfg_apply_profile(cfg_t*, const cfg_profile_t*)` (0 ok / −1 bad name length), `int cfg_defaults(cfg_t*)`, `int cfg_validate(cfg_t*)` (returns count of clamped fields, forces `version` to `CFG_VERSION`), `int cfg_from_json(cfg_t*, const char *json, size_t n, char *err, size_t err_cap)` (merge semantics; `version` ignored; oversized arrays rejected; returns 0 or −1 with `err` filled), `int cfg_to_json(const cfg_t*, char *out, size_t cap)` (bytes or −1), `int cfg_migrate(cfg_t*, uint8_t from_version)`.

- [ ] **Step 1: Write the failing test**

`test/test_cfg.c`:
```c
#include "unity.h"
#include "core/cfg.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* deterministic LCG, same pattern as the other suites; reseeded in setUp so a second run within the
 * same process (the on-target 6 KB stack rerun in test_apps/core_selftest) replays the exact same
 * fuzz sequence as the first. */
static uint32_t lcg;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

void setUp(void) { lcg = 987654321u; }
void tearDown(void) {}

static void test_defaults_are_valid(void)
{
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
    TEST_ASSERT_EQUAL_UINT16(20, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);
    TEST_ASSERT_EQUAL_UINT8(3, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(3300, c.power.shutdown_mv);
    TEST_ASSERT_EQUAL_STRING("LapTimer", c.ble.name);
}

/* Every numeric clamp cfg_validate still performs, driven one field at a time. Bounds a uint8_t
 * field cannot violate (0 low, 255 high) are not clamped by cfg_validate and are marked absent. */
typedef struct { const char *name; size_t off; uint8_t width; long lo, hi; bool has_lo, has_hi; } clamp_case_t;
#define CL(field, w, lo, hi, hl, hh) { #field, offsetof(cfg_t, field), (w), (lo), (hi), (hl), (hh) }
static const clamp_case_t CLAMPS[] = {
    CL(lap.min_lap_s,       2,    5,  600, true,  true),
    CL(lap.max_lap_s,       2,   60, 3600, true,  true),
    CL(lap.gate_rearm_m,    2,   10,  500, true,  true),
    CL(lap.pit_speed_kmh,   1,    1,   30, true,  true),
    CL(lap.pit_time_s,      1,    3,   60, true,  true),
    CL(drag.benches_kmh[0], 2,   10,  400, true,  true),
    CL(drag.benches_mph[0], 2,   10,  250, true,  true),
    CL(drag.launch_g_e2,    1,    5,   50, true,  true),
    CL(power.pit_after_s,   2,   10,  600, true,  true),
    CL(power.park_after_s,  2,   60, 7200, true,  true),
    CL(power.shutdown_mv,   2, 3000, 3600, true,  true),
    CL(power.conn_idle_s,   2,   30, 1800, true,  true),
    CL(display.full_every,  1,    1,   50, true,  true),
    CL(ble.adv_s,           2,   15,  600, true,  true),
    CL(gps.dyn_model,       1,    0,    8, false, true),
    CL(gps.rate_hz,         1,    0,   25, false, true),
    CL(imu.mot_thr,         1,    2,  255, true,  false),
    CL(imu.mot_dur_ms,      1,    1,  255, true,  false),
};

static void clamp_set(cfg_t *c, const clamp_case_t *f, long v)
{
    if (f->width == 1) { uint8_t x = (uint8_t)v; memcpy((uint8_t *)c + f->off, &x, 1); }
    else { uint16_t x = (uint16_t)v; memcpy((uint8_t *)c + f->off, &x, 2); }
}
static long clamp_get(const cfg_t *c, const clamp_case_t *f)
{
    if (f->width == 1) { uint8_t x; memcpy(&x, (const uint8_t *)c + f->off, 1); return x; }
    uint16_t x; memcpy(&x, (const uint8_t *)c + f->off, 2); return x;
}

static void test_validate_clamps_each_out_of_range_field(void)
{
    char msg[96];
    for (size_t i = 0; i < sizeof CLAMPS / sizeof CLAMPS[0]; i++) {
        const clamp_case_t *f = &CLAMPS[i];
        if (f->has_lo) {
            cfg_t c; cfg_defaults(&c);
            clamp_set(&c, f, f->lo - 1);
            snprintf(msg, sizeof msg, "%s below %ld", f->name, f->lo);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), msg);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(f->lo, clamp_get(&c, f), msg);
        }
        if (f->has_hi) {
            cfg_t c; cfg_defaults(&c);
            clamp_set(&c, f, f->hi + 1);
            snprintf(msg, sizeof msg, "%s above %ld", f->name, f->hi);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), msg);
            TEST_ASSERT_EQUAL_INT64_MESSAGE(f->hi, clamp_get(&c, f), msg);
        }
    }
    /* the non-clamp corrections: enums, rotation and the fused-rate whitelist */
    cfg_t c; cfg_defaults(&c);
    c.units = 9; c.mode = 9; c.display.rotation = 90; c.log.fused_hz = 7; c.ble.name[0] = '\0';
    TEST_ASSERT_EQUAL_INT(5, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_KMH, c.units);
    TEST_ASSERT_EQUAL_UINT8(CFG_MODE_LAP, c.mode);
    TEST_ASSERT_EQUAL_UINT8(0, c.display.rotation);
    TEST_ASSERT_EQUAL_UINT8(10, c.log.fused_hz);
    TEST_ASSERT_EQUAL_STRING("LapTimer", c.ble.name);
}

static void test_validate_resets_implausible_battery_calibration(void)
{
    const uint16_t D_ADC0 = 3000, D_TRUE0 = 3000, D_ADC1 = 4200, D_TRUE1 = 4200;
    struct { const char *why; uint16_t a0, t0, a1, t1; } bad[] = {
        { "adc points too close",   3000, 3000, 3050, 4200 },
        { "adc points inverted",    4200, 3000, 3000, 4200 },
        { "true points too close",  3000, 3000, 4200, 3099 },
        { "true points inverted",   3000, 4200, 4200, 3000 },
        { "adc_mv[0] below 1000",    900, 3000, 4200, 4200 },
        { "adc_mv[1] above 5000",   3000, 3000, 5001, 4200 },
        { "true_mv[0] below 1000",  3000,  900, 4200, 4200 },
        { "true_mv[1] above 5000",  3000, 3000, 4200, 5001 },
        { "all zero",                  0,    0,    0,    0 },
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        cfg_t c; cfg_defaults(&c);
        c.battery.adc_mv[0] = bad[i].a0; c.battery.true_mv[0] = bad[i].t0;
        c.battery.adc_mv[1] = bad[i].a1; c.battery.true_mv[1] = bad[i].t1;
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, cfg_validate(&c), bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_ADC0, c.battery.adc_mv[0], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_TRUE0, c.battery.true_mv[0], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_ADC1, c.battery.adc_mv[1], bad[i].why);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(D_TRUE1, c.battery.true_mv[1], bad[i].why);
    }
    /* a legitimate calibration survives untouched */
    cfg_t ok; cfg_defaults(&ok);
    ok.battery.adc_mv[0] = 1500; ok.battery.true_mv[0] = 3510;
    ok.battery.adc_mv[1] = 2000; ok.battery.true_mv[1] = 4180;
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&ok));
    TEST_ASSERT_EQUAL_UINT16(1500, ok.battery.adc_mv[0]);
    TEST_ASSERT_EQUAL_UINT16(4180, ok.battery.true_mv[1]);
}

static void test_from_json_merges_only_given_keys_and_ignores_unknown(void)
{
    cfg_t c; cfg_defaults(&c);
    const char *js = "{\"lap\":{\"min_lap_s\":30},\"units\":\"mph\",\"bogus\":1,\"drag\":{\"benches_kmh\":[80,160],\"rollout\":true}}";
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(30, c.lap.min_lap_s);
    TEST_ASSERT_EQUAL_UINT16(1800, c.lap.max_lap_s);            /* untouched */
    TEST_ASSERT_EQUAL_UINT8(CFG_UNITS_MPH, c.units);
    TEST_ASSERT_EQUAL_UINT8(2, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(160, c.drag.benches_kmh[1]);
    TEST_ASSERT_TRUE(c.drag.rollout);
}

static void test_from_json_rejects_malformed(void)
{
    cfg_t c; cfg_defaults(&c);
    char err[64];
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, "{\"lap\":", 7, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, "{\"units\":\"furlongs\"}", 20, err, sizeof err));
}

static void test_json_round_trip_is_lossless(void)
{
    static cfg_t a; cfg_defaults(&a);
    a.lap.min_lap_s = 33; a.drag.n_mph = 2; a.drag.benches_mph[0] = 60; a.drag.benches_mph[1] = 100;
    a.lap.n_default_layout = 1; a.lap.default_layout[0].venue = 6; a.lap.default_layout[0].layout = 2;
    a.battery.adc_mv[0] = 1500; a.battery.true_mv[0] = 3510; a.battery.adc_mv[1] = 2000; a.battery.true_mv[1] = 4180;
    strcpy(a.ble.name, "LapTimer-AB12"); a.display.live_clock = true;
    static char js[1024];
    int n = cfg_to_json(&a, js, sizeof js);
    TEST_ASSERT_GREATER_THAN(0, n);
    static cfg_t b; cfg_defaults(&b);
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&b, js, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

static void test_migrate_v1_is_noop(void)
{
    cfg_t c; cfg_defaults(&c);
    TEST_ASSERT_EQUAL_INT(0, cfg_migrate(&c, 1));
    TEST_ASSERT_EQUAL_INT(-1, cfg_migrate(&c, 0));
}

static void test_version_is_owned_by_firmware(void)
{
    cfg_t c; cfg_defaults(&c);
    char err[64];
    TEST_ASSERT_EQUAL_INT(0, cfg_from_json(&c, "{\"version\":7}", 13, err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
    c.version = 200;
    TEST_ASSERT_EQUAL_INT(1, cfg_validate(&c));
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);
}

static void test_profile_defaults_apply(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_profile_t p = { true, 25, "LapTimer-AB12" };
    TEST_ASSERT_EQUAL_INT(0, cfg_apply_profile(&c, &p));
    TEST_ASSERT_TRUE(c.display.live_clock);
    TEST_ASSERT_EQUAL_UINT8(25, c.log.fused_hz);
    TEST_ASSERT_EQUAL_STRING("LapTimer-AB12", c.ble.name);
    TEST_ASSERT_EQUAL_INT(0, cfg_validate(&c));
    cfg_profile_t bad = { false, 10, "this-name-is-way-too-long" };
    TEST_ASSERT_EQUAL_INT(-1, cfg_apply_profile(&c, &bad));
}

static void test_oversized_arrays_are_rejected(void)
{
    cfg_t c; cfg_defaults(&c); char err[64];
    const char *js = "{\"drag\":{\"benches_kmh\":[1,2,3,4,5]}}";
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT8(3, c.drag.n_kmh);
    TEST_ASSERT_EQUAL_UINT16(100, c.drag.benches_kmh[0]);
}

static void test_err_buffer_is_optional(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_t before = c;
    const char *bad_value = "{\"units\":\"furlongs\"}";
    const char *bad_section = "{\"lap\":5}";
    const char *malformed = "{\"lap\":";
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_value, strlen(bad_value), NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_section, strlen(bad_section), NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, malformed, strlen(malformed), NULL, 0));
    char err[8];
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, bad_value, strlen(bad_value), err, 0));   /* zero capacity */
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_from_json_rejects_document_deeper_than_the_depth_cap(void)
{
    static char js[256]; int p = 0;
    p += snprintf(js + p, sizeof js - (size_t)p, "{\"lap\":{\"min_lap_s\":30},\"z\":");
    for (int i = 0; i < 40; i++) js[p++] = '[';
    for (int i = 0; i < 40; i++) js[p++] = ']';
    js[p++] = '}';
    cfg_t c; cfg_defaults(&c); cfg_t before = c;
    char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, cfg_from_json(&c, js, (size_t)p, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_profile_with_bad_name_leaves_the_struct_untouched(void)
{
    cfg_t c; cfg_defaults(&c);
    cfg_t before = c;
    cfg_profile_t bad = { true, 25, "this-name-is-way-too-long" };
    TEST_ASSERT_EQUAL_INT(-1, cfg_apply_profile(&c, &bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);
}

static void test_fuzz_mutated_documents_never_corrupt_the_struct(void)
{
    static cfg_t seed; cfg_defaults(&seed);
    seed.lap.min_lap_s = 33; seed.drag.n_mph = 2; seed.drag.benches_mph[0] = 60; seed.drag.benches_mph[1] = 100;
    seed.lap.n_default_layout = 1; seed.lap.default_layout[0].venue = 6; seed.lap.default_layout[0].layout = 2;
    static char base[1024];
    int n = cfg_to_json(&seed, base, sizeof base);
    TEST_ASSERT_GREATER_THAN(0, n);
    int accepted = 0, rejected = 0;
    for (int it = 0; it < 500; it++) {
        static char js[1024]; memcpy(js, base, (size_t)n);
        int muts = 1 + (int)(rnd() % 4u);
        for (int m = 0; m < muts; m++) js[rnd() % (uint32_t)n] = (char)(rnd() % 256u);
        cfg_t c; cfg_defaults(&c); cfg_t before = c;
        char err[64]; err[0] = '\0';
        int r = cfg_from_json(&c, js, (size_t)n, err, sizeof err);
        if (r < 0) {
            rejected++;
            TEST_ASSERT_TRUE(strlen(err) > 0);
            TEST_ASSERT_EQUAL_MEMORY(&before, &c, sizeof c);   /* -1 must not have moved anything */
        } else {
            accepted++;
            cfg_validate(&c);                                   /* whatever got through must still validate */
        }
    }
    TEST_ASSERT_GREATER_THAN(0, rejected);
    TEST_ASSERT_EQUAL_INT(500, accepted + rejected);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_validate_clamps_each_out_of_range_field);
    RUN_TEST(test_from_json_merges_only_given_keys_and_ignores_unknown);
    RUN_TEST(test_from_json_rejects_malformed);
    RUN_TEST(test_json_round_trip_is_lossless);
    RUN_TEST(test_migrate_v1_is_noop);
    RUN_TEST(test_version_is_owned_by_firmware);
    RUN_TEST(test_profile_defaults_apply);
    RUN_TEST(test_oversized_arrays_are_rejected);
    RUN_TEST(test_validate_resets_implausible_battery_calibration);
    RUN_TEST(test_err_buffer_is_optional);
    RUN_TEST(test_from_json_rejects_document_deeper_than_the_depth_cap);
    RUN_TEST(test_profile_with_bad_name_leaves_the_struct_untouched);
    RUN_TEST(test_fuzz_mutated_documents_never_corrupt_the_struct);
    return UNITY_END();
}
```

Add `add_core_test(test_cfg)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/cfg.h: No such file`.

- [ ] **Step 3: Write the header**

`components/core/include/core/cfg.h`:
```c
#ifndef CORE_CFG_H
#define CORE_CFG_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CFG_VERSION 1
enum { CFG_UNITS_KMH = 0, CFG_UNITS_MPH = 1 };
enum { CFG_MODE_LAP = 0, CFG_MODE_DRAG = 1 };
#define CFG_MAX_DEFAULT_LAYOUTS 8
#define CFG_MAX_BENCHES 4

typedef struct {
    uint8_t version;
    uint8_t units;                 /* CFG_UNITS_* */
    uint8_t mode;                  /* CFG_MODE_* */
    struct {
        uint16_t min_lap_s, max_lap_s, gate_rearm_m;
        uint8_t  pit_speed_kmh, pit_time_s;
        uint8_t  n_default_layout;
        struct { uint16_t venue, layout; } default_layout[CFG_MAX_DEFAULT_LAYOUTS];
    } lap;
    struct {
        uint16_t benches_kmh[CFG_MAX_BENCHES], benches_mph[CFG_MAX_BENCHES];
        uint8_t  n_kmh, n_mph;
        bool     rollout;
        uint8_t  launch_g_e2;
    } drag;
    struct { uint16_t pit_after_s, park_after_s, shutdown_mv, conn_idle_s; } power;
    struct { bool live_clock; uint8_t full_every; uint8_t rotation; bool invert; } display;
    struct { uint16_t adc_mv[2], true_mv[2]; } battery;   /* two-point calibration; identity when adc==true */
    struct { char name[16]; uint16_t adv_s; } ble;
    struct { uint8_t fused_hz; } log;
    struct { uint8_t dyn_model, rate_hz; } gps;
    struct { uint8_t mot_thr, mot_dur_ms; } imu;
} cfg_t;

/* Hardware-profile defaults. The app calls cfg_apply_profile() at boot right after cfg_defaults()
 * and before loading the NVS blob, with values from build_config.h and the MAC-derived BLE name. */
typedef struct { bool display_live_clock; uint8_t log_fused_hz; const char *ble_name; } cfg_profile_t;
int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p);      /* 0 ok / -1 bad name length */

int cfg_defaults(cfg_t *c);
int cfg_validate(cfg_t *c);                       /* clamps; returns number of corrected fields */
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap);   /* merge; "version" is ignored (owned by firmware); arrays longer than capacity are rejected; 0 ok / -1 error with err */
int cfg_to_json(const cfg_t *c, char *out, size_t cap);                             /* bytes written or -1 */
int cfg_migrate(cfg_t *c, uint8_t from_version);                                    /* 0 ok / -1 unknown version */
#endif
```

- [ ] **Step 4: Implement defaults, validate, migrate**

`components/core/config/cfg.c`:
```c
#include "core/cfg.h"
#include <string.h>

int cfg_defaults(cfg_t *c)
{
    memset(c, 0, sizeof *c);
    c->version = CFG_VERSION; c->units = CFG_UNITS_KMH; c->mode = CFG_MODE_LAP;
    c->lap.min_lap_s = 20; c->lap.max_lap_s = 1800; c->lap.gate_rearm_m = 50; c->lap.pit_speed_kmh = 5; c->lap.pit_time_s = 10;
    c->drag.benches_kmh[0] = 100; c->drag.benches_kmh[1] = 200; c->drag.benches_kmh[2] = 300; c->drag.n_kmh = 3;
    c->drag.benches_mph[0] = 60; c->drag.benches_mph[1] = 120; c->drag.benches_mph[2] = 180; c->drag.n_mph = 3;
    c->drag.rollout = false; c->drag.launch_g_e2 = 15;
    c->power.pit_after_s = 30; c->power.park_after_s = 600; c->power.shutdown_mv = 3300; c->power.conn_idle_s = 300;
    c->display.live_clock = false; c->display.full_every = 10; c->display.rotation = 0; c->display.invert = false;
    c->battery.adc_mv[0] = 3000; c->battery.true_mv[0] = 3000; c->battery.adc_mv[1] = 4200; c->battery.true_mv[1] = 4200;
    strcpy(c->ble.name, "LapTimer"); c->ble.adv_s = 60;
    c->log.fused_hz = 10;
    c->gps.dyn_model = 4; c->gps.rate_hz = 0;        /* 0 = profile maximum */
    c->imu.mot_thr = 20; c->imu.mot_dur_ms = 40;
    return 0;
}

#define CLAMP_U(field, lo, hi) do { if ((field) < (lo)) { (field) = (lo); n++; } else if ((field) > (hi)) { (field) = (hi); n++; } } while (0)

int cfg_validate(cfg_t *c)
{
    int n = 0;
    if (c->version != CFG_VERSION) { c->version = CFG_VERSION; n++; }
    if (c->units > CFG_UNITS_MPH) { c->units = CFG_UNITS_KMH; n++; }
    if (c->mode > CFG_MODE_DRAG) { c->mode = CFG_MODE_LAP; n++; }
    CLAMP_U(c->lap.min_lap_s, 5, 600);
    CLAMP_U(c->lap.max_lap_s, 60, 3600);
    CLAMP_U(c->lap.gate_rearm_m, 10, 500);
    CLAMP_U(c->lap.pit_speed_kmh, 1, 30);
    CLAMP_U(c->lap.pit_time_s, 3, 60);
    if (c->lap.n_default_layout > CFG_MAX_DEFAULT_LAYOUTS) { c->lap.n_default_layout = CFG_MAX_DEFAULT_LAYOUTS; n++; }
    if (c->drag.n_kmh > CFG_MAX_BENCHES) { c->drag.n_kmh = CFG_MAX_BENCHES; n++; }
    if (c->drag.n_mph > CFG_MAX_BENCHES) { c->drag.n_mph = CFG_MAX_BENCHES; n++; }
    for (int i = 0; i < c->drag.n_kmh; i++) CLAMP_U(c->drag.benches_kmh[i], 10, 400);
    for (int i = 0; i < c->drag.n_mph; i++) CLAMP_U(c->drag.benches_mph[i], 10, 250);
    CLAMP_U(c->drag.launch_g_e2, 5, 50);
    CLAMP_U(c->power.pit_after_s, 10, 600);
    CLAMP_U(c->power.park_after_s, 60, 7200);
    CLAMP_U(c->power.shutdown_mv, 3000, 3600);
    CLAMP_U(c->power.conn_idle_s, 30, 1800);
    CLAMP_U(c->display.full_every, 1, 50);
    if (c->display.rotation != 0 && c->display.rotation != 180) { c->display.rotation = 0; n++; }
    /* Two-point battery calibration: the two points must be ordered and far enough apart for the
     * interpolation to be meaningful, and both in a plausible cell range. A pair that fails any of
     * that is not clamped field by field (which could invent a worse curve) but reset wholesale. */
    if (c->battery.adc_mv[1] < c->battery.adc_mv[0] + 100 || c->battery.true_mv[1] < c->battery.true_mv[0] + 100 ||
        c->battery.adc_mv[0] < 1000 || c->battery.adc_mv[0] > 5000 || c->battery.adc_mv[1] < 1000 || c->battery.adc_mv[1] > 5000 ||
        c->battery.true_mv[0] < 1000 || c->battery.true_mv[0] > 5000 || c->battery.true_mv[1] < 1000 || c->battery.true_mv[1] > 5000) {
        c->battery.adc_mv[0] = 3000; c->battery.true_mv[0] = 3000;
        c->battery.adc_mv[1] = 4200; c->battery.true_mv[1] = 4200;
        n++;
    }
    if (c->ble.name[0] == '\0') { strcpy(c->ble.name, "LapTimer"); n++; }
    if (c->ble.name[15] != '\0') { c->ble.name[15] = '\0'; n++; }
    CLAMP_U(c->ble.adv_s, 15, 600);
    if (c->log.fused_hz != 5 && c->log.fused_hz != 10 && c->log.fused_hz != 25) { c->log.fused_hz = 10; n++; }
    /* uint8_t fields: only the bounds a uint8_t can actually violate are checked. */
    if (c->gps.dyn_model > 8) { c->gps.dyn_model = 8; n++; }
    if (c->gps.rate_hz > 25) { c->gps.rate_hz = 25; n++; }
    if (c->imu.mot_thr < 2) { c->imu.mot_thr = 2; n++; }
    if (c->imu.mot_dur_ms < 1) { c->imu.mot_dur_ms = 1; n++; }
    return n;
}

int cfg_migrate(cfg_t *c, uint8_t from_version)
{
    if (from_version == 1) { c->version = CFG_VERSION; return 0; }
    return -1;
}

int cfg_apply_profile(cfg_t *c, const cfg_profile_t *p)
{
    /* Validate before touching anything: a rejected profile must leave the config untouched. */
    if (p->ble_name && strlen(p->ble_name) > 15) return -1;
    c->display.live_clock = p->display_live_clock;
    c->log.fused_hz = p->log_fused_hz;
    if (p->ble_name) strcpy(c->ble.name, p->ble_name);
    return 0;
}
```

- [ ] **Step 5: Implement the JSON codec**

`components/core/config/cfg_json.c`:
```c
#include "core/cfg.h"
#include "core/jw.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

#define MAX_TOKS 192

/* err is optional everywhere: a NULL or zero-capacity buffer just discards the message. */
static int set_err(char *err, size_t cap, const char *msg) { if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = '\0'; } return -1; }

static bool get_u16(const char *js, const jsmntok_t *t, uint16_t *out) { int64_t v; if (!json_tok_int(js, t, &v) || v < 0 || v > 65535) return false; *out = (uint16_t)v; return true; }
static bool get_u8(const char *js, const jsmntok_t *t, uint8_t *out) { int64_t v; if (!json_tok_int(js, t, &v) || v < 0 || v > 255) return false; *out = (uint8_t)v; return true; }

/* returns 0 ok, -1 type error */
static int apply(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, const char *path, int v)
{
    const jsmntok_t *t = &toks[v];
    if (!strcmp(path, "units")) {
        if (json_tok_eq(js, t, "kmh")) { c->units = CFG_UNITS_KMH; return 0; }
        if (json_tok_eq(js, t, "mph")) { c->units = CFG_UNITS_MPH; return 0; }
        return -1;
    }
    if (!strcmp(path, "mode")) {
        if (json_tok_eq(js, t, "lap")) { c->mode = CFG_MODE_LAP; return 0; }
        if (json_tok_eq(js, t, "drag")) { c->mode = CFG_MODE_DRAG; return 0; }
        return -1;
    }
    if (!strcmp(path, "lap.min_lap_s")) return get_u16(js, t, &c->lap.min_lap_s) ? 0 : -1;
    if (!strcmp(path, "lap.max_lap_s")) return get_u16(js, t, &c->lap.max_lap_s) ? 0 : -1;
    if (!strcmp(path, "lap.gate_rearm_m")) return get_u16(js, t, &c->lap.gate_rearm_m) ? 0 : -1;
    if (!strcmp(path, "lap.pit_speed_kmh")) return get_u8(js, t, &c->lap.pit_speed_kmh) ? 0 : -1;
    if (!strcmp(path, "lap.pit_time_s")) return get_u8(js, t, &c->lap.pit_time_s) ? 0 : -1;
    if (!strcmp(path, "lap.default_layout")) {
        if (t->type != JSMN_ARRAY) return -1;
        if (t->size > CFG_MAX_DEFAULT_LAYOUTS) return -1;
        int i = v + 1; uint8_t n = 0;
        for (int k = 0; k < t->size; k++) {
            int vv = json_obj_get(js, toks, ntoks, i, "venue"), ll = json_obj_get(js, toks, ntoks, i, "layout");
            if (vv < 0 || ll < 0 || !get_u16(js, &toks[vv], &c->lap.default_layout[n].venue) || !get_u16(js, &toks[ll], &c->lap.default_layout[n].layout)) return -1;
            n++; i = json_skip(toks, ntoks, i);
        }
        c->lap.n_default_layout = n; return 0;
    }
    if (!strcmp(path, "drag.benches_kmh") || !strcmp(path, "drag.benches_mph")) {
        if (t->type != JSMN_ARRAY) return -1;
        if (t->size > CFG_MAX_BENCHES) return -1;
        bool kmh = path[13] == 'k';
        uint16_t *dst = kmh ? c->drag.benches_kmh : c->drag.benches_mph; uint8_t n = 0;
        for (int k = 0; k < t->size; k++) { if (!get_u16(js, &toks[v + 1 + k], &dst[n])) return -1; n++; }
        if (kmh) c->drag.n_kmh = n; else c->drag.n_mph = n;
        return 0;
    }
    if (!strcmp(path, "drag.rollout")) return json_tok_bool(js, t, &c->drag.rollout) ? 0 : -1;
    if (!strcmp(path, "drag.launch_g")) return get_u8(js, t, &c->drag.launch_g_e2) ? 0 : -1;
    if (!strcmp(path, "power.pit_after_s")) return get_u16(js, t, &c->power.pit_after_s) ? 0 : -1;
    if (!strcmp(path, "power.park_after_s")) return get_u16(js, t, &c->power.park_after_s) ? 0 : -1;
    if (!strcmp(path, "power.shutdown_mv")) return get_u16(js, t, &c->power.shutdown_mv) ? 0 : -1;
    if (!strcmp(path, "power.conn_idle_s")) return get_u16(js, t, &c->power.conn_idle_s) ? 0 : -1;
    if (!strcmp(path, "display.live_clock")) return json_tok_bool(js, t, &c->display.live_clock) ? 0 : -1;
    if (!strcmp(path, "display.full_refresh_every")) return get_u8(js, t, &c->display.full_every) ? 0 : -1;
    if (!strcmp(path, "display.rotation")) return get_u8(js, t, &c->display.rotation) ? 0 : -1;
    if (!strcmp(path, "display.invert")) return json_tok_bool(js, t, &c->display.invert) ? 0 : -1;
    if (!strcmp(path, "battery.cal")) {
        if (t->type != JSMN_ARRAY || t->size != 2) return -1;
        int i = v + 1;
        for (int k = 0; k < 2; k++) {
            int a = json_obj_get(js, toks, ntoks, i, "adc_mv"), b = json_obj_get(js, toks, ntoks, i, "true_mv");
            if (a < 0 || b < 0 || !get_u16(js, &toks[a], &c->battery.adc_mv[k]) || !get_u16(js, &toks[b], &c->battery.true_mv[k])) return -1;
            i = json_skip(toks, ntoks, i);
        }
        return 0;
    }
    if (!strcmp(path, "ble.name")) { if (t->type != JSMN_STRING || t->end - t->start > 15) return -1; json_tok_str(js, t, c->ble.name, sizeof c->ble.name); return 0; }
    if (!strcmp(path, "ble.adv_s")) return get_u16(js, t, &c->ble.adv_s) ? 0 : -1;
    if (!strcmp(path, "log.fused_hz")) return get_u8(js, t, &c->log.fused_hz) ? 0 : -1;
    if (!strcmp(path, "gps.dyn_model")) return get_u8(js, t, &c->gps.dyn_model) ? 0 : -1;
    if (!strcmp(path, "gps.rate_hz")) return get_u8(js, t, &c->gps.rate_hz) ? 0 : -1;
    if (!strcmp(path, "imu.mot_thr")) return get_u8(js, t, &c->imu.mot_thr) ? 0 : -1;
    if (!strcmp(path, "imu.mot_dur_ms")) return get_u8(js, t, &c->imu.mot_dur_ms) ? 0 : -1;
    return 0;   /* unknown key: ignored */
}

static const char *const SECTIONS[] = { "lap", "drag", "power", "display", "battery", "ble", "log", "gps", "imu" };

static int walk(cfg_t *c, const char *js, const jsmntok_t *toks, int ntoks, int obj, const char *prefix, char *err, size_t err_cap)
{
    int i = obj + 1;
    for (int k = 0; k < toks[obj].size && i + 1 < ntoks; k++) {
        char key[32], path[64];
        json_tok_str(js, &toks[i], key, sizeof key);
        int v = i + 1;
        if (prefix[0]) snprintf(path, sizeof path, "%s.%s", prefix, key); else snprintf(path, sizeof path, "%s", key);
        bool is_section = false;
        if (!prefix[0]) for (size_t s = 0; s < sizeof SECTIONS / sizeof SECTIONS[0]; s++) if (!strcmp(key, SECTIONS[s])) is_section = true;
        if (is_section) {
            if (toks[v].type != JSMN_OBJECT) {
                char msg[64]; snprintf(msg, sizeof msg, "%s must be an object", key);
                return set_err(err, err_cap, msg);
            }
            if (walk(c, js, toks, ntoks, v, key, err, err_cap) < 0) return -1;
        } else if (apply(c, js, toks, ntoks, path, v) < 0) {
            char msg[96]; snprintf(msg, sizeof msg, "bad value for %s", path);
            return set_err(err, err_cap, msg);
        }
        i = json_skip(toks, ntoks, i + 1);        /* i is the key; step past its value subtree */
    }
    return 0;
}

/* Not reentrant: uses a static token array (called from the single conn task). */
int cfg_from_json(cfg_t *c, const char *json, size_t n, char *err, size_t err_cap)
{
    static jsmntok_t toks[MAX_TOKS];
    if (err && err_cap) err[0] = '\0';
    int cnt = json_parse(json, n, toks, MAX_TOKS);
    if (cnt < 1 || toks[0].type != JSMN_OBJECT) return set_err(err, err_cap, "malformed json");
    cfg_t tmp = *c;
    if (walk(&tmp, json, toks, cnt, 0, "", err, err_cap) < 0) return -1;
    *c = tmp;
    return 0;
}

int cfg_to_json(const cfg_t *c, char *out, size_t cap)
{
    jw_t w; jw_init(&w, out, cap);
    jw_obj_open(&w);
    jw_key(&w, "version"); jw_uint(&w, c->version);
    jw_key(&w, "units"); jw_str(&w, c->units == CFG_UNITS_MPH ? "mph" : "kmh");
    jw_key(&w, "mode"); jw_str(&w, c->mode == CFG_MODE_DRAG ? "drag" : "lap");
    jw_key(&w, "lap"); jw_obj_open(&w);
      jw_key(&w, "min_lap_s"); jw_uint(&w, c->lap.min_lap_s);
      jw_key(&w, "max_lap_s"); jw_uint(&w, c->lap.max_lap_s);
      jw_key(&w, "gate_rearm_m"); jw_uint(&w, c->lap.gate_rearm_m);
      jw_key(&w, "pit_speed_kmh"); jw_uint(&w, c->lap.pit_speed_kmh);
      jw_key(&w, "pit_time_s"); jw_uint(&w, c->lap.pit_time_s);
      jw_key(&w, "default_layout"); jw_arr_open(&w);
      for (int i = 0; i < c->lap.n_default_layout; i++) { jw_obj_open(&w); jw_key(&w, "venue"); jw_uint(&w, c->lap.default_layout[i].venue); jw_key(&w, "layout"); jw_uint(&w, c->lap.default_layout[i].layout); jw_obj_close(&w); }
      jw_arr_close(&w);
    jw_obj_close(&w);
    jw_key(&w, "drag"); jw_obj_open(&w);
      jw_key(&w, "benches_kmh"); jw_arr_open(&w); for (int i = 0; i < c->drag.n_kmh; i++) jw_uint(&w, c->drag.benches_kmh[i]); jw_arr_close(&w);
      jw_key(&w, "benches_mph"); jw_arr_open(&w); for (int i = 0; i < c->drag.n_mph; i++) jw_uint(&w, c->drag.benches_mph[i]); jw_arr_close(&w);
      jw_key(&w, "rollout"); jw_bool(&w, c->drag.rollout);
      jw_key(&w, "launch_g"); jw_uint(&w, c->drag.launch_g_e2);
    jw_obj_close(&w);
    jw_key(&w, "power"); jw_obj_open(&w);
      jw_key(&w, "pit_after_s"); jw_uint(&w, c->power.pit_after_s);
      jw_key(&w, "park_after_s"); jw_uint(&w, c->power.park_after_s);
      jw_key(&w, "shutdown_mv"); jw_uint(&w, c->power.shutdown_mv);
      jw_key(&w, "conn_idle_s"); jw_uint(&w, c->power.conn_idle_s);
    jw_obj_close(&w);
    jw_key(&w, "display"); jw_obj_open(&w);
      jw_key(&w, "live_clock"); jw_bool(&w, c->display.live_clock);
      jw_key(&w, "full_refresh_every"); jw_uint(&w, c->display.full_every);
      jw_key(&w, "rotation"); jw_uint(&w, c->display.rotation);
      jw_key(&w, "invert"); jw_bool(&w, c->display.invert);
    jw_obj_close(&w);
    jw_key(&w, "battery"); jw_obj_open(&w);
      jw_key(&w, "cal"); jw_arr_open(&w);
      for (int i = 0; i < 2; i++) { jw_obj_open(&w); jw_key(&w, "adc_mv"); jw_uint(&w, c->battery.adc_mv[i]); jw_key(&w, "true_mv"); jw_uint(&w, c->battery.true_mv[i]); jw_obj_close(&w); }
      jw_arr_close(&w);
    jw_obj_close(&w);
    jw_key(&w, "ble"); jw_obj_open(&w);
      jw_key(&w, "name"); jw_str(&w, c->ble.name);
      jw_key(&w, "adv_s"); jw_uint(&w, c->ble.adv_s);
    jw_obj_close(&w);
    jw_key(&w, "log"); jw_obj_open(&w); jw_key(&w, "fused_hz"); jw_uint(&w, c->log.fused_hz); jw_obj_close(&w);
    jw_key(&w, "gps"); jw_obj_open(&w); jw_key(&w, "dyn_model"); jw_uint(&w, c->gps.dyn_model); jw_key(&w, "rate_hz"); jw_uint(&w, c->gps.rate_hz); jw_obj_close(&w);
    jw_key(&w, "imu"); jw_obj_open(&w); jw_key(&w, "mot_thr"); jw_uint(&w, c->imu.mot_thr); jw_key(&w, "mot_dur_ms"); jw_uint(&w, c->imu.mot_dur_ms); jw_obj_close(&w);
    jw_obj_close(&w);
    return jw_overflow(&w) ? -1 : (int)jw_len(&w);
}
```

- [ ] **Step 6: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass. If the round-trip `EQUAL_MEMORY` fails, check that `cfg_defaults` zeroes the struct before filling (padding bytes must match) and that every field is emitted by `cfg_to_json`.

- [ ] **Step 7: Commit**

```bash
git add components/core/include/core/cfg.h components/core/config test/test_cfg.c test/CMakeLists.txt
git commit -m "feat(core): configuration struct, defaults, validation, JSON merge/emit"
```

---

### Task 10: Track database (`core/trk`) and bundled-table generator

**Files:**
- Create: `components/core/include/core/trk.h`, `components/core/tracks/trk.c`, `components/core/tracks/trk_json.c`, `components/core/tracks/trk_bundled.c` (generated)
- Create: `tools/tracks/gen_tracks.py`, `tools/tracks/killarney.json`, `tools/tracks/zwartkops.json`
- Create: `test/test_trk.c`
- Modify: `test/CMakeLists.txt`, spec §10.1 (add `flags` field, see step 3)

**Interfaces:**
- Consumes: `geo_dist_m`, `json`, `jw`.
- Produces (spec §10.1–10.2): `trk_pt_t`, `trk_line_t`, `trk_layout_t`, `trk_venue_t` (with `uint8_t flags`, `TRK_F_UNVERIFIED 0x01`), `extern const trk_venue_t trk_bundled[]; extern const uint16_t trk_bundled_count;`, `void trk_init(void)`, `const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out)`, `int trk_user_add(const trk_venue_t*)`, `int trk_user_count(void)`, `int trk_user_load(const uint8_t *blob, size_t n)`, `int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)`, `const trk_venue_t *trk_get(uint16_t venue_id)`, `int trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap)`, `int trk_to_json(const trk_venue_t*, char *out, size_t cap)`, `uint16_t trk_next_user_id(void)`.

- [ ] **Step 1: Write the failing test**

`test/test_trk.c`:
```c
#include "unity.h"
#include "core/trk.h"
#include "core/ses.h"
#include "core/consts.h"
#include <stdio.h>
#include <string.h>

void setUp(void) { trk_init(); }
void tearDown(void) {}

static void test_bundled_contains_killarney_with_four_layouts(void)
{
    const trk_venue_t *v = trk_get(6);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_STRING("Killarney", v->name);
    TEST_ASSERT_EQUAL_UINT8(4, v->n_layouts);
    TEST_ASSERT_EQUAL_INT8(1, v->layouts[0].dir_sign);
    TEST_ASSERT_EQUAL_INT8(-1, v->layouts[1].dir_sign);
    /* reverse shares the S/F line and has reversed sector order */
    TEST_ASSERT_EQUAL_DOUBLE(v->layouts[0].sf.p1.lat, v->layouts[1].sf.p1.lat);
    TEST_ASSERT_EQUAL_UINT8(v->layouts[0].n_sectors, v->layouts[1].n_sectors);
    TEST_ASSERT_EQUAL_DOUBLE(v->layouts[0].sectors[0].p1.lat, v->layouts[1].sectors[v->layouts[1].n_sectors - 1].p1.lat);
    TEST_ASSERT_TRUE(v->flags & TRK_F_UNVERIFIED);
}

static void test_nearest_inside_and_outside_radius(void)
{
    uint32_t d;
    const trk_venue_t *v = trk_find_nearest(-33.8567 + 0.01, 18.5170, &d);   /* ~1.1 km north */
    TEST_ASSERT_NOT_NULL(v); TEST_ASSERT_EQUAL_UINT16(6, v->id); TEST_ASSERT_UINT32_WITHIN(50, 1112, d);
    TEST_ASSERT_NULL(trk_find_nearest(-33.8567 + 0.03, 18.5170, &d));         /* ~3.3 km: outside 2 km radius */
}

static void test_user_venue_wins_on_id_clash_and_persists(void)
{
    static trk_venue_t u; memset(&u, 0, sizeof u);
    u.id = 6; strcpy(u.name, "Killarney (mine)"); u.lat = -33.8567; u.lon = 18.5170; u.radius_m = 2000; u.n_layouts = 1;
    u.layouts[0].id = 1; strcpy(u.layouts[0].name, "L1"); u.layouts[0].dir_sign = 1;
    u.layouts[0].sf.p1.lat = -33.8567; u.layouts[0].sf.p1.lon = 18.5170;
    u.layouts[0].sf.p2.lat = -33.8567; u.layouts[0].sf.p2.lon = 18.5173;    /* a real S/F line, not the degenerate default */
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&u));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    static uint8_t blob[16384]; size_t n;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob, sizeof blob, &n));
    trk_init();
    TEST_ASSERT_EQUAL_STRING("Killarney", trk_get(6)->name);
    TEST_ASSERT_EQUAL_INT(0, trk_user_load(blob, n));
    TEST_ASSERT_EQUAL_STRING("Killarney (mine)", trk_get(6)->name);
    TEST_ASSERT_EQUAL_UINT16(1000, trk_next_user_id());
}

static void test_user_store_is_bounded(void)
{
    static trk_venue_t u; memset(&u, 0, sizeof u); u.radius_m = 100; u.n_layouts = 1;
    u.layouts[0].id = 1; u.layouts[0].dir_sign = 1;
    u.layouts[0].sf.p1.lat = -26.001; u.layouts[0].sf.p1.lon = 28.0;
    u.layouts[0].sf.p2.lat = -26.001; u.layouts[0].sf.p2.lon = 28.0003;     /* a real S/F line, not the degenerate default */
    for (int i = 0; i < TRK_MAX_USER; i++) { u.id = (uint16_t)(1000 + i); TEST_ASSERT_EQUAL_INT(0, trk_user_add(&u)); }
    u.id = 1000 + TRK_MAX_USER;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&u));
    TEST_ASSERT_EQUAL_UINT16(1000 + TRK_MAX_USER, trk_next_user_id());
}

static void test_json_round_trip_with_same_and_reverse_expansion(void)
{
    const char *js =
        "{\"id\":1001,\"name\":\"Test\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"verified\":true,\"layouts\":["
        "{\"id\":1,\"name\":\"Full\",\"dir\":1,\"length_m\":2500,\"sf\":[[-26.001,28.0],[-26.001,28.0003]],"
        "\"sectors\":[[[-26.002,28.001],[-26.002,28.0013]],[[-26.003,28.002],[-26.003,28.0023]]]},"
        "{\"id\":2,\"name\":\"Full Reverse\",\"dir\":-1,\"length_m\":2500,\"sf\":\"same\",\"sectors\":\"reverse\"}]}";
    static trk_venue_t v; char err[64];
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v, js, strlen(js), err, sizeof err));
    TEST_ASSERT_EQUAL_UINT16(1001, v.id);
    TEST_ASSERT_FALSE(v.flags & TRK_F_UNVERIFIED);
    TEST_ASSERT_EQUAL_UINT8(2, v.n_layouts);
    TEST_ASSERT_EQUAL_DOUBLE(28.0003, v.layouts[1].sf.p2.lon);
    TEST_ASSERT_EQUAL_DOUBLE(-26.003, v.layouts[1].sectors[0].p1.lat);
    static char out[2048];
    int n = trk_to_json(&v, out, sizeof out);
    TEST_ASSERT_GREATER_THAN(0, n);
    static trk_venue_t v2;
    TEST_ASSERT_EQUAL_INT(0, trk_from_json(&v2, out, (size_t)n, err, sizeof err));
    TEST_ASSERT_EQUAL_MEMORY(&v, &v2, sizeof v);
}

static void test_json_rejects_bad_line(void)
{
    const char *js = "{\"id\":1001,\"name\":\"T\",\"lat\":0,\"lon\":0,\"radius_m\":100,\"layouts\":[{\"id\":1,\"name\":\"L\",\"dir\":1,\"sf\":[[0,0]]}]}";
    static trk_venue_t v; char err[64];
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, js, strlen(js), err, sizeof err));
}

/* a venue that passes trk_validate_venue; filled in place so the suite fits a 6 KB task stack */
static void mk_venue(trk_venue_t *v, uint16_t id)
{
    memset(v, 0, sizeof *v);
    v->id = id; strcpy(v->name, "User"); v->lat = -26.0; v->lon = 28.0; v->radius_m = 1500; v->n_layouts = 1;
    v->layouts[0].id = 1; strcpy(v->layouts[0].name, "Full"); v->layouts[0].dir_sign = 1;
    v->layouts[0].sf.p1.lat = -26.001; v->layouts[0].sf.p1.lon = 28.0;
    v->layouts[0].sf.p2.lat = -26.001; v->layouts[0].sf.p2.lon = 28.0003;
}

static void test_user_add_rejects_invalid_venue(void)
{
    static trk_venue_t v;
    mk_venue(&v, 1000);
    v.layouts[0].id = 0;                                         /* layout id must be non-zero */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());
    mk_venue(&v, 0);                                             /* venue id must be non-zero */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); v.radius_m = 50;                         /* radius out of range */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); v.n_layouts = TRK_MAX_LAYOUTS + 1;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); memset(v.name, 'x', sizeof v.name);      /* name not NUL-terminated */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); v.lat = 91.0;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); v.layouts[0].dir_sign = 0;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    mk_venue(&v, 1000); v.layouts[0].n_sectors = LAP_MAX_SECTORS + 1;
    TEST_ASSERT_EQUAL_INT(-1, trk_user_add(&v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                  /* nothing was stored */
    mk_venue(&v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));
}

static void test_user_blob_v2_rejects_bad_version_count_crc_and_venue(void)
{
    static trk_venue_t v;
    mk_venue(&v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));
    static uint8_t blob[16384]; size_t n;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob, sizeof blob, &n));
    TEST_ASSERT_EQUAL_UINT8(2, blob[0]);
    TEST_ASSERT_EQUAL_UINT(2 + sizeof(trk_venue_t) + 2, n);      /* version, count, venue, crc16 */

    static uint8_t copy[16384];
    memcpy(copy, blob, n);
    TEST_ASSERT_EQUAL_INT(0, trk_user_load(copy, n));            /* round trip */
    TEST_ASSERT_EQUAL_INT(1, trk_user_count());

    memcpy(copy, blob, n); copy[0] = 1;                          /* old version */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());

    memcpy(copy, blob, n); copy[1] = TRK_MAX_USER + 1;           /* count over capacity */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    memcpy(copy, blob, n);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n - 1));       /* size mismatch */
    memcpy(copy, blob, n); copy[40] ^= 0x01;                     /* one flipped payload bit */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    memcpy(copy, blob, n); copy[n - 1] ^= 0x80;                  /* flipped CRC byte */
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());

    /* a structurally impossible venue (bit rot that survives no CRC, so re-CRC it) */
    static trk_venue_t bad; mk_venue(&bad, 1000); bad.n_layouts = 200;
    memcpy(copy, blob, n);
    memcpy(copy + 2, &bad, sizeof bad);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));           /* CRC catches it first */
    trk_init();
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob, sizeof blob, &n));
    memcpy(copy, blob, n); memcpy(copy + 2, &bad, sizeof bad);
    uint16_t crc = ses_crc16(copy, n - 2);                        /* recompute so only validation can reject */
    copy[n - 2] = (uint8_t)crc; copy[n - 1] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());                  /* store left empty */
    TEST_ASSERT_NULL(trk_get(1000));
}

static void test_user_blob_rejects_sub_metre_gate_line(void)
{
    static trk_venue_t v;
    mk_venue(&v, 1000);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));
    static uint8_t blob[sizeof(trk_venue_t) + 16]; size_t n;    /* one venue's worth, not the 16 KB multi-venue headroom */
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob, sizeof blob, &n));

    /* structurally valid except the S/F line is under the 1 m gate rule (trk_from_json's rule,
     * shared via trk_validate_venue -- the loader must enforce it too) */
    static trk_venue_t degenerate;
    mk_venue(&degenerate, 1000);
    degenerate.layouts[0].sf.p2.lat = degenerate.layouts[0].sf.p1.lat;
    degenerate.layouts[0].sf.p2.lon = degenerate.layouts[0].sf.p1.lon;
    static uint8_t copy[sizeof(trk_venue_t) + 16];
    memcpy(copy, blob, n);
    memcpy(copy + 2, &degenerate, sizeof degenerate);
    uint16_t crc = ses_crc16(copy, n - 2);                        /* recompute so only validation can reject */
    copy[n - 2] = (uint8_t)crc; copy[n - 1] = (uint8_t)(crc >> 8);
    TEST_ASSERT_EQUAL_INT(-1, trk_user_load(copy, n));
    TEST_ASSERT_EQUAL_INT(0, trk_user_count());
    TEST_ASSERT_NULL(trk_get(1000));
}

/* Same named fields as mk_venue, built on top of a struct pre-filled with `fill` so any byte the
 * assignments below do not touch (compiler padding between fields) keeps the fill pattern instead
 * of being zero, unlike mk_venue which memsets to 0 first. */
static void mk_dirty_venue(trk_venue_t *v, uint16_t id, uint8_t fill)
{
    memset(v, (int)fill, sizeof *v);
    v->id = id;
    memset(v->name, 0, sizeof v->name); strcpy(v->name, "User");
    v->lat = -26.0; v->lon = 28.0; v->radius_m = 1500; v->flags = 0; v->n_layouts = 1;
    trk_layout_t *L = &v->layouts[0];
    L->id = 1;
    memset(L->name, 0, sizeof L->name); strcpy(L->name, "Full");
    L->dir_sign = 1; L->n_sectors = 0; L->length_m = 0;
    L->sf.p1.lat = -26.001; L->sf.p1.lon = 28.0;
    L->sf.p2.lat = -26.001; L->sf.p2.lon = 28.0003;
}

static void test_save_produces_identical_blobs_regardless_of_padding_garbage(void)
{
    /* a and b are used one at a time (never simultaneously live), so one static struct -- sized and
     * kept off the stack for the same 6 KB task-stack reason as the rest of this suite -- is reused
     * for both fill patterns instead of allocating two. */
    static trk_venue_t v;

    trk_init();
    mk_dirty_venue(&v, 1000, 0xAA);
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));            /* struct assignment carries v's padding into the store */
    static uint8_t blob_a[sizeof(trk_venue_t) + 16]; size_t n_a;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob_a, sizeof blob_a, &n_a));

    trk_init();
    mk_dirty_venue(&v, 1000, 0x55);                         /* same fields, different padding garbage */
    TEST_ASSERT_EQUAL_INT(0, trk_user_add(&v));
    static uint8_t blob_b[sizeof(trk_venue_t) + 16]; size_t n_b;
    TEST_ASSERT_EQUAL_INT(0, trk_user_save(blob_b, sizeof blob_b, &n_b));

    TEST_ASSERT_EQUAL_UINT(n_a, n_b);
    TEST_ASSERT_EQUAL_MEMORY(blob_a, blob_b, n_a);          /* including the CRC: identical bytes throughout */
}

static void test_json_rejects_degenerate_line_and_duplicate_layout_ids(void)
{
    static trk_venue_t v; char err[64];
    /* the two S/F endpoints are the same point */
    const char *same_pt =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.0,28.0],[-26.0,28.0]]}]}";
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, same_pt, strlen(same_pt), err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    /* 0.9 m apart: still under MIN_GATE_LEN_M */
    const char *too_short =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.0,28.0],[-26.0000081,28.0]]}]}";
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, too_short, strlen(too_short), err, sizeof err));
    /* two layouts sharing an id */
    const char *dup =
        "{\"id\":1001,\"name\":\"T\",\"lat\":-26.0,\"lon\":28.0,\"radius_m\":1500,\"layouts\":["
        "{\"id\":1,\"name\":\"F\",\"dir\":1,\"sf\":[[-26.001,28.0],[-26.001,28.0003]]},"
        "{\"id\":1,\"name\":\"R\",\"dir\":-1,\"sf\":\"same\"}]}";
    err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, dup, strlen(dup), err, sizeof err));
    TEST_ASSERT_EQUAL_STRING("duplicate layout id", err);
}

static void test_json_rejects_document_deeper_than_the_depth_cap(void)
{
    static char js[512]; int p = 0;
    p += snprintf(js + p, sizeof js - (size_t)p, "{\"z\":");
    for (int i = 0; i < 40; i++) js[p++] = '[';
    for (int i = 0; i < 40; i++) js[p++] = ']';
    p += snprintf(js + p, sizeof js - (size_t)p, ",\"id\":1000,\"name\":\"X\",\"lat\":-26.0,\"lon\":28.0,"
                  "\"radius_m\":1500,\"layouts\":[{\"id\":1,\"name\":\"F\",\"dir\":1,"
                  "\"sf\":[[-26.001,28.0],[-26.001,28.0003]]}]}");
    static trk_venue_t v; char err[64]; err[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, trk_from_json(&v, js, (size_t)p, err, sizeof err));
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bundled_contains_killarney_with_four_layouts);
    RUN_TEST(test_nearest_inside_and_outside_radius);
    RUN_TEST(test_user_venue_wins_on_id_clash_and_persists);
    RUN_TEST(test_user_store_is_bounded);
    RUN_TEST(test_json_round_trip_with_same_and_reverse_expansion);
    RUN_TEST(test_json_rejects_bad_line);
    RUN_TEST(test_user_add_rejects_invalid_venue);
    RUN_TEST(test_user_blob_v2_rejects_bad_version_count_crc_and_venue);
    RUN_TEST(test_user_blob_rejects_sub_metre_gate_line);
    RUN_TEST(test_save_produces_identical_blobs_regardless_of_padding_garbage);
    RUN_TEST(test_json_rejects_degenerate_line_and_duplicate_layout_ids);
    RUN_TEST(test_json_rejects_document_deeper_than_the_depth_cap);
    return UNITY_END();
}
```

Add `add_core_test(test_trk)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/trk.h: No such file`.

- [ ] **Step 3: Record the `flags` field in the spec**

Edit `docs/superpowers/specs/2026-09-14-lap-timer-design.md` §10.1: in the `trk_venue_t` listing, add the line `    uint8_t    flags;              /* TRK_F_UNVERIFIED = 0x01: S/F not yet confirmed on site; UI appends "?" */` after `radius_m`, and in §10.2 add the sentence: `"verified": false marks a venue whose lines were placed from map data; the generator sets TRK_F_UNVERIFIED and the UI shows the venue name with a trailing "?".`

- [ ] **Step 4: Write the header**

`components/core/include/core/trk.h`:
```c
#ifndef CORE_TRK_H
#define CORE_TRK_H
#include <stdint.h>
#include <stddef.h>
#include "core/consts.h"
#include "core/types.h"

#define TRK_F_UNVERIFIED 0x01
#define TRK_USER_ID_BASE 1000

typedef struct { double lat, lon; } trk_pt_t;
typedef struct { trk_pt_t p1, p2; } trk_line_t;      /* p1 = left end, p2 = right end in driving direction */
typedef struct {
    uint16_t   id;
    char       name[24];
    trk_line_t sf;
    int8_t     dir_sign;                             /* +1 / -1 (§6.4) */
    uint8_t    n_sectors;                            /* sector gates, excluding S/F */
    trk_line_t sectors[LAP_MAX_SECTORS];
    uint32_t   length_m;
} trk_layout_t;
typedef struct {
    uint16_t     id;                                 /* bundled 1..999, user 1000+ */
    char         name[32];
    double       lat, lon;
    uint32_t     radius_m;
    uint8_t      flags;                              /* TRK_F_* */
    uint8_t      n_layouts;
    trk_layout_t layouts[TRK_MAX_LAYOUTS];
} trk_venue_t;

extern const trk_venue_t trk_bundled[];
extern const uint16_t    trk_bundled_count;

/* The user store is module-static and not protected by a lock (see trk.c). */
void               trk_init(void);                                   /* clears the user store */
int                trk_validate_venue(const trk_venue_t *v);         /* 0 ok / -1 structurally invalid */
const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out);   /* within radius; user beats bundled on id clash */
const trk_venue_t *trk_get(uint16_t venue_id);
int                trk_user_add(const trk_venue_t *v);              /* replaces same id; -1 if full or invalid */
int                trk_user_count(void);
uint16_t           trk_next_user_id(void);
/* Blob v2: u8 version(2) | u8 count | trk_venue_t[count] | u16 crc16 LE. Load rejects a wrong
 * version, count, size or CRC and any venue failing trk_validate_venue, leaving the store empty. */
int                trk_user_load(const uint8_t *blob, size_t n);
int                trk_user_save(uint8_t *blob, size_t cap, size_t *n_out);
int                trk_from_json(trk_venue_t *out, const char *json, size_t n, char *err, size_t err_cap);
int                trk_to_json(const trk_venue_t *v, char *out, size_t cap);
#endif
```

- [ ] **Step 5: Implement the store and lookup**

`components/core/tracks/trk.c`:
```c
#include "core/trk.h"
#include "core/geo.h"
#include "core/ses.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Not reentrant: the user store below is module-static, shared by every trk_* entry point.
 * Only the conn task adds/loads/saves venues and only the pipeline task reads them, and the two
 * never overlap (an upload is applied between sessions), so no lock is taken. */
static trk_venue_t user[TRK_MAX_USER];
static uint8_t     user_n;

#define BLOB_VERSION 2

void trk_init(void) { user_n = 0; memset(user, 0, sizeof user); }

int trk_user_count(void) { return user_n; }

static bool pt_finite(const trk_pt_t *p) { return isfinite(p->lat) && isfinite(p->lon); }
static bool line_finite(const trk_line_t *l) { return pt_finite(&l->p1) && pt_finite(&l->p2); }

#define MIN_GATE_LEN_M 1.0        /* a line shorter than this cannot define a crossing direction (§6.4) */
/* Shared by both entry points that can install a venue (trk_from_json, via the final
 * trk_validate_venue() call, and trk_user_load()/trk_user_add(), via this function directly), so
 * the two never disagree about what a valid gate line is. */
static bool line_ok(const trk_line_t *l) { return geo_dist_m(l->p1.lat, l->p1.lon, l->p2.lat, l->p2.lon) >= MIN_GATE_LEN_M; }

int trk_validate_venue(const trk_venue_t *v)
{
    if (v->id == 0) return -1;
    if (v->radius_m < 100 || v->radius_m > 50000) return -1;
    if (v->n_layouts < 1 || v->n_layouts > TRK_MAX_LAYOUTS) return -1;
    if (v->name[sizeof v->name - 1] != '\0') return -1;
    if (!isfinite(v->lat) || !isfinite(v->lon)) return -1;
    if (v->lat < -90.0 || v->lat > 90.0 || v->lon < -180.0 || v->lon > 180.0) return -1;
    for (uint8_t i = 0; i < v->n_layouts; i++) {
        const trk_layout_t *L = &v->layouts[i];
        if (L->id == 0) return -1;
        if (L->dir_sign != 1 && L->dir_sign != -1) return -1;
        if (L->n_sectors > LAP_MAX_SECTORS) return -1;
        if (L->name[sizeof L->name - 1] != '\0') return -1;
        if (!line_finite(&L->sf) || !line_ok(&L->sf)) return -1;
        for (uint8_t s = 0; s < L->n_sectors; s++) if (!line_finite(&L->sectors[s]) || !line_ok(&L->sectors[s])) return -1;
    }
    return 0;
}

static const trk_venue_t *user_get(uint16_t id)
{
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == id) return &user[i];
    return NULL;
}

const trk_venue_t *trk_get(uint16_t venue_id)
{
    const trk_venue_t *u = user_get(venue_id);
    if (u) return u;
    for (uint16_t i = 0; i < trk_bundled_count; i++) if (trk_bundled[i].id == venue_id) return &trk_bundled[i];
    return NULL;
}

int trk_user_add(const trk_venue_t *v)
{
    if (trk_validate_venue(v) != 0) return -1;
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id == v->id) { user[i] = *v; return 0; }
    if (user_n >= TRK_MAX_USER) return -1;
    user[user_n++] = *v;
    return 0;
}

uint16_t trk_next_user_id(void)
{
    uint16_t id = TRK_USER_ID_BASE;
    for (uint8_t i = 0; i < user_n; i++) if (user[i].id >= id) id = (uint16_t)(user[i].id + 1);
    return id;
}

static void consider(const trk_venue_t *v, double lat, double lon, const trk_venue_t **best, double *best_d)
{
    const trk_venue_t *u = user_get(v->id);
    if (u && u != v) return;                                  /* bundled entry shadowed by a user entry */
    double d = geo_dist_m(lat, lon, v->lat, v->lon);
    if (d <= (double)v->radius_m && d < *best_d) { *best = v; *best_d = d; }
}

const trk_venue_t *trk_find_nearest(double lat, double lon, uint32_t *dist_m_out)
{
    const trk_venue_t *best = NULL; double best_d = 1e12;
    for (uint8_t i = 0; i < user_n; i++) consider(&user[i], lat, lon, &best, &best_d);
    for (uint16_t i = 0; i < trk_bundled_count; i++) consider(&trk_bundled[i], lat, lon, &best, &best_d);
    if (best && dist_m_out) *dist_m_out = (uint32_t)best_d;
    return best;
}

/* Blob v2: u8 version=2 | u8 count | trk_venue_t[count] | u16 crc16 (LE) over every preceding byte.
 * The struct is copied raw, so the blob is only valid for this build; the CRC catches NVS bit rot
 * and every venue is re-validated before it reaches the store. Any failure leaves the store empty. */
int trk_user_load(const uint8_t *blob, size_t n)
{
    trk_init();
    if (n < 4 || blob[0] != BLOB_VERSION) return -1;
    uint8_t cnt = blob[1];
    if (cnt > TRK_MAX_USER) return -1;
    size_t need = 2 + (size_t)cnt * sizeof(trk_venue_t) + 2;
    if (n != need) return -1;
    uint16_t want = (uint16_t)(blob[need - 2] | ((uint16_t)blob[need - 1] << 8));
    if (ses_crc16(blob, need - 2) != want) return -1;
    for (uint8_t i = 0; i < cnt; i++) {
        memcpy(&user[i], blob + 2 + (size_t)i * sizeof(trk_venue_t), sizeof(trk_venue_t));
        if (trk_validate_venue(&user[i]) != 0) { trk_init(); return -1; }
    }
    user_n = cnt;
    return 0;
}

#define PUT_FIELD(dst, T, f, src) memcpy((dst) + offsetof(T, f), &(src)->f, sizeof (src)->f)

/* Field-by-field copy into an already-zeroed destination: struct assignment (or a raw memcpy of the
 * whole struct) also copies the source's compiler-inserted padding bytes verbatim, which the
 * language never promises are zero, so two structurally identical venues could otherwise CRC
 * differently (§10.1's blob is declared build-specific but should still be deterministic within one
 * build). dst points directly at the destination blob bytes (uint8_t *, possibly unaligned), so each
 * field is written with memcpy at its offsetof rather than through a typed pointer; every named field
 * is written explicitly, and nothing else touches dst, so the gaps between fields stay at the memset
 * zero. */
static void canon_venue(uint8_t *dst, const trk_venue_t *src)
{
    memset(dst, 0, sizeof *src);
    PUT_FIELD(dst, trk_venue_t, id, src);
    PUT_FIELD(dst, trk_venue_t, name, src);
    PUT_FIELD(dst, trk_venue_t, lat, src);
    PUT_FIELD(dst, trk_venue_t, lon, src);
    PUT_FIELD(dst, trk_venue_t, radius_m, src);
    PUT_FIELD(dst, trk_venue_t, flags, src);
    PUT_FIELD(dst, trk_venue_t, n_layouts, src);
    /* Only the active layouts/sectors (src has already passed trk_validate_venue, so n_layouts and
     * every n_sectors are in range) are copied; slots beyond them are left at the memset zero rather
     * than carrying through whatever unused array content src happened to hold. */
    for (uint8_t i = 0; i < src->n_layouts && i < TRK_MAX_LAYOUTS; i++) {
        const trk_layout_t *sl = &src->layouts[i];
        uint8_t *ld = dst + offsetof(trk_venue_t, layouts) + (size_t)i * sizeof(trk_layout_t);
        PUT_FIELD(ld, trk_layout_t, id, sl);
        PUT_FIELD(ld, trk_layout_t, name, sl);
        PUT_FIELD(ld, trk_layout_t, sf, sl);                    /* trk_line_t is four packed doubles: no internal padding */
        PUT_FIELD(ld, trk_layout_t, dir_sign, sl);
        PUT_FIELD(ld, trk_layout_t, n_sectors, sl);
        for (uint8_t s = 0; s < sl->n_sectors && s < LAP_MAX_SECTORS; s++) {
            uint8_t *sd = ld + offsetof(trk_layout_t, sectors) + (size_t)s * sizeof(trk_line_t);
            memcpy(sd, &sl->sectors[s], sizeof sl->sectors[s]);
        }
        PUT_FIELD(ld, trk_layout_t, length_m, sl);
    }
}

int trk_user_save(uint8_t *blob, size_t cap, size_t *n_out)
{
    size_t need = 2 + (size_t)user_n * sizeof(trk_venue_t) + 2;
    if (cap < need) return -1;
    blob[0] = BLOB_VERSION; blob[1] = user_n;
    for (uint8_t i = 0; i < user_n; i++) canon_venue(blob + 2 + (size_t)i * sizeof(trk_venue_t), &user[i]);
    uint16_t crc = ses_crc16(blob, need - 2);
    blob[need - 2] = (uint8_t)crc; blob[need - 1] = (uint8_t)(crc >> 8);
    *n_out = need;
    return 0;
}
```

(The user blob is a raw struct image, which is acceptable because it is written and read by the same firmware build and CRC-protected by the storage layer in plan 03; a version byte guards future layout changes.)

- [ ] **Step 6: Implement the JSON codec**

`components/core/tracks/trk_json.c`:
```c
#include "core/trk.h"
#include "core/json.h"
#include "core/jw.h"
#include <string.h>
#include <stdio.h>

#define MAX_TOKS 512

static int fail(char *err, size_t cap, const char *m) { if (err && cap) { strncpy(err, m, cap - 1); err[cap - 1] = '\0'; } return -1; }

static bool get_pt(const char *js, const jsmntok_t *toks, int ntoks, int arr, trk_pt_t *out)
{
    if (arr < 0 || arr + 2 >= ntoks || toks[arr].type != JSMN_ARRAY || toks[arr].size != 2) return false;
    return json_tok_double(js, &toks[arr + 1], &out->lat) && json_tok_double(js, &toks[arr + 2], &out->lon);
}
static bool get_line(const char *js, const jsmntok_t *toks, int ntoks, int arr, trk_line_t *out)
{
    if (arr < 0 || arr >= ntoks || toks[arr].type != JSMN_ARRAY || toks[arr].size != 2) return false;
    int p1 = arr + 1, p2 = json_skip(toks, ntoks, p1);
    /* Degenerate/too-short lines (§6.4 needs a gate direction) are rejected once, by the shared
     * trk_validate_venue() call trk_from_json makes at the end -- not duplicated here. */
    return get_pt(js, toks, ntoks, p1, &out->p1) && get_pt(js, toks, ntoks, p2, &out->p2);
}

/* Not reentrant: static token array (single caller task). */
int trk_from_json(trk_venue_t *v, const char *json, size_t n, char *err, size_t err_cap)
{
    static jsmntok_t toks[MAX_TOKS];
    int cnt = json_parse(json, n, toks, MAX_TOKS);
    if (cnt < 1 || toks[0].type != JSMN_OBJECT) return fail(err, err_cap, "malformed json");
    memset(v, 0, sizeof *v);
    int t; int64_t iv; bool bv;
    if ((t = json_obj_get(json, toks, cnt, 0, "id")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 1 || iv > 65535) return fail(err, err_cap, "id");
    v->id = (uint16_t)iv;
    if ((t = json_obj_get(json, toks, cnt, 0, "name")) < 0) return fail(err, err_cap, "name");
    json_tok_str(json, &toks[t], v->name, sizeof v->name);
    if ((t = json_obj_get(json, toks, cnt, 0, "lat")) < 0 || !json_tok_double(json, &toks[t], &v->lat)) return fail(err, err_cap, "lat");
    if ((t = json_obj_get(json, toks, cnt, 0, "lon")) < 0 || !json_tok_double(json, &toks[t], &v->lon)) return fail(err, err_cap, "lon");
    if ((t = json_obj_get(json, toks, cnt, 0, "radius_m")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 100 || iv > 50000) return fail(err, err_cap, "radius_m");
    v->radius_m = (uint32_t)iv;
    v->flags = TRK_F_UNVERIFIED;
    if ((t = json_obj_get(json, toks, cnt, 0, "verified")) >= 0 && json_tok_bool(json, &toks[t], &bv) && bv) v->flags = 0;
    int la = json_obj_get(json, toks, cnt, 0, "layouts");
    if (la < 0 || toks[la].type != JSMN_ARRAY || toks[la].size < 1 || toks[la].size > TRK_MAX_LAYOUTS) return fail(err, err_cap, "layouts");
    int li = la + 1;
    for (int k = 0; k < toks[la].size; k++) {
        trk_layout_t *L = &v->layouts[k];
        if (toks[li].type != JSMN_OBJECT) return fail(err, err_cap, "layout object");
        if ((t = json_obj_get(json, toks, cnt, li, "id")) < 0 || !json_tok_int(json, &toks[t], &iv) || iv < 1 || iv > 65535) return fail(err, err_cap, "layout id");
        for (int prev = 0; prev < k; prev++) if (v->layouts[prev].id == (uint16_t)iv) return fail(err, err_cap, "duplicate layout id");
        L->id = (uint16_t)iv;
        if ((t = json_obj_get(json, toks, cnt, li, "name")) < 0) return fail(err, err_cap, "layout name");
        json_tok_str(json, &toks[t], L->name, sizeof L->name);
        if ((t = json_obj_get(json, toks, cnt, li, "dir")) < 0 || !json_tok_int(json, &toks[t], &iv) || (iv != 1 && iv != -1)) return fail(err, err_cap, "dir");
        L->dir_sign = (int8_t)iv;
        if ((t = json_obj_get(json, toks, cnt, li, "length_m")) >= 0 && json_tok_int(json, &toks[t], &iv) && iv >= 0) L->length_m = (uint32_t)iv;
        t = json_obj_get(json, toks, cnt, li, "sf");
        if (t < 0) return fail(err, err_cap, "sf");
        if (json_tok_eq(json, &toks[t], "same")) { if (k == 0) return fail(err, err_cap, "sf same on first"); L->sf = v->layouts[0].sf; }
        else if (!get_line(json, toks, cnt, t, &L->sf)) return fail(err, err_cap, "sf line");
        t = json_obj_get(json, toks, cnt, li, "sectors");
        if (t < 0) { L->n_sectors = 0; }
        else if (json_tok_eq(json, &toks[t], "reverse")) {
            if (k == 0) return fail(err, err_cap, "sectors reverse on first");
            L->n_sectors = v->layouts[0].n_sectors;
            for (uint8_t s = 0; s < L->n_sectors; s++) L->sectors[s] = v->layouts[0].sectors[L->n_sectors - 1 - s];
        } else {
            if (toks[t].type != JSMN_ARRAY || toks[t].size > LAP_MAX_SECTORS) return fail(err, err_cap, "sectors");
            L->n_sectors = (uint8_t)toks[t].size;
            int si = t + 1;
            for (uint8_t s = 0; s < L->n_sectors; s++) { if (!get_line(json, toks, cnt, si, &L->sectors[s])) return fail(err, err_cap, "sector line"); si = json_skip(toks, cnt, si); }
        }
        v->n_layouts++;
        li = json_skip(toks, cnt, li);
    }
    if (trk_validate_venue(v) != 0) { memset(v, 0, sizeof *v); return fail(err, err_cap, "invalid venue"); }
    return 0;
}

static void put_pt(jw_t *w, const trk_pt_t *p) { jw_arr_open(w); jw_double(w, p->lat, 7); jw_double(w, p->lon, 7); jw_arr_close(w); }
static void put_line(jw_t *w, const trk_line_t *l) { jw_arr_open(w); put_pt(w, &l->p1); put_pt(w, &l->p2); jw_arr_close(w); }

int trk_to_json(const trk_venue_t *v, char *out, size_t cap)
{
    jw_t w; jw_init(&w, out, cap);
    jw_obj_open(&w);
    jw_key(&w, "id"); jw_uint(&w, v->id);
    jw_key(&w, "name"); jw_str(&w, v->name);
    jw_key(&w, "lat"); jw_double(&w, v->lat, 7);
    jw_key(&w, "lon"); jw_double(&w, v->lon, 7);
    jw_key(&w, "radius_m"); jw_uint(&w, v->radius_m);
    jw_key(&w, "verified"); jw_bool(&w, !(v->flags & TRK_F_UNVERIFIED));
    jw_key(&w, "layouts"); jw_arr_open(&w);
    for (uint8_t k = 0; k < v->n_layouts; k++) {
        const trk_layout_t *L = &v->layouts[k];
        jw_obj_open(&w);
        jw_key(&w, "id"); jw_uint(&w, L->id);
        jw_key(&w, "name"); jw_str(&w, L->name);
        jw_key(&w, "dir"); jw_int(&w, L->dir_sign);
        jw_key(&w, "length_m"); jw_uint(&w, L->length_m);
        jw_key(&w, "sf"); put_line(&w, &L->sf);
        jw_key(&w, "sectors"); jw_arr_open(&w);
        for (uint8_t s = 0; s < L->n_sectors; s++) put_line(&w, &L->sectors[s]);
        jw_arr_close(&w);
        jw_obj_close(&w);
    }
    jw_arr_close(&w);
    jw_obj_close(&w);
    return jw_overflow(&w) ? -1 : (int)jw_len(&w);
}
```

Note: `jw_double(…, 7)` prints 7 decimals; `strtod` parses them back to the same double for the coordinates used here, which the round-trip test relies on (values with ≤ 7 decimals).

- [ ] **Step 7: Write the generator and two venue files**

`tools/tracks/gen_tracks.py`:
```python
#!/usr/bin/env python3
"""Generate components/core/tracks/trk_bundled.c from tools/tracks/*.json (spec §10.2)."""
import json, sys, argparse, math, pathlib

MAX_LAYOUTS, MAX_SECTORS = 8, 8

def dist_m(a, b):
    R = 6371008.8
    p1, p2 = math.radians(a[0]), math.radians(b[0])
    dp, dl = p2 - p1, math.radians(b[1] - a[1])
    h = math.sin(dp/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dl/2)**2
    return 2*R*math.asin(math.sqrt(h))

def check_line(line, what, name):
    assert isinstance(line, list) and len(line) == 2, f"{name}: {what} must be [[lat,lon],[lat,lon]]"
    for p in line:
        assert isinstance(p, list) and len(p) == 2, f"{name}: {what} point"
    L = dist_m(line[0], line[1])
    # a line whose endpoints coincide has no direction, so §6.4 can never detect a crossing
    assert L >= 1.0, f"{name}: {what} endpoints only {L:.2f} m apart (degenerate line)"
    assert 10 <= L <= 60, f"{name}: {what} length {L:.1f} m outside 10–60 m"

def expand(v):
    assert 1 <= v["id"] <= 999, f"{v['name']}: bundled id must be 1..999"
    assert 1 <= len(v["layouts"]) <= MAX_LAYOUTS
    lids = [L["id"] for L in v["layouts"]]
    assert len(lids) == len(set(lids)), f"{v['name']}: duplicate layout ids {lids}"
    first = v["layouts"][0]
    for k, L in enumerate(v["layouts"]):
        assert L["dir"] in (1, -1)
        if L["sf"] == "same":
            assert k > 0; L["sf"] = first["sf"]
        check_line(L["sf"], "sf", v["name"])
        secs = L.get("sectors", [])
        if secs == "reverse":
            assert k > 0; secs = list(reversed(first.get("sectors", [])))
        assert len(secs) <= MAX_SECTORS
        for s in secs: check_line(s, "sector", v["name"])
        L["sectors"] = secs
        L.setdefault("length_m", 0)
    return v

def c_str(s, n):
    assert len(s) < n, f"string too long: {s}"
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

def c_line(l):
    return f"{{ {{ {l[0][0]!r}, {l[0][1]!r} }}, {{ {l[1][0]!r}, {l[1][1]!r} }} }}"

def emit(venues, out):
    w = out.write
    w("/* GENERATED by tools/tracks/gen_tracks.py — do not edit. */\n#include \"core/trk.h\"\n\n")
    w("const trk_venue_t trk_bundled[] = {\n")
    for v in venues:
        flags = 0 if v.get("verified", False) else 1
        w(f"  {{ .id = {v['id']}, .name = {c_str(v['name'], 32)}, .lat = {v['lat']!r}, .lon = {v['lon']!r}, .radius_m = {v.get('radius_m', 2000)}, .flags = {flags}, .n_layouts = {len(v['layouts'])}, .layouts = {{\n")
        for L in v["layouts"]:
            w(f"    {{ .id = {L['id']}, .name = {c_str(L['name'], 24)}, .sf = {c_line(L['sf'])}, .dir_sign = {L['dir']}, .n_sectors = {len(L['sectors'])}, .sectors = {{\n")
            for s in L["sectors"]:
                w(f"      {c_line(s)},\n")
            w(f"    }}, .length_m = {L['length_m']} }},\n")
        w("  } },\n")
    w("};\n")
    w(f"const uint16_t trk_bundled_count = {len(venues)};\n")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    venues = [expand(json.load(open(f))) for f in sorted(a.files)]
    ids = [v["id"] for v in venues]
    assert len(ids) == len(set(ids)), "duplicate venue ids"
    with open(a.output, "w") as out:
        emit(venues, out)
    print(f"wrote {a.output}: {len(venues)} venues")

if __name__ == "__main__":
    main()
```

`tools/tracks/killarney.json` (coordinates placed from map data; `"verified": false` until confirmed on site):
```json
{
  "id": 6, "name": "Killarney", "lat": -33.8567, "lon": 18.5170, "radius_m": 2000, "verified": false,
  "layouts": [
    { "id": 1, "name": "Full", "dir": 1, "length_m": 3267,
      "sf": [[-33.85780, 18.51520], [-33.85795, 18.51535]],
      "sectors": [ [[-33.85300, 18.51900], [-33.85315, 18.51925]], [[-33.85950, 18.52100], [-33.85965, 18.52080]] ] },
    { "id": 2, "name": "Full Reverse", "dir": -1, "length_m": 3267, "sf": "same", "sectors": "reverse" },
    { "id": 3, "name": "Short", "dir": 1, "length_m": 2500,
      "sf": [[-33.85780, 18.51520], [-33.85795, 18.51535]],
      "sectors": [ [[-33.85300, 18.51900], [-33.85315, 18.51925]] ] },
    { "id": 4, "name": "Short Reverse", "dir": -1, "length_m": 2500,
      "sf": [[-33.85780, 18.51520], [-33.85795, 18.51535]],
      "sectors": [ [[-33.85300, 18.51900], [-33.85315, 18.51925]] ] }
  ]
}
```

`"sf": "same"` and `"sectors": "reverse"` always refer to the **first** layout (spec §10.2), which is why the Short Reverse layout lists its line and sector explicitly instead of using the shortcuts.

`tools/tracks/zwartkops.json`:
```json
{
  "id": 2, "name": "Zwartkops", "lat": -25.8103, "lon": 28.1497, "radius_m": 2000, "verified": false,
  "layouts": [
    { "id": 1, "name": "Full", "dir": 1, "length_m": 2400,
      "sf": [[-25.80950, 28.14850], [-25.80965, 28.14870]],
      "sectors": [ [[-25.81200, 28.15100], [-25.81215, 28.15080]] ] }
  ]
}
```

Generate:
```bash
python3 tools/tracks/gen_tracks.py tools/tracks/killarney.json tools/tracks/zwartkops.json -o components/core/tracks/trk_bundled.c
```
Expected: `wrote components/core/tracks/trk_bundled.c: 2 venues`. If a `length … outside 10–60 m` assertion fires, adjust the second point of that line so the two endpoints are 15–30 m apart (0.0001° latitude ≈ 11 m; 0.0001° longitude ≈ 9 m at −34°).

- [ ] **Step 8: Run to verify it passes**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass. The generated file is compiled through the core glob; re-run `cmake -S test -B test/build` after generating so the glob picks it up.

- [ ] **Step 9: Commit**

```bash
git add components/core/include/core/trk.h components/core/tracks tools/tracks test/test_trk.c test/CMakeLists.txt docs/superpowers/specs/2026-09-14-lap-timer-design.md
git commit -m "feat(core): track database — bundled table generator, user store, JSON codec"
```

---

### Task 11: VBO exporter (`core/exp` part 1)

**Files:**
- Create: `components/core/include/core/exp.h`, `components/core/export/exp.c`, `components/core/export/exp_vbo.c`, `test/test_exp_vbo.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ses_decode_fix`, `ses_decode_fused`, `ses_fused_state_on_fix`, `tb_days_from_civil` (inverse: civil from days, added here as `exp_civil_from_days`).
- Produces (spec §5.2, §14): `exp_meta_t`, `exp_t`, `EXP_VBO / EXP_NMEA / EXP_JSON`, `int exp_open(exp_t*, uint8_t fmt, const exp_meta_t*)`, `int exp_feed(exp_t*, uint8_t type, const uint8_t *payload, uint8_t len)` → `0` consumed, `EXP_FULL` (=1) not consumed (pull first, then feed the same frame again), `-1` error; `int exp_pull(exp_t*, uint8_t *out, size_t cap, size_t *n_out)`; `int exp_finish(exp_t*)` (emits any trailer; then pull until empty). Pull-based streaming with a 1024-byte internal window (§14 "no whole file in RAM").

- [ ] **Step 1: Write the failing test**

`test/test_exp_vbo.c`:
```c
#include "unity.h"
#include "core/exp.h"
#include "core/ses.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/* drain everything currently pullable into dst */
static size_t drain(exp_t *e, char *dst, size_t cap, size_t at)
{
    (void)cap;
    uint8_t chunk[128]; size_t n;
    while (exp_pull(e, chunk, sizeof chunk, &n) == 0 && n > 0) { memcpy(dst + at, chunk, n); at += n; }
    dst[at] = '\0';
    return at;
}

static int feed(exp_t *e, char *dst, size_t cap, size_t *at, uint8_t type, const uint8_t *frame, int frame_len)
{
    int r;
    while ((r = exp_feed(e, type, frame + 3, (uint8_t)(frame_len - SES_FRAME_OVERHEAD))) == EXP_FULL) *at = drain(e, dst, cap, *at);
    return r;
}

static void test_vbo_golden_single_fix_with_fused(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    strcpy(m.session_id, "S00042_001"); strcpy(m.fw, "v0.1.0"); strcpy(m.hwid, "moto_neo6m_epaper");
    strcpy(m.venue, "Killarney"); strcpy(m.layout, "Full");
    m.created_gps_us = 1789380900LL * 1000000LL;        /* 2026-09-14 10:15:00 UTC */
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_VBO, &m));
    char out[4096]; size_t at = 0; at = drain(&e, out, sizeof out, at);

    uint8_t fr[64];
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    ses_fused_state_t fu; ses_fused_state_init(&fu);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = m.created_gps_us; f.lat_e7 = -338567000; f.lon_e7 = 185170000; f.alt_mm = 45000; f.gspeed_mms = 34300; f.head_e5 = 9012000;
    f.hacc_mm = 2500; f.fix_type = 3; f.sats = 8; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    int n = ses_encode_fix(&fs, &f, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    ses_fused_state_on_fix(&fu, f.gps_us);
    fused_sample_t s; memset(&s, 0, sizeof s); s.gps_us = f.gps_us + 40000; s.g_lat = 0.12f; s.g_lon = -0.05f; s.lean_deg = 12.3f; s.yaw_dps = 5.2f;
    n = ses_encode_fused(&fu, &s, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    gps_fix_t f2 = f; f2.gps_us += 200000; f2.lat_e7 += 540;
    n = ses_encode_fix(&fs, &f2, fr, sizeof fr);
    TEST_ASSERT_EQUAL_INT(0, feed(&e, out, sizeof out, &at, fr[1], fr, n));
    TEST_ASSERT_EQUAL_INT(0, exp_finish(&e));
    at = drain(&e, out, sizeof out, at);

    const char *expected =
        "File created on 14/09/2026 at 10:15:00\r\n"
        "\r\n"
        "[header]\r\n"
        "satellites\r\ntime\r\nlatitude\r\nlongitude\r\nvelocity kmh\r\nheading\r\nheight\r\nlat_g\r\nlon_g\r\nlean\r\nyaw\r\n"
        "\r\n"
        "[channel units]\r\n"
        "\r\n"
        "[comments]\r\n"
        "LapTimer v0.1.0 (moto_neo6m_epaper)\r\n"
        "Session S00042_001\r\n"
        "Venue Killarney / Full\r\n"
        "\r\n"
        "[column names]\r\n"
        "sats time lat long velocity heading height lat_g lon_g lean yaw\r\n"
        "\r\n"
        "[data]\r\n"
        "008 101500.00 -2031.40200 -1111.02000 123.48 090.12 0045.00 +0.000 +0.000 +00.00 +00.00\r\n"
        "008 101500.20 -2031.39876 -1111.02000 123.48 090.12 0045.00 +0.120 -0.050 +12.30 +05.20\r\n";
    TEST_ASSERT_EQUAL_STRING(expected, out);
}

static void test_vbo_laptiming_section_when_sf_known(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    m.created_gps_us = 1789380900LL * 1000000LL; m.has_sf = 1;
    m.sf_lat1 = -33.8578; m.sf_lon1 = 18.5152; m.sf_lat2 = -33.85795; m.sf_lon2 = 18.51535;
    exp_t e; exp_open(&e, EXP_VBO, &m);
    char out[4096]; size_t at = drain(&e, out, sizeof out, 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "[laptiming]\r\nStart -1110.91200 -2031.46800 -1110.92100 -2031.47700\r\n"));
    (void)at;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_vbo_golden_single_fix_with_fused);
    RUN_TEST(test_vbo_laptiming_section_when_sf_known);
    return UNITY_END();
}
```

Add `add_core_test(test_exp_vbo)`.

Golden derivation, so the numbers can be checked by hand: latitude −33.8567° × 60 = −2031.402 minutes; the second fix adds 540 × 1e−7° = 0.000054° × 60 = 0.00324 min → −2031.39876. Longitude 18.517° × 60 = 1111.02, sign flipped (west positive) → −1111.02000. Speed 34300 mm/s × 3.6 / 1000 = 123.48 → `123.48` (34300 is a multiple of 10 so the cm/s delta encoding of the second row reproduces it exactly). Heading 90.12 → `090.12`. Height 45.00 → `0045.00`. Fused values are held from the latest FUSED before each fix; the first row has none, so zeros.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build test/build`
Expected: FAIL — `core/exp.h: No such file`.

- [ ] **Step 3: Write the header and the shared streaming core**

`components/core/include/core/exp.h`:
```c
#ifndef CORE_EXP_H
#define CORE_EXP_H
#include <stdint.h>
#include <stddef.h>
#include "core/ses.h"

enum { EXP_VBO = 1, EXP_NMEA = 2, EXP_JSON = 3 };
#define EXP_FULL 1
#define EXP_WINDOW 1024

/* char arrays carry one byte more than the matching wire field so the value is always NUL-terminated */
typedef struct {
    char    session_id[11];
    char    fw[17];
    char    hwid[25];
    char    venue[33];
    char    layout[25];
    int64_t created_gps_us;
    uint8_t has_sf;
    double  sf_lat1, sf_lon1, sf_lat2, sf_lon2;
} exp_meta_t;

typedef struct {
    uint8_t  fmt;
    uint8_t  finished;
    exp_meta_t meta;
    /* output window */
    uint8_t  win[EXP_WINDOW];
    size_t   win_len, win_pos;
    /* decoder state shared by formats */
    ses_fix_state_t   fix_st;
    ses_fused_state_t fus_st;
    fused_sample_t    held;            /* latest fused values, sample-and-hold */
    uint8_t           have_held;
    /* JSON summary state */
    uint16_t  laps, runs;
    uint8_t   json_stage;              /* 0 header pending, 1 in laps, 2 in runs */
    ses_hdr_t hdr;
    uint8_t   have_hdr;
    char      venue_name[33];
    uint8_t   run_pending;
    uint8_t   run_gate_idx;            /* DRAG_RUN emission resumes after EXP_FULL */
} exp_t;

int  exp_open(exp_t *e, uint8_t fmt, const exp_meta_t *meta);
int  exp_feed(exp_t *e, uint8_t type, const uint8_t *payload, uint8_t len);   /* 0 consumed, EXP_FULL retry after pull, -1 error */
int  exp_pull(exp_t *e, uint8_t *out, size_t cap, size_t *n_out);           /* 0 ok (n_out may be 0), -1 error */
int  exp_finish(exp_t *e);                                                   /* may return EXP_FULL: pull, then call again. Returns 0 (no-op) if already finished, -1 if a DRAG_RUN frame is mid-emission (re-feed it first). */

/* helpers shared by format implementations (internal) */
int  exp_win_free(const exp_t *e);
int  exp_win_puts(exp_t *e, const char *s);                                  /* -1 if it does not fit (nothing written) */
void exp_civil_from_days(int64_t days, int *y, unsigned *m, unsigned *d);
void exp_utc_parts(int64_t gps_us, int *y, unsigned *mo, unsigned *d, unsigned *hh, unsigned *mm, unsigned *ss, unsigned *cs);
/* per-format hooks */
int  exp_vbo_open(exp_t *e);  int exp_vbo_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len);  int exp_vbo_finish(exp_t *e);
int  exp_nmea_open(exp_t *e); int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len); int exp_nmea_finish(exp_t *e);
int  exp_json_open(exp_t *e); int exp_json_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len); int exp_json_finish(exp_t *e);
#endif
```

`components/core/export/exp.c`:
```c
#include "core/exp.h"
#include <string.h>

int exp_win_free(const exp_t *e) { return (int)(EXP_WINDOW - e->win_len); }

int exp_win_puts(exp_t *e, const char *s)
{
    size_t n = strlen(s);
    if (e->win_len + n > EXP_WINDOW) return -1;
    memcpy(e->win + e->win_len, s, n); e->win_len += n;
    return 0;
}

void exp_civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

void exp_utc_parts(int64_t gps_us, int *y, unsigned *mo, unsigned *d, unsigned *hh, unsigned *mm, unsigned *ss, unsigned *cs)
{
    int64_t total_cs = (gps_us + 5000) / 10000;           /* round to centiseconds */
    int64_t secs = total_cs / 100;
    *cs = (unsigned)(total_cs % 100);
    int64_t days = secs / 86400; int64_t sod = secs % 86400;
    exp_civil_from_days(days, y, mo, d);
    *hh = (unsigned)(sod / 3600); *mm = (unsigned)((sod % 3600) / 60); *ss = (unsigned)(sod % 60);
}

int exp_open(exp_t *e, uint8_t fmt, const exp_meta_t *meta)
{
    memset(e, 0, sizeof *e);
    e->fmt = fmt; e->meta = *meta;
    ses_fix_state_init(&e->fix_st); ses_fused_state_init(&e->fus_st);
    switch (fmt) {
    case EXP_VBO:  return exp_vbo_open(e);
    case EXP_NMEA: return exp_nmea_open(e);
    case EXP_JSON: return exp_json_open(e);
    default: return -1;
    }
}

int exp_feed(exp_t *e, uint8_t type, const uint8_t *payload, uint8_t len)
{
    if (e->finished) return -1;
    switch (e->fmt) {
    case EXP_VBO:  return exp_vbo_feed(e, type, payload, len);
    case EXP_NMEA: return exp_nmea_feed(e, type, payload, len);
    case EXP_JSON: return exp_json_feed(e, type, payload, len);
    default: return -1;
    }
}

int exp_pull(exp_t *e, uint8_t *out, size_t cap, size_t *n_out)
{
    size_t avail = e->win_len - e->win_pos;
    size_t n = avail < cap ? avail : cap;
    memcpy(out, e->win + e->win_pos, n);
    e->win_pos += n;
    if (e->win_pos == e->win_len) { e->win_pos = 0; e->win_len = 0; }
    *n_out = n;
    return 0;
}

int exp_finish(exp_t *e)
{
    if (e->finished) return 0;
    int r;
    switch (e->fmt) {
    case EXP_VBO:  r = exp_vbo_finish(e); break;
    case EXP_NMEA: r = exp_nmea_finish(e); break;
    case EXP_JSON: r = exp_json_finish(e); break;
    default: r = -1;
    }
    if (r == 0) e->finished = 1;
    return r;
}
```

- [ ] **Step 4: Implement the VBO format**

`components/core/export/exp_vbo.c`:
```c
#include "core/exp.h"
#include <stdio.h>
#include <string.h>

static const char *HEADER_PART1 =
    "\r\n[header]\r\nsatellites\r\ntime\r\nlatitude\r\nlongitude\r\nvelocity kmh\r\nheading\r\nheight\r\nlat_g\r\nlon_g\r\nlean\r\nyaw\r\n"
    "\r\n[channel units]\r\n\r\n[comments]\r\n";

int exp_vbo_open(exp_t *e)
{
    char line[160];
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(e->meta.created_gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    snprintf(line, sizeof line, "File created on %02u/%02u/%04d at %02u:%02u:%02u\r\n", d, mo, y, hh, mm, ss);
    if (exp_win_puts(e, line) < 0) return -1;
    if (exp_win_puts(e, HEADER_PART1) < 0) return -1;
    snprintf(line, sizeof line, "LapTimer %s (%s)\r\nSession %s\r\nVenue %s / %s\r\n\r\n", e->meta.fw, e->meta.hwid, e->meta.session_id, e->meta.venue, e->meta.layout);
    if (exp_win_puts(e, line) < 0) return -1;
    if (e->meta.has_sf) {
        snprintf(line, sizeof line, "[laptiming]\r\nStart %.5f %.5f %.5f %.5f\r\n\r\n",
                 -e->meta.sf_lon1 * 60.0, e->meta.sf_lat1 * 60.0, -e->meta.sf_lon2 * 60.0, e->meta.sf_lat2 * 60.0);
        if (exp_win_puts(e, line) < 0) return -1;
    }
    if (exp_win_puts(e, "[column names]\r\nsats time lat long velocity heading height lat_g lon_g lean yaw\r\n\r\n[data]\r\n") < 0) return -1;
    return 0;
}

int exp_vbo_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    if (type == SES_T_FUSED) {
        fused_sample_t s;
        if (ses_decode_fused(&e->fus_st, p, len, &s) == 1) { e->held = s; e->have_held = 1; }
        return 0;
    }
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return 0;
    if (exp_win_free(e) < 120) return EXP_FULL;
    gps_fix_t f;
    if (ses_decode_fix(&e->fix_st, type, p, len, &f) != 1) return -1;
    ses_fused_state_on_fix(&e->fus_st, f.gps_us);
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(f.gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    double lat_min = (double)f.lat_e7 / 1e7 * 60.0;
    double long_min = -((double)f.lon_e7 / 1e7 * 60.0);      /* west positive */
    double kmh = (double)f.gspeed_mms * 3.6 / 1000.0;
    double head = (double)f.head_e5 / 1e5;
    double height = (double)f.alt_mm / 1000.0;
    float glat = e->have_held ? e->held.g_lat : 0.0f, glon = e->have_held ? e->held.g_lon : 0.0f;
    float lean = e->have_held ? e->held.lean_deg : 0.0f, yaw = e->have_held ? e->held.yaw_dps : 0.0f;
    char line[120];
    snprintf(line, sizeof line, "%03u %02u%02u%02u.%02u %.5f %.5f %.2f %06.2f %07.2f %+.3f %+.3f %+06.2f %+06.2f\r\n",
             f.sats, hh, mm, ss, cs, lat_min, long_min, kmh, head, height, (double)glat, (double)glon, (double)lean, (double)yaw);
    return exp_win_puts(e, line);
}

int exp_vbo_finish(exp_t *e) { (void)e; return 0; }
```

Stub the other two formats for now so the library links; Task 12 replaces them:

`components/core/export/exp_nmea.c`:
```c
#include "core/exp.h"
int exp_nmea_open(exp_t *e) { (void)e; return -1; }
int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len) { (void)e; (void)type; (void)p; (void)len; return -1; }
int exp_nmea_finish(exp_t *e) { (void)e; return -1; }
```
`components/core/export/exp_json.c`: same three stubs with the `json` names.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass. If the golden differs only in the last digit of a coordinate, check that the test's `lat_e7` values are exactly as listed (−338567000 and +540) and that `%.5f` is used.

- [ ] **Step 6: Commit**

```bash
git add components/core/include/core/exp.h components/core/export test/test_exp_vbo.c test/CMakeLists.txt
git commit -m "feat(core): streaming exporter core and Racelogic VBO format"
```

---

### Task 12: NMEA and JSON exporters (`core/exp` part 2)

**Files:**
- Modify: `components/core/export/exp_nmea.c`, `components/core/export/exp_json.c`
- Create: `test/test_exp_nmea_json.c`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `exp_t` hooks from Task 11, `ses_decode_lap`, `ses_decode_drag_run`, `ses_decode_hdr`, `jw`.
- Produces (spec §14.2, §14.3): NMEA `GPRMC` + `GPGGA` per fix; JSON summary object `{"id":..,"hdr":{...},"laps":[...],"runs":[...]}` streamed incrementally. Each `DRAG_RUN` record (up to `DRAG_MAX_GATES` = 16 gates) is itself emitted incrementally, one gate object per step, resuming across `EXP_FULL`/pull/re-feed cycles via `exp_t.run_pending`/`run_gate_idx` — this keeps any single record within the 1024-byte window regardless of gate count. `exp_finish` is idempotent (returns 0 once already finished) and returns -1 if called while a `DRAG_RUN` frame is mid-emission (the caller must finish feeding that frame first).

- [ ] **Step 1: Write the failing test**

`test/test_exp_nmea_json.c`:
```c
#include "unity.h"
#include "core/exp.h"
#include "core/ses.h"
#include "core/json.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

static size_t drain(exp_t *e, char *dst, size_t at)
{
    uint8_t chunk[256]; size_t n;
    while (exp_pull(e, chunk, sizeof chunk, &n) == 0 && n > 0) { memcpy(dst + at, chunk, n); at += n; }
    dst[at] = '\0'; return at;
}
static void feed_frame(exp_t *e, char *dst, size_t *at, const uint8_t *fr, int n)
{
    while (exp_feed(e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)) == EXP_FULL) *at = drain(e, dst, *at);
}
static uint8_t nmea_xor(const char *s)     /* between '$' and '*' */
{
    uint8_t x = 0; for (s++; *s && *s != '*'; s++) x ^= (uint8_t)*s; return x;
}

static void test_nmea_sentences_and_checksums(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m);
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_NMEA, &m));
    char out[1024]; size_t at = 0;
    ses_fix_state_t fs; ses_fix_state_init(&fs);
    gps_fix_t f; memset(&f, 0, sizeof f);
    f.gps_us = 1789380900LL * 1000000LL; f.lat_e7 = -338567000; f.lon_e7 = 185170000; f.alt_mm = 45000; f.gspeed_mms = 34292; f.head_e5 = 9012000;
    f.pdop_e2 = 120; f.fix_type = 3; f.sats = 8; f.flags = GPS_FLAG_FIXOK | GPS_FLAG_TIME | GPS_FLAG_DATE; f.valid = 1;
    uint8_t fr[64]; int n = ses_encode_fix(&fs, &f, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    exp_finish(&e); at = drain(&e, out, at);
    /* speed 34.292 m/s = 66.66 kn */
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPRMC,101500.00,A,3351.40200,S,01831.02000,E,66.66,090.12,140926,,,A*"));
    TEST_ASSERT_NOT_NULL(strstr(out, "$GPGGA,101500.00,3351.40200,S,01831.02000,E,1,08,1.2,45.0,M,0.0,M,,*"));
    char *rmc = strstr(out, "$GPRMC"), *gga = strstr(out, "$GPGGA");
    unsigned cs;
    sscanf(strchr(rmc, '*') + 1, "%2x", &cs); TEST_ASSERT_EQUAL_HEX8(nmea_xor(rmc), cs);
    sscanf(strchr(gga, '*') + 1, "%2x", &cs); TEST_ASSERT_EQUAL_HEX8(nmea_xor(gga), cs);
    TEST_ASSERT_NOT_NULL(strstr(out, "\r\n$GPGGA"));
}

static void test_json_summary_structure(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    uint8_t fr[256]; int n;
    ses_hdr_t h; memset(&h, 0, sizeof h); memcpy(h.session_id, "S00042_001", 10); h.venue_id = 6; h.layout_id = 1; strcpy(h.fw, "v0.1.0"); h.start_gps_us = 1789380900LL * 1000000LL; h.gps_hz = 5; h.fused_hz = 10;
    n = ses_encode_hdr(&h, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    n = ses_encode_venue(6, 1, "Killarney", fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    for (int i = 1; i <= 2; i++) {
        lap_result_t lap; memset(&lap, 0, sizeof lap);
        lap.lap_no = (uint16_t)i; lap.time_ms = 112340u + (uint32_t)i; lap.flags = LAP_F_VALID; lap.n_sectors = 2; lap.sector_ms[0] = 50000; lap.sector_ms[1] = 62340u + (uint32_t)i;
        lap.stats.max_speed_cms = 6000; lap.stats.max_lean_r_cdeg = 5500;
        n = ses_encode_lap(&lap, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    }
    drag_result_t run; memset(&run, 0, sizeof run); run.run_no = 1; run.n_gates = 1; run.gates[0] = (drag_gate_res_t){ 2, 5910, 2778, 9800, 1 }; run.trap_cms = 0;
    n = ses_encode_drag_run(&run, fr, sizeof fr); feed_frame(&e, out, &at, fr, n);
    /* exp_finish may return EXP_FULL when the window is nearly full (see core/exp.h): pull and retry */
    int fin;
    while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);

    jsmntok_t toks[256];
    int cnt = json_parse(out, at, toks, 256);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int laps = json_obj_get(out, toks, cnt, 0, "laps"); TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[laps].type); TEST_ASSERT_EQUAL_INT(2, toks[laps].size);
    int runs = json_obj_get(out, toks, cnt, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int hdr = json_obj_get(out, toks, cnt, 0, "hdr"); int venue = json_obj_get(out, toks, cnt, hdr, "venue");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[venue], "Killarney"));
    int lap1 = laps + 1; int ms = json_obj_get(out, toks, cnt, lap1, "ms"); int64_t v; json_tok_int(out, &toks[ms], &v); TEST_ASSERT_EQUAL_INT64(112341, v);
    int sectors = json_obj_get(out, toks, cnt, lap1, "sectors"); TEST_ASSERT_EQUAL_INT(2, toks[sectors].size);
    int valid = json_obj_get(out, toks, cnt, lap1, "valid"); bool b; json_tok_bool(out, &toks[valid], &b); TEST_ASSERT_TRUE(b);
}

static void test_json_sixteen_gate_run_streams_across_pulls(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_002");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[8192]; size_t at = 0;
    uint8_t fr[256]; int n;
    drag_result_t run; memset(&run, 0, sizeof run); run.run_no = 3; run.n_gates = DRAG_MAX_GATES; run.trap_cms = 8472; run.flags = DRAG_F_QUARTER;
    for (uint8_t i = 0; i < DRAG_MAX_GATES; i++) run.gates[i] = (drag_gate_res_t){ (uint8_t)(i + 1), 1000u * (i + 1u), (uint16_t)(500u * (i + 1u)), 2500u * (i + 1u), 1 };
    n = ses_encode_drag_run(&run, fr, sizeof fr);
    /* small pulls force several EXP_FULL/resume cycles inside the run */
    int r;
    while ((r = exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD))) == EXP_FULL) {
        uint8_t chunk[64]; size_t got; exp_pull(&e, chunk, sizeof chunk, &got); memcpy(out + at, chunk, got); at += got;
    }
    TEST_ASSERT_EQUAL_INT(0, r);
    while ((r = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, exp_finish(&e));                       /* idempotent */
    TEST_ASSERT_EQUAL_UINT(at, drain(&e, out, at));                  /* nothing more emitted */
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int runs = json_obj_get(out, toks, cnt, 0, "runs"); TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int gates = json_obj_get(out, toks, cnt, runs + 1, "gates"); TEST_ASSERT_EQUAL_INT(DRAG_MAX_GATES, toks[gates].size);
    int last = gates + 1; for (int i = 0; i < DRAG_MAX_GATES - 1; i++) last = json_skip(toks, cnt, last);
    int dist = json_obj_get(out, toks, cnt, last, "dist_cm"); int64_t v; json_tok_int(out, &toks[dist], &v);
    TEST_ASSERT_EQUAL_INT64(2500 * DRAG_MAX_GATES, v);
}

static void test_json_header_strings_using_every_wire_byte_are_emitted_whole(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_001");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[4096]; size_t at = 0;
    ses_hdr_t h; memset(&h, 0, sizeof h);
    memcpy(h.session_id, "S00042_001", 10);
    memcpy(h.fw, "v0.3.1-abcdefghi", 16);          /* exactly 16 bytes, no NUL on the wire */
    memcpy(h.hwid, "moto_neo6m_epaper_int_bl", 24);
    h.venue_id = 6; h.layout_id = 1; h.gps_hz = 5; h.fused_hz = 10; h.start_gps_us = 1789640100000000LL;
    uint8_t fr[256]; int n = ses_encode_hdr(&h, fr, sizeof fr);
    feed_frame(&e, out, &at, fr, n);
    n = ses_encode_venue(6, 1, "0123456789012345678901234567890", fr, sizeof fr);   /* 31 chars + NUL */
    feed_frame(&e, out, &at, fr, n);
    int fin;
    while ((fin = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, fin);
    at = drain(&e, out, at);

    jsmntok_t toks[128];
    int cnt = json_parse(out, at, toks, 128);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int hdr = json_obj_get(out, toks, cnt, 0, "hdr");
    int fw = json_obj_get(out, toks, cnt, hdr, "fw");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[fw], "v0.3.1-abcdefghi"));      /* no spill, no truncation */
    int venue = json_obj_get(out, toks, cnt, hdr, "venue");
    TEST_ASSERT_TRUE(json_tok_eq(out, &toks[venue], "0123456789012345678901234567890"));
}

static void test_finish_refuses_while_a_drag_run_is_mid_emission(void)
{
    exp_meta_t m; memset(&m, 0, sizeof m); strcpy(m.session_id, "S00042_003");
    exp_t e; TEST_ASSERT_EQUAL_INT(0, exp_open(&e, EXP_JSON, &m));
    char out[8192]; size_t at = 0;
    drag_result_t run; memset(&run, 0, sizeof run);
    run.run_no = 4; run.n_gates = DRAG_MAX_GATES; run.trap_cms = 8472;
    for (uint8_t i = 0; i < DRAG_MAX_GATES; i++)
        run.gates[i] = (drag_gate_res_t){ (uint8_t)(i + 1), 1000u * (i + 1u), (uint16_t)(500u * (i + 1u)), 2500u * (i + 1u), 1 };
    uint8_t fr[256]; int n = ses_encode_drag_run(&run, fr, sizeof fr);

    /* the window fills part-way through the gate list: the frame is left half-emitted */
    TEST_ASSERT_EQUAL_INT(EXP_FULL, exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD)));
    TEST_ASSERT_EQUAL_UINT8(1, e.run_pending);
    TEST_ASSERT_EQUAL_INT(-1, exp_finish(&e));           /* closing now would truncate the run */
    TEST_ASSERT_EQUAL_UINT8(0, e.finished);              /* and the exporter is not marked finished */

    /* re-feeding the same frame after a pull resumes it, and then finish succeeds */
    int r;
    while ((r = exp_feed(&e, fr[1], fr + 3, (uint8_t)(n - SES_FRAME_OVERHEAD))) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_UINT8(0, e.run_pending);
    while ((r = exp_finish(&e)) == EXP_FULL) at = drain(&e, out, at);
    TEST_ASSERT_EQUAL_INT(0, r);
    at = drain(&e, out, at);
    jsmntok_t toks[512];
    int cnt = json_parse(out, at, toks, 512);
    TEST_ASSERT_GREATER_THAN(0, cnt);
    int runs = json_obj_get(out, toks, cnt, 0, "runs");
    TEST_ASSERT_EQUAL_INT(1, toks[runs].size);
    int gates = json_obj_get(out, toks, cnt, runs + 1, "gates");
    TEST_ASSERT_EQUAL_INT(DRAG_MAX_GATES, toks[gates].size);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nmea_sentences_and_checksums);
    RUN_TEST(test_json_summary_structure);
    RUN_TEST(test_json_sixteen_gate_run_streams_across_pulls);
    RUN_TEST(test_json_header_strings_using_every_wire_byte_are_emitted_whole);
    RUN_TEST(test_finish_refuses_while_a_drag_run_is_mid_emission);
    return UNITY_END();
}
```

Add `add_core_test(test_exp_nmea_json)`.

NMEA derivation: −33.8567° = 33° 51.402′ S → `3351.40200,S`; 18.517° = 18° 31.02′ E → `01831.02000,E`; 34.292 m/s × 1.943844 = 66.66 kn; date 14 Sep 2026 → `140926`; HDOP field carries `pdop_e2/100` = 1.2.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S test -B test/build && cmake --build test/build && ctest --test-dir test/build --output-on-failure -R test_exp_nmea_json`
Expected: FAIL — `exp_open` returns −1 for NMEA (stub).

- [ ] **Step 3: Implement NMEA**

`components/core/export/exp_nmea.c`:
```c
#include "core/exp.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

int exp_nmea_open(exp_t *e) { (void)e; return 0; }

static void latlon_fields(int32_t lat_e7, int32_t lon_e7, char *lat, char *ns, char *lon, char *ew)
{
    double la = fabs((double)lat_e7 / 1e7), lo = fabs((double)lon_e7 / 1e7);
    int lad = (int)la, lod = (int)lo;
    double lam = (la - lad) * 60.0, lom = (lo - lod) * 60.0;
    snprintf(lat, 16, "%02d%08.5f", lad, lam);
    snprintf(lon, 16, "%03d%08.5f", lod, lom);
    *ns = lat_e7 < 0 ? 'S' : 'N'; *ew = lon_e7 < 0 ? 'W' : 'E';
}

static int put_sentence(exp_t *e, const char *body)      /* body excludes '$' and '*hh' */
{
    uint8_t x = 0; for (const char *s = body; *s; s++) x ^= (uint8_t)*s;
    char line[128];
    snprintf(line, sizeof line, "$%s*%02X\r\n", body, x);
    return exp_win_puts(e, line);
}

int exp_nmea_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    if (type != SES_T_FIX_KEY && type != SES_T_FIX_DELTA) return 0;
    if (exp_win_free(e) < 200) return EXP_FULL;
    gps_fix_t f;
    if (ses_decode_fix(&e->fix_st, type, p, len, &f) != 1) return -1;
    int y; unsigned mo, d, hh, mm, ss, cs;
    exp_utc_parts(f.gps_us, &y, &mo, &d, &hh, &mm, &ss, &cs);
    char lat[16], lon[16], ns, ew;
    latlon_fields(f.lat_e7, f.lon_e7, lat, &ns, lon, &ew);
    double knots = (double)f.gspeed_mms / 1000.0 * 1.943844;
    double course = (double)f.head_e5 / 1e5;
    char body[110];
    snprintf(body, sizeof body, "GPRMC,%02u%02u%02u.%02u,%c,%s,%c,%s,%c,%.2f,%06.2f,%02u%02u%02u,,,A",
             hh, mm, ss, cs, f.valid ? 'A' : 'V', lat, ns, lon, ew, knots, course, d, mo, (unsigned)(y % 100));
    if (put_sentence(e, body) < 0) return -1;
    snprintf(body, sizeof body, "GPGGA,%02u%02u%02u.%02u,%s,%c,%s,%c,%u,%02u,%.1f,%.1f,M,0.0,M,,",
             hh, mm, ss, cs, lat, ns, lon, ew, f.valid ? 1u : 0u, f.sats, (double)f.pdop_e2 / 100.0, (double)f.alt_mm / 1000.0);
    return put_sentence(e, body);
}

int exp_nmea_finish(exp_t *e) { (void)e; return 0; }
```

- [ ] **Step 4: Implement the JSON summary**

`components/core/export/exp_json.c`:
```c
#include "core/exp.h"
#include "core/jw.h"
#include "core/core.h"
#include <stdio.h>
#include <string.h>

/* Streams: {"id":"...","hdr":{...},"laps":[ ... ],"runs":[ ... ]}
 * stage 0: nothing emitted yet (waiting for SESSION_HDR / VENUE); 1: laps array open; 2: runs array open. */

static int emit(exp_t *e, jw_t *w) { return jw_overflow(w) ? -1 : exp_win_puts(e, (const char *)w->buf); }

static int open_hdr(exp_t *e, const ses_hdr_t *h, const char *venue_name)
{
    char buf[400]; jw_t w; jw_init(&w, buf, sizeof buf);
    jw_obj_open(&w);
    jw_key(&w, "id"); jw_str(&w, e->meta.session_id);
    jw_key(&w, "hdr"); jw_obj_open(&w);
      jw_key(&w, "start_utc"); jw_int(&w, h ? h->start_gps_us / 1000000 : 0);
      jw_key(&w, "mode"); jw_str(&w, h && h->mode == 1 ? "drag" : "lap");
      jw_key(&w, "venue_id"); jw_uint(&w, h ? h->venue_id : 0);
      jw_key(&w, "layout_id"); jw_uint(&w, h ? h->layout_id : 0);
      jw_key(&w, "venue"); jw_str(&w, venue_name ? venue_name : "");
      jw_key(&w, "fw"); jw_str(&w, h ? h->fw : "");
      jw_key(&w, "gps_hz"); jw_uint(&w, h ? h->gps_hz : 0);
      jw_key(&w, "fused_hz"); jw_uint(&w, h ? h->fused_hz : 0);
    jw_obj_close(&w);
    jw_key(&w, "laps"); jw_arr_open(&w);
    /* leave the array open: strip the closing pieces by not calling jw_arr_close/jw_obj_close */
    e->json_stage = 1;
    return emit(e, &w);
}

int exp_json_open(exp_t *e) { e->json_stage = 0; e->have_hdr = 0; e->venue_name[0] = '\0'; e->run_pending = 0; e->run_gate_idx = 0; return 0; }

int exp_json_feed(exp_t *e, uint8_t type, const uint8_t *p, uint8_t len)
{
    if (e->json_stage == 0) {
        if (type == SES_T_SESSION_HDR) { if (ses_decode_hdr(p, len, &e->hdr) == 1) e->have_hdr = 1; return 0; }
        if (type == SES_T_VENUE) {
            ses_venue_t v;
            if (ses_decode_venue(p, len, &v) == 1) memcpy(e->venue_name, v.name, sizeof e->venue_name);
            return 0;
        }
        if (type != SES_T_LAP && type != SES_T_DRAG_RUN && type != SES_T_END) return 0;
        if (exp_win_free(e) < 400) return EXP_FULL;
        if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) return -1;
        /* fall through to handle this frame in stage 1/2 */
    }
    if (type == SES_T_LAP) {
        if (e->json_stage != 1) return 0;                     /* laps after runs began: ignore (log order guarantees this never happens) */
        if (exp_win_free(e) < 400) return EXP_FULL;
        lap_result_t lap; if (ses_decode_lap(p, len, &lap) != 1) return -1;
        char buf[400]; jw_t w; jw_init(&w, buf, sizeof buf);
        if (e->laps > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
        jw_obj_open(&w);
        jw_key(&w, "n"); jw_uint(&w, lap.lap_no);
        jw_key(&w, "ms"); jw_uint(&w, lap.time_ms);
        jw_key(&w, "valid"); jw_bool(&w, (lap.flags & LAP_F_VALID) != 0);
        jw_key(&w, "flags"); jw_uint(&w, lap.flags);
        jw_key(&w, "sectors"); jw_arr_open(&w); for (uint8_t i = 0; i < lap.n_sectors; i++) jw_uint(&w, lap.sector_ms[i]); jw_arr_close(&w);
        jw_key(&w, "stats"); jw_obj_open(&w);
          jw_key(&w, "max_speed_cms"); jw_uint(&w, lap.stats.max_speed_cms);
          jw_key(&w, "min_speed_cms"); jw_uint(&w, lap.stats.min_speed_cms);
          jw_key(&w, "lean_l"); jw_int(&w, lap.stats.max_lean_l_cdeg);
          jw_key(&w, "lean_r"); jw_int(&w, lap.stats.max_lean_r_cdeg);
          jw_key(&w, "glat"); jw_int(&w, lap.stats.max_glat_e3);
          jw_key(&w, "gacc"); jw_int(&w, lap.stats.max_gacc_e3);
          jw_key(&w, "gbrake"); jw_int(&w, lap.stats.max_gbrake_e3);
        jw_obj_close(&w);
        jw_obj_close(&w);
        e->laps++;
        return emit(e, &w);
    }
    if (type == SES_T_DRAG_RUN) {
        drag_result_t run; if (ses_decode_drag_run(p, len, &run) != 1) return -1;
        if (!e->run_pending) {
            if (exp_win_free(e) < 200) return EXP_FULL;
            if (e->json_stage == 1) {
                CORE_ASSERT_RET(exp_win_puts(e, "],\"runs\":[") == 0, 0x0A01, -1);
                e->json_stage = 2;
            }
            if (e->runs > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
            char buf[200]; jw_t w; jw_init(&w, buf, sizeof buf);
            jw_obj_open(&w);
            jw_key(&w, "n"); jw_uint(&w, run.run_no);
            jw_key(&w, "t0_utc_us"); jw_int(&w, run.t0_gps_us);
            jw_key(&w, "rollout"); jw_bool(&w, (run.flags & DRAG_F_ROLLOUT) != 0);
            jw_key(&w, "trap_cms"); jw_uint(&w, run.trap_cms);
            jw_key(&w, "gates"); jw_arr_open(&w);
            if (emit(e, &w) < 0) return -1;
            e->run_pending = 1; e->run_gate_idx = 0;
        }
        /* one gate object per step; after EXP_FULL the caller pulls and re-feeds the same frame, and we resume here */
        while (e->run_gate_idx < run.n_gates) {
            if (exp_win_free(e) < 120) return EXP_FULL;
            const drag_gate_res_t *g = &run.gates[e->run_gate_idx];
            char buf[120]; jw_t w; jw_init(&w, buf, sizeof buf);
            if (e->run_gate_idx > 0) CORE_ASSERT_RET(exp_win_puts(e, ",") == 0, 0x0A01, -1);
            jw_obj_open(&w);
            jw_key(&w, "id"); jw_uint(&w, g->gate_id);
            jw_key(&w, "ms"); jw_uint(&w, g->time_ms);
            jw_key(&w, "speed_cms"); jw_uint(&w, g->speed_cms);
            jw_key(&w, "dist_cm"); jw_uint(&w, g->dist_cm);
            jw_key(&w, "hit"); jw_bool(&w, g->hit != 0);
            jw_obj_close(&w);
            if (emit(e, &w) < 0) return -1;
            e->run_gate_idx++;
        }
        if (exp_win_free(e) < 4) return EXP_FULL;
        CORE_ASSERT_RET(exp_win_puts(e, "]}") == 0, 0x0A01, -1);
        e->run_pending = 0; e->runs++;
        return 0;
    }
    return 0;
}

int exp_json_finish(exp_t *e)
{
    if (e->run_pending) return -1;
    if (exp_win_free(e) < 400) return EXP_FULL;
    if (e->json_stage == 0) { if (open_hdr(e, e->have_hdr ? &e->hdr : NULL, e->venue_name[0] ? e->venue_name : NULL) < 0) return -1; }
    if (e->json_stage == 1) {
        CORE_ASSERT_RET(exp_win_puts(e, "],\"runs\":[") == 0, 0x0A01, -1);
        e->json_stage = 2;
    }
    CORE_ASSERT_RET(exp_win_puts(e, "]}") == 0, 0x0A01, -1);
    return 0;
}
```

Design note: `open_hdr` leaves the `laps` array open in the emitted text by building the prefix with `jw` and simply not closing it; subsequent lap objects are appended with explicit commas. The window free-space checks exceed the largest single write each branch can produce, so `EXP_FULL` is returned before anything partial is written. `DRAG_RUN` records are themselves streamed incrementally — the run's fixed fields and the open `"gates":[` are written once (guarded by `exp_t.run_pending`), then one gate object is appended per resumption of `exp_json_feed` for that same frame (`exp_t.run_gate_idx` tracks progress), so a run with any of the spec's up to `DRAG_MAX_GATES` (16) gates fits the 1024-byte window regardless of how many `EXP_FULL`/pull/re-feed cycles it takes; the caller must re-feed the identical `SES_T_DRAG_RUN` frame after each `EXP_FULL` until it gets `0` back. `exp_json_finish` may itself return `EXP_FULL`, and returns `-1` if a `DRAG_RUN` frame is still mid-emission (`run_pending`) — the caller pulls and calls it again, or finishes feeding the frame first (document this in `exp.h`: "exp_finish may return EXP_FULL: pull, then call again. Returns 0 (no-op) if already finished, -1 if a DRAG_RUN frame is mid-emission (re-feed it first)."). `exp_finish` itself is idempotent at the `exp.c` level: it returns `0` immediately if `e->finished` is already set.

Add to `exp.h` next to `exp_finish`: `/* may return EXP_FULL: pull, then call again */`.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build test/build && ctest --test-dir test/build --output-on-failure`
Expected: all pass (12 test executables).

- [ ] **Step 6: Commit**

```bash
git add components/core/export components/core/include/core/exp.h test/test_exp_nmea_json.c test/CMakeLists.txt
git commit -m "feat(core): NMEA and JSON summary exporters"
```

---

### Task 13: Verify CI runs the host tests

**Files:**
- Modify: none (the workflows were created in plan 00; the `host-tests` and `tracks-generated` jobs probe for `test/CMakeLists.txt` and `tools/tracks/gen_tracks.py` after checkout and now run their real steps instead of "Nothing to do").

**Interfaces:** none (CI only).

- [ ] **Step 1: Verify locally from a clean tree**

Run:
```bash
rm -rf test/build && cmake -S test -B test/build -DCMAKE_BUILD_TYPE=Debug && cmake --build test/build --parallel && ctest --test-dir test/build --output-on-failure
python3 tools/tracks/gen_tracks.py tools/tracks/*.json -o components/core/tracks/trk_bundled.c && git diff --exit-code components/core/tracks/trk_bundled.c
```
Expected: all tests pass; `git diff` prints nothing and exits 0.

- [ ] **Step 2: Push the session branch, open the PR, watch CI**

```bash
git push -u origin HEAD
gh pr create --base main --title "feat(core): plan 01 core foundation" --body-file .github/PULL_REQUEST_TEMPLATE.md
gh pr checks --watch
```
Edit the PR body checklist before merging.
Expected: `hygiene`, `host-tests`, `tracks-generated` succeed running their real steps; `build (moto_neo6m)` and `build (moto_sim)` succeed on "Nothing to do" (no `build.sh` yet).

- [ ] **Step 3: Merge and tag**

```bash
gh pr merge --squash --delete-branch
git switch main && git pull --ff-only
git tag plan-01-done && git push origin plan-01-done
```

---

### Task 14: On-target self-test (`test_apps/core_selftest`)

**Files:**
- Create: `test_apps/core_selftest/CMakeLists.txt`, `test_apps/core_selftest/sdkconfig.defaults`, `test_apps/core_selftest/main/CMakeLists.txt`, `test_apps/core_selftest/main/main.c`, `test_apps/core_selftest/components/unity_vendored/CMakeLists.txt`
- Modify: `test/test_ring.c` (stress iteration count becomes the macro `RING_STRESS_N`), `.github/workflows/firmware.yml` (build the self-test in CI), `docs/superpowers/plans/2026-09-14-plan-00-dev-environment.md` (mirror the workflow block), `docs/measurements.md` (create; bench results)

**Interfaces:**
- Consumes: every `test/test_*.c` suite unchanged (each defines `main`, `setUp`, `tearDown`); `components/core` as an ESP-IDF component; vendored Unity at `test/unity/src`.
- Produces: an ESP-IDF project that links `core`, compiles each host test file into its own translation unit with `main`/`setUp`/`tearDown` renamed per suite, and runs all suites from `app_main`, printing one `--- <suite>: OK|FAIL (<ms>) ---` line per suite and a final `=== core_selftest RESULT: PASS|FAIL, <n> failing suites, free heap <b>, min free <b> ===` line. Exit criterion: `RESULT: PASS` observed on the board.

Why: proves the pure-C core on the xtensa toolchain (packed-record alignment, `_Atomic` on xtensa, newlib `%lld`/`%f` formatting, float performance, stack use of the reader/exporter) before any firmware plan builds on it (spec §22.3).

- [ ] **Step 1: Make the ring stress count overridable**

In `test/test_ring.c`, after the `#include <pthread.h>` line add:
```c
#ifndef RING_STRESS_N
#define RING_STRESS_N 2000000ULL          /* target build overrides with -DRING_STRESS_N=20000ULL */
#endif
```
and replace both occurrences of `const uint64_t N = 2000000;` with `const uint64_t N = RING_STRESS_N;`. Mirror the same edit into Task 3 step 1's test block above. Rebuild host (clang and gcc) — 12/12 still green.

- [ ] **Step 2: Create the IDF project**

`test_apps/core_selftest/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
# components/core is the library under test; components/ holds the vendored Unity wrapper
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components/core" "${CMAKE_CURRENT_LIST_DIR}/components")
# IDF ships its own "unity" component (and "cmock", which requires it). Its component name collides
# with the "unity_vendored" name only in spirit, not in CMake target name -- but both compile a
# translation unit exporting the *same* global Unity C symbols (UnityBegin, UnityAssertEqualNumber, ...).
# IDF's copy is built without our UNITY_SUPPORT_64/UNITY_INCLUDE_DOUBLE configuration, so when both
# libunity.a (IDF's) and libunity_vendored.a (ours) land on the link line, the linker silently
# resolves Unity symbols from whichever archive it scans first -- observed to be IDF's, with a
# mismatched calling ABI, corrupting every assertion call. Exclude IDF's unity/cmock entirely so this
# project links only the pinned vendored Unity 2.6.0 in components/unity_vendored.
set(EXCLUDE_COMPONENTS "unity" "cmock")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(core_selftest)
```

`test_apps/core_selftest/sdkconfig.defaults`:
```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=24576
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0=y
CONFIG_ESP_TASK_WDT_EN=n
CONFIG_ESP_INT_WDT=y
CONFIG_FREERTOS_HZ=1000
CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT=6144
CONFIG_COMPILER_STACK_CHECK_MODE_STRONG=y
CONFIG_COMPILER_OPTIMIZATION_DEFAULT=y
```
(The main task stack is 40 KB because `test_trk.c` keeps a 16 KB blob and several 2.75 KB venues on the stack; the task watchdog is off because suites block `app_main` for seconds.)

`test_apps/core_selftest/components/unity_vendored/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "${CMAKE_CURRENT_LIST_DIR}/../../../../test/unity/src/unity.c"
                       INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../test/unity/src")
target_compile_definitions(${COMPONENT_LIB} PUBLIC UNITY_INCLUDE_DOUBLE UNITY_DOUBLE_PRECISION=1e-12 UNITY_SUPPORT_64)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function)
```

`test_apps/core_selftest/main/CMakeLists.txt` — generates one wrapper TU per host test file at configure time:
```cmake
set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../test")
# One wrapper TU per host test file, discovered rather than listed, so a new test/test_*.c is picked
# up without editing this file. The glob cannot use CONFIGURE_DEPENDS: ESP-IDF also evaluates
# component CMakeLists.txt in script mode while expanding requirements, where that keyword is an
# error. The directory property at the bottom does the same job -- adding or removing a file changes
# the directory's timestamp, which re-runs configuration and so re-runs this glob.
file(GLOB TEST_SRCS "${TEST_DIR}/test_*.c")
set(SUITES "")
foreach(f ${TEST_SRCS})
  get_filename_component(base "${f}" NAME_WE)          # .../test_cfg.c -> test_cfg
  string(REGEX REPLACE "^test_" "" s "${base}")        # test_cfg       -> cfg
  list(APPEND SUITES "${s}")
endforeach()
list(SORT SUITES)
set(WRAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/wrap")
file(MAKE_DIRECTORY "${WRAP_DIR}")
set(WRAPPERS "")
foreach(s ${SUITES})
  set(w "${WRAP_DIR}/wrap_test_${s}.c")
  file(WRITE "${w}" "#define main run_test_${s}\n#define setUp setUp_test_${s}\n#define tearDown tearDown_test_${s}\n#include \"${TEST_DIR}/test_${s}.c\"\n")
  list(APPEND WRAPPERS "${w}")
endforeach()
# suites_gen.h: one SUITE(name) line per globbed test, sorted, consumed twice as an X-macro by
# main.c -- so a new test/test_*.c is always declared, built into the suite table and run; it
# cannot be linked but silently skipped.
set(SUITES_GEN "${CMAKE_CURRENT_BINARY_DIR}/suites_gen.h")
set(SUITES_GEN_CONTENT "")
foreach(s ${SUITES})
  string(APPEND SUITES_GEN_CONTENT "SUITE(${s})\n")
endforeach()
file(WRITE "${SUITES_GEN}" "${SUITES_GEN_CONTENT}")
idf_component_register(SRCS "main.c" ${WRAPPERS} INCLUDE_DIRS "." REQUIRES core unity_vendored pthread esp_timer)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE RING_STRESS_N=20000ULL)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-unused-parameter)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TEST_DIR})
```

`test_apps/core_selftest/main/main.c`:
```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Each host test file is compiled with main/setUp/tearDown renamed per suite (see CMakeLists.txt).
 * Unity calls the global setUp()/tearDown(); these dispatch to the suite currently running. */
typedef void (*hook_t)(void);
static hook_t cur_setup, cur_teardown;
void setUp(void) { if (cur_setup) cur_setup(); }
void tearDown(void) { if (cur_teardown) cur_teardown(); }

/* suites_gen.h is generated by CMakeLists.txt from the same test/test_*.c glob that builds the
 * wrappers, one SUITE(name) line per file, so a new suite is always declared, built into the table
 * below and run -- it cannot be linked but silently skipped. */
#define SUITE(name) int run_test_##name(void); void setUp_test_##name(void); void tearDown_test_##name(void);
#include "suites_gen.h"
#undef SUITE

typedef struct { const char *name; int (*run)(void); hook_t setup, teardown; } suite_t;
#define SUITE(name) { #name, run_test_##name, setUp_test_##name, tearDown_test_##name },
static const suite_t suites[] = {
#include "suites_gen.h"
};
#undef SUITE

/* The suites above run in app_main's 40 KB task, which says nothing about whether the core fits an
 * ordinary app task. The two suites that drive the deepest core call chains (cfg walks a JSON tree,
 * trk parses and re-serialises a venue) are rerun in a 6 KB task -- the stack size spec §4.3 budgets
 * for the conn task -- and the remaining headroom is reported. */
#define STACK6K_BYTES 6144
static volatile int      stack6k_failed = -1;      /* -1 = not finished yet */
static volatile unsigned stack6k_free_bytes;

static void stack6k_task(void *arg)
{
    (void)arg;
    int failed = 0;
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
        if (strcmp(suites[i].name, "cfg") != 0 && strcmp(suites[i].name, "trk") != 0) continue;
        cur_setup = suites[i].setup; cur_teardown = suites[i].teardown;
        if (suites[i].run() != 0) failed++;
    }
    stack6k_free_bytes = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    printf("--- stack6k cfg+trk: %s --- (%u B stack, %u B never used)\n",
           failed ? "FAIL" : "OK", (unsigned)STACK6K_BYTES, stack6k_free_bytes);
    stack6k_failed = failed;
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
    pcfg.stack_size = 6144; pcfg.prio = 5;
    pcfg.pin_to_core = 1;           /* app_main is pinned to core 0; spinning test threads must not starve it */
    esp_pthread_set_cfg(&pcfg);

    const size_t n = sizeof suites / sizeof suites[0];
    int failed = 0;
    printf("\n=== core_selftest: %u suites, free heap %u ===\n", (unsigned)n, (unsigned)esp_get_free_heap_size());
    for (size_t i = 0; i < n; i++) {
        cur_setup = suites[i].setup; cur_teardown = suites[i].teardown;
        printf("--- %s ---\n", suites[i].name);
        int64_t t0 = esp_timer_get_time();
        int r = suites[i].run();
        printf("--- %s: %s (%lld ms) ---\n", suites[i].name, r == 0 ? "OK" : "FAIL", (long long)((esp_timer_get_time() - t0) / 1000));
        if (r != 0) failed++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (xTaskCreatePinnedToCore(stack6k_task, "stack6k", STACK6K_BYTES, NULL, 5, NULL, 1) != pdPASS) {
        printf("--- stack6k cfg+trk: FAIL --- (task could not be created)\n");
        stack6k_failed = 1;
    }
    while (stack6k_failed < 0) vTaskDelay(pdMS_TO_TICKS(50));

    int bad = failed + (stack6k_failed != 0 ? 1 : 0);
    printf("=== core_selftest RESULT: %s, %d failing suites, stack6k %s (%u B free), free heap %u, min free %u ===\n",
           bad ? "FAIL" : "PASS", failed, stack6k_failed == 0 ? "OK" : "FAIL", stack6k_free_bytes,
           (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
}
```

- [ ] **Step 3: Build for the ESP32**

```bash
source tools/idf-env.sh
idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build set-target esp32 build 2>&1 | tail -20
```
Expected: `Project build complete.` If the xtensa gcc reports warnings in `components/core` that the host compilers did not (e.g. `-Wconversion` notes, `_Atomic` or alignment diagnostics), record them verbatim; any `-Werror` failure in core is fixed with the minimal explicit cast, mirrored into the plan block that owns the file, and noted in the report. A failure inside a test file compiled through a wrapper (e.g. a `-Wunused` under the stricter set) is fixed in the wrapper's compile options, not in the test.

- [ ] **Step 4: Flash and observe (controller step — user holds BOOT and says "ready")**

Flash: `idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build -p /dev/cu.usbserial-0001 flash`. Then capture the serial output at 115200 baud for up to 120 s (pyserial from `tools/.venv`, pulse RTS to reset, stop at the line containing `core_selftest RESULT`).

Expected: every suite prints `OK`; the final line is `=== core_selftest RESULT: PASS, 0 failing suites, ... ===`. Record the per-suite times and the heap figures in `docs/measurements.md` under a "core_selftest on ESP32 (plan 01)" heading, with the date, IDF version, and commit SHA.

- [ ] **Step 5: Build the self-test in CI**

In `.github/workflows/firmware.yml`, job `build`, insert after the existing `Probe inputs` step:
```yaml
      - name: Probe core_selftest
        id: probe_selftest
        run: echo "present=$([ -f test_apps/core_selftest/CMakeLists.txt ] && echo true || echo false)" >> "$GITHUB_OUTPUT"
      - name: Build core_selftest (ESP32)
        if: steps.probe_selftest.outputs.present == 'true' && matrix.env == 'moto_neo6m'
        shell: bash
        run: |
          . $IDF_PATH/export.sh
          git config --global --add safe.directory "$GITHUB_WORKSPACE"
          idf.py -C test_apps/core_selftest -B test_apps/core_selftest/build set-target esp32 build
```
Keep the existing steps and their guards. Mirror the same insertion into plan 00 Task 2's `firmware.yml` block. The job and check names do not change, so branch protection needs no update.

- [ ] **Step 6: Verify, commit, hygiene**

Host: `cmake --build test/build && ctest --test-dir test/build --output-on-failure` and the gcc tree — 12/12. `git diff --check` clean; hygiene command prints nothing; `test_apps/core_selftest/build/` and `sdkconfig` are ignored (`build/` and `sdkconfig` patterns already in `.gitignore` — confirm with `git status --short`). Commit: subject `test: on-target core self-test app and CI build`, trailers.

---

### Task 15: Prototype bill of materials (`docs/hardware/bom.md`)

**Files:**
- Create: `docs/hardware/bom.md`
- Modify: `README.md` (link the BOM under a "## Hardware" heading), `docs/superpowers/plans/2026-09-14-roadmap.md` (the "Still to order" table becomes a pointer to the BOM)

**Interfaces:** none (documentation). Source of truth for parts stays spec §3.1/§3.3; the BOM adds quantities, status, and where to buy in South Africa.

- [ ] **Step 1: Write `docs/hardware/bom.md`**

```markdown
# Prototype bill of materials

Motorcycle prototype (`moto_neo6m` build). Spec references point at `docs/superpowers/specs/2026-09-14-lap-timer-design.md`. Status: **owned** (in hand), **ordered** (with expected arrival), **to order**. Prices are approximate South African landed prices in September 2026 and are only there to size the order.

## Core electronics

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 1 | ESP32 DevKit V1 (ESP32-WROOM-32, CH340, 4 MB) | 1 | controller; verified ESP32-D0WD-V3 rev 3.1, 4 MB flash | §3.1, §3.3 | owned | — | — |
| 2 | GY-NEO6M v2 GPS module | 1 | position, speed, time (5 Hz prototype) | §3.1, §7.3 | ordered, ~mid-Oct 2026 | Communica / Micro Robotics | R150 |
| 3 | GY-521 (MPU6050) | 1 | lean angle, g-forces, motion wake | §3.1, §8 | ordered, ~mid-Oct 2026 | Communica / Micro Robotics | R60 |
| 4 | Waveshare 2.9" e-Paper Module V2 (SSD1680, 296×128) | 1 | rider display | §3.1, §20.1 | to order | DIYElectronics / Micro Robotics | R450 |

## Power

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 5 | 18650 INR 3000 mAh 15 A, flat top | 2 | battery pack, wired in parallel (1S2P) | §3.1 | ordered, ~21 Sep 2026 | Communica / local vape shops | R120 ea |
| 6 | 2-slot 18650 holder | 1 | pack | §3.1 | ordered, ~21 Sep 2026 | Communica | R30 |
| 7 | TP4056 charger module, 6-pad (DW01A + FS8205A protection) | 1 | charging + cell protection | §3.1, §3.2 | ordered, ~21 Sep 2026 (verify 6-pad) | Communica / Micro Robotics | R25 |
| 8 | XC6220B331MR or AP2112K-3.3 LDO regulator | 1 (+1 spare) | 3.3 V rail into the DevKit 3V3 pin, ≥ 600 mA output, ≤ 60 µA quiescent | §3.1, §3.2 | to order | RS Components ZA / Mantech / AliExpress | R20 |
| 9 | 10 µF ceramic capacitor | 2 | regulator in/out | §3.1 | to order | Communica | R5 |
| 10 | 470 µF electrolytic capacitor, 6.3 V+ | 1 | rail bulk for radio bursts | §3.1 | to order | Communica | R5 |
| 11 | P-channel MOSFET AO3401A or SI2301 | 1 (+1 spare) | GPS power switch (PARK) | §3.1, §3.3 | to order | Mantech / RS / AliExpress | R10 |
| 12 | 100 kΩ resistor | 5 | MOSFET gate pull-up, 3× button pull-downs, CHRG pull-up | §3.1, §3.3 | to order | Communica | R5 |
| 13 | 470 kΩ resistor | 2 | battery divider | §3.1 | to order | Communica | R5 |
| 14 | 100 nF ceramic capacitor | 1 | divider filter | §3.1 | to order | Communica | R2 |
| 15 | SS14 Schottky diode | 1 | optional: USB + battery co-existence | §3.2 | to order (optional) | Communica | R3 |
| 16 | Slide or rocker switch, 3 A | 1 | pack disconnect | §3.2 | to order | Communica | R15 |

## Controls, wiring, enclosure

| # | Part | Qty | Purpose | Spec | Status | Where (ZA) | Approx |
|---|------|-----|---------|------|--------|-----------|--------|
| 17 | 12 mm momentary pushbutton, sealed, glove-friendly | 3 | MODE / UP / DOWN | §3.3, §20.8 | to order | Communica / AliExpress | R15 ea |
| 18 | Silicone hook-up wire, 22–26 AWG, and Dupont leads | 1 lot | interconnect | §3.4 | to order | Communica | R60 |
| 19 | Prototyping perfboard 5×7 cm | 1 | regulator, MOSFET, divider, pull-downs | §3.4 | to order | Communica | R20 |
| 20 | IP65 ABS enclosure ≈ 115×90×55 mm, clear lid | 1 | weatherproof housing; e-paper behind the lid | §2.3 C3 | to order | Communica / Mantech | R120 |
| 21 | RAM-style ball mount or handlebar clamp | 1 | mounting on the bike | §2.3 C3 | to order | local motorcycle shop | R250 |
| 22 | Cable gland PG7 | 1 | charge port / USB lead | — | to order | Communica | R10 |
| 23 | Double-sided foam / vibration pads | 1 lot | IMU and board damping | §2.3 C3 | to order | hardware store | R30 |

## Bench and tooling

| # | Item | Purpose | Status |
|---|------|---------|--------|
| 24 | USB-A to micro-USB data cable | flashing, serial console | owned |
| 25 | Multimeter | power-state current measurements (§16.5) | owned (verify) |
| 26 | USB-UART adapter (CP2102/CH340) | `tools/gps_sim.py` replay into GPIO 16 before the GPS arrives; optional with the `gps_sim` driver | optional |
| 27 | Bench power supply (variable) | brownout test (§22.3) | optional |

## Upgrade path (not needed for the prototype)

| Part | Replaces | Spec |
|------|----------|------|
| SEQURE M10-25Q (u-blox M10, 10 Hz, QMC5883L) | GY-NEO6M v2 | §3.5, O3 |
| microSD SPI module | internal-only storage | §13.2, O4 |
| Sharp memory LCD LS027B7DH01 or 2.42" SSD1309 OLED | e-paper (live delta variant) | O5 |
| Battery-native ESP32 board (FireBeetle 2, FeatherS3) | DevKit + external regulator | §3.5 |

## Order checklist (window B, before session 6.1)

Items 8–16 and 17–23 above; the regulator (8) and MOSFET (11) gate the power work in plan 6. Confirm the TP4056 (7) is the 6-pad protected version on arrival; if not, order one before wiring the pack.
```

- [ ] **Step 2: Link it**

`README.md`: add a section `## Hardware` after `## Firmware` with the line `Prototype parts, quantities, order status and South African sources: [docs/hardware/bom.md](docs/hardware/bom.md). Pin map and electrical details: spec §3.` In the roadmap, replace the "### Still to order" table body with the sentence `See [docs/hardware/bom.md](../hardware/bom.md) for the maintained list with quantities, status, and sources.` (keep the heading).

- [ ] **Step 3: Verify and commit**

`git diff --check` clean; hygiene command prints nothing. Commit: subject `docs: prototype bill of materials`, trailers.

## Self-review

**Spec coverage (plan 1 scope = §4.1 layering, §5.2 core API subset, §6.1–6.4 math, §12, §14, §15.1, §10.1–10.2):**
- §4.1 no-IDF/no-malloc: every file uses only libc; Task 1 harness enforces flags. ✔
- §5.2 `tb_`, `geo_`, `ses_`, `exp_`, `cfg_`, `trk_` signatures: implemented in Tasks 4–12. `geo_interp_time` uses the amended 4-argument form. `fus_`, `lap_`, `drag_` are plan 2. ✔
- §6.4 crossing math and §6.2 min-filter: Tasks 4 and 5 with tests matching §22.1 rows `test_geo.c`, `test_tb.c`. ✔
- §12.2–12.5 frame format, record layouts (sizes asserted in tests), delta rules, keyframe cadence: Tasks 6–7. Summary-file assembly (§12.5) is a logger concern (plan 3) but uses only these codecs. ✔
- §14.1 VBO (west-positive longitude, minutes, `[laptiming]`), §14.2 NMEA, §14.3 JSON summary: Tasks 11–12. The JSON *session list* (§14.3 `LIST`) is assembled by `app/cmd` from `.sum` files in plan 5 using `exp_json` per session — noted for plan 5. ✔
- §15.1 config table: every row has a field, a clamp, and a JSON key in Task 9. ✔
- §10.1–10.2 track model, JSON schema, `same`/`reverse`, generator validation (line length 10–60 m): Task 10. The `flags`/`verified` addition is written back into the spec in Task 10 step 3. ✔
- §17.9 `-Wconversion` non-fatal and vendored-file relaxation: Task 1. ✔
- §21.6 CI: workflows from plan 00 activate automatically; Task 13 verifies. ✔
- `ring.h` (§4.4): Task 3. ✔

**Placeholder scan:** no TBD/TODO; every code step has full code; the only "adjust if" guidance is the numeric assertion in the generator, which states the exact fix.

**Type consistency:** `gps_fix_t` fields (`lat_e7`, `gspeed_mms`, `head_e5`, `hacc_mm`) used identically in Tasks 7, 11, 12; `ses_hdr_t` field names match between Task 7 codec and Task 12 JSON; `exp_meta_t.session_id[11]` holds the 10-char id plus NUL; `trk_venue_t.flags` added in Task 10 header and used by the generator and JSON codec; `EXP_FULL` semantics identical in `exp_feed` and `exp_finish`.

**Known follow-ups for plan 2:** `test/data/` fixtures and the synthetic generator, `fus_`/`lap_`/`drag_` engines, `replay` CLI. Tasks 14–15 (target self-test, BOM) are written at the start of session 1.5.
