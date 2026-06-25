/*
 * signals.c  --  Signal handlers for Ctrl-C, Ctrl-Z (Part E.3)
 */
#include "shell.h"
#include "signals.h"
#include <signal.h>
#include <sys/wait.h>

/*
 * Global volatile flag: set by SIGCHLD handler, checked in REPL loop.
 */
volatile sig_atomic_t g_sigchld_received = 0;

/* ──────────────────────────────────────────────────────────
 *  SIGINT handler (Ctrl-C)
 *
 *  Forwards SIGINT to the foreground child process group.
 *  Does NOT terminate the shell itself.
 * ────────────────────────────────────────────────────────── */
static void sigint_handler(int sig)
{
    (void)sig;
    pid_t fg = g_shell.foreground_pgid;
    if (fg > 0) {
        kill(-fg, SIGINT);   /* Send to entire process group */
    }
}

/* ──────────────────────────────────────────────────────────
 *  SIGTSTP handler (Ctrl-Z)
 *
 *  Forwards SIGTSTP to the foreground child process group.
 *  The shell itself does not stop.
 *  The caller will later move the stopped process to the bg job list.
 * ────────────────────────────────────────────────────────── */
static void sigtstp_handler(int sig)
{
    (void)sig;
    pid_t fg = g_shell.foreground_pgid;
    if (fg > 0) {
        kill(-fg, SIGTSTP);  /* Send to entire process group */
    }
}

/* ──────────────────────────────────────────────────────────
 *  SIGCHLD handler
 *
 *  Sets a flag so the REPL loop knows to call check_background_jobs().
 * ────────────────────────────────────────────────────────── */
static void sigchld_handler(int sig)
{
    (void)sig;
    g_sigchld_received = 1;
}

/* ──────────────────────────────────────────────────────────
 *  Signal initialisation — install all handlers
 * ────────────────────────────────────────────────────────── */
void signal_init(void)
{
    struct sigaction sa;

    /* ── SIGINT: ignore in shell, forwarded to fg child ── */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sa.sa_flags   = SA_RESTART;   /* restart interrupted syscalls */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* ── SIGTSTP: ignore in shell, forwarded to fg child ── */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigtstp_handler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTSTP, &sa, NULL);

    /* ── SIGCHLD: reap children ── */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;  /* only on termination */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore SIGQUIT (Ctrl-\) */
    signal(SIGQUIT, SIG_IGN);
}
