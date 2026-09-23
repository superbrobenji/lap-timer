/* test_status_json.c -- status_json_format (Plan 5.6 T3 fix 2): the pure GET /api/status
 * success-body formatter extracted out of webapi.c's api_status so its exact byte output is
 * host-testable without esp_http_server, mirroring the linkhost_stream_to_json precedent
 * (test_stream_json.c).
 */
#include "unity.h"
#include "status_json.h"

#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void make_status(lt_status_t *st, uint8_t proto, uint8_t state, uint16_t flags,
                        uint8_t batt_pct, uint16_t batt_mv, uint32_t free_kb, uint16_t sessions,
                        const char *fw)
{
    memset(st, 0, sizeof *st);
    st->proto     = proto;
    st->state     = state;
    st->flags     = flags;
    st->batt_pct  = batt_pct;
    st->batt_mv   = batt_mv;
    st->free_kb   = free_kb;
    st->sessions  = sessions;
    strncpy(st->fw, fw, sizeof st->fw - 1u);
}

/* (a) a known status + non-empty ferr + fresh ages + logging=true: exact-string match. Pins the
 * field order/conversions AND that ferr is spliced in verbatim before the age/logging fields. */
void test_known_status_exact_string(void)
{
    lt_status_t st;
    make_status(&st, 1, 2, 5, 77, 4123, 2048, 9, "0.2.0-3");

    char buf[352];
    int n = status_json_format(buf, sizeof buf, &st, ",\"flash_err\":\"0x0203\"", 250, 1500, true);

    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"connected\":true,\"proto\":1,\"state\":2,\"flags\":5,"
        "\"batt_pct\":77,\"batt_mv\":4123,\"free_kb\":2048,\"sessions\":9,"
        "\"fw\":\"0.2.0-3\",\"flash_err\":\"0x0203\","
        "\"status_age_ms\":250,\"stream_age_ms\":1500,\"logging\":true}", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

/* (b) never-seen ages (-1/-1, see linkstats_age_ms) + logging=false + empty ferr: passthrough,
 * not clamped/rewritten. */
void test_never_seen_ages_and_logging_false(void)
{
    lt_status_t st;
    make_status(&st, 1, 0, 0, 0, 0, 0, 0, "");

    char buf[352];
    int n = status_json_format(buf, sizeof buf, &st, "", -1, -1, false);

    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"connected\":true,\"proto\":1,\"state\":0,\"flags\":0,"
        "\"batt_pct\":0,\"batt_mv\":0,\"free_kb\":0,\"sessions\":0,"
        "\"fw\":\"\","
        "\"status_age_ms\":-1,\"stream_age_ms\":-1,\"logging\":false}", buf);
}

/* (c) worst case: every numeric field at its type's maximum, the longest ferr fragment api_status
 * can actually build (LINKHOST_E_PROTO == -4 -> ",\"flash_err\":\"link -4\"", 22 B -- one byte
 * longer than the alternate 0xffff-hex branch's 21 B), fw at its 7-character cap (fw[8] - NUL),
 * and both ages at INT64_MAX. logging=false is picked over true ("false" > "true" by one byte) so
 * this is genuinely the longest body status_json_format can ever produce. Must still fit the
 * production 352 B buffer with room to spare. */
void test_worst_case_length_fits_352(void)
{
    lt_status_t st;
    make_status(&st, UINT8_MAX, UINT8_MAX, UINT16_MAX, UINT8_MAX, UINT16_MAX, UINT32_MAX,
               UINT16_MAX, "9.9.9-9");

    char buf[352];
    int n = status_json_format(buf, sizeof buf, &st, ",\"flash_err\":\"link -4\"",
                               INT64_MAX, INT64_MAX, false);

    TEST_ASSERT_GREATER_THAN(0, n);
    printf("test_worst_case_length_fits_352: n=%d (cap=%d)\n", n, (int)sizeof buf);
    TEST_ASSERT_LESS_THAN_INT(352, n);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

/* (d) cap too small: returns -1, and -- the actual safety property -- never writes past `cap`.
 * `region` is a fixed-size stack buffer with an untouched canary tail; snprintf's own cap
 * enforcement should make an overrun impossible, but this pins that invariant explicitly and
 * would also trip ASan (enabled for this Debug host build, see test/CMakeLists.txt) on any real
 * out-of-bounds write. */
void test_cap_too_small_never_overflows(void)
{
    lt_status_t st;
    make_status(&st, 1, 2, 3, 4, 5, 6, 7, "0.1.0-1");

    char region[24];
    memset(region, 0xAA, sizeof region);
    size_t cap = 8;   /* far too small for the real body */

    int n = status_json_format(region, cap, &st, "", 100, 200, true);
    TEST_ASSERT_EQUAL_INT(-1, n);

    for (size_t i = cap; i < sizeof region; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xAA, (uint8_t)region[i], "wrote past cap");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_known_status_exact_string);
    RUN_TEST(test_never_seen_ages_and_logging_false);
    RUN_TEST(test_worst_case_length_fits_352);
    RUN_TEST(test_cap_too_small_never_overflows);
    return UNITY_END();
}
