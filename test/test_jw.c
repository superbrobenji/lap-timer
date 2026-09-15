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
    RUN_TEST(test_parse_rejects_documents_deeper_than_the_cap);
    RUN_TEST(test_tok_str_with_zero_capacity_writes_nothing);
    RUN_TEST(test_fuzz_jw_str_always_emits_a_parsable_string);
    return UNITY_END();
}
