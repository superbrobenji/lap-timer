#include "unity.h"
#include "core/jw.h"
#include "core/json.h"
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
    int vn = json_obj_get(js, toks, 0, "n"); int64_t iv; TEST_ASSERT_TRUE(json_tok_int(js, &toks[vn], &iv)); TEST_ASSERT_EQUAL_INT64(-42, iv);
    int vs = json_obj_get(js, toks, 0, "s"); char s[8]; json_tok_str(js, &toks[vs], s, sizeof s); TEST_ASSERT_EQUAL_STRING("hi", s);
    int va = json_obj_get(js, toks, 0, "arr"); TEST_ASSERT_EQUAL_INT(JSMN_ARRAY, toks[va].type); TEST_ASSERT_EQUAL_INT(3, toks[va].size);
    int after = json_skip(toks, va);
    TEST_ASSERT_TRUE(json_tok_eq(js, &toks[after], "f"));
    int vf = json_obj_get(js, toks, 0, "f"); double dv; TEST_ASSERT_TRUE(json_tok_double(js, &toks[vf], &dv)); TEST_ASSERT_DOUBLE_WITHIN(1e-12, 1.5, dv);
    int vt = json_obj_get(js, toks, 0, "t"); bool bv; TEST_ASSERT_TRUE(json_tok_bool(js, &toks[vt], &bv)); TEST_ASSERT_TRUE(bv);
    TEST_ASSERT_EQUAL_INT(-1, json_obj_get(js, toks, 0, "missing"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_produces_expected_document);
    RUN_TEST(test_writer_overflow_is_flagged_and_terminated);
    RUN_TEST(test_tokenizer_helpers);
    return UNITY_END();
}
