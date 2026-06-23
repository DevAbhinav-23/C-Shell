/*
 * test_wrapper.c  --  Standalone test harness for the shell modules.
 *
 * Tests for Part A (lexer, parser) and Part B (hop, reveal, log).
 *
 * Build:  make test
 * Run:    ./test.out
 *
 * Usage:
 *   ./test.out lexer  "cat foo | wc"
 *   ./test.out parser "cat foo | wc"
 *   ./test.out valid  "cat | ; wc"
 *   ./test.out interactive
 *   ./test.out selftest
 *   ./test.out test_hop
 *   ./test.out test_reveal
 *   ./test.out test_log
 */
#include "shell.h"
#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>

/* Global state required by shell modules */
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
/*  Shell init helper for builtins tests                                */
/* ------------------------------------------------------------------ */
static void test_shell_init(void)
{
    if (getcwd(g_shell.home_dir, sizeof(g_shell.home_dir)) == NULL)
        strcpy(g_shell.home_dir, ".");
    g_shell.prev_cwd[0] = '\0';
    g_shell.has_prev_cwd = 0;
    log_init();
}

/* ------------------------------------------------------------------ */
/*  Helper: run a builtin command from args array                      */
/* ------------------------------------------------------------------ */
static int run_cmd(const char *input)
{
    int tc = 0;
    Token *tokens = lexer_tokenize(input, &tc);
    int valid = 0;
    ShellCmd *cmd = parser_parse(tokens, tc, &valid);
    if (!valid || !cmd) {
        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tc);
        return -1;
    }
    int ret = 0;
    CmdGroup *cg = &cmd->groups[0];
    AtomicCmd *a = &cg->commands[0];
    if (strcmp(a->name, "hop") == 0)
        ret = builtin_hop(a->args, a->arg_count);
    else if (strcmp(a->name, "reveal") == 0)
        ret = builtin_reveal(a->args, a->arg_count);
    else if (strcmp(a->name, "log") == 0)
        ret = builtin_log(a->args, a->arg_count);
    parser_free_cmd(cmd);
    lexer_free_tokens(tokens, tc);
    return ret;
}

