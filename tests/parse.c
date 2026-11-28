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
    doc = json_parse_n((src), sizeof(src)-1, &err, NULL);    \
    if (!doc)                                       \
        CUT_FATAL("Failed to parse "#src": %s (%d:%d)",    \
                err.msg, err.line, err.column);            \
    root = json_root(doc);                                 \
} while (0)

#define PARSE_FAIL(src, c) do {                            \
    (void)root;                                            \
    if (doc) json_free(doc);                               \
    doc = json_parse_n((src), sizeof(src)-1, &err, NULL);    \
    if (doc)                                       \
        CUT_FATAL("Did not fail to parse "#src);           \
    if (err.code != c)                                     \
        CUT_ERROR("expected %s, got %s",                   \
                json_error_messages[c],                    \
                json_error_messages[err.code]);            \
} while (0)

#define PARSE_END() \
    if (doc) json_free(doc);

#define INT_EQ(jv, want) do {            \
    int64_t val;                         \
    CUT_MUST(json_as_int64((jv), &val)); \
    CUT_CHECK(val == (want));            \
} while (0)

#define DOUBLE_EQ(jv, want) do {          \
    double val;                           \
    CUT_MUST(json_as_double((jv), &val)); \
    CUT_CHECK(approx(val, (want)));       \
} while (0)

#define BOOL_EQ(jv, want) do {          \
    bool val;                           \
    CUT_MUST(json_as_bool((jv), &val)); \
    CUT_CHECK(val == (want));           \
} while (0)

#define STR_EQ(jv, want) do {                             \
    StringView val__;                                     \
    CUT_MUST(val__.data = json_as_str((jv), &val__.len)); \
    CUT_CHECK(sv_equal(val__, (want)));                   \
} while (0)

#define ARR_LEN_EQ(jv, want) do {           \
    size_t sz;                              \
    CUT_MUST(json_array_length((jv), &sz)); \
    CUT_CHECK(sz == (want));                \
} while (0)

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
        PARSE_FAIL("123abc", JSON_ERR_TRAILING_CHAR);
        PARSE_FAIL("null123", JSON_ERR_TRAILING_CHAR);
        PARSE_FAIL("\"abc\"123", JSON_ERR_TRAILING_CHAR);
    PARSE_END();
}

TEST(test_json_parse_special_literals)
{
    PARSE_START();
        PARSE("null");
        CUT_MUST(json_as_null(root));

        PARSE("true");
        BOOL_EQ(root, true);

        PARSE("false");
        BOOL_EQ(root, false);

        PARSE_FAIL("nullll", JSON_ERR_INVALID_LITERAL);
    PARSE_END();
}

TEST(test_json_parse_integer)
{
    PARSE_START();
        PARSE("1");
        INT_EQ(root, 1);

        PARSE("1234");
        INT_EQ(root, 1234);

        PARSE("-9223372036854775807");
        INT_EQ(root, -9223372036854775807LL);

        PARSE("9223372036854775808");
        CUT_CHECK(!json_as_int64(root, NULL));
        DOUBLE_EQ(root, 9223372036854775808.0);

        PARSE_FAIL("01", JSON_ERR_LEADING_ZERO);
        PARSE_FAIL("+1", JSON_ERR_LEADING_PLUS);
    PARSE_END();
}

TEST(test_parse_decimal)
{
    PARSE_START();
        PARSE("1.23");
        DOUBLE_EQ(root, 1.23);

        PARSE("-1.0");
        DOUBLE_EQ(root, -1.0);

        PARSE("9223372036800000000.0");
        DOUBLE_EQ(root, 9223372036800000000.0);
        INT_EQ(root, 9223372036800000000LL);

        PARSE_FAIL(".123", JSON_ERR_LEADING_POINT);
        PARSE_FAIL("123.", JSON_ERR_MISSING_FRACTION_DIGIT);
    PARSE_END();
}

TEST(test_parse_exponent)
{
    PARSE_START();
        PARSE("1e9");
        DOUBLE_EQ(root, 1e9);

        PARSE("12e+3");
        DOUBLE_EQ(root, 12e+3);

        PARSE("-123e-45");
        DOUBLE_EQ(root, -123e-45);

        PARSE_FAIL("123e", JSON_ERR_MISSING_EXPONENT_DIGIT);
        PARSE_FAIL("1e999", JSON_ERR_NUMERIC_RANGE);
    PARSE_END();
}

TEST(test_parse_plain_string)
{
    PARSE_START();
        PARSE("\"hello, world\"");
        STR_EQ(root, "hello, world");

        PARSE("\"\"");
        STR_EQ(root, "");

        PARSE("\"\\\"abc\\\"\"");
        STR_EQ(root, "\"abc\"");

        PARSE_FAIL("\"abc",    JSON_ERR_UNTERMINATED_STRING);
        PARSE_FAIL("\"\\\"",   JSON_ERR_UNTERMINATED_STRING);
        PARSE_FAIL("\"\\a\"",  JSON_ERR_INVALID_ESCAPE);
        PARSE_FAIL("\"\x0a\"", JSON_ERR_CONTROL_CHAR);
    PARSE_END();
}

