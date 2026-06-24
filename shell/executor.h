#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"

/*
 * executor.c  --  Command execution with pipes, redirects (Part C)
 *                 Sequential & background execution (Part D)
 *
 * Executes a command group (pipeline), handling:
 *   - fork + execvp for arbitrary commands
 *   - Input redirection  (<)
 *   - Output redirection (>, >>)
 *   - Command piping (|)
 *   - Combined redirects + pipes
 *   - Sequential execution (;)  — handled at ShellCmd level in main.c
 *   - Background execution (&)  — via exec_cmd_group_bg()
 *
 * exec_cmd_group()  returns the exit status of the last command in the pipeline.
 * exec_cmd_group_bg() forks a background process, returns the job number,
 *                     and prints "[job_number] pid".
 * check_background_jobs() reaps completed background processes and prints
 *                         completion messages.
 */
int exec_cmd_group(const CmdGroup *g);
int exec_cmd_group_bg(const CmdGroup *g);
void check_background_jobs(void);

#endif /* EXECUTOR_H */
