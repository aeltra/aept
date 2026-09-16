/* test_verify.c - aept_verify_signature()'s own failure paths
 *
 * The yes/no of verification is held down by the shell tests; what was
 * dark is verify.c failing *itself* -- fork and waitpid.  Both are
 * forced deterministically: fork through the linker (--wrap=fork, the
 * way test_iofail fails syscalls), waitpid by letting SIGCHLD auto-reap
 * the child first.  Not RLIMIT_NPROC, which the first version used:
 * root is exempt from it, so a suite run as root -- the package build
 * is one -- forked anyway, took the other path, and the two lines of
 * the fork-failure branch went unreached on that machine alone.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/util.h"
#include "aept/verify.h"

#include "test.h"

static struct aept_ctx ctx;

/* --wrap=fork: the next fork() fails when armed. */
static int fork_fails;
pid_t __real_fork(void);
pid_t __wrap_fork(void)
{
    if (fork_fails) {
        errno = EAGAIN;
        return -1;
    }
    return __real_fork();
}

static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

int main(void)
{
    silence_logging();
    ctx.config.usign_keydir = (char *)"/nonexistent";

    /* ── fork failing is an error, not a hang or a crash ──────────── */

    fork_fails = 1;
    test_int_eq(aept_verify_signature(&ctx, "/no/file", "/no/sig"), -1,
                "an unforkable verify reports -1");
    fork_fails = 0;

    /* ── waitpid failing is an error too ──────────────────────────── *
     *
     * With SIGCHLD ignored the kernel auto-reaps the child, so by the
     * time waitpid() asks, there is no child to wait for: ECHILD.
     * Whether usign exists is irrelevant -- exec succeeding or the
     * child dying at exec both end in a reaped child.
     */

    signal(SIGCHLD, SIG_IGN);
    test_int_eq(aept_verify_signature(&ctx, "/no/file", "/no/sig"), -1,
                "an unreapable child reports -1");
    signal(SIGCHLD, SIG_DFL);

    return test_summary();
}
