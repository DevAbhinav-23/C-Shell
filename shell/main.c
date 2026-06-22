/*
 * main.c  --  Shell entry point
 *
 * The REPL loop: prompt -> read -> parse -> print error if invalid.
 * Part A does not execute commands, so only syntax validation is done.
 */
#include "shell.h"
#include "prompt.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"

/* Global shell state */
ShellState g_shell;

/* ------------------------------------------------------------------ */
/*  Initialise shell state (set home to cwd at startup)                */
/* ------------------------------------------------------------------ */
static void shell_init(void)
{
    if (getcwd(g_shell.home_dir, sizeof(g_shell.home_dir)) == NULL) {
        perror("getcwd");
        strcpy(g_shell.home_dir, ".");
    }
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
        }

        /* Cleanup */
        parser_free_cmd(cmd);
        lexer_free_tokens(tokens, tok_count);
        free(line);
    }

    return 0;
}
