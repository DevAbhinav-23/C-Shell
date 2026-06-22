#ifndef INPUT_H
#define INPUT_H

/*
 * input.c  --  Read user input from stdin (A.2)
 *
 * Returns a heap-allocated string (caller must free).
 * Returns NULL on EOF (Ctrl-D).
 */
char *input_read_line(void);

#endif /* INPUT_H */
