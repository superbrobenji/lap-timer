/* devcontroller/test/test_wire_escape.c -- host tests for the PURE wire_escape logic
 * (components/linkhost/host/wire_escape.c, Plan 5.6 Task 5 fix 1). Moved from test_config_diff.c
 * (which tested this behavior as config_diff_escape before the function moved out of webapi and
 * into linkhost, since the escaping is a wire-protocol concern, not a config-diff concern). */
#include "unity.h"
#include "wire_escape.h"

#include <stdbool.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* A faithful emulator of esp_console_split_argv over a single (space-escaped) arg token: a
 * backslash escapes the next char (taken literally), a bare double quote toggles quoted mode and
 * is DROPPED, an unescaped space outside quotes ends the arg. This is exactly the transform the
 * lap-timer's REPL applies, so wire_escape()->this must reproduce the original bytes exactly. */
static void split_argv_token(const char *in, char *out, size_t out_cap)
{
    size_t o = 0;
    bool in_q = false, esc = false;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (esc) { if (o + 1 < out_cap) out[o++] = c; esc = false; continue; }
        if (c == '\\') { esc = true; continue; }        /* escape: next char is literal */
        if (c == '"')  { in_q = !in_q; continue; }       /* bare quote toggles + is stripped */
        if (c == ' ' && !in_q) { o = 0; continue; }      /* unescaped space: new arg (token reset) */
        if (o + 1 < out_cap) out[o++] = c;
    }
    out[o] = '\0';
}

/* A bare {"units":"mph"} would have its quotes stripped by the console -> {units:mph} (malformed).
 * wire_escape must produce a line that split_argv reconstructs back to the exact JSON. */
void test_escape_roundtrip_quotes(void)
{
    const char raw[] = "{\"units\":\"mph\"}";
    char esc[128], back[128];
    size_t n = wire_escape(raw, esc, sizeof esc);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(strchr(esc, '"') == NULL || strstr(esc, "\\\"") != NULL);  /* every " is escaped */
    split_argv_token(esc, back, sizeof back);
    TEST_ASSERT_EQUAL_STRING(raw, back);
}

/* A string value containing a space and a backslash must also survive (space -> "\ " so the arg is
 * not split; backslash -> "\\" so the console does not eat it as an escape). */
void test_escape_roundtrip_space_and_backslash(void)
{
    const char raw[] = "{\"v\":\"a b\\c\"}";   /* bytes: {"v":"a b\c"} */
    char esc[128], back[128];
    size_t n = wire_escape(raw, esc, sizeof esc);
    TEST_ASSERT_TRUE(n > 0);
    split_argv_token(esc, back, sizeof back);
    TEST_ASSERT_EQUAL_STRING(raw, back);
}

/* wire_escape reports overflow (0) rather than truncating. */
void test_escape_overflow_returns_zero(void)
{
    char esc[4];
    TEST_ASSERT_EQUAL_UINT(0, wire_escape("{\"a\":\"bbbb\"}", esc, sizeof esc));
}

/* NULL in/out or cap == 0 -> 0, not a crash. */
void test_escape_null_and_zero_cap(void)
{
    char esc[8];
    TEST_ASSERT_EQUAL_UINT(0, wire_escape(NULL, esc, sizeof esc));
    TEST_ASSERT_EQUAL_UINT(0, wire_escape("x", NULL, sizeof esc));
    TEST_ASSERT_EQUAL_UINT(0, wire_escape("x", esc, 0));
}

/* A byte needing no escaping passes straight through. */
void test_escape_plain_passthrough(void)
{
    char esc[16];
    size_t n = wire_escape("abc123", esc, sizeof esc);
    TEST_ASSERT_EQUAL_UINT(6, n);
    TEST_ASSERT_EQUAL_STRING("abc123", esc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_escape_roundtrip_quotes);
    RUN_TEST(test_escape_roundtrip_space_and_backslash);
    RUN_TEST(test_escape_overflow_returns_zero);
    RUN_TEST(test_escape_null_and_zero_cap);
    RUN_TEST(test_escape_plain_passthrough);
    return UNITY_END();
}
