/* stallclient.c - drive aept_download() against an awkward peer
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 *
 * Usage: stallclient [-r] [-n] [-t SECS] [-o SECS] <url> <outfile>
 *
 *   -r        install the SIGINT handler with SA_RESTART
 *   -n        also handle SIGUSR1, with a handler that does nothing
 *   -t SECS   network timeout for the context doing the download
 *             (default 0, meaning wait indefinitely)
 *   -o SECS   before downloading, create a *second* context and give it
 *             this timeout -- see below
 *
 * Prints "ready", then downloads <url>, then "signals <n>" and
 * "returned <rc> error <err>", where <err> is aept_last_error().  The
 * scripts poll for that last line rather than watching the process
 * table: an exited-but-unreaped child still answers kill -0, so process
 * death is not a reliable signal from a shell script.
 *
 * -r exists because the kernel restarts an interrupted read(2) when the
 * handler carries SA_RESTART -- which is what glibc's signal(3) gives a
 * C embedder by default, and what Python's signal.siginterrupt(sig,
 * False) turns on.  Only a wait that happens in poll(2) survives it.
 *
 * -n is the opposite case, and the one an embedding application brings
 * with it: a signal aept knows nothing about, handled by the host
 * program for its own reasons -- a SIGWINCH, a SIGCHLD -- and meaning
 * nothing about the transfer.  It interrupts the wait all the same,
 * because poll(2) is never restarted, and a download that dies of it is
 * a download lost to somebody else's window being resized.  The count
 * is printed so a test can tell "no signal arrived" from "a signal
 * arrived and was survived".
 *
 * -o exists to pin the timeout down as per-context state.  The second
 * context is configured *after* the first, so if the setting were a
 * process-wide global -- as it was before -- the second value would
 * overwrite the first and the download would use the wrong one.
 *
 * This is not a test: it is the harness those scripts drive.
 */

#include <config.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aept/aept.h"
#include "aept/download.h"

static aept_ctx_t *g_ctx;
static volatile sig_atomic_t g_signals;

/* Async-signal-safe: aept_cancel() is documented as callable from a
 * signal handler, and nothing else happens here. */
static void on_sigint(int sig)
{
    (void)sig;
    if (g_ctx)
        aept_cancel(g_ctx);
}

/* Deliberately does not cancel: this is the host application's own
 * signal, and the transfer is none of its business. */
static void on_noise(int sig)
{
    (void)sig;
    g_signals++;
}

static aept_ctx_t *new_ctx(int timeout)
{
    aept_ctx_t *ctx = aept_init();

    if (!ctx) {
        fprintf(stderr, "aept_init failed\n");
        exit(2);
    }
    /* Failures are expected; the caller reads the printed result. */
    aept_set_verbosity(ctx, AEPT_LOG_ERROR - 1);
    aept_set_network_timeout(ctx, timeout);
    return ctx;
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    aept_ctx_t *other = NULL;
    int restart = 0;
    int noise = 0;
    int timeout = 0;
    int other_timeout = -1;
    int i = 1;
    int rc;

    for (; i < argc && argv[i][0] == '-'; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            restart = 1;
        } else if (strcmp(argv[i], "-n") == 0) {
            noise = 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            timeout = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            other_timeout = atoi(argv[++i]);
        } else {
            fprintf(stderr, "unknown option '%s'\n", argv[i]);
            return 2;
        }
    }

    if (argc - i != 2) {
        fprintf(stderr, "usage: %s [-r] [-n] [-t SECS] [-o SECS] <url> <outfile>\n", argv[0]);
        return 2;
    }

    g_ctx = new_ctx(timeout);
    if (other_timeout >= 0)
        other = new_ctx(other_timeout);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = restart ? SA_RESTART : 0;
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        fprintf(stderr, "sigaction failed\n");
        return 2;
    }

    if (noise) {
        /* No SA_RESTART, though it would make no difference: the waits
         * are polls, and poll(2) is not restarted either way. */
        sa.sa_handler = on_noise;
        sa.sa_flags = 0;
        if (sigaction(SIGUSR1, &sa, NULL) != 0) {
            fprintf(stderr, "sigaction failed\n");
            return 2;
        }
    }

    printf("ready\n");
    fflush(stdout);

    rc = aept_download(g_ctx, argv[i], argv[i + 1], "stalled");

    printf("signals %d\n", (int)g_signals);
    printf("returned %d error %d\n", rc, aept_last_error(g_ctx));
    fflush(stdout);

    aept_cleanup(g_ctx);
    if (other)
        aept_cleanup(other);
    return rc == 0 ? 0 : 1;
}
