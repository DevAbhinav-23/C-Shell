/*
 * lexer.c  --  Tokenizer for shell input
 *
 * Converts a raw input string into a flat array of Token values.
 * Whitespace is consumed and never emitted as a token.
 */
#include "shell.h"
#include "lexer.h"

/* ------------------------------------------------------------------ */
/*  Dynamic token buffer (grows as needed)                             */
/* ------------------------------------------------------------------ */
typedef struct {
    Token *items;
    int    count;
    int    capacity;
} TokenBuf;

static void tbuf_init(TokenBuf *tb)
{
    tb->capacity = 64;
    tb->count    = 0;
    tb->items    = malloc(sizeof(Token) * (size_t)tb->capacity);
}

static void tbuf_push(TokenBuf *tb, TokenType type, const char *value)
{
    if (tb->count >= tb->capacity) {
        tb->capacity *= 2;
        tb->items = realloc(tb->items, sizeof(Token) * (size_t)tb->capacity);
    }
    tb->items[tb->count].type  = type;
    tb->items[tb->count].value = shell_strdup(value);
    tb->count++;
}

/* ------------------------------------------------------------------ */
/*  Helper: check if a character is whitespace                         */
/* ------------------------------------------------------------------ */
static int is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* ------------------------------------------------------------------ */
/*  Helper: check if a character is a shell metacharacter              */
/* ------------------------------------------------------------------ */
static int is_meta(char c)
{
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

/* ------------------------------------------------------------------ */
/*  Read a WORD token: contiguous non-special, non-whitespace chars    */
/* ------------------------------------------------------------------ */
static const char *read_word(const char *p, TokenBuf *tb)
{
    const char *start = p;
    while (*p && !is_ws(*p) && !is_meta(*p))
        p++;

    size_t len = (size_t)(p - start);
    char  *buf = malloc(len + 1);
    memcpy(buf, start, len);
    buf[len] = '\0';

    tbuf_push(tb, TOK_WORD, buf);
    free(buf);
    return p;
}

/* ------------------------------------------------------------------ */
/*  Main tokenize function                                             */
/* ------------------------------------------------------------------ */
Token *lexer_tokenize(const char *input, int *out_count)
{
    TokenBuf tb;
    tbuf_init(&tb);

    const char *p = input;

    while (*p) {
        /* Skip whitespace */
        if (is_ws(*p)) {
            p++;
            continue;
        }

        switch (*p) {
        case '&':
            tbuf_push(&tb, TOK_AND, "&");
            p++;
            break;

        case ';':
            tbuf_push(&tb, TOK_SEMICOLON, ";");
            p++;
            break;

        case '|':
            tbuf_push(&tb, TOK_PIPE, "|");
            p++;
            break;

        case '<':
            tbuf_push(&tb, TOK_INPUT, "<");
            p++;
            break;

        case '>':
            if (*(p + 1) == '>') {
                tbuf_push(&tb, TOK_OUTPUT_APPEND, ">>");
                p += 2;
            } else {
                tbuf_push(&tb, TOK_OUTPUT, ">");
                p++;
            }
            break;

        default:
            /* Word token */
            p = read_word(p, &tb);
            break;
        }
    }

    /* Append a sentinel EOF token */
    tbuf_push(&tb, TOK_EOF, "");

    *out_count = tb.count;
    return tb.items;
}

/* ------------------------------------------------------------------ */
/*  Free token array                                                   */
/* ------------------------------------------------------------------ */
void lexer_free_tokens(Token *tokens, int count)
{
    for (int i = 0; i < count; i++)
        free(tokens[i].value);
    free(tokens);
}
