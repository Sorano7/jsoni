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
    CUT_MUST(doc != NULL);                                 \
    root = json_root(doc);                                 \
} while (0)

#define PARSE_FAIL(src, c) do {                            \
    (void)root;                                            \
    if (doc) json_free(doc);                               \
    doc = json_parse((src), sizeof(src)-1, &err);          \
    CUT_MUST(doc == NULL);                                 \
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

        PARSE("-123e-45");
        CUT_MUST(json_as_double(root, &val));
        CUT_CHECK(approx(val, -123e-45));

        PARSE_FAIL("123e", JSON_ERR_MISSING_EXPONENT_DIGIT);
        PARSE_FAIL("1e999", JSON_ERR_NUMERIC_RANGE);
    PARSE_END();
}

TEST_RUN()
