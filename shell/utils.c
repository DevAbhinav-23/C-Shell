/*
 * utils.c  --  Small utility helpers shared across the shell.
 */
#include "shell.h"

char *shell_strdup(const char *s)
{
    if (!s)
        return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (dup)
        strcpy(dup, s);
    return dup;
}
