/*
 * test_wrapper.c  --  Standalone test harness for the shell modules.
 *
 * Provides an easy way to test each module individually without
 * running the full interactive REPL.
 *
 * Build:  make test
 * Run:    ./test.out
 *
 * Usage:
 *   1. Test the lexer:        ./test.out lexer  "cat foo | wc"
 *   2. Test the parser:       ./test.out parser "cat foo | wc"
 *   3. Test syntax validation: ./test.out valid  "cat | ; wc"
 *   4. Run interactive mode:  ./test.out interactive
 *   5. Run all built-in tests: ./test.out selftest
 */
#include "shell.h"
#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include <getopt.h>

/* Global state required by prompt module */
ShellState g_shell;

/* ------------------------------------------------------------------ */
/*  Self-test infrastructure                                           */
/* ------------------------------------------------------------------ */
static int tests_run    = 0;
static int tests_passed = 0;

#define ASSERT_EQ_STR(name, expected, actual) do {          \
    tests_run++;                                            \
    if (strcmp((expected), (actual)) == 0) {                 \
        printf("  [PASS] %s\n", (name));                    \
        tests_passed++;                                     \
    } else {                                                \
        printf("  [FAIL] %s: expected \"%s\", got \"%s\"\n",\
               (name), (expected), (actual));               \
    }                                                       \
} while(0)

#define ASSERT_EQ_INT(name, expected, actual) do {          \
    tests_run++;                                            \
    if ((expected) == (actual)) {                           \
        printf("  [PASS] %s\n", (name));                    \
        tests_passed++;                                     \
    } else {                                                \
        printf("  [FAIL] %s: expected %d, got %d\n",       \
               (name), (expected), (actual));               \
    }                                                       \
} while(0)

#define ASSERT_TRUE(name, expr) do {                        \
    tests_run++;                                            \
    if ((expr)) {                                           \
        printf("  [PASS] %s\n", (name));                    \
        tests_passed++;                                     \
    } else {                                                \
        printf("  [FAIL] %s: assertion failed\n", (name)); \
    }                                                       \
} while(0)

#define ASSERT_FALSE(name, expr) do {                       \
    tests_run++;                                            \
    if (!(expr)) {                                          \
        printf("  [PASS] %s\n", (name));                    \
        tests_passed++;                                     \
    } else {                                                \
        printf("  [FAIL] %s: expected false\n", (name));   \
    }                                                       \
} while(0)

/* Helper: test validity of a string */
static int is_valid_syntax(const char *input)
{
    int tc = 0;
    Token *tokens = lexer_tokenize(input, &tc);
    int valid = 0;
    ShellCmd *cmd = parser_parse(tokens, tc, &valid);
    parser_free_cmd(cmd);
    lexer_free_tokens(tokens, tc);
    return valid;
}

