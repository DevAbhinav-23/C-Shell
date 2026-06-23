/*
 * builtin_log.c  --  log command (B.3)
 *
 * Syntax: log (purge | execute <index>)?
 *
 * - No arguments: print stored commands oldest to newest.
 * - purge: clear history.
 * - execute <index>: execute the command at the given 1-based index
 *   (newest=1, oldest=N).
 *
 * History is stored in a ring buffer of size SHELL_MAX_HISTORY (15).
 * History persists across shell sessions via a file (~/.cshell_history).
 */
#include "shell.h"
#include "executor.h"
#include <dirent.h>

/* ──────────────────────────────────────────────────────────
 *  History file helpers
 * ────────────────────────────────────────────────────────── */

static void build_history_path(char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%s/.cshell_history", g_shell.home_dir);
}

/* Save the current in-memory history to disk */
static void history_save(void)
{
    FILE *fp = fopen(g_shell.history_file, "w");
    if (!fp)
        return;

    /* Write oldest to newest */

    if (g_shell.history_full) {
        /* Ring buffer: head+1 is the oldest */
        int start = (g_shell.history_head + 1) % SHELL_MAX_HISTORY;
        for (int i = 0; i < SHELL_MAX_HISTORY; i++) {
            int idx = (start + i) % SHELL_MAX_HISTORY;
            if (g_shell.history[idx].cmd_line)
                fprintf(fp, "%s\n", g_shell.history[idx].cmd_line);
        }
    } else {
        for (int i = 0; i < g_shell.history_count; i++) {
            if (g_shell.history[i].cmd_line)
                fprintf(fp, "%s\n", g_shell.history[i].cmd_line);
        }
    }

    fclose(fp);
}

/* Load history from disk */
static void history_load(void)
{
    FILE *fp = fopen(g_shell.history_file, "r");
    if (!fp)
        return;

    char line[SHELL_BUF_SIZE];
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0)
            continue;

        /* Add to ring buffer */
        log_add(line);
    }

    fclose(fp);
}

/* ──────────────────────────────────────────────────────────
 *  Public API (called from main.c)
 * ────────────────────────────────────────────────────────── */

void log_init(void)
{
    g_shell.history_count = 0;
    g_shell.history_head  = 0;
    g_shell.history_full  = 0;
    for (int i = 0; i < SHELL_MAX_HISTORY; i++)
        g_shell.history[i].cmd_line = NULL;

    build_history_path(g_shell.history_file, sizeof(g_shell.history_file));
    history_load();
}

void log_add(const char *cmd)
{
    if (!cmd || cmd[0] == '\0')
        return;

    /* Don't add if identical to the most recent command */
    if (g_shell.history_count > 0) {
        int latest_idx;
        if (g_shell.history_full) {
            latest_idx = g_shell.history_head;
        } else {
            latest_idx = g_shell.history_count - 1;
        }
        if (g_shell.history[latest_idx].cmd_line &&
            strcmp(g_shell.history[latest_idx].cmd_line, cmd) == 0)
            return;
    }

    int idx;
    if (g_shell.history_full) {
        /* Overwrite the oldest entry */
        idx = (g_shell.history_head + 1) % SHELL_MAX_HISTORY;
        free(g_shell.history[idx].cmd_line);
        g_shell.history[idx].cmd_line = shell_strdup(cmd);
        g_shell.history_head = idx;
    } else {
        idx = g_shell.history_count;
        g_shell.history[idx].cmd_line = shell_strdup(cmd);
        g_shell.history_count++;

        if (g_shell.history_count == SHELL_MAX_HISTORY) {
            g_shell.history_full = 1;
            g_shell.history_head = SHELL_MAX_HISTORY - 1;
        }
    }

    history_save();
}

void log_free(void)
{
    for (int i = 0; i < SHELL_MAX_HISTORY; i++) {
        free(g_shell.history[i].cmd_line);
        g_shell.history[i].cmd_line = NULL;
    }
    g_shell.history_count = 0;
}

/* ──────────────────────────────────────────────────────────
 *  Get command at a 1-based index (newest=1, oldest=N)
 *  Returns NULL if index is out of range.
 * ────────────────────────────────────────────────────────── */
