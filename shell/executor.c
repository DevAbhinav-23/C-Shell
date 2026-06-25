/*
 * executor.c  --  Command execution with pipes, redirects, jobs (Parts C + D + E)
 *
 * Handles:
 *   - External command execution (fork + execvp)
 *   - Input redirection (<)
 *   - Output redirection (>, >>)
 *   - Command piping (|)
 *   - Combined redirects + pipes
 *   - Sequential execution (;)
 *   - Background execution (&) with job tracking
 *   - Process group management for Ctrl-C / Ctrl-Z (Part E)
 */
#include "shell.h"
#include "executor.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

/* ──────────────────────────────────────────────────────────
 *  Helpers
 * ────────────────────────────────────────────────────────── */

static int is_builtin(const char *name)
{
    return (strcmp(name, "hop") == 0 ||
            strcmp(name, "reveal") == 0 ||
            strcmp(name, "log") == 0);
}

static int exec_builtin(const AtomicCmd *a)
{
    if (strcmp(a->name, "hop") == 0)
        return builtin_hop(a->args, a->arg_count);
    if (strcmp(a->name, "reveal") == 0)
        return builtin_reveal(a->args, a->arg_count);
    if (strcmp(a->name, "log") == 0)
        return builtin_log(a->args, a->arg_count);
    return 1;
}

/* ──────────────────────────────────────────────────────────
 *  reconstruct_cmd  --  Reconstruct a command string from a CmdGroup
 * ────────────────────────────────────────────────────────── */
void reconstruct_cmd(const CmdGroup *g, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    for (int i = 0; i < g->command_count; i++) {
        if (i > 0) {
            strncat(buf, " | ", bufsz - strlen(buf) - 1);
        }
        const AtomicCmd *a = &g->commands[i];
        if (a->name) {
            strncat(buf, a->name, bufsz - strlen(buf) - 1);
        }
        for (int j = 1; j < a->arg_count; j++) {
            strncat(buf, " ", bufsz - strlen(buf) - 1);
            strncat(buf, a->args[j], bufsz - strlen(buf) - 1);
        }
        for (int j = 0; j < a->redirect_count; j++) {
            const char *op = (a->redirects[j].type == REDIR_INPUT) ? " <" :
                             (a->redirects[j].type == REDIR_OUTPUT) ? " >" : " >>";
            strncat(buf, op, bufsz - strlen(buf) - 1);
            strncat(buf, " ", bufsz - strlen(buf) - 1);
            strncat(buf, a->redirects[j].filename, bufsz - strlen(buf) - 1);
        }
    }
}

/* ──────────────────────────────────────────────────────────
 *  Job management helpers
 * ────────────────────────────────────────────────────────── */

int add_job(pid_t pid, pid_t pgid, const char *cmd_name,
            const char *full_cmd, JobState state, int is_bg)
{
    if (g_shell.bg_job_count >= SHELL_MAX_BG_JOBS) {
        fprintf(stderr, "bg: job table full\n");
        return -1;
    }
    BgJob *j = &g_shell.bg_jobs[g_shell.bg_job_count++];
    j->pid    = pid;
    j->pgid   = pgid;
    j->job_id = g_shell.bg_next_job_id++;
    j->state  = state;
    j->is_bg  = is_bg;
    strncpy(j->cmd_name, cmd_name ? cmd_name : "unknown", sizeof(j->cmd_name) - 1);
    j->cmd_name[sizeof(j->cmd_name) - 1] = '\0';
    strncpy(j->full_cmd, full_cmd ? full_cmd : "", sizeof(j->full_cmd) - 1);
    j->full_cmd[sizeof(j->full_cmd) - 1] = '\0';
    return j->job_id;
}

void remove_job_by_pid(pid_t pid)
{
    for (int i = 0; i < g_shell.bg_job_count; i++) {
        if (g_shell.bg_jobs[i].pid == pid) {
            g_shell.bg_jobs[i] = g_shell.bg_jobs[g_shell.bg_job_count - 1];
            g_shell.bg_job_count--;
            return;
        }
    }
}

BgJob *find_job_by_id(int job_id)
{
    for (int i = 0; i < g_shell.bg_job_count; i++) {
        if (g_shell.bg_jobs[i].job_id == job_id)
            return &g_shell.bg_jobs[i];
    }
    return NULL;
}

BgJob *find_job_by_pgid(pid_t pgid)
{
    for (int i = 0; i < g_shell.bg_job_count; i++) {
        if (g_shell.bg_jobs[i].pgid == pgid)
            return &g_shell.bg_jobs[i];
    }
    return NULL;
}

