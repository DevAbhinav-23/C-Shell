/*
 * parser.c  --  Recursive descent parser for the shell grammar (A.3)
 *
 * Parses a token stream into a ShellCmd tree.
 * If the input is syntactically valid, returns a populated ShellCmd.
 * If invalid, returns NULL and sets *out_valid = 0.
 *
 * Grammar:
 *   shell_cmd   -> cmd_group ( (&|;) cmd_group )*  &?
 *   cmd_group   -> atomic ( PIPE atomic )*
 *   atomic      -> WORD ( WORD | input | output )*
 *   input       -> INPUT WORD
 *   output      -> (OUTPUT | OUTPUT_APPEND) WORD
 */
#include "shell.h"
#include "parser.h"

/* ================================================================== */
/*  Parser cursor                                                       */
/* ================================================================== */
typedef struct {
    Token *tokens;
    int    count;
    int    pos;
} Parser;

static Token *cur(Parser *p)
{
    return (p->pos < p->count) ? &p->tokens[p->pos] : &p->tokens[p->count - 1];
}

static int cur_type(Parser *p) { return cur(p)->type; }

static int advance(Parser *p)
{
    if (p->pos < p->count) p->pos++;
    return 1;
}

/* ================================================================== */
/*  Dynamic string array  (for accumulating args)                       */
/* ================================================================== */
typedef struct {
    char **items;
    int    count;
    int    capacity;
} StrArray;

static void sa_init(StrArray *sa)  { sa->capacity = 0; sa->count = 0; sa->items = NULL; }
static void sa_free(StrArray *sa)
{
    for (int i = 0; i < sa->count; i++) free(sa->items[i]);
    free(sa->items);
}
static void sa_push(StrArray *sa, const char *s)
{
    if (sa->count >= sa->capacity) {
        sa->capacity = sa->capacity ? sa->capacity * 2 : 8;
        sa->items = realloc(sa->items, sizeof(char *) * (size_t)sa->capacity);
    }
    sa->items[sa->count++] = shell_strdup(s);
}

/* ================================================================== */
/*  Dynamic arrays for Redirect, AtomicCmd, CmdGroup                    */
/* ================================================================== */

typedef struct {
    Redirect *items;
    int       count;
    int       capacity;
} RedirArray;

static void ra_init(RedirArray *ra) { ra->capacity = 0; ra->count = 0; ra->items = NULL; }
static void ra_free(RedirArray *ra)
{
    for (int i = 0; i < ra->count; i++) free(ra->items[i].filename);
    free(ra->items);
}
static void ra_push(RedirArray *ra, RedirType type, const char *filename)
{
    if (ra->count >= ra->capacity) {
        ra->capacity = ra->capacity ? ra->capacity * 2 : 4;
        ra->items = realloc(ra->items, sizeof(Redirect) * (size_t)ra->capacity);
    }
    ra->items[ra->count].type     = type;
    ra->items[ra->count].filename = shell_strdup(filename);
    ra->count++;
}

/* AtomicCmd list (owned by a CmdGroup) */
typedef struct {
    AtomicCmd *items;
    int        count;
    int        capacity;
} AtomicArray;

static void aa_init(AtomicArray *aa) { aa->capacity = 0; aa->count = 0; aa->items = NULL; }

static void free_atomic_full(AtomicCmd *a)
{
    free(a->name);
    if (a->args) {
        for (int i = 0; i < a->arg_count; i++) free(a->args[i]);
        free(a->args);
    }
    if (a->redirects) {
        for (int i = 0; i < a->redirect_count; i++) free(a->redirects[i].filename);
        free(a->redirects);
    }
}

static void aa_free(AtomicArray *aa)
{
    for (int i = 0; i < aa->count; i++)
        free_atomic_full(&aa->items[i]);
    free(aa->items);
}

static AtomicCmd *aa_push(AtomicArray *aa)
{
    if (aa->count >= aa->capacity) {
        aa->capacity = aa->capacity ? aa->capacity * 2 : 4;
        aa->items = realloc(aa->items, sizeof(AtomicCmd) * (size_t)aa->capacity);
    }
    AtomicCmd *a = &aa->items[aa->count++];
    memset(a, 0, sizeof(*a));
    return a;
}

/* CmdGroup list (owned by a ShellCmd) */
typedef struct {
    CmdGroup *items;
    int       count;
    int       capacity;
} GroupArray;

static void ga_init(GroupArray *ga) { ga->capacity = 0; ga->count = 0; ga->items = NULL; }

static void free_group_full(CmdGroup *g)
{
    if (g->commands) {
        for (int i = 0; i < g->command_count; i++)
            free_atomic_full(&g->commands[i]);
        free(g->commands);
    }
}

static void ga_free(GroupArray *ga)
{
    for (int i = 0; i < ga->count; i++)
        free_group_full(&ga->items[i]);
    free(ga->items);
}

static CmdGroup *ga_push(GroupArray *ga)
{
    if (ga->count >= ga->capacity) {
        ga->capacity = ga->capacity ? ga->capacity * 2 : 8;
        ga->items = realloc(ga->items, sizeof(CmdGroup) * (size_t)ga->capacity);
    }
    CmdGroup *g = &ga->items[ga->count++];
    g->commands      = NULL;
    g->command_count  = 0;
    return g;
}

/* ================================================================== */
/*  Forward declarations                                               */
/* ================================================================== */
static int parse_atomic(Parser *p, AtomicCmd *a);
static int parse_cmd_group(Parser *p, CmdGroup *g);

