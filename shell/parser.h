#ifndef PARSER_H
#define PARSER_H

#include "shell.h"

/*
 * parser.c  --  Recursive descent parser for the shell grammar
 *
 * Grammar (tokens: WORD, PIPE, AND, SEMICOLON, INPUT, OUTPUT, OUTPUT_APPEND):
 *
 *   shell_cmd   -> cmd_group ( (&|;) cmd_group )*  &?
 *   cmd_group   -> atomic ( PIPE atomic )*
 *   atomic      -> WORD ( WORD | input | output )*
 *   input       -> INPUT WORD
 *   output      -> (OUTPUT | OUTPUT_APPEND) WORD
 *
 * Returns a heap-allocated ShellCmd on success, NULL on invalid syntax.
 * Sets *out_valid to 1 if the input parsed correctly, 0 otherwise.
 */
ShellCmd *parser_parse(Token *tokens, int token_count, int *out_valid);

/*
 * Free all memory owned by a ShellCmd tree.
 */
void parser_free_cmd(ShellCmd *cmd);

#endif /* PARSER_H */
