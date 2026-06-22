/*
 * builtin_hop.c  --  hop command (B.1)
 *
 * Syntax: hop ((~ | . | .. | - | name)*)?
 *
 * Changes the CWD sequentially for each argument:
 *   ~ or no args -> home dir
 *   .            -> no-op
 *   ..           -> parent dir (or no-op)
 *   -            -> previous CWD (or no-op if none)
 *   name         -> absolute or relative path
 */
#include "shell.h"

/* Helper: save current CWD into prev_cwd before a chdir */
static void save_prev(void)
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        strncpy(g_shell.prev_cwd, cwd, PATH_MAX - 1);
        g_shell.prev_cwd[PATH_MAX - 1] = '\0';
        g_shell.has_prev_cwd = 1;
    }
}

int builtin_hop(char **args, int arg_count)
{
    /* No arguments: hop to home directory */
    if (arg_count == 1) {
        save_prev();
        if (chdir(g_shell.home_dir) != 0) {
            fprintf(stderr, "No such directory!\n");
            return 1;
        }
        return 0;
    }

    /* Process each argument sequentially */
    for (int i = 1; i < arg_count; i++) {
        const char *arg = args[i];

        if (strcmp(arg, ".") == 0) {
            /* Do nothing */
            continue;
        } else if (strcmp(arg, "-") == 0) {
            /* Go to previous CWD */
            if (!g_shell.has_prev_cwd)
                continue;

            /* Save current CWD, then swap: current <-> prev */
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) == NULL)
                continue;

            /* prev_cwd will become the one we just left */
            char target[PATH_MAX];
            strncpy(target, g_shell.prev_cwd, PATH_MAX - 1);
            target[PATH_MAX - 1] = '\0';

            /* Update prev_cwd to current */
            strncpy(g_shell.prev_cwd, cwd, PATH_MAX - 1);
            g_shell.prev_cwd[PATH_MAX - 1] = '\0';
            g_shell.has_prev_cwd = 1;

            if (chdir(target) != 0) {
                fprintf(stderr, "No such directory!\n");
                return 1;
            }
        } else {
            /* ~, .., name -- anything that actually changes CWD */
            save_prev();

            const char *target = arg;
            if (strcmp(arg, "~") == 0)
                target = g_shell.home_dir;

            if (chdir(target) != 0) {
                fprintf(stderr, "No such directory!\n");
                return 1;
            }
        }
    }

    return 0;
}
