/*
 * builtin_fg_bg.c  --  fg and bg commands (Part E.4)
 *
 * fg [job_number]:
 *   - Bring a background or stopped job to the foreground.
 *   - If stopped, send SIGCONT to resume.
 *   - Wait for the job to complete or stop again.
 *   - If no job number, use the most recently created bg/stopped job.
 *   - Print the full command when bringing to foreground.
 *
 * bg [job_number]:
 *   - Resume a stopped background job by sending SIGCONT.
 *   - Job continues running in background.
 *   - Print [job_number] command_name & when resuming.
 *   - If already running: "Job already running".
 *   - If job doesn't exist: "No such job".
 */
#include "shell.h"
#include <sys/wait.h>

/* ──────────────────────────────────────────────────────────
 *  fg: bring job to foreground
 * ────────────────────────────────────────────────────────── */
int builtin_fg(char **args, int arg_count)
{
    BgJob *job = NULL;

    if (arg_count == 1) {
        /* No job number: use most recently created bg/stopped job */
        if (g_shell.bg_job_count == 0) {
            fprintf(stderr, "No such job\n");
            return 1;
        }
        /* Find the most recent job (highest job_id) */
        int max_id = -1;
        for (int i = 0; i < g_shell.bg_job_count; i++) {
            if (g_shell.bg_jobs[i].job_id > max_id) {
                max_id = g_shell.bg_jobs[i].job_id;
                job = &g_shell.bg_jobs[i];
            }
        }
    } else if (arg_count == 2) {
        char *endptr;
        long jid = strtol(args[1], &endptr, 10);
        if (*endptr != '\0' || jid <= 0) {
            fprintf(stderr, "No such job\n");
            return 1;
        }
        job = find_job_by_id((int)jid);
    } else {
        fprintf(stderr, "No such job\n");
        return 1;
    }

    if (!job) {
        fprintf(stderr, "No such job\n");
        return 1;
    }

    /* Print the full command */
    printf("%s\n", job->full_cmd);

    /* Set as foreground process group */
    g_shell.foreground_pgid = job->pgid;

    /* If stopped, send SIGCONT to resume */
    if (job->state == JOB_STOPPED) {
        kill(-job->pgid, SIGCONT);
    }

    /* Give terminal control to this process group */
    tcsetpgrp(STDIN_FILENO, job->pgid);

    /* Wait for the job */
    int status;
    pid_t ret;
    do {
        ret = waitpid(-job->pgid, &status, WUNTRACED | WCONTINUED);
    } while (ret > 0 && !WIFEXITED(status) && !WIFSIGNALED(status) && !WIFSTOPPED(status));

    /* If it was stopped again by a signal, mark it stopped */
    if (WIFSTOPPED(status)) {
        job->state = JOB_STOPPED;
        printf("\n[%d] Stopped %s\n", job->job_id, job->cmd_name);
    } else {
        /* Exited or killed — remove from job list */
        remove_job_by_pid(job->pid);
    }

    /* Return terminal control to the shell */
    tcsetpgrp(STDIN_FILENO, getpid());
    g_shell.foreground_pgid = 0;

    return 0;
}

/* ──────────────────────────────────────────────────────────
 *  bg: resume stopped background job
 * ────────────────────────────────────────────────────────── */
int builtin_bg(char **args, int arg_count)
{
    BgJob *job = NULL;

    if (arg_count == 1) {
        /* No job number: use most recently created */
        if (g_shell.bg_job_count == 0) {
            fprintf(stderr, "No such job\n");
            return 1;
        }
        int max_id = -1;
        for (int i = 0; i < g_shell.bg_job_count; i++) {
            if (g_shell.bg_jobs[i].job_id > max_id) {
                max_id = g_shell.bg_jobs[i].job_id;
                job = &g_shell.bg_jobs[i];
            }
        }
    } else if (arg_count == 2) {
        char *endptr;
        long jid = strtol(args[1], &endptr, 10);
        if (*endptr != '\0' || jid <= 0) {
            fprintf(stderr, "No such job\n");
            return 1;
        }
        job = find_job_by_id((int)jid);
    } else {
        fprintf(stderr, "No such job\n");
        return 1;
    }

    if (!job) {
        fprintf(stderr, "No such job\n");
        return 1;
    }

    /* If already running, report and do nothing */
    if (job->state == JOB_RUNNING) {
        fprintf(stderr, "Job already running\n");
        return 1;
    }

    /* Resume the stopped job */
    job->state = JOB_RUNNING;
    kill(-job->pgid, SIGCONT);
    printf("[%d] %s &\n", job->job_id, job->cmd_name);

    return 0;
}
