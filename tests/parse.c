#define JSONI_IMPL
#include "../jsoni.h"

#define CUT_IMPL
#include "../shared/cut.h"

#define PARSE_START()                                      \
    JsonError err;                                         \
    JsonDoc *doc = NULL;                                   \
    const JsonValue *root = NULL;

#define PARSE(src) do {                                    \
    if (doc) json_free(doc);                               \
    doc = json_parse((src), sizeof(src)-1, &err);          \
    if (doc == NULL)                                       \
        CUT_FATAL("Failed to parse "#src": %s (%d:%d)",    \
                err.msg, err.line, err.column);            \
    root = json_root(doc);                                 \
} while (0)

#define PARSE_FAIL(src, c) do {                            \
    (void)root;                                            \
    if (doc) json_free(doc);                               \
    doc = json_parse((src), sizeof(src)-1, &err);          \
    if (doc != NULL)                                       \
        CUT_FATAL("Did not fail to parse "#src);           \
    CUT_CHECK(err.code == c);                              \
} while (0)

#define PARSE_END() \
    if (doc) json_free(doc);

#define approx(da, db) (fabs(da - db) < 1e-6)

TEST(test_json_empty_source)
{
    PARSE_START();
        PARSE_FAIL("", JSON_ERR_UNEXPECTED_EOF);
    PARSE_END();
}

TEST(test_json_invalid_char)
{
    PARSE_START();
        PARSE_FAIL("%", JSON_ERR_ILLEGAL_CHARACTER);
    PARSE_END();
}

TEST(test_json_trailing)
{
    PARSE_START();
        PARSE_FAIL("123abc", JSON_ERR_TRAILING);
        PARSE_FAIL("null123", JSON_ERR_TRAILING);
        PARSE_FAIL("\"abc\"123", JSON_ERR_TRAILING);
    PARSE_END();
}

TEST(test_json_parse_special_literals)
{
    PARSE_START();
        PARSE("null");
        CUT_MUST(root->kind == JSON_NULL);

        PARSE("true");
        CUT_MUST(root->kind == JSON_BOOL);
        CUT_CHECK(root->as.boolean == true);

        PARSE("false");
        CUT_MUST(root->kind == JSON_BOOL);
        CUT_CHECK(root->as.boolean == false);

        PARSE_FAIL("nullll", JSON_ERR_INVALID_LITERAL);
    PARSE_END();
}

TEST(test_json_parse_integer)
{
    PARSE_START();
        int64_t val;

        PARSE("1");
        CUT_MUST(json_as_int64(root, &val));
        CUT_CHECK(val == 1);

        PARSE("1234");
        CUT_MUST(json_as_int64(root, &val));
        CUT_CHECK(val == 1234);

        PARSE("-9223372036854775807");
        CUT_MUST(json_as_int64(root, &val));
        CUT_CHECK(val == -9223372036854775807LL);

        PARSE("9223372036854775808");
        CUT_CHECK(!json_as_int64(root, &val));

        double dv;
        CUT_MUST(json_as_double(root, &dv));
        CUT_CHECK(approx(dv, 9223372036854775808.0));

        PARSE_FAIL("01", JSON_ERR_LEADING_ZERO);

        PARSE_FAIL("+1", JSON_ERR_LEADING_PLUS);
    PARSE_END();
}

TEST(test_parse_decimal)
{
    PARSE_START();
        double val;

        PARSE("1.23");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, 1.23));

        PARSE("-1.0");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, -1.0));

        PARSE("9223372036800000000.0");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, 9223372036800000000.0));

        int64_t iv;
        CUT_MUST(json_as_int64(root, &iv));
        CUT_CHECK(iv == 9223372036800000000LL);

        PARSE_FAIL(".123", JSON_ERR_LEADING_POINT);
        PARSE_FAIL("123.", JSON_ERR_MISSING_FRACTION_DIGIT);
    PARSE_END();
}

TEST(test_parse_exponent)
{
    PARSE_START();
        double val;

        PARSE("1e9");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, 1e9));

        PARSE("12e+3");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, 12e+3));

        PARSE("-123e-45");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, -123e-45));

        PARSE_FAIL("123e", JSON_ERR_MISSING_EXPONENT_DIGIT);
        PARSE_FAIL("1e999", JSON_ERR_NUMERIC_RANGE);
    PARSE_END();
}

TEST(test_parse_plain_string)
{
    PARSE_START();
        StringView s;

        PARSE("\"hello, world\"");
        CUT_MUST(s.data = json_as_str(root, &s.len));
        CUT_CHECK(sv_equal(s, "hello, world"));

        PARSE("\"\"");
        CUT_MUST(s.data = json_as_str(root, &s.len));
        CUT_CHECK(sv_equal(s, ""));

        PARSE("\"\\\"abc\\\"\"");
        CUT_MUST(s.data = json_as_str(root, &s.len));
        CUT_CHECK(sv_equal(s, "\"abc\""));

        PARSE_FAIL("\"abc",    JSON_ERR_UNTERMINATED_STRING);
        PARSE_FAIL("\"\\\"",   JSON_ERR_UNTERMINATED_STRING);
        PARSE_FAIL("\"\\a\"",  JSON_ERR_INVALID_ESCAPE);
        PARSE_FAIL("\"\x0a\"", JSON_ERR_CONTROL_CHAR);
    PARSE_END();
}

TEST(test_parse_array)
{
    PARSE_START();
        size_t sz;
        int64_t int_val;
        StringView str_val;
        bool bool_val;

        PARSE("[1, \"hello\", true]");
        CUT_MUST(json_array_length(root, &sz));
        CUT_CHECK(sz == 3);

        const JsonValue *v1 = json_array_get(root, 0);
        CUT_MUST(json_as_int64(v1, &int_val));
        CUT_CHECK(int_val == 1);

        const JsonValue *v2 = json_array_get(root, 1);
        CUT_MUST(str_val.data = json_as_str(v2, &str_val.len));
        CUT_CHECK(sv_equal(str_val, "hello"));

        const JsonValue *v3 = json_array_get(root, 2);
        CUT_MUST(json_as_bool(v3, &bool_val));
        CUT_CHECK(bool_val == true);
    PARSE_END();
}

TEST_RUN()
