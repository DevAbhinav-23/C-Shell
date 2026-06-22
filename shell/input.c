/*
 * input.c  --  User input reading (A.2)
 *
 * Reads a line from stdin using a dynamic buffer.
 * Returns NULL on EOF so the caller can exit gracefully.
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