void cleanup_done_jobs(void)
{
    int i = 0;
    while (i < g_shell.bg_job_count) {
        BgJob *j = &g_shell.bg_jobs[i];
        int status;
        pid_t ret = waitpid(j->pid, &status, WNOHANG | WUNTRACED | WCONTINUED);

        if (ret == 0) {
            i++;
        } else if (ret < 0) {
            /* Process gone — remove */
            g_shell.bg_jobs[i] = g_shell.bg_jobs[g_shell.bg_job_count - 1];
            g_shell.bg_job_count--;
        } else {
            if (WIFSTOPPED(status)) {
                j->state = JOB_STOPPED;
                i++;
            } else if (WIFCONTINUED(status)) {
                j->state = JOB_RUNNING;
                i++;
            } else {
                /* Exited/killed — remove */
                g_shell.bg_jobs[i] = g_shell.bg_jobs[g_shell.bg_job_count - 1];
                g_shell.bg_job_count--;
            }
        }
    }
}

void kill_all_children(void)
{
    for (int i = 0; i < g_shell.bg_job_count; i++) {
        kill(g_shell.bg_jobs[i].pgid, SIGKILL);
    }
    g_shell.bg_job_count = 0;
}

/* ──────────────────────────────────────────────────────────
 *  run_atomic_in_child
 *
 *  Called AFTER fork().  Sets up process group, applies file
 *  redirects (pipe redirects already set up by caller), then
 *  runs the command (builtin or external).  Never returns.
 * ────────────────────────────────────────────────────────── */
static void run_atomic_in_child(const AtomicCmd *a, pid_t pgid)
{
    /* Set up process group */
    setpgid(0, pgid);

    if (!a->name)
        _exit(0);

    /* ── Apply file redirections ─────────────────────── */
    int input_fd  = -1;
    int output_fd = -1;

    for (int i = 0; i < a->redirect_count; i++) {
        const Redirect *r = &a->redirects[i];

        if (r->type == REDIR_INPUT) {
            if (input_fd >= 0) close(input_fd);
            int fd = open(r->filename, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "No such file or directory\n");
                _exit(1);
            }
            input_fd = fd;
        } else if (r->type == REDIR_OUTPUT) {
            if (output_fd >= 0) close(output_fd);
            int fd = open(r->filename,
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                fprintf(stderr, "Unable to create file for writing\n");
                _exit(1);
            }
            output_fd = fd;
        } else if (r->type == REDIR_OUTPUT_APPEND) {
            if (output_fd >= 0) close(output_fd);
            int fd = open(r->filename,
                          O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd < 0) {
                fprintf(stderr, "Unable to create file for writing\n");
                _exit(1);
            }
            output_fd = fd;
        }
    }

    if (input_fd >= 0) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }
    if (output_fd >= 0) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }

    /* ── Builtin ─────────────────────────────────────── */
    if (is_builtin(a->name))
        _exit(exec_builtin(a));

    /* ── External command ────────────────────────────── */
    execvp(a->name, a->args);
    fprintf(stderr, "Command not found!\n");
    _exit(1);
}

/* ──────────────────────────────────────────────────────────
 *  exec_cmd_group  --  Execute a command group (pipeline)
 * ────────────────────────────────────────────────────────── */
