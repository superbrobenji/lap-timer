/* test_linkhost_frame.c -- Plan 5.5 Task 3 Steps 1-4: the ---BEGIN/---END response parser.
 * Verifies CRC-32 compatibility (esp_rom_crc32_le(0,..) == zlib), text (JSON) frames, binary
 * (base64 STATUS) frames, CRC-mismatch detection, malformed rejection, and leading-noise tolerance.
 * Real CRC vectors are computed with linkhost_crc32 (pinned below to the standard check value). */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* The well-known CRC-32 check value pins the local implementation to the esp_rom/zlib algorithm. */
static void test_crc32_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, linkhost_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, linkhost_crc32((const uint8_t *)"", 0));
}

/* Builds "---BEGIN <name> <sizewire>---\r\n<wire>\r\n---END <crc>---\r\n" with a real CRC over the
 * DECODED body (== the wire body for text frames). Returns the total byte length. */
static size_t build_frame(char *buf, size_t cap, const char *name, const char *wire,
                          size_t wire_len, uint32_t crc)
{
    int hn = snprintf(buf, cap, "---BEGIN %s %u---\r\n", name, (unsigned)wire_len);
    memcpy(buf + hn, wire, wire_len);
    int tn = snprintf(buf + hn + wire_len, cap - (size_t)hn - wire_len,
                      "\r\n---END %08x---\r\n", (unsigned)crc);
    return (size_t)hn + wire_len + (size_t)tn;
}

static void test_parse_json_frame(void)
{
    const char *json = "{\"proto\":1,\"count\":3}";
    uint32_t crc = linkhost_crc32((const uint8_t *)json, strlen(json));
    char w[256];
    size_t n = build_frame(w, sizeof w, "sessions", json, strlen(json), crc);

    linkhost_frame_t f;
    TEST_ASSERT_EQUAL_INT(0, linkhost_parse_frame((const uint8_t *)w, n, &f));
    TEST_ASSERT_EQUAL_STRING("sessions", f.name);
    TEST_ASSERT_EQUAL_UINT(strlen(json), f.size);
    TEST_ASSERT_EQUAL_UINT(strlen(json), f.body_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(f.body, json, strlen(json)));
}

/* A binary (base64) STATUS frame: the 20-byte §18.2 record, base64 == "AQIEA1eTD0DiAQAFADEuMi4zAAA=".
 * `size` is the 28-char wire length; the CRC is over the 20 decoded bytes; body_len must be 20. */
static void test_parse_binary_status_frame(void)
{
    const uint8_t rec[LT_STATUS_LEN] = {
        0x01, 0x02, 0x04, 0x03, 0x57, 0x93, 0x0f, 0x40, 0xe2, 0x01,
        0x00, 0x05, 0x00, 0x31, 0x2e, 0x32, 0x2e, 0x33, 0x00, 0x00
    };
    const char *b64 = "AQIEA1eTD0DiAQAFADEuMi4zAAA=";
    uint32_t crc = linkhost_crc32(rec, sizeof rec);
    char w[256];
    size_t n = build_frame(w, sizeof w, "status", b64, strlen(b64), crc);

    linkhost_frame_t f;
    TEST_ASSERT_EQUAL_INT(0, linkhost_parse_frame((const uint8_t *)w, n, &f));
    TEST_ASSERT_EQUAL_STRING("status", f.name);
    TEST_ASSERT_EQUAL_UINT(28, f.size);
    TEST_ASSERT_EQUAL_UINT(LT_STATUS_LEN, f.body_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(f.body, rec, sizeof rec));

    lt_status_t st;
    TEST_ASSERT_TRUE(linkhost_status_decode(f.body, &st));
    TEST_ASSERT_EQUAL_UINT8(1, st.proto);
    TEST_ASSERT_EQUAL_UINT16(0x0304, st.flags);
    TEST_ASSERT_EQUAL_UINT16(3987, st.batt_mv);
    TEST_ASSERT_EQUAL_UINT32(123456, st.free_kb);
    TEST_ASSERT_EQUAL_UINT16(5, st.sessions);
    TEST_ASSERT_EQUAL_STRING("1.2.3", st.fw);
}

static void test_bad_crc_detected(void)
{
    const char *json = "{\"ok\":true}";
    uint32_t good = linkhost_crc32((const uint8_t *)json, strlen(json));
    char w[128];
    size_t n = build_frame(w, sizeof w, "diag", json, strlen(json), good ^ 0x1u);   /* corrupt CRC */

    linkhost_frame_t f;
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_CRC, linkhost_parse_frame((const uint8_t *)w, n, &f));
}

static void test_malformed_rejected(void)
{
    linkhost_frame_t f;
    const char *junk = "laptimer> not a frame at all\r\n";
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_PROTO,
                          linkhost_parse_frame((const uint8_t *)junk, strlen(junk), &f));
    /* announced size larger than the actual body -> PROTO, not a read past the buffer */
    const char *truncated = "---BEGIN x 99---\r\nabc\r\n---END 00000000---\r\n";
    TEST_ASSERT_EQUAL_INT(LINKHOST_E_PROTO,
                          linkhost_parse_frame((const uint8_t *)truncated, strlen(truncated), &f));
}

static void test_leading_noise_tolerated(void)
{
    const char *json = "[]";
    uint32_t crc = linkhost_crc32((const uint8_t *)json, strlen(json));
    char w[256];
    size_t off = (size_t)snprintf(w, sizeof w, "sessions\r\nlaptimer> I (7) app: hi\n");
    size_t n = off + build_frame(w + off, sizeof w - off, "sessions", json, strlen(json), crc);

    linkhost_frame_t f;
    TEST_ASSERT_EQUAL_INT(0, linkhost_parse_frame((const uint8_t *)w, n, &f));
    TEST_ASSERT_EQUAL_STRING("sessions", f.name);
    TEST_ASSERT_EQUAL_UINT(2, f.body_len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_parse_json_frame);
    RUN_TEST(test_parse_binary_status_frame);
    RUN_TEST(test_bad_crc_detected);
    RUN_TEST(test_malformed_rejected);
    RUN_TEST(test_leading_noise_tolerated);
    return UNITY_END();
}