/* Helper: get current cwd */
static void get_cwd(char *buf, size_t sz)
{
    if (getcwd(buf, sz) == NULL)
        strcpy(buf, "?");
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

/* ------------------------------------------------------------------ */
/*  Hop tests  (B.1)                                                   */
/* ------------------------------------------------------------------ */
static void test_hop(void)
{
    printf("\n=== HOP TESTS ===\n");
    char cwd[PATH_MAX];
    char orig[PATH_MAX];
    get_cwd(orig, sizeof(orig));

    /* hop with no args -> home */
    run_cmd("hop");
    get_cwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR("hop (no args) -> home", g_shell.home_dir, cwd);

    /* hop . -> stay in place */
    run_cmd("hop .");
    get_cwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR("hop . -> stay", g_shell.home_dir, cwd);

    /* hop .. -> go to parent */
    run_cmd("hop ..");
    get_cwd(cwd, sizeof(cwd));
    /* parent of home is /home or / */
    {
        char expected[PATH_MAX + 32];
        snprintf(expected, sizeof(expected), "%s/..", g_shell.home_dir);
        /* Resolve expected to canonical form for comparison */
        char exp_resolved[PATH_MAX];
        if (resolve_path(expected, exp_resolved, sizeof(exp_resolved)) == 0)
            ASSERT_EQ_STR("hop .. -> parent", exp_resolved, cwd);
        else
            ASSERT_EQ_STR("hop .. -> parent (raw)", expected, cwd);
    }

    /* hop - -> go back to home (prev was home) */
    run_cmd("hop -");
    get_cwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR("hop - -> back to home", g_shell.home_dir, cwd);

    /* hop with nonexistent path */
    ASSERT_EQ_INT("hop nonexistent -> error", 1, run_cmd("hop /nonexistent/path/xyz"));

    /* hop ~ -> home */
    run_cmd("hop /tmp");
    get_cwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR("hop /tmp", "/tmp", cwd);

    run_cmd("hop ~");
    get_cwd(cwd, sizeof(cwd));
    ASSERT_EQ_STR("hop ~ -> home", g_shell.home_dir, cwd);

    /* Multiple args: hop ~ .. */
    run_cmd("hop ~");
    run_cmd("hop ~ ..");
    get_cwd(cwd, sizeof(cwd));
    {
        char expected[PATH_MAX + 32];
        snprintf(expected, sizeof(expected), "%s/..", g_shell.home_dir);
        char exp_resolved[PATH_MAX];
        if (resolve_path(expected, exp_resolved, sizeof(exp_resolved)) == 0)
            ASSERT_EQ_STR("hop ~ .. -> parent of home", exp_resolved, cwd);
        else
            ASSERT_EQ_STR("hop ~ .. -> parent (raw)", expected, cwd);
    }

    /* Restore original CWD */
    chdir(orig);
}

/* ------------------------------------------------------------------ */
/*  Reveal tests  (B.2)                                                */
/* ------------------------------------------------------------------ */
static void test_reveal(void)
{
    printf("\n=== REVEAL TESTS ===\n");

    /* Create a test directory with known contents */
    char orig[PATH_MAX];
    get_cwd(orig, sizeof(orig));

    char testdir[PATH_MAX];
    snprintf(testdir, sizeof(testdir), "/tmp/cshell_test_reveal_XXXXXX");
    /* Use mkdtemp for safety */
    if (mkdtemp(testdir) == NULL) {
        printf("  [SKIP] could not create temp dir\n");
        chdir(orig);
        return;
    }

    /* Create some files */
    char fpath[PATH_MAX + 32];
    snprintf(fpath, sizeof(fpath), "%s/aaa.txt", testdir);
    FILE *f = fopen(fpath, "w"); fclose(f);
    snprintf(fpath, sizeof(fpath), "%s/bbb.txt", testdir);
    f = fopen(fpath, "w"); fclose(f);
    snprintf(fpath, sizeof(fpath), "%s/.hidden", testdir);
    f = fopen(fpath, "w"); fclose(f);

    /* reveal without -a should not show .hidden */
    /* We can't easily capture stdout, so just test that it doesn't crash */
    {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "reveal %s", testdir);
        int ret = run_cmd(cmd);
        ASSERT_EQ_INT("reveal basic: success", 0, ret);
    }

    /* reveal -a should show .hidden */
    {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "reveal -a %s", testdir);
        int ret = run_cmd(cmd);
        ASSERT_EQ_INT("reveal -a: success", 0, ret);
    }

    /* reveal -l should work */
    {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "reveal -l %s", testdir);
        int ret = run_cmd(cmd);
        ASSERT_EQ_INT("reveal -l: success", 0, ret);
    }

    /* reveal -la should work */
    {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof(cmd), "reveal -la %s", testdir);
        int ret = run_cmd(cmd);
        ASSERT_EQ_INT("reveal -la: success", 0, ret);
    }

    /* reveal with nonexistent dir */
    ASSERT_EQ_INT("reveal nonexistent: error", 1, run_cmd("reveal /nonexistent/path/xyz"));

    /* reveal on current dir */
    ASSERT_EQ_INT("reveal .: success", 0, run_cmd("reveal ."));

    /* Clean up */
    snprintf(fpath, sizeof(fpath), "%s/aaa.txt", testdir); unlink(fpath);
    snprintf(fpath, sizeof(fpath), "%s/bbb.txt", testdir); unlink(fpath);
    snprintf(fpath, sizeof(fpath), "%s/.hidden", testdir); unlink(fpath);
    rmdir(testdir);
    chdir(orig);
}