int exec_cmd_group(const CmdGroup *g)
{
    int n = g->command_count;
    if (n == 0)
        return 0;

    /* ── Single command ──────────────────────────────── */
    if (n == 1) {
        const AtomicCmd *a = &g->commands[0];
        if (!a->name)
            return 0;

        /*
         * Builtins without redirects run in the parent process
         * so they can modify shell state (e.g. hop changes CWD).
         */
        if (a->redirect_count == 0 && is_builtin(a->name))
            return exec_builtin(a);

        /* Everything else forks */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }

        pid_t pgid = pid;
        if (pid == 0) {
            run_atomic_in_child(a, pgid);  /* never returns */
        }

        /* Parent: set up process group */
        setpgid(pid, pgid);
        g_shell.foreground_pgid = pgid;

        /* Reconstruct command string for job tracking */
        char full_cmd[1024];
        reconstruct_cmd(g, full_cmd, sizeof(full_cmd));
        add_job(pid, pgid, a->name, full_cmd, JOB_RUNNING, 0);

        int status;
        waitpid(pid, &status, WUNTRACED | WCONTINUED);

        /* Handle stop/continue */
        if (WIFSTOPPED(status)) {
            BgJob *job = find_job_by_pgid(pgid);
            if (job) {
                job->state = JOB_STOPPED;
                printf("\n[%d] Stopped %s\n", job->job_id, job->cmd_name);
            }
            g_shell.foreground_pgid = 0;
            return 0;
        } else if (WIFCONTINUED(status)) {
            /* Shouldn't happen for single-command fg */
        } else {
            remove_job_by_pid(pid);
        }

        g_shell.foreground_pgid = 0;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    /* ── Pipeline (2+ commands) ─────────────────────── */
    int   pipes[n - 1][2];
    pid_t pids[n];
    pid_t pgid = 0;

    /* Create all pipes */
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }
    }

    /* Fork each command in the pipeline */
    for (int i = 0; i < n; i++) {
        const AtomicCmd *a = &g->commands[i];

        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            for (int k = 0; k < i; k++) {
                kill(pids[k], SIGTERM);
                int st;
                waitpid(pids[k], &st, 0);
            }
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return 1;
        }

        if (pids[i] == 0) {
            /* First child sets pgid for all */
            if (i == 0)
                pgid = getpid();
            setpgid(0, pgid);

            /* Wire up pipe input */
            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);

            /* Wire up pipe output */
            if (i < n - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            /* Close ALL pipe fds */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            run_atomic_in_child(a, pgid);
            /* never reaches here */
        }
    }

    /* First child's pid becomes the pgid */
    pgid = pids[0];

    /* Parent: set all children into the same process group */
    for (int i = 0; i < n; i++)
        setpgid(pids[i], pgid);

    /* Close all pipe fds in the parent */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Set as foreground process group */
    g_shell.foreground_pgid = pgid;

    /* Reconstruct and track the pipeline */
    char full_cmd[1024];
    reconstruct_cmd(g, full_cmd, sizeof(full_cmd));
    add_job(pids[0], pgid, g->commands[0].name, full_cmd, JOB_RUNNING, 0);

    /* Wait for all children */
    int ret = 0;
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, WUNTRACED | WCONTINUED);

        if (WIFSTOPPED(status) && i == n - 1) {
            BgJob *job = find_job_by_pgid(pgid);
            if (job) {
                job->state = JOB_STOPPED;
                printf("\n[%d] Stopped %s\n", job->job_id, job->cmd_name);
            }
        }
        if (i == n - 1) {
            if (WIFEXITED(status))
                ret = WEXITSTATUS(status);
            else
                ret = 1;
        }
    }

    /* If nothing was stopped, remove the job (completed) */
    {
        BgJob *job = find_job_by_pgid(pgid);
        if (job && job->state == JOB_RUNNING)
            remove_job_by_pid(pids[0]);
    }

    g_shell.foreground_pgid = 0;
    return ret;
}

/* ──────────────────────────────────────────────────────────
 *  exec_cmd_group_bg  --  Execute a command group in background
 * ────────────────────────────────────────────────────────── */
int exec_cmd_group_bg(const CmdGroup *g)
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ── CHILD ───────────────────────────────────────── */

        /* Separate process group so the shell isn't affected */
        setpgid(0, 0);

        /* Redirect stdin to /dev/null */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        /* Reset signal handlers to default for background child */
        signal(SIGINT,  SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        /* Run the command group and exit with its status */
        _exit(exec_cmd_group(g));
    }

    /* ── PARENT ──────────────────────────────────────────── */
    pid_t pgid = pid;
    setpgid(pid, pgid);

    const char *name = (g->command_count > 0 && g->commands[0].name)
                           ? g->commands[0].name : "unknown";

    char full_cmd[1024];
    reconstruct_cmd(g, full_cmd, sizeof(full_cmd));

    int job_id = add_job(pid, pgid, name, full_cmd, JOB_RUNNING, 1);
    if (job_id < 0)
        return -1;

    printf("[%d] %d\n", job_id, (int)pid);
    fflush(stdout);

    return job_id;
}

/* ──────────────────────────────────────────────────────────
 *  check_background_jobs  --  Reap and report completed bg jobs
 * ────────────────────────────────────────────────────────── */
void check_background_jobs(void)
{
    int i = 0;
    while (i < g_shell.bg_job_count) {
        BgJob *j = &g_shell.bg_jobs[i];

        int status;
        pid_t ret = waitpid(j->pid, &status, WNOHANG | WCONTINUED);

        if (ret == j->pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("%s with pid %d exited normally\n",
                       j->cmd_name, (int)j->pid);
            } else {
                printf("%s with pid %d exited abnormally\n",
                       j->cmd_name, (int)j->pid);
            }
            fflush(stdout);

            g_shell.bg_jobs[i] = g_shell.bg_jobs[g_shell.bg_job_count - 1];
            g_shell.bg_job_count--;
        } else if (ret == 0) {
            i++;
        } else {
            /* waitpid error — treat as completed */
            g_shell.bg_jobs[i] = g_shell.bg_jobs[g_shell.bg_job_count - 1];
            g_shell.bg_job_count--;
        }
    }
}
