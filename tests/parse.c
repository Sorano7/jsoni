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

TEST(test_json_parse_number)
{
    PARSE_START();
        PARSE("1");
        CUT_MUST(root->kind == JSON_NUMBER);

        int64_t val;
        CUT_MUST(json_as_int64(root, &val));
        CUT_CHECK(val == 1);

        PARSE("1234");
        CUT_MUST(json_as_int64(root, &val));
        CUT_CHECK(val == 1234);
    PARSE_END();
}

TEST_RUN()
