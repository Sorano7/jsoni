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

typedef struct
{
    void *ctx;
    void *(*alloc)(void *ctx, size_t n);
    void (*free)(void *ctx, void *p);
} JsonAllocator;

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
        bool                                          boolean;
        struct { bool is_int; double d; int64_t i; }  num;
        struct { const char *data; size_t len; }      str;
        struct { const JsonValue *data; size_t len; } arr;
    } as;
};

#ifndef JSONI_CHUNK_SIZE
    #define JSONI_CHUNK_SIZE 2048
#endif

/* A JSON document. */
typedef struct JsonDoc JsonDoc;

#define json__error_codes(XX)                                                    \
    XX(JSON_ERR_UNEXPECTED_EOF,         "Unexpected end of file")                \
    XX(JSON_ERR_TRAILING_CHAR,          "Trailing characters")                   \
    XX(JSON_ERR_ILLEGAL_CHARACTER,      "Illegal character")                     \
    XX(JSON_ERR_INVALID_LITERAL,        "Invalid literal")                       \
                                                                                 \
    XX(JSON_ERR_MISSING_DIGIT,          "Missing digit in number")               \
    XX(JSON_ERR_MISSING_FRACTION_DIGIT, "Missing digit after decimal point")     \
    XX(JSON_ERR_MISSING_EXPONENT_DIGIT, "Missing digit after exponent")          \
    XX(JSON_ERR_NUMERIC_RANGE,          "Overflow or underflow in number")       \
    XX(JSON_ERR_LEADING_ZERO,           "Leading zeros are not allowed")         \
    XX(JSON_ERR_LEADING_PLUS,           "Leading plus sign is not allowed")      \
    XX(JSON_ERR_LEADING_POINT,          "Leading decimal point is not allowed")  \
                                                                                 \
    XX(JSON_ERR_UNTERMINATED_STRING,    "Unterminated string")                   \
    XX(JSON_ERR_INVALID_ESCAPE,         "Invalid escape sequence")               \
    XX(JSON_ERR_CONTROL_CHAR,           "Unescaped control character")           \
                                                                                 \
    XX(JSON_ERR_UNTERMINATED_ARRAY,     "Unterminated array")                    \
    XX(JSON_ERR_TRAILING_COMMA,         "Trailing comma")                        \

#define json__enum_member(M, ...) M,
#define json__enum_as_str(M, S) [M] = S,

/* JSON error codes. */
typedef enum {
    json__error_codes(json__enum_member)
} JsonErrorCode;

const char *json_error_messages[] = {
    json__error_codes(json__enum_as_str)
};

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
JsonDoc *json_parse(const char *src, size_t len, JsonError *err,
        const JsonAllocator *a);

/* Free the JSON document and all of its data. */
void json_free(JsonDoc *doc);

/* The root JSON value of the JSON document. */
const JsonValue *json_root(const JsonDoc *doc);

/* Convert the JSON value into int64. Returns false on failure. */
bool json_as_int64(const JsonValue *v, int64_t *out);

/* Convert the JSON value into double. Returns false on failure. */
bool json_as_double(const JsonValue *v, double *out);

/* Convert the JSON value into boolean. Returns false on failure. */
bool json_as_bool(const JsonValue *v, bool *out);

/*
 * Convert the JSON value into string and set n_out to length.
 * Returns NULL on failure.
 */
const char *json_as_str(const JsonValue *v, size_t *n_out);

/* Get the JSON value from v[i]. Returns null on failure. */
const JsonValue *json_array_get(const JsonValue *v, size_t i);

/* Get the array length from the JSON value. Returns false on failure. */
bool json_array_length(const JsonValue *v, size_t *out);

#endif // JSONI_H_


/***************************
 * Implementation Start
 ***************************/

#ifdef JSONI_IMPL

#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <math.h>

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
    JsonAllocator a;
};

/* Allocate n in the JSON document's arena. */
static void *json__arena_alloc(JsonDoc *doc, size_t n)
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
    if (!doc) return;

    JsonAllocator a = doc->a;
    for (JsonChunk *c = doc->head; c;)
    {
        JsonChunk *next = c->next;
        a.free(a.ctx, c);
        c = next;
    }
}

void *json_default_alloc(void *ctx, size_t n) { (void)ctx; return malloc(n); }
void json_default_free(void *ctx, void *p)    { (void)ctx; return free(p); }

/* Allocate a JSON document. */
static JsonDoc *json__doc_create(JsonAllocator a)
{
    JsonChunk *c = a.alloc(a.ctx, sizeof(*c) + JSONI_CHUNK_SIZE);
    assert(c);
    c->next = NULL;
    c->cap = JSONI_CHUNK_SIZE;

    JsonDoc *doc = (JsonDoc *)(c + 1);
    doc->head = c;
    doc->root = NULL;
    doc->a = a;

    c->used = sizeof(*doc);
    return doc;
}

