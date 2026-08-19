/* test_verify.c - aept_verify_signature()'s own failure paths
 *
 * The yes/no of verification is held down by the shell tests; what was
 * dark is verify.c failing *itself* -- fork and waitpid.  Both are
 * forced deterministically: fork by exhausting RLIMIT_NPROC, waitpid
 * by letting SIGCHLD auto-reap the child first.  (The child's own two
 * lines are invisible to gcov by construction and stay dark.)
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "aept/internal.h"
#include "aept/msg.h"
#include "aept/util.h"
#include "aept/verify.h"

#include "test.h"

static struct aept_ctx ctx;

static void silence_logging(void)
{
    ctx.config.verbosity = AEPT_LOG_ERROR - 1;
    aept_log_set_ctx(&ctx);
}

int main(void)
{
    struct rlimit old, zero;

    silence_logging();
    ctx.config.usign_keydir = (char *)"/nonexistent";

    /* ── fork failing is an error, not a hang or a crash ──────────── */

    if (getrlimit(RLIMIT_NPROC, &old) != 0) {
        perror("getrlimit");
        return 2;
    }
    zero = old;
    zero.rlim_cur = 1; /* this process already exceeds it */
    if (setrlimit(RLIMIT_NPROC, &zero) != 0) {
        perror("setrlimit");
        return 2;
    }

    test_int_eq(aept_verify_signature(&ctx, "/no/file", "/no/sig"), -1,
                "an unforkable verify reports -1");

    if (setrlimit(RLIMIT_NPROC, &old) != 0) {
        perror("setrlimit restore");
        return 2;
    }

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
