/*
 * builtin_reveal.c  --  reveal command (B.2)
 *
 * Syntax: reveal (-(a | l)*)* (~ | . | .. | - | name)?
 *
 * Lists directory contents with optional flags:
 *   -a : show hidden files (starting with .)
 *   -l : one entry per line
 *
 * The directory argument is resolved like hop (but does not change CWD).
 */
#include "shell.h"
#include <dirent.h>
#include <sys/stat.h>

/* ──────────────────────────────────────────────────────────
 *  Dynamic string array for directory entries
 * ────────────────────────────────────────────────────────── */
typedef struct {
    char **items;
    int    count;
    int    capacity;
} EntryList;

static void el_init(EntryList *el)
{
    el->capacity = 0;
    el->count    = 0;
    el->items    = NULL;
}

static void el_free(EntryList *el)
{
    for (int i = 0; i < el->count; i++)
        free(el->items[i]);
    free(el->items);
}

static void el_push(EntryList *el, const char *name)
{
    if (el->count >= el->capacity) {
        el->capacity = el->capacity ? el->capacity * 2 : 32;
        el->items = realloc(el->items, sizeof(char *) * (size_t)el->capacity);
    }
    el->items[el->count++] = shell_strdup(name);
}

/* ──────────────────────────────────────────────────────────
 *  Bubble sort (lexicographic by ASCII values)
 * ────────────────────────────────────────────────────────── */
static void el_sort(EntryList *el)
{
    for (int i = 0; i < el->count - 1; i++) {
        for (int j = 0; j < el->count - 1 - i; j++) {
            if (strcmp(el->items[j], el->items[j + 1]) > 0) {
                char *tmp   = el->items[j];
                el->items[j]     = el->items[j + 1];
                el->items[j + 1] = tmp;
            }
        }
    }
}

/* ──────────────────────────────────────────────────────────
 *  Collect directory entries
 * ────────────────────────────────────────────────────────── */
static int collect_entries(const char *dirpath, int show_hidden, EntryList *out)
{
    DIR *dir = opendir(dirpath);
    if (!dir)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        /* Skip hidden files unless -a is set */
        if (!show_hidden && name[0] == '.')
            continue;
        el_push(out, name);
    }
    closedir(dir);
    el_sort(out);
    return 0;
}

/* ──────────────────────────────────────────────────────────
 *  Print entries
 * ────────────────────────────────────────────────────────── */
static void print_entries(const EntryList *el, int line_mode)
{
    if (line_mode) {
        /* One per line */
        for (int i = 0; i < el->count; i++)
            printf("%s\n", el->items[i]);
    } else {
        /* ls-style: space-separated on one line */
        for (int i = 0; i < el->count; i++) {
            if (i > 0) printf(" ");
            printf("%s", el->items[i]);
        }
        printf("\n");
    }
}

/* ──────────────────────────────────────────────────────────
 *  builtin_reveal
 * ────────────────────────────────────────────────────────── */
int builtin_reveal(char **args, int arg_count)
{
    int show_hidden = 0;   /* -a flag */
    int line_mode   = 0;   /* -l flag */
    const char *target = NULL;

    /* Parse arguments: flags first, then optional directory */
    for (int i = 1; i < arg_count; i++) {
        const char *arg = args[i];
        if (arg[0] == '-') {
            /* It's a flag string: could be -a, -l, -la, -al, etc. */
            int j = 1;
            if (arg[j] == '\0') {
                /* Bare "-" means previous CWD */
                target = "-";
                continue;
            }
            while (arg[j]) {
                if (arg[j] == 'a')
                    show_hidden = 1;
                else if (arg[j] == 'l')
                    line_mode = 1;
                else {
                    fprintf(stderr, "reveal: Invalid Syntax!\n");
                    return 1;
                }
                j++;
            }
        } else {
            /* It's the directory argument */
            if (target != NULL) {
                fprintf(stderr, "reveal: Invalid Syntax!\n");
                return 1;
            }
            target = arg;
        }
    }

    /* Resolve the target directory path */
    char resolved[PATH_MAX];
    if (target == NULL) {
        /* No argument: current directory */
        if (getcwd(resolved, sizeof(resolved)) == NULL) {
            fprintf(stderr, "No such directory!\n");
            return 1;
        }
    } else {
        if (resolve_path(target, resolved, sizeof(resolved)) != 0) {
            fprintf(stderr, "No such directory!\n");
            return 1;
        }
    }

    /* Collect and print entries */
    EntryList entries;
    el_init(&entries);
    if (collect_entries(resolved, show_hidden, &entries) != 0) {
        fprintf(stderr, "No such directory!\n");
        el_free(&entries);
        return 1;
    }

    print_entries(&entries, line_mode);
    el_free(&entries);
    return 0;
}
