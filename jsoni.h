/*
 * jsoni  - a single-header JSON parser and serializer library.
 *
 * Usage: Include jsoni.h and define JSONI_IMPL in exactly one translation unit.
 *
 * Naming conventions:
 *     - Flags or redefinable macros:   JSONI_*
 *     - Public API:                    json_*
 *     - Private/static methods:        json__*
 */

#ifndef JSONI_H_
#define JSONI_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Kinds of a JSON value. */
typedef enum
{
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonKind;

/* Tagged union for JSON values. */
typedef struct JsonValue JsonValue;

struct JsonValue
{
    JsonKind kind;
    union
    {
        bool                                         boolean;
        struct { bool is_int; double d; int64_t i; } num;
        struct { const char *s; size_t len; }        str;
    } as;
};

#ifndef JSONI_CHUNK_SIZE
    #define JSONI_CHUNK_SIZE 2048
#endif

/* A JSON document. */
typedef struct JsonDoc JsonDoc;

/* JSON error codes. */
typedef enum
{
    JSON_ERR_UNEXPECTED_EOF,
    JSON_ERR_ILLEGAL_CHARACTER,
    JSON_ERR_INVALID_LITERAL,
} JsonErrorCode;

/* Error information. */
typedef struct JsonError
{
    JsonErrorCode code; // the error code
    int line, column;   // error location
    const char *msg;    // static message string
} JsonError;

/*
 * Parse a JSON string from `src` to `src+len`, or null-terminator if `len` is 0.
 * Returns an allocated JsonDoc on success, or NULL and fills `err` on error .
 */
JsonDoc *json_parse(const char *src, size_t len, JsonError *err);

/* Free the JSON document and all of its data. */
void json_free(JsonDoc *doc);

/* The root JSON value of the JSON document. */
const JsonValue *json_root(const JsonDoc *doc);

/* Convert the JSON value into int64. Returns false on failure. */
bool json_as_int64(const JsonValue *v, int64_t *out);

#endif // JSONI_H_


/***************************
 * Implementation Start
 ***************************/

#ifdef JSONI_IMPL

#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

/* A chunk of memory */
typedef struct JsonChunk
{
    struct JsonChunk *next;
    size_t cap, used;
    /* payload, aligned by 8 */
} JsonChunk;

#define JSONI_CHUNK_ALIGN 8

struct JsonDoc
{
    JsonChunk *head; /* JsonDoc is allocated within head */
    const JsonValue *root;
};

/* Allocate n in the JSON document's arena. */
static void *json__alloc(JsonDoc *doc, size_t n)
{
    n = (n + (JSONI_CHUNK_ALIGN-1)) & ~(size_t)(JSONI_CHUNK_ALIGN-1);
    JsonChunk *c = doc->head;

    // Oversized chunk handling
    if (c && n > c->cap / 2)
    {
        JsonChunk *nc = calloc(1, sizeof(*nc) + n);
        if (!nc) return NULL;

        nc->cap = (nc->used = n);
        nc->next = c->next;
        c->next = nc;
        return (char *)(nc + 1);
    }

    if (!c || (c->cap - c->used) < n)
    {
        size_t cap = c ? c->cap * 2 : JSONI_CHUNK_SIZE;
        if (cap < n) cap = n;

        JsonChunk *nc = calloc(1, sizeof(*nc) + cap);
        if (!nc) return NULL;

        nc->next = c;
        nc->cap  = cap;
        nc->used = 0;

        doc->head = (c = nc);
    }

    void *p = (char *)(c+1) + c->used;
    c->used += n;
    return p;
}

void json_free(JsonDoc *doc)
{
    for (JsonChunk *c = doc->head; c;)
    {
        JsonChunk *next = c->next;
        free(c);
        c = next;
    }
}

/* Allocate a JSON document. */
static JsonDoc *json__doc_create(void)
{
    JsonChunk *c = calloc(1, sizeof(*c) + JSONI_CHUNK_SIZE);
    assert(c);
    c->next = NULL;
    c->cap = JSONI_CHUNK_SIZE;

    JsonDoc *doc = (JsonDoc *)(c + 1);
    doc->head = c;
    doc->root = NULL;

    c->used = sizeof(*doc);
    return doc;
}

/* Private JSON parser type. */
typedef struct
{
    const char *src;
    size_t len, pos;
    JsonDoc *doc;
} JsonParser;

/* Whether the parser is at EOF. */
#define json__parser_is_eof(p) ((p)->pos >= (p)->len)

/* The current char the parser is at or '\0' if EOF. */
#define json__parser_chr(p) (json__parser_is_eof(p) ? '\0' : (p)->src[(p)->pos])

/* The current pointer into src or NULL if EOF. */
#define json__parser_str(p) (json__parser_is_eof(p) ? "" : (p)->src + (p)->pos)

/* The pointer to the last character of src. */
#define json__parser_str_end(p) ((p)->src + (p)->len - 1)

/* Advance parser until the next non-whitespace character or EOF. */
#define json__parser_skip_ws(p) do {                                \
    while (!json__parser_is_eof(p) && isspace(json__parser_chr(p))) \
        (p)->pos++;                                                 \
} while (0)

