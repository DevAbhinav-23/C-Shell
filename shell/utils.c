/*
 * utils.c  --  Small utility helpers shared across the shell.
 */
#include "shell.h"
#include <sys/stat.h>

char *shell_strdup(const char *s)
{
    if (!s)
        return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (dup)
        strcpy(dup, s);
    return dup;
}

/*
 * resolve_path  --  Resolve a shell path argument to an absolute path.
 *
 * Handles ~, ., .., -, and arbitrary names.
 * Writes the resolved absolute path into `out` (size PATH_MAX).
 * Does NOT change the CWD. Uses chdir to resolve .. and verify existence.
 * Returns 0 on success, -1 if the directory does not exist.
 */
int resolve_path(const char *arg, char *out, size_t out_sz)
{
    char save_cwd[PATH_MAX];
    if (getcwd(save_cwd, sizeof(save_cwd)) == NULL)
        strcpy(save_cwd, ".");

    /* Determine the target to chdir into */
    const char *target;
    if (arg == NULL || strcmp(arg, "~") == 0 || arg[0] == '\0')
        target = g_shell.home_dir;
    else if (strcmp(arg, ".") == 0)
        target = save_cwd;
    else if (strcmp(arg, "..") == 0)
        target = "..";
    else if (strcmp(arg, "-") == 0) {
        if (!g_shell.has_prev_cwd) return -1;
        target = g_shell.prev_cwd;
    } else
        target = arg;

    if (chdir(target) != 0)
        return -1;

    if (getcwd(out, out_sz) == NULL) {
        chdir(save_cwd);
        return -1;
    }

    /* Restore original CWD */
    chdir(save_cwd);
    return 0;
}
