# ──────────────────────────────────────────────────────────────────────
#  Makefile for C-Shell  (Parts A + B)
#
#  Targets:
#    make all      – builds the shell binary (shell.out)
#    make test     – builds the test harness  (test.out)
#    make clean    – removes build artifacts
#    make run      – builds and runs the shell
#    make check    – builds and runs the self-tests
# ──────────────────────────────────────────────────────────────────────

CC       = gcc
CFLAGS   = -std=c99 \
           -D_POSIX_C_SOURCE=200809L \
           -D_XOPEN_SOURCE=700 \
           -Wall -Wextra -Werror \
           -Wno-unused-parameter \
           -fno-asm \
           -g

# Source files for the shell
SRCS     = main.c prompt.c input.c lexer.c parser.c utils.c \
           builtin_hop.c builtin_reveal.c builtin_log.c
HDRS     = shell.h prompt.h input.h lexer.h parser.h \
           builtin_hop.h builtin_reveal.h builtin_log.h
OBJS     = $(SRCS:.c=.o)

# ── Default target ──────────────────────────────────────────────────
.PHONY: all
all: shell.out

shell.out: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Test target ────────────────────────────────────────────────────
.PHONY: test
test: test.out

test.out: test_wrapper.c $(filter-out main.c, $(SRCS)) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_wrapper.c $(filter-out main.c, $(SRCS))

# ── Convenience targets ────────────────────────────────────────────
.PHONY: run check clean

run: all
	./shell.out

check: test.out
	./test.out selftest

clean:
	rm -f shell.out test.out $(OBJS)