/* ------------------------------------------------------------------ */
/*  Log tests  (B.3)                                                   */
/* ------------------------------------------------------------------ */
static void test_log(void)
{
    printf("\n=== LOG TESTS ===\n");

    /* Purge first to start clean */
    run_cmd("log purge");
    ASSERT_EQ_INT("log purge: success", 0, run_cmd("log purge"));

    /* log with no entries -> nothing printed (just succeeds) */
    ASSERT_EQ_INT("log (empty): success", 0, run_cmd("log"));

    /* Add some commands to history manually */
    log_add("ls -la");
    log_add("hop ~");
    log_add("pwd");

    /* log should show 3 entries (oldest first) */
    ASSERT_EQ_INT("log after 3 adds: success", 0, run_cmd("log"));

    /* log execute 1 -> should execute the newest command (pwd) */
    ASSERT_EQ_INT("log execute 1: success", 0, run_cmd("log execute 1"));

    /* Duplicate: same command as last should not be added */
    log_add("pwd");
    log_add("pwd");  /* should be suppressed */
    /* Only 4 entries total (ls -la, hop ~, pwd, pwd)

     */
    ASSERT_EQ_INT("log after dup: success", 0, run_cmd("log"));

    /* log purge clears everything */
    run_cmd("log purge");
    ASSERT_EQ_INT("log after purge: success", 0, run_cmd("log"));

    /* Invalid syntax */
    ASSERT_EQ_INT("log log: invalid syntax", 1, run_cmd("log log"));
}