/* ------------------------------------------------------------------ */
/*  Lexer tests                                                        */
/* ------------------------------------------------------------------ */
static void test_lexer(void)
{
    printf("\n=== LEXER TESTS ===\n");

    /* Test: simple command */
    {
        int n = 0;
        Token *t = lexer_tokenize("ls -la", &n);
        ASSERT_EQ_INT("ls -la: token count (incl EOF)", 3, n);
        ASSERT_EQ_INT("ls -la: tok[0] is WORD", TOK_WORD, t[0].type);
        ASSERT_EQ_STR("ls -la: tok[0] value", "ls", t[0].value);
        ASSERT_EQ_INT("ls -la: tok[1] is WORD", TOK_WORD, t[1].type);
        ASSERT_EQ_STR("ls -la: tok[1] value", "-la", t[1].value);
        ASSERT_EQ_INT("ls -la: tok[2] is EOF", TOK_EOF, t[2].type);
        lexer_free_tokens(t, n);
    }

    /* Test: pipe */
    {
        int n = 0;
        Token *t = lexer_tokenize("cat | wc", &n);
        ASSERT_EQ_INT("pipe: token count", 4, n);
        ASSERT_EQ_INT("pipe: tok[1] is PIPE", TOK_PIPE, t[1].type);
        lexer_free_tokens(t, n);
    }

    /* Test: semicolon */
    {
        int n = 0;
        Token *t = lexer_tokenize("ls ; pwd", &n);
        ASSERT_EQ_INT("semicolon: tok[1] is SEMICOLON", TOK_SEMICOLON, t[1].type);
        lexer_free_tokens(t, n);
    }

    /* Test: ampersand */
    {
        int n = 0;
        Token *t = lexer_tokenize("ls &", &n);
        ASSERT_EQ_INT("ampersand: tok[1] is AND", TOK_AND, t[1].type);
        lexer_free_tokens(t, n);
    }

    /* Test: output redirect > */
    {
        int n = 0;
        Token *t = lexer_tokenize("echo hi > out.txt", &n);
        ASSERT_EQ_INT("output >: tok[2] is OUTPUT", TOK_OUTPUT, t[2].type);
        lexer_free_tokens(t, n);
    }

    /* Test: append redirect >> */
    {
        int n = 0;
        Token *t = lexer_tokenize("echo hi >> out.txt", &n);
        ASSERT_EQ_INT("output >>: tok[2] is APPEND", TOK_OUTPUT_APPEND, t[2].type);
        lexer_free_tokens(t, n);
    }

    /* Test: input redirect < */
    {
        int n = 0;
        Token *t = lexer_tokenize("wc < in.txt", &n);
        ASSERT_EQ_INT("input <: tok[1] is INPUT", TOK_INPUT, t[1].type);
        lexer_free_tokens(t, n);
    }

    /* Test: empty input */
    {
        int n = 0;
        Token *t = lexer_tokenize("", &n);
        ASSERT_EQ_INT("empty: just EOF", 1, n);
        ASSERT_EQ_INT("empty: tok[0] is EOF", TOK_EOF, t[0].type);
        lexer_free_tokens(t, n);
    }

    /* Test: multiple spaces / tabs */
    {
        int n = 0;
        Token *t = lexer_tokenize("  ls   -la  ", &n);
        ASSERT_EQ_INT("whitespace: token count", 3, n);
        ASSERT_EQ_STR("whitespace: tok[0]", "ls", t[0].value);
        lexer_free_tokens(t, n);
    }
}

