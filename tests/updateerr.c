/* updateerr.c - run aept_update() against a config and report what it
 * classified the outcome as
 *
 * A harness, not a test: tests/test_update_error.sh drives it.  The CLI
 * folds aept_last_error() into an exit status and a log line, so a shell
 * test cannot read the classification itself; this prints it.
 *
 * Copyright (C) 2026 Tobias Koch
 * SPDX-License-Identifier: MIT
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include "aept/aept.h"

static void quiet_log(int level, const char *msg, void *userdata)
{
    (void)userdata;
    /* Errors are the point; let them through so the test can show them. */
    if (level <= 1)
        fprintf(stderr, "%s\n", msg);
}

int main(int argc, char *argv[])
{
    aept_ctx_t *ctx;
    int rc;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <aept.conf>\n", argv[0]);
        return 2;
    }

    ctx = aept_init();
    if (!ctx) {
        fprintf(stderr, "aept_init failed\n");
        return 2;
    }
    aept_set_log_fn(ctx, quiet_log, NULL);

    if (aept_load_config(ctx, argv[1]) != 0) {
        fprintf(stderr, "cannot load %s\n", argv[1]);
        aept_cleanup(ctx);
        return 2;
    }

    rc = aept_update(ctx);
    printf("returned %d error %d\n", rc, aept_last_error(ctx));
    fflush(stdout);

    aept_cleanup(ctx);
    return rc == 0 ? 0 : 1;
}