static const char *history_get(int one_based_index)
{
    int total = g_shell.history_full ? SHELL_MAX_HISTORY : g_shell.history_count;
    if (one_based_index < 1 || one_based_index > total)
        return NULL;

    if (g_shell.history_full) {
        /* Newest is at history_head */
        int idx = (g_shell.history_head - (one_based_index - 1) + SHELL_MAX_HISTORY)
                  % SHELL_MAX_HISTORY;
        return g_shell.history[idx].cmd_line;
    } else {
        /* Newest is at history_count - 1 */
        int idx = (g_shell.history_count - 1) - (one_based_index - 1);
        return g_shell.history[idx].cmd_line;
    }
}

/* ──────────────────────────────────────────────────────────
 *  Get total number of stored commands
 * ────────────────────────────────────────────────────────── */
static int history_total(void)
{
    return g_shell.history_full ? SHELL_MAX_HISTORY : g_shell.history_count;
}

/* ──────────────────────────────────────────────────────────
 *  Print history oldest to newest
 * ────────────────────────────────────────────────────────── */
static void history_print(void)
{
    int total = history_total();

    if (g_shell.history_full) {
        int start = (g_shell.history_head + 1) % SHELL_MAX_HISTORY;
        for (int i = 0; i < SHELL_MAX_HISTORY; i++) {
            int idx = (start + i) % SHELL_MAX_HISTORY;
            if (g_shell.history[idx].cmd_line)
                printf("%s\n", g_shell.history[idx].cmd_line);
        }
    } else {
        for (int i = 0; i < total; i++) {
            if (g_shell.history[i].cmd_line)
                printf("%s\n", g_shell.history[i].cmd_line);
        }
    }
    (void)total; /* suppress unused warning when SHELL_MAX_HISTORY == 0 */
}

/* ──────────────────────────────────────────────────────────
 *  Purge history
 * ────────────────────────────────────────────────────────── */
static void history_purge(void)
{
    for (int i = 0; i < SHELL_MAX_HISTORY; i++) {
        free(g_shell.history[i].cmd_line);
        g_shell.history[i].cmd_line = NULL;
    }
    g_shell.history_count = 0;
    g_shell.history_head  = 0;
    g_shell.history_full  = 0;

    /* Remove the history file */
    remove(g_shell.history_file);
}

/* ──────────────────────────────────────────────────────────
 *  builtin_log  --  Main entry point
 * ────────────────────────────────────────────────────────── */
int builtin_log(char **args, int arg_count)
{
    /* log (no arguments): print history oldest to newest */
    if (arg_count == 1) {
        history_print();
        return 0;
    }

    /* log purge */
    if (arg_count == 2 && strcmp(args[1], "purge") == 0) {
        history_purge();
        return 0;
    }

    /* log execute <index> */
    if (arg_count == 3 && strcmp(args[1], "execute") == 0) {
        /* Parse the index */
        char *endptr;
        long idx = strtol(args[2], &endptr, 10);
        if (*endptr != '\0' || idx <= 0) {
            fprintf(stderr, "log: Invalid Syntax!\n");
            return 1;
        }

        const char *cmd = history_get((int)idx);
        if (!cmd) {
            fprintf(stderr, "log: Invalid Syntax!\n");
            return 1;
        }

        /* Execute the command by re-parsing it.
         * We need to tokenize, parse, and dispatch. Since we can't call
         * the REPL directly from here, we'll do the tokenizing and parsing
         * and return a special value. For now, print and use system()-like
         * approach, but since we can't use exec*, we re-parse and dispatch
         * via a callback-like mechanism. We handle this by returning -1
         * to signal that the caller should execute the returned command.
         *
         * Actually, the cleanest approach: we do the full pipeline ourselves
         * right here for single commands that are builtins, or re-parse.
         * For Part B, only builtins exist so we re-parse inline.
         */
        printf("%s\n", cmd);

        /* Re-parse and execute using the executor (supports pipes, redirects, etc.) */
        int tc = 0;
        Token *tokens = lexer_tokenize(cmd, &tc);
        int valid = 0;
        ShellCmd *scmd = parser_parse(tokens, tc, &valid);

        if (!valid || !scmd) {
            lexer_free_tokens(tokens, tc);
            return 0;
        }

        /* Execute each group via the executor */
        int ret = 0;
        for (int g = 0; g < scmd->group_count; g++) {
            ret = exec_cmd_group(&scmd->groups[g]);
        }

        parser_free_cmd(scmd);
        lexer_free_tokens(tokens, tc);
        return ret;
    }

    /* Anything else is invalid */
    fprintf(stderr, "log: Invalid Syntax!\n");
    return 1;
}