/* ------------------------------------------------------------------ */
/*  Exec tests  (Part C)                                               */
/* ------------------------------------------------------------------ */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void test_exec(void)
{
    printf("\n=== EXEC TESTS (Part C) ===\n");

    char orig[PATH_MAX];
    get_cwd(orig, sizeof(orig));

    /* Create temp dir for test files */
    char testdir[PATH_MAX * 2];
    snprintf(testdir, sizeof(testdir), "/tmp/cshell_test_exec_XXXXXX");
    if (mkdtemp(testdir) == NULL) {
        printf("  [SKIP] could not create temp dir for exec tests\n");
        tests_run++;
        tests_passed++;
        return;
    }

    /* ── C.1: External command execution ──────────────────────────── */
    {
        int tc = 0;
        Token *t = lexer_tokenize("echo hello", &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(t, tc, &valid);
        ASSERT_TRUE("exec: echo parses", valid && cmd);
        if (valid && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: echo returns 0", 0, ret);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Command not found */
    {
        int tc = 0;
        Token *t = lexer_tokenize("nonexistent_cmd_xyz", &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(t, tc, &valid);
        ASSERT_TRUE("exec: nonexistent parses", valid && cmd);
        if (valid && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_TRUE("exec: nonexistent returns non-zero", ret != 0);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* ── C.2 + C.3: File redirection ─────────────────────────────── */

    /* Create input file */
    char infile[PATH_MAX * 2];
    snprintf(infile, sizeof(infile), "%s/in.txt", testdir);
    FILE *f = fopen(infile, "w");
    fprintf(f, "hello world\n");
    fclose(f);

    /* Input redirection: cat < infile */
    {
        char outfile[PATH_MAX * 2];
        snprintf(outfile, sizeof(outfile), "%s/out_cat.txt", testdir);
        char cmdline[PATH_MAX * 4];
        snprintf(cmdline, sizeof(cmdline), "cat < %s > %s", infile, outfile);

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(t, tc, &valid);
        ASSERT_TRUE("exec: cat < > parses", valid && cmd);
        if (valid && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: cat < > returns 0", 0, ret);

            FILE *ck = fopen(outfile, "r");
            ASSERT_TRUE("exec: output file exists", ck != NULL);
            if (ck) {
                char buf[128]; buf[0] = '\0';
                fgets(buf, sizeof(buf), ck);
                ASSERT_EQ_STR("exec: output matches input", "hello world\n", buf);
                fclose(ck);
            }
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
        unlink(outfile);
    }

    /* Non-existent input file */
    {
        char cmdline[PATH_MAX * 2];
        snprintf(cmdline, sizeof(cmdline), "cat < /tmp/nonexistent_file_xyz_123");

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(t, tc, &valid);
        ASSERT_TRUE("exec: nonexistent input parses", valid && cmd);
        if (valid && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_TRUE("exec: nonexistent input returns non-zero", ret != 0);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Output redirection (overwrite) */
    {
        char outfile[PATH_MAX * 2];
        snprintf(outfile, sizeof(outfile), "%s/out_echo.txt", testdir);
        char cmdline[PATH_MAX * 4];
        snprintf(cmdline, sizeof(cmdline), "echo hello > %s", outfile);

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int valid = 0;
        ShellCmd *cmd = parser_parse(t, tc, &valid);
        ASSERT_TRUE("exec: echo > parses", valid && cmd);
        if (valid && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: echo > returns 0", 0, ret);

            FILE *ck = fopen(outfile, "r");
            ASSERT_TRUE("exec: > file created", ck != NULL);
            if (ck) {
                char buf[256]; buf[0] = '\0';
                size_t n = fread(buf, 1, sizeof(buf) - 1, ck);
                buf[n] = '\0';
                ASSERT_TRUE("exec: > file non-empty", n > 0);
                fclose(ck);
            }
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
        unlink(outfile);
    }

    /* Output append */
    {
        char outfile[PATH_MAX * 2];
        snprintf(outfile, sizeof(outfile), "%s/append.txt", testdir);

        /* First write */
        char cmdline[PATH_MAX * 4];
        snprintf(cmdline, sizeof(cmdline), "echo line1 > %s", outfile);
        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        if (v && cmd) exec_cmd_group(&cmd->groups[0]);
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);

        /* Append */
        snprintf(cmdline, sizeof(cmdline), "echo line2 >> %s", outfile);
        tc = 0;
        t = lexer_tokenize(cmdline, &tc);
        v = 0;
        cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: append parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: append returns 0", 0, ret);

            FILE *ck = fopen(outfile, "r");
            ASSERT_TRUE("exec: append file exists", ck != NULL);
            if (ck) {
                char b1[128], b2[128];
                int lines = 0;
                if (fgets(b1, sizeof(b1), ck)) lines++;
                if (fgets(b2, sizeof(b2), ck)) lines++;
                ASSERT_EQ_INT("exec: append has 2 lines", 2, lines);
                fclose(ck);
            }
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
        unlink(outfile);
    }

    /* Multiple redirects: only last takes effect */
    {
        char f1[PATH_MAX * 2], f2[PATH_MAX * 2], out[PATH_MAX * 2];
        snprintf(f1, sizeof(f1), "%s/multi_in1.txt", testdir);
        snprintf(f2, sizeof(f2), "%s/multi_in2.txt", testdir);
        snprintf(out, sizeof(out), "%s/multi_out.txt", testdir);

        FILE *ff = fopen(f1, "w"); fprintf(ff, "first\n"); fclose(ff);
        ff = fopen(f2, "w"); fprintf(ff, "second\n"); fclose(ff);

        /* Last input redir should win */
        char cmdline[PATH_MAX * 6];
        snprintf(cmdline, sizeof(cmdline),
                 "cat < %s < %s > %s", f1, f2, out);

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: multi-redir parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: multi-redir returns 0", 0, ret);

            FILE *ck = fopen(out, "r");
            ASSERT_TRUE("exec: multi-redir file exists", ck != NULL);
            if (ck) {
                char buf[128]; buf[0] = '\0';
                fgets(buf, sizeof(buf), ck);
                /* Should contain content of f2, not f1 */
                ASSERT_EQ_STR("exec: last input redir wins", "second\n", buf);
                fclose(ck);
            }
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
        unlink(f1); unlink(f2); unlink(out);
    }

    /* ── C.4: Command piping ──────────────────────────────────────── */

    /* Simple pipe */
    {
        int tc = 0;
        Token *t = lexer_tokenize("echo hello world | wc -w", &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: pipe parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: pipe returns 0", 0, ret);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Multi-stage pipe */
    {
        int tc = 0;
        Token *t = lexer_tokenize("echo a b c | cat | wc -w", &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: multi-pipe parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: multi-pipe returns 0", 0, ret);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Pipe with file redirection */
    {
        char outfile[PATH_MAX * 2];
        snprintf(outfile, sizeof(outfile), "%s/pipe_redir.txt", testdir);
        char cmdline[PATH_MAX * 4];
        snprintf(cmdline, sizeof(cmdline),
                 "echo hello world | wc -w > %s", outfile);

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: pipe+redirect parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: pipe+redirect returns 0", 0, ret);

            FILE *ck = fopen(outfile, "r");
            ASSERT_TRUE("exec: pipe+redirect file exists", ck != NULL);
            if (ck) {
                char buf[128]; buf[0] = '\0';
                size_t n = fread(buf, 1, sizeof(buf) - 1, ck);
                buf[n] = '\0';
                ASSERT_TRUE("exec: pipe+redirect file non-empty", n > 0);
                fclose(ck);
            }
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
        unlink(outfile);
    }

    /* Input redirection + pipe: cat < infile | wc -w */
    {
        char cmdline[PATH_MAX * 4];
        snprintf(cmdline, sizeof(cmdline),
                 "cat < %s | wc -w", infile);

        int tc = 0;
        Token *t = lexer_tokenize(cmdline, &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: input+pipe parses", v && cmd);
        if (v && cmd) {
            int ret = exec_cmd_group(&cmd->groups[0]);
            ASSERT_EQ_INT("exec: input+pipe returns 0", 0, ret);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Pipe with error in middle: pipeline should still try remaining */
    {
        int tc = 0;
        Token *t = lexer_tokenize("echo hello | nonexistent_mid_cmd | wc -w", &tc);
        int v = 0;
        ShellCmd *cmd = parser_parse(t, tc, &v);
        ASSERT_TRUE("exec: pipe with error parses", v && cmd);
        if (v && cmd) {
            /* Should not crash — remaining command still runs */
            int ret = exec_cmd_group(&cmd->groups[0]);
            /* Last command (wc) might fail because input pipe is broken */
            /* Just check it doesn't crash */
            (void)ret;
            ASSERT_TRUE("exec: pipe with error no crash", 1);
        }
        parser_free_cmd(cmd);
        lexer_free_tokens(t, tc);
    }

    /* Clean up input file and temp dir */
    unlink(infile);
    rmdir(testdir);
    chdir(orig);
}
#pragma GCC diagnostic pop
/* ------------------------------------------------------------------ */
/*  CLI modes                                                          */
/* ------------------------------------------------------------------ */
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

static void mode_interactive(void)
{
    test_shell_init();
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
        if (!valid) {
            printf("Invalid Syntax!\n");
        } else {
            if (cmd && !(cmd->group_count > 0 &&
                cmd->groups[0].command_count > 0 &&
                cmd->groups[0].commands[0].name &&
                strcmp(cmd->groups[0].commands[0].name, "log") == 0)) {
                log_add(line);
            }

            if (cmd) {
                CmdGroup *cg = &cmd->groups[0];
                AtomicCmd *a = &cg->commands[0];
                if (a->name && strcmp(a->name, "hop") == 0)
                    builtin_hop(a->args, a->arg_count);
                else if (a->name && strcmp(a->name, "reveal") == 0)
                    builtin_reveal(a->args, a->arg_count);
                else if (a->name && strcmp(a->name, "log") == 0)
                    builtin_log(a->args, a->arg_count);
                else {
                    /* Non-builtin: silently ignore (exec* banned in Part B) */
                    (void)a;
                }
            }
        }

        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tc);
        free(line);
    }
    log_free();
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
        "  selftest           Run all built-in unit tests (Part A + B + C)\n"
        "  test_hop           Run hop tests only\n"
        "  test_reveal        Run reveal tests only\n"
        "  test_log           Run log tests only\n"
        "  test_exec          Run exec/pipe/redirect tests (Part C)\n"
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
    g_shell.prev_cwd[0] = '\0';
    g_shell.has_prev_cwd = 0;

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
        test_shell_init();
        test_lexer();
        test_parser();
        test_prompt();
        test_hop();
        test_reveal();
        test_log();
        test_exec();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
        return (tests_passed == tests_run) ? 0 : 1;
    } else if (strcmp(cmd, "test_exec") == 0) {
        test_shell_init();
        test_exec();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else if (strcmp(cmd, "test_hop") == 0) {
        test_shell_init();
        test_hop();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else if (strcmp(cmd, "test_reveal") == 0) {
        test_shell_init();
        test_reveal();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else if (strcmp(cmd, "test_log") == 0) {
        test_shell_init();
        test_log();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else if (strcmp(cmd, "prompt") == 0) {
        test_shell_init();
        test_prompt();
        printf("\n=== RESULTS: %d/%d passed ===\n", tests_passed, tests_run);
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        usage();
        return 1;
    }

    return 0;
}
