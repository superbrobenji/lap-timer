/* test_status_link.c -- reproduce the on-device symptom: /api/status returns not-connected while
 * the 0xFF stream flows. `status` is the only BINARY (base64) framed response over the link; the
 * existing demux test exercises a TEXT (sessions) frame only. Three isolated cases pin the trigger.
 * Real status bytes: "---BEGIN status 28---\r\n<28 b64>\r\n---END 23a09022---\r\n"; body decodes to
 * 20 bytes; crc32 over the DECODED body == 0x23a09022 (verified). */
#include "unity.h"
#include "linkhost_proto.h"

#include <stdio.h>
#include <string.h>

void setUp(void) { linkhost_reset(); }
void tearDown(void) {}

static void app(uint8_t *s, size_t *L, const void *p, size_t n) { memcpy(s + *L, p, n); *L += n; }
static void add_frame(uint8_t *s, size_t *L)
{
    app(s, L, "---BEGIN status 28---\r\n", 23);
    app(s, L, "AQAAAAAAACQDAAAZADAuMS4wLQA=", 28);
    app(s, L, "\r\n---END 23a09022---\r\n", 22);
}
static int feed_and_pop(const uint8_t *s, size_t L, linkhost_frame_t *f, int *st)
{
    linkhost_reset();
    for (size_t i = 0; i < L; i++) linkhost_feed(&s[i], 1);   /* byte-by-byte: worst case */
    *st = 999;
    return linkhost_pop_response(f, st);
}

/* A: the frame alone -- baseline, must parse. */
void test_A_frame_only(void)
{
    uint8_t s[512]; size_t L = 0; add_frame(s, &L);
    linkhost_frame_t f; int st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed_and_pop(s, L, &f, &st), "A: no response");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, st, "A: st!=0");
    TEST_ASSERT_EQUAL_UINT(20, (unsigned)f.body_len);
}

/* B: realistic -- leftover prompt, echo (its \n flushes the prompt line), frame, stream frames. */
void test_B_realistic(void)
{
    uint8_t s[512]; size_t L = 0;
    const uint8_t f1[] = { 0xFF, 0x01, 0x00, 0x00, 0x03, 0x01, 0xAA, 0xBB };
    app(s, &L, f1, sizeof f1);
    app(s, &L, "laptimer> ", 10);     /* leftover prompt from the previous command */
    app(s, &L, "status\r\n", 8);      /* echo -- its \n terminates the prompt line */
    add_frame(s, &L);
    app(s, &L, "laptimer> ", 10);     /* new prompt after the response */
    const uint8_t f2[] = { 0xFF, 0x02, 0x00, 0x00, 0x03, 0x01, 0xCC, 0xDD };
    app(s, &L, f2, sizeof f2);
    linkhost_frame_t f; int st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed_and_pop(s, L, &f, &st), "B: no response");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, st, "B: st!=0");
    TEST_ASSERT_EQUAL_UINT(20, (unsigned)f.body_len);
}

/* C: pathological -- prompt glued directly to ---BEGIN (no newline between). Happens if the console
 * does NOT echo (dumb mode): "laptimer> ---BEGIN status..." arrives as one line. */
void test_C_prompt_glued_to_begin(void)
{
    uint8_t s[512]; size_t L = 0;
    app(s, &L, "laptimer> ", 10);
    add_frame(s, &L);
    linkhost_frame_t f; int st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed_and_pop(s, L, &f, &st), "C: no response (prompt glued to BEGIN)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, st, "C: st!=0");
}


/* frame with the real on-wire CRLF-translated separators (\r\r\n) the lap-timer console emits. */
static void add_frame_crcrlf(uint8_t *s, size_t *L)
{
    app(s, L, "---BEGIN status 28---\r\r\n", 24);
    app(s, L, "AQAAAAAAACQDAAAZADAuMS4wLQA=", 28);
    app(s, L, "\r\r\n---END 23a09022---\r\r\n", 24);
}

/* D: the ACTUAL on-device bytes -- \r\r\n separators from the console's \n->\r\n translation. */
void test_D_crlf_translated(void)
{
    uint8_t s[512]; size_t L = 0;
    const uint8_t f1[] = { 0xFF, 0x01, 0x00, 0x00, 0x03, 0x01, 0xAA, 0xBB };
    app(s, &L, f1, sizeof f1);
    app(s, &L, "status\r\r\n", 9);
    add_frame_crcrlf(s, &L);
    app(s, &L, "laptimer> ", 10);
    linkhost_frame_t f; int st;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed_and_pop(s, L, &f, &st), "D: no response");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, st, "D: st!=0 (CRLF-translated framing rejected)");
    TEST_ASSERT_EQUAL_UINT(20, (unsigned)f.body_len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_A_frame_only);
    RUN_TEST(test_B_realistic);
    RUN_TEST(test_C_prompt_glued_to_begin);
    RUN_TEST(test_D_crlf_translated);
    return UNITY_END();
}
