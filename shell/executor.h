#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"

/*
 * executor.c  --  Command execution with pipes, redirects (Part C)
 *
 * Executes a command group (pipeline), handling:
 *   - fork + execvp for arbitrary commands
 *   - Input redirection  (<)
 *   - Output redirection (>, >>)
 *   - Command piping (|)
 *   - Combined redirects + pipes
 *
 * Returns the exit status of the last command in the pipeline.
 */
int exec_cmd_group(const CmdGroup *g);

#endif /* EXECUTOR_H */