TEST(test_parse_array)
{
    PARSE_START();
        PARSE("[1, \"hello\", true]");
        ARR_LEN_EQ(root, 3);

        const JsonValue *v1 = json_array_get(root, 0);
        INT_EQ(v1, 1);

        const JsonValue *v2 = json_array_get(root, 1);
        STR_EQ(v2, "hello");

        const JsonValue *v3 = json_array_get(root, 2);
        BOOL_EQ(v3, true);

        PARSE("[]");
        ARR_LEN_EQ(root, 0);

        PARSE_FAIL("[", JSON_ERR_UNTERMINATED_ARRAY);
        PARSE_FAIL("[1,]", JSON_ERR_TRAILING_COMMA);
    PARSE_END();
}

TEST(test_parse_nested_array)
{
    PARSE_START();
        PARSE("[1, [2, 3], [[4]]]");
        ARR_LEN_EQ(root, 3);

        const JsonValue *v1 = json_array_get(root, 0);
        INT_EQ(v1, 1);

        const JsonValue *v2 = json_array_get(root, 1);
        ARR_LEN_EQ(v2, 2);
        const JsonValue *v2_1 = json_array_get(v2, 0);
        INT_EQ(v2_1, 2);
        const JsonValue *v2_2 = json_array_get(v2, 1);
        INT_EQ(v2_2, 3);

        const JsonValue *v3 = json_array_get(root, 2);
        ARR_LEN_EQ(v3, 1);
        const JsonValue *v3_1 = json_array_get(v3, 0);
        ARR_LEN_EQ(v3_1, 1);
        const JsonValue *v3_1_1 = json_array_get(v3_1, 0);
        INT_EQ(v3_1_1, 4);

    PARSE_END();
}

TEST(test_parse_object)
{
    PARSE_START();
        PARSE("{\"value\": 1, \"\": true}");
        const JsonValue *v1 = json_object_get(root, "value");
        INT_EQ(v1, 1);

        const JsonValue *v2 = json_object_get(root, "");
        BOOL_EQ(v2, true);

        CUT_CHECK(!json_object_get(root, "key"));

        PARSE("{}");

        PARSE_FAIL("{1}", JSON_ERR_OBJ_MISSING_KEY);
        PARSE_FAIL("{\"\"}", JSON_ERR_OBJ_MISSING_COLON);
        PARSE_FAIL("{\"\": }", JSON_ERR_OBJ_MISSING_VALUE);
    PARSE_END();
}

TEST(test_parse_nested_object)
{
    PARSE_START();
        PARSE("{\"inner\": {\"value\": 100}}");
        const JsonValue *v = json_object_get(json_object_get(root, "inner"), "value");
        INT_EQ(v, 100);
    PARSE_END();
}

TEST(test_parse_nested_array_and_object)
{
    PARSE_START();
        PARSE(
            "{                              "
            "   \"arr\": [                  "
            "       {                       "
            "           \"foo\": true,      "
            "           \"bar\": \"hello\"  "
            "       },                      "
            "       {                       "
            "           \"baz\": [1, null]  "
            "       }                       "
            "   ]                           "
            "}                              "
        );
        const JsonValue *arr = json_object_get(root, "arr");
        ARR_LEN_EQ(arr, 2);

        const JsonValue *obj1 = json_array_get(arr, 0);
        BOOL_EQ(json_object_get(obj1, "foo"), true);
        STR_EQ(json_object_get(obj1, "bar"), "hello");

        const JsonValue *obj2 = json_array_get(arr, 1);
        const JsonValue *baz = json_object_get(obj2, "baz");
        ARR_LEN_EQ(baz, 2);
        INT_EQ(json_array_get(baz, 0), 1);
        CUT_CHECK(json_as_null(json_array_get(baz, 1)));
    PARSE_END();
}

typedef struct
{
    size_t allocd, freed;
} TestAllocCtx;

typedef struct
{
    size_t size;
} TestAllocHeader;

static void *test_alloc(void *ctx, size_t n)
{
    TestAllocCtx *c = ctx;
    c->allocd += n;

    TestAllocHeader *h = malloc(sizeof(*h) + n);
    memset(h, 0xcd, sizeof(*h)+n);
    h->size = n;
    return h + 1;
}

static void test_free(void *ctx, void *p)
{
    TestAllocHeader *h = ((TestAllocHeader *)p) - 1;
    TestAllocCtx *c = ctx;
    c->freed += h->size;
    free(h);
}

TEST(test_custom_allocator)
{
    TestAllocCtx ctx = {0};
    JsonAllocator a = {
        .alloc = test_alloc,
        .free = test_free,
        .ctx = &ctx,
    };

    PARSE_START();
        StringView src = SV("[1, \"hello\", true]");
        doc = json_parse_n(src.data, src.len, &err, &a);
        CUT_MUST(doc && (root = json_root(doc)));
        INT_EQ(json_array_get(root, 0), 1);
    PARSE_END();

    CUT_CHECK(ctx.allocd > 0 && ctx.allocd == ctx.freed);
}

TEST_RUN()
