/* devcontroller/test/test_config_diff.c -- host tests for the PURE config diff/minify logic
 * (components/webapi/host/config_diff.c, Plan 5.5 Task 4 Step 1). No esp_http_server, no UART:
 * exercises config_diff_minify + config_diff_next_line directly. */
#include "unity.h"
#include "config_diff.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- config_diff_minify: only the changed top-level keys, minified ---- */

void test_single_changed_key(void)
{
    char out[128];
    /* Task 4 Step 1 contract: current {"a":1,"b":2}, desired {"a":1,"b":9} -> {"b":9} len 7. */
    int n = config_diff_minify("{\"a\":1,\"b\":2}", "{\"a\":1,\"b\":9}", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("{\"b\":9}", out);
}

void test_no_changes_yields_empty_object(void)
{
    char out[128];
    int n = config_diff_minify("{\"a\":1,\"b\":2}", "{\"a\":1,\"b\":2}", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("{}", out);
}

void test_new_key_is_a_change(void)
{
    char out[128];
    int n = config_diff_minify("{\"a\":1}", "{\"a\":1,\"c\":5}", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("{\"c\":5}", out);
}

void test_string_value_change(void)
{
    char out[128];
    int n = config_diff_minify("{\"name\":\"x\",\"k\":3}", "{\"name\":\"y\",\"k\":3}",
                               out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"y\"}", out);
}

void test_whitespace_insensitive(void)
{
    char out[128];
    /* pretty-printed current vs minified desired, same values -> no change. */
    int n = config_diff_minify("{ \"a\" : 1 , \"b\" : 2 }", "{\"a\":1,\"b\":2}", out, sizeof out);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("{}", out);
}

void test_multiple_changes_preserve_order(void)
{
    char out[128];
    int n = config_diff_minify("{\"a\":1,\"b\":2,\"c\":3}", "{\"a\":9,\"b\":2,\"c\":8}",
                               out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"a\":9,\"c\":8}", out);
}

void test_nested_object_value_minified(void)
{
    char out[128];
    int n = config_diff_minify("{\"g\":{\"x\":1}}", "{\"g\":{ \"x\" : 2 }}", out, sizeof out);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("{\"g\":{\"x\":2}}", out);
}

void test_single_change_over_cap_returns_negative(void)
{
    char desired[600];
    char out[700];
    size_t p = 0;
    const char *pre = "{\"x\":\"";
    memcpy(desired + p, pre, strlen(pre)); p += strlen(pre);
    for (int i = 0; i < 300; i++) desired[p++] = 'Z';   /* ~300 B single value */
    desired[p++] = '"';
    desired[p++] = '}';
    desired[p] = '\0';
    int n = config_diff_minify("{\"x\":\"a\"}", desired, out, sizeof out);
    TEST_ASSERT_TRUE(n < 0);
}

void test_malformed_json_returns_negative(void)
{
    char out[128];
    TEST_ASSERT_TRUE(config_diff_minify("not json", "{\"a\":1}", out, sizeof out) < 0);
    TEST_ASSERT_TRUE(config_diff_minify("{\"a\":1}", "[1,2,3]", out, sizeof out) < 0);
}

/* ---- config_diff_next_line: split a big minified object into <=250 B `config set` lines ---- */

void test_next_line_single_object_then_done(void)
{
    size_t cursor = 0;
    char line[256];
    int n = config_diff_next_line("{\"b\":9}", &cursor, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("{\"b\":9}", line);
    TEST_ASSERT_EQUAL_INT(7, n);
    /* consumed -> next call reports done */
    TEST_ASSERT_EQUAL_INT(0, config_diff_next_line("{\"b\":9}", &cursor, line, sizeof line));
}

void test_next_line_empty_object_is_done(void)
{
    size_t cursor = 0;
    char line[256];
    TEST_ASSERT_EQUAL_INT(0, config_diff_next_line("{}", &cursor, line, sizeof line));
}

void test_next_line_splits_when_over_budget(void)
{
    /* two fragments, each fits alone but together exceed CFG_SET_OBJ_MAX (239 B). */
    char obj[600];
    size_t p = 0;
    const char *a = "{\"q\":\"";
    const char *b = "\",\"r\":\"";
    const char *c = "\"}";
    memcpy(obj + p, a, strlen(a)); p += strlen(a);
    for (int i = 0; i < 150; i++) obj[p++] = 'A';
    memcpy(obj + p, b, strlen(b)); p += strlen(b);
    for (int i = 0; i < 150; i++) obj[p++] = 'B';
    memcpy(obj + p, c, strlen(c)); p += strlen(c);
    obj[p] = '\0';

    size_t cursor = 0;
    char line[256];
    int n1 = config_diff_next_line(obj, &cursor, line, sizeof line);
    TEST_ASSERT_TRUE(n1 > 0);
    TEST_ASSERT_TRUE((size_t)n1 <= CFG_SET_OBJ_MAX);
    TEST_ASSERT_TRUE(strstr(line, "\"q\":") != NULL);
    TEST_ASSERT_TRUE(strstr(line, "\"r\":") == NULL);   /* r spilled to the next line */

    int n2 = config_diff_next_line(obj, &cursor, line, sizeof line);
    TEST_ASSERT_TRUE(n2 > 0);
    TEST_ASSERT_TRUE(strstr(line, "\"r\":") != NULL);

    TEST_ASSERT_EQUAL_INT(0, config_diff_next_line(obj, &cursor, line, sizeof line));
}

/* ---- config_diff_escape: esp_console round-trip + escaped-length budget (B1) ---- */

/* A faithful emulator of esp_console_split_argv over a single (space-escaped) arg token: a
 * backslash escapes the next char (taken literally), a bare double quote toggles quoted mode and
 * is DROPPED, an unescaped space outside quotes ends the arg. This is exactly the transform the
 * lap-timer's REPL applies, so escape()->this must reproduce the original object byte-for-byte. */
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
 * config_diff_escape must produce a line that split_argv reconstructs back to the exact JSON. */
void test_escape_roundtrip_quotes(void)
{
    const char raw[] = "{\"units\":\"mph\"}";
    char esc[128], back[128];
    int n = config_diff_escape(raw, esc, sizeof esc);
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
    int n = config_diff_escape(raw, esc, sizeof esc);
    TEST_ASSERT_TRUE(n > 0);
    split_argv_token(esc, back, sizeof back);
    TEST_ASSERT_EQUAL_STRING(raw, back);
}

/* config_diff_escape reports overflow rather than truncating. */
void test_escape_overflow_returns_negative(void)
{
    char esc[4];
    TEST_ASSERT_TRUE(config_diff_escape("{\"a\":\"bbbb\"}", esc, sizeof esc) < 0);
}

/* THE B1 budget guarantee: config_diff_next_line packs each object so that, AFTER escaping, the
 * whole "config set <obj>" line stays within the 256 B console limit. A quote-heavy object packed
 * by raw length would blow past it (~20 quotes turn a 239 B raw object into ~260 B escaped). */
void test_next_line_escaped_line_within_console_cap(void)
{
    char obj[600];
    size_t p = 0;
    obj[p++] = '{';
    for (int i = 0; i < 20; i++) {                       /* many short string k/v pairs = many quotes */
        if (i) obj[p++] = ',';
        p += (size_t)snprintf(obj + p, sizeof obj - p, "\"k%02d\":\"v%02d\"", i, i);
    }
    obj[p++] = '}';
    obj[p] = '\0';

    size_t cursor = 0;
    char line[CFG_SET_OBJ_MAX + 1];
    char esc[CFG_SET_OBJ_MAX * 2 + 1];
    int emitted = 0, guard = 0;
    for (;;) {
        int m = config_diff_next_line(obj, &cursor, line, sizeof line);
        if (m == 0) break;
        TEST_ASSERT_TRUE(m > 0);
        int en = config_diff_escape(line, esc, sizeof esc);
        TEST_ASSERT_TRUE(en > 0);
        TEST_ASSERT_TRUE((size_t)en <= CFG_SET_OBJ_MAX);                      /* escaped object fits */
        TEST_ASSERT_TRUE((size_t)en + CFG_SET_PREFIX_LEN <= CFG_SET_LINE_MAX);/* full line <= 250 B */
        emitted++;
        TEST_ASSERT_TRUE(++guard < 50);                                       /* bounded */
    }
    TEST_ASSERT_TRUE(emitted >= 2);   /* the object was too big for one line -> it split */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_single_changed_key);
    RUN_TEST(test_no_changes_yields_empty_object);
    RUN_TEST(test_new_key_is_a_change);
    RUN_TEST(test_string_value_change);
    RUN_TEST(test_whitespace_insensitive);
    RUN_TEST(test_multiple_changes_preserve_order);
    RUN_TEST(test_nested_object_value_minified);
    RUN_TEST(test_single_change_over_cap_returns_negative);
    RUN_TEST(test_malformed_json_returns_negative);
    RUN_TEST(test_next_line_single_object_then_done);
    RUN_TEST(test_next_line_empty_object_is_done);
    RUN_TEST(test_next_line_splits_when_over_budget);
    RUN_TEST(test_escape_roundtrip_quotes);
    RUN_TEST(test_escape_roundtrip_space_and_backslash);
    RUN_TEST(test_escape_overflow_returns_negative);
    RUN_TEST(test_next_line_escaped_line_within_console_cap);
    return UNITY_END();
}
