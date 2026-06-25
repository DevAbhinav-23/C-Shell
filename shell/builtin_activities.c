/*
 * builtin_activities.c  --  activities command (Part E.1)
 *
 * Lists all running/stopped processes spawned by the shell.
 * Format: [pid] : command_name - State
 * Sorted lexicographically by command_name.
 * Terminated processes are removed from the list.
 */
#include "shell.h"
#include <sys/wait.h>

int builtin_activities(void)
{
    /* First: clean up any already-terminated jobs */
    cleanup_done_jobs();

    /* Collect surviving jobs into a temporary array for sorting */
    int count = g_shell.bg_job_count;
    if (count == 0)
        return 0;

    /* We need to verify each job is still alive; copy the live ones */
    int live_count = 0;
    BgJob *live[SHELL_MAX_BG_JOBS];

    for (int i = 0; i < g_shell.bg_job_count; i++) {
        BgJob *j = &g_shell.bg_jobs[i];
        int status;
        pid_t ret = waitpid(j->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

        if (ret == 0) {
            /* Still alive */
            live[live_count++] = j;
        } else if (ret < 0) {
            /* Process doesn't exist anymore */
            continue;
        } else {
            /* Process state changed */
            if (WIFSTOPPED(status)) {
                j->state = JOB_STOPPED;
                live[live_count++] = j;
            } else if (WIFCONTINUED(status)) {
                j->state = JOB_RUNNING;
                live[live_count++] = j;
            } else {
                /* Exited or killed — remove */
                continue;
            }
        }
    }

    /* Update the actual job list */
    g_shell.bg_job_count = 0;
    for (int i = 0; i < live_count; i++) {
        g_shell.bg_jobs[g_shell.bg_job_count++] = *live[i];
    }

    /* Sort lexicographically by cmd_name (bubble sort) */
    for (int i = 0; i < live_count - 1; i++) {
        for (int j = 0; j < live_count - 1 - i; j++) {
            if (strcmp(g_shell.bg_jobs[j].cmd_name,
                       g_shell.bg_jobs[j + 1].cmd_name) > 0) {
                BgJob tmp = g_shell.bg_jobs[j];
                g_shell.bg_jobs[j]     = g_shell.bg_jobs[j + 1];
                g_shell.bg_jobs[j + 1] = tmp;
            }
        }
    }

    /* Print */
    for (int i = 0; i < live_count; i++) {
        BgJob *j = &g_shell.bg_jobs[i];
        const char *state_str = (j->state == JOB_RUNNING) ? "Running" : "Stopped";
        printf("[%d] : %s - %s\n", (int)j->pid, j->cmd_name, state_str);
    }

    return 0;
}