/* A temporary stack of JSON values */
typedef struct
{
    JsonValue *data;
    size_t len, cap;
} JsonValueStack;

/* Free a dynamic array */
#define json__da_free(ac, da) do {        \
    if ((da)->data) (ac).free((ac).ctx, (da)->data); \
} while (0)

/* Grow or initialize a dynamic array by one */
#define json__da_grow(ac, da) do {                            \
    if ((da)->len >= (da)->cap) {                             \
        (da)->cap = (da)->cap == 0 ? 64 : (da)->cap * 2;      \
        size_t sz = (da)->cap * sizeof(*(da)->data);          \
        void *data = (ac).alloc((ac).ctx, sz);                \
        if ((da)->data) {                                     \
            memcpy(data, (da)->data, sz);                     \
            (ac).free((ac).ctx, (da)->data);                  \
        }                                                     \
        (da)->data = data;                                    \
        if (!(da)->data) abort();                             \
    }                                                         \
    (da)->len++;                                              \
} while (0)

/* Get the last slot of the dynamic array */
#define json__da_last(da) (&(da)->data[(da)->len-1])

/* Private JSON parser type. */
typedef struct
{
    const char *src;
    size_t len, pos;
    JsonDoc *doc;

    JsonValueStack value_stack;
} JsonParser;

/* Allocate a JSON value and set the kind */
static inline JsonValue *json__value_alloc(JsonParser *p, JsonKind kind, bool in_array)
{
    JsonValue *v;
    if (in_array)
    {
        json__da_grow(p->doc->a, &p->value_stack);
        v = json__da_last(&p->value_stack);
    }
    else
    {
        v = json__arena_alloc(p->doc, sizeof(*v));
    }
    v->kind = kind;
    return v;
}

/* Whether the parser is at EOF. */
#define json__is_eof(p) ((p)->pos >= (p)->len)

/* The current char the parser is at or '\0' if EOF. */
#define json__chr(p) (json__is_eof(p) ? '\0' : (p)->src[(p)->pos])

/* The current pointer into src or NULL if EOF. */
#define json__str(p) (json__is_eof(p) ? "" : (p)->src + (p)->pos)

/* The pointer to the last character of src. */
#define json__str_end(p) ((p)->src + (p)->len - 1)

#define json__is_digit(p) (!json__is_eof(p) && isdigit(json__chr(p)))

/* Advance parser until the next non-whitespace character or EOF. */
#define json__skip_ws(p) do {                                       \
    while (!json__is_eof(p) && isspace(json__chr(p)))               \
        (p)->pos++;                                                 \
} while (0)

/* Returns whether `c` is considered a token boundary. */
static inline bool json__is_token_boundary(char c)
{
    if (isdigit(c)) return true;
    if (isspace(c)) return true;

    return c == '{' || c == '}' || c == '[' || c == ']'
        || c == ':' || c == ',' || c == '.' || c == '"';
}

/* Return whether the next literal is `str`. */
static inline bool json__parser_match_str(JsonParser *p, const char *str, size_t n)
{
    if (n > p->len - p->pos) return false;

    const char *ptr = json__str(p);
    if (!ptr) return false;

    for (size_t i = 0; i < n; i++)
        if (str[i] != *ptr++) return false;

    if (ptr <= json__str_end(p))
        return json__is_token_boundary(*ptr);

    return true;
}

/* Fill the error with location and code, and return NULL for convenience. */
static const JsonValue *json__error(JsonParser *p, JsonError *err, JsonErrorCode code)
{
    assert(err);

    err->code = code;
    err->msg = json_error_messages[code];
    err->line = 0;
    err->column = 0;

    for (size_t i = 0; i < p->pos; i++)
    {
        if (p->src[i] == '\n')
        {
            err->line++;
            err->column = 0;
        }
        else
        {
            err->column++;
        }
    }

    return NULL;
}

static const JsonValue *json__parse(JsonParser *p, JsonError *err, bool in_array);

/*
 * Find the closing double-quote of a string literal and set out to the length.
 * Expects the current position to be the first character after the opening quote.
 * Return false if the string is not terminated.
 */
static bool json__find_closing_quotes(JsonParser *p, size_t *out, JsonError *err)
{
    size_t start = p->pos;

    for (; !json__is_eof(p); p->pos++)
    {
        unsigned char c = json__chr(p);
        if (c == '"') break;
        if (c == '\\') p->pos++;
    }
    if (json__is_eof(p) || json__chr(p) != '"')
        return json__error(p, err, JSON_ERR_UNTERMINATED_STRING);

    *out = p->pos - start;
    p->pos = start;
    return true;
}