/* ================================================================== */
/*  parse_atomic                                                       */
/*  atomic -> WORD ( WORD | input | output )*                          */
/*  Returns 1 on success, 0 on syntax error.                           */
/* ================================================================== */
static int parse_atomic(Parser *p, AtomicCmd *a)
{
    StrArray  args;
    RedirArray redirs;
    sa_init(&args);
    ra_init(&redirs);

    /* First WORD is the command name (mandatory) */
    if (cur_type(p) != TOK_WORD)
        goto fail;

    sa_push(&args, cur(p)->value);
    advance(p);

    /* Parse subsequent words and redirects */
    while (1) {
        int t = cur_type(p);
        if (t == TOK_WORD) {
            sa_push(&args, cur(p)->value);
            advance(p);
        } else if (t == TOK_INPUT) {
            advance(p);
            if (cur_type(p) != TOK_WORD) goto fail;
            ra_push(&redirs, REDIR_INPUT, cur(p)->value);
            advance(p);
        } else if (t == TOK_OUTPUT || t == TOK_OUTPUT_APPEND) {
            RedirType rt = (t == TOK_OUTPUT_APPEND) ? REDIR_OUTPUT_APPEND : REDIR_OUTPUT;
            advance(p);
            if (cur_type(p) != TOK_WORD) goto fail;
            ra_push(&redirs, rt, cur(p)->value);
            advance(p);
        } else {
            break;
        }
    }

    /* Build the AtomicCmd — ensure args are NULL-terminated for execvp */
    if (args.count >= args.capacity) {
        args.capacity = args.capacity ? args.capacity * 2 : 8;
        args.items = realloc(args.items, sizeof(char *) * (size_t)args.capacity);
    }
    args.items[args.count] = NULL;
    /* args.count remains the true arg count (excluding sentinel) */

    a->name = (args.count > 0) ? shell_strdup(args.items[0]) : NULL;
    a->arg_count = args.count;
    a->args = args.items; /* transfer ownership */

    a->redirect_count = redirs.count;
    a->redirects = redirs.items;

    return 1;

fail:
    sa_free(&args);
    ra_free(&redirs);
    return 0;
}

/* ================================================================== */
/*  parse_cmd_group                                                    */
/*  cmd_group -> atomic ( PIPE atomic )*                               */
/*  Returns 1 on success, 0 on syntax error.                           */
/* ================================================================== */
static int parse_cmd_group(Parser *p, CmdGroup *g)
{
    AtomicArray atomics;
    aa_init(&atomics);

    /* First atomic */
    AtomicCmd *a = aa_push(&atomics);
    if (!parse_atomic(p, a)) goto fail;

    /* Piped atomics */
    while (cur_type(p) == TOK_PIPE) {
        advance(p);
        AtomicCmd *next = aa_push(&atomics);
        if (!parse_atomic(p, next)) goto fail;
    }

    g->commands      = atomics.items;
    g->command_count  = atomics.count;
    return 1;

fail:
    aa_free(&atomics);
    return 0;
}

/* ================================================================== */
/*  parser_free_cmd                                                    */
/* ================================================================== */
void parser_free_cmd(ShellCmd *cmd)
{
    if (!cmd) return;
    for (int i = 0; i < cmd->group_count; i++)
        free_group_full(&cmd->groups[i]);
    free(cmd->groups);
    free(cmd->separators);
    free(cmd);
}

/* ================================================================== */
/*  parser_parse  --  Main entry point                                 */
/*  shell_cmd -> cmd_group ( (&|;) cmd_group )*  &?                    */
/* ================================================================== */
ShellCmd *parser_parse(Token *tokens, int token_count, int *out_valid)
{
    Parser p = { .tokens = tokens, .count = token_count, .pos = 0 };

    /* Empty input is valid (do nothing) */
    if (cur_type(&p) == TOK_EOF) {
        *out_valid = 1;
        return NULL;
    }

    GroupArray groups;
    ga_init(&groups);

    int *separators = NULL;
    int  sep_count  = 0;
    int  sep_cap    = 0;

    /* Parse first cmd_group */
    CmdGroup *g = ga_push(&groups);
    if (!parse_cmd_group(&p, g)) goto fail;

    /* Parse remaining: (&|;) cmd_group                                */
    /* When we see &, we must decide if it is a separator (followed by  */
    /* a cmd_group, i.e. a WORD) or a trailing & (followed by EOF).     */
    while (cur_type(&p) == TOK_SEMICOLON ||
           (cur_type(&p) == TOK_AND &&
            p.pos + 1 < p.count && p.tokens[p.pos + 1].type == TOK_WORD))
    {
        int sep = (cur_type(&p) == TOK_AND) ? 1 : 0;
        advance(&p);

        if (sep_count >= sep_cap) {
            sep_cap = sep_cap ? sep_cap * 2 : 8;
            separators = realloc(separators, sizeof(int) * (size_t)sep_cap);
        }
        separators[sep_count++] = sep;

        CmdGroup *next = ga_push(&groups);
        if (!parse_cmd_group(&p, next)) goto fail;
    }

    /* Trailing & */
    int trailing_amp = 0;
    if (cur_type(&p) == TOK_AND) {
        trailing_amp = 1;
        advance(&p);
    }

    /* Must be at EOF */
    if (cur_type(&p) != TOK_EOF)
        goto fail;

    /* Build ShellCmd */
    ShellCmd *cmd = malloc(sizeof(ShellCmd));
    cmd->groups       = groups.items;
    cmd->group_count   = groups.count;
    cmd->separators    = separators;
    cmd->trailing_amp  = trailing_amp;
    *out_valid = 1;
    return cmd;

fail:
    *out_valid = 0;
    ga_free(&groups);
    free(separators);
    return NULL;
}
