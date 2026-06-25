#ifndef SIGNALS_H
#define SIGNALS_H

#include "shell.h"

/*
 * signals.c  --  Signal handlers for Ctrl-C, Ctrl-Z (Part E.3)
 *
 * SIGINT  (Ctrl-C): forwards SIGINT to foreground child process group.
 * SIGTSTP (Ctrl-Z): forwards SIGTSTP to foreground child process group,
 *                    moves it to background with "Stopped" state.
 * SIGCHLD: reaps zombies via check_background_jobs().
 */

void signal_init(void);

#endif /* SIGNALS_H */