/* ------------------------------------------------------------------ */
/*  Parser / syntax validation tests                                   */
/* ------------------------------------------------------------------ */
static void test_parser(void)
{
    printf("\n=== PARSER TESTS ===\n");

    /* Valid commands */
    ASSERT_TRUE("valid: Hi there guys!",       is_valid_syntax("Hi there guys!"));
    ASSERT_TRUE("valid: cat|meow;meow>",       is_valid_syntax("cat meow.txt | meow; meow > meow.txt &"));
    ASSERT_TRUE("valid: ls -la",               is_valid_syntax("ls -la"));
    ASSERT_TRUE("valid: cat|grep|wc",          is_valid_syntax("cat foo | grep bar | wc -l"));
    ASSERT_TRUE("valid: echo > out.txt",       is_valid_syntax("echo hello > out.txt"));
    ASSERT_TRUE("valid: echo >> out.txt",      is_valid_syntax("echo hello >> out.txt"));
    ASSERT_TRUE("valid: wc < in.txt",          is_valid_syntax("wc < input.txt"));
    ASSERT_TRUE("valid: echo > out < in",      is_valid_syntax("echo hello > out.txt < input.txt"));
    ASSERT_TRUE("valid: ls;pwd;echo",          is_valid_syntax("ls ; pwd ; echo done"));
    ASSERT_TRUE("valid: ls&pwd&echo",          is_valid_syntax("ls & pwd & echo done"));
    ASSERT_TRUE("valid: ls&pwd;echo&",         is_valid_syntax("ls & pwd ; echo done &"));
    ASSERT_TRUE("valid: ls|wc&",               is_valid_syntax("ls | wc &"));
    ASSERT_TRUE("valid: cat",                  is_valid_syntax("cat"));
    ASSERT_TRUE("valid: complex pipeline",     is_valid_syntax("cmd1 | cmd2 | cmd3 ; cmd4 | cmd5 &"));

    /* Invalid commands */
    ASSERT_FALSE("invalid: cat|;wc",           is_valid_syntax("cat meow.txt | ; meow"));
    ASSERT_FALSE("invalid: | cmd",             is_valid_syntax("| cmd"));
    ASSERT_FALSE("invalid: cmd |",             is_valid_syntax("cmd |"));
    ASSERT_FALSE("invalid: ; cmd",             is_valid_syntax("; cmd"));
    ASSERT_FALSE("invalid: cmd ;",             is_valid_syntax("cmd ;"));
    ASSERT_FALSE("invalid: cmd & ; cmd2",      is_valid_syntax("cmd & ; cmd2"));
    ASSERT_FALSE("invalid: < input.txt",       is_valid_syntax("< input.txt"));
    ASSERT_FALSE("invalid: > output.txt",      is_valid_syntax("> output.txt"));
    ASSERT_FALSE("invalid: cmd > ",            is_valid_syntax("cmd > "));
    ASSERT_FALSE("invalid: cmd < ",            is_valid_syntax("cmd < "));
    ASSERT_FALSE("invalid: cmd >> ",           is_valid_syntax("cmd >> "));
    ASSERT_FALSE("invalid: cmd||cmd",          is_valid_syntax("cmd | | cmd2"));
    ASSERT_FALSE("invalid: cmd&&cmd",          is_valid_syntax("cmd & & cmd2"));
    ASSERT_FALSE("invalid: cmd;;cmd",          is_valid_syntax("cmd ; ; cmd2"));

    /* Empty input is valid */
    ASSERT_TRUE("valid: empty input",          is_valid_syntax(""));
    {
        int tc = 0;
        Token *tokens = lexer_tokenize("", &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(tokens, tc, &valid);
        ASSERT_TRUE("valid: empty returns NULL cmd", cmd == NULL);
        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tc);
    }
}

/* ------------------------------------------------------------------ */
/*  Prompt test                                                        */
/* ------------------------------------------------------------------ */
static void test_prompt(void)
{
    printf("\n=== PROMPT TEST ===\n");
    printf("Printing prompt (should show <user@host:path>):\n");
    prompt_print();
    printf("\n");
    tests_run++;
    tests_passed++;
    printf("  [PASS] prompt_print() ran without crash\n");
}

static void shell_init(void)
{
    if (getcwd(g_shell.home_dir, sizeof(g_shell.home_dir)) == NULL)
        strcpy(g_shell.home_dir, ".");
}

/* ------------------------------------------------------------------ */
/*  CLI modes                                                          */
/* ------------------------------------------------------------------ */
static void mode_interactive(void)
{
    shell_init();
    printf("Interactive mode (Ctrl-D to exit)\n");
    while (1) {
        prompt_print();
        char *line = input_read_line();
        if (!line) { printf("\n"); break; }
        if (line[0] == '\0') { free(line); continue; }

        int tc = 0;
        Token *tokens = lexer_tokenize(line, &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(tokens, tc, &valid);
        if (!valid)
            printf("Invalid Syntax!\n");

        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tc);
        free(line);
    }
}

static void mode_lexer(const char *input)
{
    printf("Tokens for: \"%s\"\n", input);
    int n = 0;
    Token *tokens = lexer_tokenize(input, &n);
    const char *type_names[] = {
        "WORD", "PIPE", "AND", "SEMICOLON",
        "INPUT", "OUTPUT", "OUTPUT_APPEND", "EOF"
    };
    for (int i = 0; i < n; i++) {
        printf("  [%2d] %-15s  \"%s\"\n", i,
               type_names[tokens[i].type], tokens[i].value);
    }
    lexer_free_tokens(tokens, n);
}

