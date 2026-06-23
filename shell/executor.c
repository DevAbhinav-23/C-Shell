/*
 * executor.c  --  Command execution with pipes, redirects (Part C)
 *
 * Handles:
 *   - External command execution (fork + execvp)
 *   - Input redirection (<)
 *   - Output redirection (>, >>)
 *   - Command piping (|)
 *   - Combined redirects + pipes
 *
 * Builtin commands (hop, reveal, log) are still handled but run in child
 * processes when redirects or pipes are involved.
 */
#include "shell.h"
#include "executor.h"
#include <sys/wait.h>
#include <fcntl.h>

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
 *  run_atomic_in_child
 *
 *  Called AFTER fork().  Applies file redirects (pipe redirects
 *  already set up by caller), then runs the command (builtin or
 *  external).  Never returns — calls _exit() when done.
 * ────────────────────────────────────────────────────────── */
static void run_atomic_in_child(const AtomicCmd *a)
{
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

    /* Only the last redirect of each type takes effect */
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

    /* If we get here, exec failed */
    fprintf(stderr, "Command not found!\n");
    _exit(1);
}

/* ──────────────────────────────────────────────────────────
 *  exec_cmd_group  --  Execute a command group (pipeline)
 *
 *  Handles pipes, file redirects, builtins, external commands.
 *  Returns the exit status of the last command in the pipeline.
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
        if (pid == 0)
            run_atomic_in_child(a);  /* never returns */

        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    /* ── Pipeline (2+ commands) ─────────────────────── */
    int   pipes[n - 1][2];
    pid_t pids[n];

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
            /* Wait for already-forked children */
            for (int k = 0; k < i; k++) {
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
            /* ── CHILD ───────────────────────────────── */

            /* Wire up pipe input (read from previous command) */
            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);

            /* Wire up pipe output (write to next command) */
            if (i < n - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            /* Close ALL pipe fds in the child */
            for (int j = 0; j < n - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            /* Apply file redirects and run command */
            run_atomic_in_child(a);
            /* never reaches here */
        }
    }

    /* ── PARENT ──────────────────────────────────────── */
    /* Close all pipe fds — the parent doesn't need them */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for all children in order */
    int ret = 0;
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == n - 1)
            ret = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    return ret;
}
