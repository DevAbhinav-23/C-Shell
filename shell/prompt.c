/*
 * prompt.c  --  Shell prompt implementation (A.1)
 *
 * Displays: <Username@SystemName:current_path>
 * Replaces the home-directory prefix with '~' when applicable.
 */
#include "shell.h"
#include <sys/utsname.h>

/* ------------------------------------------------------------------ */
/*  Helper: build the display path, replacing home prefix with '~'    */
/* ------------------------------------------------------------------ */
static void build_display_path(char *out, size_t out_sz, const char *cwd)
{
    const char *home = g_shell.home_dir;

    /* If cwd starts with the home directory ... */
    if (strncmp(cwd, home, strlen(home)) == 0) {
        const char *rest = cwd + strlen(home);
        if (*rest == '\0') {
            /* cwd IS the home directory */
            snprintf(out, out_sz, "~");
        } else if (*rest == '/') {
            snprintf(out, out_sz, "~%s", rest);
        } else {
            /* home is a prefix but not a directory boundary -- show as-is */
            snprintf(out, out_sz, "%s", cwd);
        }
    } else {
        snprintf(out, out_sz, "%s", cwd);
    }
}

/* ------------------------------------------------------------------ */
/*  Print the prompt                                                   */
/* ------------------------------------------------------------------ */
void prompt_print(void)
{
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "?");

    /* Username */
    const char *user = getlogin();
    if (!user)
        user = getenv("USER");
    if (!user)
        user = "unknown";

    /* Hostname via uname */
    struct utsname uts;
    const char *host = "unknown";
    if (uname(&uts) == 0)
        host = uts.nodename;

    char display_path[PATH_MAX];
    build_display_path(display_path, sizeof(display_path), cwd);

    printf("<%s@%s:%s> ", user, host, display_path);
    fflush(stdout);
}