/* Parse a JSON string value. */
static const JsonValue *json__parse_string(JsonParser *p, JsonError *err, bool in_array)
{
    p->pos++;
    const unsigned char *s = (unsigned char *)json__str(p);

    size_t ilen = 0;
    if (!json__find_closing_quotes(p, &ilen, err))
        return NULL;
    char *out = json__arena_alloc(p->doc, ilen);
    size_t olen = 0;

    size_t i = 0;
    for (; i < ilen; i++)
    {
        unsigned char c = s[i];
        if (c == '"') break;
        if (c < 0x20)
            return (void *)json__error(p, err, JSON_ERR_CONTROL_CHAR);

        if (c == '\\')
        {
            if (++i >= ilen)
                return (void *)json__error(p, err, JSON_ERR_UNTERMINATED_STRING);

            unsigned long cp;
            switch (c = s[i])
            {
                case '"':  cp = '"';  break;
                case '\\': cp = '\\'; break;
                case '/':  cp = '/';  break;
                case 'b':  cp = '\b'; break;
                case 'f':  cp = '\f'; break;
                case 'n':  cp = '\n'; break;
                case 'r':  cp = '\r'; break;
                case 't':  cp = '\t'; break;

                case 'u':
                {
                    assert("unicode escape not implemented" && false);
                }

                default:
                    return (void *)json__error(p, err, JSON_ERR_INVALID_ESCAPE);
            }

            out[olen++] = (unsigned char)cp;
        }
        else if (c < 0x80)
        {
            out[olen++] = c;
        }
        else
        {
            assert("utf8 validation not implemented" && false);
        }
    }
    p->pos += ++i;

    JsonValue *v = json__value_alloc(p, JSON_STRING, in_array);
    v->as.str.data = out;
    v->as.str.len = olen;
    return v;
}

/* Skip digits or return err_code if missing first digit */
#define json__skip_digits(p, err, err_code) do {    \
    if (!json__is_digit(p))                         \
        return json__error((p), (err), (err_code)); \
    while (json__is_digit(p)) (p)->pos++;           \
} while (0)

/* Parse a JSON number value. */
static const JsonValue *json__parse_number(JsonParser *p, JsonError *err, bool in_array)
{
    JsonValue *v = json__value_alloc(p, JSON_NUMBER, in_array);
    v->as.num.is_int = true;

    size_t start = p->pos;

    if (json__chr(p) == '-') p->pos++;

    if (json__chr(p) == '0')
        return json__error(p, err, JSON_ERR_LEADING_ZERO);
    json__skip_digits(p, err, JSON_ERR_MISSING_DIGIT);

    if (json__chr(p) == '.')
    {
        v->as.num.is_int = false;
        p->pos++;

        json__skip_digits(p, err, JSON_ERR_MISSING_FRACTION_DIGIT);
    }

    if (json__chr(p) == 'e')
    {
        v->as.num.is_int = false;
        p->pos++;

        if (json__chr(p) == '-' || json__chr(p) == '+')
            p->pos++;

        json__skip_digits(p, err, JSON_ERR_MISSING_EXPONENT_DIGIT);
    }

    size_t len = p->pos - start;
    char buffer[len+1];
    memcpy(buffer, p->src + start, len);
    buffer[len] = '\0';

    if (v->as.num.is_int)
    {
        errno = 0;
        v->as.num.i = strtoll(buffer, NULL, 10);
    }

    // Has fraction/exponent part or cannot fit into int64
    if (!v->as.num.is_int || errno == ERANGE)
    {
        v->as.num.is_int = false;

        errno = 0;
        v->as.num.d = strtod(buffer, NULL);
        if (errno == ERANGE || !isfinite(v->as.num.d))
            return json__error(p, err, JSON_ERR_NUMERIC_RANGE);
    }

    return v;
}

/* Parse a literal name (null, true, or false). */
static const JsonValue *json__parse_literal(JsonParser *p, JsonError *err, bool in_array)
{
    JsonValue *v = json__value_alloc(p, JSON_NULL, in_array);

    if (json__parser_match_str(p, "null",  4))
    {
        v->kind = JSON_NULL;
        p->pos += 4;
    }
    else if (json__parser_match_str(p, "true",  4))
    {
        v->kind = JSON_BOOL;
        v->as.boolean = true;
        p->pos += 4;
    }
    else if (json__parser_match_str(p, "false", 5))
    {
        v->kind = JSON_BOOL;
        v->as.boolean = false;
        p->pos += 5;
    }
    else
    {
        return json__error(p, err, JSON_ERR_INVALID_LITERAL);
    }
    return v;
}

