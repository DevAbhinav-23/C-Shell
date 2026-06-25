#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <signal.h>

/* ──────────────────────────────────────────────────────────
 *  Constants
 * ────────────────────────────────────────────────────────── */
#define SHELL_BUF_SIZE  4096
#define SHELL_MAX_ARGS  256
#define SHELL_MAX_CMDS  64
#define SHELL_MAX_REDIR 16
#define SHELL_MAX_HISTORY 15
#define SHELL_MAX_BG_JOBS 64

/* ──────────────────────────────────────────────────────────
 *  Token Types  (used by the lexer and parser)
 * ────────────────────────────────────────────────────────── */
typedef enum {
    TOK_WORD,           /* generic word / name                         */
    TOK_PIPE,           /* |                                          */
    TOK_AND,            /* &                                          */
    TOK_SEMICOLON,      /* ;                                          */
    TOK_INPUT,          /* <                                          */
    TOK_OUTPUT,         /* >                                          */
    TOK_OUTPUT_APPEND,  /* >>                                         */
    TOK_EOF             /* end-of-input                               */
} TokenType;

typedef struct {
    TokenType type;
    char      *value;   /* heap-allocated copy of the token text      */
} Token;

/* ──────────────────────────────────────────────────────────
 *  Redirect descriptor  (attached to an atomic command)
 * ────────────────────────────────────────────────────────── */
typedef enum {
    REDIR_INPUT,
    REDIR_OUTPUT,
    REDIR_OUTPUT_APPEND
} RedirType;

typedef struct {
    char     *filename;
    RedirType type;
} Redirect;

/* ──────────────────────────────────────────────────────────
 *  Atomic command  (single command in a pipeline)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char      *name;            /* command name (first word)           */
    char     **args;            /* argument words (including name[0])  */
    int        arg_count;
    Redirect  *redirects;
    int        redirect_count;
} AtomicCmd;

/* ──────────────────────────────────────────────────────────
 *  Command group  (a pipeline of atomic commands)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    AtomicCmd *commands;
    int        command_count;
} CmdGroup;

/* ──────────────────────────────────────────────────────────
 *  Shell command  (full parsed representation)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    CmdGroup *groups;
    int        group_count;
    int       *separators;     /* 0 = ';', 1 = '&' (length = group_count - 1) */
    int        trailing_amp;   /* 1 if input ended with trailing &    */
} ShellCmd;

/* ──────────────────────────────────────────────────────────
 *  History entry (for log command, Part B.3)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char *cmd_line;            /* the raw command string               */
} HistoryEntry;

/* ──────────────────────────────────────────────────────────
 *  Job state (Part E)
 * ────────────────────────────────────────────────────────── */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED
} JobState;

/* ──────────────────────────────────────────────────────────
 *  Background / stopped job tracking  (Parts D + E)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    pid_t     pid;              /* representative PID                  */
    pid_t     pgid;             /* process group ID                    */
    char      cmd_name[256];    /* first command name                  */
    char      full_cmd[1024];   /* full reconstructed command string   */
    int       job_id;           /* job number (display / fg / bg)      */
    JobState  state;            /* Running or Stopped                  */
    int       is_bg;            /* 1 if background, 0 if stopped-fg    */
} BgJob;

/* ──────────────────────────────────────────────────────────
 *  Global shell state
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char home_dir[PATH_MAX];   /* directory where the shell was started */
    char prev_cwd[PATH_MAX];   /* previous CWD (for hop -)             */
    int  has_prev_cwd;         /* 1 if prev_cwd is valid               */
    char history_file[PATH_MAX]; /* path to persistent history file    */
    HistoryEntry history[SHELL_MAX_HISTORY];
    int  history_count;        /* number of entries in the ring buffer  */
    int  history_head;         /* index of the newest entry             */
    int  history_full;         /* 1 if the ring buffer has wrapped      */
    BgJob bg_jobs[SHELL_MAX_BG_JOBS];
    int   bg_job_count;
    int   bg_next_job_id;
    volatile pid_t foreground_pgid;  /* PID/PGID of foreground process (Part E) */
} ShellState;

extern ShellState g_shell;
extern volatile sig_atomic_t g_sigchld_received;

/* ──────────────────────────────────────────────────────────
 *  Utility helpers  (utils.c)
 * ────────────────────────────────────────────────────────── */
char *shell_strdup(const char *s);
int   resolve_path(const char *arg, char *out, size_t out_sz);

/* ──────────────────────────────────────────────────────────
 *  Prompt  (prompt.c)
 * ────────────────────────────────────────────────────────── */
void prompt_print(void);

/* ──────────────────────────────────────────────────────────
 *  Input  (input.c)
 * ────────────────────────────────────────────────────────── */
char *input_read_line(void);

/* ──────────────────────────────────────────────────────────
 *  Lexer  (lexer.c)
 * ────────────────────────────────────────────────────────── */
Token *lexer_tokenize(const char *input, int *out_count);
void   lexer_free_tokens(Token *tokens, int count);

/* ──────────────────────────────────────────────────────────
 *  Parser  (parser.c)
 * ────────────────────────────────────────────────────────── */
ShellCmd *parser_parse(Token *tokens, int token_count, int *out_valid);
void      parser_free_cmd(ShellCmd *cmd);

/* ──────────────────────────────────────────────────────────
 *  Executor  (executor.c)
 * ────────────────────────────────────────────────────────── */
int  exec_cmd_group(const CmdGroup *g);
int  exec_cmd_group_bg(const CmdGroup *g);
void check_background_jobs(void);
void kill_all_children(void);

/* Job management helpers (executor.c) */
int    add_job(pid_t pid, pid_t pgid, const char *cmd_name,
               const char *full_cmd, JobState state, int is_bg);
void   remove_job_by_pid(pid_t pid);
BgJob *find_job_by_id(int job_id);
BgJob *find_job_by_pgid(pid_t pgid);
void   cleanup_done_jobs(void);
void   reconstruct_cmd(const CmdGroup *g, char *buf, size_t bufsz);

/* ──────────────────────────────────────────────────────────
 *  Signals  (signals.c)
 * ────────────────────────────────────────────────────────── */
void signal_init(void);

/* ──────────────────────────────────────────────────────────
 *  Builtin: hop  (builtin_hop.c)
 * ────────────────────────────────────────────────────────── */
int builtin_hop(char **args, int arg_count);

/* ──────────────────────────────────────────────────────────
 *  Builtin: reveal  (builtin_reveal.c)
 * ────────────────────────────────────────────────────────── */
int builtin_reveal(char **args, int arg_count);

/* ──────────────────────────────────────────────────────────
 *  Builtin: log  (builtin_log.c)
 * ────────────────────────────────────────────────────────── */
int  builtin_log(char **args, int arg_count);
void log_init(void);
void log_add(const char *cmd);
void log_free(void);

/* ──────────────────────────────────────────────────────────
 *  Builtin: activities  (builtin_activities.c)  — Part E.1
 * ────────────────────────────────────────────────────────── */
int builtin_activities(void);

/* ──────────────────────────────────────────────────────────
 *  Builtin: ping  (builtin_ping.c)  — Part E.2
 * ────────────────────────────────────────────────────────── */
int builtin_ping(char **args, int arg_count);

/* ──────────────────────────────────────────────────────────
 *  Builtin: fg / bg  (builtin_fg_bg.c)  — Part E.4
 * ────────────────────────────────────────────────────────── */
int builtin_fg(char **args, int arg_count);
int builtin_bg(char **args, int arg_count);

#endif /* SHELL_H */