/* Returns whether `c` is considered a token boundary. */
static inline bool json__is_token_boundary(char c)
{
    if (isdigit(c)) return true;
    if (isspace(c)) return true;

    return c == '{' || c == '}' || c == '[' || c == ']'
        || c == ':' || c == ',' || c == '.';
}

/* Return whether the next literal is `str`. */
static inline bool json__parser_match_str(JsonParser *p, const char *str, size_t n)
{
    if (n > p->len - p->pos) return false;

    const char *ptr = json__parser_str(p);
    if (!ptr) return false;

    for (size_t i = 0; i < n; i++)
        if (str[i] != *ptr++) return false;

    if (ptr <= json__parser_str_end(p))
        return json__is_token_boundary(*ptr);

    return true;
}

static const char *json_error_messages[] = {
    [JSON_ERR_UNEXPECTED_EOF]    = "Unexpected end of file",
    [JSON_ERR_ILLEGAL_CHARACTER] = "Illegal character",
    [JSON_ERR_INVALID_LITERAL]   = "Invalid literal",
};

/* Fill the error with location and code, and return NULL for convenience. */
static const JsonValue *json__error(JsonParser *p, JsonError *err, JsonErrorCode code)
{
    assert(err);

    err->code = code;
    err->msg = json_error_messages[code];
    err->line = 0;

    size_t start = 0;
    for (; start < p->pos; start++)
    {
        if (p->src[start] == '\n')
            err->line++;
    }
    err->column = p->pos - start;

    return NULL;
}

static const JsonValue *json__parse(JsonParser *p, JsonError *err);

/* Parse a JSON number value. */
static const JsonValue *json__parse_number(JsonParser *p, JsonError *err)
{
    (void)err;

    JsonValue *v = json__alloc(p->doc, sizeof(*v));
    v->kind = JSON_NUMBER;
    v->as.num.is_int = true;
    v->as.num.i = 0;

    while (!json__parser_is_eof(p) && isdigit(json__parser_chr(p)))
    {
        v->as.num.i *= 10;
        v->as.num.i += json__parser_chr(p) - '0';
        p->pos++;
    }

    return v;
}

/* Parse a literal name (null, true, or false). */
static const JsonValue *json__parse_literal(JsonParser *p, JsonError *err)
{
    JsonValue *v = json__alloc(p->doc, sizeof(*v));

    if (json__parser_match_str(p, "null",  4))
    {
        v->kind = JSON_NULL;
    }
    else if (json__parser_match_str(p, "true",  4))
    {
        v->kind = JSON_BOOL;
        v->as.boolean = true;
    }
    else if (json__parser_match_str(p, "false", 5))
    {
        v->kind = JSON_BOOL;
        v->as.boolean = false;
    }
    else
    {
        return json__error(p, err, JSON_ERR_INVALID_LITERAL);
    }
    return v;
}

/* Parse a JSON value, return NULL if error. */
static const JsonValue *json__parse(JsonParser *p, JsonError *err)
{
    if (p->len == 0)
        return json__error(p, err, JSON_ERR_UNEXPECTED_EOF);

    json__parser_skip_ws(p);

    if (isdigit(json__parser_chr(p)))
        return json__parse_number(p, err);

    if (isalpha(json__parser_chr(p)))
        return json__parse_literal(p, err);

    return json__error(p, err, JSON_ERR_ILLEGAL_CHARACTER);
}

JsonDoc *json_parse(const char *src, size_t len, JsonError *err)
{
    assert(src);
    if (len == 0) len = strlen(src);

    JsonDoc *doc = json__doc_create();

    JsonParser parser = {
        .src = src, .len = len,
        .pos = 0,   .doc = doc,
    };

    doc->root = json__parse(&parser, err);
    if (!doc->root)
    {
        json_free(doc);
        return NULL;
    }

    return doc;
}

const JsonValue *json_root(const JsonDoc *doc)
{
    return doc->root;
}

bool json_as_int64(const JsonValue *v, int64_t *out)
{
    if (v->kind != JSON_NUMBER || !v->as.num.is_int)
        return false;

    *out = v->as.num.i;
    return true;
}

#endif // JSONI_IMPL
