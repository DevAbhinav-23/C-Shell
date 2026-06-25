/*
 * main.c  --  Shell entry point (Parts A through E)
 *
 * The REPL loop: prompt -> read -> parse -> dispatch.
 * Builtin commands (hop, reveal, log, activities, ping, fg, bg)
 * are handled internally. External commands use fork+execvp.
 * Signal handling: Ctrl-C, Ctrl-D, Ctrl-Z (Part E.3).
 */
#include "shell.h"
#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "signals.h"
#include <signal.h>
#include <sys/wait.h>

/* Global shell state */
ShellState g_shell;

/* ------------------------------------------------------------------ */
/*  Initialise shell state                                             */
/* ------------------------------------------------------------------ */
static void shell_init(void)
{
    if (getcwd(g_shell.home_dir, sizeof(g_shell.home_dir)) == NULL) {
        perror("getcwd");
        strcpy(g_shell.home_dir, ".");
    }
    g_shell.prev_cwd[0] = '\0';
    g_shell.has_prev_cwd = 0;
    g_shell.foreground_pgid = 0;

    /* Initialise the log / history subsystem */
    log_init();

    /* Initialise background job tracking */
    g_shell.bg_job_count   = 0;
    g_shell.bg_next_job_id = 1;

    /* Initialise signal handlers (Part E.3) */
    signal_init();
}

/* ------------------------------------------------------------------ */
/*  Check if a command name is "log" (used to suppress storing)        */
/* ------------------------------------------------------------------ */
static int is_log_command(const ShellCmd *cmd)
{
    if (!cmd || cmd->group_count == 0) return 0;
    const CmdGroup *g = &cmd->groups[0];
    if (g->command_count == 0) return 0;
    const AtomicCmd *a = &g->commands[0];
    return (a->name && strcmp(a->name, "log") == 0);
}

/* ------------------------------------------------------------------ */
/*  Check if a command is a "meta" command that is handled inline      */
/* ------------------------------------------------------------------ */
static int is_meta_command(const char *name)
{
    return (name && (
        strcmp(name, "activities") == 0 ||
        strcmp(name, "ping") == 0 ||
        strcmp(name, "fg") == 0 ||
        strcmp(name, "bg") == 0));
}

/* ------------------------------------------------------------------ */
/*  Execute a parsed ShellCmd                                          */
/*                                                                     */
/*  - Meta commands (activities, ping, fg, bg) are handled in parent.  */
/*  - Builtin commands (hop, reveal, log) run in parent if no redirs.  */
/*  - Everything else uses exec_cmd_group / exec_cmd_group_bg.         */
/*                                                                     */
/*  Part D: iterates over all cmd_groups.  The separators array tells  */
/*  us how to run each group:                                          */
/*      separators[i] == 0  →  sequential (;): wait for completion     */
/*      separators[i] == 1  →  background (&): fork and don't wait     */
/*      trailing_amp == 1   →  last group also runs in background      */
/*                                                                     */
/*  If a sequential command fails, subsequent commands still run.       */
/* ------------------------------------------------------------------ */
static int exec_cmd(ShellCmd *cmd)
{
    if (!cmd)
        return 0;

    int ret = 0;

    for (int i = 0; i < cmd->group_count; i++) {
        /* Determine if this group should run in background */
        int is_bg;
        if (i < cmd->group_count - 1)
            is_bg = cmd->separators[i];
        else
            is_bg = cmd->trailing_amp;

        /* Check if it's a single meta/builtin command (handle in parent) */
        CmdGroup *cg = &cmd->groups[i];
        if (cg->command_count == 1 && cg->commands[0].name) {
            const char *name = cg->commands[0].name;

            if (is_meta_command(name) && !is_bg) {
                if (strcmp(name, "activities") == 0)
                    ret = builtin_activities();
                else if (strcmp(name, "ping") == 0)
                    ret = builtin_ping(cg->commands[0].args, cg->commands[0].arg_count);
                else if (strcmp(name, "fg") == 0)
                    ret = builtin_fg(cg->commands[0].args, cg->commands[0].arg_count);
                else if (strcmp(name, "bg") == 0)
                    ret = builtin_bg(cg->commands[0].args, cg->commands[0].arg_count);
                continue;
            }
        }

        if (is_bg) {
            exec_cmd_group_bg(&cmd->groups[i]);
        } else {
            ret = exec_cmd_group(&cmd->groups[i]);
        }
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  Main REPL loop                                                     */
/* ------------------------------------------------------------------ */
int main(void)
{
    shell_init();

    while (1) {
        /* Reap any completed background jobs and handle SIGCHLD */
        if (g_sigchld_received) {
            g_sigchld_received = 0;
            check_background_jobs();
        }

        prompt_print();

        char *line = input_read_line();
        if (!line) {
            /* EOF (Ctrl-D): kill all children and print "logout" */
            kill_all_children();
            printf("logout\n");
            break;
        }

        /* Check for completed background processes (D.2) */
        check_background_jobs();

        /* Skip empty input */
        if (line[0] == '\0') {
            free(line);
            continue;
        }

        /* Tokenize */
        int tok_count = 0;
        Token *tokens = lexer_tokenize(line, &tok_count);

        /* Parse */
        int valid = 0;
        ShellCmd *cmd = parser_parse(tokens, tok_count, &valid);

        if (!valid) {
            printf("Invalid Syntax!\n");
        } else {
            /* Add to log BEFORE executing (unless it's a log command) */
            if (!is_log_command(cmd)) {
                log_add(line);
            }

            /* Execute */
            exec_cmd(cmd);
        }

        /* Cleanup */
        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tok_count);
        free(line);
    }

    /* Clean up on exit */
    log_free();

    return 0;
}
