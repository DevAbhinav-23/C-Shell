#ifndef LEXER_H
#define LEXER_H

#include "shell.h"

/*
 * lexer.c  --  Tokenizer for shell input
 *
 * Breaks a raw input string into a sequence of Token values.
 * The lexer is grammar-agnostic; it recognises the individual
 * tokens that the parser will combine.
 *
 * Tokenization rules
 * ------------------
 *   - Whitespace (space, tab, \n, \r) is skipped (never produces a token).
 *   - & and ; are single-character tokens.
 *   - | is a single-character token.
 *   - < is a single-character token.
 *   - > starts output token; if followed by >, it becomes >>.
 *   - Anything else (contiguous non-whitespace, non-special chars)
 *     is read as a WORD token.
 *
 * The caller must free the returned array with lexer_free_tokens().
 */

Token *lexer_tokenize(const char *input, int *out_count);
void   lexer_free_tokens(Token *tokens, int count);

#endif /* LEXER_H */