/* Parse an array of JSON values */
static const JsonValue *json__parse_array(JsonParser *p, JsonError *err, bool in_array)
{
    p->pos++;
    json__skip_ws(p);

    JsonValue *v = json__value_alloc(p, JSON_ARRAY, in_array);
    v->as.arr.len = 0;
    v->as.arr.data = NULL;

    if (json__chr(p) == ']')
    {
        p->pos++;
        return v;
    }

    size_t stack_start = p->value_stack.len;

    while (!json__is_eof(p))
    {
        if (!json__parse(p, err, true))
            return NULL;

        v->as.arr.len++;

        json__skip_ws(p);

        if (json__chr(p) == ']')
            break;

        if (json__chr(p) == ',')
        {
            p->pos++;
            json__skip_ws(p);
            if (json__chr(p) == ']')
                return json__error(p, err, JSON_ERR_TRAILING_COMMA);
        }
    }

    if (json__chr(p) != ']')
        return json__error(p, err, JSON_ERR_UNTERMINATED_ARRAY);
    p->pos++;

    if (v->as.arr.len > 0)
    {
        size_t arr_size = v->as.arr.len * sizeof(JsonValue);
        JsonValue *data = json__arena_alloc(p->doc, arr_size);
        memcpy(data, p->value_stack.data + stack_start, arr_size);
        v->as.arr.data = data;

        p->value_stack.len = stack_start;
    }

    return v;
}

/* Parse a JSON value, return NULL if error. */
static const JsonValue *json__parse(JsonParser *p, JsonError *err, bool in_array)
{
    if (p->len == 0)
        return json__error(p, err, JSON_ERR_UNEXPECTED_EOF);

    json__skip_ws(p);

    if (json__is_eof(p))
        return json__error(p, err, JSON_ERR_UNEXPECTED_EOF);

    char c = json__chr(p);

    if (isdigit(c) || c == '-')
        return json__parse_number(p, err, in_array);

    if (isalpha(c))
        return json__parse_literal(p, err, in_array);

    switch (c)
    {
        case '+':
            return json__error(p, err, JSON_ERR_LEADING_PLUS);
        case '.':
            return json__error(p, err, JSON_ERR_LEADING_POINT);
        case '"':
            return json__parse_string(p, err, in_array);
        case '[':
            return json__parse_array(p, err, in_array);
        default:
            return json__error(p, err, JSON_ERR_ILLEGAL_CHARACTER);
    }

}

JsonDoc *json_parse(const char *src, size_t len, JsonError *err,
        const JsonAllocator *a)
{
    assert(src);
    if (len == 0) len = strlen(src);

    JsonAllocator ac = {
        .alloc = json_default_alloc,
        .free  = json_default_free,
        .ctx   = NULL,
    };
    if (a) ac = *a;

    JsonDoc *doc = json__doc_create(ac);

    JsonParser parser = {
        .src = src,
        .len = len,
        .pos = 0,
        .doc = doc,
    };

    doc->root = json__parse(&parser, err, false);
    if (!doc->root)
        goto error;

    if (!json__is_eof(&parser))
    {
        json__error(&parser, err, JSON_ERR_TRAILING_CHAR);
        goto error;
    }

    goto cleanup;

error:
    json_free(doc);
    doc = NULL;

cleanup:
    json__da_free(ac, &parser.value_stack);

    return doc;
}

const JsonValue *json_root(const JsonDoc *doc)
{
    if (!doc) return NULL;
    return doc->root;
}

bool json_as_int64(const JsonValue *v, int64_t *out)
{
    if (!v || v->kind != JSON_NUMBER)
        return false;

    if (!v->as.num.is_int)
    {
        if (v->as.num.d > (long double)INT64_MAX)
            return false;
        if (out) *out = (int64_t)v->as.num.d;
    }
    else
    {
        if (out) *out = v->as.num.i;
    }
    return true;
}

bool json_as_double(const JsonValue *v, double *out)
{
    if (!v || v->kind != JSON_NUMBER)
        return false;

    if (out)
    {
        if (v->as.num.is_int)
            *out = (double)v->as.num.i;
        else
            *out = v->as.num.d;
    }
    return true;
}

bool json_as_bool(const JsonValue *v, bool *out)
{
    if (!v || v->kind != JSON_BOOL)
        return false;

    if (out)
        *out = v->as.boolean;
    return true;
}

const char *json_as_str(const JsonValue *v, size_t *n_out)
{
    if (!v || v->kind != JSON_STRING)
        return NULL;

    if (n_out)
        *n_out = v->as.str.len;
    return v->as.str.data;
}

const JsonValue *json_array_get(const JsonValue *v, size_t i)
{
    if (!v || v->kind != JSON_ARRAY)
        return NULL;

    if (i >= v->as.arr.len)
        return NULL;

    return &v->as.arr.data[i];
}

bool json_array_length(const JsonValue *v, size_t *out)
{
    if (!v || v->kind != JSON_ARRAY)
        return false;

    if (out)
        *out = v->as.arr.len;
    return true;
}

#endif // JSONI_IMPL
