/*
 * main.c  --  Shell entry point (Parts A + B)
 *
 * The REPL loop: prompt -> read -> parse -> dispatch.
 * Builtin commands (hop, reveal, log) are handled internally.
 */
#include "shell.h"
#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "executor.h"

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

    /* Initialise the log / history subsystem */
    log_init();
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
/*  Execute a parsed ShellCmd                                          */
/*                                                                     */
/*  Part C: only the first cmd_group is executed.  Sequential (;) and  */
/*  background (&, &&) operators cause remaining groups to be ignored. */
/* ------------------------------------------------------------------ */
static int exec_cmd(ShellCmd *cmd)
{
    if (!cmd)
        return 0;

    int ret = 0;

    /* Only execute the first cmd_group — see Part C spec */
    if (cmd->group_count > 0) {
        ret = exec_cmd_group(&cmd->groups[0]);
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
        prompt_print();

        char *line = input_read_line();
        if (!line) {
            /* EOF (Ctrl-D) */
            printf("\n");
            break;
        }

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