static void mode_parser(const char *input)
{
    printf("Parsing: \"%s\"\n", input);
    int tc = 0;
    Token *tokens = lexer_tokenize(input, &tc);
    int valid = 0;
    ShellCmd *cmd = parser_parse(tokens, tc, &valid);

    if (!valid) {
        printf("  Invalid Syntax!\n");
    } else if (cmd) {
        printf("  Valid. Parsed structure:\n");
        printf("  group_count: %d\n", cmd->group_count);
        printf("  trailing_amp: %d\n", cmd->trailing_amp);
        for (int i = 0; i < cmd->group_count; i++) {
            CmdGroup *g = &cmd->groups[i];
            printf("  Group %d: %d atomic cmd(s)\n", i, g->command_count);
            for (int j = 0; j < g->command_count; j++) {
                AtomicCmd *a = &g->commands[j];
                printf("    Atomic %d: name=%s, args=%d, redirects=%d\n",
                       j, a->name, a->arg_count, a->redirect_count);
                for (int k = 0; k < a->arg_count; k++)
                    printf("      arg[%d] = \"%s\"\n", k, a->args[k]);
                for (int k = 0; k < a->redirect_count; k++) {
                    const char *op = a->redirects[k].type == REDIR_INPUT ? "<" :
                                     a->redirects[k].type == REDIR_OUTPUT ? ">" : ">>";
                    printf("      redir: %s \"%s\"\n", op, a->redirects[k].filename);
                }
            }
            if (i < cmd->group_count - 1)
                printf("    separator: %s\n", cmd->separators[i] ? "&" : ";");
        }
    } else {
        printf("  Valid. (empty command)\n");
    }

    parser_free_cmd(cmd);
    lexer_free_tokens(tokens, tc);
}

static void mode_valid(const char *input)
{
    printf("%s -> %s\n", input, is_valid_syntax(input) ? "VALID" : "INVALID");
}

/* ------------------------------------------------------------------ */
/*  Usage                                                              */
/* ------------------------------------------------------------------ */
static void usage(void)
{
    printf(
        "Usage: test.out <command> [args]\n"
        "\n"
        "Commands:\n"
        "  lexer  <input>     Tokenize input and print tokens\n"
        "  parser <input>     Parse input and print structure\n"
        "  valid  <input>     Check if input is syntactically valid\n"
        "  interactive        Run the shell in interactive mode\n"
        "  selftest           Run all built-in unit tests\n"
        "  prompt             Test the prompt module\n"
        "\n"
        "Examples:\n"
        "  ./test.out lexer  \"cat foo | wc\"\n"
        "  ./test.out parser \"ls -la > out.txt\"\n"
        "  ./test.out valid  \"cat | ; wc\"\n"
        "  ./test.out selftest\n"
    );
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    /* Init shell state for prompt */
    if (getcwd(g_shell.home_dir, sizeof(g_shell.home_dir)) == NULL)
        strcpy(g_shell.home_dir, ".");

    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "lexer") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: test.out lexer <input>\n"); return 1; }
        mode_lexer(argv[2]);
    } else if (strcmp(cmd, "parser") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: test.out parser <input>\n"); return 1; }
        mode_parser(argv[2]);
    } else if (strcmp(cmd, "valid") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: test.out valid <input>\n"); return 1; }
        mode_valid(argv[2]);
    } else if (strcmp(cmd, "interactive") == 0) {
        mode_interactive();
    } else if (strcmp(cmd, "selftest") == 0) {
        test_lexer();
        test_parser();
        test_prompt();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
        return (tests_passed == tests_run) ? 0 : 1;
    } else if (strcmp(cmd, "prompt") == 0) {
        test_prompt();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        return 1;
    }

    return 0;
}
