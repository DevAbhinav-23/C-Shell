/*
 * builtin_ping.c  --  ping command (Part E.2)
 *
 * Syntax: ping <signal_number> <pid>
 *
 * Sends signal_number % 32 to the specified process.
 * On success: "Sent signal signal_number to process with pid <pid>"
 * On failure: "No such process found"
 * Signal number < 0 or > 31: "Invalid signal: <signal>"
 */
#include "shell.h"
#include <signal.h>

int builtin_ping(char **args, int arg_count)
{
    /* Must have exactly 3 args: ping, signal_number, pid */
    if (arg_count != 3) {
        fprintf(stderr, "Invalid Syntax!\n");
        return 1;
    }

    /* Parse signal_number */
    char *endptr;
    long sig_val = strtol(args[1], &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "Invalid signal: %s\n", args[1]);
        return 1;
    }

    /* Validate signal range */
    if (sig_val < 0 || sig_val > 31) {
        fprintf(stderr, "Invalid signal: %s\n", args[1]);
        return 1;
    }

    /* Parse pid */
    long pid_val = strtol(args[2], &endptr, 10);
    if (*endptr != '\0' || pid_val <= 0) {
        fprintf(stderr, "Invalid Syntax!\n");
        return 1;
    }

    int actual_signal = (int)(sig_val % 32);
    if (actual_signal < 0)
        actual_signal += 32;

    pid_t target_pid = (pid_t)pid_val;

    /* Send the signal */
    if (kill(target_pid, actual_signal) != 0) {
        fprintf(stderr, "No such process found\n");
        return 1;
    }

    printf("Sent signal %ld to process with pid %ld\n", sig_val, pid_val);
    return 0;
}
