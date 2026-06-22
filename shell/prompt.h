#ifndef PROMPT_H
#define PROMPT_H

/*
 * prompt.c  --  Display the shell prompt: <Username@SystemName:cwd>
 *
 * Requirements (A.1):
 *   - Username  = getlogin() / getenv("USER")
 *   - SystemName = gethostname()
 *   - cwd shows ~ when under the shell's home directory
 */
void prompt_print(void);

#endif /* PROMPT_H */
