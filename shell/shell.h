#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>

/* ──────────────────────────────────────────────────────────
 *  Constants
 * ────────────────────────────────────────────────────────── */
#define SHELL_BUF_SIZE  4096
#define SHELL_MAX_ARGS  256
#define SHELL_MAX_CMDS  64
#define SHELL_MAX_REDIR 16

/* ──────────────────────────────────────────────────────────
 *  Token Types  (used by the lexer and parser)
 * ────────────────────────────────────────────────────────── */
typedef enum {
    TOK_WORD,           /* generic word / name                         */
    TOK_PIPE,           /* |                                          */
    TOK_AND,            /* &                                          */
    TOK_SEMICOLON,      /* ;                                          */
    TOK_INPUT,          /* <                                          */
    TOK_OUTPUT,         /* >                                          */
    TOK_OUTPUT_APPEND,  /* >>                                         */
    TOK_EOF             /* end-of-input                               */
} TokenType;

typedef struct {
    TokenType type;
    char      *value;   /* heap-allocated copy of the token text      */
} Token;

/* ──────────────────────────────────────────────────────────
 *  Redirect descriptor  (attached to an atomic command)
 * ────────────────────────────────────────────────────────── */
typedef enum {
    REDIR_INPUT,
    REDIR_OUTPUT,
    REDIR_OUTPUT_APPEND
} RedirType;

typedef struct {
    char     *filename;
    RedirType type;
} Redirect;

/* ──────────────────────────────────────────────────────────
 *  Atomic command  (single command in a pipeline)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char      *name;            /* command name (first word)           */
    char     **args;            /* argument words (including name[0])  */
    int        arg_count;
    Redirect  *redirects;
    int        redirect_count;
} AtomicCmd;

/* ──────────────────────────────────────────────────────────
 *  Command group  (a pipeline of atomic commands)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    AtomicCmd *commands;
    int        command_count;
} CmdGroup;

/* ──────────────────────────────────────────────────────────
 *  Shell command  (full parsed representation)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    CmdGroup *groups;
    int        group_count;
    int       *separators;     /* 0 = ';', 1 = '&' (length = group_count - 1) */
    int        trailing_amp;   /* 1 if input ended with trailing &    */
} ShellCmd;

/* ──────────────────────────────────────────────────────────
 *  Global shell state
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char home_dir[PATH_MAX];   /* directory where the shell was started */
} ShellState;

extern ShellState g_shell;

/* ──────────────────────────────────────────────────────────
 *  Utility helpers  (utils.c)
 * ────────────────────────────────────────────────────────── */
char *shell_strdup(const char *s);

/* ──────────────────────────────────────────────────────────
 *  Prompt  (prompt.c)
 * ────────────────────────────────────────────────────────── */
void prompt_print(void);

/* ──────────────────────────────────────────────────────────
 *  Input  (input.c)
 * ────────────────────────────────────────────────────────── */
char *input_read_line(void);

/* ──────────────────────────────────────────────────────────
 *  Lexer  (lexer.c)
 * ────────────────────────────────────────────────────────── */
Token *lexer_tokenize(const char *input, int *out_count);
void   lexer_free_tokens(Token *tokens, int count);

/* ──────────────────────────────────────────────────────────
 *  Parser  (parser.c)
 * ────────────────────────────────────────────────────────── */
ShellCmd *parser_parse(Token *tokens, int token_count, int *out_valid);
void      parser_free_cmd(ShellCmd *cmd);

#endif /* SHELL_H */
