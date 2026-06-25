/*
 * input.c  --  User input reading (Part A + E.3)
 *
 * Reads a line from stdin using a dynamic buffer.
 * Returns NULL on EOF (Ctrl-D) so the caller can exit gracefully.
 * Signal handlers use SA_RESTART, so fgetc is automatically restarted
 * after SIGINT/SIGTSTP. No manual EINTR handling is needed.
 */
#include "shell.h"

char *input_read_line(void)
{
    size_t capacity = SHELL_BUF_SIZE;
    size_t len      = 0;
    char  *buf      = malloc(capacity);
    if (!buf)
        return NULL;

    int ch;
    while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
        if (len + 1 >= capacity) {
            capacity *= 2;
            char *tmp = realloc(buf, capacity);
            if (!tmp) {
                free(buf);
                return NULL;
            }
            buf = tmp;
        }
        buf[len++] = (char)ch;
    }

    if (ch == EOF && len == 0) {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}
