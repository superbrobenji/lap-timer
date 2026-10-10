/* test_cfg_blob.c -- host coverage for core/cfg_blob.h (debt sweep A, final review C1 / Ruling
 * F-1): the cfg blob decision (framing + version/migrate/validate) extracted out of
 * lt_nvs.c's lt_cfg_load/lt_cfg_save into a pure, host-testable helper. Every case here mirrors
 * a decision lt_cfg_load used to make inline.
 */
#include "unity.h"
#include "core/cfg.h"
#include "core/cfg_blob.h"
#include "core/ses.h"      /* ses_crc16 -- hand-building a corrupted/foreign-version frame */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* (1) cfg_defaults -> wrap -> unwrap == 0 and byte-identical. */
static void test_round_trip_is_byte_identical(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    cfg_t b; memset(&b, 0xAA, sizeof b);
    TEST_ASSERT_EQUAL_INT(0, cfg_blob_unwrap(buf, sizeof buf, &b));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof a);
}

/* (2) a hand-built version-0 frame (unsupported: cfg_migrate_supported(0) == false) -> -3,
 * c left untouched (pre-filled with a sentinel, compared byte for byte). */
static void test_unsupported_version_leaves_c_untouched(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    buf[0] = 0;                                       /* stored version 0: unsupported */
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));      /* version byte + payload, same as blob_wrap */
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFFu);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);

    cfg_t sentinel; memset(&sentinel, 0x5A, sizeof sentinel);
    cfg_t c = sentinel;
    TEST_ASSERT_EQUAL_INT(-3, cfg_blob_unwrap(buf, sizeof buf, &c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
}

/* (3) flip a payload byte -> CRC mismatch -> -1, c untouched. */
static void test_crc_mismatch_leaves_c_untouched(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));
    buf[10] ^= 0x01u;                                  /* well inside the payload */

    cfg_t sentinel; memset(&sentinel, 0x5A, sizeof sentinel);
    cfg_t c = sentinel;
    TEST_ASSERT_EQUAL_INT(-1, cfg_blob_unwrap(buf, sizeof buf, &c));
    TEST_ASSERT_EQUAL_MEMORY(&sentinel, &c, sizeof c);
}

/* (4) a v1 (== CFG_VERSION) frame whose drag.n_kmh byte is 200 -> unwrap >= 1 (cfg_validate
 * clamped it) and n_kmh == CFG_MAX_BENCHES. The byte is poked directly at its real on-flash
 * offset (offsetof) and the CRC recomputed, rather than going through cfg_defaults -- this is
 * exactly the shape of a corrupted-in-place stored blob. */
static void test_v1_frame_with_bad_n_kmh_is_clamped(void)
{
    cfg_t a; cfg_defaults(&a);
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&a, buf, sizeof buf));

    size_t off = offsetof(cfg_t, drag.n_kmh);
    buf[off] = 200;                                    /* buf[k] == cfg_t byte k, for k >= 1 (wrap is a 1:1 copy) */
    uint16_t crc = ses_crc16(buf, sizeof(cfg_t));
    buf[sizeof(cfg_t)]     = (uint8_t)(crc & 0xFFu);
    buf[sizeof(cfg_t) + 1] = (uint8_t)(crc >> 8);

    cfg_t c; memset(&c, 0, sizeof c);
    int rc = cfg_blob_unwrap(buf, sizeof buf, &c);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, rc);
    TEST_ASSERT_EQUAL_UINT8(CFG_MAX_BENCHES, c.drag.n_kmh);
}

/* (5) cfg_migrate_supported is exactly the versions cfg_migrate accepts today. */
static void test_cfg_migrate_supported_versions(void)
{
    TEST_ASSERT_TRUE(cfg_migrate_supported(1));
    TEST_ASSERT_FALSE(cfg_migrate_supported(0));
    TEST_ASSERT_FALSE(cfg_migrate_supported(2));
}

/* (6) #96, Task 3's blob-migration analysis: a genuine v1-saved blob (every field set by a
 * pre-#96 firmware -- version byte 1, dist_units never written because the field did not exist
 * yet) must MIGRATE, not reset, on a firmware that now carries CFG_VERSION 2. Built by hand-
 * framing a v1 image (cfg_blob_wrap(&v1, ...) with v1.version == 1) rather than corrupting a v2
 * frame's version byte, so this is the real on-flash shape a device upgrading from the previous
 * firmware actually has -- not the synthetic "unsupported version" case test (2) above already
 * covers. mode is deliberately DRAG (not the zero/default value) to prove the migrate path does
 * not silently reset it.
 *
 * Review finding I1 (Task 3 fix round 1): this test frames its "v1 image" from the CURRENT cfg_t
 * (cfg_defaults on today's struct, reinterpreted at today's offsets on both the wrap and unwrap
 * side), so it is layout-consistent by construction and CANNOT fail on a struct-layout regression
 * -- if dist_units were moved back next to `units`, this same test would still pass, because
 * `mode` would be written and read back at the same (now-different) offset on both sides. It does
 * not "catch" an insert-next-to-units layout, despite what an earlier version of this comment
 * claimed. What it still legitimately proves: a v1-tagged frame takes the migrate path, returns
 * >= 0, bumps version, defaults dist_units, and leaves every other field exactly as the v1
 * firmware wrote it -- i.e. cfg_migrate()/cfg_blob_unwrap()'s *logic* is correct. The layout
 * guarantee this logic depends on (dist_units is cfg_t's last byte, so a v1 payload really is a
 * byte-for-byte prefix of v2's) is proven separately and at compile time by the
 * _Static_assert in core/cfg.h, which a target build enforces on the real xtensa ABI too. */
static void test_v1_blob_migrates_without_corrupting_other_fields(void)
{
    cfg_t v1; cfg_defaults(&v1);
    v1.version = 1;
    v1.mode    = CFG_MODE_DRAG;
    v1.ble.name[0] = '\0'; strcpy(v1.ble.name, "LapTimer-V1");
    v1.lap.min_lap_s = 33;
    uint8_t buf[CFG_BLOB_LEN];
    TEST_ASSERT_EQUAL_UINT(CFG_BLOB_LEN, cfg_blob_wrap(&v1, buf, sizeof buf));
    TEST_ASSERT_EQUAL_UINT8(1, buf[0]);   /* the on-flash frame really is tagged version 1 */

    cfg_t c; memset(&c, 0xAA, sizeof c);
    int rc = cfg_blob_unwrap(buf, sizeof buf, &c);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, rc);              /* migrated (+validated), not rejected */
    TEST_ASSERT_EQUAL_UINT8(CFG_VERSION, c.version);      /* bumped to the firmware's version */
    TEST_ASSERT_EQUAL_UINT8(CFG_DIST_M, c.dist_units);    /* new field: defaulted, not garbage */
    TEST_ASSERT_EQUAL_UINT8(CFG_MODE_DRAG, c.mode);       /* untouched by cfg_migrate()'s logic */
    TEST_ASSERT_EQUAL_UINT16(33, c.lap.min_lap_s);        /* untouched -- every field past the header */
    TEST_ASSERT_EQUAL_STRING("LapTimer-V1", c.ble.name);  /* untouched */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_is_byte_identical);
    RUN_TEST(test_unsupported_version_leaves_c_untouched);
    RUN_TEST(test_crc_mismatch_leaves_c_untouched);
    RUN_TEST(test_v1_frame_with_bad_n_kmh_is_clamped);
    RUN_TEST(test_cfg_migrate_supported_versions);
    RUN_TEST(test_v1_blob_migrates_without_corrupting_other_fields);
    return UNITY_END();
}
